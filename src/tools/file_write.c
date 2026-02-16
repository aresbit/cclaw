// file_write.c - File writing tool for CClaw
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
#include <fcntl.h>

// File write tool instance data
typedef struct file_write_tool_t {
    config_t* config;           // Configuration for path restrictions
    str_t workspace_dir;        // Working directory restriction
    size_t max_file_size;       // Maximum file size to write (bytes)
    bool allow_overwrite;       // Whether to allow overwriting existing files
} file_write_tool_t;

// Forward declarations for vtable
static str_t file_write_get_name(void);
static str_t file_write_get_description(void);
static str_t file_write_get_version(void);
static err_t file_write_create(tool_t** out_tool);
static void file_write_destroy(tool_t* tool);
static err_t file_write_init(tool_t* tool, const tool_context_t* context);
static void file_write_cleanup(tool_t* tool);
static err_t file_write_execute(tool_t* tool, const str_t* args, tool_result_t* out_result);
static str_t file_write_get_parameters_schema(void);
static bool file_write_requires_memory(void);
static bool file_write_allowed_in_autonomous(autonomy_level_t level);

// VTable definition
static const tool_vtable_t file_write_vtable = {
    .get_name = file_write_get_name,
    .get_description = file_write_get_description,
    .get_version = file_write_get_version,
    .create = file_write_create,
    .destroy = file_write_destroy,
    .init = file_write_init,
    .cleanup = file_write_cleanup,
    .execute = file_write_execute,
    .get_parameters_schema = file_write_get_parameters_schema,
    .requires_memory = file_write_requires_memory,
    .allowed_in_autonomous = file_write_allowed_in_autonomous
};

// Get vtable
const tool_vtable_t* file_write_tool_get_vtable(void) {
    return &file_write_vtable;
}

static str_t file_write_get_name(void) {
    return STR_LIT("file_write");
}

static str_t file_write_get_description(void) {
    return STR_LIT("Write file contents with safety restrictions");
}

static str_t file_write_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t file_write_create(tool_t** out_tool) {
    if (!out_tool) return ERR_INVALID_ARGUMENT;

    tool_t* tool = tool_alloc(&file_write_vtable);
    if (!tool) return ERR_OUT_OF_MEMORY;

    file_write_tool_t* file_write_data = calloc(1, sizeof(file_write_tool_t));
    if (!file_write_data) {
        tool_free(tool);
        return ERR_OUT_OF_MEMORY;
    }

    // Default configuration
    file_write_data->config = NULL;
    file_write_data->workspace_dir = STR_NULL;
    file_write_data->max_file_size = 10 * 1024 * 1024; // 10 MB default limit
    file_write_data->allow_overwrite = true;           // Allow overwrite by default

    tool->impl_data = file_write_data;

    *out_tool = tool;
    return ERR_OK;
}

static void file_write_destroy(tool_t* tool) {
    if (!tool || !tool->impl_data) return;

    file_write_tool_t* file_write_data = (file_write_tool_t*)tool->impl_data;

    // Cleanup will free resources if initialized
    if (tool->initialized) {
        file_write_cleanup(tool);
    }

    // Free workspace directory string
    free((void*)file_write_data->workspace_dir.data);

    free(file_write_data);
    tool->impl_data = NULL;

    tool_free(tool);
}

static err_t file_write_init(tool_t* tool, const tool_context_t* context) {
    if (!tool || !tool->impl_data) return ERR_INVALID_ARGUMENT;
    if (tool->initialized) return ERR_OK;

    file_write_tool_t* file_write_data = (file_write_tool_t*)tool->impl_data;

    // Copy context
    tool->context = *context;

    // Set workspace from context if provided
    if (!str_empty(context->workspace_dir)) {
        file_write_data->workspace_dir = str_dup(context->workspace_dir, NULL);
        if (str_empty(file_write_data->workspace_dir)) {
            return ERR_OUT_OF_MEMORY;
        }
    }

    // TODO: Load configuration for max file size and overwrite settings

    tool->initialized = true;
    return ERR_OK;
}

static void file_write_cleanup(tool_t* tool) {
    if (!tool || !tool->impl_data || !tool->initialized) return;

    // Nothing specific to cleanup for file write tool
    tool->initialized = false;
}

// Check if path is safe to access (within workspace)
static bool is_path_safe(file_write_tool_t* tool_data, const char* path) {
    if (str_empty(tool_data->workspace_dir)) {
        // No workspace restriction
        return true;
    }

    char resolved_path[PATH_MAX];
    char resolved_workspace[PATH_MAX];

    // Resolve paths to absolute
    if (realpath(path, resolved_path) == NULL) {
        // If file doesn't exist, resolve parent directory
        char parent_path[PATH_MAX];
        strncpy(parent_path, path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';

        char* last_slash = strrchr(parent_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (realpath(parent_path, resolved_path) == NULL) {
                return false;
            }
        } else {
            // No directory component, use current directory
            if (getcwd(resolved_path, sizeof(resolved_path)) == NULL) {
                return false;
            }
        }
    }

    if (realpath(tool_data->workspace_dir.data, resolved_workspace) == NULL) {
        return false;
    }

    // Check if resolved_path starts with resolved_workspace
    size_t workspace_len = strlen(resolved_workspace);
    if (strncmp(resolved_path, resolved_workspace, workspace_len) != 0) {
        return false;
    }

    return true;
}

// Write file contents atomically (write to temp file then rename)
static err_t write_file_atomically(const char* path, const char* content, size_t content_len,
                                   bool allow_overwrite, tool_result_t* out_result) {
    int fd = -1;
    FILE* file = NULL;
    char temp_path[PATH_MAX + 10];
    size_t written = 0;

    // Check if file exists and overwrite is not allowed
    if (!allow_overwrite && access(path, F_OK) == 0) {
        str_t error = STR_LIT("File already exists and overwrite not allowed");
        tool_result_set_error(out_result, &error);
        return ERR_FILE_EXISTS;
    }

    // Create temporary file in same directory
    snprintf(temp_path, sizeof(temp_path), "%s.XXXXXX", path);
    fd = mkstemp(temp_path);
    if (fd < 0) {
        str_t error = STR_LIT("Failed to create temporary file");
        tool_result_set_error(out_result, &error);
        return ERR_IO;
    }

    // Convert fd to FILE* for easier writing
    file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temp_path);
        str_t error = STR_LIT("Failed to open temporary file");
        tool_result_set_error(out_result, &error);
        return ERR_IO;
    }

    // Write content
    written = fwrite(content, 1, content_len, file);
    if (written != content_len) {
        fclose(file);
        unlink(temp_path);
        str_t error = STR_LIT("Failed to write entire content");
        tool_result_set_error(out_result, &error);
        return ERR_WRITE_FAILED;
    }

    // Flush to ensure data is written
    if (fflush(file) != 0) {
        fclose(file);
        unlink(temp_path);
        str_t error = STR_LIT("Failed to flush file");
        tool_result_set_error(out_result, &error);
        return ERR_IO;
    }

    // Close file
    fclose(file);

    // Atomically rename temp file to target
    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        str_t error = STR_LIT("Failed to rename temporary file");
        tool_result_set_error(out_result, &error);
        return ERR_IO;
    }

    // Set success result
    str_t success_msg = STR_LIT("File written successfully");
    tool_result_set_success(out_result, &success_msg);

    return ERR_OK;
}

static err_t file_write_execute(tool_t* tool, const str_t* args, tool_result_t* out_result) {
    if (!tool || !tool->impl_data || !tool->initialized || !args || !out_result) {
        return ERR_INVALID_ARGUMENT;
    }

    file_write_tool_t* file_write_data = (file_write_tool_t*)tool->impl_data;

    // Parse args (expecting JSON with "path" and "content" fields)
    // For simplicity, treat args as JSON string for now
    // TODO: Parse JSON args properly using json_config.h
    // For now, assume format: {"path": "file.txt", "content": "text"}
    // We'll implement a simple parser for demonstration

    // Convert args to C string for parsing
    char* args_str = malloc(args->len + 1);
    if (!args_str) return ERR_OUT_OF_MEMORY;

    memcpy(args_str, args->data, args->len);
    args_str[args->len] = '\0';

    // Simple JSON parsing (for demonstration)
    char* path_start = strstr(args_str, "\"path\"");
    char* content_start = strstr(args_str, "\"content\"");

    if (!path_start) {
        free(args_str);
        str_t error = STR_LIT("Missing 'path' field in arguments");
        tool_result_set_error(out_result, &error);
        return ERR_INVALID_ARGUMENT;
    }

    if (!content_start) {
        free(args_str);
        str_t error = STR_LIT("Missing 'content' field in arguments");
        tool_result_set_error(out_result, &error);
        return ERR_INVALID_ARGUMENT;
    }

    // Extract path value (simplified parsing)
    char* path = NULL;
    char* content = NULL;

    // Find path value (after "path":)
    char* path_quote = strchr(path_start + 6, '"');
    if (path_quote) {
        char* path_end = strchr(path_quote + 1, '"');
        if (path_end) {
            size_t path_len = path_end - (path_quote + 1);
            path = malloc(path_len + 1);
            if (path) {
                memcpy(path, path_quote + 1, path_len);
                path[path_len] = '\0';
            }
        }
    }

    // Find content value (after "content":)
    char* content_quote = strchr(content_start + 9, '"');
    if (content_quote) {
        char* content_end = strchr(content_quote + 1, '"');
        if (content_end) {
            size_t content_len = content_end - (content_quote + 1);
            content = malloc(content_len + 1);
            if (content) {
                memcpy(content, content_quote + 1, content_len);
                content[content_len] = '\0';
            }
        }
    }

    free(args_str);

    if (!path || !content) {
        free(path);
        free(content);
        str_t error = STR_LIT("Failed to parse arguments");
        tool_result_set_error(out_result, &error);
        return ERR_INVALID_ARGUMENT;
    }

    // Check if path is safe
    if (!is_path_safe(file_write_data, path)) {
        free(path);
        free(content);
        str_t error = STR_LIT("Path not allowed (outside workspace)");
        tool_result_set_error(out_result, &error);
        return ERR_PERMISSION_DENIED;
    }

    // Check content size
    size_t content_len = strlen(content);
    if (content_len > file_write_data->max_file_size) {
        free(path);
        free(content);
        str_t error = STR_LIT("Content too large");
        tool_result_set_error(out_result, &error);
        return ERR_FILE_TOO_LARGE;
    }

    // Write file
    err_t result = write_file_atomically(path, content, content_len,
                                         file_write_data->allow_overwrite, out_result);

    free(path);
    free(content);
    return result;
}

static str_t file_write_get_parameters_schema(void) {
    // JSON schema for file_write tool parameters
    const char* schema = "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"path\": {"
                "\"type\": \"string\","
                "\"description\": \"Path to file to write\""
            "},"
            "\"content\": {"
                "\"type\": \"string\","
                "\"description\": \"Content to write to file\""
            "}"
        "},"
        "\"required\": [\"path\", \"content\"]"
    "}";

    return (str_t){ .data = schema, .len = strlen(schema) };
}

static bool file_write_requires_memory(void) {
    return false;
}

static bool file_write_allowed_in_autonomous(autonomy_level_t level) {
    // Only allow in supervised mode, not in full autonomy (potentially dangerous)
    return level == AUTONOMY_LEVEL_SUPERVISED;
}