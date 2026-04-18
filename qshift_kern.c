#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ETH_P_IP    0x0800
#define IPPROTO_TCP 6
#define TLS_HANDSHAKE_TYPE 0x16

// THE BRIDGE: This creates the shared memory map between Kernel and User-Space
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64); // Supports up to 64 CPU network queues
    __type(key, int);
    __type(value, int);
} xsks_map SEC(".maps");

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

    if (ip->id == bpf_htons(0x7777)) {
        return XDP_PASS; 
    }

    uint32_t ip_hdr_len = ip->ihl * 4;
    if (ip_hdr_len < sizeof(struct iphdr)) return XDP_PASS;

    struct tcphdr *tcp = (void *)ip + ip_hdr_len;
    if ((void *)(tcp + 1) > data_end) return XDP_PASS;

    // 2. Find the Payload
    uint32_t tcp_hdr_len = tcp->doff * 4;
    void *payload = (void *)tcp + tcp_hdr_len;
    if (payload + 1 > data_end) return XDP_PASS;

    // 3. The Quantum Intercept
    uint8_t *tls_type = payload;
    if (*tls_type == TLS_HANDSHAKE_TYPE) {
        bpf_printk("[Q-Shift] TLS ClientHello intercepted! Rerouting to User-Space...\n");
        
        // INSTANT REDIRECT: Shunt the packet up the AF_XDP socket map
        // If the socket isn't ready, it falls back to XDP_PASS and lets it go normally.
        return bpf_redirect_map(&xsks_map, 0, XDP_PASS);
    }

    // All normal traffic (like standard web browsing) passes completely untouched
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
