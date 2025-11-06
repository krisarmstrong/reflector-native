/*
 * packet.c - Common packet validation and reflection logic
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This module provides platform-agnostic packet inspection and reflection
 * logic for ITO (Integrated Test & Optimization) packets.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "reflector.h"

/*
 * Fast path packet validation for ITO packets
 *
 * Optimized with branch prediction hints and minimal validation overhead.
 * Checks (in order of increasing cost):
 * 1. Length check (54 bytes minimum) - LIKELY to pass
 * 2. Destination MAC match - LIKELY to fail (most traffic)
 * 3. EtherType = IPv4 (0x0800) - LIKELY to pass if MAC matched
 * 4. IP Protocol = UDP (0x11) - LIKELY to pass
 * 5. ITO signature match - LIKELY to pass if UDP
 *
 * Returns: true if packet should be reflected, false otherwise
 */
ALWAYS_INLINE bool is_ito_packet(const uint8_t *data, uint32_t len, const uint8_t mac[6])
{
	static int debug_count = 0;

	/* Prefetch packet data for upcoming checks */
	PREFETCH_READ(data);
	PREFETCH_READ(data + 64); /* Prefetch UDP header area */

	/* Fast rejection: minimum length check - LIKELY to pass */
	if (unlikely(len < MIN_ITO_PACKET_LEN)) {
		if (unlikely(debug_count++ < 3)) {
			reflector_log(LOG_DEBUG, "Packet too short: %u bytes (need %d)", len, MIN_ITO_PACKET_LEN);
		}
		return false;
	}

	/* Check destination MAC matches our interface - UNLIKELY to match (filters most traffic) */
	if (unlikely(memcmp(&data[ETH_DST_OFFSET], mac, 6) != 0)) {
		if (unlikely(debug_count++ < 3)) {
			reflector_log(LOG_DEBUG, "MAC mismatch: got %02x:%02x:%02x:%02x:%02x:%02x, want %02x:%02x:%02x:%02x:%02x:%02x",
			              data[0], data[1], data[2], data[3], data[4], data[5],
			              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
		return false;
	}

	/* Check EtherType = IPv4 (0x0800) - LIKELY to be IPv4 at this point */
	uint16_t ethertype = (data[ETH_TYPE_OFFSET] << 8) | data[ETH_TYPE_OFFSET + 1];
	if (unlikely(ethertype != ETH_P_IP)) {
		if (unlikely(debug_count++ < 3)) {
			reflector_log(LOG_DEBUG, "Not IPv4: ethertype=0x%04x", ethertype);
		}
		return false;
	}

	/* Check IP version and header length - LIKELY to be valid IPv4 */
	uint8_t ver_ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET];
	uint8_t version = ver_ihl >> 4;
	uint8_t ihl = ver_ihl & 0x0F;

	if (unlikely(version != 4 || ihl < 5)) {
		if (unlikely(debug_count++ < 3)) {
			reflector_log(LOG_DEBUG, "Bad IP: version=%u, ihl=%u", version, ihl);
		}
		return false;
	}

	/* Check IP protocol = UDP - LIKELY to be UDP at this point */
	uint8_t ip_proto = data[ETH_HDR_LEN + IP_PROTO_OFFSET];
	if (unlikely(ip_proto != IPPROTO_UDP)) {
		if (unlikely(debug_count++ < 3)) {
			reflector_log(LOG_DEBUG, "Not UDP: protocol=%u", ip_proto);
		}
		return false;
	}

	/* Calculate UDP payload offset */
	uint32_t ip_hdr_len = ihl * 4;
	uint32_t udp_payload_offset = ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN;

	/* Ensure we have enough data for signature - LIKELY to have enough */
	if (unlikely(len < udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN)) {
		if (unlikely(debug_count++ < 3)) {
			reflector_log(LOG_DEBUG, "Too short for signature: len=%u, need=%u",
			              len, udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN);
		}
		return false;
	}

	/* Check for ITO signatures - LIKELY to match at this point */
	const uint8_t *sig = &data[udp_payload_offset + ITO_SIG_OFFSET];

	if (unlikely(debug_count++ < 3)) {
		char sig_str[8];
		memcpy(sig_str, sig, ITO_SIG_LEN);
		sig_str[ITO_SIG_LEN] = '\0';
		reflector_log(LOG_DEBUG, "UDP payload signature: '%s' (checking for PROBEOT/DATA:OT/LATENCY)", sig_str);
	}

	/* Optimize signature check with early exit */
	if (likely(memcmp(sig, ITO_SIG_PROBEOT, ITO_SIG_LEN) == 0 ||
	           memcmp(sig, ITO_SIG_DATAOT, ITO_SIG_LEN) == 0 ||
	           memcmp(sig, ITO_SIG_LATENCY, ITO_SIG_LEN) == 0)) {
		reflector_log(LOG_INFO, "ITO packet matched! len=%u", len);
		return true;
	}

	return false;
}

/*
 * Reflect packet in-place by swapping headers (optimized version)
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
ALWAYS_INLINE void reflect_packet_inplace(uint8_t *data, uint32_t len)
{
	(void)len;  /* Length not needed for in-place swapping */

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

	/* Swap IP addresses (4 bytes each) - use 32-bit operations */
	uint32_t ip_offset = ETH_HDR_LEN;
	uint32_t *ip_src = (uint32_t *)&data[ip_offset + IP_SRC_OFFSET];
	uint32_t *ip_dst = (uint32_t *)&data[ip_offset + IP_DST_OFFSET];
	uint32_t temp_ip = *ip_src;
	*ip_src = *ip_dst;
	*ip_dst = temp_ip;

	/* Swap UDP ports (2 bytes each) - use 16-bit operations */
	uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
	uint16_t *udp_src = (uint16_t *)&data[udp_offset + UDP_SRC_PORT_OFFSET];
	uint16_t *udp_dst = (uint16_t *)&data[udp_offset + UDP_DST_PORT_OFFSET];
	uint16_t temp_port = *udp_src;
	*udp_src = *udp_dst;
	*udp_dst = temp_port;

	/* Note: Checksums are typically handled by NIC offload or ignored by test tools */
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
ito_sig_type_t get_ito_signature_type(const uint8_t *data, uint32_t len)
{
	/* Calculate UDP payload offset */
	uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
	uint32_t ip_hdr_len = ihl * 4;
	uint32_t udp_payload_offset = ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN;

	/* Safety check */
	if (len < udp_payload_offset + ITO_SIG_OFFSET + ITO_SIG_LEN) {
		return ITO_SIG_TYPE_UNKNOWN;
	}

	const uint8_t *sig = &data[udp_payload_offset + ITO_SIG_OFFSET];

	if (memcmp(sig, ITO_SIG_PROBEOT, ITO_SIG_LEN) == 0) {
		return ITO_SIG_TYPE_PROBEOT;
	} else if (memcmp(sig, ITO_SIG_DATAOT, ITO_SIG_LEN) == 0) {
		return ITO_SIG_TYPE_DATAOT;
	} else if (memcmp(sig, ITO_SIG_LATENCY, ITO_SIG_LEN) == 0) {
		return ITO_SIG_TYPE_LATENCY;
	}

	return ITO_SIG_TYPE_UNKNOWN;
}

/*
 * Update per-signature statistics (inlined for performance)
 */
ALWAYS_INLINE void update_signature_stats(reflector_stats_t *stats, ito_sig_type_t sig_type)
{
	switch (sig_type) {
	case ITO_SIG_TYPE_PROBEOT:
		stats->sig_probeot_count++;
		break;
	case ITO_SIG_TYPE_DATAOT:
		stats->sig_dataot_count++;
		break;
	case ITO_SIG_TYPE_LATENCY:
		stats->sig_latency_count++;
		break;
	case ITO_SIG_TYPE_UNKNOWN:
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
		stats->tx_errors++;  /* Update legacy counter */
		break;
	case ERR_RX_NOMEM:
		stats->err_nomem++;
		stats->rx_nomem++;  /* Update legacy counter */
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
	printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
	       stats->packets_received,
	       stats->packets_reflected,
	       stats->packets_dropped,
	       stats->bytes_received,
	       stats->bytes_reflected);

	printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
	       stats->sig_probeot_count,
	       stats->sig_dataot_count,
	       stats->sig_latency_count,
	       stats->sig_unknown_count);

	printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
	       stats->err_invalid_mac,
	       stats->err_invalid_ethertype,
	       stats->err_invalid_protocol,
	       stats->err_invalid_signature,
	       stats->err_too_short,
	       stats->err_tx_failed,
	       stats->err_nomem);

	printf("%" PRIu64 ",%.2f,%.2f,%.2f,",
	       stats->latency.count,
	       stats->latency.min_ns / 1000.0,
	       stats->latency.max_ns / 1000.0,
	       stats->latency.avg_ns / 1000.0);

	printf("%.2f,%.2f\n", stats->pps, stats->mbps);
}

/*
 * Print statistics (dispatcher based on format)
 */
void reflector_print_stats_formatted(const reflector_stats_t *stats,
				     stats_format_t format)
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
