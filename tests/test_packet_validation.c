/*
 * test_packet_validation.c - Unit tests for packet validation
 */

#include "reflector.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int tests_passed = 0;
int tests_failed = 0;

/* Helper to create test config with specified MAC */
static reflector_config_t make_test_config(const uint8_t mac[6])
{
	reflector_config_t config = {0};
	memcpy(config.mac, mac, 6);
	/* Disable OUI and port filtering for backward compatibility with existing tests */
	config.filter_oui = false;
	config.ito_port = 0; /* 0 = any port */
	config.reflect_mode = REFLECT_MODE_ALL;
	return config;
}

#define TEST(name) void test_##name()
#define RUN_TEST(name)                                                                             \
	do {                                                                                           \
		printf("Running %s...", #name);                                                            \
		test_##name();                                                                             \
		printf(" PASS\n");                                                                         \
		tests_passed++;                                                                            \
	} while (0)

#define ASSERT(cond)                                                                               \
	do {                                                                                           \
		if (!(cond)) {                                                                             \
			printf("\n  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
			tests_failed++;                                                                        \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Test ITO packet validation */
TEST(ito_packet_valid_probeot)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};

	/* Minimal ITO packet: Eth(14) + IP(20) + UDP(8) + Header(5) + PROBEOT(7) = 54 bytes */
	uint8_t packet[64] = {
	    /* Ethernet: dst MAC, src MAC, type=0x0800 */
	    0x00,
	    0x01,
	    0x55,
	    0x17,
	    0x1e,
	    0x1b, /* dst */
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98, /* src */
	    0x08,
	    0x00, /* IPv4 */

	    /* IP: version/IHL=0x45, proto=17(UDP) at offset 9 */
	    0x45,
	    0x00,
	    0x00,
	    0x27, /* ver/ihl, len */
	    0x00,
	    0x00,
	    0x40,
	    0x00, /* id, flags */
	    0x40,
	    0x11,
	    0x00,
	    0x00, /* ttl, proto=UDP, checksum */
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a, /* src IP */
	    0xc0,
	    0xa8,
	    0x00,
	    0x01, /* dst IP */

	    /* UDP: src port, dst port, len, checksum */
	    0x0f,
	    0x02,
	    0x0f,
	    0x02, /* ports */
	    0x00,
	    0x13,
	    0x00,
	    0x00, /* len, checksum */

	    /* UDP payload: 5-byte header + PROBEOT */
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00, /* 5-byte header */
	    'P',
	    'R',
	    'O',
	    'B',
	    'E',
	    'O',
	    'T', /* PROBEOT */
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == true);
}

TEST(ito_packet_too_short)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[50] = {0}; /* Too short */

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

TEST(ito_packet_wrong_mac)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* wrong dst MAC */
	    0x00, 0xc0, 0x17, 0x54, 0x05, 0x98, 0x08, 0x00, 0x45, 0x00, 0x00, 0x27,
	    0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x0a,
	    0xc0, 0xa8, 0x00, 0x01, 0x0f, 0x02, 0x0f, 0x02, 0x00, 0x13, 0x00, 0x00,
	    0x09, 0x10, 0xea, 0x1d, 0x00, 'P',  'R',  'O',  'B',  'E',  'O',  'T',
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

TEST(ito_packet_not_udp)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b, 0x00, 0xc0, 0x17, 0x54, 0x05, 0x98, 0x08, 0x00,
	    0x45, 0x00, 0x00, 0x27, 0x00, 0x00, 0x40, 0x00, 0x40, 0x06, 0x00, 0x00, /* proto=TCP, not
	                                                                               UDP */
	    0xc0, 0xa8, 0x00, 0x0a, 0xc0, 0xa8, 0x00, 0x01, 0x0f, 0x02, 0x0f, 0x02, 0x00, 0x13,
	    0x00, 0x00, 0x09, 0x10, 0xea, 0x1d, 0x00, 'P',  'R',  'O',  'B',  'E',  'O',  'T',
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

TEST(ito_packet_wrong_signature)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b, 0x00, 0xc0, 0x17, 0x54, 0x05, 0x98, 0x08, 0x00,
	    0x45, 0x00, 0x00, 0x27, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8,
	    0x00, 0x0a, 0xc0, 0xa8, 0x00, 0x01, 0x0f, 0x02, 0x0f, 0x02, 0x00, 0x13, 0x00, 0x00,
	    0x09, 0x10, 0xea, 0x1d, 0x00, 'I',  'N',  'V',  'A',  'L',  'I',  'D', /* Wrong signature */
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

/* Test IPv6 packet rejection (must be rejected - ITO only supports IPv4) */
TEST(ito_packet_ipv6_rejected)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    /* Ethernet: dst MAC, src MAC, type=0x86DD (IPv6) */
	    0x00,
	    0x01,
	    0x55,
	    0x17,
	    0x1e,
	    0x1b, /* dst */
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98, /* src */
	    0x86,
	    0xDD, /* IPv6 EtherType - MUST BE REJECTED */

	    /* IPv6 header (simplified - just enough to test rejection) */
	    0x60,
	    0x00,
	    0x00,
	    0x00, /* version/class/flow */
	    0x00,
	    0x14,
	    0x11,
	    0x40, /* payload len, next header (UDP=17), hop limit */
	    /* Rest filled with zeros */
	};

	/* IPv6 packets must be rejected since ITO only works with IPv4 */
	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

/* Test broadcast MAC rejection */
TEST(ito_packet_broadcast_mac_rejected)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    /* Ethernet: broadcast dst MAC */
	    0xff,
	    0xff,
	    0xff,
	    0xff,
	    0xff,
	    0xff, /* broadcast dst */
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98, /* src */
	    0x08,
	    0x00, /* IPv4 */

	    /* IP header */
	    0x45,
	    0x00,
	    0x00,
	    0x27,
	    0x00,
	    0x00,
	    0x40,
	    0x00,
	    0x40,
	    0x11,
	    0x00,
	    0x00,
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a,
	    0xc0,
	    0xa8,
	    0x00,
	    0x01,

	    /* UDP header */
	    0x0f,
	    0x02,
	    0x0f,
	    0x02,
	    0x00,
	    0x13,
	    0x00,
	    0x00,

	    /* ITO signature */
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00,
	    'P',
	    'R',
	    'O',
	    'B',
	    'E',
	    'O',
	    'T',
	};

	/* Broadcast MAC must be rejected - we only accept unicast to our MAC */
	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

/* Test multicast MAC rejection (IPv4 multicast range 01:00:5E:xx:xx:xx) */
TEST(ito_packet_multicast_mac_rejected)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    /* Ethernet: IPv4 multicast dst MAC */
	    0x01,
	    0x00,
	    0x5e,
	    0x00,
	    0x00,
	    0x01, /* multicast dst */
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98, /* src */
	    0x08,
	    0x00, /* IPv4 */

	    /* IP header */
	    0x45,
	    0x00,
	    0x00,
	    0x27,
	    0x00,
	    0x00,
	    0x40,
	    0x00,
	    0x40,
	    0x11,
	    0x00,
	    0x00,
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a,
	    0xe0,
	    0x00,
	    0x00,
	    0x01, /* Multicast IP 224.0.0.1 */

	    /* UDP header */
	    0x0f,
	    0x02,
	    0x0f,
	    0x02,
	    0x00,
	    0x13,
	    0x00,
	    0x00,

	    /* ITO signature */
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00,
	    'P',
	    'R',
	    'O',
	    'B',
	    'E',
	    'O',
	    'T',
	};

	/* Multicast MAC must be rejected - we only accept unicast to our MAC */
	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

/* Test jumbo frame handling (9000+ bytes) */
TEST(ito_packet_jumbo_frame_valid)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};

	/* Create a 9000-byte jumbo frame with valid ITO packet */
	uint8_t packet[9000];
	memset(packet, 0, sizeof(packet));

	/* Ethernet header */
	memcpy(&packet[0], mac, 6); /* dst MAC */
	packet[6] = 0x00;
	packet[7] = 0xc0; /* src MAC */
	packet[8] = 0x17;
	packet[9] = 0x54;
	packet[10] = 0x05;
	packet[11] = 0x98;
	packet[12] = 0x08;
	packet[13] = 0x00; /* IPv4 */

	/* IP header - adjust length for jumbo frame */
	packet[14] = 0x45; /* ver/ihl */
	packet[15] = 0x00; /* tos */
	packet[16] = 0x23;
	packet[17] = 0x1A; /* total length = 8986 (9000-14) */
	packet[18] = 0x00;
	packet[19] = 0x00; /* id */
	packet[20] = 0x40;
	packet[21] = 0x00; /* flags/frag */
	packet[22] = 0x40; /* ttl */
	packet[23] = 0x11; /* protocol = UDP */
	packet[24] = 0x00;
	packet[25] = 0x00; /* checksum */
	packet[26] = 0xc0;
	packet[27] = 0xa8; /* src IP */
	packet[28] = 0x00;
	packet[29] = 0x0a;
	packet[30] = 0xc0;
	packet[31] = 0xa8; /* dst IP */
	packet[32] = 0x00;
	packet[33] = 0x01;

	/* UDP header */
	packet[34] = 0x0f;
	packet[35] = 0x02; /* src port */
	packet[36] = 0x0f;
	packet[37] = 0x02; /* dst port */
	packet[38] = 0x22;
	packet[39] = 0xFA; /* length */
	packet[40] = 0x00;
	packet[41] = 0x00; /* checksum */

	/* ITO signature at proper offset */
	packet[42] = 0x09;
	packet[43] = 0x10; /* 5-byte header */
	packet[44] = 0xea;
	packet[45] = 0x1d;
	packet[46] = 0x00;
	memcpy(&packet[47], "PROBEOT", 7); /* signature */

	/* Jumbo frame with valid ITO signature should be accepted */
	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == true);
}

/* Test DATA:OT signature */
TEST(ito_packet_valid_dataot)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    /* Ethernet */
	    0x00,
	    0x01,
	    0x55,
	    0x17,
	    0x1e,
	    0x1b,
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98,
	    0x08,
	    0x00,

	    /* IP */
	    0x45,
	    0x00,
	    0x00,
	    0x27,
	    0x00,
	    0x00,
	    0x40,
	    0x00,
	    0x40,
	    0x11,
	    0x00,
	    0x00,
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a,
	    0xc0,
	    0xa8,
	    0x00,
	    0x01,

	    /* UDP */
	    0x0f,
	    0x02,
	    0x0f,
	    0x02,
	    0x00,
	    0x13,
	    0x00,
	    0x00,

	    /* ITO signature: DATA:OT */
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00,
	    'D',
	    'A',
	    'T',
	    'A',
	    ':',
	    'O',
	    'T',
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == true);
}

/* Test LATENCY signature */
TEST(ito_packet_valid_latency)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    /* Ethernet */
	    0x00,
	    0x01,
	    0x55,
	    0x17,
	    0x1e,
	    0x1b,
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98,
	    0x08,
	    0x00,

	    /* IP */
	    0x45,
	    0x00,
	    0x00,
	    0x27,
	    0x00,
	    0x00,
	    0x40,
	    0x00,
	    0x40,
	    0x11,
	    0x00,
	    0x00,
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a,
	    0xc0,
	    0xa8,
	    0x00,
	    0x01,

	    /* UDP */
	    0x0f,
	    0x02,
	    0x0f,
	    0x02,
	    0x00,
	    0x13,
	    0x00,
	    0x00,

	    /* ITO signature: LATENCY */
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00,
	    'L',
	    'A',
	    'T',
	    'E',
	    'N',
	    'C',
	    'Y',
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == true);
}

/* Test IP header length validation (IHL < 5 is invalid) */
TEST(ito_packet_invalid_ihl)
{
	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
	uint8_t packet[64] = {
	    /* Ethernet */
	    0x00,
	    0x01,
	    0x55,
	    0x17,
	    0x1e,
	    0x1b,
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98,
	    0x08,
	    0x00,

	    /* IP with invalid IHL=3 (should be >= 5) */
	    0x43,
	    0x00,
	    0x00,
	    0x27, /* version=4, ihl=3 (INVALID) */
	    0x00,
	    0x00,
	    0x40,
	    0x00,
	    0x40,
	    0x11,
	    0x00,
	    0x00,
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a,
	    0xc0,
	    0xa8,
	    0x00,
	    0x01,

	    /* UDP */
	    0x0f,
	    0x02,
	    0x0f,
	    0x02,
	    0x00,
	    0x13,
	    0x00,
	    0x00,

	    /* ITO signature */
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00,
	    'P',
	    'R',
	    'O',
	    'B',
	    'E',
	    'O',
	    'T',
	};

	reflector_config_t config = make_test_config(mac);
	ASSERT(is_ito_packet(packet, sizeof(packet), &config) == false);
}

/* Test packet reflection */
TEST(reflect_packet_swaps_headers)
{
	uint8_t packet[64] = {
	    /* Ethernet */
	    0x00,
	    0x01,
	    0x55,
	    0x17,
	    0x1e,
	    0x1b, /* dst */
	    0x00,
	    0xc0,
	    0x17,
	    0x54,
	    0x05,
	    0x98, /* src */
	    0x08,
	    0x00,
	    /* IP */
	    0x45,
	    0x00,
	    0x00,
	    0x27,
	    0x00,
	    0x00,
	    0x40,
	    0x00,
	    0x40,
	    0x11,
	    0x00,
	    0x00,
	    0xc0,
	    0xa8,
	    0x00,
	    0x0a, /* src IP: 192.168.0.10 */
	    0xc0,
	    0xa8,
	    0x00,
	    0x01, /* dst IP: 192.168.0.1 */
	    /* UDP */
	    0x0f,
	    0x02, /* src port: 3842 */
	    0x0f,
	    0x03, /* dst port: 3843 */
	    0x00,
	    0x13,
	    0x00,
	    0x00,
	    0x09,
	    0x10,
	    0xea,
	    0x1d,
	    0x00,
	    'P',
	    'R',
	    'O',
	    'B',
	    'E',
	    'O',
	    'T',
	};

	uint8_t expected_dst_mac[6] = {0x00, 0xc0, 0x17, 0x54, 0x05, 0x98};
	uint8_t expected_src_mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};

	reflect_packet_inplace(packet, sizeof(packet));

	/* Check MAC swap */
	ASSERT(memcmp(&packet[0], expected_dst_mac, 6) == 0);
	ASSERT(memcmp(&packet[6], expected_src_mac, 6) == 0);

	/* Check IP swap */
	ASSERT(packet[26] == 0xc0 && packet[27] == 0xa8 && packet[28] == 0x00 &&
	       packet[29] == 0x01); /* src IP now 192.168.0.1 */
	ASSERT(packet[30] == 0xc0 && packet[31] == 0xa8 && packet[32] == 0x00 &&
	       packet[33] == 0x0a); /* dst IP now 192.168.0.10 */

	/* Check UDP port swap */
	ASSERT(packet[34] == 0x0f && packet[35] == 0x03); /* src port now 3843 */
	ASSERT(packet[36] == 0x0f && packet[37] == 0x02); /* dst port now 3842 */
}

int main(void)
{
	printf("Running packet validation tests...\n\n");

	/* Basic validation tests */
	RUN_TEST(ito_packet_valid_probeot);
	RUN_TEST(ito_packet_too_short);
	RUN_TEST(ito_packet_wrong_mac);
	RUN_TEST(ito_packet_not_udp);
	RUN_TEST(ito_packet_wrong_signature);

	/* IPv6 rejection test */
	RUN_TEST(ito_packet_ipv6_rejected);

	/* MAC address edge case tests */
	RUN_TEST(ito_packet_broadcast_mac_rejected);
	RUN_TEST(ito_packet_multicast_mac_rejected);

	/* Jumbo frame test */
	RUN_TEST(ito_packet_jumbo_frame_valid);

	/* All signature type tests */
	RUN_TEST(ito_packet_valid_dataot);
	RUN_TEST(ito_packet_valid_latency);

	/* IP header validation */
	RUN_TEST(ito_packet_invalid_ihl);

	/* Reflection tests */
	RUN_TEST(reflect_packet_swaps_headers);

	printf("\n=================================\n");
	printf("Tests passed: %d\n", tests_passed);
	printf("Tests failed: %d\n", tests_failed);
	printf("=================================\n");

	return tests_failed == 0 ? 0 : 1;
}
