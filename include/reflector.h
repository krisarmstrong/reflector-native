/*
 * reflector.h - Core data structures and definitions for packet reflector
 *
 * Copyright (c) 2025 Kris Armstrong
 * High-performance packet reflector for network test tools
 */

#ifndef REFLECTOR_H
#define REFLECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* Threading support: GCD on macOS, pthreads elsewhere */
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <pthread.h>
#endif

/* Version information - Auto-generated from git tags */
#include "version_generated.h"

/* Compiler hints for branch prediction */
#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

/* Force inline for hot path functions */
#ifdef __GNUC__
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ALWAYS_INLINE inline
#endif

/* Memory prefetch hints */
#ifdef __GNUC__
#define PREFETCH_READ(addr)  __builtin_prefetch(addr, 0, 3)
#define PREFETCH_WRITE(addr) __builtin_prefetch(addr, 1, 3)
#else
#define PREFETCH_READ(addr)  ((void)0)
#define PREFETCH_WRITE(addr) ((void)0)
#endif

/*
 * Conditional debug logging for hot-path performance
 *
 * When ENABLE_HOT_PATH_DEBUG is not defined, DEBUG_LOG becomes a no-op
 * with zero runtime overhead (no function call, no argument evaluation).
 *
 * Usage: DEBUG_LOG("packet too short: %u bytes", len);
 *
 * To enable: compile with -DENABLE_HOT_PATH_DEBUG
 */
#ifdef ENABLE_HOT_PATH_DEBUG
#define DEBUG_LOG(fmt, ...) reflector_log(LOG_DEBUG, fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) ((void)0)
#endif

/* Configuration constants */
#define MAX_IFNAME_LEN 16
#define MAX_WORKERS 16
#define BATCH_SIZE 64
#define STATS_FLUSH_BATCHES 8  /* Flush stats every 8 batches (~512 packets) */
#define FRAME_SIZE 4096
#define NUM_FRAMES 4096
#define UMEM_SIZE (NUM_FRAMES * FRAME_SIZE)  /* 16MB */

/* ITO packet signatures */
#define ITO_SIG_PROBEOT "PROBEOT"
#define ITO_SIG_DATAOT  "DATA:OT"
#define ITO_SIG_LATENCY "LATENCY"
#define ITO_SIG_LEN 7

/* Ethernet frame offsets */
#define ETH_DST_OFFSET  0
#define ETH_SRC_OFFSET  6
#define ETH_TYPE_OFFSET 12
#define ETH_HDR_LEN     14

/* IPv4 header offsets (relative to Ethernet payload) */
#define IP_VER_IHL_OFFSET  0
#define IP_PROTO_OFFSET    9
#define IP_SRC_OFFSET      12
#define IP_DST_OFFSET      16
#define IP_HDR_MIN_LEN     20

/* UDP header offsets (relative to IP payload) */
#define UDP_SRC_PORT_OFFSET 0
#define UDP_DST_PORT_OFFSET 2
#define UDP_HDR_LEN         8

/* ITO packet signature offset (relative to UDP payload) */
#define ITO_SIG_OFFSET 5  /* 5-byte header before signature */

/* Minimum packet sizes */
#define MIN_ITO_PACKET_LEN 54  /* Eth(14) + IP(20) + UDP(8) + Sig(7) + padding */

/* EtherType values */
#define ETH_P_IP 0x0800

/* IP Protocol values */
#define IPPROTO_UDP 17

/* ITO packet signature types (for statistics) */
typedef enum {
	ITO_SIG_TYPE_PROBEOT = 0,
	ITO_SIG_TYPE_DATAOT = 1,
	ITO_SIG_TYPE_LATENCY = 2,
	ITO_SIG_TYPE_UNKNOWN = 3,
	ITO_SIG_TYPE_COUNT = 4
} ito_sig_type_t;

/* Error category types */
typedef enum {
	ERR_RX_INVALID_MAC = 0,     /* Wrong destination MAC */
	ERR_RX_INVALID_ETHERTYPE,   /* Not IPv4 */
	ERR_RX_INVALID_PROTOCOL,    /* Not UDP */
	ERR_RX_INVALID_SIGNATURE,   /* No ITO signature */
	ERR_RX_TOO_SHORT,           /* Packet too short */
	ERR_TX_FAILED,              /* Transmission failed */
	ERR_RX_NOMEM,               /* Memory allocation failed */
	ERR_CATEGORY_COUNT
} error_category_t;

/* Latency statistics */
typedef struct {
	uint64_t count;              /* Number of measurements */
	uint64_t total_ns;           /* Total latency in nanoseconds */
	uint64_t min_ns;             /* Minimum latency */
	uint64_t max_ns;             /* Maximum latency */
	double avg_ns;               /* Average latency */
} latency_stats_t;

/* Statistics structure */
typedef struct {
	/* Basic packet counters */
	uint64_t packets_received;
	uint64_t packets_reflected;
	uint64_t packets_dropped;
	uint64_t bytes_received;
	uint64_t bytes_reflected;

	/* Per-signature counters */
	uint64_t sig_probeot_count;
	uint64_t sig_dataot_count;
	uint64_t sig_latency_count;
	uint64_t sig_unknown_count;

	/* Error counters by category */
	uint64_t err_invalid_mac;
	uint64_t err_invalid_ethertype;
	uint64_t err_invalid_protocol;
	uint64_t err_invalid_signature;
	uint64_t err_too_short;
	uint64_t err_tx_failed;
	uint64_t err_nomem;

	/* Legacy error counters (for compatibility) */
	uint64_t rx_invalid;        /* Total validation failures */
	uint64_t rx_nomem;          /* Memory allocation failures */
	uint64_t tx_errors;         /* Transmission errors */
	uint64_t poll_timeout;      /* Poll timeouts */

	/* Latency measurements */
	latency_stats_t latency;

	/* Performance metrics */
	double pps;                  /* Packets per second (reflected) */
	double mbps;                 /* Megabits per second (reflected) */

	/* Timing */
	uint64_t start_time_ns;      /* Start timestamp */
	uint64_t last_update_ns;     /* Last update timestamp */
} reflector_stats_t;

/* Statistics output format */
typedef enum {
	STATS_FORMAT_TEXT,           /* Human-readable text format */
	STATS_FORMAT_JSON,           /* Machine-readable JSON format */
	STATS_FORMAT_CSV             /* CSV format for logging */
} stats_format_t;

/* Configuration structure */
typedef struct {
	char ifname[MAX_IFNAME_LEN];    /* Interface name */
	int ifindex;                     /* Interface index */
	uint8_t mac[6];                  /* Interface MAC address */
	int num_workers;                 /* Number of worker threads */
	bool enable_stats;               /* Enable statistics collection */
	bool promiscuous;                /* Enable promiscuous mode */
	bool zero_copy;                  /* Enable zero-copy mode (if supported) */
	int batch_size;                  /* Packet batch size */
	int frame_size;                  /* Frame size in UMEM */
	int num_frames;                  /* Number of frames in UMEM */
	int queue_id;                    /* RX/TX queue ID (-1 for auto) */
	bool busy_poll;                  /* Enable busy polling */
	int poll_timeout_ms;             /* Poll timeout in milliseconds */
	bool measure_latency;            /* Enable latency measurements */
	stats_format_t stats_format;     /* Statistics output format */
	int stats_interval_sec;          /* Statistics display interval (seconds) */
	int cpu_affinity;                /* CPU to pin worker thread (-1 for auto) */
	bool use_huge_pages;             /* Use huge pages for UMEM (Linux only) */
	bool software_checksum;          /* Calculate checksums in software (fallback) */
} reflector_config_t;

/* Packet descriptor */
typedef struct {
    uint8_t *data;          /* Packet data pointer */
    uint32_t len;           /* Packet length */
    uint64_t addr;          /* Buffer address (for zero-copy) */
    uint64_t timestamp;     /* Receive timestamp (nanoseconds) */
} packet_t;

/* Platform-specific context (opaque) */
typedef struct platform_ctx platform_ctx_t;

/* Worker thread context */
typedef struct {
    int worker_id;
    int queue_id;
    int cpu_id;
    platform_ctx_t *pctx;
    reflector_config_t *config;
    reflector_stats_t stats;
    volatile bool running;
} worker_ctx_t;

/* Reflector context */
typedef struct {
    reflector_config_t config;
    platform_ctx_t **platform_contexts;  /* Array of per-worker contexts */
    worker_ctx_t *workers;
#ifdef __APPLE__
    dispatch_group_t worker_group;       /* GCD group for worker synchronization */
    dispatch_queue_t *worker_queues;     /* Per-worker GCD queues */
#else
    pthread_t *worker_tids;              /* Thread IDs for joining */
#endif
    reflector_stats_t global_stats;
    volatile bool running;
    int num_workers;
} reflector_ctx_t;

/* Platform abstraction interface */
typedef struct {
    const char *name;

    /* Initialize platform-specific context */
    int (*init)(reflector_ctx_t *rctx, worker_ctx_t *wctx);

    /* Cleanup platform-specific context */
    void (*cleanup)(worker_ctx_t *wctx);

    /* Receive a batch of packets */
    int (*recv_batch)(worker_ctx_t *wctx, packet_t *pkts, int max_pkts);

    /* Send a batch of packets */
    int (*send_batch)(worker_ctx_t *wctx, packet_t *pkts, int num_pkts);

    /* Return packets to fill queue (for platforms that need it) */
    void (*release_batch)(worker_ctx_t *wctx, packet_t *pkts, int num_pkts);

} platform_ops_t;

/* ========================================================================
 * FUNCTION DECLARATIONS
 * ======================================================================== */

/* ------------------------------------------------------------------------
 * Core Initialization and Cleanup
 * ------------------------------------------------------------------------ */

/**
 * Initialize reflector context for specified network interface
 * @param rctx Reflector context to initialize (must be zeroed)
 * @param ifname Network interface name (e.g., "eth0", "en0")
 * @return 0 on success, -1 on error
 */
int reflector_init(reflector_ctx_t *rctx, const char *ifname);

/**
 * Cleanup reflector and release all resources
 * @param rctx Reflector context to cleanup
 */
void reflector_cleanup(reflector_ctx_t *rctx);

/* ------------------------------------------------------------------------
 * Start/Stop Packet Reflection
 * ------------------------------------------------------------------------ */

/**
 * Start packet reflection on all configured worker threads
 * @param rctx Initialized reflector context
 * @return 0 on success, negative error code on failure
 */
int reflector_start(reflector_ctx_t *rctx);

/**
 * Stop packet reflection and wait for all workers to exit
 * @param rctx Running reflector context
 */
void reflector_stop(reflector_ctx_t *rctx);

/* ------------------------------------------------------------------------
 * Configuration Management
 * ------------------------------------------------------------------------ */

/**
 * Update reflector configuration (must call before start)
 * @param rctx Reflector context
 * @param config New configuration to apply
 * @return 0 on success, -1 on error
 */
int reflector_set_config(reflector_ctx_t *rctx, const reflector_config_t *config);

/**
 * Get current reflector configuration
 * @param rctx Reflector context
 * @param config Output buffer for configuration
 */
void reflector_get_config(const reflector_ctx_t *rctx, reflector_config_t *config);

/* ------------------------------------------------------------------------
 * Statistics Collection
 * ------------------------------------------------------------------------ */

/**
 * Get aggregated statistics from all worker threads
 * @param rctx Reflector context
 * @param stats Output buffer for statistics
 */
void reflector_get_stats(const reflector_ctx_t *rctx, reflector_stats_t *stats);

/**
 * Reset all statistics counters to zero
 * @param rctx Reflector context
 */
void reflector_reset_stats(reflector_ctx_t *rctx);

/* ------------------------------------------------------------------------
 * Network Interface Utilities
 * ------------------------------------------------------------------------ */

/**
 * Get interface index from interface name
 * @param ifname Interface name (e.g., "eth0")
 * @return Interface index on success, -1 on error
 */
int get_interface_index(const char *ifname);

/**
 * Get MAC address for specified interface
 * @param ifname Interface name
 * @param mac Output buffer for 6-byte MAC address
 * @return 0 on success, -1 on error
 */
int get_interface_mac(const char *ifname, uint8_t mac[6]);

/**
 * Get number of RX queues for interface
 * @param ifname Interface name
 * @return Number of queues, or 1 if unable to determine
 */
int get_num_rx_queues(const char *ifname);

/**
 * Get CPU affinity for specific queue (best-effort heuristic)
 * @param ifname Interface name
 * @param queue_id Queue ID to query
 * @return CPU ID, or -1 if unable to determine
 */
int get_queue_cpu_affinity(const char *ifname, int queue_id);

/**
 * Get high-resolution monotonic timestamp in nanoseconds
 * @return Timestamp in nanoseconds, or 0 on error
 */
uint64_t get_timestamp_ns(void);

/* ------------------------------------------------------------------------
 * Packet Validation and Reflection
 * ------------------------------------------------------------------------ */

/**
 * Validate if packet is an ITO test packet
 * Fast-path validation with branch prediction hints.
 * @param data Packet data buffer
 * @param len Packet length in bytes
 * @param mac Expected destination MAC address
 * @return true if valid ITO packet, false otherwise
 */
bool is_ito_packet(const uint8_t *data, uint32_t len, const uint8_t mac[6]);

/**
 * Get ITO signature type from validated packet
 * @param data Packet data buffer (must be validated first)
 * @param len Packet length in bytes
 * @return Signature type (PROBEOT, DATAOT, LATENCY, or UNKNOWN)
 */
ito_sig_type_t get_ito_signature_type(const uint8_t *data, uint32_t len);

/**
 * Reflect packet in-place by swapping MAC/IP/port headers
 * Uses SIMD instructions when available (SSE2/NEON).
 * Modifies packet buffer directly (zero-copy).
 * NOTE: Checksums handled by NIC offload or ignored by test tools
 * @param data Packet data buffer (will be modified)
 * @param len Packet length in bytes
 */
void reflect_packet_inplace(uint8_t *data, uint32_t len);

/**
 * Reflect packet with optional software checksum calculation
 *
 * @param data Packet data buffer (will be modified)
 * @param len Packet length in bytes
 * @param software_checksum Whether to recalculate IP/UDP checksums in software
 */
void reflect_packet_with_checksum(uint8_t *data, uint32_t len, bool software_checksum);

/* ------------------------------------------------------------------------
 * Statistics Helper Functions
 * ------------------------------------------------------------------------ */

/**
 * Update per-signature statistics counters
 * @param stats Statistics structure to update
 * @param sig_type Signature type to increment
 */
void update_signature_stats(reflector_stats_t *stats, ito_sig_type_t sig_type);

/**
 * Update latency statistics with new measurement
 * @param latency Latency statistics structure
 * @param latency_ns Latency measurement in nanoseconds
 */
void update_latency_stats(latency_stats_t *latency, uint64_t latency_ns);

/**
 * Update error statistics by category
 * @param stats Statistics structure to update
 * @param err_cat Error category
 */
void update_error_stats(reflector_stats_t *stats, error_category_t err_cat);

/**
 * Print statistics in specified format
 * @param stats Statistics to print
 * @param format Output format (TEXT/JSON/CSV)
 */
void reflector_print_stats_formatted(const reflector_stats_t *stats, stats_format_t format);

/**
 * Print statistics in JSON format
 * @param stats Statistics to print
 */
void reflector_print_stats_json(const reflector_stats_t *stats);

/**
 * Print statistics in CSV format
 * @param stats Statistics to print
 */
void reflector_print_stats_csv(const reflector_stats_t *stats);

/* Platform detection */
const platform_ops_t* get_platform_ops(void);

/* Logging */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

void reflector_log(log_level_t level, const char *fmt, ...);
void reflector_set_log_level(log_level_t level);

#endif /* REFLECTOR_H */
