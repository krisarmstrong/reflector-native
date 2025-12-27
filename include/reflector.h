/*
 * reflector.h - Core data structures and definitions for packet reflector
 *
 * Copyright (c) 2025 Kris Armstrong
 * High-performance packet reflector for network test tools
 */

#ifndef REFLECTOR_H
#define REFLECTOR_H

#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>

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
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
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
#define PREFETCH_READ(addr) __builtin_prefetch(addr, 0, 3)
#define PREFETCH_WRITE(addr) __builtin_prefetch(addr, 1, 3)
#else
#define PREFETCH_READ(addr) ((void)0)
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
#define STATS_FLUSH_BATCHES 8 /* Flush stats every 8 batches (~512 packets) */
#define FRAME_SIZE 4096
#define NUM_FRAMES 4096
#define UMEM_SIZE (NUM_FRAMES * FRAME_SIZE) /* 16MB */

/* ITO packet signatures (NetAlly/Fluke/NETSCOUT) */
#define ITO_SIG_PROBEOT "PROBEOT"
#define ITO_SIG_DATAOT "DATA:OT"
#define ITO_SIG_LATENCY "LATENCY"
#define ITO_SIG_LEN 7

/* Custom signatures (RFC2544/Y.1564 tester) */
#define CUSTOM_SIG_RFC2544 "RFC2544"
#define CUSTOM_SIG_Y1564 "Y.1564 " /* Padded to 7 bytes */
#define CUSTOM_SIG_LEN 7

/* Ethernet frame offsets */
#define ETH_DST_OFFSET 0
#define ETH_SRC_OFFSET 6
#define ETH_TYPE_OFFSET 12
#define ETH_HDR_LEN 14

/* IPv4 header offsets (relative to Ethernet payload) */
#define IP_VER_IHL_OFFSET 0
#define IP_PROTO_OFFSET 9
#define IP_SRC_OFFSET 12
#define IP_DST_OFFSET 16
#define IP_HDR_MIN_LEN 20

/* UDP header offsets (relative to IP payload) */
#define UDP_SRC_PORT_OFFSET 0
#define UDP_DST_PORT_OFFSET 2
#define UDP_HDR_LEN 8

/* ITO packet signature offset (relative to UDP payload) */
#define ITO_SIG_OFFSET 5 /* 5-byte header before signature */

/* Minimum packet sizes */
#define MIN_ITO_PACKET_LEN 54      /* Eth(14) + IP(20) + UDP(8) + Sig(7) + padding */
#define MIN_ITO_PACKET_LEN_IPV6 69 /* Eth(14) + IPv6(40) + UDP(8) + Sig(7) */
#define MIN_ITO_PACKET_LEN_VLAN 58 /* Eth(14) + VLAN(4) + IP(20) + UDP(8) + Sig(7) + padding */

/* EtherType values (guard against redefinition on Linux) */
#ifndef ETH_P_IP
#define ETH_P_IP 0x0800
#endif
#ifndef ETH_P_IPV6
#define ETH_P_IPV6 0x86DD
#endif
#ifndef ETH_P_8021Q
#define ETH_P_8021Q 0x8100  /* 802.1Q VLAN tagged frame */
#endif
#ifndef ETH_P_8021AD
#define ETH_P_8021AD 0x88A8 /* 802.1ad QinQ */
#endif

/* VLAN header (802.1Q) */
#define VLAN_HDR_LEN 4      /* TPID (2) + TCI (2) */
#define VLAN_TPID_OFFSET 0  /* Tag Protocol Identifier */
#define VLAN_TCI_OFFSET 2   /* Tag Control Information */

/* IPv6 header offsets (40 bytes fixed, unlike IPv4) */
#define IPV6_HDR_LEN 40
#define IPV6_NEXT_HDR_OFFSET 6   /* Next header (protocol) */
#define IPV6_SRC_OFFSET 8        /* Source address (16 bytes) */
#define IPV6_DST_OFFSET 24       /* Destination address (16 bytes) */
#define IPV6_ADDR_LEN 16

/* IP Protocol values - only define if not from system headers
 * On Linux, IPPROTO_UDP is an enum in <netinet/in.h>, not a macro
 */
#if !defined(__linux__) && !defined(IPPROTO_UDP)
#define IPPROTO_UDP 17
#endif

/* ITO test packet standard port (NetAlly test tools) */
#define ITO_UDP_PORT 3842

/* NetAlly OUI prefix for source MAC validation (00:c0:17) */
#define NETALLY_OUI_BYTE0 0x00
#define NETALLY_OUI_BYTE1 0xc0
#define NETALLY_OUI_BYTE2 0x17

/* Minimum software checksum packet length: ETH(14) + IP(20) + UDP(8) = 42 */
#define MIN_CHECKSUM_PACKET_LEN 42

/* Reflection mode - what headers to swap */
typedef enum {
	REFLECT_MODE_MAC = 0,    /* Swap MAC addresses only */
	REFLECT_MODE_MAC_IP = 1, /* Swap MAC + IP addresses */
	REFLECT_MODE_ALL = 2     /* Swap MAC + IP + UDP ports (default) */
} reflect_mode_t;

/* Signature filter mode - which packet types to accept */
typedef enum {
	SIG_FILTER_ALL = 0,     /* Accept all known signatures (default) */
	SIG_FILTER_ITO = 1,     /* ITO only (PROBEOT, DATA:OT, LATENCY) */
	SIG_FILTER_RFC2544 = 2, /* RFC2544 only */
	SIG_FILTER_Y1564 = 3,   /* Y.1564 only */
	SIG_FILTER_CUSTOM = 4   /* Custom signatures only (RFC2544 + Y.1564) */
} sig_filter_t;

/* Packet signature types (for statistics) */
typedef enum {
	SIG_TYPE_PROBEOT = 0,  /* ITO: PROBEOT */
	SIG_TYPE_DATAOT = 1,   /* ITO: DATA:OT */
	SIG_TYPE_LATENCY = 2,  /* ITO: LATENCY */
	SIG_TYPE_RFC2544 = 3,  /* Custom: RFC2544 */
	SIG_TYPE_Y1564 = 4,    /* Custom: Y.1564 */
	SIG_TYPE_UNKNOWN = 5,
	SIG_TYPE_COUNT = 6
} sig_type_t;

/* Legacy alias for compatibility */
typedef sig_type_t ito_sig_type_t;
#define ITO_SIG_TYPE_PROBEOT SIG_TYPE_PROBEOT
#define ITO_SIG_TYPE_DATAOT SIG_TYPE_DATAOT
#define ITO_SIG_TYPE_LATENCY SIG_TYPE_LATENCY
#define ITO_SIG_TYPE_UNKNOWN SIG_TYPE_UNKNOWN
#define ITO_SIG_TYPE_COUNT SIG_TYPE_COUNT

/* Error category types */
typedef enum {
	ERR_RX_INVALID_MAC = 0,   /* Wrong destination MAC */
	ERR_RX_INVALID_ETHERTYPE, /* Not IPv4 */
	ERR_RX_INVALID_PROTOCOL,  /* Not UDP */
	ERR_RX_INVALID_SIGNATURE, /* No ITO signature */
	ERR_RX_TOO_SHORT,         /* Packet too short */
	ERR_TX_FAILED,            /* Transmission failed */
	ERR_RX_NOMEM,             /* Memory allocation failed */
	ERR_CATEGORY_COUNT
} error_category_t;

/* Latency statistics */
typedef struct {
	uint64_t count;    /* Number of measurements */
	uint64_t total_ns; /* Total latency in nanoseconds */
	uint64_t min_ns;   /* Minimum latency */
	uint64_t max_ns;   /* Maximum latency */
	double avg_ns;     /* Average latency */
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
	uint64_t sig_rfc2544_count;
	uint64_t sig_y1564_count;
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
	uint64_t rx_invalid;   /* Total validation failures */
	uint64_t rx_nomem;     /* Memory allocation failures */
	uint64_t tx_errors;    /* Transmission errors */
	uint64_t poll_timeout; /* Poll timeouts */

	/* Latency measurements */
	latency_stats_t latency;

	/* Performance metrics */
	double pps;  /* Packets per second (reflected) */
	double mbps; /* Megabits per second (reflected) */

	/* Timing */
	uint64_t start_time_ns;  /* Start timestamp */
	uint64_t last_update_ns; /* Last update timestamp */
} reflector_stats_t;

/* Statistics output format */
typedef enum {
	STATS_FORMAT_TEXT, /* Human-readable text format */
	STATS_FORMAT_JSON, /* Machine-readable JSON format */
	STATS_FORMAT_CSV   /* CSV format for logging */
} stats_format_t;

/* Configuration structure */
typedef struct {
	char ifname[MAX_IFNAME_LEN]; /* Interface name */
	int ifindex;                 /* Interface index */
	uint8_t mac[6];              /* Interface MAC address */
	int num_workers;             /* Number of worker threads */
	bool enable_stats;           /* Enable statistics collection */
	bool promiscuous;            /* Enable promiscuous mode */
	bool zero_copy;              /* Enable zero-copy mode (if supported) */
	int batch_size;              /* Packet batch size */
	int frame_size;              /* Frame size in UMEM */
	int num_frames;              /* Number of frames in UMEM */
	int queue_id;                /* RX/TX queue ID (-1 for auto) */
	bool busy_poll;              /* Enable busy polling */
	int poll_timeout_ms;         /* Poll timeout in milliseconds */
	bool measure_latency;        /* Enable latency measurements */
	stats_format_t stats_format; /* Statistics output format */
	int stats_interval_sec;      /* Statistics display interval (seconds) */
	int cpu_affinity;            /* CPU to pin worker thread (-1 for auto) */
	bool use_huge_pages;         /* Use huge pages for UMEM (Linux only) */
	bool software_checksum;      /* Calculate checksums in software (fallback) */

	/* DPDK options (Linux only, requires --dpdk flag) */
	bool use_dpdk;   /* Use DPDK instead of AF_XDP (100G mode) */
	char *dpdk_args; /* EAL arguments (e.g., "--lcores=1-4") */

	/* ITO packet filtering options */
	uint16_t ito_port; /* Required UDP port (default 3842, 0 = any) */
	bool filter_oui;   /* Filter by source MAC OUI (default true) */
	uint8_t oui[3];    /* Required OUI bytes (default 00:c0:17 NetAlly) */

	/* Reflection mode */
	reflect_mode_t reflect_mode; /* What to swap: MAC, MAC+IP, or ALL */

	/* Signature filter */
	sig_filter_t sig_filter; /* Which signatures to accept (default: ALL) */

	/* Protocol support */
	bool enable_ipv6; /* Enable IPv6 packet reflection (default: true) */
	bool enable_vlan; /* Enable VLAN-tagged packet handling (default: true) */
} reflector_config_t;

/* Packet descriptor */
typedef struct {
	uint8_t *data;      /* Packet data pointer */
	uint32_t len;       /* Packet length */
	uint64_t addr;      /* Buffer address (for zero-copy) */
	uint64_t timestamp; /* Receive timestamp (nanoseconds) */
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
	platform_ctx_t **platform_contexts; /* Array of per-worker contexts */
	worker_ctx_t *workers;
#ifdef __APPLE__
	dispatch_group_t worker_group;   /* GCD group for worker synchronization */
	dispatch_queue_t *worker_queues; /* Per-worker GCD queues */
#else
	pthread_t *worker_tids; /* Thread IDs for joining */
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
 *
 * @param rctx Reflector context to initialize (must be zeroed)
 * @param ifname Network interface name (e.g., "eth0", "en0")
 * @return 0 on success
 * @return -1 if interface not found (check errno for ENODEV)
 * @return -1 if failed to get MAC address (check errno)
 *
 * Example:
 * @code
 *   reflector_ctx_t rctx = {0};
 *   if (reflector_init(&rctx, "eth0") < 0) {
 *       perror("reflector_init");
 *       return 1;
 *   }
 *   // ... use reflector ...
 *   reflector_cleanup(&rctx);
 * @endcode
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
 *
 * @param rctx Initialized reflector context
 * @return 0 on success
 * @return -ENOMEM if memory allocation failed for workers or contexts
 * @return -1 if platform initialization failed (socket/BPF setup)
 * @return -1 if thread creation failed
 *
 * @note On Linux with AF_XDP, may fall back to AF_PACKET if XDP init fails
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
 * Detect NIC capabilities and print performance recommendations
 * Checks vendor/model, speed, and suggests DPDK if beneficial
 * @param ifname Interface name
 */
void print_nic_recommendations(const char *ifname);

/**
 * Check if DPDK libraries are available on the system
 * @return true if DPDK is installed, false otherwise
 */
bool is_dpdk_available(void);

/**
 * Get NIC vendor and device IDs from sysfs (Linux only)
 * @param ifname Interface name
 * @param vendor_id Output: PCI vendor ID
 * @param device_id Output: PCI device ID
 * @return 0 on success, -1 on error
 */
int get_nic_vendor(const char *ifname, uint16_t *vendor_id, uint16_t *device_id);

/**
 * Get NIC link speed in Mbps
 * @param ifname Interface name
 * @return Speed in Mbps, or -1 if unable to determine
 */
int get_nic_speed(const char *ifname);

/**
 * Print warning when falling back to AF_PACKET mode
 * Explains limitations and how to upgrade to AF_XDP/DPDK
 * @param ifname Interface name
 */
void print_af_packet_warning(const char *ifname);

/**
 * Print list of recommended NICs for high-performance scenarios
 */
void print_recommended_nics(void);

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

/**
 * Drop unnecessary privileges after initialization
 * On Linux: Drops to 'nobody' user if running as root
 * On macOS: No-op (BPF requires root or group access)
 * @return 0 on success, -1 on error (non-fatal warning on Linux)
 */
int drop_privileges(void);

/* ------------------------------------------------------------------------
 * Packet Validation and Reflection
 * ------------------------------------------------------------------------ */

/**
 * Validate if packet is an ITO test packet
 * Fast-path validation with branch prediction hints.
 * @param data Packet data buffer
 * @param len Packet length in bytes
 * @param config Reflector config (for port/OUI filtering)
 * @return true if valid ITO packet, false otherwise
 */
bool is_ito_packet(const uint8_t *data, uint32_t len, const reflector_config_t *config);

/**
 * Extended ITO packet validation with IPv6 and VLAN support
 * @param data Packet data buffer
 * @param len Packet length in bytes
 * @param config Reflector config
 * @param is_ipv6 Output: true if IPv6 packet
 * @param is_vlan Output: true if VLAN-tagged
 * @return true if valid ITO packet
 */
bool is_ito_packet_extended(const uint8_t *data, uint32_t len, const reflector_config_t *config,
                            bool *is_ipv6, bool *is_vlan);

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

/**
 * Reflect packet with configurable mode and optional checksum
 *
 * @param data Packet data buffer (will be modified)
 * @param len Packet length in bytes
 * @param mode Reflection mode (MAC, MAC+IP, or ALL)
 * @param software_checksum Whether to recalculate checksums in software
 */
void reflect_packet_with_mode(uint8_t *data, uint32_t len, reflect_mode_t mode,
                              bool software_checksum);

/**
 * Reflect IPv6 packet in-place
 * Swaps MAC addresses, IPv6 addresses, and UDP ports
 * @param data Packet data buffer (will be modified)
 * @param len Packet length in bytes
 * @param mode Reflection mode (MAC, MAC+IP, or ALL)
 * @param software_checksum Whether to recalculate checksums
 */
void reflect_packet_ipv6(uint8_t *data, uint32_t len, reflect_mode_t mode,
                         bool software_checksum);

/**
 * Check if packet has VLAN tag and get inner EtherType
 * @param data Packet data buffer
 * @param len Packet length
 * @param inner_ethertype Output: inner EtherType (IPv4/IPv6)
 * @param vlan_offset Output: offset where IP header starts (after VLAN)
 * @return true if VLAN-tagged, false otherwise
 */
bool is_vlan_tagged(const uint8_t *data, uint32_t len, uint16_t *inner_ethertype,
                    uint32_t *vlan_offset);

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
const platform_ops_t *get_platform_ops(void);

/* Logging */
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;

void reflector_log(log_level_t level, const char *fmt, ...);
void reflector_set_log_level(log_level_t level);

#endif /* REFLECTOR_H */
