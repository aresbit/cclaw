// null.c - Null memory backend for testing
// SPDX-License-Identifier: MIT

#include "core/memory.h"
#include <stdlib.h>

// Null memory instance data (empty)
typedef struct null_memory_t {
    // No data needed for null backend
    int dummy;
} null_memory_t;

// Forward declarations for vtable
static str_t null_get_name(void);
static str_t null_get_version(void);
static err_t null_create(const memory_config_t* config, memory_t** out_memory);
static void null_destroy(memory_t* memory);
static err_t null_init(memory_t* memory);
static void null_cleanup(memory_t* memory);
static err_t null_store(memory_t* memory, const memory_entry_t* entry);
static err_t null_store_multiple(memory_t* memory, const memory_entry_t* entries, uint32_t count);
static err_t null_recall(memory_t* memory, const str_t* key, memory_entry_t* out_entry);
static err_t null_recall_by_id(memory_t* memory, const str_t* id, memory_entry_t* out_entry);
static err_t null_search(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                        memory_entry_t** out_entries, uint32_t* out_count);
static err_t null_forget(memory_t* memory, const str_t* key);
static err_t null_forget_by_id(memory_t* memory, const str_t* id);
static err_t null_forget_old(memory_t* memory, uint64_t cutoff_timestamp);
static err_t null_get_stats(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts);
static err_t null_backup(memory_t* memory, const str_t* backup_path);
static err_t null_restore(memory_t* memory, const str_t* backup_path);

// VTable definition
static const memory_vtable_t null_vtable = {
    .get_name = null_get_name,
    .get_version = null_get_version,
    .create = null_create,
    .destroy = null_destroy,
    .init = null_init,
    .cleanup = null_cleanup,
    .store = null_store,
    .store_multiple = NULL, // Not implemented
    .recall = null_recall,
    .recall_by_id = null_recall_by_id,
    .search = null_search,
    .forget = null_forget,
    .forget_by_id = null_forget_by_id,
    .forget_old = null_forget_old,
    .get_stats = null_get_stats,
    .backup = null_backup,
    .restore = null_restore
};

// Get vtable
const memory_vtable_t* memory_null_get_vtable(void) {
    return &null_vtable;
}

static str_t null_get_name(void) {
    return STR_LIT("null");
}

static str_t null_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t null_create(const memory_config_t* config, memory_t** out_memory) {
    if (!config || !out_memory) return ERR_INVALID_ARGUMENT;

    memory_t* memory = memory_alloc(&null_vtable);
    if (!memory) return ERR_OUT_OF_MEMORY;

    memory->config = *config;

    null_memory_t* null_mem = calloc(1, sizeof(null_memory_t));
    if (!null_mem) {
        memory_free(memory);
        return ERR_OUT_OF_MEMORY;
    }

    memory->impl_data = null_mem;
    *out_memory = memory;
    return ERR_OK;
}

static void null_destroy(memory_t* memory) {
    if (!memory || !memory->impl_data) return;

    null_memory_t* null_mem = (null_memory_t*)memory->impl_data;

    if (memory->initialized) {
        null_cleanup(memory);
    }

    free(null_mem);
    memory->impl_data = NULL;
    free(memory);
}

static err_t null_init(memory_t* memory) {
    if (!memory || !memory->impl_data) return ERR_INVALID_ARGUMENT;
    if (memory->initialized) return ERR_OK;

    memory->initialized = true;
    return ERR_OK;
}

static void null_cleanup(memory_t* memory) {
    if (!memory || !memory->impl_data || !memory->initialized) return;

    memory->initialized = false;
}

static err_t null_store(memory_t* memory, const memory_entry_t* entry) {
    // Null backend: always succeed but don't store anything
    (void)memory;
    (void)entry;
    return ERR_OK;
}

static err_t null_recall(memory_t* memory, const str_t* key, memory_entry_t* out_entry) {
    // Null backend: always return NOT_FOUND
    (void)memory;
    (void)key;
    (void)out_entry;
    return ERR_NOT_FOUND;
}

static err_t null_recall_by_id(memory_t* memory, const str_t* id, memory_entry_t* out_entry) {
    (void)memory;
    (void)id;
    (void)out_entry;
    return ERR_NOT_FOUND;
}

static err_t null_search(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                        memory_entry_t** out_entries, uint32_t* out_count) {
    // Null backend: return empty results
    (void)memory;
    (void)query;
    (void)opts;

    *out_entries = NULL;
    *out_count = 0;
    return ERR_OK;
}

static err_t null_forget(memory_t* memory, const str_t* key) {
    // Null backend: always succeed
    (void)memory;
    (void)key;
    return ERR_OK;
}

static err_t null_forget_by_id(memory_t* memory, const str_t* id) {
    (void)memory;
    (void)id;
    return ERR_OK;
}

static err_t null_forget_old(memory_t* memory, uint64_t cutoff_timestamp) {
    (void)memory;
    (void)cutoff_timestamp;
    return ERR_OK;
}

static err_t null_get_stats(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts) {
    if (!memory || !total_entries) return ERR_INVALID_ARGUMENT;

    *total_entries = 0;
    if (by_category_counts) {
        // Zero out all categories
        for (int i = 0; i < 4; i++) {
            by_category_counts[i] = 0;
        }
    }

    return ERR_OK;
}

static err_t null_backup(memory_t* memory, const str_t* backup_path) {
    (void)memory;
    (void)backup_path;
    return ERR_OK;
}

static err_t null_restore(memory_t* memory, const str_t* backup_path) {
    (void)memory;
    (void)backup_path;
    return ERR_OK;
}