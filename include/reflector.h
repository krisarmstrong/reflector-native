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

/* Configuration constants */
#define MAX_IFNAME_LEN 16
#define MAX_WORKERS 16
#define BATCH_SIZE 64
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

/* Function declarations */

/* Core initialization and cleanup */
int reflector_init(reflector_ctx_t *rctx, const char *ifname);
void reflector_cleanup(reflector_ctx_t *rctx);

/* Start/stop reflection */
int reflector_start(reflector_ctx_t *rctx);
void reflector_stop(reflector_ctx_t *rctx);

/* Configuration */
int reflector_set_config(reflector_ctx_t *rctx, const reflector_config_t *config);
void reflector_get_config(const reflector_ctx_t *rctx, reflector_config_t *config);

/* Statistics */
void reflector_get_stats(const reflector_ctx_t *rctx, reflector_stats_t *stats);
void reflector_reset_stats(reflector_ctx_t *rctx);

/* Utility functions */
int get_interface_index(const char *ifname);
int get_interface_mac(const char *ifname, uint8_t mac[6]);
int get_num_rx_queues(const char *ifname);
int get_queue_cpu_affinity(const char *ifname, int queue_id);
uint64_t get_timestamp_ns(void);

/* Packet validation and reflection */
bool is_ito_packet(const uint8_t *data, uint32_t len, const uint8_t mac[6]);
ito_sig_type_t get_ito_signature_type(const uint8_t *data, uint32_t len);
void reflect_packet_inplace(uint8_t *data, uint32_t len);

/* Statistics helpers */
void update_signature_stats(reflector_stats_t *stats, ito_sig_type_t sig_type);
void update_latency_stats(latency_stats_t *latency, uint64_t latency_ns);
void update_error_stats(reflector_stats_t *stats, error_category_t err_cat);
void reflector_print_stats_formatted(const reflector_stats_t *stats, stats_format_t format);
void reflector_print_stats_json(const reflector_stats_t *stats);
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
