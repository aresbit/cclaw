// file_read.c - File reading tool for CClaw
// SPDX-License-Identifier: MIT

#include "core/tool.h"
#include "core/config.h"
#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

// File read tool instance data
typedef struct file_read_tool_t {
    config_t* config;           // Configuration for path restrictions
    str_t workspace_dir;        // Working directory restriction
    size_t max_file_size;       // Maximum file size to read (bytes)
} file_read_tool_t;

// Forward declarations for vtable
static str_t file_read_get_name(void);
static str_t file_read_get_description(void);
static str_t file_read_get_version(void);
static err_t file_read_create(tool_t** out_tool);
static void file_read_destroy(tool_t* tool);
static err_t file_read_init(tool_t* tool, const tool_context_t* context);
static void file_read_cleanup(tool_t* tool);
static err_t file_read_execute(tool_t* tool, const str_t* args, tool_result_t* out_result);
static str_t file_read_get_parameters_schema(void);
static bool file_read_requires_memory(void);
static bool file_read_allowed_in_autonomous(autonomy_level_t level);

// VTable definition
static const tool_vtable_t file_read_vtable = {
    .get_name = file_read_get_name,
    .get_description = file_read_get_description,
    .get_version = file_read_get_version,
    .create = file_read_create,
    .destroy = file_read_destroy,
    .init = file_read_init,
    .cleanup = file_read_cleanup,
    .execute = file_read_execute,
    .get_parameters_schema = file_read_get_parameters_schema,
    .requires_memory = file_read_requires_memory,
    .allowed_in_autonomous = file_read_allowed_in_autonomous
};

// Get vtable
const tool_vtable_t* file_read_tool_get_vtable(void) {
    return &file_read_vtable;
}

static str_t file_read_get_name(void) {
    return STR_LIT("file_read");
}

static str_t file_read_get_description(void) {
    return STR_LIT("Read file contents with safety restrictions");
}

static str_t file_read_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t file_read_create(tool_t** out_tool) {
    if (!out_tool) return ERR_INVALID_ARGUMENT;

    tool_t* tool = tool_alloc(&file_read_vtable);
    if (!tool) return ERR_OUT_OF_MEMORY;

    file_read_tool_t* file_read_data = calloc(1, sizeof(file_read_tool_t));
    if (!file_read_data) {
        tool_free(tool);
        return ERR_OUT_OF_MEMORY;
    }

    // Default configuration
    file_read_data->config = NULL;
    file_read_data->workspace_dir = STR_NULL;
    file_read_data->max_file_size = 10 * 1024 * 1024; // 10 MB default limit

    tool->impl_data = file_read_data;

    *out_tool = tool;
    return ERR_OK;
}

static void file_read_destroy(tool_t* tool) {
    if (!tool || !tool->impl_data) return;

    file_read_tool_t* file_read_data = (file_read_tool_t*)tool->impl_data;

    // Cleanup will free resources if initialized
    if (tool->initialized) {
        file_read_cleanup(tool);
    }

    // Free workspace directory string
    free((void*)file_read_data->workspace_dir.data);

    free(file_read_data);
    tool->impl_data = NULL;

    tool_free(tool);
}

static err_t file_read_init(tool_t* tool, const tool_context_t* context) {
    if (!tool || !tool->impl_data) return ERR_INVALID_ARGUMENT;
    if (tool->initialized) return ERR_OK;

    file_read_tool_t* file_read_data = (file_read_tool_t*)tool->impl_data;

    // Copy context
    tool->context = *context;

    // Set workspace from context if provided
    if (!str_empty(context->workspace_dir)) {
        file_read_data->workspace_dir = str_dup(context->workspace_dir, NULL);
        if (str_empty(file_read_data->workspace_dir)) {
            return ERR_OUT_OF_MEMORY;
        }
    }

    // TODO: Load configuration for max file size and other restrictions

    tool->initialized = true;
    return ERR_OK;
}

static void file_read_cleanup(tool_t* tool) {
    if (!tool || !tool->impl_data || !tool->initialized) return;

    // Nothing specific to cleanup for file read tool
    tool->initialized = false;
}

// Check if path is safe to access (within workspace)
static bool is_path_safe(file_read_tool_t* tool_data, const char* path) {
    if (str_empty(tool_data->workspace_dir)) {
        // No workspace restriction
        return true;
    }

    char resolved_path[PATH_MAX];
    char resolved_workspace[PATH_MAX];

    // Resolve paths to absolute
    if (realpath(path, resolved_path) == NULL) {
        // Cannot resolve path
        return false;
    }

    if (realpath(tool_data->workspace_dir.data, resolved_workspace) == NULL) {
        // Cannot resolve workspace
        return false;
    }

    // Check if resolved_path starts with resolved_workspace
    size_t workspace_len = strlen(resolved_workspace);
    if (strncmp(resolved_path, resolved_workspace, workspace_len) != 0) {
        return false;
    }

    // Also ensure it's not a directory traversal attack (e.g., workspace/../etc/passwd)
    // realpath should have normalized this, but double-check
    return true;
}

// Read file contents with safety checks
static err_t read_file_contents(const char* path, size_t max_size, tool_result_t* out_result) {
    FILE* file = NULL;
    char* buffer = NULL;
    struct stat st;

    // Get file info
    if (stat(path, &st) != 0) {
        str_t error = STR_LIT("Failed to stat file");
        tool_result_set_error(out_result, &error);
        return ERR_FILE_NOT_FOUND;
    }

    // Check if it's a regular file
    if (!S_ISREG(st.st_mode)) {
        str_t error = STR_LIT("Not a regular file");
        tool_result_set_error(out_result, &error);
        return ERR_INVALID_ARGUMENT;
    }

    // Check file size
    if (st.st_size > (off_t)max_size) {
        str_t error = STR_LIT("File too large");
        tool_result_set_error(out_result, &error);
        return ERR_FILE_TOO_LARGE;
    }

    // Open file
    file = fopen(path, "rb");
    if (!file) {
        str_t error = STR_LIT("Failed to open file");
        tool_result_set_error(out_result, &error);
        return ERR_IO;
    }

    // Allocate buffer
    buffer = malloc((size_t)st.st_size + 1);
    if (!buffer) {
        fclose(file);
        return ERR_OUT_OF_MEMORY;
    }

    // Read file
    size_t bytes_read = fread(buffer, 1, (size_t)st.st_size, file);
    fclose(file);

    if (bytes_read != (size_t)st.st_size) {
        free(buffer);
        str_t error = STR_LIT("Failed to read entire file");
        tool_result_set_error(out_result, &error);
        return ERR_IO;
    }

    // Null-terminate (for safety, though we treat as binary)
    buffer[bytes_read] = '\0';

    // Set success result
    str_t content = { .data = buffer, .len = (uint32_t)bytes_read };
    tool_result_set_success(out_result, &content);

    // Note: tool_result_set_success duplicates the string, so we can free our buffer
    free(buffer);

    return ERR_OK;
}

static err_t file_read_execute(tool_t* tool, const str_t* args, tool_result_t* out_result) {
    if (!tool || !tool->impl_data || !tool->initialized || !args || !out_result) {
        return ERR_INVALID_ARGUMENT;
    }

    file_read_tool_t* file_read_data = (file_read_tool_t*)tool->impl_data;

    // Parse args (expecting JSON with "path" field)
    // For simplicity, treat args as plain file path for now
    // TODO: Parse JSON args properly
    char* path = malloc(args->len + 1);
    if (!path) return ERR_OUT_OF_MEMORY;

    memcpy(path, args->data, args->len);
    path[args->len] = '\0';

    // Check if path is safe
    if (!is_path_safe(file_read_data, path)) {
        free(path);
        str_t error = STR_LIT("Path not allowed (outside workspace)");
        tool_result_set_error(out_result, &error);
        return ERR_PERMISSION_DENIED;
    }

    // Read file
    err_t result = read_file_contents(path, file_read_data->max_file_size, out_result);

    free(path);
    return result;
}

static str_t file_read_get_parameters_schema(void) {
    // JSON schema for file_read tool parameters
    const char* schema = "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"path\": {"
                "\"type\": \"string\","
                "\"description\": \"Path to file to read\""
            "}"
        "},"
        "\"required\": [\"path\"]"
    "}";

    return (str_t){ .data = schema, .len = strlen(schema) };
}

static bool file_read_requires_memory(void) {
    return false;
}

static bool file_read_allowed_in_autonomous(autonomy_level_t level) {
    // Allow in supervised or full autonomy modes
    return level >= AUTONOMY_LEVEL_SUPERVISED;
}