/*
 * dpdk_platform.c - DPDK platform implementation for 100G line-rate
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This platform provides the highest performance packet processing
 * by using DPDK's poll-mode drivers for direct NIC access.
 *
 * Requirements:
 * - DPDK libraries installed (libdpdk-dev)
 * - NIC bound to vfio-pci or uio_pci_generic driver
 * - Hugepages configured
 * - Run with --dpdk flag to enable
 */

#ifdef HAVE_DPDK

#include "reflector.h"

#include <errno.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DPDK configuration constants */
#define DPDK_NUM_MBUFS 8192
#define DPDK_MBUF_CACHE 256
#define DPDK_RX_DESC 1024
#define DPDK_TX_DESC 1024
#define DPDK_MAX_PKT_BURST 64

/* Platform context (per-worker) */
struct platform_ctx {
	uint16_t port_id;
	uint16_t queue_id;
	struct rte_mempool *mbuf_pool;
	struct rte_mbuf *rx_mbufs[DPDK_MAX_PKT_BURST];
	struct rte_mbuf *tx_mbufs[DPDK_MAX_PKT_BURST];
	int pending_rx;  /* Number of mbufs in rx_mbufs awaiting release */
	bool is_primary; /* Worker 0 owns EAL/port initialization */
};

/* Shared state (initialized by worker 0) */
static struct {
	bool initialized;
	uint16_t port_id;
	struct rte_mempool *mbuf_pool;
	uint16_t num_rx_queues;
	uint16_t num_tx_queues;
	struct rte_ether_addr mac_addr;
} dpdk_shared = {.initialized = false};

/* Port configuration */
static struct rte_eth_conf port_conf = {
    .rxmode =
        {
            .mq_mode = RTE_ETH_MQ_RX_RSS,
        },
    .txmode =
        {
            .mq_mode = RTE_ETH_MQ_TX_NONE,
        },
    .rx_adv_conf =
        {
            .rss_conf =
                {
                    .rss_key = NULL,
                    .rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_UDP | RTE_ETH_RSS_TCP,
                },
        },
};

/*
 * Parse EAL arguments from config string
 * Returns argc-style count, argv stored in provided array
 */
static int parse_eal_args(const char *dpdk_args, char **argv, int max_args)
{
	int argc = 0;

	/* First arg is program name (required by EAL) */
	argv[argc++] = "reflector";

	if (dpdk_args == NULL || dpdk_args[0] == '\0') {
		return argc;
	}

	/* Make a copy we can modify */
	char *args_copy = strdup(dpdk_args);
	if (args_copy == NULL) {
		return argc;
	}

	/* Tokenize the argument string */
	char *saveptr;
	char *token = strtok_r(args_copy, " \t", &saveptr);

	while (token != NULL && argc < max_args - 1) {
		argv[argc++] = strdup(token);
		token = strtok_r(NULL, " \t", &saveptr);
	}

	free(args_copy);
	return argc;
}

/*
 * Initialize DPDK EAL and port (called by worker 0 only)
 */
static int dpdk_init_eal_and_port(reflector_ctx_t *rctx, int num_queues)
{
	char *argv[32];
	int argc;
	int ret;
	uint16_t port_id;
	struct rte_eth_dev_info dev_info;

	/* Parse EAL arguments */
	argc = parse_eal_args(rctx->config.dpdk_args, argv, 32);

	/* Initialize EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		reflector_log(LOG_ERROR, "DPDK EAL init failed: %s", rte_strerror(rte_errno));
		return -1;
	}

	/* Find available port */
	uint16_t nb_ports = rte_eth_dev_count_avail();
	if (nb_ports == 0) {
		reflector_log(LOG_ERROR, "No DPDK ports available. Check NIC binding.");
		reflector_log(LOG_ERROR, "Use: dpdk-devbind.py --bind=vfio-pci <pci-id>");
		return -1;
	}

	/* Use first available port (or parse from args in future) */
	port_id = 0;
	RTE_ETH_FOREACH_DEV(port_id)
	{
		break; /* Use first port */
	}

	/* Get port info */
	ret = rte_eth_dev_info_get(port_id, &dev_info);
	if (ret < 0) {
		reflector_log(LOG_ERROR, "Failed to get device info: %s", rte_strerror(-ret));
		return -1;
	}

	reflector_log(LOG_INFO, "DPDK port %u: %s", port_id, dev_info.driver_name);

	/* Adjust queue count based on device limits */
	if ((uint16_t)num_queues > dev_info.max_rx_queues) {
		num_queues = dev_info.max_rx_queues;
		reflector_log(LOG_WARN, "Limiting to %d RX queues (device max)", num_queues);
	}
	if ((uint16_t)num_queues > dev_info.max_tx_queues) {
		num_queues = dev_info.max_tx_queues;
		reflector_log(LOG_WARN, "Limiting to %d TX queues (device max)", num_queues);
	}

	/* Create mempool for packet buffers */
	dpdk_shared.mbuf_pool =
	    rte_pktmbuf_pool_create("mbuf_pool", DPDK_NUM_MBUFS * num_queues, DPDK_MBUF_CACHE, 0,
	                            RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (dpdk_shared.mbuf_pool == NULL) {
		reflector_log(LOG_ERROR, "Failed to create mbuf pool: %s", rte_strerror(rte_errno));
		return -1;
	}

	/* Configure the port */
	ret = rte_eth_dev_configure(port_id, num_queues, num_queues, &port_conf);
	if (ret < 0) {
		reflector_log(LOG_ERROR, "Failed to configure port: %s", rte_strerror(-ret));
		return -1;
	}

	/* Adjust descriptor counts if needed */
	uint16_t nb_rxd = DPDK_RX_DESC;
	uint16_t nb_txd = DPDK_TX_DESC;
	ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd);
	if (ret < 0) {
		reflector_log(LOG_ERROR, "Failed to adjust descriptors: %s", rte_strerror(-ret));
		return -1;
	}

	/* Setup RX and TX queues */
	for (int q = 0; q < num_queues; q++) {
		ret = rte_eth_rx_queue_setup(port_id, q, nb_rxd, rte_eth_dev_socket_id(port_id), NULL,
		                             dpdk_shared.mbuf_pool);
		if (ret < 0) {
			reflector_log(LOG_ERROR, "Failed to setup RX queue %d: %s", q, rte_strerror(-ret));
			return -1;
		}

		ret = rte_eth_tx_queue_setup(port_id, q, nb_txd, rte_eth_dev_socket_id(port_id), NULL);
		if (ret < 0) {
			reflector_log(LOG_ERROR, "Failed to setup TX queue %d: %s", q, rte_strerror(-ret));
			return -1;
		}
	}

	/* Enable promiscuous mode */
	ret = rte_eth_promiscuous_enable(port_id);
	if (ret < 0) {
		reflector_log(LOG_WARN, "Failed to enable promiscuous mode: %s", rte_strerror(-ret));
		/* Continue anyway - might work for direct traffic */
	}

	/* Start the port */
	ret = rte_eth_dev_start(port_id);
	if (ret < 0) {
		reflector_log(LOG_ERROR, "Failed to start port: %s", rte_strerror(-ret));
		return -1;
	}

	/* Get MAC address */
	ret = rte_eth_macaddr_get(port_id, &dpdk_shared.mac_addr);
	if (ret < 0) {
		reflector_log(LOG_ERROR, "Failed to get MAC address: %s", rte_strerror(-ret));
		return -1;
	}

	/* Store shared state */
	dpdk_shared.port_id = port_id;
	dpdk_shared.num_rx_queues = num_queues;
	dpdk_shared.num_tx_queues = num_queues;
	dpdk_shared.initialized = true;

	reflector_log(LOG_INFO,
	              "DPDK port %u started: MAC=%02x:%02x:%02x:%02x:%02x:%02x, "
	              "%d queues, %d RX desc, %d TX desc",
	              port_id, dpdk_shared.mac_addr.addr_bytes[0], dpdk_shared.mac_addr.addr_bytes[1],
	              dpdk_shared.mac_addr.addr_bytes[2], dpdk_shared.mac_addr.addr_bytes[3],
	              dpdk_shared.mac_addr.addr_bytes[4], dpdk_shared.mac_addr.addr_bytes[5],
	              num_queues, nb_rxd, nb_txd);

	return 0;
}

/*
 * Initialize DPDK platform for a worker
 */
int dpdk_platform_init(reflector_ctx_t *rctx, worker_ctx_t *wctx)
{
	struct platform_ctx *pctx;

	/* Allocate platform context */
	pctx = calloc(1, sizeof(*pctx));
	if (pctx == NULL) {
		reflector_log(LOG_ERROR, "Failed to allocate DPDK platform context");
		return -1;
	}

	/* Worker 0 initializes EAL and port */
	if (wctx->worker_id == 0) {
		pctx->is_primary = true;

		if (dpdk_init_eal_and_port(rctx, rctx->config.num_workers) < 0) {
			free(pctx);
			return -1;
		}

		/* Copy MAC to config */
		memcpy(rctx->config.mac, dpdk_shared.mac_addr.addr_bytes, 6);
	} else {
		/* Wait for worker 0 to finish initialization (30 second timeout) */
		const int timeout_us = 30 * 1000000; /* 30 seconds */
		int waited_us = 0;
		while (!dpdk_shared.initialized) {
			rte_delay_us(100);
			waited_us += 100;
			if (waited_us > timeout_us) {
				reflector_log(LOG_ERROR, "Timeout waiting for DPDK initialization");
				free(pctx);
				return -1;
			}
		}
		pctx->is_primary = false;
	}

	/* Attach to queue */
	pctx->port_id = dpdk_shared.port_id;
	pctx->queue_id = wctx->queue_id;
	pctx->mbuf_pool = dpdk_shared.mbuf_pool;
	pctx->pending_rx = 0;

	wctx->pctx = pctx;

	reflector_log(LOG_DEBUG, "DPDK worker %d attached to port %u queue %u", wctx->worker_id,
	              pctx->port_id, pctx->queue_id);

	return 0;
}

/*
 * Cleanup DPDK platform
 */
void dpdk_platform_cleanup(worker_ctx_t *wctx)
{
	struct platform_ctx *pctx = (struct platform_ctx *)wctx->pctx;

	if (pctx == NULL) {
		return;
	}

	/* Free any pending RX mbufs */
	for (int i = 0; i < pctx->pending_rx; i++) {
		if (pctx->rx_mbufs[i] != NULL) {
			rte_pktmbuf_free(pctx->rx_mbufs[i]);
		}
	}

	/* Only worker 0 stops the port and cleans up EAL */
	if (pctx->is_primary) {
		reflector_log(LOG_DEBUG, "DPDK primary worker stopping port %u", pctx->port_id);

		int ret = rte_eth_dev_stop(pctx->port_id);
		if (ret < 0) {
			reflector_log(LOG_WARN, "Failed to stop port: %s", rte_strerror(-ret));
		}

		ret = rte_eth_dev_close(pctx->port_id);
		if (ret < 0) {
			reflector_log(LOG_WARN, "Failed to close port: %s", rte_strerror(-ret));
		}

		/* Note: rte_eal_cleanup() can cause issues if called before
		 * all workers are done, so we skip it. The OS will clean up. */

		dpdk_shared.initialized = false;
	}

	free(pctx);
	wctx->pctx = NULL;
}

/*
 * Receive a batch of packets
 */
int dpdk_platform_recv_batch(worker_ctx_t *wctx, packet_t *pkts, int max_pkts)
{
	struct platform_ctx *pctx = (struct platform_ctx *)wctx->pctx;
	uint16_t nb_rx;

	if (max_pkts > DPDK_MAX_PKT_BURST) {
		max_pkts = DPDK_MAX_PKT_BURST;
	}

	/* Receive packets */
	nb_rx = rte_eth_rx_burst(pctx->port_id, pctx->queue_id, pctx->rx_mbufs, max_pkts);

	if (nb_rx == 0) {
		return 0;
	}

	/* Map mbufs to packet_t array */
	for (uint16_t i = 0; i < nb_rx; i++) {
		struct rte_mbuf *mb = pctx->rx_mbufs[i];
		pkts[i].data = rte_pktmbuf_mtod(mb, uint8_t *);
		pkts[i].len = rte_pktmbuf_pkt_len(mb);
		pkts[i].addr = (uint64_t)(uintptr_t)mb; /* Store mbuf pointer for release */

		if (wctx->config->measure_latency) {
			pkts[i].timestamp = get_timestamp_ns();
		}
	}

	pctx->pending_rx = nb_rx;
	return nb_rx;
}

/*
 * Send a batch of packets
 */
int dpdk_platform_send_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
	struct platform_ctx *pctx = (struct platform_ctx *)wctx->pctx;
	struct rte_mbuf *tx_mbufs[DPDK_MAX_PKT_BURST];
	uint16_t nb_tx;

	if (num_pkts <= 0 || num_pkts > DPDK_MAX_PKT_BURST) {
		return 0;
	}

	/* The packets were already reflected in-place by the core loop.
	 * We just need to send the mbufs back out. */
	for (int i = 0; i < num_pkts; i++) {
		tx_mbufs[i] = (struct rte_mbuf *)(uintptr_t)pkts[i].addr;
	}

	/* Transmit */
	nb_tx = rte_eth_tx_burst(pctx->port_id, pctx->queue_id, tx_mbufs, num_pkts);

	/* Free any packets that couldn't be sent */
	if (unlikely(nb_tx < (uint16_t)num_pkts)) {
		for (uint16_t i = nb_tx; i < (uint16_t)num_pkts; i++) {
			rte_pktmbuf_free(tx_mbufs[i]);
		}
	}

	return nb_tx;
}

/*
 * Release a batch of packets (free mbufs that weren't sent)
 */
void dpdk_platform_release_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
	(void)wctx; /* Unused */

	/* Free mbufs for packets that weren't transmitted */
	for (int i = 0; i < num_pkts; i++) {
		struct rte_mbuf *mb = (struct rte_mbuf *)(uintptr_t)pkts[i].addr;
		if (mb != NULL) {
			rte_pktmbuf_free(mb);
		}
	}
}

/* Platform operations structure */
static const platform_ops_t dpdk_platform_ops = {
    .name = "Linux DPDK (100G line-rate)",
    .init = dpdk_platform_init,
    .cleanup = dpdk_platform_cleanup,
    .recv_batch = dpdk_platform_recv_batch,
    .send_batch = dpdk_platform_send_batch,
    .release_batch = dpdk_platform_release_batch,
};

/*
 * Get DPDK platform operations
 */
const platform_ops_t *get_dpdk_platform_ops(void)
{
	return &dpdk_platform_ops;
}

#endif /* HAVE_DPDK */
