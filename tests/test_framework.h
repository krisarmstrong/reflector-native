/*
 * test_framework.h - Minimal Unit Test Framework for Reflector-Native
 *
 * A lightweight, header-only test framework with no external dependencies.
 * Provides colored output, various assertion types, and test organization.
 *
 * Usage:
 *   #include "test_framework.h"
 *
 *   TEST(my_test_name) {
 *       ASSERT_EQ(1, 1);
 *       ASSERT_TRUE(some_condition);
 *   }
 *
 *   int main(void) {
 *       TEST_SUITE("My Tests");
 *       RUN_TEST(my_test_name);
 *       TEST_SUMMARY();
 *       return test_failures;
 *   }
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Terminal Colors
 * ============================================================================ */

#define TERM_RED "\033[0;31m"
#define TERM_GREEN "\033[0;32m"
#define TERM_YELLOW "\033[0;33m"
#define TERM_BLUE "\033[0;34m"
#define TERM_CYAN "\033[0;36m"
#define TERM_BOLD "\033[1m"
#define TERM_RESET "\033[0m"

/* ============================================================================
 * Test Counters (global)
 * ============================================================================ */

static int test_passed = 0;
static int test_failed = 0;
static int test_total = 0;
static const char *current_suite = "Default";

/* ============================================================================
 * Test Macros
 * ============================================================================ */

/* Define a test function */
#define TEST(name) static void test_##name(void)

/* Run a test and track results */
#define RUN_TEST(name)                                                                             \
	do {                                                                                           \
		int _prev_failed = test_failed;                                                            \
		printf("  " TERM_CYAN "TEST" TERM_RESET " %s ... ", #name);                                \
		fflush(stdout);                                                                            \
		test_##name();                                                                             \
		test_total++;                                                                              \
		if (test_failed == _prev_failed) {                                                         \
			printf(TERM_GREEN "PASS" TERM_RESET "\n");                                             \
			test_passed++;                                                                         \
		}                                                                                          \
	} while (0)

/* Define a test suite (for organization) */
#define TEST_SUITE(name)                                                                           \
	do {                                                                                           \
		current_suite = name;                                                                      \
		printf("\n" TERM_BOLD TERM_BLUE "=== %s ===" TERM_RESET "\n", name);                       \
	} while (0)

/* Print final test summary */
#define TEST_SUMMARY()                                                                             \
	do {                                                                                           \
		printf("\n" TERM_BOLD "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" TERM_RESET "\n");         \
		printf(TERM_BOLD "Test Summary:" TERM_RESET "\n");                                         \
		printf("  Total:  %d\n", test_total);                                                      \
		printf("  " TERM_GREEN "Passed: %d" TERM_RESET "\n", test_passed);                         \
		if (test_failed > 0) {                                                                     \
			printf("  " TERM_RED "Failed: %d" TERM_RESET "\n", test_failed);                       \
		} else {                                                                                   \
			printf("  Failed: %d\n", test_failed);                                                 \
		}                                                                                          \
		printf(TERM_BOLD "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" TERM_RESET "\n");              \
		if (test_failed == 0) {                                                                    \
			printf(TERM_GREEN TERM_BOLD "All tests passed!" TERM_RESET "\n");                      \
		} else {                                                                                   \
			printf(TERM_RED TERM_BOLD "Some tests failed!" TERM_RESET "\n");                       \
		}                                                                                          \
	} while (0)

/* ============================================================================
 * Assertion Macros
 * ============================================================================ */

/* Basic assertion */
#define ASSERT(cond)                                                                               \
	do {                                                                                           \
		if (!(cond)) {                                                                             \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Assertion failed:" TERM_RESET " %s\n", #cond);                 \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert true */
#define ASSERT_TRUE(cond) ASSERT(cond)

/* Assert false */
#define ASSERT_FALSE(cond) ASSERT(!(cond))

/* Assert equal (integers) */
#define ASSERT_EQ(expected, actual)                                                                \
	do {                                                                                           \
		long long _exp = (long long)(expected);                                                    \
		long long _act = (long long)(actual);                                                      \
		if (_exp != _act) {                                                                        \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %lld\n", _exp);                        \
			printf("    " TERM_RED "Actual:" TERM_RESET "   %lld\n", _act);                        \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert not equal */
#define ASSERT_NE(expected, actual)                                                                \
	do {                                                                                           \
		long long _exp = (long long)(expected);                                                    \
		long long _act = (long long)(actual);                                                      \
		if (_exp == _act) {                                                                        \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Values should differ but both are:" TERM_RESET " %lld\n",      \
			       _exp);                                                                          \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert greater than */
#define ASSERT_GT(a, b)                                                                            \
	do {                                                                                           \
		long long _a = (long long)(a);                                                             \
		long long _b = (long long)(b);                                                             \
		if (!(_a > _b)) {                                                                          \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %lld > %lld\n", _a, _b);               \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert greater than or equal */
#define ASSERT_GE(a, b)                                                                            \
	do {                                                                                           \
		long long _a = (long long)(a);                                                             \
		long long _b = (long long)(b);                                                             \
		if (!(_a >= _b)) {                                                                         \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %lld >= %lld\n", _a, _b);              \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert less than */
#define ASSERT_LT(a, b)                                                                            \
	do {                                                                                           \
		long long _a = (long long)(a);                                                             \
		long long _b = (long long)(b);                                                             \
		if (!(_a < _b)) {                                                                          \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %lld < %lld\n", _a, _b);               \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert less than or equal */
#define ASSERT_LE(a, b)                                                                            \
	do {                                                                                           \
		long long _a = (long long)(a);                                                             \
		long long _b = (long long)(b);                                                             \
		if (!(_a <= _b)) {                                                                         \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %lld <= %lld\n", _a, _b);              \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert NULL */
#define ASSERT_NULL(ptr)                                                                           \
	do {                                                                                           \
		if ((ptr) != NULL) {                                                                       \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected NULL, got:" TERM_RESET " %p\n", (void *)(ptr));       \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert not NULL */
#define ASSERT_NOT_NULL(ptr)                                                                       \
	do {                                                                                           \
		if ((ptr) == NULL) {                                                                       \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected non-NULL pointer" TERM_RESET "\n");                   \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert floating point equal (with epsilon) */
#define ASSERT_FLOAT_EQ(expected, actual, epsilon)                                                 \
	do {                                                                                           \
		double _exp = (double)(expected);                                                          \
		double _act = (double)(actual);                                                            \
		double _eps = (double)(epsilon);                                                           \
		if (fabs(_exp - _act) > _eps) {                                                            \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %f (±%f)\n", _exp, _eps);              \
			printf("    " TERM_RED "Actual:" TERM_RESET "   %f\n", _act);                          \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert floating point greater than */
#define ASSERT_FLOAT_GT(a, b)                                                                      \
	do {                                                                                           \
		double _a = (double)(a);                                                                   \
		double _b = (double)(b);                                                                   \
		if (!(_a > _b)) {                                                                          \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " %f > %f\n", _a, _b);                   \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert string equal */
#define ASSERT_STR_EQ(expected, actual)                                                            \
	do {                                                                                           \
		const char *_exp = (expected);                                                             \
		const char *_act = (actual);                                                               \
		if (_exp == NULL || _act == NULL || strcmp(_exp, _act) != 0) {                             \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Expected:" TERM_RESET " \"%s\"\n", _exp ? _exp : "(null)");    \
			printf("    " TERM_RED "Actual:" TERM_RESET "   \"%s\"\n", _act ? _act : "(null)");    \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert string not equal */
#define ASSERT_STR_NE(expected, actual)                                                            \
	do {                                                                                           \
		const char *_exp = (expected);                                                             \
		const char *_act = (actual);                                                               \
		if (_exp != NULL && _act != NULL && strcmp(_exp, _act) == 0) {                             \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Strings should differ but both are:" TERM_RESET " \"%s\"\n",   \
			       _exp);                                                                          \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert memory equal */
#define ASSERT_MEM_EQ(expected, actual, size)                                                      \
	do {                                                                                           \
		if (memcmp((expected), (actual), (size)) != 0) {                                           \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Memory regions differ" TERM_RESET " (size=%zu)\n",             \
			       (size_t)(size));                                                                \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert memory not equal */
#define ASSERT_MEM_NE(expected, actual, size)                                                      \
	do {                                                                                           \
		if (memcmp((expected), (actual), (size)) == 0) {                                           \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Memory regions should differ" TERM_RESET " (size=%zu)\n",      \
			       (size_t)(size));                                                                \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Assert value in range [min, max] */
#define ASSERT_IN_RANGE(value, min, max)                                                           \
	do {                                                                                           \
		long long _val = (long long)(value);                                                       \
		long long _min = (long long)(min);                                                         \
		long long _max = (long long)(max);                                                         \
		if (_val < _min || _val > _max) {                                                          \
			printf(TERM_RED "FAIL" TERM_RESET "\n");                                               \
			printf("    " TERM_RED "Value:" TERM_RESET " %lld not in range [%lld, %lld]\n", _val,  \
			       _min, _max);                                                                    \
			printf("    at %s:%d\n", __FILE__, __LINE__);                                          \
			test_failed++;                                                                         \
			return;                                                                                \
		}                                                                                          \
	} while (0)

/* Skip a test with a message */
#define SKIP_TEST(reason)                                                                          \
	do {                                                                                           \
		printf(TERM_YELLOW "SKIP" TERM_RESET " (%s)\n", reason);                                   \
		return;                                                                                    \
	} while (0)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Print a hex dump for debugging */
static inline void test_hexdump(const char *label, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	printf("    %s (%zu bytes): ", label, len);
	for (size_t i = 0; i < len && i < 32; i++) {
		printf("%02x ", p[i]);
	}
	if (len > 32) {
		printf("...");
	}
	printf("\n");
}

/* Print MAC address for debugging */
static inline void test_print_mac(const char *label, const uint8_t *mac)
{
	printf("    %s: %02x:%02x:%02x:%02x:%02x:%02x\n", label, mac[0], mac[1], mac[2], mac[3], mac[4],
	       mac[5]);
}

#endif /* TEST_FRAMEWORK_H */
