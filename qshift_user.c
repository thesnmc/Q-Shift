#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>
#include <poll.h>
#include <xdp/xsk.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <oqs/oqs.h>

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define ML_KEM_KEY_SIZE 1184
#define MAX_PAYLOAD_SIZE 2000

static volatile int keep_running = 1;
int ifindex;

// Global buffer for our Pre-Fetched Quantum Key
uint8_t quantum_key_buffer[ML_KEM_KEY_SIZE];

// THE STATE TRACKER STRUCT (Must match the Kernel)
struct saved_payload {
    uint32_t payload_len;
    uint8_t data[MAX_PAYLOAD_SIZE];
};

// --- THE DYNAMIC TLS POINTER JUMPER ---
int locate_tls_key_share(unsigned char *payload, int payload_size) {
    if (payload_size < 43) return -1; 
    if (payload[0] != 0x16 || payload[5] != 0x01) return -1;

    int p = 43; 
    if (p >= payload_size) return -1; 
    uint8_t session_id_len = payload[p];
    p += 1 + session_id_len;

    if (p + 2 > payload_size) return -1;
    uint16_t cipher_suites_len = (payload[p] << 8) | payload[p+1];
    p += 2 + cipher_suites_len;

    if (p >= payload_size) return -1;
    uint8_t comp_methods_len = payload[p];
    p += 1 + comp_methods_len;

    if (p + 2 > payload_size) return -1;
    uint16_t total_ext_len = (payload[p] << 8) | payload[p+1];
    p += 2;

    int ext_end = p + total_ext_len;
    if (ext_end > payload_size) ext_end = payload_size; 

    // TLV HUNTER LOOP
    while (p + 4 <= ext_end) {
        uint16_t ext_type = (payload[p] << 8) | payload[p+1];
        uint16_t ext_len = (payload[p+2] << 8) | payload[p+3];
        
        if (ext_type == 0x0033) {
            return p + 4; // Return exact offset of the VALUE
        }
        p += 4 + ext_len; // Jump to next extension
    }
    return -1; 
}

// --- THE BROKER ABSTRACTION ---
void fetch_quantum_entropy() {
    printf("[ SYSTEM ] Fetching Quantum Entropy from Broker (/dev/shm/qshift_entropy.bin)...\n");
    uint8_t entropy_seed[1000];
    int use_fallback = 1;

    // Read hardware entropy from the shared memory pipe
    int fd = open("/dev/shm/qshift_entropy.bin", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, entropy_seed, sizeof(entropy_seed)) == sizeof(entropy_seed)) {
            use_fallback = 0;
            printf("[ SYSTEM ] Successfully loaded hardware entropy from Broker.\n");
        }
        close(fd);
    }

    if (use_fallback) {
        printf("[ WARNING ] Broker offline or missing data. Using local PRNG fallback...\n");
        for(int i = 0; i < 1000; i++) entropy_seed[i] = rand() % 256;
    }

    // THE TRUE FIPS 203 QUANTUM FORGE
    printf("[ SYSTEM ] Forging NIST FIPS 203 ML-KEM-768 Keypair...\n");
    OQS_init();
    OQS_KEM *kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_768);

    if (kem == NULL) {
        printf("[ FATAL ] Failed to initialize ML-KEM-768 algorithm.\n");
        exit(1);
    }

    uint8_t public_key[OQS_KEM_ml_kem_768_length_public_key];
    uint8_t secret_key[OQS_KEM_ml_kem_768_length_secret_key];

    // In production, the seed is injected into the DRBG here. 
    OQS_KEM_keypair(kem, public_key, secret_key);

    memcpy(quantum_key_buffer, public_key, ML_KEM_KEY_SIZE);
    printf("[ SUCCESS ] 1,184-byte ML-KEM-768 Public Key forged and loaded into Shield!\n");

    OQS_KEM_free(kem);
    OQS_destroy();
}

void sigint_handler(int sig) {
    printf("\n[ Q-SHIFT ] Caught SIGINT. Initiating graceful teardown...\n");
    keep_running = 0;
}

int get_mtu(const char *ifname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    ioctl(sock, SIOCGIFMTU, &ifr);
    close(sock);
    return ifr.ifr_mtu;
}

uint16_t forge_checksum(uint16_t *data, int len) {
    uint32_t sum = 0;
    while (len > 1) { sum += *data++; len -= 2; }
    if (len > 0) sum += *(uint8_t *)data;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return ~sum;
}

int main(int argc, char **argv) {
    char ifname[IFNAMSIZ] = "lo";
    int opt;

    while ((opt = getopt(argc, argv, "i:")) != -1) {
        if (opt == 'i') strncpy(ifname, optarg, IFNAMSIZ-1);
    }

    signal(SIGINT, sigint_handler);

    ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        printf("Error: Interface %s not found.\n", ifname);
        return 1;
    }

    int current_mtu = get_mtu(ifname);
    printf("[ SYSTEM ] Interface: %s | Hardware MTU: %d bytes\n", ifname, current_mtu);

    // PRE-FETCH
    fetch_quantum_entropy();

    struct bpf_object *obj = bpf_object__open_file("qshift_kern.o", NULL);
    bpf_object__load(obj);
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, "qshift_xdp_hook");
    bpf_xdp_attach(ifindex, bpf_program__fd(prog), XDP_FLAGS_SKB_MODE, NULL);

    // GET THE STATE MAP FD
    int state_map_fd = bpf_object__find_map_fd_by_name(obj, "tcp_state_map");

    void *umem_area;
    posix_memalign(&umem_area, getpagesize(), NUM_FRAMES * FRAME_SIZE);

    struct xsk_umem *umem;
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    xsk_umem__create(&umem, umem_area, NUM_FRAMES * FRAME_SIZE, &fq, &cq, NULL);

    struct xsk_socket *xsk;
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_socket_config cfg = { .rx_size = 2048, .tx_size = 2048, .libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD };
    xsk_socket__create(&xsk, ifname, 0, umem, &rx, &tx, &cfg);

    int map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
    int xsk_fd = xsk_socket__fd(xsk);
    int key = 0;
    bpf_map_update_elem(map_fd, &key, &xsk_fd, BPF_ANY);

    uint32_t idx;
    xsk_ring_prod__reserve(&fq, 64, &idx);
    for (int i = 0; i < 64; i++) *xsk_ring_prod__fill_addr(&fq, idx++) = i * FRAME_SIZE;
    xsk_ring_prod__submit(&fq, 64);

    printf("[ BRIDGE UP ] Q-Shift Active on %s. Waiting for TLS Handshakes...\n", ifname);

    struct pollfd fds = { .fd = xsk_fd, .events = POLLIN };

    while (keep_running) {
        int ret = poll(&fds, 1, 1000);
        if (ret <= 0) continue;

        uint32_t rx_idx;
        if (xsk_ring_cons__peek(&rx, 1, &rx_idx) > 0) {
            const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&rx, rx_idx);
            uint8_t *pkt = xsk_umem__get_data(umem_area, desc->addr);

            struct ethhdr *eth = (struct ethhdr *)pkt;
            struct iphdr *ip = (struct iphdr *)(eth + 1);
            struct tcphdr *tcp = (struct tcphdr *)((uint8_t *)ip + (ip->ihl * 4));

            uint8_t *tcp_payload = (uint8_t *)tcp + (tcp->doff * 4);
            uint32_t original_payload_len = ntohs(ip->tot_len) - (ip->ihl * 4) - (tcp->doff * 4);

            printf("\n[ +++ Q-SHIFT FORGE INITIATED +++ ]\n");

            // 1. DYNAMIC POINTER JUMP
            int target_offset = locate_tls_key_share(tcp_payload, original_payload_len);
            
            if (target_offset != -1) {
                printf("[ PARSER ] Target Locked: key_share at offset %d. Executing Injection...\n", target_offset);
                
                // Shift the original payload down to make room, then inject the quantum key
                int remaining_len = original_payload_len - target_offset;
                memmove(tcp_payload + target_offset + ML_KEM_KEY_SIZE, tcp_payload + target_offset, remaining_len);
                memcpy(tcp_payload + target_offset, quantum_key_buffer, ML_KEM_KEY_SIZE);
            } else {
                printf("[ PARSER ] Warning: No key_share found. Injecting at tail fallback...\n");
                memcpy(tcp_payload + original_payload_len, quantum_key_buffer, ML_KEM_KEY_SIZE);
            }

            uint32_t total_new_payload = original_payload_len + ML_KEM_KEY_SIZE;
            uint32_t max_tcp_payload = current_mtu - (ip->ihl * 4) - (tcp->doff * 4);

            // 2. STATE MAP UPDATE
            uint32_t seq_num = ntohl(tcp->seq);
            struct saved_payload saved = {0};
            saved.payload_len = total_new_payload > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : total_new_payload;
            memcpy(saved.data, tcp_payload, saved.payload_len);
            
            // Push the forged packet down into the Kernel LRU Hash Map
            bpf_map_update_elem(state_map_fd, &seq_num, &saved, BPF_ANY);
            printf("[ STATE ] Forged payload mapped to TCP Seq %u for Kernel Fast-Path.\n", seq_num);

            // 3. MTU CLEAVE & TRANSMIT
            if (total_new_payload <= max_tcp_payload) {
                ip->tot_len = htons((ip->ihl * 4) + (tcp->doff * 4) + total_new_payload);
                ip->id = htons(0x7777);
                ip->check = 0;
                ip->check = forge_checksum((uint16_t *)ip, ip->ihl * 4);

                printf("Forged Packet (Intact): %u payload bytes. No cleave needed.\n", total_new_payload);

                uint32_t tx_idx;
                if (xsk_ring_prod__reserve(&tx, 1, &tx_idx) >= 1) {
                    xsk_ring_prod__tx_desc(&tx, tx_idx)->addr = desc->addr;
                    xsk_ring_prod__tx_desc(&tx, tx_idx)->len = (ip->ihl * 4) + (tcp->doff * 4) + total_new_payload;
                    xsk_ring_prod__submit(&tx, 1);
                    printf("[ SUCCESS ] Intact Quantum Payload injected!\n");
                }
            } else {
                uint32_t pkt1_payload_len = max_tcp_payload;
                uint32_t pkt2_payload_len = total_new_payload - pkt1_payload_len;

                ip->tot_len = htons((ip->ihl * 4) + (tcp->doff * 4) + pkt1_payload_len);
                ip->id = htons(0x7777);
                ip->check = 0;
                ip->check = forge_checksum((uint16_t *)ip, ip->ihl * 4);

                printf("Forged Packet 1 (Vanguard): %u payload bytes.\n", pkt1_payload_len);
                printf("Forged Packet 2 (Remnant): %u payload bytes.\n", pkt2_payload_len);

                uint32_t tx_idx;
                if (xsk_ring_prod__reserve(&tx, 2, &tx_idx) >= 2) {
                    xsk_ring_prod__tx_desc(&tx, tx_idx)->addr = desc->addr;
                    xsk_ring_prod__tx_desc(&tx, tx_idx)->len = current_mtu;

                    xsk_ring_prod__tx_desc(&tx, tx_idx + 1)->addr = desc->addr + FRAME_SIZE;
                    xsk_ring_prod__tx_desc(&tx, tx_idx + 1)->len = (ip->ihl * 4) + (tcp->doff * 4) + pkt2_payload_len;

                    xsk_ring_prod__submit(&tx, 2);
                    printf("[ SUCCESS ] Segmented Quantum Payloads injected!\n");
                }
            }

            xsk_ring_cons__release(&rx, 1);
        }
    }

    printf("[ SYSTEM ] Detaching XDP Hook from %s...\n", ifname);
    bpf_xdp_detach(ifindex, XDP_FLAGS_SKB_MODE, NULL);
    xsk_socket__delete(xsk);
    xsk_umem__delete(umem);
    free(umem_area);
    printf("[ SYSTEM ] Teardown complete. Memory freed. Exiting.\n");

    return 0;
}
