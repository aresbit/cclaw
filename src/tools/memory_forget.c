// memory_forget.c - Memory deletion tool for CClaw
// SPDX-License-Identifier: MIT

#include "core/tool.h"
#include "core/memory.h"
#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Memory forget tool instance data
typedef struct memory_forget_tool_t {
    memory_t* memory;          // Memory system instance
    bool memory_owned;         // Whether we own the memory instance
} memory_forget_tool_t;

// Forward declarations for vtable
static str_t memory_forget_get_name(void);
static str_t memory_forget_get_description(void);
static str_t memory_forget_get_version(void);
static err_t memory_forget_create(tool_t** out_tool);
static void memory_forget_destroy(tool_t* tool);
static err_t memory_forget_init(tool_t* tool, const tool_context_t* context);
static void memory_forget_cleanup(tool_t* tool);
static err_t memory_forget_execute(tool_t* tool, const str_t* args, tool_result_t* out_result);
static str_t memory_forget_get_parameters_schema(void);
static bool memory_forget_requires_memory(void);
static bool memory_forget_allowed_in_autonomous(autonomy_level_t level);

// VTable definition
static const tool_vtable_t memory_forget_vtable = {
    .get_name = memory_forget_get_name,
    .get_description = memory_forget_get_description,
    .get_version = memory_forget_get_version,
    .create = memory_forget_create,
    .destroy = memory_forget_destroy,
    .init = memory_forget_init,
    .cleanup = memory_forget_cleanup,
    .execute = memory_forget_execute,
    .get_parameters_schema = memory_forget_get_parameters_schema,
    .requires_memory = memory_forget_requires_memory,
    .allowed_in_autonomous = memory_forget_allowed_in_autonomous
};

// Get vtable
const tool_vtable_t* memory_forget_tool_get_vtable(void) {
    return &memory_forget_vtable;
}

static str_t memory_forget_get_name(void) {
    return STR_LIT("memory_forget");
}

static str_t memory_forget_get_description(void) {
    return STR_LIT("Delete information from memory system by key or id");
}

static str_t memory_forget_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t memory_forget_create(tool_t** out_tool) {
    if (!out_tool) return ERR_INVALID_ARGUMENT;

    tool_t* tool = tool_alloc(&memory_forget_vtable);
    if (!tool) return ERR_OUT_OF_MEMORY;

    memory_forget_tool_t* forget_data = calloc(1, sizeof(memory_forget_tool_t));
    if (!forget_data) {
        tool_free(tool);
        return ERR_OUT_OF_MEMORY;
    }

    // Default configuration
    forget_data->memory = NULL;
    forget_data->memory_owned = false;

    tool->impl_data = forget_data;

    *out_tool = tool;
    return ERR_OK;
}

static void memory_forget_destroy(tool_t* tool) {
    if (!tool || !tool->impl_data) return;

    memory_forget_tool_t* forget_data = (memory_forget_tool_t*)tool->impl_data;

    // Cleanup will free resources if initialized
    if (tool->initialized) {
        memory_forget_cleanup(tool);
    }

    // If we own the memory instance, free it
    if (forget_data->memory_owned && forget_data->memory) {
        memory_free(forget_data->memory);
    }

    free(forget_data);
    tool->impl_data = NULL;

    tool_free(tool);
}

static err_t memory_forget_init(tool_t* tool, const tool_context_t* context) {
    if (!tool || !tool->impl_data) return ERR_INVALID_ARGUMENT;
    if (tool->initialized) return ERR_OK;

    memory_forget_tool_t* forget_data = (memory_forget_tool_t*)tool->impl_data;

    // Copy context
    tool->context = *context;

    // Check if memory is provided in context
    if (context->memory) {
        // Use provided memory instance
        forget_data->memory = context->memory;
        forget_data->memory_owned = false;
    } else {
        // Create our own memory instance
        // TODO: Load memory configuration from context or default
        memory_config_t config = memory_config_default();

        err_t err = memory_create("sqlite", &config, &forget_data->memory);
        if (err != ERR_OK) {
            return err;
        }

        forget_data->memory_owned = true;

        // Initialize the memory instance
        err = forget_data->memory->vtable->init(forget_data->memory);
        if (err != ERR_OK) {
            memory_free(forget_data->memory);
            forget_data->memory = NULL;
            return err;
        }
    }

    tool->initialized = true;
    return ERR_OK;
}

static void memory_forget_cleanup(tool_t* tool) {
    if (!tool || !tool->impl_data || !tool->initialized) return;

    memory_forget_tool_t* forget_data = (memory_forget_tool_t*)tool->impl_data;

    // Cleanup memory instance if we own it
    if (forget_data->memory_owned && forget_data->memory) {
        forget_data->memory->vtable->cleanup(forget_data->memory);
    }

    tool->initialized = false;
}

// Parse JSON arguments for memory forget
static err_t parse_memory_forget_args(const char* args_json,
                                      str_t* out_key,
                                      str_t* out_id) {
    if (!args_json || (!out_key && !out_id)) {
        return ERR_INVALID_ARGUMENT;
    }

    // TODO: Use proper JSON parsing with json_config.h
    // For now, implement simple JSON parsing

    // Expected format: {"key": "...", "id": "..."}
    // Either key or id should be provided

    // Extract key (optional)
    char* key_start = strstr(args_json, "\"key\"");
    if (key_start && out_key) {
        char* key_quote = strchr(key_start + 5, '"');
        if (key_quote) {
            char* key_end = strchr(key_quote + 1, '"');
            if (key_end) {
                size_t key_len = key_end - (key_quote + 1);
                char* key = malloc(key_len + 1);
                if (!key) return ERR_OUT_OF_MEMORY;
                memcpy(key, key_quote + 1, key_len);
                key[key_len] = '\0';
                out_key->data = key;
                out_key->len = (uint32_t)key_len;
            }
        }
    }

    // Extract id (optional)
    char* id_start = strstr(args_json, "\"id\"");
    if (id_start && out_id) {
        char* id_quote = strchr(id_start + 4, '"');
        if (id_quote) {
            char* id_end = strchr(id_quote + 1, '"');
            if (id_end) {
                size_t id_len = id_end - (id_quote + 1);
                char* id = malloc(id_len + 1);
                if (!id) {
                    if (!str_empty(*out_key)) free((void*)out_key->data);
                    return ERR_OUT_OF_MEMORY;
                }
                memcpy(id, id_quote + 1, id_len);
                id[id_len] = '\0';
                out_id->data = id;
                out_id->len = (uint32_t)id_len;
            }
        }
    }

    // Validate: either key or id must be provided
    if ((!out_key || str_empty(*out_key)) && (!out_id || str_empty(*out_id))) {
        if (!str_empty(*out_key)) free((void*)out_key->data);
        if (!str_empty(*out_id)) free((void*)out_id->data);
        return ERR_INVALID_ARGUMENT;
    }

    return ERR_OK;
}

static err_t memory_forget_execute(tool_t* tool, const str_t* args, tool_result_t* out_result) {
    if (!tool || !tool->impl_data || !tool->initialized || !args || !out_result) {
        return ERR_INVALID_ARGUMENT;
    }

    memory_forget_tool_t* forget_data = (memory_forget_tool_t*)tool->impl_data;

    if (!forget_data->memory) {
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
    str_t id = STR_NULL;

    err_t parse_err = parse_memory_forget_args(args_str, &key, &id);
    free(args_str);

    if (parse_err != ERR_OK) {
        if (!str_empty(key)) free((void*)key.data);
        if (!str_empty(id)) free((void*)id.data);

        str_t error = STR_LIT("Failed to parse arguments");
        tool_result_set_error(out_result, &error);
        return parse_err;
    }

    err_t forget_err = ERR_OK;

    if (!str_empty(key)) {
        // Forget by key
        forget_err = forget_data->memory->vtable->forget(forget_data->memory, &key);
    } else if (!str_empty(id)) {
        // Forget by id
        forget_err = forget_data->memory->vtable->forget_by_id(forget_data->memory, &id);
    }

    // Cleanup parsed arguments
    if (!str_empty(key)) free((void*)key.data);
    if (!str_empty(id)) free((void*)id.data);

    if (forget_err != ERR_OK) {
        if (forget_err == ERR_NOT_FOUND) {
            str_t error = STR_LIT("Memory entry not found");
            tool_result_set_error(out_result, &error);
        } else {
            str_t error = STR_LIT("Failed to delete memory entry");
            tool_result_set_error(out_result, &error);
        }
        return forget_err;
    }

    // Success
    str_t success_msg = STR_LIT("Memory entry deleted successfully");
    tool_result_set_success(out_result, &success_msg);

    return ERR_OK;
}

static str_t memory_forget_get_parameters_schema(void) {
    // JSON schema for memory_forget tool parameters
    const char* schema = "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"key\": {"
                "\"type\": \"string\","
                "\"description\": \"Key of memory entry to delete\""
            "},"
            "\"id\": {"
                "\"type\": \"string\","
                "\"description\": \"ID of memory entry to delete\""
            "}"
        "},"
        "\"anyOf\": ["
            "{ \"required\": [\"key\"] },"
            "{ \"required\": [\"id\"] }"
        "]"
    "}";

    return (str_t){ .data = schema, .len = strlen(schema) };
}

static bool memory_forget_requires_memory(void) {
    return true;  // This tool requires access to memory system
}

static bool memory_forget_allowed_in_autonomous(autonomy_level_t level) {
    // Only allow in supervised mode, not in full autonomy (potentially dangerous)
    return level == AUTONOMY_LEVEL_SUPERVISED;
}