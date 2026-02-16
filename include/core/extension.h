// extension.h - Extension system for CClaw (Pi philosophy: agent extends itself)
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_EXTENSION_H
#define CCLAW_CORE_EXTENSION_H

#include "core/types.h"
#include "core/error.h"
#include "core/agent.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct extension_t extension_t;
typedef struct extension_api_t extension_api_t;
typedef struct extension_manifest_t extension_manifest_t;

// Extension types (Pi-style: code as extension)
typedef enum {
    EXTENSION_TYPE_TOOL,       // New tool implementation
    EXTENSION_TYPE_COMMAND,    // New CLI command
    EXTENSION_TYPE_PROVIDER,   // New AI provider
    EXTENSION_TYPE_CHANNEL,    // New communication channel
    EXTENSION_TYPE_HOOK,       // Event hook/interceptor
    EXTENSION_TYPE_THEME,      // UI theme/styling
} extension_type_t;

// Extension manifest (metadata)
struct extension_manifest_t {
    str_t name;                // Extension identifier
    str_t version;             // Semver
    str_t description;
    str_t author;
    str_t license;

    extension_type_t type;

    // Dependencies
    str_t* dependencies;       // Array of "name@version" strings
    uint32_t dependency_count;

    // Permissions
    bool needs_filesystem;     // Access to filesystem
    bool needs_network;        // Network access
    bool needs_shell;          // Shell execution
    bool needs_memory;         // Memory system access

    // Source info
    str_t source_file;         // Main source file
    str_t entry_point;         // Entry function name (for C extensions)
};

// Extension API (what extensions can use)
// This is the "contract" between agent and extensions
struct extension_api_t {
    // Logging
    void (*log_info)(const char* msg);
    void (*log_error)(const char* msg);
    void (*log_debug)(const char* msg);

    // Tool registration
    err_t (*register_tool)(const char* name, const tool_vtable_t* vtable);
    err_t (*unregister_tool)(const char* name);

    // Memory access
    err_t (*memory_store)(const char* key, const char* content);
    err_t (*memory_recall)(const char* key, char** out_content);

    // HTTP
    err_t (*http_get)(const char* url, char** out_response);
    err_t (*http_post)(const char* url, const char* body, char** out_response);

    // Agent interaction
    err_t (*agent_ask)(const char* question, char** out_answer);

    // File operations (within workspace)
    err_t (*file_read)(const char* path, char** out_content);
    err_t (*file_write)(const char* path, const char* content);

    // Shell (if permitted)
    err_t (*shell_exec)(const char* command, char** out_output);

    // Hot reload notification
    void (*request_reload)(void);
};

// Extension instance
struct extension_t {
    extension_manifest_t manifest;
    extension_api_t api;

    // Runtime state
    bool loaded;
    bool initialized;
    uint64_t load_time;
    uint64_t last_modified;    // For hot-reload detection

    // Shared object handle (for compiled extensions)
    void* dl_handle;

    // Source code (for interpreted extensions)
    str_t source_code;

    // User data
    void* user_data;
};

// ============================================================================
// Extension Registry
// ============================================================================

err_t extension_registry_init(void);
void extension_registry_shutdown(void);

err_t extension_load(const str_t* path, extension_t** out_extension);
err_t extension_unload(extension_t* extension);
err_t extension_reload(extension_t* extension);

err_t extension_registry_find(const str_t* name, extension_t** out_extension);
err_t extension_registry_list(extension_t*** out_extensions, uint32_t* out_count);

// ============================================================================
// Extension Lifecycle
// ============================================================================

typedef err_t (*extension_init_fn_t)(const extension_api_t* api, void** out_user_data);
typedef void (*extension_cleanup_fn_t)(void* user_data);

err_t extension_initialize(extension_t* extension);
void extension_cleanup(extension_t* extension);

// ============================================================================
// Hot Reload
// ============================================================================

err_t extension_watch_start(const str_t* extensions_dir);
void extension_watch_stop(void);
err_t extension_watch_poll(void);  // Check for changes and reload

// ============================================================================
// Manifest Operations
// ============================================================================

err_t extension_manifest_parse(const str_t* json, extension_manifest_t* out_manifest);
void extension_manifest_free(extension_manifest_t* manifest);
err_t extension_manifest_to_json(const extension_manifest_t* manifest, str_t* out_json);

// ============================================================================
// Code Generation (Agent writes extensions)
// ============================================================================

// Generate a tool extension from description
err_t extension_generate_tool(const str_t* name,
                              const str_t* description,
                              const str_t* parameters_schema,
                              const str_t* implementation_code,
                              str_t* out_source);

// Generate a command extension
err_t extension_generate_command(const str_t* name,
                                 const str_t* description,
                                 const str_t* implementation_code,
                                 str_t* out_source);

// Generate extension manifest
err_t extension_generate_manifest(const str_t* name,
                                  extension_type_t type,
                                  const str_t* description,
                                  str_t* out_manifest_json);

// ============================================================================
// Pi-Style Self-Extension Helpers
// ============================================================================

// Agent uses this to create a new extension from natural language description
err_t extension_create_from_description(agent_t* agent,
                                        const str_t* description,
                                        extension_t** out_extension);

// Test an extension before loading
err_t extension_test_compile(const str_t* source_code,
                             str_t* out_errors);

// Validate extension safety
err_t extension_validate(const extension_t* extension,
                         bool* out_is_safe,
                         str_t* out_warnings);

// ============================================================================
// Constants
// ============================================================================

#define EXTENSION_DIR_DEFAULT ".cclaw/extensions"
#define EXTENSION_FILE_EXTENSION ".c"
#define EXTENSION_MANIFEST_FILE "manifest.json"
#define EXTENSION_MAX_NAME_LEN 64
#define EXTENSION_MAX_DEPENDENCIES 16

// Template for tool extension (C code)
#define EXTENSION_TOOL_TEMPLATE \
    "// Auto-generated tool extension for CClaw\n" \
    "#include \"cclaw_extension.h\"\n" \
    "\n" \
    "static const char* TOOL_NAME = \"%s\";\n" \
    "static const char* TOOL_DESCRIPTION = \"%s\";\n" \
    "\n" \
    "static err_t tool_execute(void* ctx, const str_t* args, tool_result_t* result) {\n" \
    "%s\n" \
    "    return ERR_OK;\n" \
    "}\n" \
    "\n" \
    "EXTENSION_EXPORT void extension_init(const extension_api_t* api) {\n" \
    "    tool_def_t def = {\n" \
    "        .name = TOOL_NAME,\n" \
    "        .description = TOOL_DESCRIPTION,\n" \
    "        .execute = tool_execute\n" \
    "    };\n" \
    "    api->register_tool(TOOL_NAME, &def);\n" \
    "}\n"

#endif // CCLAW_CORE_EXTENSION_H
