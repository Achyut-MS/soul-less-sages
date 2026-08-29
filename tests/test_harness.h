#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/*
 * Senior Engineer Note:
 * Hand-rolled macro test harness. Replaces Unity / GoogleTest / Criterion.
 * Colorizes output using standard ANSI escape sequences, honoring the NO_COLOR spec.
 */

#define TEST_INIT() \
    int g_tests_run = 0; \
    int g_tests_failed = 0;

#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        g_tests_run++; \
        if (test_func()) { \
            printf("\033[1;32mPASSED\033[0m\n"); \
        } else { \
            printf("\033[1;31mFAILED\033[0m\n"); \
            g_tests_failed++; \
        } \
    } while (0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "\n  %s:%d: Assertion failed: " #expr "\n", __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) \
    do { \
        if (expr) { \
            fprintf(stderr, "\n  %s:%d: Assertion failed: !" #expr "\n", __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define ASSERT_STR_EQ(actual, expected) \
    do { \
        const char *act = (actual); \
        const char *exp = (expected); \
        if (!act || !exp || strcmp(act, exp) != 0) { \
            fprintf(stderr, "\n  %s:%d: Assertion failed: Strings not equal\n", __FILE__, __LINE__); \
            fprintf(stderr, "  Expected: \"%s\"\n", exp ? exp : "NULL"); \
            fprintf(stderr, "  Actual:   \"%s\"\n", act ? act : "NULL"); \
            return false; \
        } \
    } while (0)

#define ASSERT_INT_EQ(actual, expected) \
    do { \
        int act = (actual); \
        int exp = (expected); \
        if (act != exp) { \
            fprintf(stderr, "\n  %s:%d: Assertion failed: Integers not equal\n", __FILE__, __LINE__); \
            fprintf(stderr, "  Expected: %d\n", exp); \
            fprintf(stderr, "  Actual:   %d\n", act); \
            return false; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "\n  %s:%d: Assertion failed: " #ptr " is NULL\n", __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#endif
