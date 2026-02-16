// memory.h - Memory system interface for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_MEMORY_H
#define CCLAW_CORE_MEMORY_H

#include "core/types.h"
#include "core/error.h"
#include "core/alloc.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct memory_t memory_t;
typedef struct memory_vtable_t memory_vtable_t;

// Memory configuration
typedef struct memory_config_t {
    str_t backend;          // "sqlite", "markdown", "null"
    str_t data_dir;         // Directory for memory storage
    uint32_t max_entries;   // Maximum number of entries to store
    bool compression;       // Enable compression
    uint32_t retention_days; // Days to keep entries
} memory_config_t;

// Memory search options
typedef struct memory_search_opts_t {
    uint32_t limit;         // Maximum results to return
    memory_category_t category_filter; // Filter by category (0 = all)
    uint64_t min_timestamp; // Minimum timestamp
    uint64_t max_timestamp; // Maximum timestamp
    double min_score;       // Minimum relevance score
    bool include_metadata;  // Include metadata in results
} memory_search_opts_t;

// Memory VTable - defines the interface
struct memory_vtable_t {
    // Memory backend identification
    str_t (*get_name)(void);
    str_t (*get_version)(void);

    // Lifecycle
    err_t (*create)(const memory_config_t* config, memory_t** out_memory);
    void (*destroy)(memory_t* memory);

    // Initialization and cleanup
    err_t (*init)(memory_t* memory);
    void (*cleanup)(memory_t* memory);

    // Core operations
    err_t (*store)(memory_t* memory, const memory_entry_t* entry);
    err_t (*store_multiple)(memory_t* memory, const memory_entry_t* entries, uint32_t count);

    // Retrieval
    err_t (*recall)(memory_t* memory, const str_t* key, memory_entry_t* out_entry);
    err_t (*recall_by_id)(memory_t* memory, const str_t* id, memory_entry_t* out_entry);

    // Search
    err_t (*search)(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                    memory_entry_t** out_entries, uint32_t* out_count);

    // Deletion
    err_t (*forget)(memory_t* memory, const str_t* key);
    err_t (*forget_by_id)(memory_t* memory, const str_t* id);
    err_t (*forget_old)(memory_t* memory, uint64_t cutoff_timestamp);

    // Statistics
    err_t (*get_stats)(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts);

    // Backup and restore
    err_t (*backup)(memory_t* memory, const str_t* backup_path);
    err_t (*restore)(memory_t* memory, const str_t* backup_path);
};

// Memory instance structure
struct memory_t {
    const memory_vtable_t* vtable;
    memory_config_t config;
    void* impl_data;           // Backend-specific data
    bool initialized;
};

// Helper macros for memory backend implementation
#define MEMORY_IMPLEMENT(name, vtable_ptr) \
    const memory_vtable_t* name##_get_vtable(void) { return vtable_ptr; }

// Global memory registry (similar to provider registry)
err_t memory_registry_init(void);
void memory_registry_shutdown(void);
err_t memory_register(const char* name, const memory_vtable_t* vtable);
err_t memory_create(const char* name, const memory_config_t* config, memory_t** out_memory);
err_t memory_registry_list(const char*** out_names, uint32_t* out_count);

// Built-in memory backends
const memory_vtable_t* memory_sqlite_get_vtable(void);
const memory_vtable_t* memory_markdown_get_vtable(void);
const memory_vtable_t* memory_null_get_vtable(void);  // Null backend for testing

// Memory creation helpers
memory_t* memory_alloc(const memory_vtable_t* vtable);
void memory_free(memory_t* memory);

// Entry helpers
memory_entry_t* memory_entry_create(const str_t* key, const str_t* content,
                                    memory_category_t category, const str_t* session_id);
void memory_entry_free(memory_entry_t* entry);
void memory_entry_array_free(memory_entry_t* entries, uint32_t count);

// String utilities for memory operations
str_t memory_generate_id(void);
str_t memory_get_current_timestamp(void);
memory_category_t memory_parse_category(const str_t* category_str);
str_t memory_category_to_string(memory_category_t category);

// Search helpers
memory_search_opts_t memory_search_opts_default(void);
err_t memory_search_simple(memory_t* memory, const str_t* query, uint32_t limit,
                           memory_entry_t** out_entries, uint32_t* out_count);

// Default configuration
memory_config_t memory_config_default(void);

// Default retention period (30 days)
#define MEMORY_RETENTION_DAYS_DEFAULT 30
#define MEMORY_MAX_ENTRIES_DEFAULT 10000

#endif // CCLAW_CORE_MEMORY_H