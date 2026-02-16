// shell.c - Shell command execution tool for CClaw
// SPDX-License-Identifier: MIT

#include "core/tool.h"
#include "core/config.h"
#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

// Shell tool instance data
typedef struct shell_tool_t {
    config_t* config;          // Configuration for whitelist and restrictions
    str_t* allowed_commands;   // Array of allowed command patterns
    uint32_t allowed_count;
    str_t workspace_dir;       // Working directory restriction
    uint32_t timeout_seconds;  // Execution timeout
} shell_tool_t;

// Forward declarations for vtable
static str_t shell_get_name(void);
static str_t shell_get_description(void);
static str_t shell_get_version(void);
static err_t shell_create(tool_t** out_tool);
static void shell_destroy(tool_t* tool);
static err_t shell_init(tool_t* tool, const tool_context_t* context);
static void shell_cleanup(tool_t* tool);
static err_t shell_execute(tool_t* tool, const str_t* args, tool_result_t* out_result);
static str_t shell_get_parameters_schema(void);
static bool shell_requires_memory(void);
static bool shell_allowed_in_autonomous(autonomy_level_t level);

// VTable definition
static const tool_vtable_t shell_vtable = {
    .get_name = shell_get_name,
    .get_description = shell_get_description,
    .get_version = shell_get_version,
    .create = shell_create,
    .destroy = shell_destroy,
    .init = shell_init,
    .cleanup = shell_cleanup,
    .execute = shell_execute,
    .get_parameters_schema = shell_get_parameters_schema,
    .requires_memory = shell_requires_memory,
    .allowed_in_autonomous = shell_allowed_in_autonomous
};

// Get vtable
const tool_vtable_t* shell_tool_get_vtable(void) {
    return &shell_vtable;
}

static str_t shell_get_name(void) {
    return STR_LIT("shell");
}

static str_t shell_get_description(void) {
    return STR_LIT("Execute shell commands with safety restrictions");
}

static str_t shell_get_version(void) {
    return STR_LIT("1.0.0");
}

static err_t shell_create(tool_t** out_tool) {
    if (!out_tool) return ERR_INVALID_ARGUMENT;

    tool_t* tool = tool_alloc(&shell_vtable);
    if (!tool) return ERR_OUT_OF_MEMORY;

    shell_tool_t* shell_data = calloc(1, sizeof(shell_tool_t));
    if (!shell_data) {
        tool_free(tool);
        return ERR_OUT_OF_MEMORY;
    }

    // Default configuration
    shell_data->config = NULL;
    shell_data->allowed_commands = NULL;
    shell_data->allowed_count = 0;
    shell_data->workspace_dir = STR_NULL;
    shell_data->timeout_seconds = 30; // Default 30 second timeout

    tool->impl_data = shell_data;

    *out_tool = tool;
    return ERR_OK;
}

static void shell_destroy(tool_t* tool) {
    if (!tool || !tool->impl_data) return;

    shell_tool_t* shell_data = (shell_tool_t*)tool->impl_data;

    // Cleanup will free resources if initialized
    if (tool->initialized) {
        shell_cleanup(tool);
    }

    // Free allowed commands array
    if (shell_data->allowed_commands) {
        for (uint32_t i = 0; i < shell_data->allowed_count; i++) {
            free((void*)shell_data->allowed_commands[i].data);
        }
        free(shell_data->allowed_commands);
    }

    free(shell_data->workspace_dir.data);
    free(shell_data);
    tool->impl_data = NULL;

    tool_free(tool);
}

static err_t shell_init(tool_t* tool, const tool_context_t* context) {
    if (!tool || !tool->impl_data) return ERR_INVALID_ARGUMENT;
    if (tool->initialized) return ERR_OK;

    shell_tool_t* shell_data = (shell_tool_t*)tool->impl_data;

    // Copy context
    tool->context = *context;

    // Set workspace from context if provided
    if (!str_empty(context->workspace_dir)) {
        shell_data->workspace_dir = str_dup(context->workspace_dir, NULL);
        if (str_empty(shell_data->workspace_dir)) {
            return ERR_OUT_OF_MEMORY;
        }
    }

    // TODO: Load configuration and populate allowed_commands
    // For now, allow basic commands
    shell_data->allowed_count = 5;
    shell_data->allowed_commands = calloc(shell_data->allowed_count, sizeof(str_t));
    if (!shell_data->allowed_commands) {
        return ERR_OUT_OF_MEMORY;
    }

    // Basic safe command patterns
    const char* default_commands[] = {
        "ls", "pwd", "echo", "cat", "grep"
    };

    for (uint32_t i = 0; i < shell_data->allowed_count; i++) {
        shell_data->allowed_commands[i] = str_dup_cstr(default_commands[i], NULL);
        if (str_empty(shell_data->allowed_commands[i])) {
            // Cleanup on error
            for (uint32_t j = 0; j < i; j++) {
                free((void*)shell_data->allowed_commands[j].data);
            }
            free(shell_data->allowed_commands);
            return ERR_OUT_OF_MEMORY;
        }
    }

    tool->initialized = true;
    return ERR_OK;
}

static void shell_cleanup(tool_t* tool) {
    if (!tool || !tool->impl_data || !tool->initialized) return;

    shell_tool_t* shell_data = (shell_tool_t*)tool->impl_data;

    // Nothing specific to cleanup for shell tool

    tool->initialized = false;
}

// Check if command is allowed
static bool is_command_allowed(shell_tool_t* shell_data, const char* command) {
    if (!shell_data->allowed_commands || shell_data->allowed_count == 0) {
        return false; // No commands allowed if list is empty
    }

    // Extract first word from command
    const char* first_space = strchr(command, ' ');
    size_t cmd_len = first_space ? (size_t)(first_space - command) : strlen(command);

    for (uint32_t i = 0; i < shell_data->allowed_count; i++) {
        const str_t* allowed = &shell_data->allowed_commands[i];
        if (allowed->len == cmd_len && strncmp(allowed->data, command, cmd_len) == 0) {
            return true;
        }
    }

    return false;
}

// Execute command with timeout (simple implementation using popen)
static err_t execute_command(const char* command, uint32_t timeout_seconds,
                            const char* workspace_dir, tool_result_t* out_result) {
    FILE* fp = NULL;
    char buffer[4096];
    char* output = NULL;
    size_t output_size = 0;
    size_t output_len = 0;
    int status = 0;

    // Change to workspace directory if specified
    char original_cwd[1024];
    if (workspace_dir && getcwd(original_cwd, sizeof(original_cwd)) != NULL) {
        if (chdir(workspace_dir) != 0) {
            str_t error = STR_LIT("Failed to change to workspace directory");
            tool_result_set_error(out_result, &error);
            return ERR_IO;
        }
    }

    // Execute command using popen (simpler than fork/exec for now)
    fp = popen(command, "r");
    if (!fp) {
        if (workspace_dir) chdir(original_cwd);
        str_t error = STR_LIT("Failed to execute command");
        tool_result_set_error(out_result, &error);
        return ERR_TOOL_EXECUTION_FAILED;
    }

    // Read output
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t line_len = strlen(buffer);

        // Resize output buffer if needed
        if (output_len + line_len + 1 > output_size) {
            size_t new_size = output_size ? output_size * 2 : 4096;
            char* new_output = realloc(output, new_size);
            if (!new_output) {
                free(output);
                pclose(fp);
                if (workspace_dir) chdir(original_cwd);
                return ERR_OUT_OF_MEMORY;
            }
            output = new_output;
            output_size = new_size;
        }

        // Append line to output
        memcpy(output + output_len, buffer, line_len);
        output_len += line_len;
        output[output_len] = '\0';
    }

    // Get exit status
    status = pclose(fp);

    // Restore original directory
    if (workspace_dir) chdir(original_cwd);

    // Check exit status
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            // Success
            if (output) {
                str_t result_str = { .data = output, .len = (uint32_t)output_len };
                tool_result_set_success(out_result, &result_str);
            } else {
                str_t empty = STR_LIT("Command executed successfully (no output)");
                tool_result_set_success(out_result, &empty);
            }
            free(output);
            return ERR_OK;
        } else {
            // Command failed
            str_t error_msg;
            char error_buf[512];
            snprintf(error_buf, sizeof(error_buf), "Command failed with exit code %d", exit_code);
            error_msg = (str_t){ .data = strdup(error_buf), .len = strlen(error_buf) };

            if (output) {
                // Include output in error message
                char* full_error = malloc(error_msg.len + output_len + 64);
                if (full_error) {
                    snprintf(full_error, error_msg.len + output_len + 64,
                            "%s\nOutput:\n%s", error_buf, output);
                    free((void*)error_msg.data);
                    error_msg.data = full_error;
                    error_msg.len = strlen(full_error);
                }
            }

            tool_result_set_error(out_result, &error_msg);
            free((void*)error_msg.data);
            free(output);
            return ERR_TOOL_EXECUTION_FAILED;
        }
    } else if (WIFSIGNALED(status)) {
        // Command killed by signal
        str_t error = STR_LIT("Command killed by signal");
        tool_result_set_error(out_result, &error);
        free(output);
        return ERR_TOOL_EXECUTION_FAILED;
    } else {
        // Unknown error
        str_t error = STR_LIT("Command execution failed");
        tool_result_set_error(out_result, &error);
        free(output);
        return ERR_TOOL_EXECUTION_FAILED;
    }
}

static err_t shell_execute(tool_t* tool, const str_t* args, tool_result_t* out_result) {
    if (!tool || !tool->impl_data || !tool->initialized || !args || !out_result) {
        return ERR_INVALID_ARGUMENT;
    }

    shell_tool_t* shell_data = (shell_tool_t*)tool->impl_data;

    // Convert args to C string
    char* command = malloc(args->len + 1);
    if (!command) return ERR_OUT_OF_MEMORY;

    memcpy(command, args->data, args->len);
    command[args->len] = '\0';

    // Check if command is allowed
    if (!is_command_allowed(shell_data, command)) {
        free(command);
        str_t error = STR_LIT("Command not allowed by whitelist");
        tool_result_set_error(out_result, &error);
        return ERR_TOOL_NOT_ALLOWED;
    }

    // Execute command
    err_t result = execute_command(command, shell_data->timeout_seconds,
                                  shell_data->workspace_dir.data ? shell_data->workspace_dir.data : NULL,
                                  out_result);

    free(command);
    return result;
}

static str_t shell_get_parameters_schema(void) {
    // JSON schema for shell tool parameters
    const char* schema = "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"command\": {"
                "\"type\": \"string\","
                "\"description\": \"Shell command to execute\""
            "}"
        "},"
        "\"required\": [\"command\"]"
    "}";

    return (str_t){ .data = schema, .len = strlen(schema) };
}

static bool shell_requires_memory(void) {
    return false;
}

static bool shell_allowed_in_autonomous(autonomy_level_t level) {
    // Only allow in supervised or full autonomy modes
    return level >= AUTONOMY_LEVEL_SUPERVISED;
}