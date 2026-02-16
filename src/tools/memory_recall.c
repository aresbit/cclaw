// memory_recall.c - Memory recall tool for CClaw
// SPDX-License-Identifier: MIT

#include "core/tool.h"
#include "core/memory.h"
#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Memory recall tool instance data
typedef struct memory_recall_tool_t {
    memory_t* memory;          // Memory system instance
    bool memory_owned;         // Whether we own the memory instance
} memory_recall_tool_t;

// Forward declarations for vtable
static str_t memory_recall_get_name(void);
static str_t memory_recall_get_description(void);
static str_t memory_recall_get_version(void);
static err_t memory_recall_create(tool_t** out_tool);
static void memory_recall_destroy(tool_t* tool);
static err_t memory_recall_init(tool_t* tool, const tool_context_t* context);
static void memory_recall_cleanup(tool_t* tool);
static err_t memory_recall_execute(tool_t* tool, const str_t* args, tool_result_t* out_result);
static str_t memory_recall_get_parameters_schema(void);
static bool memory_recall_requires_memory(void);
static bool memory_recall_allowed_in_autonomous(autonomy_level_t level);

// VTable definition
static const tool_vtable_t memory_recall_vtable = {
    .get_name = memory_recall_get_name,
    .get_description = memory_recall_get_description,
    .get_version = memory_recall_get_version,
    .create = memory_recall_create,
    .destroy = memory_recall_destroy,
    .init = memory_recall_init,
    .cleanup = memory_recall_cleanup,
    .execute = memory_recall_execute,
    .get_parameters_schema = memory_recall_get_parameters_schema,
    .requires_memory = memory_recall_requires_memory,
    .allowed_in_autonomous = memory_recall_allowed_in_autonomous
};

// Get vtable
const tool_vtable_t* memory_recall_tool_get_vtable(void) {
    return &memory_recall_vtable;
}

static str_t memory_recall_get_name(void) {
    return STR_LIT("memory_recall");
}

static str_t memory_recall_get_description(void) {
    return STR_LIT("Retrieve information from memory system by key or search query");
}

static str_t memory_recall_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t memory_recall_create(tool_t** out_tool) {
    if (!out_tool) return ERR_INVALID_ARGUMENT;

    tool_t* tool = tool_alloc(&memory_recall_vtable);
    if (!tool) return ERR_OUT_OF_MEMORY;

    memory_recall_tool_t* recall_data = calloc(1, sizeof(memory_recall_tool_t));
    if (!recall_data) {
        tool_free(tool);
        return ERR_OUT_OF_MEMORY;
    }

    // Default configuration
    recall_data->memory = NULL;
    recall_data->memory_owned = false;

    tool->impl_data = recall_data;

    *out_tool = tool;
    return ERR_OK;
}

static void memory_recall_destroy(tool_t* tool) {
    if (!tool || !tool->impl_data) return;

    memory_recall_tool_t* recall_data = (memory_recall_tool_t*)tool->impl_data;

    // Cleanup will free resources if initialized
    if (tool->initialized) {
        memory_recall_cleanup(tool);
    }

    // If we own the memory instance, free it
    if (recall_data->memory_owned && recall_data->memory) {
        memory_free(recall_data->memory);
    }

    free(recall_data);
    tool->impl_data = NULL;

    tool_free(tool);
}

static err_t memory_recall_init(tool_t* tool, const tool_context_t* context) {
    if (!tool || !tool->impl_data) return ERR_INVALID_ARGUMENT;
    if (tool->initialized) return ERR_OK;

    memory_recall_tool_t* recall_data = (memory_recall_tool_t*)tool->impl_data;

    // Copy context
    tool->context = *context;

    // Check if memory is provided in context
    if (context->memory) {
        // Use provided memory instance
        recall_data->memory = context->memory;
        recall_data->memory_owned = false;
    } else {
        // Create our own memory instance
        // TODO: Load memory configuration from context or default
        memory_config_t config = memory_config_default();

        err_t err = memory_create("sqlite", &config, &recall_data->memory);
        if (err != ERR_OK) {
            return err;
        }

        recall_data->memory_owned = true;

        // Initialize the memory instance
        err = recall_data->memory->vtable->init(recall_data->memory);
        if (err != ERR_OK) {
            memory_free(recall_data->memory);
            recall_data->memory = NULL;
            return err;
        }
    }

    tool->initialized = true;
    return ERR_OK;
}

static void memory_recall_cleanup(tool_t* tool) {
    if (!tool || !tool->impl_data || !tool->initialized) return;

    memory_recall_tool_t* recall_data = (memory_recall_tool_t*)tool->impl_data;

    // Cleanup memory instance if we own it
    if (recall_data->memory_owned && recall_data->memory) {
        recall_data->memory->vtable->cleanup(recall_data->memory);
    }

    tool->initialized = false;
}

// Parse JSON arguments for memory recall
static err_t parse_memory_recall_args(const char* args_json,
                                      str_t* out_query,
                                      str_t* out_key,
                                      uint32_t* out_limit,
                                      memory_category_t* out_category) {
    if (!args_json || (!out_query && !out_key)) {
        return ERR_INVALID_ARGUMENT;
    }

    // TODO: Use proper JSON parsing with json_config.h
    // For now, implement simple JSON parsing

    // Expected format: {"query": "...", "key": "...", "limit": 10, "category": "..."}
    // Either query or key should be provided

    // Extract query (optional)
    char* query_start = strstr(args_json, "\"query\"");
    if (query_start && out_query) {
        char* query_quote = strchr(query_start + 7, '"');
        if (query_quote) {
            char* query_end = strchr(query_quote + 1, '"');
            if (query_end) {
                size_t query_len = query_end - (query_quote + 1);
                char* query = malloc(query_len + 1);
                if (!query) return ERR_OUT_OF_MEMORY;
                memcpy(query, query_quote + 1, query_len);
                query[query_len] = '\0';
                out_query->data = query;
                out_query->len = (uint32_t)query_len;
            }
        }
    }

    // Extract key (optional)
    char* key_start = strstr(args_json, "\"key\"");
    if (key_start && out_key) {
        char* key_quote = strchr(key_start + 5, '"');
        if (key_quote) {
            char* key_end = strchr(key_quote + 1, '"');
            if (key_end) {
                size_t key_len = key_end - (key_quote + 1);
                char* key = malloc(key_len + 1);
                if (!key) {
                    if (!str_empty(*out_query)) free((void*)out_query->data);
                    return ERR_OUT_OF_MEMORY;
                }
                memcpy(key, key_quote + 1, key_len);
                key[key_len] = '\0';
                out_key->data = key;
                out_key->len = (uint32_t)key_len;
            }
        }
    }

    // Extract limit (optional)
    char* limit_start = strstr(args_json, "\"limit\"");
    if (limit_start && out_limit) {
        char* limit_val = limit_start + 7;
        while (*limit_val && (*limit_val == ' ' || *limit_val == ':')) limit_val++;
        if (*limit_val >= '0' && *limit_val <= '9') {
            *out_limit = (uint32_t)atoi(limit_val);
        } else {
            *out_limit = 10; // Default limit
        }
    } else if (out_limit) {
        *out_limit = 10; // Default limit
    }

    // Extract category (optional)
    char* category_start = strstr(args_json, "\"category\"");
    if (category_start && out_category) {
        char* category_quote = strchr(category_start + 10, '"');
        if (category_quote) {
            char* category_end = strchr(category_quote + 1, '"');
            if (category_end) {
                size_t category_len = category_end - (category_quote + 1);
                char* category_str = malloc(category_len + 1);
                if (category_str) {
                    memcpy(category_str, category_quote + 1, category_len);
                    category_str[category_len] = '\0';
                    str_t category_str_t = { .data = category_str, .len = (uint32_t)category_len };
                    *out_category = memory_parse_category(&category_str_t);
                    free(category_str);
                }
            }
        }
    }

    // Validate: either query or key must be provided
    if ((!out_query || str_empty(*out_query)) && (!out_key || str_empty(*out_key))) {
        if (!str_empty(*out_query)) free((void*)out_query->data);
        if (!str_empty(*out_key)) free((void*)out_key->data);
        return ERR_INVALID_ARGUMENT;
    }

    return ERR_OK;
}

// Format memory entries as string
static char* format_memory_entries(const memory_entry_t* entries, uint32_t count) {
    if (!entries || count == 0) {
        return strdup("No results found");
    }

    // Calculate required buffer size
    size_t total_size = 0;
    for (uint32_t i = 0; i < count; i++) {
        total_size += entries[i].key.len + entries[i].content.len + 128; // Extra for formatting
    }

    char* buffer = malloc(total_size + 1);
    if (!buffer) return NULL;

    char* ptr = buffer;
    ptr[0] = '\0';

    for (uint32_t i = 0; i < count; i++) {
        const memory_entry_t* entry = &entries[i];

        // Format: [ID] Key: content (score: X.XX)
        ptr += snprintf(ptr, total_size - (ptr - buffer),
                       "[%d] Key: %.*s\n"
                       "     Content: %.*s\n"
                       "     Category: %s, Score: %.2f\n"
                       "     Timestamp: %.*s\n\n",
                       i + 1,
                       (int)entry->key.len, entry->key.data,
                       (int)entry->content.len, entry->content.data,
                       memory_category_to_string(entry->category),
                       entry->score,
                       (int)entry->timestamp.len, entry->timestamp.data);
    }

    return buffer;
}

static err_t memory_recall_execute(tool_t* tool, const str_t* args, tool_result_t* out_result) {
    if (!tool || !tool->impl_data || !tool->initialized || !args || !out_result) {
        return ERR_INVALID_ARGUMENT;
    }

    memory_recall_tool_t* recall_data = (memory_recall_tool_t*)tool->impl_data;

    if (!recall_data->memory) {
        str_t error = STR_LIT("Memory system not initialized");
        tool_result_set_error(out_result, &error);
        return ERR_MEMORY;
    }

    // Convert args to C string for parsing
    char* args_str = malloc(args->len + 1);
    if (!args_str) return ERR_OUT_OF_MEMORY;

    memcpy(args_str, args->data, args->len);
    args_str[args->len] = '\0';

    // Parse arguments
    str_t query = STR_NULL;
    str_t key = STR_NULL;
    uint32_t limit = 10;
    memory_category_t category = MEMORY_CATEGORY_CORE; // Default to all categories

    err_t parse_err = parse_memory_recall_args(args_str, &query, &key, &limit, &category);
    free(args_str);

    if (parse_err != ERR_OK) {
        if (!str_empty(query)) free((void*)query.data);
        if (!str_empty(key)) free((void*)key.data);

        str_t error = STR_LIT("Failed to parse arguments");
        tool_result_set_error(out_result, &error);
        return parse_err;
    }

    memory_entry_t* entries = NULL;
    uint32_t entry_count = 0;
    err_t recall_err = ERR_OK;

    if (!str_empty(key)) {
        // Recall by key
        memory_entry_t entry;
        recall_err = recall_data->memory->vtable->recall(recall_data->memory, &key, &entry);

        if (recall_err == ERR_OK) {
            entries = malloc(sizeof(memory_entry_t));
            if (entries) {
                entries[0] = entry;
                entry_count = 1;
            } else {
                recall_err = ERR_OUT_OF_MEMORY;
            }
        }
    } else if (!str_empty(query)) {
        // Search by query
        memory_search_opts_t opts = memory_search_opts_default();
        opts.limit = limit;
        if (category != MEMORY_CATEGORY_CORE) { // MEMORY_CATEGORY_CORE is 0, meaning "all"
            opts.category_filter = category;
        }

        recall_err = recall_data->memory->vtable->search(recall_data->memory, &query, &opts,
                                                         &entries, &entry_count);
    }

    // Cleanup parsed arguments
    if (!str_empty(query)) free((void*)query.data);
    if (!str_empty(key)) free((void*)key.data);

    if (recall_err != ERR_OK) {
        if (entries) memory_entry_array_free(entries, entry_count);
        str_t error = STR_LIT("Failed to recall from memory");
        tool_result_set_error(out_result, &error);
        return recall_err;
    }

    // Format results
    char* formatted_results = format_memory_entries(entries, entry_count);
    memory_entry_array_free(entries, entry_count);

    if (!formatted_results) {
        str_t error = STR_LIT("Failed to format results");
        tool_result_set_error(out_result, &error);
        return ERR_OUT_OF_MEMORY;
    }

    // Set success result
    str_t results_str = { .data = formatted_results, .len = strlen(formatted_results) };
    tool_result_set_success(out_result, &results_str);
    free(formatted_results);

    return ERR_OK;
}

static str_t memory_recall_get_parameters_schema(void) {
    // JSON schema for memory_recall tool parameters
    const char* schema = "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"query\": {"
                "\"type\": \"string\","
                "\"description\": \"Search query for semantic search\""
            "},"
            "\"key\": {"
                "\"type\": \"string\","
                "\"description\": \"Exact key to retrieve\""
            "},"
            "\"limit\": {"
                "\"type\": \"integer\","
                "\"description\": \"Maximum number of results (default: 10)\","
                "\"minimum\": 1,"
                "\"maximum\": 100"
            "},"
            "\"category\": {"
                "\"type\": \"string\","
                "\"description\": \"Filter by category (core, daily, conversation, custom)\","
                "\"enum\": [\"core\", \"daily\", \"conversation\", \"custom\"]"
            "}"
        "},"
        "\"anyOf\": ["
            "{ \"required\": [\"query\"] },"
            "{ \"required\": [\"key\"] }"
        "]"
    "}";

    return (str_t){ .data = schema, .len = strlen(schema) };
}

static bool memory_recall_requires_memory(void) {
    return true;  // This tool requires access to memory system
}

static bool memory_recall_allowed_in_autonomous(autonomy_level_t level) {
    // Allow in supervised or full autonomy modes
    return level >= AUTONOMY_LEVEL_SUPERVISED;
}