/*
 * packet.c - Common packet validation and reflection logic
 *
 * Copyright (c) 2025 Kris Armstrong
 *
 * This module provides platform-agnostic packet inspection and reflection
 * logic for ITO (Integrated Test & Optimization) packets.
 */

#include <string.h>
#include <arpa/inet.h>
#include "reflector.h"

/*
 * Fast path packet validation for ITO packets
 *
 * Checks (in order of increasing cost):
 * 1. Length check (54 bytes minimum)
 * 2. Destination MAC match
 * 3. EtherType = IPv4 (0x0800)
 * 4. IP Protocol = UDP (0x11)
 * 5. ITO signature match
 *
 * Returns: true if packet should be reflected, false otherwise
 */
bool is_ito_packet(const uint8_t *data, uint32_t len, const uint8_t mac[6])
{
    /* Fast rejection: minimum length check */
    if (len < MIN_ITO_PACKET_LEN) {
        return false;
    }

    /* Check destination MAC matches our interface */
    if (memcmp(&data[ETH_DST_OFFSET], mac, 6) != 0) {
        return false;
    }

    /* Check EtherType = IPv4 (0x0800) */
    uint16_t ethertype = (data[ETH_TYPE_OFFSET] << 8) | data[ETH_TYPE_OFFSET + 1];
    if (ethertype != ETH_P_IP) {
        return false;
    }

    /* Check IP version and header length */
    uint8_t ver_ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET];
    uint8_t version = ver_ihl >> 4;
    uint8_t ihl = ver_ihl & 0x0F;

    if (version != 4 || ihl < 5) {
        return false;
    }

    /* Check IP protocol = UDP */
    uint8_t ip_proto = data[ETH_HDR_LEN + IP_PROTO_OFFSET];
    if (ip_proto != IPPROTO_UDP) {
        return false;
    }

    /* Calculate UDP payload offset */
    uint32_t ip_hdr_len = ihl * 4;
    uint32_t udp_payload_offset = ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN;

    /* Ensure we have enough data for signature */
    if (len < udp_payload_offset + ITO_SIG_LEN) {
        return false;
    }

    /* Check for ITO signatures */
    const uint8_t *sig = &data[udp_payload_offset + ITO_SIG_OFFSET];

    if (memcmp(sig, ITO_SIG_PROBEOT, ITO_SIG_LEN) == 0 ||
        memcmp(sig, ITO_SIG_DATAOT, ITO_SIG_LEN) == 0 ||
        memcmp(sig, ITO_SIG_LATENCY, ITO_SIG_LEN) == 0) {
        return true;
    }

    return false;
}

/*
 * Reflect packet in-place by swapping headers
 *
 * This function performs zero-copy reflection by modifying the packet
 * buffer directly. It swaps:
 * - Ethernet: src <-> dst MAC
 * - IPv4: src <-> dst IP
 * - UDP: src <-> dst port
 *
 * Assumes packet has been validated by is_ito_packet()
 */
void reflect_packet_inplace(uint8_t *data, uint32_t len)
{
    uint8_t temp[6];  /* Temp buffer for swapping */

    /* Swap Ethernet MAC addresses */
    memcpy(temp, &data[ETH_DST_OFFSET], 6);
    memcpy(&data[ETH_DST_OFFSET], &data[ETH_SRC_OFFSET], 6);
    memcpy(&data[ETH_SRC_OFFSET], temp, 6);

    /* Get IP header length */
    uint8_t ihl = data[ETH_HDR_LEN + IP_VER_IHL_OFFSET] & 0x0F;
    uint32_t ip_hdr_len = ihl * 4;

    /* Swap IP addresses (4 bytes each) */
    uint32_t ip_offset = ETH_HDR_LEN;
    memcpy(temp, &data[ip_offset + IP_SRC_OFFSET], 4);
    memcpy(&data[ip_offset + IP_SRC_OFFSET], &data[ip_offset + IP_DST_OFFSET], 4);
    memcpy(&data[ip_offset + IP_DST_OFFSET], temp, 4);

    /* Swap UDP ports (2 bytes each) */
    uint32_t udp_offset = ETH_HDR_LEN + ip_hdr_len;
    uint16_t temp_port;
    memcpy(&temp_port, &data[udp_offset + UDP_SRC_PORT_OFFSET], 2);
    memcpy(&data[udp_offset + UDP_SRC_PORT_OFFSET], &data[udp_offset + UDP_DST_PORT_OFFSET], 2);
    memcpy(&data[udp_offset + UDP_DST_PORT_OFFSET], &temp_port, 2);

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
