/*
 * packet.c - Common packet validation and reflection logic
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This module provides platform-agnostic packet inspection and reflection
 * logic for ITO (Integrated Test & Optimization) packets.
 */

#include "reflector.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#ifdef __linux__
#include <netinet/in.h> /* IPPROTO_UDP */
#endif

/* SIMD support for x86_64 architectures */
#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
#include <emmintrin.h> /* SSE2 */
#include <pmmintrin.h> /* SSE3 */

/* CPU feature detection flags */
static int cpu_has_sse2 = 0;
static int cpu_has_sse3 = 0;
static pthread_once_t cpu_detect_once = PTHREAD_ONCE_INIT;

/*
 * Detect CPU features at runtime
 */
static void detect_cpu_features(void)
{
	unsigned int eax, ebx, ecx, edx;

	/* Check for SSE2 (CPUID.01H:EDX.SSE2[bit 26]) */
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		cpu_has_sse2 = (edx & (1 << 26)) ? 1 : 0;
		cpu_has_sse3 = (ecx & (1 << 0)) ? 1 : 0;
	} else {
		cpu_has_sse2 = 0;
		cpu_has_sse3 = 0;
	}

	/* Log which implementation (runs only once) */
	if (cpu_has_sse2) {
		reflector_log(LOG_INFO, "SIMD: x86_64 SSE2 enabled");
	} else {
		reflector_log(LOG_INFO, "Using scalar packet reflection (SSE2 not available)");
	}
}
#endif /* __x86_64__ */

/* SIMD support for ARM64/AArch64 architectures (Apple Silicon, AWS Graviton, etc.) */
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>

/* ARM64 always has NEON, no runtime detection needed */
static int cpu_has_neon __attribute__((unused)) = 1;
#endif /* __aarch64__ */

/*
 * Fast path packet validation for ITO packets
 *
 * Optimized with branch prediction hints and minimal validation overhead.
 * Checks (in order of increasing cost):
 * 1. Length check (54 bytes minimum) - LIKELY to pass
 * 2. Destination MAC match - LIKELY to fail (most traffic)
 * 3. Source MAC OUI check (optional) - Filter by vendor (e.g., NetAlly 00:c0:17)
 * 4. EtherType = IPv4 (0x0800) - LIKELY to pass if MAC matched
 * 5. IP Protocol = UDP (0x11) - LIKELY to pass
 * 6. UDP port check (optional) - Filter by port (e.g., 3842)
 * 7. ITO signature match - LIKELY to pass if UDP
 *
 * Returns: true if packet should be reflected, false otherwise
 */
ALWAYS_INLINE bool is_ito_packet(const uint8_t *data, uint32_t len,
                                 const reflector_config_t *config)
{
	static _Thread_local int debug_count = 0;

	/* Prefetch packet data for upcoming checks */
	PREFETCH_READ(data);
	PREFETCH_READ(data + 64); /* Prefetch UDP header area */

	/* Fast rejection: minimum length check - LIKELY to pass */
	if (unlikely(len < MIN_ITO_PACKET_LEN)) {
		if (unlikely(debug_count++ < 3)) {
			DEBUG_LOG("Packet too short: %u bytes (need %d)", len, MIN_ITO_PACKET_LEN);
		}
		return false;
	}

	/* Check destination MAC matches our interface - UNLIKELY to match (filters most traffic) */
	if (unlikely(memcmp(&data[ETH_DST_OFFSET], config->mac, 6) != 0)) {
		if (unlikely(debug_count++ < 3)) {
			DEBUG_LOG("MAC mismatch: got %02x:%02x:%02x:%02x:%02x:%02x, want "
			          "%02x:%02x:%02x:%02x:%02x:%02x",
			          data[0], data[1], data[2], data[3], data[4], data[5], config->mac[0],
			          config->mac[1], config->mac[2], config->mac[3], config->mac[4],
			          config->mac[5]);
		}
		return false;
	}

	/* Check source MAC OUI if filtering enabled (default: NetAlly 00:c0:17) */
	if (config->filter_oui) {
		if (unlikely(data[ETH_SRC_OFFSET] != config->oui[0] ||
		             data[ETH_SRC_OFFSET + 1] != config->oui[1] ||
		             data[ETH_SRC_OFFSET + 2] != config->oui[2])) {
			if (unlikely(debug_count++ < 3)) {
				DEBUG_LOG("OUI mismatch: got %02x:%02x:%02x, want %02x:%02x:%02x",
				          data[ETH_SRC_OFFSET], data[ETH_SRC_OFFSET + 1], data[ETH_SRC_OFFSET + 2],
				          config->oui[0], config->oui[1], config->oui[2]);
			}
			return false;
		}
	}

	/* Check EtherType = IPv4 (0x0800) - LIKELY to be IPv4 at this point */
	uint16_t ethertype = (data[ETH_TYPE_OFFSET] << 8) | data[ETH_TYPE_OFFSET + 1];
	if (unlikely(ethertype != ETH_P_IP)) {
		if (unlikely(debug_count++ < 3)) {
			DEBUG_LOG("Not IPv4: ethertype=0x%04x", ethertype);
		}
		return false;
	}

	/* Check IP version and header length - LIKELY to be valid IPv4 */
	uint8_t ver_ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET];
	uint8_t version = ver_ihl >> 4;
	uint8_t ihl = ver_ihl & 0x0F;

	if (unlikely(version != 4 || ihl < 5)) {
		if (unlikely(debug_count++ < 3)) {
			DEBUG_LOG("Bad IP: version=%u, ihl=%u", version, ihl);
		}
		return false;
	}

	/* Check IP protocol = UDP - LIKELY to be UDP at this point */
	uint8_t ip_proto = data[ETH_HDR_LEN + IP_PROTO_OFFSET];
	if (unlikely(ip_proto != IPPROTO_UDP)) {
		if (unlikely(debug_count++ < 3)) {
			DEBUG_LOG("Not UDP: protocol=%u", ip_proto);
		}
		return false;
	}

	/* Calculate UDP payload offset */
	uint32_t ip_hdr_len = ihl * 4;
	uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
	uint32_t udp_payload_offset = udp_offset + UDP_HDR_LEN;

	/* Check destination UDP port if filtering enabled (default: 3842) */
	if (config->ito_port != 0) {
		uint16_t dst_port = (data[udp_offset + UDP_DST_PORT_OFFSET] << 8) |
		                    data[udp_offset + UDP_DST_PORT_OFFSET + 1];
		if (unlikely(dst_port != config->ito_port)) {
			if (unlikely(debug_count++ < 3)) {
				DEBUG_LOG("Port mismatch: got %u, want %u", dst_port, config->ito_port);
			}
			return false;
		}
	}

	/* Ensure we have enough data for signature - LIKELY to have enough */
	if (unlikely(len < udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN)) {
		if (unlikely(debug_count++ < 3)) {
			DEBUG_LOG("Too short for signature: len=%u, need=%u", len,
			          udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN);
		}
		return false;
	}

	/* Check for signatures - LIKELY to match at this point */
	const uint8_t *sig = &data[udp_payload_offset + ITO_SIG_OFFSET];

	if (unlikely(debug_count++ < 3)) {
		/* Ensure buffer is large enough for signature + null terminator */
		_Static_assert(ITO_SIG_LEN <= 7, "sig_str buffer too small for ITO_SIG_LEN");
		char sig_str[ITO_SIG_LEN + 1];
		memcpy(sig_str, sig, ITO_SIG_LEN);
		sig_str[ITO_SIG_LEN] = '\0';
		DEBUG_LOG("UDP payload signature: '%s'", sig_str);
	}

	/* Check signatures based on filter mode */
	sig_filter_t filter = config->sig_filter;

	/* ITO signatures (NetAlly/Fluke/NETSCOUT) */
	if (filter == SIG_FILTER_ALL || filter == SIG_FILTER_ITO) {
		if (memcmp(sig, ITO_SIG_PROBEOT, ITO_SIG_LEN) == 0 ||
		    memcmp(sig, ITO_SIG_DATAOT, ITO_SIG_LEN) == 0 ||
		    memcmp(sig, ITO_SIG_LATENCY, ITO_SIG_LEN) == 0) {
			DEBUG_LOG("ITO packet matched! len=%u", len);
			return true;
		}
	}

	/* Custom signatures (RFC2544/Y.1564 tester) */
	if (filter == SIG_FILTER_ALL || filter == SIG_FILTER_CUSTOM ||
	    filter == SIG_FILTER_RFC2544) {
		if (memcmp(sig, CUSTOM_SIG_RFC2544, CUSTOM_SIG_LEN) == 0) {
			DEBUG_LOG("RFC2544 packet matched! len=%u", len);
			return true;
		}
	}

	if (filter == SIG_FILTER_ALL || filter == SIG_FILTER_CUSTOM ||
	    filter == SIG_FILTER_Y1564) {
		if (memcmp(sig, CUSTOM_SIG_Y1564, CUSTOM_SIG_LEN) == 0) {
			DEBUG_LOG("Y.1564 packet matched! len=%u", len);
			return true;
		}
	}

	return false;
}

#if defined(__x86_64__) || defined(_M_X64)
/*
 * SIMD-optimized packet reflection using SSE2 instructions
 *
 * Uses 128-bit SIMD operations to swap headers in parallel:
 * - Load 16 bytes at a time into SIMD registers
 * - Perform parallel swaps using shuffle/blend operations
 * - Store results back in fewer memory operations
 *
 * Expected performance gain: 2-3% over scalar version
 */
static ALWAYS_INLINE void reflect_packet_inplace_simd(uint8_t *data, uint32_t len)
{
	(void)len;

	/* Prefetch areas we'll modify */
	PREFETCH_WRITE(data);
	PREFETCH_WRITE(data + 32);

	/*
	 * Ethernet header layout (14 bytes):
	 * [0-5]  dst MAC
	 * [6-11] src MAC
	 * [12-13] EtherType
	 *
	 * Load first 16 bytes (covers full Ethernet header + 2 bytes of IP)
	 * We'll swap MAC addresses using SIMD shuffle
	 */
	__m128i eth_header = _mm_loadu_si128((__m128i *)data);

	/* Create shuffle mask to swap src/dst MAC (6 bytes each)
	 * Original: [dst0-5][src0-5][type0-1][ip0-1]
	 * Target:   [src0-5][dst0-5][type0-1][ip0-1]
	 * Shuffle:  6,7,8,9,10,11, 0,1,2,3,4,5, 12,13,14,15
	 */
	__m128i mac_shuffle =
	    _mm_set_epi8(15, 14, 13, 12,    /* Keep last 4 bytes (EtherType + IP start) */
	                 5, 4, 3, 2, 1, 0,  /* Original dst MAC -> new src MAC */
	                 11, 10, 9, 8, 7, 6 /* Original src MAC -> new dst MAC */
	    );

	eth_header = _mm_shuffle_epi8(eth_header, mac_shuffle);
	_mm_storeu_si128((__m128i *)data, eth_header);

	/* Get IP header length to find UDP header */
	uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
	uint32_t ip_hdr_len = ihl * 4;

	/*
	 * Swap IP addresses using aligned 32-bit operations
	 * IP header is at offset 14 (after Ethernet)
	 */
	uint32_t ip_offset = ETH_HDR_LEN;

	/* Load 16 bytes starting at IP source address (covers src IP, dst IP, and more)
	 * IP src is at offset 12 in IP header, dst at offset 16
	 */
	__m128i ip_block = _mm_loadu_si128((__m128i *)&data[ip_offset + IP_SRC_OFFSET]);

	/* Shuffle to swap 32-bit IP addresses
	 * Bytes [0-3] = src IP, [4-7] = dst IP
	 * We want to swap these two 32-bit values
	 */
	__m128i ip_shuffle = _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, /* Keep bytes 8-15 unchanged */
	                                  3, 2, 1, 0, /* Original src IP -> position of dst */
	                                  7, 6, 5, 4  /* Original dst IP -> position of src */
	);

	ip_block = _mm_shuffle_epi8(ip_block, ip_shuffle);
	_mm_storeu_si128((__m128i *)&data[ip_offset + IP_SRC_OFFSET], ip_block);

	/*
	 * Swap UDP ports using 32-bit operation (load both ports, swap, store)
	 * This is faster than two separate 16-bit operations
	 */
	uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
	uint32_t *ports = (uint32_t *)&data[udp_offset];
	uint32_t port_pair = *ports;

	/* Swap the two 16-bit halves using rotate */
	*ports = (port_pair >> 16) | (port_pair << 16);
}
#endif /* __x86_64__ */

#if defined(__aarch64__) || defined(__ARM_NEON)
/*
 * NEON-optimized packet reflection for ARM64 (Apple Silicon, AWS Graviton)
 *
 * Uses 128-bit NEON SIMD operations to swap headers in parallel.
 * NEON is ARM's SIMD instruction set, equivalent to SSE/AVX on x86.
 *
 * Expected performance gain: 2-3% over scalar version on ARM64
 */
static ALWAYS_INLINE void reflect_packet_inplace_neon(uint8_t *data, uint32_t len)
{
	(void)len;

	/* Prefetch areas we'll modify */
	PREFETCH_WRITE(data);
	PREFETCH_WRITE(data + 32);

	/*
	 * Ethernet header: Swap MAC addresses using NEON
	 * Load 16 bytes (covers both MAC addresses + EtherType)
	 */
	uint8x16_t eth_header = vld1q_u8(data);

	/* Create shuffle indices to swap src/dst MAC
	 * Original: [dst0-5][src0-5][type0-1][extra0-1]
	 * Target:   [src0-5][dst0-5][type0-1][extra0-1]
	 * Indices:  6,7,8,9,10,11, 0,1,2,3,4,5, 12,13,14,15
	 */
	const uint8_t shuffle_indices[16] = {
	    6,  7,  8,  9, 10, 11, /* src MAC -> dst position */
	    0,  1,  2,  3, 4,  5,  /* dst MAC -> src position */
	    12, 13, 14, 15         /* Keep EtherType and padding */
	};
	uint8x16_t shuffle_mask = vld1q_u8(shuffle_indices);

	/* Perform shuffle (vqtbl1q_u8 is the NEON shuffle instruction) */
	eth_header = vqtbl1q_u8(eth_header, shuffle_mask);

	/* Store back */
	vst1q_u8(data, eth_header);

	/* Get IP header length */
	uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
	uint32_t ip_hdr_len = ihl * 4;

	/*
	 * Swap IP addresses using NEON 32-bit operations
	 */
	uint32_t ip_offset = ETH_HDR_LEN;

	/* Load IP source and destination as 32-bit values */
	uint32x2_t ip_addrs = vld1_u32((uint32_t *)&data[ip_offset + IP_SRC_OFFSET]);

	/* Reverse the two 32-bit values (swap src and dst) */
	ip_addrs = vrev64_u32(ip_addrs);

	/* Store back */
	vst1_u32((uint32_t *)&data[ip_offset + IP_SRC_OFFSET], ip_addrs);

	/*
	 * Swap UDP ports using 32-bit operation
	 * Load both ports as one 32-bit value, then swap the halves
	 */
	uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
	uint32_t *ports = (uint32_t *)&data[udp_offset];
	uint32_t port_pair = *ports;

	/* Rotate 16 bits to swap the two 16-bit port values */
	*ports = (port_pair >> 16) | (port_pair << 16);
}
#endif /* __aarch64__ */

/*
 * Scalar (non-SIMD) packet reflection - fallback for all platforms
 *
 * This function performs zero-copy reflection by modifying the packet
 * buffer directly. It swaps:
 * - Ethernet: src <-> dst MAC (6 bytes)
 * - IPv4: src <-> dst IP (4 bytes)
 * - UDP: src <-> dst port (2 bytes)
 *
 * Assumes packet has been validated by is_ito_packet()
 * Optimized with direct integer swaps and prefetching
 */
__attribute__((unused)) static ALWAYS_INLINE void reflect_packet_inplace_scalar(uint8_t *data,
                                                                                uint32_t len)
{
	(void)len; /* Length not needed for in-place swapping */

	/* Prefetch areas we'll modify */
	PREFETCH_WRITE(data);
	PREFETCH_WRITE(data + 32);

	/* Swap Ethernet MAC addresses - use 64-bit aligned access */
	uint64_t temp_mac;
	memcpy(&temp_mac, &data[ETH_DST_OFFSET], 6);
	memcpy(&data[ETH_DST_OFFSET], &data[ETH_SRC_OFFSET], 6);
	memcpy(&data[ETH_SRC_OFFSET], &temp_mac, 6);

	/* Get IP header length */
	uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
	uint32_t ip_hdr_len = ihl * 4;

	/* Swap IP addresses (4 bytes each) - use memcpy for alignment safety */
	uint32_t ip_offset = ETH_HDR_LEN;
	uint32_t ip_src_val, ip_dst_val;
	memcpy(&ip_src_val, &data[ip_offset + IP_SRC_OFFSET], 4);
	memcpy(&ip_dst_val, &data[ip_offset + IP_DST_OFFSET], 4);
	memcpy(&data[ip_offset + IP_SRC_OFFSET], &ip_dst_val, 4);
	memcpy(&data[ip_offset + IP_DST_OFFSET], &ip_src_val, 4);

	/* Swap UDP ports (2 bytes each) - use memcpy for alignment safety */
	uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
	uint16_t udp_src_val, udp_dst_val;
	memcpy(&udp_src_val, &data[udp_offset + UDP_SRC_PORT_OFFSET], 2);
	memcpy(&udp_dst_val, &data[udp_offset + UDP_DST_PORT_OFFSET], 2);
	memcpy(&data[udp_offset + UDP_SRC_PORT_OFFSET], &udp_dst_val, 2);
	memcpy(&data[udp_offset + UDP_DST_PORT_OFFSET], &udp_src_val, 2);

	/* Note: Checksums are typically handled by NIC offload or ignored by test tools */
}

/*
 * Calculate IP header checksum (RFC 791)
 * Standard internet checksum algorithm for software fallback
 */
static uint16_t calculate_ip_checksum(const uint8_t *iph, uint32_t ihl_bytes)
{
	uint32_t sum = 0;
	uint32_t count = ihl_bytes;

	/* Sum all 16-bit words, skipping checksum field */
	/* Use memcpy for safe unaligned access */
	for (uint32_t i = 0; i < count / 2; i++) {
		if (i != 5) { /* Skip checksum field at offset 10 (word 5) */
			uint16_t word;
			memcpy(&word, iph + i * 2, sizeof(word));
			sum += ntohs(word);
		}
	}

	/* Fold 32-bit sum to 16 bits */
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return htons((uint16_t)~sum);
}

/*
 * Calculate UDP checksum (RFC 768)
 * Uses IP pseudo-header + UDP header + data
 */
static uint16_t calculate_udp_checksum(const uint8_t *iph, const uint8_t *udph, uint32_t udp_len)
{
	uint32_t sum = 0;

	/* IP pseudo-header for UDP checksum */
	const uint16_t *src_ip = (const uint16_t *)(iph + 12);
	const uint16_t *dst_ip = (const uint16_t *)(iph + 16);

	/* Sum source IP (2 words) */
	sum += ntohs(src_ip[0]);
	sum += ntohs(src_ip[1]);

	/* Sum destination IP (2 words) */
	sum += ntohs(dst_ip[0]);
	sum += ntohs(dst_ip[1]);

	/* Sum protocol (UDP) + UDP length */
	sum += IPPROTO_UDP;
	sum += udp_len;

	/* Sum UDP header + data (skip checksum field at offset 6) */
	const uint16_t *ptr = (const uint16_t *)udph;
	for (uint32_t i = 0; i < udp_len / 2; i++) {
		if (i != 3) { /* Skip UDP checksum field at offset 6 (word 3) */
			sum += ntohs(ptr[i]);
		}
	}

	/* Handle odd byte */
	if (udp_len & 1) {
		sum += (uint16_t)(udph[udp_len - 1]) << 8;
	}

	/* Fold 32-bit sum to 16 bits */
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	/* UDP checksum 0 means no checksum, use 0xFFFF instead */
	uint16_t checksum = (uint16_t)~sum;
	return checksum == 0 ? htons(0xFFFF) : htons(checksum);
}

/*
 * Main packet reflection function with runtime SIMD dispatch
 *
 * Automatically detects CPU capabilities and uses the fastest available
 * implementation:
 * - x86_64: SSE2/SSE3 SIMD
 * - ARM64: NEON SIMD
 * - Others: Optimized scalar
 *
 * Note: Does NOT calculate checksums - use reflect_packet_with_checksum()
 * if software checksum calculation is needed.
 */
ALWAYS_INLINE void reflect_packet_inplace(uint8_t *data, uint32_t len)
{
#if defined(__x86_64__) || defined(_M_X64)
	/* Thread-safe CPU feature detection (once only) */
	pthread_once(&cpu_detect_once, detect_cpu_features);

	/* Dispatch to SIMD or scalar version */
	if (likely(cpu_has_sse2)) {
		reflect_packet_inplace_simd(data, len);
	} else {
		reflect_packet_inplace_scalar(data, len);
	}

#elif defined(__aarch64__) || defined(__ARM_NEON)
	/* ARM64: NEON is always available, no runtime detection needed */
	static int logged = 0;
	if (unlikely(!logged)) {
		reflector_log(LOG_INFO, "Using SIMD packet reflection (ARM64 NEON)");
		logged = 1;
	}

	reflect_packet_inplace_neon(data, len);

#else
	/* Other architectures: Use optimized scalar version */
	static int logged = 0;
	if (unlikely(!logged)) {
		reflector_log(LOG_INFO, "Using scalar packet reflection (no SIMD)");
		logged = 1;
	}

	reflect_packet_inplace_scalar(data, len);
#endif
}

/*
 * Reflect packet with optional software checksum calculation
 *
 * Performs packet reflection and recalculates IP/UDP checksums if
 * software_checksum is enabled. Use this instead of reflect_packet_inplace()
 * when NIC checksum offload is unavailable or unreliable.
 */
void reflect_packet_with_checksum(uint8_t *data, uint32_t len, bool software_checksum)
{
	/* Perform SIMD/scalar packet reflection */
	reflect_packet_inplace(data, len);

	/* Recalculate checksums if software fallback enabled */
	if (software_checksum && len >= MIN_CHECKSUM_PACKET_LEN) {
		uint8_t *iph = data + ETH_HDR_LEN;
		uint8_t ihl = (iph[0] & 0x0F) * 4; /* IP header length in bytes */

		if (ihl >= IP_HDR_MIN_LEN && len >= (uint32_t)(ETH_HDR_LEN + ihl + UDP_HDR_LEN)) {
			/* Recalculate IP checksum */
			uint16_t *ip_check = (uint16_t *)(iph + 10);
			*ip_check = 0; /* Clear before calculation */
			*ip_check = calculate_ip_checksum(iph, ihl);

			/* Recalculate UDP checksum */
			uint8_t *udph = iph + ihl;
			uint16_t udp_len = ntohs(*(uint16_t *)(udph + 4));

			if (len >= (uint32_t)(ETH_HDR_LEN + ihl + udp_len)) {
				uint16_t *udp_check = (uint16_t *)(udph + 6);
				*udp_check = 0; /* Clear before calculation */
				*udp_check = calculate_udp_checksum(iph, udph, udp_len);
			}
		}
	}
}

/*
 * Reflect packet with configurable mode and optional checksum
 *
 * Supports three reflection modes:
 * - REFLECT_MODE_MAC: Swap Ethernet MAC addresses only
 * - REFLECT_MODE_MAC_IP: Swap MAC + IP addresses
 * - REFLECT_MODE_ALL: Swap MAC + IP + UDP ports (default, full reflection)
 */
void reflect_packet_with_mode(uint8_t *data, uint32_t len, reflect_mode_t mode,
                              bool software_checksum)
{
	/* All modes require at least Ethernet header */
	if (len < ETH_HDR_LEN) {
		return;
	}

	/* Prefetch areas we'll modify */
	PREFETCH_WRITE(data);

	/* Swap Ethernet MAC addresses (all modes do this) */
	uint64_t temp_mac;
	memcpy(&temp_mac, &data[ETH_DST_OFFSET], 6);
	memcpy(&data[ETH_DST_OFFSET], &data[ETH_SRC_OFFSET], 6);
	memcpy(&data[ETH_SRC_OFFSET], &temp_mac, 6);

	if (mode == REFLECT_MODE_MAC) {
		/* MAC-only mode: done */
		return;
	}

	/* For MAC+IP and ALL modes, need IP header */
	if (len < ETH_HDR_LEN + IP_HDR_MIN_LEN) {
		return;
	}

	/* Get IP header length */
	uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
	uint32_t ip_hdr_len = ihl * 4;

	if (ip_hdr_len < IP_HDR_MIN_LEN || len < ETH_HDR_LEN + ip_hdr_len) {
		return;
	}

	/* Swap IP addresses */
	uint32_t ip_offset = ETH_HDR_LEN;
	uint32_t ip_src_val, ip_dst_val;
	memcpy(&ip_src_val, &data[ip_offset + IP_SRC_OFFSET], 4);
	memcpy(&ip_dst_val, &data[ip_offset + IP_DST_OFFSET], 4);
	memcpy(&data[ip_offset + IP_SRC_OFFSET], &ip_dst_val, 4);
	memcpy(&data[ip_offset + IP_DST_OFFSET], &ip_src_val, 4);

	if (mode == REFLECT_MODE_MAC_IP) {
		/* MAC+IP mode: recalculate IP checksum if needed, then done */
		if (software_checksum) {
			uint8_t *iph = data + ETH_HDR_LEN;
			uint16_t *ip_check = (uint16_t *)(iph + 10);
			*ip_check = 0;
			*ip_check = calculate_ip_checksum(iph, ip_hdr_len);
		}
		return;
	}

	/* REFLECT_MODE_ALL: Also swap UDP ports */
	if (len < ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN) {
		return;
	}

	uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
	uint16_t udp_src_val, udp_dst_val;
	memcpy(&udp_src_val, &data[udp_offset + UDP_SRC_PORT_OFFSET], 2);
	memcpy(&udp_dst_val, &data[udp_offset + UDP_DST_PORT_OFFSET], 2);
	memcpy(&data[udp_offset + UDP_SRC_PORT_OFFSET], &udp_dst_val, 2);
	memcpy(&data[udp_offset + UDP_DST_PORT_OFFSET], &udp_src_val, 2);

	/* Recalculate checksums if software fallback enabled */
	if (software_checksum && len >= MIN_CHECKSUM_PACKET_LEN) {
		uint8_t *iph = data + ETH_HDR_LEN;

		/* Recalculate IP checksum */
		uint16_t *ip_check = (uint16_t *)(iph + 10);
		*ip_check = 0;
		*ip_check = calculate_ip_checksum(iph, ip_hdr_len);

		/* Recalculate UDP checksum */
		uint8_t *udph = iph + ip_hdr_len;
		uint16_t udp_len = ntohs(*(uint16_t *)(udph + 4));

		if (len >= ETH_HDR_LEN + ip_hdr_len + udp_len) {
			uint16_t *udp_check = (uint16_t *)(udph + 6);
			*udp_check = 0;
			*udp_check = calculate_udp_checksum(iph, udph, udp_len);
		}
	}
}

/*
 * Alternative: Reflect packet with copy
 *
 * For platforms that can't do in-place modification, this creates a new
 * reflected packet. Caller must provide destination buffer.
 */
void reflect_packet_copy(const uint8_t *src, uint8_t *dst, uint32_t len)
{
	/* Copy entire packet first */
	memcpy(dst, src, len);

	/* Then do in-place reflection on the copy */
	reflect_packet_inplace(dst, len);
}

/*
 * Get ITO signature type from packet
 *
 * Returns the specific ITO signature type for statistics tracking.
 * Assumes packet has been validated with is_ito_packet()
 */
sig_type_t get_ito_signature_type(const uint8_t *data, uint32_t len)
{
	/* Calculate UDP payload offset */
	uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
	uint32_t ip_hdr_len = ihl * 4;
	uint32_t udp_payload_offset = ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN;

	/* Safety check */
	if (len < udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN) {
		return SIG_TYPE_UNKNOWN;
	}

	const uint8_t *sig = &data[udp_payload_offset + ITO_SIG_OFFSET];

	/* ITO signatures (NetAlly/Fluke/NETSCOUT) */
	if (memcmp(sig, ITO_SIG_PROBEOT, ITO_SIG_LEN) == 0) {
		return SIG_TYPE_PROBEOT;
	} else if (memcmp(sig, ITO_SIG_DATAOT, ITO_SIG_LEN) == 0) {
		return SIG_TYPE_DATAOT;
	} else if (memcmp(sig, ITO_SIG_LATENCY, ITO_SIG_LEN) == 0) {
		return SIG_TYPE_LATENCY;
	}

	/* Custom signatures (RFC2544/Y.1564 tester) */
	if (memcmp(sig, CUSTOM_SIG_RFC2544, CUSTOM_SIG_LEN) == 0) {
		return SIG_TYPE_RFC2544;
	} else if (memcmp(sig, CUSTOM_SIG_Y1564, CUSTOM_SIG_LEN) == 0) {
		return SIG_TYPE_Y1564;
	}

	return SIG_TYPE_UNKNOWN;
}

/*
 * Update per-signature statistics (inlined for performance)
 */
ALWAYS_INLINE void update_signature_stats(reflector_stats_t *stats, sig_type_t sig_type)
{
	switch (sig_type) {
	case SIG_TYPE_PROBEOT:
		stats->sig_probeot_count++;
		break;
	case SIG_TYPE_DATAOT:
		stats->sig_dataot_count++;
		break;
	case SIG_TYPE_LATENCY:
		stats->sig_latency_count++;
		break;
	case SIG_TYPE_RFC2544:
		stats->sig_rfc2544_count++;
		break;
	case SIG_TYPE_Y1564:
		stats->sig_y1564_count++;
		break;
	case SIG_TYPE_UNKNOWN:
	default:
		stats->sig_unknown_count++;
		break;
	}
}

/*
 * Update latency statistics (inlined for performance)
 */
ALWAYS_INLINE void update_latency_stats(latency_stats_t *latency, uint64_t latency_ns)
{
	latency->count++;
	latency->total_ns += latency_ns;

	if (unlikely(latency->count == 1)) {
		latency->min_ns = latency_ns;
		latency->max_ns = latency_ns;
	} else {
		if (unlikely(latency_ns < latency->min_ns))
			latency->min_ns = latency_ns;
		if (unlikely(latency_ns > latency->max_ns))
			latency->max_ns = latency_ns;
	}

	latency->avg_ns = (double)latency->total_ns / (double)latency->count;
}

/*
 * Update error statistics by category (inlined for performance)
 */
ALWAYS_INLINE void update_error_stats(reflector_stats_t *stats, error_category_t err_cat)
{
	switch (err_cat) {
	case ERR_RX_INVALID_MAC:
		stats->err_invalid_mac++;
		break;
	case ERR_RX_INVALID_ETHERTYPE:
		stats->err_invalid_ethertype++;
		break;
	case ERR_RX_INVALID_PROTOCOL:
		stats->err_invalid_protocol++;
		break;
	case ERR_RX_INVALID_SIGNATURE:
		stats->err_invalid_signature++;
		break;
	case ERR_RX_TOO_SHORT:
		stats->err_too_short++;
		break;
	case ERR_TX_FAILED:
		stats->err_tx_failed++;
		stats->tx_errors++; /* Update legacy counter */
		break;
	case ERR_RX_NOMEM:
		stats->err_nomem++;
		stats->rx_nomem++; /* Update legacy counter */
		break;
	default:
		break;
	}

	/* Update legacy rx_invalid counter */
	if (likely(err_cat >= ERR_RX_INVALID_MAC && err_cat <= ERR_RX_TOO_SHORT)) {
		stats->rx_invalid++;
	}
}

/*
 * Print statistics in JSON format
 */
void reflector_print_stats_json(const reflector_stats_t *stats)
{
	printf("{\n");
	printf("  \"packets\": {\n");
	printf("    \"received\": %" PRIu64 ",\n", stats->packets_received);
	printf("    \"reflected\": %" PRIu64 ",\n", stats->packets_reflected);
	printf("    \"dropped\": %" PRIu64 "\n", stats->packets_dropped);
	printf("  },\n");
	printf("  \"bytes\": {\n");
	printf("    \"received\": %" PRIu64 ",\n", stats->bytes_received);
	printf("    \"reflected\": %" PRIu64 "\n", stats->bytes_reflected);
	printf("  },\n");
	printf("  \"signatures\": {\n");
	printf("    \"probeot\": %" PRIu64 ",\n", stats->sig_probeot_count);
	printf("    \"dataot\": %" PRIu64 ",\n", stats->sig_dataot_count);
	printf("    \"latency\": %" PRIu64 ",\n", stats->sig_latency_count);
	printf("    \"unknown\": %" PRIu64 "\n", stats->sig_unknown_count);
	printf("  },\n");
	printf("  \"errors\": {\n");
	printf("    \"invalid_mac\": %" PRIu64 ",\n", stats->err_invalid_mac);
	printf("    \"invalid_ethertype\": %" PRIu64 ",\n", stats->err_invalid_ethertype);
	printf("    \"invalid_protocol\": %" PRIu64 ",\n", stats->err_invalid_protocol);
	printf("    \"invalid_signature\": %" PRIu64 ",\n", stats->err_invalid_signature);
	printf("    \"too_short\": %" PRIu64 ",\n", stats->err_too_short);
	printf("    \"tx_failed\": %" PRIu64 ",\n", stats->err_tx_failed);
	printf("    \"no_memory\": %" PRIu64 "\n", stats->err_nomem);
	printf("  },\n");
	printf("  \"latency\": {\n");
	printf("    \"count\": %" PRIu64 ",\n", stats->latency.count);
	printf("    \"min_ns\": %" PRIu64 ",\n", stats->latency.min_ns);
	printf("    \"max_ns\": %" PRIu64 ",\n", stats->latency.max_ns);
	printf("    \"avg_ns\": %.2f,\n", stats->latency.avg_ns);
	printf("    \"min_us\": %.2f,\n", stats->latency.min_ns / 1000.0);
	printf("    \"max_us\": %.2f,\n", stats->latency.max_ns / 1000.0);
	printf("    \"avg_us\": %.2f\n", stats->latency.avg_ns / 1000.0);
	printf("  },\n");
	printf("  \"performance\": {\n");
	printf("    \"pps\": %.2f,\n", stats->pps);
	printf("    \"mbps\": %.2f\n", stats->mbps);
	printf("  }\n");
	printf("}\n");
}

/*
 * Print statistics in CSV format
 */
void reflector_print_stats_csv(const reflector_stats_t *stats)
{
	printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",", stats->packets_received,
	       stats->packets_reflected, stats->packets_dropped, stats->bytes_received,
	       stats->bytes_reflected);

	printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",", stats->sig_probeot_count,
	       stats->sig_dataot_count, stats->sig_latency_count, stats->sig_unknown_count);

	printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
	       stats->err_invalid_mac, stats->err_invalid_ethertype, stats->err_invalid_protocol,
	       stats->err_invalid_signature, stats->err_too_short, stats->err_tx_failed,
	       stats->err_nomem);

	printf("%" PRIu64 ",%.2f,%.2f,%.2f,", stats->latency.count, stats->latency.min_ns / 1000.0,
	       stats->latency.max_ns / 1000.0, stats->latency.avg_ns / 1000.0);

	printf("%.2f,%.2f\n", stats->pps, stats->mbps);
}

/*
 * Print statistics (dispatcher based on format)
 */
void reflector_print_stats_formatted(const reflector_stats_t *stats, stats_format_t format)
{
	switch (format) {
	case STATS_FORMAT_JSON:
		reflector_print_stats_json(stats);
		break;
	case STATS_FORMAT_CSV:
		reflector_print_stats_csv(stats);
		break;
	case STATS_FORMAT_TEXT:
	default:
		/* Text format is handled by main.c for historical reasons */
		break;
	}
}

/* ========================================================================
 * VLAN (802.1Q) Support
 * ======================================================================== */

/*
 * Check if packet has VLAN tag (802.1Q)
 *
 * VLAN-tagged frames have EtherType 0x8100 at offset 12,
 * followed by 4-byte VLAN tag, then the real EtherType.
 *
 * Frame structure:
 * [0-5]   Dst MAC
 * [6-11]  Src MAC
 * [12-13] TPID (0x8100 for 802.1Q)
 * [14-15] TCI (PCP, DEI, VID)
 * [16-17] Inner EtherType (actual protocol)
 * [18+]   Payload
 */
bool is_vlan_tagged(const uint8_t *data, uint32_t len, uint16_t *inner_ethertype,
                    uint32_t *vlan_offset)
{
	/* Need at least Ethernet header + VLAN tag */
	if (len < ETH_HDR_LEN + VLAN_HDR_LEN) {
		return false;
	}

	/* Check for 802.1Q TPID */
	uint16_t tpid = (data[ETH_TYPE_OFFSET] << 8) | data[ETH_TYPE_OFFSET + 1];

	if (tpid == ETH_P_8021Q || tpid == ETH_P_8021AD) {
		/* VLAN tagged - get inner EtherType after VLAN header */
		*inner_ethertype = (data[ETH_HDR_LEN + 2] << 8) | data[ETH_HDR_LEN + 3];
		*vlan_offset = ETH_HDR_LEN + VLAN_HDR_LEN;
		return true;
	}

	return false;
}

/* ========================================================================
 * IPv6 Support
 * ======================================================================== */

/*
 * Calculate UDP checksum for IPv6
 * Uses IPv6 pseudo-header + UDP header + data
 */
static uint16_t calculate_udp6_checksum(const uint8_t *ip6h, const uint8_t *udph, uint32_t udp_len)
{
	uint32_t sum = 0;

	/* IPv6 pseudo-header:
	 * - Source address (16 bytes)
	 * - Destination address (16 bytes)
	 * - UDP length (4 bytes, upper layer packet length)
	 * - Zeros (3 bytes)
	 * - Next header (1 byte, = 17 for UDP)
	 */

	/* Sum source IPv6 address (8 words) */
	const uint16_t *src = (const uint16_t *)(ip6h + IPV6_SRC_OFFSET);
	for (int i = 0; i < 8; i++) {
		sum += ntohs(src[i]);
	}

	/* Sum destination IPv6 address (8 words) */
	const uint16_t *dst = (const uint16_t *)(ip6h + IPV6_DST_OFFSET);
	for (int i = 0; i < 8; i++) {
		sum += ntohs(dst[i]);
	}

	/* UDP length (as 32-bit value split into two 16-bit words) */
	sum += (udp_len >> 16) & 0xFFFF;
	sum += udp_len & 0xFFFF;

	/* Next header = UDP (17) */
	sum += IPPROTO_UDP;

	/* Sum UDP header + data (skip checksum field at offset 6) */
	const uint16_t *ptr = (const uint16_t *)udph;
	for (uint32_t i = 0; i < udp_len / 2; i++) {
		if (i != 3) { /* Skip UDP checksum field */
			sum += ntohs(ptr[i]);
		}
	}

	/* Handle odd byte */
	if (udp_len & 1) {
		sum += (uint16_t)(udph[udp_len - 1]) << 8;
	}

	/* Fold 32-bit sum to 16 bits */
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	/* UDP checksum 0 means no checksum, use 0xFFFF instead */
	/* Note: For IPv6, UDP checksum is mandatory (can't be 0) */
	uint16_t checksum = (uint16_t)~sum;
	return checksum == 0 ? htons(0xFFFF) : htons(checksum);
}

/*
 * Reflect IPv6 packet in-place
 *
 * IPv6 header is fixed 40 bytes (no variable options like IPv4 IHL).
 * Swaps:
 * - Ethernet MAC addresses
 * - IPv6 source/destination (16 bytes each)
 * - UDP ports (if mode == ALL)
 */
void reflect_packet_ipv6(uint8_t *data, uint32_t len, reflect_mode_t mode,
                         bool software_checksum)
{
	/* Determine if VLAN tagged */
	uint16_t inner_etype = 0;
	uint32_t ip_offset = ETH_HDR_LEN;

	uint16_t outer_etype = (data[ETH_TYPE_OFFSET] << 8) | data[ETH_TYPE_OFFSET + 1];
	if (outer_etype == ETH_P_8021Q || outer_etype == ETH_P_8021AD) {
		ip_offset = ETH_HDR_LEN + VLAN_HDR_LEN;
		inner_etype = (data[ETH_HDR_LEN + 2] << 8) | data[ETH_HDR_LEN + 3];
		(void)inner_etype; /* Suppress unused warning */
	}

	/* Verify minimum length for IPv6 */
	if (len < ip_offset + IPV6_HDR_LEN) {
		return;
	}

	/* Prefetch areas we'll modify */
	PREFETCH_WRITE(data);
	PREFETCH_WRITE(data + 32);

	/* Swap Ethernet MAC addresses (all modes) */
	uint64_t temp_mac;
	memcpy(&temp_mac, &data[ETH_DST_OFFSET], 6);
	memcpy(&data[ETH_DST_OFFSET], &data[ETH_SRC_OFFSET], 6);
	memcpy(&data[ETH_SRC_OFFSET], &temp_mac, 6);

	if (mode == REFLECT_MODE_MAC) {
		return;
	}

	/* Swap IPv6 addresses (16 bytes each) */
	uint8_t temp_addr[IPV6_ADDR_LEN];
	memcpy(temp_addr, &data[ip_offset + IPV6_SRC_OFFSET], IPV6_ADDR_LEN);
	memcpy(&data[ip_offset + IPV6_SRC_OFFSET], &data[ip_offset + IPV6_DST_OFFSET], IPV6_ADDR_LEN);
	memcpy(&data[ip_offset + IPV6_DST_OFFSET], temp_addr, IPV6_ADDR_LEN);

	if (mode == REFLECT_MODE_MAC_IP) {
		return;
	}

	/* REFLECT_MODE_ALL: Also swap UDP ports */
	uint32_t udp_offset = ip_offset + IPV6_HDR_LEN;

	if (len < udp_offset + UDP_HDR_LEN) {
		return;
	}

	/* Swap UDP ports */
	uint16_t udp_src_val, udp_dst_val;
	memcpy(&udp_src_val, &data[udp_offset + UDP_SRC_PORT_OFFSET], 2);
	memcpy(&udp_dst_val, &data[udp_offset + UDP_DST_PORT_OFFSET], 2);
	memcpy(&data[udp_offset + UDP_SRC_PORT_OFFSET], &udp_dst_val, 2);
	memcpy(&data[udp_offset + UDP_DST_PORT_OFFSET], &udp_src_val, 2);

	/* Recalculate UDP checksum if software fallback enabled */
	/* Note: IPv6 UDP checksum is mandatory */
	if (software_checksum) {
		uint8_t *ip6h = data + ip_offset;
		uint8_t *udph = data + udp_offset;
		uint16_t udp_len = ntohs(*(uint16_t *)(udph + 4));

		if (len >= udp_offset + udp_len) {
			uint16_t *udp_check = (uint16_t *)(udph + 6);
			*udp_check = 0;
			*udp_check = calculate_udp6_checksum(ip6h, udph, udp_len);
		}
	}
}

/*
 * Extended ITO packet validation with IPv6 and VLAN support
 *
 * Handles:
 * - IPv4 packets (EtherType 0x0800)
 * - IPv6 packets (EtherType 0x86DD)
 * - VLAN-tagged packets (EtherType 0x8100/0x88A8)
 *
 * Returns: true if valid ITO packet, false otherwise
 */
bool is_ito_packet_extended(const uint8_t *data, uint32_t len, const reflector_config_t *config,
                            bool *is_ipv6, bool *is_vlan)
{
	*is_ipv6 = false;
	*is_vlan = false;

	/* Prefetch packet data */
	PREFETCH_READ(data);
	PREFETCH_READ(data + 64);

	/* Fast rejection: absolute minimum length */
	if (unlikely(len < MIN_ITO_PACKET_LEN)) {
		return false;
	}

	/* Check destination MAC matches our interface */
	if (unlikely(memcmp(&data[ETH_DST_OFFSET], config->mac, 6) != 0)) {
		return false;
	}

	/* Check source MAC OUI if filtering enabled */
	if (config->filter_oui) {
		if (unlikely(data[ETH_SRC_OFFSET] != config->oui[0] ||
		             data[ETH_SRC_OFFSET + 1] != config->oui[1] ||
		             data[ETH_SRC_OFFSET + 2] != config->oui[2])) {
			return false;
		}
	}

	/* Parse EtherType - check for VLAN first */
	uint16_t ethertype = (data[ETH_TYPE_OFFSET] << 8) | data[ETH_TYPE_OFFSET + 1];
	uint32_t ip_offset = ETH_HDR_LEN;

	/* Handle VLAN tag */
	if (ethertype == ETH_P_8021Q || ethertype == ETH_P_8021AD) {
		if (!config->enable_vlan) {
			return false;
		}
		if (len < ETH_HDR_LEN + VLAN_HDR_LEN + IP_HDR_MIN_LEN) {
			return false;
		}
		*is_vlan = true;
		/* Get inner EtherType */
		ethertype = (data[ETH_HDR_LEN + 2] << 8) | data[ETH_HDR_LEN + 3];
		ip_offset = ETH_HDR_LEN + VLAN_HDR_LEN;
	}

	/* Check for IPv4 or IPv6 */
	uint32_t ip_hdr_len;
	uint8_t ip_proto;

	if (ethertype == ETH_P_IP) {
		/* IPv4 validation */
		if (len < ip_offset + IP_HDR_MIN_LEN) {
			return false;
		}

		uint8_t ver_ihl = data[ip_offset + IP_VER_IHL_OFFSET];
		uint8_t version = ver_ihl >> 4;
		uint8_t ihl = ver_ihl & 0x0F;

		if (unlikely(version != 4 || ihl < 5)) {
			return false;
		}

		ip_hdr_len = ihl * 4;
		ip_proto = data[ip_offset + IP_PROTO_OFFSET];

	} else if (ethertype == ETH_P_IPV6) {
		/* IPv6 validation */
		if (!config->enable_ipv6) {
			return false;
		}
		if (len < ip_offset + IPV6_HDR_LEN) {
			return false;
		}

		*is_ipv6 = true;
		ip_hdr_len = IPV6_HDR_LEN;
		ip_proto = data[ip_offset + IPV6_NEXT_HDR_OFFSET];

	} else {
		/* Unknown EtherType */
		return false;
	}

	/* Check IP protocol = UDP */
	if (unlikely(ip_proto != IPPROTO_UDP)) {
		return false;
	}

	/* Calculate UDP offset */
	uint32_t udp_offset = ip_offset + ip_hdr_len;
	uint32_t udp_payload_offset = udp_offset + UDP_HDR_LEN;

	/* Check length */
	if (len < udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN) {
		return false;
	}

	/* Check destination UDP port if filtering enabled */
	if (config->ito_port != 0) {
		uint16_t dst_port =
		    (data[udp_offset + UDP_DST_PORT_OFFSET] << 8) | data[udp_offset + UDP_DST_PORT_OFFSET + 1];
		if (unlikely(dst_port != config->ito_port)) {
			return false;
		}
	}

	/* Check for ITO signatures */
	const uint8_t *sig = &data[udp_payload_offset + ITO_SIG_OFFSET];

	if (likely(memcmp(sig, ITO_SIG_PROBEOT, ITO_SIG_LEN) == 0 ||
	           memcmp(sig, ITO_SIG_DATAOT, ITO_SIG_LEN) == 0 ||
	           memcmp(sig, ITO_SIG_LATENCY, ITO_SIG_LEN) == 0)) {
		return true;
	}

	/* Check for RFC2544/Y.1564 custom signatures */
	if (memcmp(sig, CUSTOM_SIG_RFC2544, CUSTOM_SIG_LEN) == 0 ||
	    memcmp(sig, CUSTOM_SIG_Y1564, CUSTOM_SIG_LEN) == 0) {
		return true;
	}

	return false;
}
