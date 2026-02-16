// tool.h - Tool system interface for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_TOOL_H
#define CCLAW_CORE_TOOL_H

#include "core/types.h"
#include "core/error.h"
#include "core/memory.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct tool_t tool_t;
typedef struct tool_vtable_t tool_vtable_t;

// Tool result structure
typedef struct tool_result_t {
    str_t content;
    bool success;
    str_t error_message;
} tool_result_t;

// Tool execution context
typedef struct tool_context_t {
    void* user_data;           // User-provided context
    memory_t* memory;          // Memory system for memory tools
    str_t workspace_dir;       // Current workspace directory
    // Add other context fields as needed
} tool_context_t;

// Tool VTable - defines the tool interface
struct tool_vtable_t {
    // Tool identification
    str_t (*get_name)(void);
    str_t (*get_description)(void);
    str_t (*get_version)(void);

    // Lifecycle
    err_t (*create)(tool_t** out_tool);
    void (*destroy)(tool_t* tool);

    // Initialization and cleanup
    err_t (*init)(tool_t* tool, const tool_context_t* context);
    void (*cleanup)(tool_t* tool);

    // Execution
    err_t (*execute)(tool_t* tool, const str_t* args, tool_result_t* out_result);

    // Parameter schema (JSON)
    str_t (*get_parameters_schema)(void);

    // Whether this tool requires memory access
    bool (*requires_memory)(void);

    // Whether this tool is allowed in autonomous mode
    bool (*allowed_in_autonomous)(autonomy_level_t level);
};

// Tool instance structure
struct tool_t {
    const tool_vtable_t* vtable;
    tool_context_t context;
    void* impl_data;           // Tool-specific data
    bool initialized;
};

// Tool registry (similar to provider and memory registries)
err_t tool_registry_init(void);
void tool_registry_shutdown(void);
err_t tool_register(const char* name, const tool_vtable_t* vtable);
err_t tool_create(const char* name, tool_t** out_tool);
err_t tool_registry_list(const char*** out_names, uint32_t* out_count);

// Built-in tools
const tool_vtable_t* shell_tool_get_vtable(void);
const tool_vtable_t* file_read_tool_get_vtable(void);
const tool_vtable_t* file_write_tool_get_vtable(void);
const tool_vtable_t* memory_store_tool_get_vtable(void);
const tool_vtable_t* memory_recall_tool_get_vtable(void);
const tool_vtable_t* memory_forget_tool_get_vtable(void);

// Tool creation helpers
tool_t* tool_alloc(const tool_vtable_t* vtable);
void tool_free(tool_t* tool);

// Result helpers
tool_result_t tool_result_create(void);
void tool_result_free(tool_result_t* result);
void tool_result_set_success(tool_result_t* result, const str_t* content);
void tool_result_set_error(tool_result_t* result, const str_t* error_message);

// Context helpers
tool_context_t tool_context_default(void);
err_t tool_context_set_memory(tool_context_t* context, memory_t* memory);
err_t tool_context_set_workspace(tool_context_t* context, const str_t* workspace_dir);

// Helper macros for tool implementation
#define TOOL_IMPLEMENT(name, vtable_ptr) \
    const tool_vtable_t* name##_get_vtable(void) { return vtable_ptr; }

#endif // CCLAW_CORE_TOOL_H