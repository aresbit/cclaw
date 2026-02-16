// base.c - Tool base implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/tool.h"
#include <stdlib.h>
#include <string.h>

// Tool registry (similar to provider and memory registries)
typedef struct {
    const char* name;
    const tool_vtable_t* vtable;
} tool_backend_entry_t;

#define MAX_TOOL_BACKENDS 32
static tool_backend_entry_t g_registry[MAX_TOOL_BACKENDS];
static uint32_t g_backend_count = 0;
static bool g_registry_initialized = false;

err_t tool_registry_init(void) {
    if (g_registry_initialized) return ERR_OK;

    memset(g_registry, 0, sizeof(g_registry));
    g_backend_count = 0;
    g_registry_initialized = true;

    // Register built-in tools
    // These will be registered when their respective modules are implemented
    tool_register("shell", shell_tool_get_vtable());
    tool_register("file_read", file_read_tool_get_vtable());
    tool_register("file_write", file_write_tool_get_vtable());
    tool_register("memory_store", memory_store_tool_get_vtable());
    tool_register("memory_recall", memory_recall_tool_get_vtable());
    tool_register("memory_forget", memory_forget_tool_get_vtable());

    return ERR_OK;
}

void tool_registry_shutdown(void) {
    memset(g_registry, 0, sizeof(g_registry));
    g_backend_count = 0;
    g_registry_initialized = false;
}

err_t tool_register(const char* name, const tool_vtable_t* vtable) {
    if (!name || !vtable) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) tool_registry_init();
    if (g_backend_count >= MAX_TOOL_BACKENDS) return ERR_OUT_OF_MEMORY;

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

err_t tool_create(const char* name, tool_t** out_tool) {
    if (!name || !out_tool) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) tool_registry_init();

    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return g_registry[i].vtable->create(out_tool);
        }
    }

    return ERR_NOT_FOUND;
}

err_t tool_registry_list(const char*** out_names, uint32_t* out_count) {
    if (!out_names || !out_count) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) tool_registry_init();

    static const char* names[MAX_TOOL_BACKENDS];
    for (uint32_t i = 0; i < g_backend_count; i++) {
        names[i] = g_registry[i].name;
    }

    *out_names = names;
    *out_count = g_backend_count;

    return ERR_OK;
}

// Tool creation helpers
tool_t* tool_alloc(const tool_vtable_t* vtable) {
    tool_t* tool = calloc(1, sizeof(tool_t));
    if (tool) {
        tool->vtable = vtable;
    }
    return tool;
}

void tool_free(tool_t* tool) {
    if (!tool) return;
    if (tool->vtable && tool->vtable->destroy) {
        tool->vtable->destroy(tool);
    } else {
        free(tool);
    }
}

// Result helpers
tool_result_t tool_result_create(void) {
    return (tool_result_t){
        .content = STR_NULL,
        .success = false,
        .error_message = STR_NULL
    };
}

void tool_result_free(tool_result_t* result) {
    if (!result) return;

    free((void*)result->content.data);
    free((void*)result->error_message.data);

    result->content = STR_NULL;
    result->error_message = STR_NULL;
    result->success = false;
}

void tool_result_set_success(tool_result_t* result, const str_t* content) {
    if (!result) return;

    tool_result_free(result);

    result->success = true;
    if (content && !str_empty(*content)) {
        result->content.data = strdup(content->data);
        result->content.len = content->len;
    }
}

void tool_result_set_error(tool_result_t* result, const str_t* error_message) {
    if (!result) return;

    tool_result_free(result);

    result->success = false;
    if (error_message && !str_empty(*error_message)) {
        result->error_message.data = strdup(error_message->data);
        result->error_message.len = error_message->len;
    }
}

// Context helpers
tool_context_t tool_context_default(void) {
    return (tool_context_t){
        .user_data = NULL,
        .memory = NULL,
        .workspace_dir = STR_NULL
    };
}

err_t tool_context_set_memory(tool_context_t* context, memory_t* memory) {
    if (!context) return ERR_INVALID_ARGUMENT;

    context->memory = memory;
    return ERR_OK;
}

err_t tool_context_set_workspace(tool_context_t* context, const str_t* workspace_dir) {
    if (!context || !workspace_dir) return ERR_INVALID_ARGUMENT;

    free((void*)context->workspace_dir.data);
    context->workspace_dir = str_dup(*workspace_dir, NULL);
    return ERR_OK;
}