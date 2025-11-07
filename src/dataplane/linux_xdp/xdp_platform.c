/*
 * xdp_platform.c - Linux AF_XDP platform implementation
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This implements the platform abstraction layer using AF_XDP sockets
 * for zero-copy packet I/O on Linux. Achieves line-rate performance
 * through:
 * - Zero-copy UMEM shared with kernel
 * - Batched packet processing
 * - Busy-polling mode
 * - Per-queue AF_XDP sockets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if_link.h>
#include <linux/if_xdp.h>
#include <bpf/libbpf.h>
#include <xdp/xsk.h>
#include <bpf/bpf.h>

#include "reflector.h"

/* Platform-specific context for AF_XDP */
struct platform_ctx {
    struct xsk_socket_info {
        struct xsk_ring_cons rx;
        struct xsk_ring_prod tx;
        struct xsk_umem_info {
            struct xsk_ring_prod fq;  /* Fill queue */
            struct xsk_ring_cons cq;  /* Completion queue */
            struct xsk_umem *umem;
            void *buffer;
            uint64_t buffer_size;
        } umem;
        struct xsk_socket *xsk;
        uint32_t outstanding_tx;
    } xsk_info;

    struct bpf_object *bpf_obj;
    int xsks_map_fd;
    int mac_map_fd;
    int stats_map_fd;
    int prog_fd;

    uint32_t frame_size;
    uint32_t num_frames;
    uint32_t umem_frame_free;
};

/*
 * Configure UMEM (User Memory) for zero-copy packet buffers
 */
static int configure_umem(struct platform_ctx *pctx, void *buffer, uint64_t size)
{
    struct xsk_umem_config cfg = {
        .fill_size = NUM_FRAMES / 2,
        .comp_size = NUM_FRAMES / 2,
        .frame_size = pctx->frame_size,
        .frame_headroom = 0,  /* XDP_PACKET_HEADROOM */
        .flags = 0
    };

    int ret = xsk_umem__create(&pctx->xsk_info.umem.umem,
                               buffer, size,
                               &pctx->xsk_info.umem.fq,
                               &pctx->xsk_info.umem.cq,
                               &cfg);

    if (ret) {
        reflector_log(LOG_ERROR, "Failed to create UMEM: %s", strerror(-ret));
        return ret;
    }

    pctx->xsk_info.umem.buffer = buffer;
    pctx->xsk_info.umem.buffer_size = size;

    return 0;
}

/*
 * Populate fill queue with buffers for kernel to use
 */
static void populate_fill_queue(struct platform_ctx *pctx, uint32_t num)
{
    uint32_t idx;

    if (xsk_ring_prod__reserve(&pctx->xsk_info.umem.fq, num, &idx) != num) {
        reflector_log(LOG_ERROR, "Failed to reserve fill queue entries");
        return;
    }

    for (uint32_t i = 0; i < num; i++) {
        uint64_t addr = i * pctx->frame_size;
        *xsk_ring_prod__fill_addr(&pctx->xsk_info.umem.fq, idx++) = addr;
    }

    xsk_ring_prod__submit(&pctx->xsk_info.umem.fq, num);
}

/*
 * Load and attach XDP program
 */
static int load_xdp_program(worker_ctx_t *wctx)
{
    struct platform_ctx *pctx = wctx->pctx;
    reflector_config_t *cfg = wctx->config;
    int ret;

    /* Check if BPF object file exists */
    if (access("src/xdp/filter.bpf.o", F_OK) != 0) {
        reflector_log(LOG_WARN, "eBPF filter not found, will use SKB mode without filter");
        pctx->bpf_obj = NULL;
        pctx->prog_fd = -1;
        return 0;  /* Not an error - AF_XDP works without eBPF */
    }

    /* Load BPF object file */
    pctx->bpf_obj = bpf_object__open_file("src/xdp/filter.bpf.o", NULL);
    if (libbpf_get_error(pctx->bpf_obj)) {
        reflector_log(LOG_WARN, "Failed to load eBPF filter, will use SKB mode without filter");
        pctx->bpf_obj = NULL;
        pctx->prog_fd = -1;
        return 0;  /* Not an error - AF_XDP works without eBPF */
    }

    /* Load BPF program into kernel */
    ret = bpf_object__load(pctx->bpf_obj);
    if (ret) {
        reflector_log(LOG_ERROR, "Failed to load BPF object: %s", strerror(-ret));
        bpf_object__close(pctx->bpf_obj);
        return ret;
    }

    /* Get program FD */
    struct bpf_program *prog = bpf_object__find_program_by_name(pctx->bpf_obj, "xdp_filter_ito");
    if (!prog) {
        reflector_log(LOG_ERROR, "Failed to find XDP program");
        bpf_object__close(pctx->bpf_obj);
        return -1;
    }
    pctx->prog_fd = bpf_program__fd(prog);

    /* Get map FDs */
    pctx->xsks_map_fd = bpf_object__find_map_fd_by_name(pctx->bpf_obj, "xsks_map");
    pctx->mac_map_fd = bpf_object__find_map_fd_by_name(pctx->bpf_obj, "mac_map");
    pctx->stats_map_fd = bpf_object__find_map_fd_by_name(pctx->bpf_obj, "stats_map");

    if (pctx->xsks_map_fd < 0 || pctx->mac_map_fd < 0 || pctx->stats_map_fd < 0) {
        reflector_log(LOG_ERROR, "Failed to find BPF maps");
        bpf_object__close(pctx->bpf_obj);
        return -1;
    }

    /* Store interface MAC in BPF map */
    uint32_t key = 0;
    ret = bpf_map_update_elem(pctx->mac_map_fd, &key, cfg->mac, BPF_ANY);
    if (ret) {
        reflector_log(LOG_ERROR, "Failed to update MAC map: %s", strerror(-ret));
        bpf_object__close(pctx->bpf_obj);
        return ret;
    }

    /* Attach XDP program to interface */
    ret = bpf_xdp_attach(cfg->ifindex, pctx->prog_fd, XDP_FLAGS_DRV_MODE, NULL);
    if (ret) {
        reflector_log(LOG_WARN, "Failed to attach in driver mode, trying SKB mode");
        ret = bpf_xdp_attach(cfg->ifindex, pctx->prog_fd, XDP_FLAGS_SKB_MODE, NULL);
        if (ret) {
            reflector_log(LOG_ERROR, "Failed to attach XDP program: %s", strerror(-ret));
            bpf_object__close(pctx->bpf_obj);
            return ret;
        }
    }

    reflector_log(LOG_INFO, "XDP program attached to %s (ifindex %d)", cfg->ifname, cfg->ifindex);
    return 0;
}

/*
 * Initialize AF_XDP socket
 */
static int init_xsk(worker_ctx_t *wctx)
{
    struct platform_ctx *pctx = wctx->pctx;
    reflector_config_t *cfg = wctx->config;
    int ret;

    struct xsk_socket_config xsk_cfg = {
        .rx_size = NUM_FRAMES / 2,
        .tx_size = NUM_FRAMES / 2,
        .libbpf_flags = 0,
        .xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST,
        .bind_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY
    };

    /* Create AF_XDP socket */
    ret = xsk_socket__create(&pctx->xsk_info.xsk,
                            cfg->ifname,
                            wctx->queue_id,
                            pctx->xsk_info.umem.umem,
                            &pctx->xsk_info.rx,
                            &pctx->xsk_info.tx,
                            &xsk_cfg);

    if (ret) {
        reflector_log(LOG_ERROR, "Failed to create XSK socket: %s", strerror(-ret));
        return ret;
    }

    /* Add socket FD to XSK map for XDP redirect (only if eBPF program is loaded) */
    if (pctx->xsks_map_fd >= 0) {
        int xsk_fd = xsk_socket__fd(pctx->xsk_info.xsk);
        uint32_t queue_id = wctx->queue_id;
        ret = bpf_map_update_elem(pctx->xsks_map_fd, &queue_id, &xsk_fd, BPF_ANY);
        if (ret) {
            reflector_log(LOG_ERROR, "Failed to update XSK map: %s", strerror(-ret));
            xsk_socket__delete(pctx->xsk_info.xsk);
            return ret;
        }
        reflector_log(LOG_INFO, "AF_XDP socket created on queue %d (with eBPF filter)", wctx->queue_id);
    } else {
        reflector_log(LOG_INFO, "AF_XDP socket created on queue %d (SKB mode, no eBPF filter)", wctx->queue_id);
    }

    return 0;
}

/*
 * Initialize platform (AF_XDP)
 */
int xdp_platform_init(reflector_ctx_t *rctx, worker_ctx_t *wctx)
{
    struct platform_ctx *pctx = calloc(1, sizeof(*pctx));
    if (!pctx) {
        reflector_log(LOG_ERROR, "Failed to allocate platform context");
        return -ENOMEM;
    }

    wctx->pctx = pctx;
    pctx->frame_size = wctx->config->frame_size;
    pctx->num_frames = wctx->config->num_frames;

    /* Initialize map FDs to -1 (will stay -1 if no eBPF program) */
    pctx->xsks_map_fd = -1;
    pctx->mac_map_fd = -1;
    pctx->stats_map_fd = -1;
    pctx->prog_fd = -1;

    /* Allocate UMEM buffer */
    uint64_t umem_size = pctx->num_frames * pctx->frame_size;

    /* Try with hugepages first for better performance */
#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000  /* Linux-specific flag for huge pages */
#endif
    void *umem_buffer = mmap(NULL, umem_size,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                            -1, 0);

    if (umem_buffer == MAP_FAILED) {
        /* Fallback to normal pages if hugepages not available */
        reflector_log(LOG_WARN, "Hugepages not available, using normal pages");
        umem_buffer = mmap(NULL, umem_size,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1, 0);
        if (umem_buffer == MAP_FAILED) {
            int saved_errno = errno;
            reflector_log(LOG_ERROR, "Failed to allocate UMEM: %s", strerror(saved_errno));
            free(pctx);
            return saved_errno ? -saved_errno : -ENOMEM;
        }
    }

    reflector_log(LOG_INFO, "Allocated UMEM: %lu MB (%u frames of %u bytes)",
                 umem_size / (1024 * 1024), pctx->num_frames, pctx->frame_size);

    /* Configure UMEM */
    int ret = configure_umem(pctx, umem_buffer, umem_size);
    if (ret) {
        munmap(umem_buffer, umem_size);
        free(pctx);
        return ret;
    }

    /* Load and attach XDP program (only for first worker) */
    if (wctx->worker_id == 0) {
        ret = load_xdp_program(wctx);
        if (ret) {
            xsk_umem__delete(pctx->xsk_info.umem.umem);
            munmap(umem_buffer, umem_size);
            free(pctx);
            return ret;
        }
    }

    /* Initialize AF_XDP socket */
    ret = init_xsk(wctx);
    if (ret) {
        if (wctx->worker_id == 0) {
            bpf_xdp_detach(wctx->config->ifindex, XDP_FLAGS_UPDATE_IF_NOEXIST, NULL);
            bpf_object__close(pctx->bpf_obj);
        }
        xsk_umem__delete(pctx->xsk_info.umem.umem);
        munmap(umem_buffer, umem_size);
        free(pctx);
        return ret;
    }

    /* Populate fill queue with initial buffers */
    populate_fill_queue(pctx, NUM_FRAMES / 2);

    pctx->xsk_info.outstanding_tx = 0;

    reflector_log(LOG_INFO, "AF_XDP platform initialized for worker %d", wctx->worker_id);
    return 0;
}

/*
 * Cleanup platform
 */
void xdp_platform_cleanup(worker_ctx_t *wctx)
{
    struct platform_ctx *pctx = wctx->pctx;
    if (!pctx) {
        return;
    }

    /* Delete AF_XDP socket */
    if (pctx->xsk_info.xsk) {
        xsk_socket__delete(pctx->xsk_info.xsk);
    }

    /* Detach XDP program (only for first worker) */
    if (wctx->worker_id == 0 && pctx->bpf_obj) {
        bpf_xdp_detach(wctx->config->ifindex, XDP_FLAGS_UPDATE_IF_NOEXIST, NULL);
        bpf_object__close(pctx->bpf_obj);
    }

    /* Delete UMEM */
    if (pctx->xsk_info.umem.umem) {
        munmap(pctx->xsk_info.umem.buffer, pctx->xsk_info.umem.buffer_size);
        xsk_umem__delete(pctx->xsk_info.umem.umem);
    }

    free(pctx);
    wctx->pctx = NULL;
}

/*
 * Receive batch of packets (zero-copy)
 */
int xdp_platform_recv_batch(worker_ctx_t *wctx, packet_t *pkts, int max_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    uint32_t idx_rx, idx_fq = 0;
    int rcvd;

    /* Check if we need to wake up kernel (NEED_WAKEUP flag) */
    if (xsk_ring_prod__needs_wakeup(&pctx->xsk_info.umem.fq)) {
        recvfrom(xsk_socket__fd(pctx->xsk_info.xsk), NULL, 0, MSG_DONTWAIT, NULL, NULL);
    }

    /* Receive packets from RX ring */
    rcvd = xsk_ring_cons__peek(&pctx->xsk_info.rx, max_pkts, &idx_rx);
    if (rcvd == 0) {
        return 0;
    }

    /* Process received packets */
    for (int i = 0; i < rcvd; i++) {
        uint64_t addr = xsk_ring_cons__rx_desc(&pctx->xsk_info.rx, idx_rx)->addr;
        uint32_t len = xsk_ring_cons__rx_desc(&pctx->xsk_info.rx, idx_rx++)->len;

        pkts[i].addr = addr;
        pkts[i].len = len;
        pkts[i].data = xsk_umem__get_data(pctx->xsk_info.umem.buffer, addr);
        pkts[i].timestamp = get_timestamp_ns();

        wctx->stats.packets_received++;
        wctx->stats.bytes_received += len;
    }

    /* Release RX descriptors */
    xsk_ring_cons__release(&pctx->xsk_info.rx, rcvd);

    return rcvd;
}

/*
 * Send batch of packets (zero-copy)
 */
int xdp_platform_send_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    uint32_t idx_tx;

    /* Validate num_pkts to prevent out-of-bounds access */
    if (unlikely(num_pkts < 0 || num_pkts > BATCH_SIZE)) {
        reflector_log(LOG_ERROR, "Invalid num_pkts: %d (must be 0-%d)", num_pkts, BATCH_SIZE);
        return 0;
    }

    /* Reserve space in TX ring */
    int reserved = xsk_ring_prod__reserve(&pctx->xsk_info.tx, num_pkts, &idx_tx);
    if (reserved == 0) {
        /* TX ring full, complete some packets first */
        uint32_t idx_cq;
        int completed = xsk_ring_cons__peek(&pctx->xsk_info.umem.cq, BATCH_SIZE, &idx_cq);
        if (completed > 0) {
            xsk_ring_cons__release(&pctx->xsk_info.umem.cq, completed);
            pctx->xsk_info.outstanding_tx -= completed;
        }
        return 0;
    }

    /* Submit packets to TX ring */
    for (int i = 0; i < reserved; i++) {
        struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&pctx->xsk_info.tx, idx_tx++);
        tx_desc->addr = pkts[i].addr;
        tx_desc->len = pkts[i].len;

        wctx->stats.packets_reflected++;
        wctx->stats.bytes_reflected += pkts[i].len;
    }

    xsk_ring_prod__submit(&pctx->xsk_info.tx, reserved);
    pctx->xsk_info.outstanding_tx += reserved;

    /* Kick TX if needed */
    if (xsk_ring_prod__needs_wakeup(&pctx->xsk_info.tx)) {
        sendto(xsk_socket__fd(pctx->xsk_info.xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    }

    return reserved;
}

/*
 * Return buffers to fill queue
 */
void xdp_platform_release_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
    struct platform_ctx *pctx = wctx->pctx;
    uint32_t idx_fq;

    /* Validate num_pkts to prevent out-of-bounds access */
    if (unlikely(num_pkts < 0 || num_pkts > BATCH_SIZE)) {
        reflector_log(LOG_ERROR, "Invalid num_pkts: %d (must be 0-%d)", num_pkts, BATCH_SIZE);
        return;
    }

    int reserved = xsk_ring_prod__reserve(&pctx->xsk_info.umem.fq, num_pkts, &idx_fq);
    for (int i = 0; i < reserved; i++) {
        *xsk_ring_prod__fill_addr(&pctx->xsk_info.umem.fq, idx_fq++) = pkts[i].addr;
    }
    xsk_ring_prod__submit(&pctx->xsk_info.umem.fq, reserved);
}

/* Platform operations structure */
static const platform_ops_t xdp_platform_ops = {
    .name = "Linux AF_XDP",
    .init = xdp_platform_init,
    .cleanup = xdp_platform_cleanup,
    .recv_batch = xdp_platform_recv_batch,
    .send_batch = xdp_platform_send_batch,
    .release_batch = xdp_platform_release_batch,
};

const platform_ops_t* get_xdp_platform_ops(void)
{
    return &xdp_platform_ops;
}
