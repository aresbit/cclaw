// config.h - Configuration system for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_CONFIG_H
#define CCLAW_CORE_CONFIG_H

#include "types.h"
#include "error.h"

// Configuration structure (inspired by Rust original)
typedef struct config_t {
    // Paths (computed, not serialized)
    str_t workspace_dir;
    str_t config_path;

    // API configuration
    str_t api_key;
    str_t default_provider;
    str_t default_model;
    double default_temperature;

    // Memory configuration
    struct {
        str_t backend; // "sqlite", "markdown", "none"
        bool auto_save;
        bool hygiene_enabled;
        uint32_t archive_after_days;
        uint32_t purge_after_days;
        uint32_t conversation_retention_days;
        str_t embedding_provider;
        str_t embedding_model;
        uint32_t embedding_dimensions;
        double vector_weight;
        double keyword_weight;
        uint32_t embedding_cache_size;
        uint32_t chunk_max_tokens;
    } memory;

    // Gateway configuration
    struct {
        uint16_t port;
        str_t host;
        bool require_pairing;
        bool allow_public_bind;
        str_t* paired_tokens;
        uint32_t paired_tokens_count;
        uint32_t pair_rate_limit_per_minute;
        uint32_t webhook_rate_limit_per_minute;
        uint64_t idempotency_ttl_secs;
    } gateway;

    // Autonomy configuration
    struct {
        autonomy_level_t level;
        bool workspace_only;
        str_t* allowed_commands;
        uint32_t allowed_commands_count;
        str_t* forbidden_paths;
        uint32_t forbidden_paths_count;
        uint32_t max_actions_per_hour;
        uint32_t max_cost_per_day_cents;
        bool require_approval_for_medium_risk;
        bool block_high_risk_commands;
    } autonomy;

    // Runtime configuration
    struct {
        runtime_kind_t kind;
        struct {
            str_t image;
            str_t network;
            uint64_t memory_limit_mb;
            double cpu_limit;
            bool read_only_rootfs;
            bool mount_workspace;
            str_t* allowed_workspace_roots;
            uint32_t allowed_workspace_roots_count;
        } docker;
    } runtime;

    // Reliability configuration
    struct {
        uint32_t provider_retries;
        uint64_t provider_backoff_ms;
        str_t* fallback_providers;
        uint32_t fallback_providers_count;
        uint64_t channel_initial_backoff_secs;
        uint64_t channel_max_backoff_secs;
        uint64_t scheduler_poll_secs;
        uint32_t scheduler_retries;
    } reliability;

    // Model routing configuration
    struct {
        str_t hint;
        str_t provider;
        str_t model;
        str_t api_key;
    }* model_routes;
    uint32_t model_routes_count;

    // Heartbeat configuration
    struct {
        bool enabled;
        uint32_t interval_minutes;
    } heartbeat;

    // Channel configurations
    struct {
        bool cli;
        struct {
            str_t bot_token;
            str_t* allowed_users;
            uint32_t allowed_users_count;
        }* telegram;
        struct {
            str_t bot_token;
            str_t guild_id;
            str_t* allowed_users;
            uint32_t allowed_users_count;
        }* discord;
        struct {
            str_t bot_token;
            str_t app_token;
            str_t channel_id;
            str_t* allowed_users;
            uint32_t allowed_users_count;
        }* slack;
        struct {
            uint16_t port;
            str_t secret;
        }* webhook;
        struct {
            str_t* allowed_contacts;
            uint32_t allowed_contacts_count;
        }* imessage;
        struct {
            str_t homeserver;
            str_t access_token;
            str_t room_id;
            str_t* allowed_users;
            uint32_t allowed_users_count;
        }* matrix;
        struct {
            str_t access_token;
            str_t phone_number_id;
            str_t verify_token;
            str_t app_secret;
            str_t* allowed_numbers;
            uint32_t allowed_numbers_count;
        }* whatsapp;
        struct {
            str_t access_token;
        }* email;
        struct {
            str_t server;
            uint16_t port;
            str_t nickname;
            str_t username;
            str_t* channels;
            uint32_t channels_count;
            str_t* allowed_users;
            uint32_t allowed_users_count;
            str_t server_password;
            str_t nickserv_password;
            str_t sasl_password;
            bool verify_tls;
        }* irc;
    } channels;

    // Tunnel configuration
    struct {
        str_t provider; // "none", "cloudflare", "tailscale", "ngrok", "custom"
        struct {
            str_t token;
        }* cloudflare;
        struct {
            bool funnel;
            str_t hostname;
        }* tailscale;
        struct {
            str_t auth_token;
            str_t domain;
        }* ngrok;
        struct {
            str_t start_command;
            str_t health_url;
            str_t url_pattern;
        }* custom;
    } tunnel;

    // Browser configuration
    struct {
        bool enabled;
        str_t* allowed_domains;
        uint32_t allowed_domains_count;
        str_t session_name;
    } browser;

    // Composio configuration
    struct {
        bool enabled;
        str_t api_key;
        str_t entity_id;
    } composio;

    // Secrets configuration
    struct {
        bool encrypt;
    } secrets;

    // Identity configuration
    struct {
        str_t format; // "openclaw" or "aieos"
        str_t aieos_path;
        str_t aieos_inline;
    } identity;

    // Observability configuration
    struct {
        str_t backend; // "none", "log", "prometheus", "otel"
        str_t otel_endpoint;
        str_t otel_service_name;
    } observability;

    // Allocator for dynamic data
    allocator_t* alloc;
} config_t;

// Configuration API
config_t* config_create(allocator_t* alloc);
void config_destroy(config_t* config);

err_t config_load(str_t path, config_t** out_config);
err_t config_save(config_t* config, str_t path);
err_t config_save_default(config_t* config);

config_t* config_default(allocator_t* alloc);
err_t config_validate(config_t* config);

// Environment variable overrides
void config_apply_env_overrides(config_t* config);

// Path resolution
str_t config_get_workspace_path(config_t* config, str_t relative_path);
str_t config_get_config_dir(config_t* config);

// Utility functions
bool config_is_channel_enabled(config_t* config, channel_type_t type);
bool config_is_provider_available(config_t* config, str_t provider_name);
str_t config_get_api_key_for_provider(config_t* config, str_t provider_name);

// JSON serialization
str_t config_to_json(config_t* config, allocator_t* alloc);
err_t config_from_json(str_t json, allocator_t* alloc, config_t** out_config);

// Forward declaration for internal JSON parser
struct json_value_t;
typedef struct json_value_t json_value_t;
err_t config_from_json_str(json_value_t* json, allocator_t* alloc, config_t** out_config);

#endif // CCLAW_CORE_CONFIG_H