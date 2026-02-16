// memory_store.c - Memory storage tool for CClaw
// SPDX-License-Identifier: MIT

#include "core/tool.h"
#include "core/memory.h"
#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Memory store tool instance data
typedef struct memory_store_tool_t {
    memory_t* memory;          // Memory system instance
    bool memory_owned;         // Whether we own the memory instance
} memory_store_tool_t;

// Forward declarations for vtable
static str_t memory_store_get_name(void);
static str_t memory_store_get_description(void);
static str_t memory_store_get_version(void);
static err_t memory_store_create(tool_t** out_tool);
static void memory_store_destroy(tool_t* tool);
static err_t memory_store_init(tool_t* tool, const tool_context_t* context);
static void memory_store_cleanup(tool_t* tool);
static err_t memory_store_execute(tool_t* tool, const str_t* args, tool_result_t* out_result);
static str_t memory_store_get_parameters_schema(void);
static bool memory_store_requires_memory(void);
static bool memory_store_allowed_in_autonomous(autonomy_level_t level);

// VTable definition
static const tool_vtable_t memory_store_vtable = {
    .get_name = memory_store_get_name,
    .get_description = memory_store_get_description,
    .get_version = memory_store_get_version,
    .create = memory_store_create,
    .destroy = memory_store_destroy,
    .init = memory_store_init,
    .cleanup = memory_store_cleanup,
    .execute = memory_store_execute,
    .get_parameters_schema = memory_store_get_parameters_schema,
    .requires_memory = memory_store_requires_memory,
    .allowed_in_autonomous = memory_store_allowed_in_autonomous
};

// Get vtable
const tool_vtable_t* memory_store_tool_get_vtable(void) {
    return &memory_store_vtable;
}

static str_t memory_store_get_name(void) {
    return STR_LIT("memory_store");
}

static str_t memory_store_get_description(void) {
    return STR_LIT("Store information in memory system for later recall");
}

static str_t memory_store_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t memory_store_create(tool_t** out_tool) {
    if (!out_tool) return ERR_INVALID_ARGUMENT;

    tool_t* tool = tool_alloc(&memory_store_vtable);
    if (!tool) return ERR_OUT_OF_MEMORY;

    memory_store_tool_t* store_data = calloc(1, sizeof(memory_store_tool_t));
    if (!store_data) {
        tool_free(tool);
        return ERR_OUT_OF_MEMORY;
    }

    // Default configuration
    store_data->memory = NULL;
    store_data->memory_owned = false;

    tool->impl_data = store_data;

    *out_tool = tool;
    return ERR_OK;
}

static void memory_store_destroy(tool_t* tool) {
    if (!tool || !tool->impl_data) return;

    memory_store_tool_t* store_data = (memory_store_tool_t*)tool->impl_data;

    // Cleanup will free resources if initialized
    if (tool->initialized) {
        memory_store_cleanup(tool);
    }

    // If we own the memory instance, free it
    if (store_data->memory_owned && store_data->memory) {
        memory_free(store_data->memory);
    }

    free(store_data);
    tool->impl_data = NULL;

    tool_free(tool);
}

static err_t memory_store_init(tool_t* tool, const tool_context_t* context) {
    if (!tool || !tool->impl_data) return ERR_INVALID_ARGUMENT;
    if (tool->initialized) return ERR_OK;

    memory_store_tool_t* store_data = (memory_store_tool_t*)tool->impl_data;

    // Copy context
    tool->context = *context;

    // Check if memory is provided in context
    if (context->memory) {
        // Use provided memory instance
        store_data->memory = context->memory;
        store_data->memory_owned = false;
    } else {
        // Create our own memory instance
        // TODO: Load memory configuration from context or default
        memory_config_t config = memory_config_default();

        err_t err = memory_create("sqlite", &config, &store_data->memory);
        if (err != ERR_OK) {
            return err;
        }

        store_data->memory_owned = true;

        // Initialize the memory instance
        err = store_data->memory->vtable->init(store_data->memory);
        if (err != ERR_OK) {
            memory_free(store_data->memory);
            store_data->memory = NULL;
            return err;
        }
    }

    tool->initialized = true;
    return ERR_OK;
}

static void memory_store_cleanup(tool_t* tool) {
    if (!tool || !tool->impl_data || !tool->initialized) return;

    memory_store_tool_t* store_data = (memory_store_tool_t*)tool->impl_data;

    // Cleanup memory instance if we own it
    if (store_data->memory_owned && store_data->memory) {
        store_data->memory->vtable->cleanup(store_data->memory);
    }

    tool->initialized = false;
}

// Parse JSON arguments for memory store
static err_t parse_memory_store_args(const char* args_json,
                                     str_t* out_key,
                                     str_t* out_content,
                                     memory_category_t* out_category,
                                     str_t* out_session_id) {
    if (!args_json || !out_key || !out_content) {
        return ERR_INVALID_ARGUMENT;
    }

    // TODO: Use proper JSON parsing with json_config.h
    // For now, implement simple JSON parsing

    // Expected format: {"key": "...", "content": "...", "category": "...", "session_id": "..."}

    char* key_start = strstr(args_json, "\"key\"");
    char* content_start = strstr(args_json, "\"content\"");

    if (!key_start || !content_start) {
        return ERR_INVALID_ARGUMENT;
    }

    // Extract key
    char* key_quote = strchr(key_start + 5, '"');
    if (!key_quote) return ERR_INVALID_ARGUMENT;
    char* key_end = strchr(key_quote + 1, '"');
    if (!key_end) return ERR_INVALID_ARGUMENT;

    size_t key_len = key_end - (key_quote + 1);
    char* key = malloc(key_len + 1);
    if (!key) return ERR_OUT_OF_MEMORY;
    memcpy(key, key_quote + 1, key_len);
    key[key_len] = '\0';
    out_key->data = key;
    out_key->len = (uint32_t)key_len;

    // Extract content
    char* content_quote = strchr(content_start + 9, '"');
    if (!content_quote) {
        free((void*)out_key->data);
        return ERR_INVALID_ARGUMENT;
    }
    char* content_end = strchr(content_quote + 1, '"');
    if (!content_end) {
        free((void*)out_key->data);
        return ERR_INVALID_ARGUMENT;
    }

    size_t content_len = content_end - (content_quote + 1);
    char* content = malloc(content_len + 1);
    if (!content) {
        free((void*)out_key->data);
        return ERR_OUT_OF_MEMORY;
    }
    memcpy(content, content_quote + 1, content_len);
    content[content_len] = '\0';
    out_content->data = content;
    out_content->len = (uint32_t)content_len;

    // Extract category (optional)
    char* category_start = strstr(args_json, "\"category\"");
    if (category_start) {
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
    } else {
        *out_category = MEMORY_CATEGORY_CUSTOM; // Default category
    }

    // Extract session_id (optional)
    char* session_start = strstr(args_json, "\"session_id\"");
    if (session_start) {
        char* session_quote = strchr(session_start + 12, '"');
        if (session_quote) {
            char* session_end = strchr(session_quote + 1, '"');
            if (session_end) {
                size_t session_len = session_end - (session_quote + 1);
                char* session_id = malloc(session_len + 1);
                if (session_id) {
                    memcpy(session_id, session_quote + 1, session_len);
                    session_id[session_len] = '\0';
                    out_session_id->data = session_id;
                    out_session_id->len = (uint32_t)session_len;
                }
            }
        }
    }

    return ERR_OK;
}

static err_t memory_store_execute(tool_t* tool, const str_t* args, tool_result_t* out_result) {
    if (!tool || !tool->impl_data || !tool->initialized || !args || !out_result) {
        return ERR_INVALID_ARGUMENT;
    }

    memory_store_tool_t* store_data = (memory_store_tool_t*)tool->impl_data;

    if (!store_data->memory) {
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
    str_t key = STR_NULL;
    str_t content = STR_NULL;
    memory_category_t category = MEMORY_CATEGORY_CUSTOM;
    str_t session_id = STR_NULL;

    err_t parse_err = parse_memory_store_args(args_str, &key, &content, &category, &session_id);
    free(args_str);

    if (parse_err != ERR_OK) {
        if (!str_empty(key)) free((void*)key.data);
        if (!str_empty(content)) free((void*)content.data);
        if (!str_empty(session_id)) free((void*)session_id.data);

        str_t error = STR_LIT("Failed to parse arguments");
        tool_result_set_error(out_result, &error);
        return parse_err;
    }

    // Create memory entry
    memory_entry_t* entry = memory_entry_create(&key, &content, category, &session_id);
    if (!entry) {
        free((void*)key.data);
        free((void*)content.data);
        free((void*)session_id.data);

        str_t error = STR_LIT("Failed to create memory entry");
        tool_result_set_error(out_result, &error);
        return ERR_OUT_OF_MEMORY;
    }

    // Store in memory
    err_t store_err = store_data->memory->vtable->store(store_data->memory, entry);

    // Cleanup
    memory_entry_free(entry);
    free((void*)key.data);
    free((void*)content.data);
    free((void*)session_id.data);

    if (store_err != ERR_OK) {
        str_t error = STR_LIT("Failed to store in memory");
        tool_result_set_error(out_result, &error);
        return store_err;
    }

    // Success
    str_t success_msg = STR_LIT("Memory stored successfully");
    tool_result_set_success(out_result, &success_msg);

    return ERR_OK;
}

static str_t memory_store_get_parameters_schema(void) {
    // JSON schema for memory_store tool parameters
    const char* schema = "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"key\": {"
                "\"type\": \"string\","
                "\"description\": \"Unique key for the memory entry\""
            "},"
            "\"content\": {"
                "\"type\": \"string\","
                "\"description\": \"Content to store in memory\""
            "},"
            "\"category\": {"
                "\"type\": \"string\","
                "\"description\": \"Category (core, daily, conversation, custom)\","
                "\"enum\": [\"core\", \"daily\", \"conversation\", \"custom\"]"
            "},"
            "\"session_id\": {"
                "\"type\": \"string\","
                "\"description\": \"Optional session identifier\""
            "}"
        "},"
        "\"required\": [\"key\", \"content\"]"
    "}";

    return (str_t){ .data = schema, .len = strlen(schema) };
}

static bool memory_store_requires_memory(void) {
    return true;  // This tool requires access to memory system
}

static bool memory_store_allowed_in_autonomous(autonomy_level_t level) {
    // Allow in supervised or full autonomy modes
    return level >= AUTONOMY_LEVEL_SUPERVISED;
}