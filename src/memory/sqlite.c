// sqlite.c - SQLite memory backend for CClaw
// SPDX-License-Identifier: MIT

#include "core/memory.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// SQLite memory instance data
typedef struct sqlite_memory_t {
    sqlite3* db;
    sqlite3_stmt* stmt_insert;
    sqlite3_stmt* stmt_select_by_key;
    sqlite3_stmt* stmt_select_by_id;
    sqlite3_stmt* stmt_search;
    sqlite3_stmt* stmt_delete_by_key;
    sqlite3_stmt* stmt_delete_by_id;
    sqlite3_stmt* stmt_delete_old;
    sqlite3_stmt* stmt_count_total;
    sqlite3_stmt* stmt_count_by_category;
    char* db_path;
    bool use_compression;
} sqlite_memory_t;

// Forward declarations for vtable
static str_t sqlite_get_name(void);
static str_t sqlite_get_version(void);
static err_t sqlite_create(const memory_config_t* config, memory_t** out_memory);
static void sqlite_destroy(memory_t* memory);
static err_t sqlite_init(memory_t* memory);
static void sqlite_cleanup(memory_t* memory);
static err_t sqlite_store(memory_t* memory, const memory_entry_t* entry);
static err_t sqlite_store_multiple(memory_t* memory, const memory_entry_t* entries, uint32_t count);
static err_t sqlite_recall(memory_t* memory, const str_t* key, memory_entry_t* out_entry);
static err_t sqlite_recall_by_id(memory_t* memory, const str_t* id, memory_entry_t* out_entry);
static err_t sqlite_search(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                          memory_entry_t** out_entries, uint32_t* out_count);
static err_t sqlite_forget(memory_t* memory, const str_t* key);
static err_t sqlite_forget_by_id(memory_t* memory, const str_t* id);
static err_t sqlite_forget_old(memory_t* memory, uint64_t cutoff_timestamp);
static err_t sqlite_get_stats(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts);
static err_t sqlite_backup(memory_t* memory, const str_t* backup_path);
static err_t sqlite_restore(memory_t* memory, const str_t* backup_path);

// VTable definition
static const memory_vtable_t sqlite_vtable = {
    .get_name = sqlite_get_name,
    .get_version = sqlite_get_version,
    .create = sqlite_create,
    .destroy = sqlite_destroy,
    .init = sqlite_init,
    .cleanup = sqlite_cleanup,
    .store = sqlite_store,
    .store_multiple = NULL, // TODO: Implement batch insert
    .recall = sqlite_recall,
    .recall_by_id = sqlite_recall_by_id,
    .search = sqlite_search,
    .forget = sqlite_forget,
    .forget_by_id = sqlite_forget_by_id,
    .forget_old = sqlite_forget_old,
    .get_stats = sqlite_get_stats,
    .backup = sqlite_backup,
    .restore = sqlite_restore
};

// Get vtable
const memory_vtable_t* memory_sqlite_get_vtable(void) {
    return &sqlite_vtable;
}

// Helper functions
static const char* get_table_schema(void) {
    return "CREATE TABLE IF NOT EXISTS memories ("
           "id TEXT PRIMARY KEY,"
           "key TEXT NOT NULL,"
           "content TEXT NOT NULL,"
           "category INTEGER NOT NULL,"
           "timestamp TEXT NOT NULL,"
           "session_id TEXT,"
           "score REAL DEFAULT 1.0,"
           "created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),"
           "updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))"
           ");"
           "CREATE INDEX IF NOT EXISTS idx_memories_key ON memories(key);"
           "CREATE INDEX IF NOT EXISTS idx_memories_category ON memories(category);"
           "CREATE INDEX IF NOT EXISTS idx_memories_created_at ON memories(created_at);"
           "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(key, content, tokenize='porter');"
           "CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
           "  INSERT INTO memories_fts(rowid, key, content) VALUES (new.rowid, new.key, new.content);"
           "END;"
           "CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
           "  DELETE FROM memories_fts WHERE rowid = old.rowid;"
           "END;";
}

static err_t prepare_statements(sqlite_memory_t* sqlite_mem) {
    const char* insert_sql = "INSERT INTO memories (id, key, content, category, timestamp, session_id, score) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?);";
    const char* select_by_key_sql = "SELECT * FROM memories WHERE key = ? ORDER BY created_at DESC LIMIT 1;";
    const char* select_by_id_sql = "SELECT * FROM memories WHERE id = ?;";
    const char* search_sql = "SELECT * FROM memories WHERE rowid IN ("
                             "  SELECT rowid FROM memories_fts WHERE key MATCH ? OR content MATCH ?"
                             ") ORDER BY created_at DESC LIMIT ?;";
    const char* delete_by_key_sql = "DELETE FROM memories WHERE key = ?;";
    const char* delete_by_id_sql = "DELETE FROM memories WHERE id = ?;";
    const char* delete_old_sql = "DELETE FROM memories WHERE created_at < ?;";
    const char* count_total_sql = "SELECT COUNT(*) FROM memories;";
    const char* count_by_category_sql = "SELECT category, COUNT(*) FROM memories GROUP BY category;";

    int rc;
    rc = sqlite3_prepare_v2(sqlite_mem->db, insert_sql, -1, &sqlite_mem->stmt_insert, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, select_by_key_sql, -1, &sqlite_mem->stmt_select_by_key, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, select_by_id_sql, -1, &sqlite_mem->stmt_select_by_id, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, search_sql, -1, &sqlite_mem->stmt_search, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, delete_by_key_sql, -1, &sqlite_mem->stmt_delete_by_key, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, delete_by_id_sql, -1, &sqlite_mem->stmt_delete_by_id, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, delete_old_sql, -1, &sqlite_mem->stmt_delete_old, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, count_total_sql, -1, &sqlite_mem->stmt_count_total, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    rc = sqlite3_prepare_v2(sqlite_mem->db, count_by_category_sql, -1, &sqlite_mem->stmt_count_by_category, NULL);
    if (rc != SQLITE_OK) return ERR_MEMORY;

    return ERR_OK;
}

static void finalize_statements(sqlite_memory_t* sqlite_mem) {
    if (sqlite_mem->stmt_insert) sqlite3_finalize(sqlite_mem->stmt_insert);
    if (sqlite_mem->stmt_select_by_key) sqlite3_finalize(sqlite_mem->stmt_select_by_key);
    if (sqlite_mem->stmt_select_by_id) sqlite3_finalize(sqlite_mem->stmt_select_by_id);
    if (sqlite_mem->stmt_search) sqlite3_finalize(sqlite_mem->stmt_search);
    if (sqlite_mem->stmt_delete_by_key) sqlite3_finalize(sqlite_mem->stmt_delete_by_key);
    if (sqlite_mem->stmt_delete_by_id) sqlite3_finalize(sqlite_mem->stmt_delete_by_id);
    if (sqlite_mem->stmt_delete_old) sqlite3_finalize(sqlite_mem->stmt_delete_old);
    if (sqlite_mem->stmt_count_total) sqlite3_finalize(sqlite_mem->stmt_count_total);
    if (sqlite_mem->stmt_count_by_category) sqlite3_finalize(sqlite_mem->stmt_count_by_category);
}

static str_t sqlite_get_name(void) {
    return STR_LIT("sqlite");
}

static str_t sqlite_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t sqlite_create(const memory_config_t* config, memory_t** out_memory) {
    if (!config || !out_memory) return ERR_INVALID_ARGUMENT;

    memory_t* memory = memory_alloc(&sqlite_vtable);
    if (!memory) return ERR_OUT_OF_MEMORY;

    memory->config = *config;

    sqlite_memory_t* sqlite_mem = calloc(1, sizeof(sqlite_memory_t));
    if (!sqlite_mem) {
        memory_free(memory);
        return ERR_OUT_OF_MEMORY;
    }

    // Determine database path
    if (!str_empty(config->data_dir)) {
        size_t path_len = config->data_dir.len + 32;
        sqlite_mem->db_path = malloc(path_len);
        if (!sqlite_mem->db_path) {
            free(sqlite_mem);
            memory_free(memory);
            return ERR_OUT_OF_MEMORY;
        }
        snprintf(sqlite_mem->db_path, path_len, "%.*s/memories.db",
                 (int)config->data_dir.len, config->data_dir.data);
    } else {
        sqlite_mem->db_path = strdup(":memory:"); // In-memory database
    }

    sqlite_mem->use_compression = config->compression;
    memory->impl_data = sqlite_mem;

    *out_memory = memory;
    return ERR_OK;
}

static void sqlite_destroy(memory_t* memory) {
    if (!memory || !memory->impl_data) return;

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    // Cleanup will close database if initialized
    if (memory->initialized) {
        sqlite_cleanup(memory);
    }

    free(sqlite_mem->db_path);
    free(sqlite_mem);
    memory->impl_data = NULL;

    free(memory);
}

static err_t sqlite_init(memory_t* memory) {
    if (!memory || !memory->impl_data) return ERR_INVALID_ARGUMENT;
    if (memory->initialized) return ERR_OK;

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    // Open database
    int rc = sqlite3_open(sqlite_mem->db_path, &sqlite_mem->db);
    if (rc != SQLITE_OK) {
        return ERR_MEMORY;
    }

    // Enable WAL mode for better concurrency
    rc = sqlite3_exec(sqlite_mem->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(sqlite_mem->db);
        return ERR_MEMORY;
    }

    // Create tables
    rc = sqlite3_exec(sqlite_mem->db, get_table_schema(), NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(sqlite_mem->db);
        return ERR_MEMORY;
    }

    // Prepare statements
    err_t err = prepare_statements(sqlite_mem);
    if (err != ERR_OK) {
        sqlite3_close(sqlite_mem->db);
        return err;
    }

    memory->initialized = true;
    return ERR_OK;
}

static void sqlite_cleanup(memory_t* memory) {
    if (!memory || !memory->impl_data || !memory->initialized) return;

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    finalize_statements(sqlite_mem);

    if (sqlite_mem->db) {
        sqlite3_close(sqlite_mem->db);
        sqlite_mem->db = NULL;
    }

    memory->initialized = false;
}

static err_t sqlite_store(memory_t* memory, const memory_entry_t* entry) {
    if (!memory || !memory->impl_data || !memory->initialized || !entry) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    // Bind parameters
    sqlite3_bind_text(sqlite_mem->stmt_insert, 1, entry->id.data, -1, SQLITE_STATIC);
    sqlite3_bind_text(sqlite_mem->stmt_insert, 2, entry->key.data, -1, SQLITE_STATIC);
    sqlite3_bind_text(sqlite_mem->stmt_insert, 3, entry->content.data, -1, SQLITE_STATIC);
    sqlite3_bind_int(sqlite_mem->stmt_insert, 4, entry->category);
    sqlite3_bind_text(sqlite_mem->stmt_insert, 5, entry->timestamp.data, -1, SQLITE_STATIC);

    if (!str_empty(entry->session_id)) {
        sqlite3_bind_text(sqlite_mem->stmt_insert, 6, entry->session_id.data, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(sqlite_mem->stmt_insert, 6);
    }

    sqlite3_bind_double(sqlite_mem->stmt_insert, 7, entry->score);

    int rc = sqlite3_step(sqlite_mem->stmt_insert);
    sqlite3_reset(sqlite_mem->stmt_insert);

    if (rc != SQLITE_DONE) {
        return ERR_MEMORY;
    }

    return ERR_OK;
}

static err_t sqlite_recall(memory_t* memory, const str_t* key, memory_entry_t* out_entry) {
    if (!memory || !memory->impl_data || !memory->initialized || !key || !out_entry) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    sqlite3_bind_text(sqlite_mem->stmt_select_by_key, 1, key->data, -1, SQLITE_STATIC);

    int rc = sqlite3_step(sqlite_mem->stmt_select_by_key);
    if (rc == SQLITE_ROW) {
        // Extract columns
        out_entry->id.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_key, 0));
        out_entry->id.len = strlen(out_entry->id.data);

        out_entry->key.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_key, 1));
        out_entry->key.len = strlen(out_entry->key.data);

        out_entry->content.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_key, 2));
        out_entry->content.len = strlen(out_entry->content.data);

        out_entry->category = sqlite3_column_int(sqlite_mem->stmt_select_by_key, 3);

        out_entry->timestamp.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_key, 4));
        out_entry->timestamp.len = strlen(out_entry->timestamp.data);

        const char* session_id = (const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_key, 5);
        if (session_id) {
            out_entry->session_id.data = strdup(session_id);
            out_entry->session_id.len = strlen(session_id);
        } else {
            out_entry->session_id = STR_NULL;
        }

        out_entry->score = sqlite3_column_double(sqlite_mem->stmt_select_by_key, 6);
    }

    sqlite3_reset(sqlite_mem->stmt_select_by_key);

    return (rc == SQLITE_ROW) ? ERR_OK : ERR_NOT_FOUND;
}

static err_t sqlite_recall_by_id(memory_t* memory, const str_t* id, memory_entry_t* out_entry) {
    if (!memory || !memory->impl_data || !memory->initialized || !id || !out_entry) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    sqlite3_bind_text(sqlite_mem->stmt_select_by_id, 1, id->data, -1, SQLITE_STATIC);

    int rc = sqlite3_step(sqlite_mem->stmt_select_by_id);
    if (rc == SQLITE_ROW) {
        // Extract columns (similar to recall)
        out_entry->id.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_id, 0));
        out_entry->id.len = strlen(out_entry->id.data);

        out_entry->key.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_id, 1));
        out_entry->key.len = strlen(out_entry->key.data);

        out_entry->content.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_id, 2));
        out_entry->content.len = strlen(out_entry->content.data);

        out_entry->category = sqlite3_column_int(sqlite_mem->stmt_select_by_id, 3);

        out_entry->timestamp.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_id, 4));
        out_entry->timestamp.len = strlen(out_entry->timestamp.data);

        const char* session_id = (const char*)sqlite3_column_text(sqlite_mem->stmt_select_by_id, 5);
        if (session_id) {
            out_entry->session_id.data = strdup(session_id);
            out_entry->session_id.len = strlen(session_id);
        } else {
            out_entry->session_id = STR_NULL;
        }

        out_entry->score = sqlite3_column_double(sqlite_mem->stmt_select_by_id, 6);
    }

    sqlite3_reset(sqlite_mem->stmt_select_by_id);

    return (rc == SQLITE_ROW) ? ERR_OK : ERR_NOT_FOUND;
}

static err_t sqlite_search(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                          memory_entry_t** out_entries, uint32_t* out_count) {
    if (!memory || !memory->impl_data || !memory->initialized || !query || !out_entries || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    // For now, simple FTS search
    // TODO: Add category filtering, timestamp range, etc.

    sqlite3_bind_text(sqlite_mem->stmt_search, 1, query->data, -1, SQLITE_STATIC);
    sqlite3_bind_text(sqlite_mem->stmt_search, 2, query->data, -1, SQLITE_STATIC);
    sqlite3_bind_int(sqlite_mem->stmt_search, 3, opts ? opts->limit : 10);

    // Collect results
    memory_entry_t* entries = NULL;
    uint32_t count = 0;
    uint32_t capacity = 0;

    while (sqlite3_step(sqlite_mem->stmt_search) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity = capacity ? capacity * 2 : 16;
            memory_entry_t* new_entries = realloc(entries, capacity * sizeof(memory_entry_t));
            if (!new_entries) {
                memory_entry_array_free(entries, count);
                sqlite3_reset(sqlite_mem->stmt_search);
                return ERR_OUT_OF_MEMORY;
            }
            entries = new_entries;
        }

        memory_entry_t* entry = &entries[count];

        entry->id.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_search, 0));
        entry->id.len = strlen(entry->id.data);

        entry->key.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_search, 1));
        entry->key.len = strlen(entry->key.data);

        entry->content.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_search, 2));
        entry->content.len = strlen(entry->content.data);

        entry->category = sqlite3_column_int(sqlite_mem->stmt_search, 3);

        entry->timestamp.data = strdup((const char*)sqlite3_column_text(sqlite_mem->stmt_search, 4));
        entry->timestamp.len = strlen(entry->timestamp.data);

        const char* session_id = (const char*)sqlite3_column_text(sqlite_mem->stmt_search, 5);
        if (session_id) {
            entry->session_id.data = strdup(session_id);
            entry->session_id.len = strlen(session_id);
        } else {
            entry->session_id = STR_NULL;
        }

        entry->score = sqlite3_column_double(sqlite_mem->stmt_search, 6);

        count++;
    }

    sqlite3_reset(sqlite_mem->stmt_search);

    *out_entries = entries;
    *out_count = count;
    return ERR_OK;
}

static err_t sqlite_forget(memory_t* memory, const str_t* key) {
    if (!memory || !memory->impl_data || !memory->initialized || !key) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    sqlite3_bind_text(sqlite_mem->stmt_delete_by_key, 1, key->data, -1, SQLITE_STATIC);

    int rc = sqlite3_step(sqlite_mem->stmt_delete_by_key);
    sqlite3_reset(sqlite_mem->stmt_delete_by_key);

    if (rc != SQLITE_DONE) {
        return ERR_MEMORY;
    }

    return ERR_OK;
}

static err_t sqlite_forget_by_id(memory_t* memory, const str_t* id) {
    if (!memory || !memory->impl_data || !memory->initialized || !id) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    sqlite3_bind_text(sqlite_mem->stmt_delete_by_id, 1, id->data, -1, SQLITE_STATIC);

    int rc = sqlite3_step(sqlite_mem->stmt_delete_by_id);
    sqlite3_reset(sqlite_mem->stmt_delete_by_id);

    if (rc != SQLITE_DONE) {
        return ERR_MEMORY;
    }

    return ERR_OK;
}

static err_t sqlite_forget_old(memory_t* memory, uint64_t cutoff_timestamp) {
    if (!memory || !memory->impl_data || !memory->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    sqlite3_bind_int64(sqlite_mem->stmt_delete_old, 1, (sqlite3_int64)cutoff_timestamp);

    int rc = sqlite3_step(sqlite_mem->stmt_delete_old);
    sqlite3_reset(sqlite_mem->stmt_delete_old);

    if (rc != SQLITE_DONE) {
        return ERR_MEMORY;
    }

    return ERR_OK;
}

static err_t sqlite_get_stats(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts) {
    if (!memory || !memory->impl_data || !memory->initialized || !total_entries) {
        return ERR_INVALID_ARGUMENT;
    }

    sqlite_memory_t* sqlite_mem = (sqlite_memory_t*)memory->impl_data;

    // Get total count
    int rc = sqlite3_step(sqlite_mem->stmt_count_total);
    if (rc == SQLITE_ROW) {
        *total_entries = sqlite3_column_int(sqlite_mem->stmt_count_total, 0);
    }
    sqlite3_reset(sqlite_mem->stmt_count_total);

    // Get counts by category if requested
    if (by_category_counts) {
        // For simplicity, just zero out the array
        // In real implementation, we would populate it
        memset(by_category_counts, 0, sizeof(uint32_t) * 4); // 4 categories
    }

    return ERR_OK;
}

static err_t sqlite_backup(memory_t* memory, const str_t* backup_path) {
    // TODO: Implement database backup
    (void)memory;
    (void)backup_path;
    return ERR_NOT_IMPLEMENTED;
}

static err_t sqlite_restore(memory_t* memory, const str_t* backup_path) {
    // TODO: Implement database restore
    (void)memory;
    (void)backup_path;
    return ERR_NOT_IMPLEMENTED;
}