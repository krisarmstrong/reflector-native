/*
 * test_utils.c - Unit tests for utility functions
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

/* Test timestamp function */
TEST(timestamp_monotonic) {
    uint64_t t1 = get_timestamp_ns();
    uint64_t t2 = get_timestamp_ns();
    
    ASSERT(t2 >= t1);  /* Time must be monotonic */
    ASSERT(t2 - t1 < 1000000);  /* Should be less than 1ms apart */
}

/* Test signature type detection */
TEST(signature_type_probeot) {
    uint8_t packet[64] = {
        /* Ethernet */
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        /* IP */
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        /* UDP */
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        /* 5-byte header + PROBEOT */
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'P', 'R', 'O', 'B', 'E', 'O', 'T',
    };

    ito_sig_type_t type = get_ito_signature_type(packet, sizeof(packet));
    ASSERT(type == ITO_SIG_TYPE_PROBEOT);
}

TEST(signature_type_dataot) {
    uint8_t packet[64] = {
        /* Ethernet */
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        /* IP */
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        /* UDP */
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        /* 5-byte header + DATA:OT */
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'D', 'A', 'T', 'A', ':', 'O', 'T',
    };

    ito_sig_type_t type = get_ito_signature_type(packet, sizeof(packet));
    ASSERT(type == ITO_SIG_TYPE_DATAOT);
}

TEST(signature_type_latency) {
    uint8_t packet[64] = {
        /* Ethernet */
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        /* IP */
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        /* UDP */
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        /* 5-byte header + LATENCY */
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'L', 'A', 'T', 'E', 'N', 'C', 'Y',
    };

    ito_sig_type_t type = get_ito_signature_type(packet, sizeof(packet));
    ASSERT(type == ITO_SIG_TYPE_LATENCY);
}

TEST(signature_type_unknown) {
    uint8_t packet[64] = {
        /* Ethernet */
        0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b,
        0x00, 0xc0, 0x17, 0x54, 0x05, 0x98,
        0x08, 0x00,
        /* IP */
        0x45, 0x00, 0x00, 0x27,
        0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xc0, 0xa8, 0x00, 0x0a,
        0xc0, 0xa8, 0x00, 0x01,
        /* UDP */
        0x0f, 0x02, 0x0f, 0x02,
        0x00, 0x13, 0x00, 0x00,
        /* 5-byte header + invalid */
        0x09, 0x10, 0xea, 0x1d, 0x00,
        'I', 'N', 'V', 'A', 'L', 'I', 'D',
    };

    ito_sig_type_t type = get_ito_signature_type(packet, sizeof(packet));
    ASSERT(type == ITO_SIG_TYPE_UNKNOWN);
}

/* Test statistics functions */
TEST(latency_stats_update) {
    latency_stats_t stats = {0};
    
    update_latency_stats(&stats, 100000);  /* 100 microseconds */
    ASSERT(stats.count == 1);
    ASSERT(stats.min_ns == 100000);
    ASSERT(stats.max_ns == 100000);
    ASSERT(stats.avg_ns == 100000.0);
    
    update_latency_stats(&stats, 200000);  /* 200 microseconds */
    ASSERT(stats.count == 2);
    ASSERT(stats.min_ns == 100000);
    ASSERT(stats.max_ns == 200000);
    ASSERT(stats.avg_ns == 150000.0);
}

TEST(signature_stats_update) {
    reflector_stats_t stats = {0};
    
    update_signature_stats(&stats, ITO_SIG_TYPE_PROBEOT);
    ASSERT(stats.sig_probeot_count == 1);
    
    update_signature_stats(&stats, ITO_SIG_TYPE_DATAOT);
    ASSERT(stats.sig_dataot_count == 1);
    
    update_signature_stats(&stats, ITO_SIG_TYPE_LATENCY);
    ASSERT(stats.sig_latency_count == 1);
    
    update_signature_stats(&stats, ITO_SIG_TYPE_UNKNOWN);
    ASSERT(stats.sig_unknown_count == 1);
}

TEST(error_stats_update) {
    reflector_stats_t stats = {0};
    
    update_error_stats(&stats, ERR_RX_INVALID_MAC);
    ASSERT(stats.err_invalid_mac == 1);
    
    update_error_stats(&stats, ERR_RX_INVALID_ETHERTYPE);
    ASSERT(stats.err_invalid_ethertype == 1);
    
    update_error_stats(&stats, ERR_TX_FAILED);
    ASSERT(stats.err_tx_failed == 1);
}

int main(void) {
    printf("Running utility function tests...\n\n");

    RUN_TEST(timestamp_monotonic);
    RUN_TEST(signature_type_probeot);
    RUN_TEST(signature_type_dataot);
    RUN_TEST(signature_type_latency);
    RUN_TEST(signature_type_unknown);
    RUN_TEST(latency_stats_update);
    RUN_TEST(signature_stats_update);
    RUN_TEST(error_stats_update);

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed == 0 ? 0 : 1;
}
