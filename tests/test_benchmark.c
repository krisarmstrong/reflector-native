/*
 * test_benchmark.c - Performance benchmarks for packet reflection
 */

#include "reflector.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BENCHMARK_ITERATIONS 1000000

/* Benchmark packet reflection performance */
void benchmark_packet_reflection(void)
{
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

	uint64_t start = get_timestamp_ns();

	for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
		reflect_packet_inplace(packet, sizeof(packet));
		reflect_packet_inplace(packet, sizeof(packet)); /* Swap back */
	}

	uint64_t end = get_timestamp_ns();
	uint64_t elapsed_ns = end - start;

	double elapsed_sec = elapsed_ns / 1000000000.0;
	double ops_per_sec = BENCHMARK_ITERATIONS / elapsed_sec;
	double ns_per_op = (double)elapsed_ns / BENCHMARK_ITERATIONS;

	printf("Packet Reflection Benchmark:\n");
	printf("  Iterations: %d\n", BENCHMARK_ITERATIONS);
	printf("  Total time: %.3f seconds\n", elapsed_sec);
	printf("  Operations/sec: %.2f M ops/sec\n", ops_per_sec / 1000000.0);
	printf("  Time per operation: %.2f ns\n", ns_per_op);
	printf("\n");
}

/* Benchmark ITO packet validation */
void benchmark_packet_validation(void)
{
	uint8_t packet[64] = {
	    0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b, 0x00, 0xc0, 0x17, 0x54, 0x05, 0x98, 0x08, 0x00,
	    0x45, 0x00, 0x00, 0x27, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8,
	    0x00, 0x0a, 0xc0, 0xa8, 0x00, 0x01, 0x0f, 0x02, 0x0f, 0x02, 0x00, 0x13, 0x00, 0x00,
	    0x09, 0x10, 0xea, 0x1d, 0x00, 'P',  'R',  'O',  'B',  'E',  'O',  'T',
	};

	/* Create config with MAC address */
	reflector_config_t config = {0};
	config.mac[0] = 0x00;
	config.mac[1] = 0x01;
	config.mac[2] = 0x55;
	config.mac[3] = 0x17;
	config.mac[4] = 0x1e;
	config.mac[5] = 0x1b;
	config.reflect_mode = REFLECT_MODE_ALL;

	uint64_t start = get_timestamp_ns();
	volatile bool result;

	for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
		result = is_ito_packet(packet, sizeof(packet), &config);
	}

	uint64_t end = get_timestamp_ns();
	uint64_t elapsed_ns = end - start;

	double elapsed_sec = elapsed_ns / 1000000000.0;
	double ops_per_sec = BENCHMARK_ITERATIONS / elapsed_sec;
	double ns_per_op = (double)elapsed_ns / BENCHMARK_ITERATIONS;

	printf("Packet Validation Benchmark:\n");
	printf("  Iterations: %d\n", BENCHMARK_ITERATIONS);
	printf("  Total time: %.3f seconds\n", elapsed_sec);
	printf("  Operations/sec: %.2f M ops/sec\n", ops_per_sec / 1000000.0);
	printf("  Time per operation: %.2f ns\n", ns_per_op);
	printf("  Result: %s (validation successful)\n", result ? "true" : "false");
	printf("\n");
}

/* Benchmark signature type detection */
void benchmark_signature_detection(void)
{
	uint8_t packet[64] = {
	    0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b, 0x00, 0xc0, 0x17, 0x54, 0x05, 0x98, 0x08, 0x00,
	    0x45, 0x00, 0x00, 0x27, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8,
	    0x00, 0x0a, 0xc0, 0xa8, 0x00, 0x01, 0x0f, 0x02, 0x0f, 0x02, 0x00, 0x13, 0x00, 0x00,
	    0x09, 0x10, 0xea, 0x1d, 0x00, 'P',  'R',  'O',  'B',  'E',  'O',  'T',
	};

	uint64_t start = get_timestamp_ns();
	volatile ito_sig_type_t type;

	for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
		type = get_ito_signature_type(packet, sizeof(packet));
	}

	uint64_t end = get_timestamp_ns();
	uint64_t elapsed_ns = end - start;

	double elapsed_sec = elapsed_ns / 1000000000.0;
	double ops_per_sec = BENCHMARK_ITERATIONS / elapsed_sec;
	double ns_per_op = (double)elapsed_ns / BENCHMARK_ITERATIONS;

	printf("Signature Detection Benchmark:\n");
	printf("  Iterations: %d\n", BENCHMARK_ITERATIONS);
	printf("  Total time: %.3f seconds\n", elapsed_sec);
	printf("  Operations/sec: %.2f M ops/sec\n", ops_per_sec / 1000000.0);
	printf("  Time per operation: %.2f ns\n", ns_per_op);
	printf("  Result: %d (signature type)\n", type);
	printf("\n");
}

int main(void)
{
	printf("===================================\n");
	printf("Performance Benchmarks\n");
	printf("===================================\n\n");

	benchmark_packet_reflection();
	benchmark_packet_validation();
	benchmark_signature_detection();

	printf("===================================\n");
	printf("Benchmarks complete!\n");
	printf("===================================\n");

	return 0;
}
