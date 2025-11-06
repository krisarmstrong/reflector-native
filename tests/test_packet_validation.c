/*
 * test_packet_validation.c - Unit tests for packet validation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "reflector.h"

int tests_passed = 0;
int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s...", #name); \
    test_##name(); \
    printf(" PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\n  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Test ITO packet validation */
TEST(ito_packet_valid_probeot) {
    uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};

    /* Minimal ITO packet: Eth(14) + IP(20) + UDP(8) + Header(5) + PROBEOT(7) = 54 bytes */
    uint8_t packet[64] = {
        /* Ethernet: dst MAC, src MAC, type=0x0800 */
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,  /* dst */
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,  /* src */
        0x08, 0x00,                          /* IPv4 */

        /* IP: version/IHL=0x45, proto=17(UDP) at offset 9 */
        0x45, 0x00, 0x00, 0x27,              /* ver/ihl, len */
        0x00, 0x00, 0x40, 0x00,              /* id, flags */
        0x40, 0x11, 0x00, 0x00,              /* ttl, proto=UDP, checksum */
        0xc0, 0xa8, 0x00, 0x0a,              /* src IP */
        0xc0, 0xa8, 0x00, 0x01,              /* dst IP */

        /* UDP: src port, dst port, len, checksum */
        0x0f, 0x02, 0x0f, 0x02,              /* ports */
        0x00, 0x13, 0x00, 0x00,              /* len, checksum */

        /* UDP payload: 5-byte header + PROBEOT */
        0x09, 0x10, 0xea, 0x1d, 0x00,        /* 5-byte header */
        'P', 'R', 'O', 'B', 'E', 'O', 'T',   /* PROBEOT */
    };

    ASSERT(is_ito_packet(packet, sizeof(packet), mac) == true);
}

TEST(ito_packet_too_short) {
    uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
    uint8_t packet[50] = {0};  /* Too short */

    ASSERT(is_ito_packet(packet, sizeof(packet), mac) == false);
}

TEST(ito_packet_wrong_mac) {
    uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
    uint8_t packet[64] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  /* wrong dst MAC */
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'P', 'R', 'O', 'B', 'E', 'O', 'T',
    };

    ASSERT(is_ito_packet(packet, sizeof(packet), mac) == false);
}

TEST(ito_packet_not_udp) {
    uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
    uint8_t packet[64] = {
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00,              /* proto=TCP, not UDP */
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'P', 'R', 'O', 'B', 'E', 'O', 'T',
    };

    ASSERT(is_ito_packet(packet, sizeof(packet), mac) == false);
}

TEST(ito_packet_wrong_signature) {
    uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};
    uint8_t packet[64] = {
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'I', 'N', 'V', 'A', 'L', 'I', 'D',   /* Wrong signature */
    };

    ASSERT(is_ito_packet(packet, sizeof(packet), mac) == false);
}

/* Test packet reflection */
TEST(reflect_packet_swaps_headers) {
    uint8_t packet[64] = {
        /* Ethernet */
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,  /* dst */
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,  /* src */
        0x08, 0x00,
        /* IP */
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,              /* src IP: 192.168.0.10 */
        0xc0, 0xa8, 0x00, 0x01,              /* dst IP: 192.168.0.1 */
        /* UDP */
        0x0f, 0x02,                          /* src port: 3842 */
        0x0f, 0x03,                          /* dst port: 3843 */
        0x00, 0x13, 0x00, 0x00,
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'P', 'R', 'O', 'B', 'E', 'O', 'T',
    };

    uint8_t expected_dst_mac[6] = {0x00, 0xc0, 0x17, 0x54, 0x05, 0x98};
    uint8_t expected_src_mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};

    reflect_packet_inplace(packet, sizeof(packet));

    /* Check MAC swap */
    ASSERT(memcmp(&packet[0], expected_dst_mac, 6) == 0);
    ASSERT(memcmp(&packet[6], expected_src_mac, 6) == 0);

    /* Check IP swap */
    ASSERT(packet[26] == 0xc0 && packet[27] == 0xa8 && packet[28] == 0x00 && packet[29] == 0x01);  /* src IP now 192.168.0.1 */
    ASSERT(packet[30] == 0xc0 && packet[31] == 0xa8 && packet[32] == 0x00 && packet[33] == 0x0a);  /* dst IP now 192.168.0.10 */

    /* Check UDP port swap */
    ASSERT(packet[34] == 0x0f && packet[35] == 0x03);  /* src port now 3843 */
    ASSERT(packet[36] == 0x0f && packet[37] == 0x02);  /* dst port now 3842 */
}

int main(void) {
    printf("Running packet validation tests...\n\n");

    RUN_TEST(ito_packet_valid_probeot);
    RUN_TEST(ito_packet_too_short);
    RUN_TEST(ito_packet_wrong_mac);
    RUN_TEST(ito_packet_not_udp);
    RUN_TEST(ito_packet_wrong_signature);
    RUN_TEST(reflect_packet_swaps_headers);

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed == 0 ? 0 : 1;
}
