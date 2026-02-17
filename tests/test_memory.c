// test_memory.c - Memory system tests for CClaw
// SPDX-License-Identifier: MIT

#include "core/memory.h"
#include "core/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(expr) \
    do { \
        if (!(expr)) { \
            printf("FAIL: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define TEST_OK(err) TEST((err) == ERR_OK)

static bool test_memory_registry(void) {
    printf("Testing memory registry...\n");

    // Initialize registry
    err_t err = memory_registry_init();
    TEST_OK(err);

    // List backends
    const char** names = NULL;
    uint32_t count = 0;
    err = memory_registry_list(&names, &count);
    TEST_OK(err);

    printf("Found %u memory backends:\n", count);
    for (uint32_t i = 0; i < count; i++) {
        printf("  %s\n", names[i]);
    }

    // Should have at least sqlite, markdown, null
    TEST(count >= 3);

    memory_registry_shutdown();
    return true;
}

static bool test_memory_create(void) {
    printf("Testing memory creation...\n");

    memory_config_t config = memory_config_default();
    str_t backend = STR_LIT("sqlite");
    config.backend = backend;

    memory_t* memory = NULL;
    err_t err = memory_create("sqlite", &config, &memory);
    TEST_OK(err);
    TEST(memory != NULL);

    // Initialize
    err = memory->vtable->init(memory);
    TEST_OK(err);
    TEST(memory->initialized == true);

    // Cleanup
    memory->vtable->cleanup(memory);
    memory->vtable->destroy(memory);

    return true;
}

static bool test_memory_store_recall(void) {
    printf("Testing memory store and recall...\n");

    memory_config_t config = memory_config_default();
    str_t backend = STR_LIT("sqlite");
    config.backend = backend;

    memory_t* memory = NULL;
    err_t err = memory_create("sqlite", &config, &memory);
    TEST_OK(err);

    err = memory->vtable->init(memory);
    TEST_OK(err);

    // Create a test entry
    str_t key = STR_LIT("test_key");
    str_t content = STR_LIT("This is a test memory entry.");
    memory_entry_t* entry = memory_entry_create(&key, &content, MEMORY_CATEGORY_CORE, NULL);
    TEST(entry != NULL);

    // Store the entry
    err = memory->vtable->store(memory, entry);
    TEST_OK(err);

    // Try to recall it
    memory_entry_t recalled_entry = {0};
    err = memory->vtable->recall(memory, &key, &recalled_entry);
    TEST_OK(err);

    // Check that content matches
    TEST(str_equal(entry->content, recalled_entry.content));
    TEST(str_equal(entry->key, recalled_entry.key));
    TEST(entry->category == recalled_entry.category);

    // Clean up
    memory_entry_free(entry);
    free((void*)recalled_entry.id.data);
    free((void*)recalled_entry.key.data);
    free((void*)recalled_entry.content.data);
    free((void*)recalled_entry.timestamp.data);
    free((void*)recalled_entry.session_id.data);

    memory->vtable->cleanup(memory);
    memory->vtable->destroy(memory);

    return true;
}

static bool test_memory_search(void) {
    printf("Testing memory search...\n");

    memory_config_t config = memory_config_default();
    str_t backend = STR_LIT("sqlite");
    config.backend = backend;

    memory_t* memory = NULL;
    err_t err = memory_create("sqlite", &config, &memory);
    TEST_OK(err);

    err = memory->vtable->init(memory);
    TEST_OK(err);

    // Store a few entries
    const char* keys[] = {"apple", "banana", "cherry"};
    const char* contents[] = {
        "I like apples because they are red.",
        "Bananas are yellow and sweet.",
        "Cherries are small and delicious."
    };

    for (int i = 0; i < 3; i++) {
        str_t key = STR_VIEW(keys[i]);
        str_t content = STR_VIEW(contents[i]);
        memory_entry_t* entry = memory_entry_create(&key, &content, MEMORY_CATEGORY_CORE, NULL);
        TEST(entry != NULL);

        err = memory->vtable->store(memory, entry);
        TEST_OK(err);

        memory_entry_free(entry);
    }

    // Search for "apple"
    str_t query = STR_LIT("apple");
    memory_entry_t* results = NULL;
    uint32_t result_count = 0;

    memory_search_opts_t opts = memory_search_opts_default();
    opts.limit = 10;

    err = memory->vtable->search(memory, &query, &opts, &results, &result_count);
    TEST_OK(err);

    printf("Found %u results for 'apple'\n", result_count);
    TEST(result_count >= 1);

    if (results && result_count > 0) {
        for (uint32_t i = 0; i < result_count; i++) {
            printf("  Result %u: key='%.*s'\n", i,
                   (int)results[i].key.len, results[i].key.data);
        }
    }

    // Clean up results
    if (results) {
        memory_entry_array_free(results, result_count);
    }

    memory->vtable->cleanup(memory);
    memory->vtable->destroy(memory);

    return true;
}

static bool test_null_backend(void) {
    printf("Testing null backend...\n");

    memory_config_t config = memory_config_default();
    str_t backend = STR_LIT("null");
    config.backend = backend;

    memory_t* memory = NULL;
    err_t err = memory_create("null", &config, &memory);
    TEST_OK(err);

    err = memory->vtable->init(memory);
    TEST_OK(err);

    // Store should always succeed
    str_t key = STR_LIT("test");
    str_t content = STR_LIT("test content");
    memory_entry_t* entry = memory_entry_create(&key, &content, MEMORY_CATEGORY_CORE, NULL);
    TEST(entry != NULL);

    err = memory->vtable->store(memory, entry);
    TEST_OK(err);

    // Recall should always fail (not found)
    memory_entry_t recalled = {0};
    err = memory->vtable->recall(memory, &key, &recalled);
    TEST(err == ERR_NOT_FOUND);

    memory_entry_free(entry);
    memory->vtable->cleanup(memory);
    memory->vtable->destroy(memory);

    return true;
}

int main(void) {
    printf("CClaw Memory System Tests\n");
    printf("========================\n\n");

    int passed = 0;
    int failed = 0;

    // Run tests
    if (test_memory_registry()) {
        printf("✓ test_memory_registry passed\n\n");
        passed++;
    } else {
        printf("✗ test_memory_registry failed\n\n");
        failed++;
    }

    if (test_memory_create()) {
        printf("✓ test_memory_create passed\n\n");
        passed++;
    } else {
        printf("✗ test_memory_create failed\n\n");
        failed++;
    }

    if (test_memory_store_recall()) {
        printf("✓ test_memory_store_recall passed\n\n");
        passed++;
    } else {
        printf("✗ test_memory_store_recall failed\n\n");
        failed++;
    }

    if (test_memory_search()) {
        printf("✓ test_memory_search passed\n\n");
        passed++;
    } else {
        printf("✗ test_memory_search failed\n\n");
        failed++;
    }

    if (test_null_backend()) {
        printf("✓ test_null_backend passed\n\n");
        passed++;
    } else {
        printf("✗ test_null_backend failed\n\n");
        failed++;
    }

    printf("========================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    return failed > 0 ? 1 : 0;
}