// base.h - AI Provider base interface for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_BASE_H
#define CCLAW_PROVIDERS_BASE_H

#include "core/types.h"
#include "core/error.h"
#include "utils/http.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct provider_t provider_t;
typedef struct provider_vtable_t provider_vtable_t;
typedef struct chat_response_t chat_response_t;
typedef struct provider_config_t provider_config_t;

// chat_role_t and chat_message_t are defined in core/types.h

// Tool definition for function calling
typedef struct tool_def_t {
    str_t name;
    str_t description;
    str_t parameters;      // JSON schema
} tool_def_t;

// Chat response structure
typedef struct chat_response_t {
    str_t content;
    str_t finish_reason;   // "stop", "length", "tool_calls", etc.
    str_t model;
    uint32_t prompt_tokens;
    uint32_t completion_tokens;
    uint32_t total_tokens;
    str_t tool_calls;      // JSON array if tools were called
} chat_response_t;

// Provider configuration
typedef struct provider_config_t {
    str_t name;                    // Provider name (e.g., "openrouter", "deepseek")
    str_t api_key;
    str_t base_url;                // API base URL
    str_t default_model;
    double default_temperature;
    uint32_t max_tokens;
    uint32_t timeout_ms;
    bool stream;                   // Enable streaming responses
    // Retry configuration
    uint32_t max_retries;
    uint32_t retry_delay_ms;
} provider_config_t;

// Provider VTable - defines the interface
struct provider_vtable_t {
    // Provider identification
    str_t (*get_name)(void);
    str_t (*get_version)(void);

    // Lifecycle
    err_t (*create)(const provider_config_t* config, provider_t** out_provider);
    void (*destroy)(provider_t* provider);

    // Connection
    err_t (*connect)(provider_t* provider);
    void (*disconnect)(provider_t* provider);
    bool (*is_connected)(provider_t* provider);

    // Chat completion
    err_t (*chat)(provider_t* provider,
                  const chat_message_t* messages,
                  uint32_t message_count,
                  const tool_def_t* tools,
                  uint32_t tool_count,
                  const char* model,
                  double temperature,
                  chat_response_t** out_response);

    // Stream chat (callback-based)
    err_t (*chat_stream)(provider_t* provider,
                         const chat_message_t* messages,
                         uint32_t message_count,
                         const char* model,
                         double temperature,
                         void (*on_chunk)(const char* chunk, void* user_data),
                         void* user_data);

    // Model management
    err_t (*list_models)(provider_t* provider, str_t** out_models, uint32_t* out_count);
    bool (*supports_model)(provider_t* provider, const char* model);

    // Health check
    err_t (*health_check)(provider_t* provider, bool* out_healthy);

    // Get available models (static)
    const char** (*get_available_models)(uint32_t* out_count);
};

// Provider instance structure
struct provider_t {
    const provider_vtable_t* vtable;
    provider_config_t config;
    http_client_t* http;
    void* impl_data;           // Provider-specific data
    bool connected;
};

// Helper macros for provider implementation
#define PROVIDER_IMPLEMENT(name, vtable_ptr) \
    const provider_vtable_t* name##_get_vtable(void) { return vtable_ptr; }

// Global provider registry
err_t provider_registry_init(void);
void provider_registry_shutdown(void);

// Register a provider type
err_t provider_register(const char* name, const provider_vtable_t* vtable);

// Create provider by name
err_t provider_create(const char* name, const provider_config_t* config, provider_t** out_provider);

// Get registered provider names
err_t provider_registry_list(const char*** out_names, uint32_t* out_count);

// Built-in providers
const provider_vtable_t* openrouter_get_vtable(void);
const provider_vtable_t* deepseek_get_vtable(void);
const provider_vtable_t* kimi_get_vtable(void);
const provider_vtable_t* openai_get_vtable(void);
const provider_vtable_t* anthropic_get_vtable(void);

// Provider creation helpers
provider_t* provider_alloc(const provider_vtable_t* vtable);
void provider_free(provider_t* provider);

// Retry and failover helpers
err_t provider_chat_with_retry(provider_t* provider,
                               const chat_message_t* messages,
                               uint32_t message_count,
                               const tool_def_t* tools,
                               uint32_t tool_count,
                               const char* model,
                               double temperature,
                               uint32_t max_retries,
                               uint64_t retry_delay_ms,
                               chat_response_t** out_response);

// Response helpers
chat_response_t* chat_response_create(void);
void chat_response_free(chat_response_t* response);
void chat_response_clear(chat_response_t* response);

// Message helpers
chat_message_t* chat_message_create(chat_role_t role, const char* content);
void chat_message_free(chat_message_t* message);
void chat_message_array_free(chat_message_t* messages, uint32_t count);

// Tool helpers
tool_def_t* tool_def_create(const char* name, const char* description, const char* parameters);
void tool_def_free(tool_def_t* tool);
void tool_def_array_free(tool_def_t* tools, uint32_t count);

// Parse chat response from JSON (common helper)
err_t provider_parse_chat_response(const char* json_str, chat_response_t* out_response);

// Build chat request JSON (common helper)
char* provider_build_chat_request(const provider_t* provider,
                                   const chat_message_t* messages,
                                   uint32_t message_count,
                                   const tool_def_t* tools,
                                   uint32_t tool_count,
                                   const char* model,
                                   double temperature,
                                   bool stream);

// Default model names
#define DEFAULT_OPENROUTER_MODEL "anthropic/claude-3.5-sonnet"
#define DEFAULT_DEEPSEEK_MODEL "deepseek-chat"
#define DEFAULT_KIMI_MODEL "moonshot-k2.5"
#define DEFAULT_OPENAI_MODEL "gpt-4o"
#define DEFAULT_ANTHROPIC_MODEL "claude-3-5-sonnet-20241022"

// Provider-specific base URLs
#define OPENROUTER_BASE_URL "https://openrouter.ai/api/v1"
#define DEEPSEEK_BASE_URL "https://api.deepseek.com/v1"
#define KIMI_BASE_URL "https://api.moonshot.cn/v1"
#define OPENAI_BASE_URL "https://api.openai.com/v1"
#define ANTHROPIC_BASE_URL "https://api.anthropic.com/v1"

#endif // CCLAW_PROVIDERS_BASE_H
