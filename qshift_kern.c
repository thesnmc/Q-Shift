#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ETH_P_IP    0x0800
#define IPPROTO_TCP 6
#define TLS_HANDSHAKE_TYPE 0x16

// Define the maximum size for a saved packet payload (to fit inside the BPF map value)
#define MAX_PAYLOAD_SIZE 2000

// THE BRIDGE: AF_XDP Shared Memory Map
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

// THE STATE TRACKER: LRU Hash Map for TCP Retransmissions
// Key: TCP Sequence Number (uint32_t)
// Value: The forged Quantum Payload bytes + length metadata
struct saved_payload {
    uint32_t payload_len;
    uint8_t data[MAX_PAYLOAD_SIZE];
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 10000); // Hold up to 10,000 active handshakes
    __type(key, uint32_t);
    __type(value, struct saved_payload);
} tcp_state_map SEC(".maps");

// Helper function for raw 1s-complement checksum calculation (RFC 1071)
static __always_inline uint16_t csum_fold_helper(uint32_t csum) {
    csum = (csum & 0xffff) + (csum >> 16);
    csum = (csum & 0xffff) + (csum >> 16);
    return (uint16_t)~csum;
}

SEC("xdp")
int qshift_xdp_hook(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    // 1. Parse Headers (Ethernet -> IP -> TCP)
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) return XDP_PASS;
    if (eth->h_proto != bpf_htons(ETH_P_IP)) return XDP_PASS;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) return XDP_PASS;
    if (ip->protocol != IPPROTO_TCP) return XDP_PASS;

    // Prevent loopback broadcast storms
    if (ip->id == bpf_htons(0x7777)) {
        return XDP_PASS;
    }

    uint32_t ip_hdr_len = ip->ihl * 4;
    if (ip_hdr_len < sizeof(struct iphdr)) return XDP_PASS;

    struct tcphdr *tcp = (void *)ip + ip_hdr_len;
    if ((void *)(tcp + 1) > data_end) return XDP_PASS;

    // 2. Identify the TLS Payload
    uint32_t tcp_hdr_len = tcp->doff * 4;
    void *payload = (void *)tcp + tcp_hdr_len;
    if (payload + 1 > data_end) return XDP_PASS;

    uint8_t *tls_type = payload;

    // 3. The Intercept & State Check
    if (*tls_type == TLS_HANDSHAKE_TYPE) {

        // Grab the Sequence Number as our unique identifier
        uint32_t seq_num = bpf_ntohl(tcp->seq);

        // CHECK STATE: Is this a Retransmission?
        struct saved_payload *saved = bpf_map_lookup_elem(&tcp_state_map, &seq_num);

        if (saved != NULL) {
            bpf_printk("[Q-Shift] TCP Retransmission Detected (Seq: %u). Executing Kernel Fast-Path.\n", seq_num);

            // Verify the saved payload isn't corrupted and fits in our buffer
            if (saved->payload_len > 0 && saved->payload_len <= MAX_PAYLOAD_SIZE) {

                // Calculate the exact size difference needed
                int current_payload_len = bpf_ntohs(ip->tot_len) - ip_hdr_len - tcp_hdr_len;
                int size_diff = saved->payload_len - current_payload_len;

                // Adjust the tail of the packet to make room for the saved quantum payload
                if (bpf_xdp_adjust_tail(ctx, size_diff)) {
                    bpf_printk("[FATAL] Kernel Fast-Path Tail Adjustment Failed.\n");
                    return XDP_PASS;
                }

                // Re-evaluate pointers after tail adjustment!
                data = (void *)(long)ctx->data;
                data_end = (void *)(long)ctx->data_end;

                // We have to re-parse the headers because memory addresses changed
                eth = data;
                if ((void *)(eth + 1) > data_end) return XDP_PASS;
                ip = (void *)(eth + 1);
                if ((void *)(ip + 1) > data_end) return XDP_PASS;
                ip_hdr_len = ip->ihl * 4;
                tcp = (void *)ip + ip_hdr_len;
                if ((void *)(tcp + 1) > data_end) return XDP_PASS;
                tcp_hdr_len = tcp->doff * 4;
                payload = (void *)tcp + tcp_hdr_len;

                // THE VERIFIER BYPASS PATCH
                // Inject the saved Quantum Payload directly in Ring-0
                // We use a bounded loop with strict per-byte Verifier checks
                uint8_t *dst = payload;
                #pragma unroll
                for (int i = 0; i < MAX_PAYLOAD_SIZE; i++) {
                    if (i >= saved->payload_len) break;
                    
                    // The kernel mathematically accepts this explicit bounds check
                    if ((void *)(dst + 1) > data_end) break; 
                    
                    *dst = saved->data[i];
                    dst++;
                }

                // Update IP Headers for the new size
                ip->tot_len = bpf_htons(ip_hdr_len + tcp_hdr_len + saved->payload_len);
                ip->id = bpf_htons(0x7777); // Mark it so we don't intercept it again

                // NOTE: In a full production build, we would execute incremental checksum
                // updates here. For the prototype, we let the NIC hardware offload handle it
                // or let User-Space catch it.

                bpf_printk("[SUCCESS] Kernel Fast-Path Executed. Firing XDP_TX.\n");
                return XDP_TX; // Instantly bounce the packet back to the network
            }
        }

        // SLOW PATH: First time seeing this packet. Send to User-Space for the Quantum Forge.
        bpf_printk("[Q-Shift] New TLS ClientHello. Rerouting to User-Space (AF_XDP)...\n");
        return bpf_redirect_map(&xsks_map, 0, XDP_PASS);
    }

    // Normal traffic passes untouched
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
