// basic.c - Basic tests for CClaw
// SPDX-License-Identifier: MIT

#include "cclaw.h"
#include "core/types.h"
#include "core/error.h"

#include <stdio.h>
#include <string.h>

// Test utilities
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define TEST_RUN(name, func) \
    do { \
        printf("Running test: %s... ", (name)); \
        fflush(stdout); \
        if (func()) { \
            printf("PASS\n"); \
            passed++; \
        } else { \
            printf("FAIL\n"); \
            failed++; \
        } \
        total++; \
    } while (0)

// Test functions
static bool test_version(void) {
    uint32_t major, minor, patch;
    cclaw_get_version(&major, &minor, &patch);

    TEST_ASSERT(major == CCLAW_VERSION_MAJOR, "Major version mismatch");
    TEST_ASSERT(minor == CCLAW_VERSION_MINOR, "Minor version mismatch");
    TEST_ASSERT(patch == CCLAW_VERSION_PATCH, "Patch version mismatch");

    const char* version_str = cclaw_get_version_string();
    TEST_ASSERT(version_str != NULL, "Version string is NULL");
    TEST_ASSERT(strlen(version_str) > 0, "Version string is empty");

    return true;
}

static bool test_platform(void) {
    const char* platform = cclaw_get_platform_name();
    TEST_ASSERT(platform != NULL, "Platform name is NULL");
    TEST_ASSERT(strlen(platform) > 0, "Platform name is empty");

    // At least one platform should be true
    bool any_platform =
        cclaw_is_platform_windows() ||
        cclaw_is_platform_linux() ||
        cclaw_is_platform_macos() ||
        cclaw_is_platform_android();

    TEST_ASSERT(any_platform, "No platform detected");

    return true;
}

static bool test_string_utils(void) {
    str_t s1 = STR_LIT("hello");
    str_t s2 = STR_LIT("world");
    str_t s3 = STR_LIT("hello");
    str_t empty = STR_NULL;

    TEST_ASSERT(s1.len == 5, "String length incorrect");
    TEST_ASSERT(str_equal(s1, s3), "Equal strings not recognized");
    TEST_ASSERT(!str_equal(s1, s2), "Different strings incorrectly equal");
    TEST_ASSERT(str_empty(empty), "Empty string not recognized");
    TEST_ASSERT(!str_empty(s1), "Non-empty string incorrectly empty");

    return true;
}

static bool test_error_codes(void) {
    TEST_ASSERT(error_to_string(ERR_OK) != NULL, "Error string for OK is NULL");
    TEST_ASSERT(error_to_string(ERR_FAILED) != NULL, "Error string for FAILED is NULL");
    TEST_ASSERT(error_to_string(ERR_OUT_OF_MEMORY) != NULL, "Error string for OOM is NULL");

    // Test error formatting
    str_t formatted = error_format(ERR_INVALID_ARGUMENT, STR_LIT("test error"));
    TEST_ASSERT(!str_empty(formatted), "Formatted error is empty");

    return true;
}

static bool test_init_shutdown(void) {
    err_t err = cclaw_init();
    TEST_ASSERT(err == ERR_OK, "Initialization failed");

    cclaw_shutdown();

    return true;
}

// Main test runner
int main(void) {
    printf("CClaw Test Suite\n");
    printf("Version: %s\n", CCLAW_VERSION_STRING);
    printf("Platform: %s\n", cclaw_get_platform_name());
    printf("\n");

    int total = 0;
    int passed = 0;
    int failed = 0;

    // Run tests
    TEST_RUN("version", test_version);
    TEST_RUN("platform", test_platform);
    TEST_RUN("string_utils", test_string_utils);
    TEST_RUN("error_codes", test_error_codes);
    TEST_RUN("init_shutdown", test_init_shutdown);

    // Summary
    printf("\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", total);
    printf("  Passed: %d\n", passed);
    printf("  Failed: %d\n", failed);

    if (failed > 0) {
        printf("\nSome tests failed!\n");
        return 1;
    }

    printf("\nAll tests passed!\n");
    return 0;
}