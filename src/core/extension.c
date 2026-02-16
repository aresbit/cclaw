// extension.c - Extension system implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/extension.h"
#include "core/alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// Internal Registry
// ============================================================================

#define MAX_EXTENSIONS 64

static struct {
    extension_t* extensions[MAX_EXTENSIONS];
    uint32_t count;
    bool initialized;
    str_t watch_dir;
    bool watching;
} g_registry = {0};

// ============================================================================
// Manifest Operations
// ============================================================================

void extension_manifest_free(extension_manifest_t* manifest) {
    if (!manifest) return;

    free((void*)manifest->name.data);
    free((void*)manifest->version.data);
    free((void*)manifest->description.data);
    free((void*)manifest->author.data);
    free((void*)manifest->license.data);
    free((void*)manifest->source_file.data);
    free((void*)manifest->entry_point.data);

    for (uint32_t i = 0; i < manifest->dependency_count; i++) {
        free((void*)manifest->dependencies[i].data);
    }
    free(manifest->dependencies);

    memset(manifest, 0, sizeof(*manifest));
}

err_t extension_manifest_parse(const str_t* json, extension_manifest_t* out_manifest) {
    if (!json || !out_manifest) return ERR_INVALID_ARGUMENT;

    // TODO: Parse JSON manifest
    // For now, create a minimal default manifest

    *out_manifest = (extension_manifest_t){0};
    out_manifest->name = str_dup(*json, NULL); // Use input as name for now
    out_manifest->version = str_dup_cstr("0.1.0", NULL);
    out_manifest->type = EXTENSION_TYPE_TOOL;
    out_manifest->needs_filesystem = true;
    out_manifest->needs_network = false;
    out_manifest->needs_shell = false;

    return ERR_OK;
}

err_t extension_manifest_to_json(const extension_manifest_t* manifest, str_t* out_json) {
    if (!manifest || !out_json) return ERR_INVALID_ARGUMENT;

    // Simple JSON generation
    char buffer[2048];
    snprintf(buffer, sizeof(buffer),
        "{\n"
        "  \"name\": \"%.*s\",\n"
        "  \"version\": \"%.*s\",\n"
        "  \"description\": \"%.*s\",\n"
        "  \"type\": \"%s\",\n"
        "  \"permissions\": {\n"
        "    \"filesystem\": %s,\n"
        "    \"network\": %s,\n"
        "    \"shell\": %s\n"
        "  }\n"
        "}\n",
        (int)manifest->name.len, manifest->name.data,
        (int)manifest->version.len, manifest->version.data,
        (int)manifest->description.len, manifest->description.data,
        manifest->type == EXTENSION_TYPE_TOOL ? "tool" :
        manifest->type == EXTENSION_TYPE_COMMAND ? "command" :
        manifest->type == EXTENSION_TYPE_PROVIDER ? "provider" :
        manifest->type == EXTENSION_TYPE_CHANNEL ? "channel" : "hook",
        manifest->needs_filesystem ? "true" : "false",
        manifest->needs_network ? "true" : "false",
        manifest->needs_shell ? "true" : "false"
    );

    *out_json = str_dup_cstr(buffer, NULL);
    return ERR_OK;
}

// ============================================================================
// Extension Lifecycle
// ============================================================================

static uint64_t get_file_mtime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_mtime * 1000;
}

err_t extension_load(const str_t* path, extension_t** out_extension) {
    if (!path || !out_extension) return ERR_INVALID_ARGUMENT;
    if (g_registry.count >= MAX_EXTENSIONS) return ERR_OUT_OF_MEMORY;

    extension_t* ext = calloc(1, sizeof(extension_t));
    if (!ext) return ERR_OUT_OF_MEMORY;

    // Check if file exists
    char* path_cstr = strndup(path->data, path->len);
    if (access(path_cstr, F_OK) != 0) {
        free(path_cstr);
        free(ext);
        return ERR_NOT_FOUND;
    }

    // Read manifest or infer from filename
    ext->manifest.name = str_dup(*path, NULL);
    ext->manifest.version = str_dup_cstr("0.1.0", NULL);
    ext->manifest.type = EXTENSION_TYPE_TOOL;
    ext->manifest.source_file = str_dup(*path, NULL);

    ext->loaded = true;
    ext->last_modified = get_file_mtime(path_cstr);

    free(path_cstr);

    // Add to registry
    g_registry.extensions[g_registry.count++] = ext;

    *out_extension = ext;
    return ERR_OK;
}

err_t extension_unload(extension_t* extension) {
    if (!extension) return ERR_INVALID_ARGUMENT;

    // Remove from registry
    for (uint32_t i = 0; i < g_registry.count; i++) {
        if (g_registry.extensions[i] == extension) {
            // Shift remaining
            for (uint32_t j = i; j < g_registry.count - 1; j++) {
                g_registry.extensions[j] = g_registry.extensions[j + 1];
            }
            g_registry.count--;
            break;
        }
    }

    // Cleanup
    extension_cleanup(extension);
    extension_manifest_free(&extension->manifest);
    free((void*)extension->source_code.data);
    free(extension);

    return ERR_OK;
}

err_t extension_reload(extension_t* extension) {
    if (!extension) return ERR_INVALID_ARGUMENT;

    // Cleanup and re-initialize
    extension_cleanup(extension);

    char* path = strndup(extension->manifest.source_file.data,
                         extension->manifest.source_file.len);
    extension->last_modified = get_file_mtime(path);
    free(path);

    return extension_initialize(extension);
}

err_t extension_initialize(extension_t* extension) {
    if (!extension) return ERR_INVALID_ARGUMENT;
    if (extension->initialized) return ERR_OK;

    // TODO: Load shared object or compile source
    // For now, just mark as initialized

    extension->initialized = true;
    return ERR_OK;
}

void extension_cleanup(extension_t* extension) {
    if (!extension || !extension->initialized) return;

    // TODO: Call cleanup function if available

    if (extension->dl_handle) {
        dlclose(extension->dl_handle);
        extension->dl_handle = NULL;
    }

    extension->initialized = false;
}

// ============================================================================
// Registry Operations
// ============================================================================

err_t extension_registry_init(void) {
    if (g_registry.initialized) return ERR_OK;

    memset(&g_registry, 0, sizeof(g_registry));
    g_registry.initialized = true;

    return ERR_OK;
}

void extension_registry_shutdown(void) {
    if (!g_registry.initialized) return;

    // Unload all extensions
    for (uint32_t i = 0; i < g_registry.count; i++) {
        extension_t* ext = g_registry.extensions[i];
        extension_manifest_free(&ext->manifest);
        free((void*)ext->source_code.data);
        free(ext);
    }

    free((void*)g_registry.watch_dir.data);
    memset(&g_registry, 0, sizeof(g_registry));
}

err_t extension_registry_find(const str_t* name, extension_t** out_extension) {
    if (!name || !out_extension) return ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < g_registry.count; i++) {
        if (str_equal(g_registry.extensions[i]->manifest.name, *name)) {
            *out_extension = g_registry.extensions[i];
            return ERR_OK;
        }
    }

    return ERR_NOT_FOUND;
}

err_t extension_registry_list(extension_t*** out_extensions, uint32_t* out_count) {
    if (!out_extensions || !out_count) return ERR_INVALID_ARGUMENT;

    *out_extensions = g_registry.extensions;
    *out_count = g_registry.count;
    return ERR_OK;
}

// ============================================================================
// Code Generation
// ============================================================================

err_t extension_generate_tool(const str_t* name,
                              const str_t* description,
                              const str_t* parameters_schema,
                              const str_t* implementation_code,
                              str_t* out_source) {
    if (!name || !description || !implementation_code || !out_source) {
        return ERR_INVALID_ARGUMENT;
    }

    // Generate C code for tool extension
    size_t buf_size = 4096 + implementation_code->len;
    char* buffer = malloc(buf_size);
    if (!buffer) return ERR_OUT_OF_MEMORY;

    snprintf(buffer, buf_size,
        "// Auto-generated tool extension: %.*s\n"
        "// Generated by CClaw Agent\n"
        "#include \"cclaw_extension.h\"\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "static const char* TOOL_NAME = \"%.*s\";\n"
        "static const char* TOOL_DESCRIPTION = \"%.*s\";\n"
        "\n"
        "// Tool parameters schema:\n"
        "// %.*s\n"
        "\n"
        "static err_t tool_execute(void* ctx, const str_t* args, tool_result_t* result) {\n"
        "    (void)ctx;\n"
        "    \n"
        "%.*s\n"
        "    \n"
        "    return ERR_OK;\n"
        "}\n"
        "\n"
        "EXTENSION_EXPORT void extension_init(const extension_api_t* api) {\n"
        "    api->log_info(\"Loading tool: %.*s\");\n"
        "    \n"
        "    // Register the tool\n"
        "    tool_def_t def = {\n"
        "        .name = TOOL_NAME,\n"
        "        .description = TOOL_DESCRIPTION\n"
        "    };\n"
        "    api->register_tool(TOOL_NAME, &def);\n"
        "}\n",
        (int)name->len, name->data,
        (int)name->len, name->data,
        (int)description->len, description->data,
        parameters_schema ? (int)parameters_schema->len : 4,
        parameters_schema ? parameters_schema->data : "null",
        (int)implementation_code->len, implementation_code->data,
        (int)name->len, name->data
    );

    out_source->data = buffer;
    out_source->len = strlen(buffer);
    return ERR_OK;
}

err_t extension_generate_manifest(const str_t* name,
                                  extension_type_t type,
                                  const str_t* description,
                                  str_t* out_manifest_json) {
    if (!name || !description || !out_manifest_json) {
        return ERR_INVALID_ARGUMENT;
    }

    extension_manifest_t manifest = {0};
    manifest.name = str_dup(*name, NULL);
    manifest.version = str_dup_cstr("0.1.0", NULL);
    manifest.description = str_dup(*description, NULL);
    manifest.type = type;
    manifest.needs_filesystem = true;

    err_t err = extension_manifest_to_json(&manifest, out_manifest_json);

    extension_manifest_free(&manifest);
    return err;
}

// ============================================================================
// Hot Reload
// ============================================================================

err_t extension_watch_start(const str_t* extensions_dir) {
    if (!extensions_dir) return ERR_INVALID_ARGUMENT;

    g_registry.watch_dir = str_dup(*extensions_dir, NULL);
    g_registry.watching = true;

    return ERR_OK;
}

void extension_watch_stop(void) {
    g_registry.watching = false;
    free((void*)g_registry.watch_dir.data);
    g_registry.watch_dir = STR_NULL;
}

err_t extension_watch_poll(void) {
    if (!g_registry.watching || str_empty(g_registry.watch_dir)) {
        return ERR_INVALID_STATE;
    }

    // Check each extension for modifications
    for (uint32_t i = 0; i < g_registry.count; i++) {
        extension_t* ext = g_registry.extensions[i];

        char* path = strndup(ext->manifest.source_file.data,
                             ext->manifest.source_file.len);
        uint64_t mtime = get_file_mtime(path);
        free(path);

        if (mtime > ext->last_modified) {
            // Extension changed, reload it
            extension_reload(ext);
        }
    }

    return ERR_OK;
}
