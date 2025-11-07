/*
 * bpf_platform.c - macOS BPF platform implementation
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This implements the platform abstraction layer using Berkeley Packet Filter (BPF)
 * for macOS. While not as fast as Linux AF_XDP, it achieves good performance with
 * larger frames and proper buffer tuning.
 *
 * Performance optimizations:
 * - Large BPF buffer sizes (4MB)
 * - Immediate mode disabled (batch reads)
 * - BPF filtering at kernel level
 * - Pre-allocated packet buffers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "reflector.h"

#ifndef ETHERTYPE_IP
#define ETHERTYPE_IP 0x0800
#endif

#define BPF_DEV_PREFIX "/dev/bpf"
#define BPF_BUFFER_SIZE (4 * 1024 * 1024)  /* 4MB for batching */
#define MAX_BPF_DEVS 256

/* Platform-specific context for BPF */
struct platform_ctx {
    int bpf_fd;               /* BPF device file descriptor */
    int write_fd;             /* Separate FD for writing (same device) */
    uint8_t *read_buffer;     /* Buffer for reading packets */
    uint8_t *write_buffer;    /* Buffer for writing reflected packets */
    size_t buffer_size;       /* BPF buffer size */
    size_t read_offset;       /* Current offset in read buffer */
    size_t read_len;          /* Valid data length in read buffer */
};

/*
 * Open an available BPF device
 * macOS has /dev/bpf0 through /dev/bpfN
 */
static int open_bpf_device(void)
{
    char dev_name[32];
    int fd = -1;

    for (int i = 0; i < MAX_BPF_DEVS; i++) {
        snprintf(dev_name, sizeof(dev_name), "%s%d", BPF_DEV_PREFIX, i);
        fd = open(dev_name, O_RDWR);
        if (fd >= 0) {
            reflector_log(LOG_DEBUG, "Opened %s", dev_name);
            return fd;
        }
        if (errno != EBUSY) {
            break;
        }
    }

    int saved_errno = errno;
    reflector_log(LOG_ERROR, "Failed to open BPF device: %s", strerror(saved_errno));
    return saved_errno ? -saved_errno : -ENXIO;
}

/*
 * Set BPF filter program for ITO packet classification
 *
 * Filters at kernel level to only copy ITO packets to userspace:
 * - Ethernet with matching destination MAC
 * - IPv4 (EtherType 0x0800)
 * - UDP (protocol 17)
 * - ITO signatures (PROBEOT, DATA:OT, LATENCY) at offset 5 in UDP payload
 *
 * This significantly reduces CPU usage by avoiding unnecessary packet copies.
 */
static int set_bpf_filter(int fd, const uint8_t mac[6])
{
    /*
     * Classic BPF program structure:
     * - Load operations (BPF_LD) read packet data
     * - Jump operations (BPF_JMP) conditionally branch
     * - Return (BPF_RET) accepts (non-zero) or rejects (0)
     *
     * Offsets:
     * - 0-5: Destination MAC
     * - 12-13: EtherType
     * - 14+9: IP protocol (14 is ETH_HLEN, +9 for protocol field)
     * - 14+20+8+5: UDP payload+5 for ITO signature (14 ETH + 20 IP + 8 UDP + 5 offset)
     */

    struct bpf_insn insns[] = {
        /* Load destination MAC byte 0 and compare */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 0),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, mac[0], 0, 22),

        /* Load destination MAC byte 1 and compare */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 1),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, mac[1], 0, 20),

        /* Load destination MAC byte 2 and compare */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 2),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, mac[2], 0, 18),

        /* Load destination MAC byte 3 and compare */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 3),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, mac[3], 0, 16),

        /* Load destination MAC byte 4 and compare */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 4),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, mac[4], 0, 14),

        /* Load destination MAC byte 5 and compare */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 5),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, mac[5], 0, 12),

        /* Load EtherType (2 bytes at offset 12) and check for IPv4 (0x0800) */
        BPF_STMT(BPF_LD + BPF_H + BPF_ABS, 12),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 0x0800, 0, 10),

        /* Load IP protocol byte (offset 14+9=23) and check for UDP (17) */
        BPF_STMT(BPF_LD + BPF_B + BPF_ABS, 23),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 17, 0, 8),

        /* Check for ITO signature at UDP payload offset 5 */
        /* ITO signatures are 7 bytes at offset: 14 (ETH) + 20 (IP) + 8 (UDP) + 5 (offset) = 47 */

        /* Check for "PROBEOT" (first 4 bytes: "PROB" = 0x50524F42) */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, 47),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 0x50524F42, 3, 0),  /* 'P''R''O''B' */

        /* Check for "DATA:OT" (first 4 bytes: "DATA" = 0x44415441) */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, 47),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 0x44415441, 1, 0),  /* 'D''A''T''A' */

        /* Check for "LATENCY" (first 4 bytes: "LATE" = 0x4C415445) */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, 47),
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 0x4C415445, 0, 1),  /* 'L''A''T''E' */

        /* Accept packet (return full length) */
        BPF_STMT(BPF_RET + BPF_K, (u_int)-1),

        /* Reject packet (return 0) */
        BPF_STMT(BPF_RET + BPF_K, 0),
    };

    struct bpf_program filter = {
        .bf_len = sizeof(insns) / sizeof(insns[0]),
        .bf_insns = insns
    };

    if (ioctl(fd, BIOCSETF, &filter) < 0) {
        int saved_errno = errno;
        reflector_log(LOG_ERROR, "Failed to set BPF filter: %s", strerror(saved_errno));
        return saved_errno ? -saved_errno : -EIO;
    }

    reflector_log(LOG_INFO, "Kernel-level BPF filter installed (filters ITO packets before userspace copy)");
    return 0;
}

/*
 * Initialize BPF platform
 */
int bpf_platform_init(reflector_ctx_t *rctx, worker_ctx_t *wctx)
{
    (void)rctx;  /* Not used on macOS */

    struct platform_ctx *pctx = calloc(1, sizeof(*pctx));
    if (!pctx) {
        reflector_log(LOG_ERROR, "Failed to allocate platform context");
        return -ENOMEM;
    }

    wctx->pctx = pctx;
    pctx->buffer_size = BPF_BUFFER_SIZE;

    /* Allocate buffers */
    pctx->read_buffer = malloc(pctx->buffer_size);
    pctx->write_buffer = malloc(pctx->buffer_size);
    if (!pctx->read_buffer || !pctx->write_buffer) {
        reflector_log(LOG_ERROR, "Failed to allocate buffers");
        free(pctx->read_buffer);
        free(pctx->write_buffer);
        free(pctx);
        return -ENOMEM;
    }

    /* Open BPF device for reading */
    pctx->bpf_fd = open_bpf_device();
    if (pctx->bpf_fd < 0) {
        int ret = pctx->bpf_fd;  /* Propagate error code */
        free(pctx->read_buffer);
        free(pctx->write_buffer);
        free(pctx);
        return ret;
    }

    /* Open another BPF device for writing (macOS limitation: one-way devices) */
    pctx->write_fd = open_bpf_device();
    if (pctx->write_fd < 0) {
        int ret = pctx->write_fd;  /* Propagate error code */
        close(pctx->bpf_fd);
        free(pctx->read_buffer);
        free(pctx->write_buffer);
        free(pctx);
        return ret;
    }

    /* Set buffer size for read device */
    u_int buf_size = (u_int)pctx->buffer_size;
    if (ioctl(pctx->bpf_fd, BIOCSBLEN, &buf_size) < 0) {
        reflector_log(LOG_WARN, "Failed to set BPF buffer size: %s", strerror(errno));
    }

    /* Bind to interface (read) */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, wctx->config->ifname, sizeof(ifr.ifr_name) - 1);

    if (ioctl(pctx->bpf_fd, BIOCSETIF, &ifr) < 0) {
        reflector_log(LOG_ERROR, "Failed to bind BPF to %s: %s",
                     wctx->config->ifname, strerror(errno));
        close(pctx->bpf_fd);
        close(pctx->write_fd);
        free(pctx->read_buffer);
        free(pctx->write_buffer);
        free(pctx);
        return -1;
    }

    /* Bind to interface (write) */
    if (ioctl(pctx->write_fd, BIOCSETIF, &ifr) < 0) {
        reflector_log(LOG_ERROR, "Failed to bind write BPF to %s: %s",
                     wctx->config->ifname, strerror(errno));
        close(pctx->bpf_fd);
        close(pctx->write_fd);
        free(pctx->read_buffer);
        free(pctx->write_buffer);
        free(pctx);
        return -1;
    }

    /* Enable immediate mode for low latency (disable for higher throughput) */
    u_int enable = wctx->config->busy_poll ? 0 : 1;
    if (ioctl(pctx->bpf_fd, BIOCIMMEDIATE, &enable) < 0) {
        reflector_log(LOG_WARN, "Failed to set immediate mode: %s", strerror(errno));
    }

    /* See sent packets (disable to avoid loops) */
    enable = 0;
    if (ioctl(pctx->bpf_fd, BIOCSSEESENT, &enable) < 0) {
        reflector_log(LOG_WARN, "Failed to disable see-sent: %s", strerror(errno));
    }

    /* Set to promiscuous mode if requested */
    if (wctx->config->promiscuous) {
        if (ioctl(pctx->bpf_fd, BIOCPROMISC, NULL) < 0) {
            reflector_log(LOG_WARN, "Failed to enable promiscuous mode: %s", strerror(errno));
        }
    }

    /* Install BPF filter */
    if (set_bpf_filter(pctx->bpf_fd, wctx->config->mac) < 0) {
        close(pctx->bpf_fd);
        close(pctx->write_fd);
        free(pctx->read_buffer);
        free(pctx->write_buffer);
        free(pctx);
        return -1;
    }

    /* Set read timeout */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = wctx->config->poll_timeout_ms * 1000;
    if (ioctl(pctx->bpf_fd, BIOCSRTIMEOUT, &tv) < 0) {
        reflector_log(LOG_WARN, "Failed to set read timeout: %s", strerror(errno));
    }

    pctx->read_offset = 0;
    pctx->read_len = 0;

    reflector_log(LOG_INFO, "BPF platform initialized on %s (buffer: %zu KB)",
                 wctx->config->ifname, pctx->buffer_size / 1024);
    return 0;
}

/*
 * Cleanup BPF platform
 */
void bpf_platform_cleanup(worker_ctx_t *wctx)
{
    struct platform_ctx *pctx = wctx->pctx;
    if (!pctx) {
        return;
    }

    if (pctx->bpf_fd >= 0) {
        close(pctx->bpf_fd);
    }
    if (pctx->write_fd >= 0) {
        close(pctx->write_fd);
    }

    free(pctx->read_buffer);
    free(pctx->write_buffer);
    free(pctx);
    wctx->pctx = NULL;
}

/*
 * Receive batch of packets from BPF
 */
int bpf_platform_recv_batch(worker_ctx_t *wctx, packet_t *pkts, int max_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    int num_pkts = 0;

    /* If buffer is empty, read from BPF device */
    if (pctx->read_offset >= pctx->read_len) {
        ssize_t n = read(pctx->bpf_fd, pctx->read_buffer, pctx->buffer_size);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                wctx->stats.poll_timeout++;
                return 0;
            }
            reflector_log(LOG_ERROR, "BPF read error: %s", strerror(errno));
            return -1;
        }

        pctx->read_len = n;
        pctx->read_offset = 0;
    }

    /* Parse BPF packets from buffer */
    while (pctx->read_offset < pctx->read_len && num_pkts < max_pkts) {
        struct bpf_hdr *bh = (struct bpf_hdr *)(pctx->read_buffer + pctx->read_offset);

        /* Validate BPF header */
        if (pctx->read_offset + bh->bh_hdrlen + bh->bh_caplen > pctx->read_len) {
            break;
        }

        /* Extract packet data (skip BPF header) */
        uint8_t *pkt_data = pctx->read_buffer + pctx->read_offset + bh->bh_hdrlen;
        uint32_t pkt_len = bh->bh_caplen;

        /* Store packet (note: points into read_buffer, must be processed before next read) */
        pkts[num_pkts].data = pkt_data;
        pkts[num_pkts].len = pkt_len;
        pkts[num_pkts].addr = 0;  /* Not used on macOS */

        /* Only timestamp if latency measurement is enabled (avoid hot-path syscall overhead) */
        pkts[num_pkts].timestamp = wctx->config->measure_latency ? get_timestamp_ns() : 0;

        wctx->stats.packets_received++;
        wctx->stats.bytes_received += pkt_len;

        num_pkts++;

        /* Move to next packet (BPF aligns to word boundary) */
        pctx->read_offset += BPF_WORDALIGN(bh->bh_hdrlen + bh->bh_caplen);
    }

    return num_pkts;
}

/*
 * Send batch of packets via BPF
 */
int bpf_platform_send_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    int sent = 0;

    for (int i = 0; i < num_pkts; i++) {
        ssize_t n = write(pctx->write_fd, pkts[i].data, pkts[i].len);
        if (n < 0) {
            if (errno == EAGAIN || errno == ENOBUFS) {
                /* Write buffer full, try again later */
                break;
            }
            reflector_log(LOG_ERROR, "BPF write error: %s", strerror(errno));
            wctx->stats.tx_errors++;
            continue;
        }

        if ((size_t)n != pkts[i].len) {
            reflector_log(LOG_WARN, "Partial write: %zd / %u bytes", n, pkts[i].len);
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
 * Release batch (no-op for BPF, packets are copied)
 */
void bpf_platform_release_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
    /* BPF doesn't use zero-copy, so nothing to release */
    (void)wctx;
    (void)pkts;
    (void)num_pkts;
}

/* Platform operations structure */
static const platform_ops_t bpf_platform_ops = {
    .name = "macOS BPF",
    .init = bpf_platform_init,
    .cleanup = bpf_platform_cleanup,
    .recv_batch = bpf_platform_recv_batch,
    .send_batch = bpf_platform_send_batch,
    .release_batch = bpf_platform_release_batch,
};

const platform_ops_t* get_bpf_platform_ops(void)
{
    return &bpf_platform_ops;
}
