/*
 * filter.bpf.c - eBPF XDP filter for ITO packet classification
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This eBPF program runs in the Linux kernel at the driver level,
 * filtering ITO packets before they reach userspace. Matching packets
 * are redirected to AF_XDP socket, others pass to normal network stack.
 *
 * This achieves line-rate performance by:
 * - Early packet filtering in kernel
 * - Avoiding unnecessary copies to userspace
 * - Leveraging XDP zero-copy RX
 */

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* ITO packet signatures */
#define ITO_SIG_LEN 7

/* Map for XDP socket redirect */
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, 64);  /* Max 64 queues */
} xsks_map SEC(".maps");

/* Map for storing interface MAC addresses */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, 6);  /* MAC address */
    __uint(max_entries, 1);
} mac_map SEC(".maps");

/*
 * Hash map for O(1) signature lookup
 * Key: 7-byte ITO signature
 * Value: signature type (1=valid, value unused but required)
 * Max 16 signatures (expandable for future)
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, ITO_SIG_LEN);
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, 16);
} sig_map SEC(".maps");

/* Statistics map */
struct xdp_stats {
    __u64 packets_total;
    __u64 packets_ito;
    __u64 packets_passed;
    __u64 packets_dropped;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(struct xdp_stats));
    __uint(max_entries, 1);
} stats_map SEC(".maps");

/*
 * Main XDP program
 *
 * Packet flow:
 * 1. Parse Ethernet header
 * 2. Check destination MAC matches interface
 * 3. Parse IPv4 header
 * 4. Check for UDP protocol
 * 5. Check for ITO signature
 * 6. If match -> XDP_REDIRECT to AF_XDP socket
 * 7. Otherwise -> XDP_PASS to normal stack
 */
SEC("xdp")
int xdp_filter_ito(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    /* Update total packet count */
    __u32 key = 0;
    struct xdp_stats *stats = bpf_map_lookup_elem(&stats_map, &key);
    if (stats) {
        __sync_fetch_and_add(&stats->packets_total, 1);
    }

    /* Parse Ethernet header */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        goto pass;
    }

    /* Check for IPv4 */
    if (eth->h_proto != bpf_htons(ETH_P_IP)) {
        goto pass;
    }

    /* Get interface MAC from map and check destination */
    __u8 *mac_addr = bpf_map_lookup_elem(&mac_map, &key);
    if (mac_addr) {
        if (bpf_memcmp(eth->h_dest, mac_addr, 6) != 0) {
            /* Not for us, pass through */
            goto pass;
        }
    }

    /* Parse IPv4 header */
    struct iphdr *iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end) {
        goto pass;
    }

    /* Verify IPv4 and header length */
    if (iph->version != 4 || iph->ihl < 5) {
        goto pass;
    }

    /* Check for UDP protocol */
    if (iph->protocol != IPPROTO_UDP) {
        goto pass;
    }

    /* Parse UDP header */
    __u32 ip_hdr_len = iph->ihl * 4;
    struct udphdr *udph = (void *)iph + ip_hdr_len;
    if ((void *)(udph + 1) > data_end) {
        goto pass;
    }

    /* Check UDP payload for ITO signature (at offset 5 - 5-byte proprietary header) */
    __u8 *payload = (void *)(udph + 1);
    __u8 *signature = payload + 5;  /* ITO signature is at offset 5 in UDP payload */
    if (signature + ITO_SIG_LEN > (__u8 *)data_end) {
        goto pass;
    }

    /*
     * O(1) signature lookup via hash map
     * Replaces O(N) sequential memcmp checks
     * Scales to many signatures without performance degradation
     */
    __u32 *sig_type = bpf_map_lookup_elem(&sig_map, signature);
    if (sig_type) {
        /* ITO packet detected - redirect to AF_XDP socket */
        if (stats) {
            __sync_fetch_and_add(&stats->packets_ito, 1);
        }

        /* Redirect to AF_XDP socket for this queue */
        return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, 0);
    }

pass:
    /* Not an ITO packet, pass to normal network stack */
    if (stats) {
        __sync_fetch_and_add(&stats->packets_passed, 1);
    }
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
