/*
 * packet_platform.c - Linux AF_PACKET platform implementation (fallback)
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * AF_PACKET fallback for NICs that don't support AF_XDP.
 * Performance is better than macOS BPF but nowhere near AF_XDP line-rate.
 *
 * Recommended NICs for AF_XDP:
 * - Intel: i40e (XL710), ice (E810), ixgbe (82599, X540, X550)
 * - Mellanox: mlx4, mlx5
 * - Netronome: nfp
 * - Broadcom: bnxt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "reflector.h"

#define PACKET_BUFFER_SIZE (2 * 1024 * 1024)  /* 2MB */

/* Platform-specific context for AF_PACKET */
struct platform_ctx {
    int sock_fd;              /* AF_PACKET socket */
    uint8_t *buffer;          /* Packet buffer */
    size_t buffer_size;
};

/*
 * Initialize AF_PACKET platform
 */
int packet_platform_init(reflector_ctx_t *rctx, worker_ctx_t *wctx)
{
    (void)rctx;

    reflector_log(LOG_WARN, "===========================================");
    reflector_log(LOG_WARN, "WARNING: Using AF_PACKET (fallback mode)");
    reflector_log(LOG_WARN, "Performance will be LIMITED - NOT line-rate");
    reflector_log(LOG_WARN, "===========================================");
    reflector_log(LOG_WARN, "For 10G line-rate, use NICs with AF_XDP support:");
    reflector_log(LOG_WARN, "  Intel: i40e (XL710), ice (E810), ixgbe (82599/X540/X550)");
    reflector_log(LOG_WARN, "  Mellanox: mlx4, mlx5");
    reflector_log(LOG_WARN, "  Netronome: nfp");
    reflector_log(LOG_WARN, "  Broadcom: bnxt");
    reflector_log(LOG_WARN, "===========================================");

    struct platform_ctx *pctx = calloc(1, sizeof(*pctx));
    if (!pctx) {
        return -ENOMEM;
    }

    wctx->pctx = pctx;
    pctx->buffer_size = PACKET_BUFFER_SIZE;
    pctx->buffer = malloc(pctx->buffer_size);
    if (!pctx->buffer) {
        free(pctx);
        return -ENOMEM;
    }

    /* Create AF_PACKET socket */
    pctx->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (pctx->sock_fd < 0) {
        reflector_log(LOG_ERROR, "Failed to create AF_PACKET socket: %s", strerror(errno));
        free(pctx->buffer);
        free(pctx);
        return -1;
    }

    /* Bind to interface */
    struct sockaddr_ll sll = {0};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = wctx->config->ifindex;

    if (bind(pctx->sock_fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        reflector_log(LOG_ERROR, "Failed to bind AF_PACKET socket: %s", strerror(errno));
        close(pctx->sock_fd);
        free(pctx->buffer);
        free(pctx);
        return -1;
    }

    /* Set promiscuous mode if requested */
    if (wctx->config->promiscuous) {
        struct packet_mreq mr = {0};
        mr.mr_ifindex = wctx->config->ifindex;
        mr.mr_type = PACKET_MR_PROMISC;
        if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
            reflector_log(LOG_WARN, "Failed to enable promiscuous mode: %s", strerror(errno));
        }
    }

    /* Set socket receive timeout for polling */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000;  /* 1ms timeout for responsive polling */
    if (setsockopt(pctx->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        reflector_log(LOG_WARN, "Failed to set socket timeout: %s", strerror(errno));
    }

    reflector_log(LOG_INFO, "AF_PACKET platform initialized on %s (fallback mode, no-copy)",
                 wctx->config->ifname);
    return 0;
}

/*
 * Cleanup AF_PACKET platform
 */
void packet_platform_cleanup(worker_ctx_t *wctx)
{
    struct platform_ctx *pctx = wctx->pctx;
    if (!pctx) {
        return;
    }

    if (pctx->sock_fd >= 0) {
        close(pctx->sock_fd);
    }
    free(pctx->buffer);
    free(pctx);
    wctx->pctx = NULL;
}

/*
 * Receive batch of packets from AF_PACKET
 * Note: Returns packets pointing directly into buffer (no copy)
 * Caller must process/reflect before next recv call
 */
int packet_platform_recv_batch(worker_ctx_t *wctx, packet_t *pkts, int max_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    int num_pkts = 0;

    /* Read one packet into buffer - blocking with timeout */
    ssize_t n = recv(pctx->sock_fd, pctx->buffer, pctx->buffer_size, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            wctx->stats.poll_timeout++;
            return 0;
        }
        reflector_log(LOG_ERROR, "AF_PACKET recv error: %s", strerror(errno));
        return -1;
    }

    if (n == 0) {
        return 0;
    }

    /* Point directly at buffer - NO MALLOC */
    pkts[0].data = pctx->buffer;
    pkts[0].len = n;
    pkts[0].addr = 0;
    pkts[0].timestamp = get_timestamp_ns();

    wctx->stats.packets_received++;
    wctx->stats.bytes_received += n;
    num_pkts = 1;

    (void)max_pkts;  /* Only process one packet at a time with this approach */
    return num_pkts;
}

/*
 * Send batch of packets via AF_PACKET
 */
int packet_platform_send_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    int sent = 0;

    for (int i = 0; i < num_pkts; i++) {
        ssize_t n = send(pctx->sock_fd, pkts[i].data, pkts[i].len, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == ENOBUFS) {
                break;
            }
            reflector_log(LOG_ERROR, "AF_PACKET send error: %s", strerror(errno));
            wctx->stats.tx_errors++;
            continue;
        }

        if ((size_t)n != pkts[i].len) {
            reflector_log(LOG_WARN, "Partial send: %zd / %u bytes", n, pkts[i].len);
            wctx->stats.tx_errors++;
            continue;
        }

        wctx->stats.packets_reflected++;
        wctx->stats.bytes_reflected += pkts[i].len;
        sent++;
    }

    return sent;
}

/*
 * Release batch - no-op since we don't malloc
 */
void packet_platform_release_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
    (void)wctx;
    (void)pkts;
    (void)num_pkts;
    /* No malloc, so nothing to free */
}

/* Platform operations structure */
static const platform_ops_t packet_platform_ops = {
    .name = "Linux AF_PACKET (fallback)",
    .init = packet_platform_init,
    .cleanup = packet_platform_cleanup,
    .recv_batch = packet_platform_recv_batch,
    .send_batch = packet_platform_send_batch,
    .release_batch = packet_platform_release_batch,
};

const platform_ops_t* get_packet_platform_ops(void)
{
    return &packet_platform_ops;
}
