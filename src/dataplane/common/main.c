/*
 * main.c - Simple CLI for testing reflector dataplane
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "reflector.h"

static volatile bool g_running = true;
static reflector_ctx_t g_rctx;

void signal_handler(int sig)
{
    (void)sig;
    g_running = false;
}

void print_stats(const reflector_stats_t *stats, double elapsed)
{
    double pps = stats->packets_reflected / elapsed;
    double mbps = (stats->bytes_reflected * 8.0) / (elapsed * 1000000.0);

    printf("\r[%.1fs] RX: %llu pkts (%llu bytes) | "
           "Reflected: %llu pkts (%llu bytes) | "
           "%.0f pps, %.2f Mbps   ",
           elapsed,
           (unsigned long long)stats->packets_received,
           (unsigned long long)stats->bytes_received,
           (unsigned long long)stats->packets_reflected,
           (unsigned long long)stats->bytes_reflected,
           pps, mbps);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface> [-v]\n", argv[0]);
        return 1;
    }

    const char *ifname = argv[1];
    bool verbose = (argc > 2 && strcmp(argv[2], "-v") == 0);

    if (verbose) {
        reflector_set_log_level(LOG_DEBUG);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Network Reflector v%d.%d.%d\n",
           REFLECTOR_VERSION_MAJOR, REFLECTOR_VERSION_MINOR, REFLECTOR_VERSION_PATCH);
    printf("Starting on interface: %s\n", ifname);

    if (reflector_init(&g_rctx, ifname) < 0) {
        fprintf(stderr, "Failed to initialize reflector\n");
        return 1;
    }

    if (reflector_start(&g_rctx) < 0) {
        fprintf(stderr, "Failed to start reflector\n");
        reflector_cleanup(&g_rctx);
        return 1;
    }

    printf("Reflector running... Press Ctrl-C to stop\n\n");

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (g_running) {
        sleep(1);

        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) +
                        (now.tv_nsec - start.tv_nsec) / 1e9;

        reflector_stats_t stats;
        reflector_get_stats(&g_rctx, &stats);
        print_stats(&stats, elapsed);
    }

    printf("\n\nStopping reflector...\n");

    reflector_stats_t final_stats;
    reflector_get_stats(&g_rctx, &final_stats);

    reflector_cleanup(&g_rctx);

    printf("\nFinal Statistics:\n");
    printf("  Packets received:  %llu\n", (unsigned long long)final_stats.packets_received);
    printf("  Packets reflected: %llu\n", (unsigned long long)final_stats.packets_reflected);
    printf("  Bytes received:    %llu\n", (unsigned long long)final_stats.bytes_received);
    printf("  Bytes reflected:   %llu\n", (unsigned long long)final_stats.bytes_reflected);
    printf("  TX errors:         %llu\n", (unsigned long long)final_stats.tx_errors);

    return 0;
}
