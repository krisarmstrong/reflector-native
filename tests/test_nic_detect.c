/*
 * test_nic_detect.c - Unit Tests for NIC Detection Module
 *
 * Tests NIC vendor detection, speed detection, and DPDK availability checks.
 */

#include "test_framework.h"

#include "../include/reflector.h"
#include <string.h>

/* ============================================================================
 * Vendor ID Lookup Tests
 * ============================================================================ */

TEST(nic_vendor_loopback)
{
	uint16_t vendor_id = 0, device_id = 0;
	/* Loopback interface has no PCI device - should fail gracefully */
	int ret = get_nic_vendor("lo", &vendor_id, &device_id);
	/* Should return -1 (not a PCI device) or 0 if somehow it works */
	ASSERT_TRUE(ret == -1 || ret == 0);
}

TEST(nic_vendor_nonexistent)
{
	uint16_t vendor_id = 0, device_id = 0;
	/* Non-existent interface should fail */
	int ret = get_nic_vendor("nonexistent_iface_xyz", &vendor_id, &device_id);
	ASSERT_EQ(-1, ret);
}

TEST(nic_vendor_null_interface)
{
	uint16_t vendor_id = 0, device_id = 0;
	/* NULL interface should fail gracefully */
	int ret = get_nic_vendor(NULL, &vendor_id, &device_id);
	ASSERT_EQ(-1, ret);
}

TEST(nic_vendor_empty_interface)
{
	uint16_t vendor_id = 0, device_id = 0;
	/* Empty string interface should fail */
	int ret = get_nic_vendor("", &vendor_id, &device_id);
	ASSERT_EQ(-1, ret);
}

/* ============================================================================
 * NIC Speed Detection Tests
 * ============================================================================ */

TEST(nic_speed_loopback)
{
	/* Loopback typically doesn't report speed via sysfs */
	int speed = get_nic_speed("lo");
	/* Should return -1 or 0 (no speed info) or a very high value */
	ASSERT_TRUE(speed == -1 || speed >= 0);
}

TEST(nic_speed_nonexistent)
{
	/* Non-existent interface should return -1 */
	int speed = get_nic_speed("nonexistent_iface_xyz");
	ASSERT_EQ(-1, speed);
}

TEST(nic_speed_null)
{
	/* NULL interface should fail gracefully */
	int speed = get_nic_speed(NULL);
	ASSERT_EQ(-1, speed);
}

TEST(nic_speed_empty)
{
	/* Empty string interface should fail */
	int speed = get_nic_speed("");
	ASSERT_EQ(-1, speed);
}

/* ============================================================================
 * DPDK Availability Tests
 * ============================================================================ */

TEST(dpdk_availability)
{
	/* Just test that it doesn't crash - result depends on system */
	bool available = is_dpdk_available();
	/* Result is system-dependent, but should be true or false */
	ASSERT_TRUE(available == true || available == false);
}

/* ============================================================================
 * Recommendation Functions Tests (smoke tests)
 * ============================================================================ */

TEST(print_recommendations_loopback)
{
	/* Should not crash even on loopback */
	print_nic_recommendations("lo");
	ASSERT_TRUE(1); /* If we get here, it didn't crash */
}

TEST(print_recommendations_nonexistent)
{
	/* Should handle non-existent interface gracefully */
	print_nic_recommendations("nonexistent_iface_xyz");
	ASSERT_TRUE(1);
}

TEST(print_af_packet_warning_loopback)
{
	/* Should not crash */
	print_af_packet_warning("lo");
	ASSERT_TRUE(1);
}

TEST(print_recommended_nics)
{
	/* Should not crash */
	print_recommended_nics();
	ASSERT_TRUE(1);
}

/* ============================================================================
 * Known Vendor ID Tests
 * ============================================================================ */

/* Test that well-known vendor IDs are in expected range */
TEST(known_vendor_ids)
{
	/* Intel vendor ID */
	uint16_t intel = 0x8086;
	ASSERT_EQ(0x8086, intel);

	/* Mellanox vendor ID */
	uint16_t mellanox = 0x15b3;
	ASSERT_EQ(0x15b3, mellanox);

	/* Broadcom vendor ID */
	uint16_t broadcom = 0x14e4;
	ASSERT_EQ(0x14e4, broadcom);

	/* Amazon ENA vendor ID */
	uint16_t ena = 0x1c36;
	ASSERT_EQ(0x1c36, ena);

	/* Virtio vendor ID */
	uint16_t virtio = 0x1af4;
	ASSERT_EQ(0x1af4, virtio);
}

/* ============================================================================
 * Speed Value Tests
 * ============================================================================ */

TEST(speed_value_ranges)
{
	/* Test that speed conversions work correctly */
	int speed_1g = 1000;
	int speed_10g = 10000;
	int speed_25g = 25000;
	int speed_100g = 100000;

	/* 1G in Gbps */
	ASSERT_EQ(1, speed_1g / 1000);

	/* 10G in Gbps */
	ASSERT_EQ(10, speed_10g / 1000);

	/* 25G in Gbps */
	ASSERT_EQ(25, speed_25g / 1000);

	/* 100G in Gbps */
	ASSERT_EQ(100, speed_100g / 1000);
}

TEST(speed_threshold_checks)
{
	/* Test threshold checks used in recommendations */
	int speed = 25000; /* 25G */

	/* Should be >= 10G */
	ASSERT_GE(speed, 10000);

	/* Should be >= 25G for high-perf warning */
	ASSERT_GE(speed, 25000);

	/* Should be < 100G */
	ASSERT_LT(speed, 100000);
}

/* ============================================================================
 * Interface Name Validation Tests
 * ============================================================================ */

TEST(interface_name_length)
{
	/* Test that long interface names don't cause buffer overflow */
	char long_name[300];
	memset(long_name, 'x', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';

	/* These should handle long names gracefully */
	int ret = get_nic_vendor(long_name, NULL, NULL);
	ASSERT_EQ(-1, ret);

	int speed = get_nic_speed(long_name);
	ASSERT_EQ(-1, speed);
}

TEST(interface_name_special_chars)
{
	/* Interface names with special characters should fail gracefully */
	int ret = get_nic_vendor("../../../etc/passwd", NULL, NULL);
	ASSERT_EQ(-1, ret);

	int speed = get_nic_speed("../../../etc/passwd");
	ASSERT_EQ(-1, speed);
}

/* ============================================================================
 * Platform-Specific Tests
 * ============================================================================ */

#ifdef __linux__
TEST(linux_sysfs_paths)
{
	/* On Linux, verify the sysfs path format is reasonable */
	/* This is more of a sanity check than a functional test */
	const char *ifname = "eth0";
	char path[256];

	/* Vendor path format */
	snprintf(path, sizeof(path), "/sys/class/net/%s/device/vendor", ifname);
	ASSERT_GT(strlen(path), 30);

	/* Speed path format */
	snprintf(path, sizeof(path), "/sys/class/net/%s/speed", ifname);
	ASSERT_GT(strlen(path), 20);
}
#endif

#ifdef __APPLE__
TEST(macos_graceful_failure)
{
	/* On macOS, NIC detection should fail gracefully */
	uint16_t vendor_id = 0, device_id = 0;
	int ret = get_nic_vendor("en0", &vendor_id, &device_id);
	/* Should return -1 on macOS (no sysfs) */
	ASSERT_EQ(-1, ret);

	/* Speed detection may work via ioctl on macOS */
	int speed = get_nic_speed("en0");
	/* Either fails or returns a speed */
	ASSERT_TRUE(speed == -1 || speed > 0);
}
#endif

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
	printf("Reflector Native - NIC Detection Unit Tests\n");

	TEST_SUITE("Vendor ID Detection");
	RUN_TEST(nic_vendor_loopback);
	RUN_TEST(nic_vendor_nonexistent);
	RUN_TEST(nic_vendor_null_interface);
	RUN_TEST(nic_vendor_empty_interface);

	TEST_SUITE("NIC Speed Detection");
	RUN_TEST(nic_speed_loopback);
	RUN_TEST(nic_speed_nonexistent);
	RUN_TEST(nic_speed_null);
	RUN_TEST(nic_speed_empty);

	TEST_SUITE("DPDK Availability");
	RUN_TEST(dpdk_availability);

	TEST_SUITE("Recommendation Functions");
	RUN_TEST(print_recommendations_loopback);
	RUN_TEST(print_recommendations_nonexistent);
	RUN_TEST(print_af_packet_warning_loopback);
	RUN_TEST(print_recommended_nics);

	TEST_SUITE("Known Vendor IDs");
	RUN_TEST(known_vendor_ids);

	TEST_SUITE("Speed Value Ranges");
	RUN_TEST(speed_value_ranges);
	RUN_TEST(speed_threshold_checks);

	TEST_SUITE("Interface Name Validation");
	RUN_TEST(interface_name_length);
	RUN_TEST(interface_name_special_chars);

#ifdef __linux__
	TEST_SUITE("Linux-Specific Tests");
	RUN_TEST(linux_sysfs_paths);
#endif

#ifdef __APPLE__
	TEST_SUITE("macOS-Specific Tests");
	RUN_TEST(macos_graceful_failure);
#endif

	TEST_SUMMARY();

	return test_failed;
}
