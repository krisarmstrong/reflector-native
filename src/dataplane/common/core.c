/*
 * core.c - Core reflector engine and worker thread management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include "reflector.h"

/* Forward declarations */
extern const platform_ops_t* get_xdp_platform_ops(void);
extern const platform_ops_t* get_packet_platform_ops(void);
extern const platform_ops_t* get_bpf_platform_ops(void);

/* Global platform ops (set at runtime) */
static const platform_ops_t *platform_ops = NULL;

/* Worker thread main loop */
static void* worker_thread(void *arg)
{
    worker_ctx_t *wctx = (worker_ctx_t *)arg;
    packet_t pkts_rx[BATCH_SIZE];
    packet_t pkts_tx[BATCH_SIZE];
    int num_tx;

    /* Set CPU affinity if specified */
    if (wctx->cpu_id >= 0) {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(wctx->cpu_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#endif
        reflector_log(LOG_DEBUG, "Worker %d pinned to CPU %d", wctx->worker_id, wctx->cpu_id);
    }

    reflector_log(LOG_INFO, "Worker %d started (queue %d)", wctx->worker_id, wctx->queue_id);

    while (wctx->running) {
        /* Receive batch */
        int rcvd = platform_ops->recv_batch(wctx, pkts_rx, BATCH_SIZE);
        if (rcvd <= 0) {
            continue;
        }

        /* Process and reflect ITO packets */
        num_tx = 0;
        for (int i = 0; i < rcvd; i++) {
            if (is_ito_packet(pkts_rx[i].data, pkts_rx[i].len, wctx->config->mac)) {
                /* Reflect in-place */
                reflect_packet_inplace(pkts_rx[i].data, pkts_rx[i].len);
                pkts_tx[num_tx++] = pkts_rx[i];
            } else {
                /* Not ITO packet, release buffer */
                if (platform_ops->release_batch) {
                    platform_ops->release_batch(wctx, &pkts_rx[i], 1);
                }
            }
        }

        /* Send reflected packets */
        if (num_tx > 0) {
            platform_ops->send_batch(wctx, pkts_tx, num_tx);
        }
    }

    reflector_log(LOG_INFO, "Worker %d stopped", wctx->worker_id);
    return NULL;
}

/* Initialize reflector */
int reflector_init(reflector_ctx_t *rctx, const char *ifname)
{
    memset(rctx, 0, sizeof(*rctx));

    /* Set defaults */
    strncpy(rctx->config.ifname, ifname, MAX_IFNAME_LEN - 1);
    rctx->config.frame_size = FRAME_SIZE;
    rctx->config.num_frames = NUM_FRAMES;
    rctx->config.batch_size = BATCH_SIZE;
    rctx->config.poll_timeout_ms = 100;

    /* Get interface info */
    rctx->config.ifindex = get_interface_index(ifname);
    if (rctx->config.ifindex < 0) {
        return -1;
    }

    if (get_interface_mac(ifname, rctx->config.mac) < 0) {
        return -1;
    }

    /* Determine platform */
#ifdef __linux__
    /* Try AF_XDP first, fall back to AF_PACKET if it fails */
    platform_ops = get_xdp_platform_ops();
#elif defined(__APPLE__)
    platform_ops = get_bpf_platform_ops();
#else
    reflector_log(LOG_ERROR, "Unsupported platform");
    return -1;
#endif

    /* Get number of queues (Linux only) */
#ifdef __linux__
    int num_queues = get_num_rx_queues(ifname);
    rctx->config.num_workers = num_queues;
#else
    rctx->config.num_workers = 1;
#endif

    reflector_log(LOG_INFO, "Reflector initialized on %s (%d workers, platform: %s)",
                 ifname, rctx->config.num_workers, platform_ops->name);

    return 0;
}

/* Start reflector workers */
int reflector_start(reflector_ctx_t *rctx)
{
    rctx->num_workers = rctx->config.num_workers;
    rctx->workers = calloc(rctx->num_workers, sizeof(worker_ctx_t));
    rctx->platform_contexts = calloc(rctx->num_workers, sizeof(platform_ctx_t *));

    if (!rctx->workers || !rctx->platform_contexts) {
        free(rctx->workers);
        free(rctx->platform_contexts);
        return -ENOMEM;
    }

    rctx->running = true;

    /* Initialize and start workers */
    for (int i = 0; i < rctx->num_workers; i++) {
        worker_ctx_t *wctx = &rctx->workers[i];
        wctx->worker_id = i;
        wctx->queue_id = i;
        wctx->cpu_id = get_queue_cpu_affinity(rctx->config.ifname, i);
        wctx->config = &rctx->config;
        wctx->running = true;

        /* Initialize platform */
        if (platform_ops->init(rctx, wctx) < 0) {
#ifdef __linux__
            /* Try AF_PACKET fallback on Linux if AF_XDP fails */
            if (platform_ops == get_xdp_platform_ops()) {
                reflector_log(LOG_WARN, "AF_XDP init failed, trying AF_PACKET fallback...");
                platform_ops = get_packet_platform_ops();
                if (platform_ops->init(rctx, wctx) < 0) {
                    reflector_log(LOG_ERROR, "Failed to initialize AF_PACKET for worker %d", i);
                    reflector_stop(rctx);
                    return -1;
                }
            } else {
                reflector_log(LOG_ERROR, "Failed to initialize platform for worker %d", i);
                reflector_stop(rctx);
                return -1;
            }
#else
            reflector_log(LOG_ERROR, "Failed to initialize platform for worker %d", i);
            reflector_stop(rctx);
            return -1;
#endif
        }

        rctx->platform_contexts[i] = wctx->pctx;

        /* Create thread */
        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_thread, wctx) != 0) {
            reflector_log(LOG_ERROR, "Failed to create worker thread %d", i);
            reflector_stop(rctx);
            return -1;
        }
        pthread_detach(tid);
    }

    reflector_log(LOG_INFO, "Reflector started with %d workers", rctx->num_workers);
    return 0;
}

/* Stop reflector */
void reflector_stop(reflector_ctx_t *rctx)
{
    rctx->running = false;

    if (rctx->workers) {
        for (int i = 0; i < rctx->num_workers; i++) {
            rctx->workers[i].running = false;
        }

        /* Wait for threads to exit */
        sleep(1);

        /* Cleanup platform contexts */
        for (int i = 0; i < rctx->num_workers; i++) {
            if (platform_ops && platform_ops->cleanup) {
                platform_ops->cleanup(&rctx->workers[i]);
            }
        }

        free(rctx->workers);
        free(rctx->platform_contexts);
        rctx->workers = NULL;
        rctx->platform_contexts = NULL;
    }

    reflector_log(LOG_INFO, "Reflector stopped");
}

/* Cleanup reflector */
void reflector_cleanup(reflector_ctx_t *rctx)
{
    if (rctx->running) {
        reflector_stop(rctx);
    }
}

/* Get aggregated statistics */
void reflector_get_stats(const reflector_ctx_t *rctx, reflector_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));

    for (int i = 0; i < rctx->num_workers; i++) {
        stats->packets_received += rctx->workers[i].stats.packets_received;
        stats->packets_reflected += rctx->workers[i].stats.packets_reflected;
        stats->packets_dropped += rctx->workers[i].stats.packets_dropped;
        stats->bytes_received += rctx->workers[i].stats.bytes_received;
        stats->bytes_reflected += rctx->workers[i].stats.bytes_reflected;
        stats->rx_invalid += rctx->workers[i].stats.rx_invalid;
        stats->rx_nomem += rctx->workers[i].stats.rx_nomem;
        stats->tx_errors += rctx->workers[i].stats.tx_errors;
    }
}

const platform_ops_t* get_platform_ops(void)
{
    return platform_ops;
}
