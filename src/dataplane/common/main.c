/*
 * main.c - Simple CLI for testing reflector dataplane
 */

#include "reflector.h"

#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "platform_config.h"

static volatile bool g_running = true;
static reflector_ctx_t g_rctx;
static stats_format_t g_stats_format = STATS_FORMAT_TEXT;
static int g_stats_interval = 10; /* Default 10 seconds */

void signal_handler(int sig)
{
	(void)sig;
	g_running = false;
}

void print_stats_text(const reflector_stats_t *stats, double elapsed)
{
	double pps = (elapsed > 0) ? stats->packets_reflected / elapsed : 0.0;
	double mbps = (elapsed > 0) ? (stats->bytes_reflected * 8.0) / (elapsed * 1000000.0) : 0.0;

	printf("\r[%.1fs] RX: %" PRIu64 " pkts (%" PRIu64 " bytes) | "
	       "Reflected: %" PRIu64 " pkts | "
	       "%.0f pps, %.2f Mbps",
	       elapsed, stats->packets_received, stats->bytes_received, stats->packets_reflected, pps,
	       mbps);

	/* Show signature breakdown if any packets */
	if (stats->packets_reflected > 0) {
		printf(" | PROBEOT:%" PRIu64 " DATA:%" PRIu64 " LAT:%" PRIu64, stats->sig_probeot_count,
		       stats->sig_dataot_count, stats->sig_latency_count);
	}

	/* Show latency if measured */
	if (stats->latency.count > 0) {
		printf(" | Latency: %.1f/%.1f/%.1f us (min/avg/max)", stats->latency.min_ns / 1000.0,
		       stats->latency.avg_ns / 1000.0, stats->latency.max_ns / 1000.0);
	}

	printf("   ");
	fflush(stdout);
}

void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <interface> [options]\n", prog);
	fprintf(stderr, "\nGeneral Options:\n");
	fprintf(stderr, "  -v, --verbose       Enable verbose logging\n");
	fprintf(stderr, "  --json              Output statistics in JSON format\n");
	fprintf(stderr, "  --csv               Output statistics in CSV format\n");
	fprintf(stderr, "  --latency           Enable latency measurements\n");
	fprintf(stderr, "  --stats-interval N  Statistics update interval in seconds (default: 10)\n");
	fprintf(stderr, "\nPacket Filtering Options:\n");
	fprintf(stderr, "  --port N            ITO UDP port to match (default: 3842, 0 = any)\n");
	fprintf(stderr, "  --no-oui-filter     Disable source MAC OUI filtering\n");
	fprintf(stderr, "  --oui XX:XX:XX      Custom source OUI (default: 00:c0:17 NetAlly)\n");
	fprintf(stderr, "\nReflection Mode:\n");
	fprintf(stderr, "  --mode MODE         What to swap: mac, mac-ip, or all (default: all)\n");
	fprintf(stderr, "                        mac    = Ethernet MAC only\n");
	fprintf(stderr, "                        mac-ip = MAC + IP addresses\n");
	fprintf(stderr, "                        all    = MAC + IP + UDP ports\n");
#if HAVE_DPDK
	fprintf(stderr, "\nDPDK Options (100G line-rate mode):\n");
	fprintf(stderr, "  --dpdk              Use DPDK instead of AF_XDP (requires NIC binding)\n");
	fprintf(stderr, "  --dpdk-args ARGS    Pass arguments to DPDK EAL (e.g., \"--lcores=1-4\")\n");
#endif
	fprintf(stderr, "\n  -h, --help          Show this help message\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	/* Check for help first */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		}
	}

	const char *ifname = argv[1];
	bool verbose = false;
	bool measure_latency = false;

	/* ITO packet filtering defaults */
	uint16_t ito_port = ITO_UDP_PORT; /* Default port 3842 */
	bool filter_oui = true;           /* Filter by NetAlly OUI by default */
	uint8_t oui[3] = {NETALLY_OUI_BYTE0, NETALLY_OUI_BYTE1, NETALLY_OUI_BYTE2};
	reflect_mode_t reflect_mode = REFLECT_MODE_ALL;

#if HAVE_DPDK
	bool use_dpdk = false;
	char *dpdk_args = NULL;
#endif

	/* Parse options */
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			verbose = true;
		} else if (strcmp(argv[i], "--json") == 0) {
			g_stats_format = STATS_FORMAT_JSON;
		} else if (strcmp(argv[i], "--csv") == 0) {
			g_stats_format = STATS_FORMAT_CSV;
		} else if (strcmp(argv[i], "--latency") == 0) {
			measure_latency = true;
		} else if (strcmp(argv[i], "--stats-interval") == 0) {
			if (i + 1 < argc) {
				char *endptr;
				long val = strtol(argv[++i], &endptr, 10);
				if (*endptr != '\0' || val <= 0 || val > INT_MAX) {
					fprintf(stderr, "Invalid stats interval: %s\n", argv[i]);
					return 1;
				}
				g_stats_interval = (int)val;
			} else {
				fprintf(stderr, "Missing value for --stats-interval\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--port") == 0) {
			if (i + 1 < argc) {
				char *endptr;
				long val = strtol(argv[++i], &endptr, 10);
				if (*endptr != '\0' || val < 0 || val > 65535) {
					fprintf(stderr, "Invalid port: %s (must be 0-65535)\n", argv[i]);
					return 1;
				}
				ito_port = (uint16_t)val;
			} else {
				fprintf(stderr, "Missing value for --port\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--no-oui-filter") == 0) {
			filter_oui = false;
		} else if (strcmp(argv[i], "--oui") == 0) {
			if (i + 1 < argc) {
				unsigned int b0, b1, b2;
				if (sscanf(argv[++i], "%x:%x:%x", &b0, &b1, &b2) != 3 || b0 > 255 || b1 > 255 ||
				    b2 > 255) {
					fprintf(stderr, "Invalid OUI format: %s (use XX:XX:XX)\n", argv[i]);
					return 1;
				}
				oui[0] = (uint8_t)b0;
				oui[1] = (uint8_t)b1;
				oui[2] = (uint8_t)b2;
			} else {
				fprintf(stderr, "Missing value for --oui\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--mode") == 0) {
			if (i + 1 < argc) {
				i++;
				if (strcmp(argv[i], "mac") == 0) {
					reflect_mode = REFLECT_MODE_MAC;
				} else if (strcmp(argv[i], "mac-ip") == 0) {
					reflect_mode = REFLECT_MODE_MAC_IP;
				} else if (strcmp(argv[i], "all") == 0) {
					reflect_mode = REFLECT_MODE_ALL;
				} else {
					fprintf(stderr, "Invalid mode: %s (use mac, mac-ip, or all)\n", argv[i]);
					return 1;
				}
			} else {
				fprintf(stderr, "Missing value for --mode\n");
				return 1;
			}
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
#if HAVE_DPDK
		} else if (strcmp(argv[i], "--dpdk") == 0) {
			use_dpdk = true;
		} else if (strcmp(argv[i], "--dpdk-args") == 0) {
			if (i + 1 < argc) {
				dpdk_args = argv[++i];
			} else {
				fprintf(stderr, "Missing value for --dpdk-args\n");
				return 1;
			}
#endif
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	if (verbose) {
		reflector_set_log_level(LOG_DEBUG);
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	printf("Network Reflector v%d.%d.%d\n", REFLECTOR_VERSION_MAJOR, REFLECTOR_VERSION_MINOR,
	       REFLECTOR_VERSION_PATCH);
	printf("Starting on interface: %s\n", ifname);

	if (reflector_init(&g_rctx, ifname) < 0) {
		fprintf(stderr, "Failed to initialize reflector\n");
		return 1;
	}

	/* Configure options */
	g_rctx.config.measure_latency = measure_latency;
	g_rctx.config.stats_format = g_stats_format;
	g_rctx.config.stats_interval_sec = g_stats_interval;

	/* ITO filtering options */
	g_rctx.config.ito_port = ito_port;
	g_rctx.config.filter_oui = filter_oui;
	memcpy(g_rctx.config.oui, oui, 3);
	g_rctx.config.reflect_mode = reflect_mode;

#if HAVE_DPDK
	g_rctx.config.use_dpdk = use_dpdk;
	g_rctx.config.dpdk_args = dpdk_args;
#endif

	if (reflector_start(&g_rctx) < 0) {
		fprintf(stderr, "Failed to start reflector\n");
		reflector_cleanup(&g_rctx);
		return 1;
	}

	if (g_stats_format == STATS_FORMAT_TEXT) {
		printf("Reflector running... Press Ctrl-C to stop\n");
		if (measure_latency) {
			printf("Latency measurement: ENABLED\n");
		}
		printf("\n");
	}

	struct timespec start, now, last_stats;
	clock_gettime(CLOCK_MONOTONIC, &start);
	last_stats = start;

	while (g_running) {
		sleep(1);

		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
		double since_last =
		    (now.tv_sec - last_stats.tv_sec) + (now.tv_nsec - last_stats.tv_nsec) / 1e9;

		/* Print stats at interval */
		if (since_last >= g_stats_interval) {
			reflector_stats_t stats;
			reflector_get_stats(&g_rctx, &stats);

			switch (g_stats_format) {
			case STATS_FORMAT_JSON:
				reflector_print_stats_json(&stats);
				break;
			case STATS_FORMAT_CSV:
				reflector_print_stats_csv(&stats);
				break;
			case STATS_FORMAT_TEXT:
			default:
				print_stats_text(&stats, elapsed);
				break;
			}

			last_stats = now;
		}
	}

	if (g_stats_format == STATS_FORMAT_TEXT) {
		printf("\n\nStopping reflector...\n");
	}

	reflector_stats_t final_stats;
	reflector_get_stats(&g_rctx, &final_stats);

	reflector_cleanup(&g_rctx);

	if (g_stats_format == STATS_FORMAT_TEXT) {
		printf("\nFinal Statistics:\n");
		printf("  Packets received:  %" PRIu64 "\n", final_stats.packets_received);
		printf("  Packets reflected: %" PRIu64 "\n", final_stats.packets_reflected);
		printf("  Bytes received:    %" PRIu64 "\n", final_stats.bytes_received);
		printf("  Bytes reflected:   %" PRIu64 "\n", final_stats.bytes_reflected);
		printf("\nSignature Breakdown:\n");
		printf("  PROBEOT packets:   %" PRIu64 "\n", final_stats.sig_probeot_count);
		printf("  DATA:OT packets:   %" PRIu64 "\n", final_stats.sig_dataot_count);
		printf("  LATENCY packets:   %" PRIu64 "\n", final_stats.sig_latency_count);
		if (measure_latency && final_stats.latency.count > 0) {
			printf("\nLatency Statistics:\n");
			printf("  Measurements:      %" PRIu64 "\n", final_stats.latency.count);
			printf("  Min latency:       %.2f us\n", final_stats.latency.min_ns / 1000.0);
			printf("  Avg latency:       %.2f us\n", final_stats.latency.avg_ns / 1000.0);
			printf("  Max latency:       %.2f us\n", final_stats.latency.max_ns / 1000.0);
		}
		if (final_stats.tx_errors > 0 || final_stats.rx_invalid > 0) {
			printf("\nErrors:\n");
			printf("  TX errors:         %" PRIu64 "\n", final_stats.tx_errors);
			printf("  RX invalid:        %" PRIu64 "\n", final_stats.rx_invalid);
		}
	} else {
		/* Final stats in JSON/CSV */
		reflector_print_stats_formatted(&final_stats, g_stats_format);
	}

	return 0;
}
