/*
 * util.c - Utility functions for interface management and system queries
 *
 * Copyright (c) 2025 Kris Armstrong
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>

#ifdef __linux__
#include <linux/ethtool.h>
#include <linux/sockios.h>
#endif

#ifdef __APPLE__
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif

#include "reflector.h"

/* Current log level */
static log_level_t current_log_level = LOG_INFO;

/*
 * Set logging level
 */
void reflector_set_log_level(log_level_t level)
{
    current_log_level = level;
}

/*
 * Logging function
 */
void reflector_log(log_level_t level, const char *fmt, ...)
{
    if (level < current_log_level) {
        return;
    }

    const char *level_str[] = {
        [LOG_DEBUG] = "DEBUG",
        [LOG_INFO]  = "INFO",
        [LOG_WARN]  = "WARN",
        [LOG_ERROR] = "ERROR"
    };

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        ts.tv_sec = 0;
        ts.tv_nsec = 0;
    }

    fprintf(stderr, "[%ld.%06ld] [%s] ",
            ts.tv_sec, ts.tv_nsec / 1000, level_str[level]);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

/*
 * Get interface index from name
 */
int get_interface_index(const char *ifname)
{
    unsigned int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        reflector_log(LOG_ERROR, "Interface %s not found: %s", ifname, strerror(errno));
        return -1;
    }
    return (int)ifindex;
}

/*
 * Get MAC address of interface
 */
int get_interface_mac(const char *ifname, uint8_t mac[6])
{
#ifdef __linux__
    int fd, ret;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        reflector_log(LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
    if (ret < 0) {
        reflector_log(LOG_ERROR, "Failed to get MAC address for %s: %s",
                     ifname, strerror(errno));
        close(fd);
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(fd);

#elif defined(__APPLE__)
    /* macOS uses getifaddrs() to get MAC address */
    struct ifaddrs *ifap, *ifaptr;

    if (getifaddrs(&ifap) != 0) {
        reflector_log(LOG_ERROR, "Failed to get interface list: %s", strerror(errno));
        return -1;
    }

    for (ifaptr = ifap; ifaptr != NULL; ifaptr = ifaptr->ifa_next) {
        if (strcmp(ifaptr->ifa_name, ifname) == 0 &&
            ifaptr->ifa_addr->sa_family == AF_LINK) {
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifaptr->ifa_addr;
            memcpy(mac, LLADDR(sdl), 6);
            freeifaddrs(ifap);

            reflector_log(LOG_DEBUG, "Interface %s MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                         ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return 0;
        }
    }

    freeifaddrs(ifap);
    reflector_log(LOG_ERROR, "Failed to find MAC address for %s", ifname);
    return -1;
#endif

    reflector_log(LOG_DEBUG, "Interface %s MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 ifname, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return 0;
}

/*
 * Get number of RX queues for interface
 * Returns 1 if unable to determine (fallback)
 */
int get_num_rx_queues(const char *ifname)
{
#ifdef __linux__
    int fd, ret;
    struct ifreq ifr;
    struct ethtool_channels channels;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        reflector_log(LOG_WARN, "Failed to create socket for queue query, assuming 1 queue");
        return 1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    memset(&channels, 0, sizeof(channels));
    channels.cmd = ETHTOOL_GCHANNELS;
    ifr.ifr_data = (char *)&channels;

    ret = ioctl(fd, SIOCETHTOOL, &ifr);
    close(fd);

    if (ret < 0) {
        reflector_log(LOG_WARN, "Failed to query channels for %s, assuming 1 queue: %s",
                     ifname, strerror(errno));
        return 1;
    }

    /* Use combined channels if available, otherwise RX channels */
    int num_queues = channels.combined_count ? channels.combined_count : channels.rx_count;

    if (num_queues == 0) {
        num_queues = 1;
    }

    reflector_log(LOG_DEBUG, "Interface %s has %d RX queues", ifname, num_queues);
    return num_queues;
#else
    /* macOS doesn't expose queue information */
    (void)ifname;
    return 1;
#endif
}

/*
 * Get CPU affinity for a specific queue
 * Returns -1 if unable to determine
 *
 * On Linux, this reads /proc/irq/<irq>/smp_affinity to find which CPU
 * handles the IRQ for this queue. This is a best-effort heuristic.
 */
int get_queue_cpu_affinity(const char *ifname, int queue_id)
{
#ifdef __linux__
    /* For now, simple round-robin assignment */
    /* TODO: Parse /proc/irq for actual IRQ affinity */
    (void)ifname;
    return queue_id % sysconf(_SC_NPROCESSORS_ONLN);
#else
    (void)ifname;
    (void)queue_id;
    return -1;
#endif
}

/*
 * Get high-resolution timestamp in nanoseconds
 */
uint64_t get_timestamp_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        return 0;  /* Fallback on error */
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/*
 * Set interface promiscuous mode
 */
int set_interface_promisc(const char *ifname, bool enable)
{
    int fd, ret;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        reflector_log(LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    /* Get current flags */
    ret = ioctl(fd, SIOCGIFFLAGS, &ifr);
    if (ret < 0) {
        reflector_log(LOG_ERROR, "Failed to get interface flags: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /* Modify promiscuous flag */
    if (enable) {
        ifr.ifr_flags |= IFF_PROMISC;
    } else {
        ifr.ifr_flags &= ~IFF_PROMISC;
    }

    /* Set new flags */
    ret = ioctl(fd, SIOCSIFFLAGS, &ifr);
    if (ret < 0) {
        reflector_log(LOG_ERROR, "Failed to set interface flags: %s", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    reflector_log(LOG_DEBUG, "Interface %s promiscuous mode: %s",
                 ifname, enable ? "enabled" : "disabled");
    return 0;
}

/*
 * Check if interface is up
 */
bool is_interface_up(const char *ifname)
{
    int fd, ret;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    ret = ioctl(fd, SIOCGIFFLAGS, &ifr);
    close(fd);

    if (ret < 0) {
        return false;
    }

    return (ifr.ifr_flags & IFF_UP) != 0;
}
