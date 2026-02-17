// base.c - Memory base implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Entry helpers
memory_entry_t* memory_entry_create(const str_t* key, const str_t* content,
                                    memory_category_t category, const str_t* session_id) {
    if (!key || !content) return NULL;

    memory_entry_t* entry = calloc(1, sizeof(memory_entry_t));
    if (!entry) return NULL;

    entry->key = str_dup(*key, NULL);  // Use default allocator
    entry->content = str_dup(*content, NULL);
    entry->category = category;

    // Generate ID if not provided
    entry->id = memory_generate_id();

    // Set current timestamp
    entry->timestamp = memory_get_current_timestamp();

    // Set session ID if provided
    if (session_id && !str_empty(*session_id)) {
        entry->session_id = str_dup(*session_id, NULL);
    }

    // Default score
    entry->score = 1.0;

    return entry;
}

void memory_entry_free(memory_entry_t* entry) {
    if (!entry) return;

    free((void*)entry->id.data);
    free((void*)entry->key.data);
    free((void*)entry->content.data);
    free((void*)entry->timestamp.data);
    free((void*)entry->session_id.data);

    free(entry);
}

void memory_entry_array_free(memory_entry_t* entries, uint32_t count) {
    if (!entries) return;

    for (uint32_t i = 0; i < count; i++) {
        free((void*)entries[i].id.data);
        free((void*)entries[i].key.data);
        free((void*)entries[i].content.data);
        free((void*)entries[i].timestamp.data);
        free((void*)entries[i].session_id.data);
    }

    free(entries);
}

// Memory registry (similar to provider registry)
typedef struct {
    const char* name;
    const memory_vtable_t* vtable;
} memory_backend_entry_t;

#define MAX_MEMORY_BACKENDS 16
static memory_backend_entry_t g_registry[MAX_MEMORY_BACKENDS];
static uint32_t g_backend_count = 0;
static bool g_registry_initialized = false;

err_t memory_registry_init(void) {
    if (g_registry_initialized) return ERR_OK;

    memset(g_registry, 0, sizeof(g_registry));
    g_backend_count = 0;
    g_registry_initialized = true;

    // Register built-in backends
    // These will be registered when their respective modules are implemented
    memory_register("sqlite", memory_sqlite_get_vtable());
    memory_register("markdown", memory_markdown_get_vtable());
    memory_register("null", memory_null_get_vtable());

    return ERR_OK;
}

void memory_registry_shutdown(void) {
    memset(g_registry, 0, sizeof(g_registry));
    g_backend_count = 0;
    g_registry_initialized = false;
}

err_t memory_register(const char* name, const memory_vtable_t* vtable) {
    if (!name || !vtable) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) memory_registry_init();
    if (g_backend_count >= MAX_MEMORY_BACKENDS) return ERR_OUT_OF_MEMORY;

    // Check for duplicates
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return ERR_INVALID_ARGUMENT; // Already registered
        }
    }

    g_registry[g_backend_count].name = name;
    g_registry[g_backend_count].vtable = vtable;
    g_backend_count++;

    return ERR_OK;
}

err_t memory_create(const char* name, const memory_config_t* config, memory_t** out_memory) {
    if (!name || !config || !out_memory) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) memory_registry_init();

    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return g_registry[i].vtable->create(config, out_memory);
        }
    }

    return ERR_NOT_FOUND;
}

err_t memory_registry_list(const char*** out_names, uint32_t* out_count) {
    if (!out_names || !out_count) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) memory_registry_init();

    static const char* names[MAX_MEMORY_BACKENDS];
    for (uint32_t i = 0; i < g_backend_count; i++) {
        names[i] = g_registry[i].name;
    }

    *out_names = names;
    *out_count = g_backend_count;

    return ERR_OK;
}

// Memory creation helpers
memory_t* memory_alloc(const memory_vtable_t* vtable) {
    memory_t* memory = calloc(1, sizeof(memory_t));
    if (memory) {
        memory->vtable = vtable;
    }
    return memory;
}

void memory_free(memory_t* memory) {
    if (!memory) return;
    if (memory->vtable && memory->vtable->destroy) {
        memory->vtable->destroy(memory);
    } else {
        free(memory);
    }
}

// String utilities
str_t memory_generate_id(void) {
    // Simple timestamp-based ID generation
    // In production, use UUID or more robust method
    static uint64_t counter = 0;
    char id[64];
    time_t now = time(NULL);
    snprintf(id, sizeof(id), "mem_%ld_%lu", (long)now, (unsigned long)counter++);
    return str_dup_cstr(id, NULL);
}

str_t memory_get_current_timestamp(void) {
    char timestamp[64];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    return str_dup_cstr(timestamp, NULL);
}

memory_category_t memory_parse_category(const str_t* category_str) {
    if (!category_str || str_empty(*category_str)) return MEMORY_CATEGORY_CORE;

    if (str_equal_cstr(*category_str, "core")) return MEMORY_CATEGORY_CORE;
    if (str_equal_cstr(*category_str, "daily")) return MEMORY_CATEGORY_DAILY;
    if (str_equal_cstr(*category_str, "conversation")) return MEMORY_CATEGORY_CONVERSATION;
    if (str_equal_cstr(*category_str, "custom")) return MEMORY_CATEGORY_CUSTOM;

    return MEMORY_CATEGORY_CORE; // Default
}

str_t memory_category_to_string(memory_category_t category) {
    switch (category) {
        case MEMORY_CATEGORY_CORE: return STR_LIT("core");
        case MEMORY_CATEGORY_DAILY: return STR_LIT("daily");
        case MEMORY_CATEGORY_CONVERSATION: return STR_LIT("conversation");
        case MEMORY_CATEGORY_CUSTOM: return STR_LIT("custom");
        default: return STR_LIT("core");
    }
}

// Search helpers
memory_search_opts_t memory_search_opts_default(void) {
    return (memory_search_opts_t){
        .limit = 10,
        .category_filter = 0, // All categories
        .min_timestamp = 0,
        .max_timestamp = 0,
        .min_score = 0.0,
        .include_metadata = false
    };
}

err_t memory_search_simple(memory_t* memory, const str_t* query, uint32_t limit,
                           memory_entry_t** out_entries, uint32_t* out_count) {
    if (!memory || !memory->vtable || !memory->vtable->search || !query || !out_entries || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }

    memory_search_opts_t opts = memory_search_opts_default();
    opts.limit = limit;

    return memory->vtable->search(memory, query, &opts, out_entries, out_count);
}

// Default configuration
memory_config_t memory_config_default(void) {
    return (memory_config_t){
        .backend = STR_LIT("sqlite"),
        .data_dir = STR_NULL,
        .max_entries = MEMORY_MAX_ENTRIES_DEFAULT,
        .compression = false,
        .retention_days = MEMORY_RETENTION_DAYS_DEFAULT
    };
}