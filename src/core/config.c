// config.c - Configuration system implementation
// SPDX-License-Identifier: MIT

#include "core/config.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>

// Default configuration values
#define DEFAULT_PROVIDER "openrouter"
#define DEFAULT_MODEL "anthropic/claude-sonnet-4-20250514"
#define DEFAULT_TEMPERATURE 0.7
#define DEFAULT_PORT 8080
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_MEMORY_BACKEND "sqlite"

typedef struct allocator_t {
    void* (*alloc)(size_t size);
    void (*free)(void* ptr);
    void* (*realloc)(void* ptr, size_t size);
} allocator_t;

// Global default allocator using stdlib
static void* default_alloc(size_t size) {
    return malloc(size);
}

static void default_free(void* ptr) {
    free(ptr);
}

static void* default_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

static allocator_t g_default_allocator = {
    .alloc = default_alloc,
    .free = default_free,
    .realloc = default_realloc
};

allocator_t* allocator_default(void) {
    return &g_default_allocator;
}

static str_t str_dup_impl(str_t s, allocator_t* alloc) {
    if (str_empty(s)) return STR_NULL;

    char* data = alloc->alloc(s.len + 1);
    if (!data) return STR_NULL;

    memcpy(data, s.data, s.len);
    data[s.len] = '\0';

    return (str_t){.data = data, .len = s.len};
}

static void str_free_impl(str_t s, allocator_t* alloc) {
    if (s.data) {
        alloc->free((void*)s.data);
    }
}

// Create a new empty configuration
config_t* config_create(allocator_t* alloc) {
    if (!alloc) alloc = allocator_default();

    config_t* config = alloc->alloc(sizeof(config_t));
    if (!config) return NULL;

    memset(config, 0, sizeof(config_t));
    config->alloc = alloc;

    return config;
}

// Destroy a configuration and free all associated memory
void config_destroy(config_t* config) {
    if (!config) return;

    allocator_t* alloc = config->alloc;
    if (!alloc) alloc = allocator_default();

    // Free all string fields
    str_free_impl(config->workspace_dir, alloc);
    str_free_impl(config->config_path, alloc);
    str_free_impl(config->api_key, alloc);
    str_free_impl(config->default_provider, alloc);
    str_free_impl(config->default_model, alloc);

    // Free memory configuration strings
    str_free_impl(config->memory.backend, alloc);
    str_free_impl(config->memory.embedding_provider, alloc);
    str_free_impl(config->memory.embedding_model, alloc);

    // Free gateway configuration
    str_free_impl(config->gateway.host, alloc);
    if (config->gateway.paired_tokens) {
        for (uint32_t i = 0; i < config->gateway.paired_tokens_count; i++) {
            str_free_impl(config->gateway.paired_tokens[i], alloc);
        }
        alloc->free(config->gateway.paired_tokens);
    }

    // Free autonomy configuration arrays
    if (config->autonomy.allowed_commands) {
        for (uint32_t i = 0; i < config->autonomy.allowed_commands_count; i++) {
            str_free_impl(config->autonomy.allowed_commands[i], alloc);
        }
        alloc->free(config->autonomy.allowed_commands);
    }
    if (config->autonomy.forbidden_paths) {
        for (uint32_t i = 0; i < config->autonomy.forbidden_paths_count; i++) {
            str_free_impl(config->autonomy.forbidden_paths[i], alloc);
        }
        alloc->free(config->autonomy.forbidden_paths);
    }

    // Free runtime configuration
    str_free_impl(config->runtime.docker.image, alloc);
    str_free_impl(config->runtime.docker.network, alloc);
    if (config->runtime.docker.allowed_workspace_roots) {
        for (uint32_t i = 0; i < config->runtime.docker.allowed_workspace_roots_count; i++) {
            str_free_impl(config->runtime.docker.allowed_workspace_roots[i], alloc);
        }
        alloc->free(config->runtime.docker.allowed_workspace_roots);
    }

    // Free the config itself
    alloc->free(config);
}

// Create a default configuration
config_t* config_default(allocator_t* alloc) {
    if (!alloc) alloc = allocator_default();

    config_t* config = config_create(alloc);
    if (!config) return NULL;

    // API configuration
    config->default_provider = str_dup_impl(STR_LIT(DEFAULT_PROVIDER), alloc);
    config->default_model = str_dup_impl(STR_LIT(DEFAULT_MODEL), alloc);
    config->default_temperature = DEFAULT_TEMPERATURE;

    // Memory configuration
    config->memory.backend = str_dup_impl(STR_LIT(DEFAULT_MEMORY_BACKEND), alloc);
    config->memory.auto_save = true;
    config->memory.hygiene_enabled = true;
    config->memory.archive_after_days = 7;
    config->memory.purge_after_days = 30;
    config->memory.conversation_retention_days = 30;
    config->memory.embedding_provider = str_dup_impl(STR_LIT("none"), alloc);
    config->memory.embedding_model = str_dup_impl(STR_LIT("text-embedding-3-small"), alloc);
    config->memory.embedding_dimensions = 1536;
    config->memory.vector_weight = 0.7;
    config->memory.keyword_weight = 0.3;
    config->memory.embedding_cache_size = 10000;
    config->memory.chunk_max_tokens = 512;

    // Gateway configuration
    config->gateway.port = DEFAULT_PORT;
    config->gateway.host = str_dup_impl(STR_LIT(DEFAULT_HOST), alloc);
    config->gateway.require_pairing = true;
    config->gateway.allow_public_bind = false;
    config->gateway.pair_rate_limit_per_minute = 10;
    config->gateway.webhook_rate_limit_per_minute = 60;
    config->gateway.idempotency_ttl_secs = 300;

    // Autonomy configuration
    config->autonomy.level = AUTONOMY_LEVEL_SUPERVISED;
    config->autonomy.workspace_only = true;
    config->autonomy.max_actions_per_hour = 20;
    config->autonomy.max_cost_per_day_cents = 500;
    config->autonomy.require_approval_for_medium_risk = true;
    config->autonomy.block_high_risk_commands = true;

    // Default allowed commands
    const char* default_commands[] = {
        "git", "npm", "cargo", "ls", "cat", "grep", "find",
        "echo", "pwd", "wc", "head", "tail"
    };
    config->autonomy.allowed_commands_count = sizeof(default_commands) / sizeof(default_commands[0]);
    config->autonomy.allowed_commands = alloc->alloc(sizeof(str_t) * config->autonomy.allowed_commands_count);
    for (uint32_t i = 0; i < config->autonomy.allowed_commands_count; i++) {
        config->autonomy.allowed_commands[i] = str_dup_impl(STR_VIEW(default_commands[i]), alloc);
    }

    // Default forbidden paths
    const char* default_forbidden[] = {
        "/etc", "/root", "/home", "/usr", "/bin", "/sbin",
        "/lib", "/opt", "/boot", "/dev", "/proc", "/sys",
        "/var", "/tmp", "~/.ssh", "~/.gnupg", "~/.aws", "~/.config"
    };
    config->autonomy.forbidden_paths_count = sizeof(default_forbidden) / sizeof(default_forbidden[0]);
    config->autonomy.forbidden_paths = alloc->alloc(sizeof(str_t) * config->autonomy.forbidden_paths_count);
    for (uint32_t i = 0; i < config->autonomy.forbidden_paths_count; i++) {
        config->autonomy.forbidden_paths[i] = str_dup_impl(STR_VIEW(default_forbidden[i]), alloc);
    }

    // Runtime configuration
    config->runtime.kind = RUNTIME_KIND_NATIVE;
    config->runtime.docker.image = str_dup_impl(STR_LIT("alpine:3.20"), alloc);
    config->runtime.docker.network = str_dup_impl(STR_LIT("none"), alloc);
    config->runtime.docker.memory_limit_mb = 512;
    config->runtime.docker.cpu_limit = 1.0;
    config->runtime.docker.read_only_rootfs = true;
    config->runtime.docker.mount_workspace = true;

    // Reliability configuration
    config->reliability.provider_retries = 2;
    config->reliability.provider_backoff_ms = 500;
    config->reliability.channel_initial_backoff_secs = 2;
    config->reliability.channel_max_backoff_secs = 60;
    config->reliability.scheduler_poll_secs = 15;
    config->reliability.scheduler_retries = 2;

    // Heartbeat configuration
    config->heartbeat.enabled = false;
    config->heartbeat.interval_minutes = 30;

    // Channel configuration
    config->channels.cli = true;

    // Tunnel configuration
    config->tunnel.provider = str_dup_impl(STR_LIT("none"), alloc);

    // Browser configuration
    config->browser.enabled = false;

    // Composio configuration
    config->composio.enabled = false;
    config->composio.entity_id = str_dup_impl(STR_LIT("default"), alloc);

    // Secrets configuration
    config->secrets.encrypt = true;

    // Identity configuration
    config->identity.format = str_dup_impl(STR_LIT("openclaw"), alloc);

    // Observability configuration
    config->observability.backend = str_dup_impl(STR_LIT("none"), alloc);

    return config;
}

// Helper: Load string array from JSON
static str_t* load_string_array(json_array_t* arr, uint32_t* count, allocator_t* alloc) {
    if (!arr || !count) return NULL;

    size_t len = json_array_length(arr);
    if (len == 0) {
        *count = 0;
        return NULL;
    }

    str_t* strings = alloc->alloc(sizeof(str_t) * len);
    if (!strings) return NULL;

    for (size_t i = 0; i < len; i++) {
        json_value_t* item = json_array_get(arr, i);
        const char* str = json_as_string(item, "");
        strings[i] = str_dup_impl(STR_VIEW(str), alloc);
    }

    *count = (uint32_t)len;
    return strings;
}

// Load configuration from file
err_t config_load(str_t path, config_t** out_config) {
    if (!out_config) return ERR_INVALID_ARGUMENT;

    allocator_t* alloc = allocator_default();

    // Determine config path
    char config_path[512];
    if (str_empty(path)) {
        const char* home = getenv("HOME");
        if (!home) home = ".";
        snprintf(config_path, sizeof(config_path), "%s/.cclaw/config.json", home);
    } else {
        snprintf(config_path, sizeof(config_path), "%.*s", (int)path.len, path.data);
    }

    // Check if file exists
    struct stat st;
    if (stat(config_path, &st) != 0) {
        // Config doesn't exist, create default
        config_t* config = config_default(alloc);
        if (!config) return ERR_OUT_OF_MEMORY;

        // Set paths
        const char* home = getenv("HOME");
        if (!home) home = ".";
        char workspace_path[512];
        snprintf(workspace_path, sizeof(workspace_path), "%s/.cclaw/workspace", home);

        config->workspace_dir = str_dup_impl(STR_VIEW(workspace_path), alloc);
        config->config_path = str_dup_impl(STR_VIEW(config_path), alloc);

        // Create directory if needed
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "%s/.cclaw", home);
        mkdir(dir_path, 0755);
        snprintf(dir_path, sizeof(dir_path), "%s/.cclaw/workspace", home);
        mkdir(dir_path, 0755);

        // Save default config
        config_save(config, config->config_path);

        *out_config = config;
        return ERR_OK;
    }

    // Parse existing config
    json_value_t* json = json_parse_file(config_path);
    if (!json) {
        // Parse error, fall back to default
        config_t* config = config_default(alloc);
        if (!config) return ERR_OUT_OF_MEMORY;

        const char* home = getenv("HOME");
        if (!home) home = ".";
        char workspace_path[512];
        snprintf(workspace_path, sizeof(workspace_path), "%s/.cclaw/workspace", home);

        config->workspace_dir = str_dup_impl(STR_VIEW(workspace_path), alloc);
        config->config_path = str_dup_impl(STR_VIEW(config_path), alloc);

        *out_config = config;
        return ERR_OK;
    }

    // Convert JSON to config
    err_t err = config_from_json_str(json, alloc, out_config);
    json_free(json);

    if (err == ERR_OK) {
        // Set config_path for loaded config
        if (*out_config) {
            (*out_config)->config_path = str_dup_impl(STR_VIEW(config_path), alloc);
        }
    } else {
        // Error, use default
        config_t* config = config_default(alloc);
        if (!config) return ERR_OUT_OF_MEMORY;

        const char* home = getenv("HOME");
        if (!home) home = ".";
        char workspace_path[512];
        snprintf(workspace_path, sizeof(workspace_path), "%s/.cclaw/workspace", home);

        config->workspace_dir = str_dup_impl(STR_VIEW(workspace_path), alloc);
        config->config_path = str_dup_impl(STR_VIEW(config_path), alloc);

        *out_config = config;
    }

    return ERR_OK;
}

// Parse config from JSON value
err_t config_from_json_str(json_value_t* json, allocator_t* alloc, config_t** out_config) {
    if (!json || !out_config) return ERR_INVALID_ARGUMENT;

    if (!json_is_object(json)) return ERR_CONFIG_PARSE;

    json_object_t* root = json_as_object(json);
    config_t* config = config_create(alloc);
    if (!config) return ERR_OUT_OF_MEMORY;

    // API configuration
    const char* api_key = json_object_get_string(root, "api_key", NULL);
    if (api_key) {
        config->api_key = str_dup_impl(STR_VIEW(api_key), alloc);
    }

    const char* provider = json_object_get_string(root, "default_provider", NULL);
    if (provider) {
        str_free_impl(config->default_provider, alloc);
        config->default_provider = str_dup_impl(STR_VIEW(provider), alloc);
    }

    const char* model = json_object_get_string(root, "default_model", NULL);
    if (model) {
        str_free_impl(config->default_model, alloc);
        config->default_model = str_dup_impl(STR_VIEW(model), alloc);
    }

    config->default_temperature = json_object_get_number(root, "default_temperature", DEFAULT_TEMPERATURE);

    // Workspace directory
    const char* workspace_dir = json_object_get_string(root, "workspace_dir", NULL);
    if (workspace_dir) {
        str_free_impl(config->workspace_dir, alloc);
        config->workspace_dir = str_dup_impl(STR_VIEW(workspace_dir), alloc);
    }

    // Memory configuration
    json_object_t* memory = json_object_get_object(root, "memory");
    if (memory) {
        const char* backend = json_object_get_string(memory, "backend", NULL);
        if (backend) {
            str_free_impl(config->memory.backend, alloc);
            config->memory.backend = str_dup_impl(STR_VIEW(backend), alloc);
        }
        config->memory.auto_save = json_object_get_bool(memory, "auto_save", true);
        config->memory.hygiene_enabled = json_object_get_bool(memory, "hygiene_enabled", true);
        config->memory.archive_after_days = (uint32_t)json_object_get_number(memory, "archive_after_days", 7);
        config->memory.purge_after_days = (uint32_t)json_object_get_number(memory, "purge_after_days", 30);
    }

    // Gateway configuration
    json_object_t* gateway = json_object_get_object(root, "gateway");
    if (gateway) {
        config->gateway.port = (uint16_t)json_object_get_number(gateway, "port", DEFAULT_PORT);
        const char* host = json_object_get_string(gateway, "host", NULL);
        if (host) {
            str_free_impl(config->gateway.host, alloc);
            config->gateway.host = str_dup_impl(STR_VIEW(host), alloc);
        }
        config->gateway.require_pairing = json_object_get_bool(gateway, "require_pairing", true);
        config->gateway.allow_public_bind = json_object_get_bool(gateway, "allow_public_bind", false);
    }

    // Autonomy configuration
    json_object_t* autonomy = json_object_get_object(root, "autonomy");
    if (autonomy) {
        int level = (int)json_object_get_number(autonomy, "level", AUTONOMY_LEVEL_SUPERVISED);
        config->autonomy.level = (autonomy_level_t)level;
        config->autonomy.workspace_only = json_object_get_bool(autonomy, "workspace_only", true);
        config->autonomy.max_actions_per_hour = (uint32_t)json_object_get_number(autonomy, "max_actions_per_hour", 20);
    }

    *out_config = config;
    return ERR_OK;
}

// Helper: Create parent directories for a file path (mkdir -p style)
static int mkdir_p(const char* path) {
    char tmp[512];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, 0755) != 0)
                    return -1;
            } else if (!S_ISDIR(st.st_mode)) {
                return -1;
            }
            *p = '/';
        }
    }
    return 0;
}

// Save configuration to file
err_t config_save(config_t* config, str_t path) {
    if (!config) return ERR_INVALID_ARGUMENT;

    allocator_t* alloc = config->alloc;
    if (!alloc) alloc = allocator_default();

    char config_path[512];
    if (str_empty(path)) {
        snprintf(config_path, sizeof(config_path), "%.*s", (int)config->config_path.len, config->config_path.data);
    } else {
        snprintf(config_path, sizeof(config_path), "%.*s", (int)path.len, path.data);
    }

    // Create parent directories if they don't exist
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s", config_path);
    char* dir = dirname(dir_path);
    if (mkdir_p(dir) != 0) {
        return ERR_IO;
    }

    // Convert to JSON
    json_value_t* json = json_create_object();
    if (!json) return ERR_OUT_OF_MEMORY;

    // API configuration
    if (!str_empty(config->api_key)) {
        json_object_set_string(json, "api_key", config->api_key.data);
    }
    json_object_set_string(json, "default_provider", str_empty(config->default_provider) ? DEFAULT_PROVIDER : config->default_provider.data);
    json_object_set_string(json, "default_model", str_empty(config->default_model) ? DEFAULT_MODEL : config->default_model.data);
    json_object_set_number(json, "default_temperature", config->default_temperature);

    // Memory configuration
    json_value_t* memory = json_create_object();
    json_object_set_string(memory, "backend", config->memory.backend.data);
    json_object_set_bool(memory, "auto_save", config->memory.auto_save);
    json_object_set_bool(memory, "hygiene_enabled", config->memory.hygiene_enabled);
    json_object_set_number(memory, "archive_after_days", config->memory.archive_after_days);
    json_object_set_number(memory, "purge_after_days", config->memory.purge_after_days);
    json_object_set_number(memory, "conversation_retention_days", config->memory.conversation_retention_days);
    json_object_set(json, "memory", memory);

    // Gateway configuration
    json_value_t* gateway = json_create_object();
    json_object_set_number(gateway, "port", config->gateway.port);
    json_object_set_string(gateway, "host", config->gateway.host.data);
    json_object_set_bool(gateway, "require_pairing", config->gateway.require_pairing);
    json_object_set_bool(gateway, "allow_public_bind", config->gateway.allow_public_bind);
    json_object_set_number(gateway, "pair_rate_limit_per_minute", config->gateway.pair_rate_limit_per_minute);
    json_object_set_number(gateway, "webhook_rate_limit_per_minute", config->gateway.webhook_rate_limit_per_minute);
    json_object_set(json, "gateway", gateway);

    // Autonomy configuration
    json_value_t* autonomy = json_create_object();
    json_object_set_number(autonomy, "level", config->autonomy.level);
    json_object_set_bool(autonomy, "workspace_only", config->autonomy.workspace_only);
    json_object_set_number(autonomy, "max_actions_per_hour", config->autonomy.max_actions_per_hour);
    json_object_set_number(autonomy, "max_cost_per_day_cents", config->autonomy.max_cost_per_day_cents);
    json_object_set_bool(autonomy, "require_approval_for_medium_risk", config->autonomy.require_approval_for_medium_risk);
    json_object_set_bool(autonomy, "block_high_risk_commands", config->autonomy.block_high_risk_commands);

    // Allowed commands array
    if (config->autonomy.allowed_commands_count > 0) {
        json_value_t* allowed_cmds = json_create_array();
        for (uint32_t i = 0; i < config->autonomy.allowed_commands_count; i++) {
            json_value_t* cmd = json_create_string(config->autonomy.allowed_commands[i].data);
            json_array_append(allowed_cmds, cmd);
        }
        json_object_set(autonomy, "allowed_commands", allowed_cmds);
    }

    json_object_set(json, "autonomy", autonomy);

    // Runtime configuration
    json_value_t* runtime = json_create_object();
    json_object_set_number(runtime, "kind", config->runtime.kind);
    json_value_t* docker = json_create_object();
    json_object_set_string(docker, "image", config->runtime.docker.image.data);
    json_object_set_string(docker, "network", config->runtime.docker.network.data);
    json_object_set_number(docker, "memory_limit_mb", config->runtime.docker.memory_limit_mb);
    json_object_set_number(docker, "cpu_limit", config->runtime.docker.cpu_limit);
    json_object_set_bool(docker, "read_only_rootfs", config->runtime.docker.read_only_rootfs);
    json_object_set_bool(docker, "mount_workspace", config->runtime.docker.mount_workspace);
    json_object_set(runtime, "docker", docker);
    json_object_set(json, "runtime", runtime);

    // Reliability configuration
    json_value_t* reliability = json_create_object();
    json_object_set_number(reliability, "provider_retries", config->reliability.provider_retries);
    json_object_set_number(reliability, "provider_backoff_ms", config->reliability.provider_backoff_ms);
    json_object_set_number(reliability, "channel_initial_backoff_secs", config->reliability.channel_initial_backoff_secs);
    json_object_set_number(reliability, "channel_max_backoff_secs", config->reliability.channel_max_backoff_secs);
    json_object_set(json, "reliability", reliability);

    // Heartbeat configuration
    json_value_t* heartbeat = json_create_object();
    json_object_set_bool(heartbeat, "enabled", config->heartbeat.enabled);
    json_object_set_number(heartbeat, "interval_minutes", config->heartbeat.interval_minutes);
    json_object_set(json, "heartbeat", heartbeat);

    // Print to string
    char* json_str = json_print(json, true);
    json_free(json);

    if (!json_str) return ERR_OUT_OF_MEMORY;

    // Write to temp file first, then rename (atomic)
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", config_path);

    FILE* file = fopen(temp_path, "w");
    if (!file) {
        free(json_str);
        return ERR_IO;
    }

    fprintf(file, "%s\n", json_str);
    fclose(file);
    free(json_str);

    // Atomic rename
    if (rename(temp_path, config_path) != 0) {
        remove(temp_path);
        return ERR_IO;
    }

    return ERR_OK;
}

// Save configuration to default path
err_t config_save_default(config_t* config) {
    if (!config) return ERR_INVALID_ARGUMENT;
    return config_save(config, config->config_path);
}

// Validate configuration
err_t config_validate(config_t* config) {
    if (!config) return ERR_INVALID_ARGUMENT;

    // Check required fields
    if (str_empty(config->default_provider)) {
        return ERR_CONFIG_INVALID;
    }

    // Validate temperature range
    if (config->default_temperature < 0.0 || config->default_temperature > 2.0) {
        return ERR_CONFIG_INVALID;
    }

    return ERR_OK;
}

// Apply environment variable overrides
void config_apply_env_overrides(config_t* config) {
    if (!config) return;

    allocator_t* alloc = config->alloc;
    if (!alloc) alloc = allocator_default();

    // ZEROCLAW_API_KEY or API_KEY
    const char* api_key = getenv("ZEROCLAW_API_KEY");
    if (!api_key) api_key = getenv("API_KEY");
    if (api_key && *api_key) {
        str_free_impl(config->api_key, alloc);
        config->api_key = str_dup_impl(STR_VIEW(api_key), alloc);
    }

    // ZEROCLAW_PROVIDER or PROVIDER
    const char* provider = getenv("ZEROCLAW_PROVIDER");
    if (!provider) provider = getenv("PROVIDER");
    if (provider && *provider) {
        str_free_impl(config->default_provider, alloc);
        config->default_provider = str_dup_impl(STR_VIEW(provider), alloc);
    }

    // ZEROCLAW_MODEL
    const char* model = getenv("ZEROCLAW_MODEL");
    if (model && *model) {
        str_free_impl(config->default_model, alloc);
        config->default_model = str_dup_impl(STR_VIEW(model), alloc);
    }

    // ZEROCLAW_WORKSPACE
    const char* workspace = getenv("ZEROCLAW_WORKSPACE");
    if (workspace && *workspace) {
        str_free_impl(config->workspace_dir, alloc);
        config->workspace_dir = str_dup_impl(STR_VIEW(workspace), alloc);
    }

    // ZEROCLAW_GATEWAY_PORT or PORT
    const char* port_str = getenv("ZEROCLAW_GATEWAY_PORT");
    if (!port_str) port_str = getenv("PORT");
    if (port_str && *port_str) {
        int port = atoi(port_str);
        if (port > 0 && port <= 65535) {
            config->gateway.port = (uint16_t)port;
        }
    }

    // ZEROCLAW_GATEWAY_HOST or HOST
    const char* host = getenv("ZEROCLAW_GATEWAY_HOST");
    if (!host) host = getenv("HOST");
    if (host && *host) {
        str_free_impl(config->gateway.host, alloc);
        config->gateway.host = str_dup_impl(STR_VIEW(host), alloc);
    }

    // ZEROCLAW_TEMPERATURE
    const char* temp_str = getenv("ZEROCLAW_TEMPERATURE");
    if (temp_str && *temp_str) {
        double temp = atof(temp_str);
        if (temp >= 0.0 && temp <= 2.0) {
            config->default_temperature = temp;
        }
    }
}

// Get workspace path for a relative path
str_t config_get_workspace_path(config_t* config, str_t relative_path) {
    (void)config;
    (void)relative_path;
    // TODO: Implement path joining
    return STR_NULL;
}

// Get configuration directory
str_t config_get_config_dir(config_t* config) {
    if (!config) return STR_NULL;
    return config->config_path;
}

// Check if a channel is enabled
bool config_is_channel_enabled(config_t* config, channel_type_t type) {
    if (!config) return false;

    switch (type) {
        case CHANNEL_TYPE_CLI:
            return config->channels.cli;
        case CHANNEL_TYPE_TELEGRAM:
            return config->channels.telegram != NULL;
        case CHANNEL_TYPE_DISCORD:
            return config->channels.discord != NULL;
        case CHANNEL_TYPE_SLACK:
            return config->channels.slack != NULL;
        case CHANNEL_TYPE_WHATSAPP:
            return config->channels.whatsapp != NULL;
        case CHANNEL_TYPE_MATRIX:
            return config->channels.matrix != NULL;
        case CHANNEL_TYPE_EMAIL:
            return config->channels.email != NULL;
        case CHANNEL_TYPE_IRC:
            return config->channels.irc != NULL;
        default:
            return false;
    }
}

// Check if a provider is available
bool config_is_provider_available(config_t* config, str_t provider_name) {
    if (!config) return false;
    if (str_empty(provider_name)) return false;

    // Check if provider name matches default
    if (str_equal(config->default_provider, provider_name)) {
        return !str_empty(config->api_key);
    }

    // Check model routes
    for (uint32_t i = 0; i < config->model_routes_count; i++) {
        if (str_equal(config->model_routes[i].provider, provider_name)) {
            return true;
        }
    }

    return false;
}

// Get API key for a provider
str_t config_get_api_key_for_provider(config_t* config, str_t provider_name) {
    if (!config) return STR_NULL;

    // Return default API key if provider matches
    if (str_equal(config->default_provider, provider_name)) {
        return config->api_key;
    }

    // Check model routes for provider-specific API key
    for (uint32_t i = 0; i < config->model_routes_count; i++) {
        if (str_equal(config->model_routes[i].provider, provider_name)) {
            if (!str_empty(config->model_routes[i].api_key)) {
                return config->model_routes[i].api_key;
            }
        }
    }

    return STR_NULL;
}

// Convert configuration to JSON (stub)
str_t config_to_json(config_t* config, allocator_t* alloc) {
    (void)config;
    (void)alloc;
    // TODO: Implement JSON serialization
    return STR_NULL;
}

// Parse configuration from JSON (stub)
err_t config_from_json(str_t json, allocator_t* alloc, config_t** out_config) {
    (void)json;
    (void)alloc;
    (void)out_config;
    // TODO: Implement JSON parsing
    return ERR_NOT_IMPLEMENTED;
}
