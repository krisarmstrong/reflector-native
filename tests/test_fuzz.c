/*
 * test_fuzz.c - Fuzz testing for packet validation logic
 *
 * Tests packet validation against:
 * - Randomized packet data
 * - Edge case packet lengths
 * - Corrupted headers
 * - Malformed fields
 *
 * Run with: ./test_fuzz [iterations] [seed]
 */

#include "reflector.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_ITERATIONS 100000
#define MAX_PACKET_SIZE 9216 /* Jumbo frame + overhead */

int tests_run = 0;
int crashes_detected = 0;
int invalid_packets = 0;
int valid_packets = 0;

static unsigned int seed;

/* PRNG for reproducible fuzzing */
static uint32_t xorshift32(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static uint8_t random_byte(void)
{
	return (uint8_t)(xorshift32() & 0xFF);
}

static uint32_t random_range(uint32_t min, uint32_t max)
{
	if (min >= max)
		return min;
	return min + (xorshift32() % (max - min + 1));
}

/* Generate completely random packet */
static size_t generate_random_packet(uint8_t *buffer, size_t max_len)
{
	size_t len = random_range(0, max_len);
	for (size_t i = 0; i < len; i++) {
		buffer[i] = random_byte();
	}
	return len;
}

/* Generate packet with valid Ethernet header but random payload */
static size_t generate_eth_random_payload(uint8_t *buffer, const uint8_t mac[6])
{
	size_t len = random_range(14, 1518);

	/* Valid Ethernet header */
	memcpy(&buffer[0], mac, 6);                         /* dst MAC */
	for (int i = 6; i < 12; i++) buffer[i] = random_byte(); /* src MAC */
	buffer[12] = 0x08;
	buffer[13] = 0x00; /* IPv4 */

	/* Random payload */
	for (size_t i = 14; i < len; i++) {
		buffer[i] = random_byte();
	}
	return len;
}

/* Generate packet with valid Eth+IP but random UDP/payload */
static size_t generate_ip_random_payload(uint8_t *buffer, const uint8_t mac[6])
{
	size_t len = random_range(34, 1518);

	/* Ethernet header */
	memcpy(&buffer[0], mac, 6);
	for (int i = 6; i < 12; i++) buffer[i] = random_byte();
	buffer[12] = 0x08;
	buffer[13] = 0x00;

	/* Valid IP header */
	buffer[14] = 0x45; /* version=4, ihl=5 */
	buffer[15] = 0x00;
	buffer[16] = (len - 14) >> 8;
	buffer[17] = (len - 14) & 0xFF;
	buffer[18] = random_byte();
	buffer[19] = random_byte();
	buffer[20] = 0x40;
	buffer[21] = 0x00;
	buffer[22] = 0x40; /* TTL */
	buffer[23] = 0x11; /* UDP */
	buffer[24] = 0x00;
	buffer[25] = 0x00;
	/* Random IPs */
	for (int i = 26; i < 34; i++) buffer[i] = random_byte();

	/* Random UDP and payload */
	for (size_t i = 34; i < len; i++) {
		buffer[i] = random_byte();
	}
	return len;
}

/* Generate nearly-valid ITO packet with random mutations */
static size_t generate_mutated_ito(uint8_t *buffer, const uint8_t mac[6])
{
	/* Start with valid ITO packet template */
	uint8_t template[64] = {
	    /* Ethernet */
	    0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b, /* dst (will be replaced) */
	    0x00, 0xc0, 0x17, 0x54, 0x05, 0x98, /* src */
	    0x08, 0x00,                         /* IPv4 */
	    /* IP */
	    0x45, 0x00, 0x00, 0x27, 0x00, 0x00, 0x40, 0x00,
	    0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x0a,
	    0xc0, 0xa8, 0x00, 0x01,
	    /* UDP */
	    0x0f, 0x02, 0x0f, 0x02, 0x00, 0x13, 0x00, 0x00,
	    /* ITO signature */
	    0x09, 0x10, 0xea, 0x1d, 0x00,
	    'P', 'R', 'O', 'B', 'E', 'O', 'T',
	};

	memcpy(buffer, template, sizeof(template));
	memcpy(&buffer[0], mac, 6); /* Set correct dst MAC */

	/* Apply random mutations */
	int num_mutations = random_range(0, 5);
	for (int i = 0; i < num_mutations; i++) {
		int pos = random_range(0, sizeof(template) - 1);
		buffer[pos] = random_byte();
	}

	return sizeof(template);
}

/* Generate packet with edge case sizes */
static size_t generate_edge_size_packet(uint8_t *buffer, const uint8_t mac[6])
{
	/* Edge case sizes that might trigger bugs */
	static const size_t edge_sizes[] = {
	    0, 1, 13, 14, 15, 33, 34, 35, 41, 42, 43, 46, 47,
	    53, 54, 55, 64, 65, 127, 128, 1500, 1514, 1518, 1519,
	    8999, 9000, 9001, 9216
	};

	int idx = random_range(0, sizeof(edge_sizes) / sizeof(edge_sizes[0]) - 1);
	size_t len = edge_sizes[idx];

	if (len > MAX_PACKET_SIZE) len = MAX_PACKET_SIZE;

	/* Fill with pattern that includes valid header if possible */
	if (len >= 14) {
		memcpy(&buffer[0], mac, 6);
		for (size_t i = 6; i < len; i++) buffer[i] = random_byte();
		buffer[12] = 0x08;
		buffer[13] = 0x00;
	} else {
		for (size_t i = 0; i < len; i++) buffer[i] = random_byte();
	}

	return len;
}

/* Generate packet with corrupted IP header length */
static size_t generate_bad_ihl(uint8_t *buffer, const uint8_t mac[6])
{
	size_t len = 64;

	/* Ethernet */
	memcpy(&buffer[0], mac, 6);
	for (int i = 6; i < 12; i++) buffer[i] = random_byte();
	buffer[12] = 0x08;
	buffer[13] = 0x00;

	/* IP with bad IHL (0-4, or > 15) */
	int bad_ihl = random_range(0, 4); /* IHL must be >= 5 */
	if (random_range(0, 1)) {
		bad_ihl = random_range(0, 15); /* Sometimes try all values */
	}
	buffer[14] = 0x40 | (bad_ihl & 0x0F); /* version=4, ihl=bad */

	/* Rest random */
	for (size_t i = 15; i < len; i++) buffer[i] = random_byte();

	return len;
}

/* Generate IPv6 packet (should be rejected) */
static size_t generate_ipv6_packet(uint8_t *buffer, const uint8_t mac[6])
{
	size_t len = random_range(54, 1518);

	/* Ethernet with IPv6 */
	memcpy(&buffer[0], mac, 6);
	for (int i = 6; i < 12; i++) buffer[i] = random_byte();
	buffer[12] = 0x86;
	buffer[13] = 0xDD; /* IPv6 */

	/* Random IPv6 content */
	buffer[14] = 0x60 | (random_byte() & 0x0F); /* version 6 */
	for (size_t i = 15; i < len; i++) buffer[i] = random_byte();

	return len;
}

/* Generate broadcast/multicast packets (should be rejected) */
static size_t generate_broadcast_packet(uint8_t *buffer)
{
	size_t len = 64;

	/* Broadcast MAC */
	memset(&buffer[0], 0xFF, 6);
	for (int i = 6; i < 12; i++) buffer[i] = random_byte();
	buffer[12] = 0x08;
	buffer[13] = 0x00;

	for (size_t i = 14; i < len; i++) buffer[i] = random_byte();

	return len;
}

static size_t generate_multicast_packet(uint8_t *buffer)
{
	size_t len = 64;

	/* IPv4 multicast MAC: 01:00:5E:xx:xx:xx */
	buffer[0] = 0x01;
	buffer[1] = 0x00;
	buffer[2] = 0x5E;
	buffer[3] = random_byte() & 0x7F;
	buffer[4] = random_byte();
	buffer[5] = random_byte();
	for (int i = 6; i < 12; i++) buffer[i] = random_byte();
	buffer[12] = 0x08;
	buffer[13] = 0x00;

	for (size_t i = 14; i < len; i++) buffer[i] = random_byte();

	return len;
}

/* Test packet validation doesn't crash on fuzzed input */
static void fuzz_is_ito_packet(int iterations, const uint8_t mac[6])
{
	uint8_t buffer[MAX_PACKET_SIZE];
	reflector_config_t config = {0};
	memcpy(config.mac, mac, 6);
	config.filter_oui = false;
	config.ito_port = 0;
	config.reflect_mode = REFLECT_MODE_ALL;

	printf("Running %d fuzz iterations on is_ito_packet...\n", iterations);

	for (int i = 0; i < iterations; i++) {
		size_t len = 0;
		int test_type = random_range(0, 9);

		switch (test_type) {
		case 0:
			len = generate_random_packet(buffer, MAX_PACKET_SIZE);
			break;
		case 1:
			len = generate_eth_random_payload(buffer, mac);
			break;
		case 2:
			len = generate_ip_random_payload(buffer, mac);
			break;
		case 3:
			len = generate_mutated_ito(buffer, mac);
			break;
		case 4:
			len = generate_edge_size_packet(buffer, mac);
			break;
		case 5:
			len = generate_bad_ihl(buffer, mac);
			break;
		case 6:
			len = generate_ipv6_packet(buffer, mac);
			break;
		case 7:
			len = generate_broadcast_packet(buffer);
			break;
		case 8:
			len = generate_multicast_packet(buffer);
			break;
		default:
			len = generate_random_packet(buffer, 64);
			break;
		}

		/* This should never crash, regardless of input */
		bool result = is_ito_packet(buffer, len, &config);
		tests_run++;

		if (result) {
			valid_packets++;
		} else {
			invalid_packets++;
		}

		/* Progress indicator */
		if (i > 0 && i % 10000 == 0) {
			printf("  Progress: %d/%d iterations\n", i, iterations);
		}
	}
}

/* Test packet reflection doesn't crash on fuzzed input */
static void fuzz_reflect_packet(int iterations)
{
	uint8_t buffer[MAX_PACKET_SIZE];

	printf("Running %d fuzz iterations on reflect_packet_inplace...\n", iterations);

	for (int i = 0; i < iterations; i++) {
		size_t len = generate_random_packet(buffer, MAX_PACKET_SIZE);

		/* Only call on packets large enough to have headers */
		if (len >= 42) {
			reflect_packet_inplace(buffer, len);
		}

		tests_run++;

		if (i > 0 && i % 10000 == 0) {
			printf("  Progress: %d/%d iterations\n", i, iterations);
		}
	}
}

int main(int argc, char *argv[])
{
	int iterations = DEFAULT_ITERATIONS;

	if (argc > 1) {
		iterations = atoi(argv[1]);
		if (iterations <= 0)
			iterations = DEFAULT_ITERATIONS;
	}

	if (argc > 2) {
		seed = (unsigned int)atoi(argv[2]);
	} else {
		seed = (unsigned int)time(NULL);
	}

	printf("=================================\n");
	printf("Reflector Fuzz Testing\n");
	printf("=================================\n");
	printf("Iterations: %d\n", iterations);
	printf("Seed: %u (reproduce with: ./test_fuzz %d %u)\n", seed, iterations, seed);
	printf("\n");

	uint8_t mac[6] = {0x00, 0x01, 0x55, 0x17, 0x1e, 0x1b};

	/* Fuzz is_ito_packet */
	fuzz_is_ito_packet(iterations, mac);

	/* Fuzz reflect_packet_inplace */
	fuzz_reflect_packet(iterations / 2);

	printf("\n=================================\n");
	printf("Fuzz Testing Complete\n");
	printf("=================================\n");
	printf("Total tests: %d\n", tests_run);
	printf("Valid packets detected: %d\n", valid_packets);
	printf("Invalid packets (correctly rejected): %d\n", invalid_packets);
	printf("Crashes: %d\n", crashes_detected);
	printf("\n");

	if (crashes_detected == 0) {
		printf("SUCCESS: No crashes detected during fuzz testing\n");
		return 0;
	} else {
		printf("FAILURE: Crashes detected! Check logs.\n");
		return 1;
	}
}
