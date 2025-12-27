/*
 * test_platform_fallback.c - Platform fallback and multi-worker integration tests
 *
 * Tests:
 * - Platform selection and fallback behavior
 * - Multi-worker initialization and coordination
 * - Worker scaling up/down
 * - Platform capability detection
 */

#include "reflector.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int tests_passed = 0;
int tests_failed = 0;

#ifdef __APPLE__
#define LOOPBACK_IF "lo0"
#else
#define LOOPBACK_IF "lo"
#endif

#define TEST(name)                                                                                 \
	do {                                                                                           \
		printf("Running %s... ", name);                                                            \
		fflush(stdout);                                                                            \
	} while (0)

#define PASS()                                                                                     \
	do {                                                                                           \
		printf("PASS\n");                                                                          \
		tests_passed++;                                                                            \
	} while (0)

#define FAIL(msg)                                                                                  \
	do {                                                                                           \
		printf("FAIL: %s\n", msg);                                                                 \
		tests_failed++;                                                                            \
	} while (0)

/*
 * Test basic initialization
 */
void test_basic_init(void)
{
	TEST("basic_init");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Verify configuration was set */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);

	if (strcmp(config.ifname, LOOPBACK_IF) != 0) {
		FAIL("Interface name not set correctly");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test that fallback works when preferred platform unavailable
 * Note: This test may require specific conditions to trigger fallback
 */
void test_platform_fallback(void)
{
	TEST("platform_fallback");

	reflector_ctx_t rctx = {0};

	/* Initialize with loopback which may not support all platforms */
	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Verify we got a working configuration */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);

	if (config.ifindex < 0) {
		FAIL("Invalid interface index");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test single worker initialization
 */
void test_single_worker_init(void)
{
	TEST("single_worker_init");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Request single worker */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.num_workers = 1;
	if (reflector_set_config(&rctx, &config) < 0) {
		FAIL("Failed to set single worker config");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_get_config(&rctx, &config);
	if (config.num_workers != 1) {
		FAIL("Worker count not set to 1");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test multi-worker initialization
 */
void test_multi_worker_init(void)
{
	TEST("multi_worker_init");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Request multiple workers */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.num_workers = 4;
	if (reflector_set_config(&rctx, &config) < 0) {
		FAIL("Failed to set multi-worker config");
		reflector_cleanup(&rctx);
		return;
	}

	/* May be capped by system capabilities */
	reflector_get_config(&rctx, &config);
	if (config.num_workers < 1) {
		FAIL("No workers configured");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test worker scaling
 */
void test_worker_scaling(void)
{
	TEST("worker_scaling");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Start with 1 worker */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.num_workers = 1;
	if (reflector_set_config(&rctx, &config) < 0) {
		FAIL("Failed to set initial worker count");
		reflector_cleanup(&rctx);
		return;
	}

	/* Scale to 2 workers */
	config.num_workers = 2;
	if (reflector_set_config(&rctx, &config) < 0) {
		/* Scaling may not be supported, which is OK */
		reflector_cleanup(&rctx);
		PASS();
		return;
	}

	/* Scale back to 1 worker */
	config.num_workers = 1;
	if (reflector_set_config(&rctx, &config) < 0) {
		FAIL("Failed to scale down workers");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test maximum worker limit
 */
void test_worker_limit(void)
{
	TEST("worker_limit");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Request excessive workers */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.num_workers = 1000;
	if (reflector_set_config(&rctx, &config) < 0) {
		/* May fail which is OK */
		reflector_cleanup(&rctx);
		PASS();
		return;
	}

	/* Should be capped to reasonable maximum */
	reflector_get_config(&rctx, &config);
	if (config.num_workers > 128) {
		FAIL("Worker count not capped");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test concurrent context creation
 */
typedef struct {
	int thread_id;
	int status;
	reflector_ctx_t ctx;
} thread_data_t;

static void *create_context_thread(void *arg)
{
	thread_data_t *data = (thread_data_t *)arg;

	data->status = reflector_init(&data->ctx, LOOPBACK_IF);
	if (data->status == 0) {
		/* Small sleep to simulate work */
		usleep(1000);
		reflector_cleanup(&data->ctx);
	}

	return NULL;
}

void test_concurrent_contexts(void)
{
	TEST("concurrent_contexts");

	const int num_threads = 4;
	pthread_t threads[4];
	thread_data_t thread_data[4];

	/* Create contexts concurrently */
	for (int i = 0; i < num_threads; i++) {
		thread_data[i].thread_id = i;
		thread_data[i].status = -1;
		memset(&thread_data[i].ctx, 0, sizeof(reflector_ctx_t));
		pthread_create(&threads[i], NULL, create_context_thread, &thread_data[i]);
	}

	/* Wait for all threads */
	for (int i = 0; i < num_threads; i++) {
		pthread_join(threads[i], NULL);
	}

	/* Check results - at least some should succeed */
	int success_count = 0;
	for (int i = 0; i < num_threads; i++) {
		if (thread_data[i].status == 0) {
			success_count++;
		}
	}

	if (success_count == 0) {
		FAIL("No contexts created successfully");
		return;
	}

	PASS();
}

/*
 * Test statistics retrieval
 */
void test_stats_get(void)
{
	TEST("stats_get");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Get initial stats (should be zero) */
	reflector_stats_t stats;
	reflector_get_stats(&rctx, &stats);

	if (stats.packets_received != 0 || stats.packets_reflected != 0) {
		FAIL("Initial stats not zero");
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

	/* Reset stats */
	reflector_reset_stats(&rctx);

	/* Get stats after reset */
	reflector_stats_t stats;
	reflector_get_stats(&rctx, &stats);

	if (stats.packets_received != 0) {
		FAIL("Stats not reset properly");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test graceful shutdown with active workers
 */
void test_graceful_shutdown(void)
{
	TEST("graceful_shutdown");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Configure workers */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.num_workers = 2;
	reflector_set_config(&rctx, &config);

	/* Cleanup should be graceful and not crash */
	reflector_cleanup(&rctx);

	PASS();
}

/*
 * Test CPU affinity configuration
 */
void test_cpu_affinity(void)
{
	TEST("cpu_affinity");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Set CPU affinity */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.cpu_affinity = 0; /* First CPU */
	int ret = reflector_set_config(&rctx, &config);

	/* May not be supported on all platforms */
	if (ret < 0) {
		reflector_cleanup(&rctx);
		PASS(); /* Not supported is OK */
		return;
	}

	reflector_cleanup(&rctx);
	PASS();
}

/*
 * Test batch size configuration
 */
void test_batch_size(void)
{
	TEST("batch_size");

	reflector_ctx_t rctx = {0};

	if (reflector_init(&rctx, LOOPBACK_IF) < 0) {
		FAIL("Failed to initialize reflector");
		return;
	}

	/* Set batch size */
	reflector_config_t config;
	reflector_get_config(&rctx, &config);
	config.batch_size = 64;
	if (reflector_set_config(&rctx, &config) < 0) {
		FAIL("Failed to set batch size");
		reflector_cleanup(&rctx);
		return;
	}

	reflector_get_config(&rctx, &config);
	if (config.batch_size != 64) {
		FAIL("Batch size not applied");
		reflector_cleanup(&rctx);
		return;
	}

	/* Test invalid batch size */
	config.batch_size = 0;
	int ret = reflector_set_config(&rctx, &config);
	(void)ret; /* May fail or use default, both are acceptable */

	reflector_cleanup(&rctx);
	PASS();
}

int main(void)
{
	printf("Running platform and multi-worker integration tests...\n\n");

	/* Reduce log noise */
	reflector_set_log_level(LOG_ERROR);

	/* Basic tests */
	test_basic_init();
	test_platform_fallback();

	/* Worker tests */
	test_single_worker_init();
	test_multi_worker_init();
	test_worker_scaling();
	test_worker_limit();
	test_concurrent_contexts();

	/* Stats tests */
	test_stats_get();
	test_stats_reset();

	/* Configuration tests */
	test_cpu_affinity();
	test_batch_size();

	/* Cleanup tests */
	test_graceful_shutdown();

	/* Summary */
	printf("\n=================================\n");
	printf("Tests passed: %d\n", tests_passed);
	printf("Tests failed: %d\n", tests_failed);
	printf("=================================\n");

	if (tests_failed == 0) {
		printf("All platform/worker tests passed!\n");
		return 0;
	} else {
		printf("Some tests failed\n");
		return 1;
	}
}
