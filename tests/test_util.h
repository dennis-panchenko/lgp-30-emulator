/* Minimal assert-based test harness. No dependency, single header.
 * Each test_*.c includes this, defines TEST(name) functions, and ends
 * main() with a sequence of RUN_TEST(name) calls followed by
 * `return test_util_report();`. Failing an assertion records the
 * failure and continues running the rest of the test body, so one bad
 * assertion doesn't hide the next one. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;
static int g_current_test_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                       \
    do {                                                                     \
        g_current_test_failed = 0;                                          \
        g_tests_run++;                                                      \
        name();                                                             \
        if (g_current_test_failed) {                                        \
            g_tests_failed++;                                               \
            fprintf(stderr, "FAIL: %s\n", #name);                           \
        } else {                                                            \
            printf("PASS: %s\n", #name);                                    \
        }                                                                    \
    } while (0)

#define ASSERT_EQ_U32(actual, expected, msg)                                 \
    do {                                                                     \
        uint32_t _a = (uint32_t)(actual);                                    \
        uint32_t _e = (uint32_t)(expected);                                  \
        if (_a != _e) {                                                      \
            fprintf(stderr, "  %s:%d: %s: expected 0x%08x, got 0x%08x\n",   \
                    __FILE__, __LINE__, (msg), _e, _a);                      \
            g_current_test_failed = 1;                                       \
        }                                                                    \
    } while (0)

#define ASSERT_EQ_INT(actual, expected, msg)                                 \
    do {                                                                     \
        long long _a = (long long)(actual);                                  \
        long long _e = (long long)(expected);                                \
        if (_a != _e) {                                                      \
            fprintf(stderr, "  %s:%d: %s: expected %lld, got %lld\n",       \
                    __FILE__, __LINE__, (msg), _e, _a);                      \
            g_current_test_failed = 1;                                       \
        }                                                                    \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                               \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, (msg));    \
            g_current_test_failed = 1;                                       \
        }                                                                    \
    } while (0)

static int test_util_report(void) {
    printf("\n%d/%d tests passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed ? 1 : 0;
}

#endif
