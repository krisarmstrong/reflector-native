/*
 * test_integration.c - Integration tests for packet reflector
 *
 * Tests:
 * - Multi-worker initialization and cleanup
 * - Platform fallback behavior
 * - Configuration validation
 * - Worker lifecycle management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "reflector.h"

int tests_passed = 0;
int tests_failed = 0;

/* Platform-specific loopback interface name */
#ifdef __APPLE__
#define LOOPBACK_IF "lo0"
#else
#define LOOPBACK_IF "lo"
#endif

#define TEST(name) \
    do { \
        printf("Running %s... ", name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
    } while(0)

/*
 * Test basic initialization and cleanup
 */
void test_init_cleanup(void)
{
    TEST("init_cleanup");

    reflector_ctx_t rctx = {0};

    /* Initialize with loopback interface */
    int ret = reflector_init(&rctx, LOOPBACK_IF);
    if (ret < 0) {
        FAIL("Failed to initialize reflector on loopback");
        return;
    }

    /* Verify configuration was set */
    if (rctx.config.ifindex <= 0) {
        FAIL("Invalid interface index");
        reflector_cleanup(&rctx);
        return;
    }

    /* Cleanup should succeed */
    reflector_cleanup(&rctx);

    PASS();
}

/*
 * Test configuration defaults
 */
void test_config_defaults(void)
{
    TEST("config_defaults");

    reflector_ctx_t rctx = {0};

    if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
        FAIL("Failed to initialize reflector");
        return;
    }

    /* Verify defaults */
    if (rctx.config.frame_size != FRAME_SIZE) {
        FAIL("Incorrect default frame_size");
        reflector_cleanup(&rctx);
        return;
    }

    if (rctx.config.num_frames != NUM_FRAMES) {
        FAIL("Incorrect default num_frames");
        reflector_cleanup(&rctx);
        return;
    }

    if (rctx.config.batch_size != BATCH_SIZE) {
        FAIL("Incorrect default batch_size");
        reflector_cleanup(&rctx);
        return;
    }

    if (rctx.config.cpu_affinity != -1) {
        FAIL("Incorrect default cpu_affinity");
        reflector_cleanup(&rctx);
        return;
    }

    if (rctx.config.use_huge_pages != false) {
        FAIL("Incorrect default use_huge_pages");
        reflector_cleanup(&rctx);
        return;
    }

    if (rctx.config.software_checksum != false) {
        FAIL("Incorrect default software_checksum");
        reflector_cleanup(&rctx);
        return;
    }

    reflector_cleanup(&rctx);
    PASS();
}

/*
 * Test invalid interface handling
 */
void test_invalid_interface(void)
{
    TEST("invalid_interface");

    reflector_ctx_t rctx = {0};

    /* Try to initialize with non-existent interface */
    int ret = reflector_init(&rctx, "nonexistent999");
    if (ret == 0) {
        FAIL("Should have failed with invalid interface");
        reflector_cleanup(&rctx);
        return;
    }

    /* Context should still be safe to cleanup */
    reflector_cleanup(&rctx);

    PASS();
}

/*
 * Test worker initialization (no actual start)
 */
void test_worker_allocation(void)
{
    TEST("worker_allocation");

    reflector_ctx_t rctx = {0};

    if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
        FAIL("Failed to initialize reflector");
        return;
    }

    /* Verify worker count was set */
    if (rctx.config.num_workers <= 0) {
        FAIL("No workers configured");
        reflector_cleanup(&rctx);
        return;
    }

    reflector_cleanup(&rctx);
    PASS();
}

/*
 * Test statistics initialization
 */
void test_stats_init(void)
{
    TEST("stats_init");

    reflector_ctx_t rctx = {0};

    if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
        FAIL("Failed to initialize reflector");
        return;
    }

    reflector_stats_t stats;
    reflector_get_stats(&rctx, &stats);

    /* All counters should be zero */
    if (stats.packets_received != 0 ||
        stats.packets_reflected != 0 ||
        stats.bytes_received != 0 ||
        stats.bytes_reflected != 0) {
        FAIL("Statistics not initialized to zero");
        reflector_cleanup(&rctx);
        return;
    }

    reflector_cleanup(&rctx);
    PASS();
}

/*
 * Test statistics reset
 */
void test_stats_reset(void)
{
    TEST("stats_reset");

    reflector_ctx_t rctx = {0};

    if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
        FAIL("Failed to initialize reflector");
        return;
    }

    /* Manually set some stats (simulating activity) */
    rctx.global_stats.packets_received = 100;
    rctx.global_stats.packets_reflected = 90;

    /* Reset should clear them */
    reflector_reset_stats(&rctx);

    reflector_stats_t stats;
    reflector_get_stats(&rctx, &stats);

    if (stats.packets_received != 0 || stats.packets_reflected != 0) {
        FAIL("Statistics not reset properly");
        reflector_cleanup(&rctx);
        return;
    }

    reflector_cleanup(&rctx);
    PASS();
}

/*
 * Test configuration updates
 */
void test_config_update(void)
{
    TEST("config_update");

    reflector_ctx_t rctx = {0};

    if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
        FAIL("Failed to initialize reflector");
        return;
    }

    /* Modify configuration */
    reflector_config_t config = rctx.config;
    config.measure_latency = true;
    config.software_checksum = true;

    if (reflector_set_config(&rctx, &config) < 0) {
        FAIL("Failed to set configuration");
        reflector_cleanup(&rctx);
        return;
    }

    /* Verify changes were applied */
    if (!rctx.config.measure_latency || !rctx.config.software_checksum) {
        FAIL("Configuration not updated correctly");
        reflector_cleanup(&rctx);
        return;
    }

    reflector_cleanup(&rctx);
    PASS();
}

/*
 * Test getting configuration
 */
void test_config_get(void)
{
    TEST("config_get");

    reflector_ctx_t rctx = {0};

    if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
        FAIL("Failed to initialize reflector");
        return;
    }

    reflector_config_t config;
    reflector_get_config(&rctx, &config);

    /* Verify config matches internal state */
    if (config.ifindex != rctx.config.ifindex ||
        config.frame_size != rctx.config.frame_size ||
        config.num_frames != rctx.config.num_frames) {
        FAIL("Retrieved configuration doesn't match");
        reflector_cleanup(&rctx);
        return;
    }

    reflector_cleanup(&rctx);
    PASS();
}

/*
 * Test interface utility functions
 */
void test_interface_utils(void)
{
    TEST("interface_utils");

    /* Test getting interface index */
    int ifindex = get_interface_index(LOOPBACK_IF);
    if (ifindex <= 0) {
        FAIL("Failed to get loopback interface index");
        return;
    }

    /* Test getting MAC address */
    uint8_t mac[6];
    if (get_interface_mac(LOOPBACK_IF, mac) < 0) {
        FAIL("Failed to get loopback MAC address");
        return;
    }

    /* Test getting timestamp */
    uint64_t ts = get_timestamp_ns();
    if (ts == 0) {
        FAIL("Failed to get timestamp");
        return;
    }

    PASS();
}

int main(void)
{
    printf("Running integration tests...\n\n");

    /* Set log level to error to reduce noise */
    reflector_set_log_level(LOG_ERROR);

    /* Run tests */
    test_init_cleanup();
    test_config_defaults();
    test_invalid_interface();
    test_worker_allocation();
    test_stats_init();
    test_stats_reset();
    test_config_update();
    test_config_get();
    test_interface_utils();

    /* Print summary */
    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    if (tests_failed == 0) {
        printf("✅ Integration tests passed!\n");
        return 0;
    } else {
        printf("❌ Some integration tests failed\n");
        return 1;
    }
}
