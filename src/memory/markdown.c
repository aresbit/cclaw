// markdown.c - Markdown file-based memory backend for CClaw
// SPDX-License-Identifier: MIT

#include "core/memory.h"
#include "core/alloc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

// Markdown memory instance data
typedef struct markdown_memory_t {
    char* base_dir;
    bool use_compression;
    bool use_categories;  // Store in separate category directories
} markdown_memory_t;

// Forward declarations for vtable
static str_t markdown_get_name(void);
static str_t markdown_get_version(void);
static err_t markdown_create(const memory_config_t* config, memory_t** out_memory);
static void markdown_destroy(memory_t* memory);
static err_t markdown_init(memory_t* memory);
static void markdown_cleanup(memory_t* memory);
static err_t markdown_store(memory_t* memory, const memory_entry_t* entry);
static err_t markdown_store_multiple(memory_t* memory, const memory_entry_t* entries, uint32_t count);
static err_t markdown_recall(memory_t* memory, const str_t* key, memory_entry_t* out_entry);
static err_t markdown_recall_by_id(memory_t* memory, const str_t* id, memory_entry_t* out_entry);
static err_t markdown_search(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                            memory_entry_t** out_entries, uint32_t* out_count);
static err_t markdown_forget(memory_t* memory, const str_t* key);
static err_t markdown_forget_by_id(memory_t* memory, const str_t* id);
static err_t markdown_forget_old(memory_t* memory, uint64_t cutoff_timestamp);
static err_t markdown_get_stats(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts);
static err_t markdown_backup(memory_t* memory, const str_t* backup_path);
static err_t markdown_restore(memory_t* memory, const str_t* backup_path);

// VTable definition
static const memory_vtable_t markdown_vtable = {
    .get_name = markdown_get_name,
    .get_version = markdown_get_version,
    .create = markdown_create,
    .destroy = markdown_destroy,
    .init = markdown_init,
    .cleanup = markdown_cleanup,
    .store = markdown_store,
    .store_multiple = NULL, // TODO: Implement batch store
    .recall = markdown_recall,
    .recall_by_id = markdown_recall_by_id,
    .search = markdown_search,
    .forget = markdown_forget,
    .forget_by_id = markdown_forget_by_id,
    .forget_old = markdown_forget_old,
    .get_stats = markdown_get_stats,
    .backup = markdown_backup,
    .restore = markdown_restore
};

// Get vtable
const memory_vtable_t* memory_markdown_get_vtable(void) {
    return &markdown_vtable;
}

// Helper functions
static bool ensure_directory_exists(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        // Create directory with 0755 permissions
        if (mkdir(path, 0755) != 0) {
            return false;
        }
    }
    return true;
}

static char* get_category_directory(markdown_memory_t* md_mem, memory_category_t category) {
    const char* category_str;
    switch (category) {
        case MEMORY_CATEGORY_CORE: category_str = "core"; break;
        case MEMORY_CATEGORY_DAILY: category_str = "daily"; break;
        case MEMORY_CATEGORY_CONVERSATION: category_str = "conversation"; break;
        case MEMORY_CATEGORY_CUSTOM: category_str = "custom"; break;
        default: category_str = "uncategorized"; break;
    }

    // Calculate path length
    size_t base_len = strlen(md_mem->base_dir);
    size_t cat_len = strlen(category_str);
    size_t total_len = base_len + 1 + cat_len + 1; // base + '/' + category + '\0'

    char* path = malloc(total_len);
    if (!path) return NULL;

    snprintf(path, total_len, "%s/%s", md_mem->base_dir, category_str);
    return path;
}

static char* get_entry_filepath(markdown_memory_t* md_mem, const memory_entry_t* entry) {
    // Create a safe filename from the key
    // Replace non-alphanumeric characters with underscores
    const char* key = entry->key.data;
    size_t key_len = entry->key.len;

    // Calculate max possible filename length
    size_t max_len = key_len * 2 + 64; // Allow for worst-case encoding

    char* safe_key = malloc(max_len);
    if (!safe_key) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < key_len && j < max_len - 1; i++) {
        char c = key[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            safe_key[j++] = c;
        } else if (c == ' ') {
            safe_key[j++] = '_';
        } else {
            // Encode as hex
            snprintf(safe_key + j, max_len - j, "_%02X_", (unsigned char)c);
            j += 4;
        }
    }
    safe_key[j] = '\0';

    // Truncate if too long
    if (strlen(safe_key) > 128) {
        safe_key[128] = '\0';
    }

    // Get category directory
    char* cat_dir = get_category_directory(md_mem, entry->category);
    if (!cat_dir) {
        free(safe_key);
        return NULL;
    }

    // Create full path: base_dir/category/safe_key.md
    size_t path_len = strlen(cat_dir) + 1 + strlen(safe_key) + 4; // + '/' + key + '.md' + '\0'
    char* filepath = malloc(path_len);
    if (filepath) {
        snprintf(filepath, path_len, "%s/%s.md", cat_dir, safe_key);
    }

    free(cat_dir);
    free(safe_key);
    return filepath;
}

static str_t markdown_get_name(void) {
    return STR_LIT("markdown");
}

static str_t markdown_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t markdown_create(const memory_config_t* config, memory_t** out_memory) {
    if (!config || !out_memory) return ERR_INVALID_ARGUMENT;

    memory_t* memory = memory_alloc(&markdown_vtable);
    if (!memory) return ERR_OUT_OF_MEMORY;

    memory->config = *config;

    markdown_memory_t* md_mem = calloc(1, sizeof(markdown_memory_t));
    if (!md_mem) {
        memory_free(memory);
        return ERR_OUT_OF_MEMORY;
    }

    // Determine base directory
    if (!str_empty(config->data_dir)) {
        md_mem->base_dir = malloc(config->data_dir.len + 1);
        if (!md_mem->base_dir) {
            free(md_mem);
            memory_free(memory);
            return ERR_OUT_OF_MEMORY;
        }
        memcpy(md_mem->base_dir, config->data_dir.data, config->data_dir.len);
        md_mem->base_dir[config->data_dir.len] = '\0';
    } else {
        // Default to current directory
        md_mem->base_dir = strdup("./memories");
        if (!md_mem->base_dir) {
            free(md_mem);
            memory_free(memory);
            return ERR_OUT_OF_MEMORY;
        }
    }

    md_mem->use_compression = config->compression;
    md_mem->use_categories = true; // Always use categories for markdown

    memory->impl_data = md_mem;

    *out_memory = memory;
    return ERR_OK;
}

static void markdown_destroy(memory_t* memory) {
    if (!memory || !memory->impl_data) return;

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;

    // Cleanup will close handles if initialized
    if (memory->initialized) {
        markdown_cleanup(memory);
    }

    free(md_mem->base_dir);
    free(md_mem);
    memory->impl_data = NULL;

    free(memory);
}

static err_t markdown_init(memory_t* memory) {
    if (!memory || !memory->impl_data) return ERR_INVALID_ARGUMENT;
    if (memory->initialized) return ERR_OK;

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;

    // Create base directory if it doesn't exist
    if (!ensure_directory_exists(md_mem->base_dir)) {
        return ERR_IO;
    }

    // Create category directories
    const char* categories[] = {"core", "daily", "conversation", "custom"};
    for (int i = 0; i < 4; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", md_mem->base_dir, categories[i]);
        if (!ensure_directory_exists(path)) {
            return ERR_IO;
        }
    }

    memory->initialized = true;
    return ERR_OK;
}

static void markdown_cleanup(memory_t* memory) {
    if (!memory || !memory->impl_data || !memory->initialized) return;

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;
    // Nothing to clean up for file-based storage

    memory->initialized = false;
}

static err_t markdown_store(memory_t* memory, const memory_entry_t* entry) {
    if (!memory || !memory->impl_data || !memory->initialized || !entry) {
        return ERR_INVALID_ARGUMENT;
    }

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;

    // Get filepath for this entry
    char* filepath = get_entry_filepath(md_mem, entry);
    if (!filepath) return ERR_OUT_OF_MEMORY;

    // Create Markdown content
    FILE* f = fopen(filepath, "w");
    if (!f) {
        free(filepath);
        return ERR_IO;
    }

    // Write metadata as YAML frontmatter
    fprintf(f, "---\n");
    fprintf(f, "id: %.*s\n", (int)entry->id.len, entry->id.data);
    fprintf(f, "key: %.*s\n", (int)entry->key.len, entry->key.data);
    fprintf(f, "category: %d\n", entry->category);
    fprintf(f, "timestamp: %.*s\n", (int)entry->timestamp.len, entry->timestamp.data);
    if (!str_empty(entry->session_id)) {
        fprintf(f, "session_id: %.*s\n", (int)entry->session_id.len, entry->session_id.data);
    }
    fprintf(f, "score: %f\n", entry->score);
    fprintf(f, "---\n\n");

    // Write content
    fprintf(f, "%.*s\n", (int)entry->content.len, entry->content.data);

    fclose(f);
    free(filepath);
    return ERR_OK;
}

static err_t markdown_recall(memory_t* memory, const str_t* key, memory_entry_t* out_entry) {
    if (!memory || !memory->impl_data || !memory->initialized || !key || !out_entry) {
        return ERR_INVALID_ARGUMENT;
    }

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;

    // We need to search through all category directories for a file matching the key
    // This is inefficient for markdown backend - better to use search instead
    // For now, just return NOT_FOUND
    (void)md_mem;

    return ERR_NOT_FOUND;
}

static err_t markdown_recall_by_id(memory_t* memory, const str_t* id, memory_entry_t* out_entry) {
    // Markdown backend doesn't efficiently support lookup by ID
    // Would need to scan all files
    (void)memory;
    (void)id;
    (void)out_entry;
    return ERR_NOT_IMPLEMENTED;
}

static bool file_matches_query(const char* filepath, const char* query) {
    FILE* f = fopen(filepath, "r");
    if (!f) return false;

    // Simple check: read file and search for query substring
    char buffer[4096];
    bool found = false;
    size_t query_len = strlen(query);

    while (fgets(buffer, sizeof(buffer), f) && !found) {
        if (strstr(buffer, query) != NULL) {
            found = true;
        }
    }

    fclose(f);
    return found;
}

static err_t scan_directory(const char* dirpath, const char* query,
                           memory_entry_t** out_entries, uint32_t* out_count,
                           uint32_t limit) {
    DIR* dir = opendir(dirpath);
    if (!dir) return ERR_IO;

    struct dirent* entry;
    uint32_t count = 0;
    uint32_t capacity = 0;
    memory_entry_t* entries = NULL;

    while ((entry = readdir(dir)) != NULL && count < limit) {
        if (entry->d_type != DT_REG) continue;

        // Check if it's a .md file
        size_t name_len = strlen(entry->d_name);
        if (name_len < 4 || strcmp(entry->d_name + name_len - 3, ".md") != 0) {
            continue;
        }

        // Build full path
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        if (!file_matches_query(filepath, query)) {
            continue;
        }

        // Read file and parse entry
        FILE* f = fopen(filepath, "r");
        if (!f) continue;

        // Resize entries array if needed
        if (count >= capacity) {
            capacity = capacity ? capacity * 2 : 16;
            memory_entry_t* new_entries = realloc(entries, capacity * sizeof(memory_entry_t));
            if (!new_entries) {
                closedir(dir);
                memory_entry_array_free(entries, count);
                return ERR_OUT_OF_MEMORY;
            }
            entries = new_entries;
        }

        // Simple parsing - in reality would need proper YAML frontmatter parsing
        // For now, just create a minimal entry
        memory_entry_t* mem_entry = &entries[count];

        // Extract key from filename (remove .md extension)
        char* key = strndup(entry->d_name, name_len - 3);
        if (key) {
            // TODO: decode safe_key back to original key
            mem_entry->key.data = key;
            mem_entry->key.len = strlen(key);
        } else {
            mem_entry->key = STR_NULL;
        }

        // TODO: Parse actual content from file
        mem_entry->content = STR_LIT("[Content would be parsed from file]");
        mem_entry->id = STR_LIT("markdown-entry");
        mem_entry->category = MEMORY_CATEGORY_CUSTOM; // Would parse from frontmatter
        mem_entry->timestamp = STR_LIT("2025-01-01 00:00:00");
        mem_entry->session_id = STR_NULL;
        mem_entry->score = 1.0;

        fclose(f);
        count++;
    }

    closedir(dir);

    *out_entries = entries;
    *out_count = count;
    return ERR_OK;
}

static err_t markdown_search(memory_t* memory, const str_t* query, const memory_search_opts_t* opts,
                            memory_entry_t** out_entries, uint32_t* out_count) {
    if (!memory || !memory->impl_data || !memory->initialized || !query || !out_entries || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;

    // Convert query to C string for searching
    char* query_cstr = malloc(query->len + 1);
    if (!query_cstr) return ERR_OUT_OF_MEMORY;

    memcpy(query_cstr, query->data, query->len);
    query_cstr[query->len] = '\0';

    uint32_t limit = opts ? opts->limit : 10;

    // Search through all category directories
    const char* categories[] = {"core", "daily", "conversation", "custom"};
    err_t result = ERR_NOT_FOUND;
    memory_entry_t* all_entries = NULL;
    uint32_t total_count = 0;

    for (int i = 0; i < 4; i++) {
        char dirpath[512];
        snprintf(dirpath, sizeof(dirpath), "%s/%s", md_mem->base_dir, categories[i]);

        memory_entry_t* category_entries = NULL;
        uint32_t category_count = 0;

        err_t scan_result = scan_directory(dirpath, query_cstr, &category_entries, &category_count,
                                          limit - total_count);

        if (scan_result == ERR_OK && category_count > 0) {
            // Merge results
            memory_entry_t* new_all = realloc(all_entries, (total_count + category_count) * sizeof(memory_entry_t));
            if (!new_all) {
                memory_entry_array_free(category_entries, category_count);
                free(query_cstr);
                return ERR_OUT_OF_MEMORY;
            }

            all_entries = new_all;
            memcpy(all_entries + total_count, category_entries, category_count * sizeof(memory_entry_t));
            total_count += category_count;
            free(category_entries);

            result = ERR_OK;

            if (total_count >= limit) {
                break;
            }
        } else if (category_entries) {
            free(category_entries);
        }
    }

    free(query_cstr);

    if (result == ERR_OK) {
        *out_entries = all_entries;
        *out_count = total_count;
    } else {
        *out_entries = NULL;
        *out_count = 0;
    }

    return result;
}

static err_t markdown_forget(memory_t* memory, const str_t* key) {
    // Markdown backend doesn't efficiently support deletion by key
    (void)memory;
    (void)key;
    return ERR_NOT_IMPLEMENTED;
}

static err_t markdown_forget_by_id(memory_t* memory, const str_t* id) {
    (void)memory;
    (void)id;
    return ERR_NOT_IMPLEMENTED;
}

static err_t markdown_forget_old(memory_t* memory, uint64_t cutoff_timestamp) {
    (void)memory;
    (void)cutoff_timestamp;
    return ERR_NOT_IMPLEMENTED;
}

static err_t markdown_get_stats(memory_t* memory, uint32_t* total_entries, uint32_t* by_category_counts) {
    if (!memory || !memory->impl_data || !memory->initialized || !total_entries) {
        return ERR_INVALID_ARGUMENT;
    }

    markdown_memory_t* md_mem = (markdown_memory_t*)memory->impl_data;

    // Count files in each category directory
    uint32_t total = 0;
    const char* categories[] = {"core", "daily", "conversation", "custom"};

    for (int i = 0; i < 4; i++) {
        char dirpath[512];
        snprintf(dirpath, sizeof(dirpath), "%s/%s", md_mem->base_dir, categories[i]);

        DIR* dir = opendir(dirpath);
        if (dir) {
            uint32_t count = 0;
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {
                    size_t name_len = strlen(entry->d_name);
                    if (name_len >= 4 && strcmp(entry->d_name + name_len - 3, ".md") == 0) {
                        count++;
                    }
                }
            }
            closedir(dir);

            total += count;
            if (by_category_counts) {
                by_category_counts[i] = count;
            }
        }
    }

    *total_entries = total;
    return ERR_OK;
}

static err_t markdown_backup(memory_t* memory, const str_t* backup_path) {
    // For markdown backend, backup is just copying files
    (void)memory;
    (void)backup_path;
    return ERR_NOT_IMPLEMENTED;
}

static err_t markdown_restore(memory_t* memory, const str_t* backup_path) {
    (void)memory;
    (void)backup_path;
    return ERR_NOT_IMPLEMENTED;
}