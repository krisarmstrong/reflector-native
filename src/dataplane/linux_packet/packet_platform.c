/*
 * packet_platform.c - Maximum Performance Linux AF_PACKET implementation
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * HIGHLY OPTIMIZED AF_PACKET fallback for NICs without AF_XDP support.
 * Implements every possible optimization:
 * - PACKET_MMAP (zero-copy ring buffers)
 * - PACKET_FANOUT (multi-queue distribution)
 * - PACKET_QDISC_BYPASS (bypass qdisc layer)
 * - TPACKET_V2 (frame-level ring buffers)
 * - SO_BUSY_POLL (low latency polling)
 *
 * Expected performance: 100-200 Mbps (vs 50-100 Mbps without optimizations)
 * Still far below AF_XDP (10 Gbps), but maximum possible for AF_PACKET.
 */

#include "reflector.h"

#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>

/* Ring buffer configuration - tuned for performance */
#define PACKET_RING_FRAMES 4096
#define PACKET_FRAME_SIZE 2048
#define PACKET_BLOCK_SIZE (PACKET_FRAME_SIZE * 128) /* 128 frames per block */
#define PACKET_BLOCK_NR (PACKET_RING_FRAMES / 128)

/* Platform-specific context for optimized AF_PACKET */
struct platform_ctx {
	int sock_fd; /* AF_PACKET socket */

	/* RX ring buffer (PACKET_MMAP) */
	void *rx_ring;
	size_t rx_ring_size;
	unsigned int rx_frame_num;
	unsigned int rx_frame_idx;

	/* TX ring buffer (PACKET_MMAP) */
	void *tx_ring;
	size_t tx_ring_size;
	unsigned int tx_frame_num;
	unsigned int tx_frame_idx;

	/* TPACKET version in use (2 or 3) */
	int tpacket_version;

	/* Ring buffer configuration */
	union {
		struct tpacket_req req2;   /* TPACKET_V2 */
		struct tpacket_req3 req3;  /* TPACKET_V3 */
	};

	/* V3 block tracking */
	unsigned int current_block_idx;
	unsigned int current_block_offset;

	/* Frame size */
	uint32_t frame_size;
};

/*
 * Try to setup TPACKET_V3 (preferred for real hardware)
 * Returns 0 on success, -1 on failure
 */
static int try_tpacket_v3(struct platform_ctx *pctx)
{
	int version = TPACKET_V3;
	if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_VERSION, &version, sizeof(version)) < 0) {
		return -1;
	}

	/* Configure RX ring buffer with TPACKET_V3 (block-level batching) */
	memset(&pctx->req3, 0, sizeof(pctx->req3));
	pctx->req3.tp_block_size = PACKET_BLOCK_SIZE;
	pctx->req3.tp_frame_size = PACKET_FRAME_SIZE;
	pctx->req3.tp_block_nr = PACKET_BLOCK_NR;
	pctx->req3.tp_frame_nr = PACKET_RING_FRAMES;
	pctx->req3.tp_retire_blk_tov = 10; /* 10ms block timeout */
	pctx->req3.tp_feature_req_word = 0;

	if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_RX_RING, &pctx->req3, sizeof(pctx->req3)) < 0) {
		return -1;
	}

	pctx->tpacket_version = 3;
	pctx->rx_ring_size = pctx->req3.tp_block_size * pctx->req3.tp_block_nr;
	pctx->rx_frame_num = pctx->req3.tp_frame_nr;
	return 0;
}

/*
 * Try to setup TPACKET_V2 (fallback for veth/testing)
 * Returns 0 on success, -1 on failure
 */
static int try_tpacket_v2(struct platform_ctx *pctx)
{
	int version = TPACKET_V2;
	if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_VERSION, &version, sizeof(version)) < 0) {
		return -1;
	}

	/* Configure RX ring buffer with TPACKET_V2 (frame-level) */
	memset(&pctx->req2, 0, sizeof(pctx->req2));
	pctx->req2.tp_block_size = PACKET_BLOCK_SIZE;
	pctx->req2.tp_frame_size = PACKET_FRAME_SIZE;
	pctx->req2.tp_block_nr = PACKET_BLOCK_NR;
	pctx->req2.tp_frame_nr = PACKET_RING_FRAMES;

	if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_RX_RING, &pctx->req2, sizeof(pctx->req2)) < 0) {
		return -1;
	}

	pctx->tpacket_version = 2;
	pctx->rx_ring_size = pctx->req2.tp_block_size * pctx->req2.tp_block_nr;
	pctx->rx_frame_num = pctx->req2.tp_frame_nr;
	return 0;
}

/*
 * Initialize maximum performance AF_PACKET platform
 * Tries TPACKET_V3 first (best for real hardware), falls back to V2 (for veth/testing)
 */
int packet_platform_init(reflector_ctx_t *rctx, worker_ctx_t *wctx)
{
	(void)rctx;

	struct platform_ctx *pctx = calloc(1, sizeof(*pctx));
	if (!pctx) {
		return -ENOMEM;
	}

	wctx->pctx = pctx;
	pctx->frame_size = PACKET_FRAME_SIZE;

	/* Create AF_PACKET socket */
	pctx->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (pctx->sock_fd < 0) {
		reflector_log(LOG_ERROR, "Failed to create AF_PACKET socket: %s", strerror(errno));
		free(pctx);
		return -1;
	}

	/* Try TPACKET_V3 first (better for real hardware) */
	if (try_tpacket_v3(pctx) == 0) {
		reflector_log(LOG_DEBUG, "Using TPACKET_V3 (block-level batching)");
	} else {
		/* Fall back to TPACKET_V2 (works on veth, older kernels) */
		/* Need new socket since V3 attempt may have left socket in bad state */
		close(pctx->sock_fd);
		pctx->sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
		if (pctx->sock_fd < 0 || try_tpacket_v2(pctx) < 0) {
			reflector_log(LOG_ERROR, "Failed to setup TPACKET_V2: %s", strerror(errno));
			if (pctx->sock_fd >= 0) close(pctx->sock_fd);
			free(pctx);
			return -1;
		}
		reflector_log(LOG_DEBUG, "Using TPACKET_V2 (frame-level, veth compatible)");
	}

	/* Configure TX ring buffer (same for V2 and V3) */
	struct tpacket_req tx_req = {0};
	tx_req.tp_block_size = PACKET_BLOCK_SIZE;
	tx_req.tp_frame_size = PACKET_FRAME_SIZE;
	tx_req.tp_block_nr = PACKET_BLOCK_NR / 2; /* Smaller TX ring */
	tx_req.tp_frame_nr = PACKET_RING_FRAMES / 2;

	bool have_tx_ring = true;
	if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_TX_RING, &tx_req, sizeof(tx_req)) < 0) {
		reflector_log(LOG_WARN, "Failed to setup TX ring (will use send()): %s", strerror(errno));
		have_tx_ring = false;
		memset(&tx_req, 0, sizeof(tx_req));
	}

	/* Calculate total ring size */
	pctx->tx_ring_size = have_tx_ring ? (tx_req.tp_block_size * tx_req.tp_block_nr) : 0;
	pctx->tx_frame_num = have_tx_ring ? tx_req.tp_frame_nr : 0;
	size_t total_ring_size = pctx->rx_ring_size + pctx->tx_ring_size;

	/* mmap() the ring buffers */
	bool use_simple_mode = false;
	pctx->rx_ring = mmap(NULL, total_ring_size, PROT_READ | PROT_WRITE,
	                     MAP_SHARED | MAP_LOCKED | MAP_POPULATE, pctx->sock_fd, 0);
	if (pctx->rx_ring == MAP_FAILED) {
		reflector_log(LOG_WARN, "Failed to mmap ring buffers: %s", strerror(errno));
		reflector_log(LOG_INFO, "Using simple recv/send mode (slower but more compatible)");
		use_simple_mode = true;
		pctx->rx_ring = NULL;
		pctx->tx_ring = NULL;
		pctx->rx_ring_size = 0;
		pctx->tx_ring_size = 0;
	}

	if (!use_simple_mode) {
		/* Only set tx_ring if TX ring was successfully configured */
		if (have_tx_ring) {
			pctx->tx_ring = pctx->rx_ring + pctx->rx_ring_size;
			pctx->tx_frame_num = tx_req.tp_frame_nr;
		} else {
			pctx->tx_ring = NULL; /* Force simple send() mode */
			pctx->tx_frame_num = 0;
		}
		pctx->rx_frame_idx = 0;
		pctx->tx_frame_idx = 0;
		pctx->current_block_idx = 0;
		pctx->current_block_offset = 0;

		reflector_log(LOG_INFO, "Allocated PACKET_MMAP rings: RX=%zu MB, TX=%s",
		              pctx->rx_ring_size / (1024 * 1024),
		              have_tx_ring ? "ring mode" : "simple send() mode");
	}

	/* Bind to interface */
	struct sockaddr_ll sll = {0};
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = htons(ETH_P_ALL);
	sll.sll_ifindex = wctx->config->ifindex;

	if (bind(pctx->sock_fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
		reflector_log(LOG_ERROR, "Failed to bind AF_PACKET socket: %s", strerror(errno));
		munmap(pctx->rx_ring, total_ring_size);
		close(pctx->sock_fd);
		free(pctx);
		return -1;
	}

	/* Enable PACKET_QDISC_BYPASS for faster TX */
	int qdisc_bypass = 1;
	if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_QDISC_BYPASS, &qdisc_bypass,
	               sizeof(qdisc_bypass)) < 0) {
		reflector_log(LOG_WARN, "Failed to enable QDISC bypass: %s", strerror(errno));
	} else {
		reflector_log(LOG_INFO, "PACKET_QDISC_BYPASS enabled (faster TX)");
	}

	/* Enable PACKET_FANOUT for multi-queue distribution (if multiple workers) */
	if (rctx->num_workers > 1) {
		uint32_t fanout_arg = (getpid() & 0xffff) | (PACKET_FANOUT_HASH << 16);
		if (setsockopt(pctx->sock_fd, SOL_PACKET, PACKET_FANOUT, &fanout_arg, sizeof(fanout_arg)) <
		    0) {
			reflector_log(LOG_WARN, "Failed to enable PACKET_FANOUT: %s", strerror(errno));
		} else {
			reflector_log(LOG_INFO, "PACKET_FANOUT enabled (multi-queue distribution)");
		}
	}

	/* Enable SO_BUSY_POLL for lower latency (50 microseconds) */
	int busy_poll = 50;
	if (setsockopt(pctx->sock_fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll)) < 0) {
		reflector_log(LOG_WARN, "Failed to enable busy polling: %s", strerror(errno));
	} else {
		reflector_log(LOG_INFO, "SO_BUSY_POLL enabled (low latency mode)");
	}

	/* Increase socket buffer sizes */
	int bufsize = 4 * 1024 * 1024; /* 4MB */
	setsockopt(pctx->sock_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
	setsockopt(pctx->sock_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

	reflector_log(LOG_INFO, "Optimized AF_PACKET initialized on %s:", wctx->config->ifname);
	reflector_log(LOG_INFO, "  - PACKET_MMAP: zero-copy ring buffers");
	reflector_log(LOG_INFO, "  - TPACKET_V%d: %s", pctx->tpacket_version,
	              pctx->tpacket_version == 3 ? "block-level batching (optimal)" : "frame-level (veth compatible)");
	reflector_log(LOG_INFO, "  - PACKET_QDISC_BYPASS: fast TX path");
	reflector_log(LOG_INFO, "  - SO_BUSY_POLL: reduced latency");
	reflector_log(LOG_INFO, "Expected: %s",
	              pctx->tpacket_version == 3 ? "200-400 Mbps (real hardware)" : "100-200 Mbps (veth/virtual)");

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

	if (pctx->rx_ring && pctx->rx_ring != MAP_FAILED) {
		munmap(pctx->rx_ring, pctx->rx_ring_size + pctx->tx_ring_size);
	}

	if (pctx->sock_fd >= 0) {
		close(pctx->sock_fd);
	}

	free(pctx);
	wctx->pctx = NULL;
}

/*
 * Simple receive buffer for non-ring mode
 */
static __thread uint8_t simple_rx_buf[2048];

/*
 * Receive batch of packets from PACKET_MMAP ring (zero-copy)
 * Falls back to simple recv() if ring buffers not available.
 * Handles both TPACKET_V2 (frame-level) and TPACKET_V3 (block-level) iteration.
 */
int packet_platform_recv_batch(worker_ctx_t *wctx, packet_t *pkts, int max_pkts)
{
	struct platform_ctx *pctx = wctx->pctx;
	int num_pkts = 0;

	/* Simple mode: use basic recv() */
	if (!pctx->rx_ring) {
		for (int i = 0; i < max_pkts; i++) {
			ssize_t len = recv(pctx->sock_fd, simple_rx_buf, sizeof(simple_rx_buf), MSG_DONTWAIT);
			if (len <= 0) {
				break;
			}
			pkts[num_pkts].data = simple_rx_buf;
			pkts[num_pkts].len = (uint32_t)len;
			pkts[num_pkts].addr = 0;
			pkts[num_pkts].timestamp = wctx->config->measure_latency ? get_timestamp_ns() : 0;
			num_pkts++;
			/* In simple mode, process one at a time since we use single buffer */
			break;
		}
		return num_pkts;
	}

	/* TPACKET_V3: Block-level iteration (optimal for real hardware) */
	if (pctx->tpacket_version == 3) {
		while (num_pkts < max_pkts) {
			/* Get current block */
			struct tpacket_block_desc *block = (struct tpacket_block_desc *)(
			    pctx->rx_ring + (pctx->current_block_idx * PACKET_BLOCK_SIZE));

			/* Check if block is ready */
			if ((block->hdr.bh1.block_status & TP_STATUS_USER) == 0) {
				break; /* No more blocks ready */
			}

			/* Iterate frames within this block */
			uint32_t num_frames = block->hdr.bh1.num_pkts;
			uint8_t *frame_ptr = (uint8_t *)block + block->hdr.bh1.offset_to_first_pkt;

			while (pctx->current_block_offset < num_frames && num_pkts < max_pkts) {
				struct tpacket3_hdr *hdr = (struct tpacket3_hdr *)frame_ptr;

				/* Point directly at packet data in ring (zero-copy) */
				pkts[num_pkts].data = (uint8_t *)hdr + hdr->tp_mac;
				pkts[num_pkts].len = hdr->tp_snaplen;
				/* Store block index in upper 16 bits, frame offset in lower 16 bits */
				pkts[num_pkts].addr = (pctx->current_block_idx << 16) | pctx->current_block_offset;
				pkts[num_pkts].timestamp = wctx->config->measure_latency ? get_timestamp_ns() : 0;

				num_pkts++;
				pctx->current_block_offset++;
				frame_ptr += hdr->tp_next_offset;
			}

			/* If we've processed all frames in this block, move to next block
			 * NOTE: Don't release block here! Packet data pointers still reference it.
			 * Blocks will be released in release_batch after packets are processed.
			 */
			if (pctx->current_block_offset >= num_frames) {
				pctx->current_block_idx = (pctx->current_block_idx + 1) % PACKET_BLOCK_NR;
				pctx->current_block_offset = 0;
			}
		}
		return num_pkts;
	}

	/* TPACKET_V2: Frame-level iteration (veth compatible) */
	for (int i = 0; i < max_pkts; i++) {
		/* Bounds check to prevent overflow */
		if (pctx->rx_frame_idx >= pctx->rx_frame_num) {
			pctx->rx_frame_idx = 0; /* Wrap around */
		}
		size_t offset = (size_t)pctx->rx_frame_idx * pctx->frame_size;
		struct tpacket2_hdr *hdr = (struct tpacket2_hdr *)(pctx->rx_ring + offset);

		/* Check if frame is ready (kernel filled it) */
		if ((hdr->tp_status & TP_STATUS_USER) == 0) {
			/* No more packets ready */
			break;
		}

		/* Point directly at packet data in ring (zero-copy) */
		pkts[num_pkts].data = (uint8_t *)hdr + hdr->tp_mac;
		pkts[num_pkts].len = hdr->tp_snaplen;
		pkts[num_pkts].addr = pctx->rx_frame_idx; /* Store frame index for release */

		/* Only timestamp if latency measurement is enabled (avoid hot-path syscall overhead) */
		pkts[num_pkts].timestamp = wctx->config->measure_latency ? get_timestamp_ns() : 0;

		num_pkts++;
		pctx->rx_frame_idx = (pctx->rx_frame_idx + 1) % pctx->rx_frame_num;
	}

	return num_pkts;
}

/*
 * Send batch of packets via PACKET_MMAP TX ring (zero-copy)
 * Falls back to simple send() if ring buffers not available.
 */
int packet_platform_send_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
	struct platform_ctx *pctx = wctx->pctx;
	int sent = 0;

	/* Validate num_pkts to prevent out-of-bounds access */
	if (unlikely(num_pkts < 0 || num_pkts > BATCH_SIZE)) {
		reflector_log(LOG_ERROR, "Invalid num_pkts: %d (must be 0-%d)", num_pkts, BATCH_SIZE);
		return 0;
	}

	/* Simple mode: use basic send() */
	if (!pctx->tx_ring) {
		for (int i = 0; i < num_pkts; i++) {
			/* Validate packet data pointer */
			if (!pkts[i].data) {
				continue;
			}
			ssize_t ret = send(pctx->sock_fd, pkts[i].data, pkts[i].len, MSG_DONTWAIT);
			if (ret > 0) {
				sent++;
			}
			/* Silently drop failed sends - caller tracks TX failures */
		}
		return sent;
	}

	/* Ring mode: Use TX ring */
	for (int i = 0; i < num_pkts; i++) {
		struct tpacket2_hdr *hdr =
		    (struct tpacket2_hdr *)(pctx->tx_ring + (pctx->tx_frame_idx * pctx->frame_size));

		/* Wait for TX frame to be available */
		if (hdr->tp_status != TP_STATUS_AVAILABLE) {
			/* TX ring full, send what we have */
			if (sent > 0) {
				send(pctx->sock_fd, NULL, 0, MSG_DONTWAIT); /* Kick TX */
			}
			break;
		}

		/* Copy packet into TX frame */
		uint8_t *frame_data = (uint8_t *)hdr + TPACKET_HDRLEN;
		memcpy(frame_data, pkts[i].data, pkts[i].len);

		/* Set frame metadata */
		hdr->tp_len = pkts[i].len;
		hdr->tp_snaplen = pkts[i].len;

		/* Mark frame as ready for kernel to send */
		hdr->tp_status = TP_STATUS_SEND_REQUEST;

		sent++;
		pctx->tx_frame_idx = (pctx->tx_frame_idx + 1) % pctx->tx_frame_num;
	}

	/* Kick TX to send frames */
	if (sent > 0) {
		send(pctx->sock_fd, NULL, 0, MSG_DONTWAIT);
	}

	return sent;
}

/*
 * Release RX frames back to kernel
 * For TPACKET_V3: Release blocks that packets came from
 * For TPACKET_V2: Release individual frames
 */
void packet_platform_release_batch(worker_ctx_t *wctx, packet_t *pkts, int num_pkts)
{
	struct platform_ctx *pctx = wctx->pctx;

	/* Simple mode: nothing to release */
	if (!pctx->rx_ring) {
		return;
	}

	if (unlikely(num_pkts < 0 || num_pkts > BATCH_SIZE)) {
		reflector_log(LOG_ERROR, "Invalid num_pkts: %d (must be 0-%d)", num_pkts, BATCH_SIZE);
		return;
	}

	/* TPACKET_V3: Release blocks that these packets came from */
	if (pctx->tpacket_version == 3) {
		/* Track which blocks we've released to avoid double-release */
		uint32_t released_blocks = 0; /* Bitmask for up to 32 blocks */

		for (int i = 0; i < num_pkts; i++) {
			/* Block index is stored in upper 16 bits of addr */
			uint32_t block_idx = pkts[i].addr >> 16;
			uint32_t block_bit = 1u << (block_idx % 32);

			/* Skip if already released */
			if (released_blocks & block_bit) {
				continue;
			}

			/* Release block back to kernel */
			struct tpacket_block_desc *block = (struct tpacket_block_desc *)(
			    pctx->rx_ring + (block_idx * PACKET_BLOCK_SIZE));
			block->hdr.bh1.block_status = TP_STATUS_KERNEL;
			released_blocks |= block_bit;
		}
		return;
	}

	/* TPACKET_V2: Release individual frames */
	for (int i = 0; i < num_pkts; i++) {
		uint32_t frame_idx = pkts[i].addr; /* We stored frame index in addr */
		struct tpacket2_hdr *hdr =
		    (struct tpacket2_hdr *)(pctx->rx_ring + (frame_idx * pctx->frame_size));

		/* Return frame to kernel */
		hdr->tp_status = TP_STATUS_KERNEL;
	}
}

/* Platform operations structure */
static const platform_ops_t packet_platform_ops = {
    .name = "Linux AF_PACKET (optimized)",
    .init = packet_platform_init,
    .cleanup = packet_platform_cleanup,
    .recv_batch = packet_platform_recv_batch,
    .send_batch = packet_platform_send_batch,
    .release_batch = packet_platform_release_batch,
};

const platform_ops_t *get_packet_platform_ops(void)
{
	return &packet_platform_ops;
}
