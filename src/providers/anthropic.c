// anthropic.c - Anthropic Provider implementation
// SPDX-License-Identifier: MIT

#include "providers/anthropic.h"
#include "providers/base.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Anthropic provider instance data
typedef struct anthropic_data_t {
    str_t version;               // API version
    str_t beta;                  // Beta features
    uint32_t max_tokens;         // Max tokens to generate
} anthropic_data_t;

// Forward declarations for vtable
static str_t anthropic_get_name(void);
static str_t anthropic_get_version(void);
err_t anthropic_create(const provider_config_t* config, provider_t** out_provider);
void anthropic_destroy(provider_t* provider);
static err_t anthropic_connect(provider_t* provider);
static void anthropic_disconnect(provider_t* provider);
static bool anthropic_is_connected(provider_t* provider);
static err_t anthropic_chat(provider_t* provider,
                           const chat_message_t* messages,
                           uint32_t message_count,
                           const tool_def_t* tools,
                           uint32_t tool_count,
                           const char* model,
                           double temperature,
                           chat_response_t** out_response);
static err_t anthropic_chat_stream(provider_t* provider,
                                  const chat_message_t* messages,
                                  uint32_t message_count,
                                  const char* model,
                                  double temperature,
                                  void (*on_chunk)(const char* chunk, void* user_data),
                                  void* user_data);
static err_t anthropic_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count);
static bool anthropic_supports_model(provider_t* provider, const char* model);
static err_t anthropic_health_check(provider_t* provider, bool* out_healthy);
static const char** anthropic_get_available_models(uint32_t* out_count);

// VTable definition
static const provider_vtable_t anthropic_vtable = {
    .get_name = anthropic_get_name,
    .get_version = anthropic_get_version,
    .create = anthropic_create,
    .destroy = anthropic_destroy,
    .connect = anthropic_connect,
    .disconnect = anthropic_disconnect,
    .is_connected = anthropic_is_connected,
    .chat = anthropic_chat,
    .chat_stream = NULL, // TODO: Implement streaming
    .list_models = anthropic_list_models,
    .supports_model = anthropic_supports_model,
    .health_check = anthropic_health_check,
    .get_available_models = anthropic_get_available_models
};

// Get vtable
const provider_vtable_t* anthropic_get_vtable(void) {
    return &anthropic_vtable;
}

static str_t anthropic_get_name(void) {
    return STR_LIT("anthropic");
}

static str_t anthropic_get_version(void) {
    return STR_LIT("1.0.0");
}

err_t anthropic_create(const provider_config_t* config, provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = calloc(1, sizeof(provider_t));
    if (!provider) return ERR_OUT_OF_MEMORY;

    provider->vtable = &anthropic_vtable;
    provider->config = *config;

    // Set default base URL if not provided
    if (str_empty(config->base_url)) {
        provider->config.base_url = (str_t){ .data = ANTHROPIC_BASE_URL, .len = strlen(ANTHROPIC_BASE_URL) };
    }

    // Create HTTP client
    http_client_config_t http_config = http_client_default_config();
    http_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 60000;
    provider->http = http_client_create(&http_config);
    if (!provider->http) {
        free(provider);
        return ERR_NETWORK;
    }

    // Add Anthropic-specific headers
    if (!str_empty(config->api_key)) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "x-api-key: %.*s", (int)config->api_key.len, config->api_key.data);
        http_client_add_header(provider->http, "Authorization", auth_header);
    }

    // Add content type and version headers
    http_client_add_header(provider->http, "Content-Type", "application/json");
    http_client_add_header(provider->http, "anthropic-version", "2023-06-01"); // Default version

    // Add beta header if specified in config
    anthropic_data_t* data = calloc(1, sizeof(anthropic_data_t));
    if (data) {
        // Set default values
        data->version = (str_t){ .data = strdup("2023-06-01"), .len = strlen("2023-06-01") };
        data->max_tokens = 1024; // Default max tokens

        provider->impl_data = data;
    }

    *out_provider = provider;
    return ERR_OK;
}

void anthropic_destroy(provider_t* provider) {
    if (!provider) return;

    if (provider->http) {
        http_client_destroy(provider->http);
    }

    if (provider->impl_data) {
        anthropic_data_t* data = (anthropic_data_t*)provider->impl_data;
        free((void*)data->version.data);
        free((void*)data->beta.data);
        free(data);
    }

    free(provider);
}

static err_t anthropic_connect(provider_t* provider) {
    if (!provider) return ERR_INVALID_ARGUMENT;

    // Anthropic doesn't have a simple models endpoint like OpenAI
    // We'll try to make a lightweight request to check connectivity
    http_response_t* response = NULL;
    char url[512];
    snprintf(url, sizeof(url), "%s/messages", ANTHROPIC_BASE_URL);

    // Create minimal request
    const char* test_body = "{\"model\":\"claude-3-haiku-20240307\",\"max_tokens\":1,\"messages\":[{\"role\":\"user\",\"content\":\"test\"}]}";

    err_t err = http_post_json(provider->http, url, test_body, &response);
    if (err == ERR_OK && response) {
        // Even if it's an auth error (401), it means the endpoint is reachable
        provider->connected = (response->status_code == 401 || http_response_is_success(response));
        http_response_free(response);
        return provider->connected ? ERR_OK : ERR_NETWORK;
    }

    provider->connected = false;
    return ERR_NETWORK;
}

static void anthropic_disconnect(provider_t* provider) {
    if (provider) {
        provider->connected = false;
    }
}

static bool anthropic_is_connected(provider_t* provider) {
    return provider && provider->connected;
}

static char* build_anthropic_request(const provider_t* provider,
                                     const chat_message_t* messages,
                                     uint32_t message_count,
                                     const tool_def_t* tools,
                                     uint32_t tool_count,
                                     const char* model,
                                     double temperature) {
    json_value_t* root = json_create_object();
    if (!root) return NULL;

    const char* model_name = model ? model : DEFAULT_ANTHROPIC_MODEL;
    json_object_set_string(root, "model", model_name);

    // Build messages array for Anthropic
    json_value_t* messages_arr = json_create_array();
    for (uint32_t i = 0; i < message_count; i++) {
        json_value_t* msg_obj = json_create_object();

        // Convert role to Anthropic format
        const char* role_str = "user";
        switch (messages[i].role) {
            case CHAT_ROLE_SYSTEM:
                // Anthropic handles system messages differently
                // We'll set it as the first user message with system content
                role_str = "user";
                break;
            case CHAT_ROLE_USER: role_str = "user"; break;
            case CHAT_ROLE_ASSISTANT: role_str = "assistant"; break;
            case CHAT_ROLE_TOOL: role_str = "user"; break; // Anthropic doesn't have tool role
        }
        json_object_set_string(msg_obj, "role", role_str);

        // Build content array (Anthropic uses array format)
        json_value_t* content_arr = json_create_array();
        json_value_t* text_block = json_create_object();
        json_object_set_string(text_block, "type", "text");

        if (messages[i].content.len > 0) {
            // Create null-terminated copy
            char* text_copy = malloc(messages[i].content.len + 1);
            if (text_copy) {
                memcpy(text_copy, messages[i].content.data, messages[i].content.len);
                text_copy[messages[i].content.len] = '\0';
                json_object_set_string(text_block, "text", text_copy);
                free(text_copy);
            } else {
                json_object_set_string(text_block, "text", "");
            }
        } else {
            json_object_set_string(text_block, "text", "");
        }

        json_array_append(content_arr, text_block);
        json_object_set(msg_obj, "content", content_arr);

        json_array_append(messages_arr, msg_obj);
    }
    json_object_set(root, "messages", messages_arr);

    // Set max_tokens (required for Anthropic)
    if (provider->impl_data) {
        anthropic_data_t* data = (anthropic_data_t*)provider->impl_data;
        json_object_set_number(root, "max_tokens", (double)data->max_tokens);
    } else {
        json_object_set_number(root, "max_tokens", 1024.0); // Default
    }

    // Set temperature if in valid range
    if (temperature >= 0.0 && temperature <= 1.0) {
        json_object_set_number(root, "temperature", temperature);
    }

    // TODO: Add tools support for Anthropic
    (void)tools;
    (void)tool_count;

    // Add system prompt if first message is system
    if (message_count > 0 && messages[0].role == CHAT_ROLE_SYSTEM) {
        // Create null-terminated copy
        char* system_copy = malloc(messages[0].content.len + 1);
        if (system_copy) {
            memcpy(system_copy, messages[0].content.data, messages[0].content.len);
            system_copy[messages[0].content.len] = '\0';
            json_object_set_string(root, "system", system_copy);
            free(system_copy);
        }
        // Remove the system message from messages array for Anthropic
        // This is handled above by converting system to user role
    }

    char* json_str = json_print(root, false);
    json_free(root);
    return json_str;
}

static err_t parse_anthropic_response(const char* json_str, chat_response_t* response) {
    json_value_t* root = json_parse(json_str);
    if (!root) return ERR_CONFIG_PARSE;

    json_object_t* obj = json_as_object(root);
    if (!obj) {
        json_free(root);
        return ERR_CONFIG_PARSE;
    }

    // Parse content from Anthropic response
    json_array_t* content = json_object_get_array(obj, "content");
    if (content && json_array_length(content) > 0) {
        // Concatenate all text blocks
        size_t total_len = 0;

        // First pass: calculate total length
        for (uint32_t i = 0; i < json_array_length(content); i++) {
            json_value_t* block = json_array_get(content, i);
            json_object_t* block_obj = json_as_object(block);
            if (block_obj) {
                const char* type = json_object_get_string(block_obj, "type", "");
                if (strcmp(type, "text") == 0) {
                    const char* text = json_object_get_string(block_obj, "text", "");
                    if (text) total_len += strlen(text);
                }
            }
        }

        // Allocate and concatenate
        if (total_len > 0) {
            char* combined = malloc(total_len + 1);
            if (combined) {
                combined[0] = '\0';
                size_t offset = 0;

                for (uint32_t i = 0; i < json_array_length(content); i++) {
                    json_value_t* block = json_array_get(content, i);
                    json_object_t* block_obj = json_as_object(block);
                    if (block_obj) {
                        const char* type = json_object_get_string(block_obj, "type", "");
                        if (strcmp(type, "text") == 0) {
                            const char* text = json_object_get_string(block_obj, "text", "");
                            if (text) {
                                size_t len = strlen(text);
                                memcpy(combined + offset, text, len);
                                offset += len;
                            }
                        }
                    }
                }
                combined[offset] = '\0';

                response->content = (str_t){ .data = combined, .len = (uint32_t)offset };
            }
        }
    }

    // Parse model
    const char* model = json_object_get_string(obj, "model", DEFAULT_ANTHROPIC_MODEL);
    response->model = (str_t){ .data = strdup(model), .len = strlen(model) };

    // Parse stop reason
    const char* stop_reason = json_object_get_string(obj, "stop_reason", "end_turn");
    response->finish_reason = (str_t){ .data = strdup(stop_reason), .len = strlen(stop_reason) };

    // Parse usage
    json_object_t* usage = json_object_get_object(obj, "usage");
    if (usage) {
        uint32_t input_tokens = (uint32_t)json_object_get_number(usage, "input_tokens", 0);
        uint32_t output_tokens = (uint32_t)json_object_get_number(usage, "output_tokens", 0);
        // Map to standard fields
        response->prompt_tokens = input_tokens;
        response->completion_tokens = output_tokens;
        response->total_tokens = input_tokens + output_tokens;
    }

    json_free(root);
    return ERR_OK;
}

static err_t anthropic_chat(provider_t* provider,
                           const chat_message_t* messages,
                           uint32_t message_count,
                           const tool_def_t* tools,
                           uint32_t tool_count,
                           const char* model,
                           double temperature,
                           chat_response_t** out_response) {
    if (!provider || !provider->http || !out_response) return ERR_INVALID_ARGUMENT;

    // Build request JSON
    char* request_body = build_anthropic_request(provider, messages, message_count, tools, tool_count, model, temperature);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/messages", ANTHROPIC_BASE_URL);

    // Send request
    http_response_t* http_resp = NULL;
    err_t err = http_post_json(provider->http, url, request_body, &http_resp);
    free(request_body);

    if (err != ERR_OK) return err;
    if (!http_response_is_success(http_resp)) {
        http_response_free(http_resp);
        return ERR_PROVIDER;
    }

    // Parse response
    chat_response_t* response = calloc(1, sizeof(chat_response_t));
    if (!response) {
        http_response_free(http_resp);
        return ERR_OUT_OF_MEMORY;
    }

    err = parse_anthropic_response(http_resp->body.data, response);
    http_response_free(http_resp);

    if (err != ERR_OK) {
        chat_response_free(response);
        return err;
    }

    *out_response = response;
    return ERR_OK;
}

static err_t anthropic_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count) {
    (void)provider;
    if (!out_models || !out_count) return ERR_INVALID_ARGUMENT;

    uint32_t count = 0;
    while (ANTHROPIC_MODELS[count]) count++;

    str_t* models = calloc(count, sizeof(str_t));
    if (!models) return ERR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < count; i++) {
        models[i] = (str_t){ .data = strdup(ANTHROPIC_MODELS[i]), .len = strlen(ANTHROPIC_MODELS[i]) };
    }

    *out_models = models;
    *out_count = count;
    return ERR_OK;
}

static bool anthropic_supports_model(provider_t* provider, const char* model) {
    (void)provider;
    if (!model) return false;

    for (uint32_t i = 0; ANTHROPIC_MODELS[i]; i++) {
        if (strcmp(ANTHROPIC_MODELS[i], model) == 0) return true;
    }

    // Check if it's a Claude model
    if (strncmp(model, "claude-", 7) == 0) return true;

    return false;
}

static err_t anthropic_health_check(provider_t* provider, bool* out_healthy) {
    if (!provider || !out_healthy) return ERR_INVALID_ARGUMENT;

    // Try to get models list (Anthropic doesn't have a models endpoint)
    // We'll try a lightweight request instead
    char url[512];
    snprintf(url, sizeof(url), "%s/messages", ANTHROPIC_BASE_URL);

    const char* test_body = "{\"model\":\"claude-3-haiku-20240307\",\"max_tokens\":1,\"messages\":[{\"role\":\"user\",\"content\":\"test\"}]}";

    http_response_t* response = NULL;
    err_t err = http_post_json(provider->http, url, test_body, &response);

    if (err == ERR_OK && response) {
        // 401 means valid endpoint but bad auth, which is still "healthy"
        *out_healthy = (response->status_code == 401 || http_response_is_success(response));
        http_response_free(response);
        return ERR_OK;
    }

    *out_healthy = false;
    return ERR_OK;
}

static const char** anthropic_get_available_models(uint32_t* out_count) {
    if (out_count) {
        uint32_t count = 0;
        while (ANTHROPIC_MODELS[count]) count++;
        *out_count = count;
    }
    return (const char**)ANTHROPIC_MODELS;
}

// Anthropic-specific functions
err_t anthropic_set_version(provider_t* provider, const char* version) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    anthropic_data_t* data = (anthropic_data_t*)provider->impl_data;
    free((void*)data->version.data);

    if (version) {
        data->version = (str_t){ .data = strdup(version), .len = strlen(version) };
        // Update HTTP header
        http_client_add_header(provider->http, "anthropic-version", version);
    } else {
        data->version = (str_t){0};
        http_client_add_header(provider->http, "anthropic-version", "2023-06-01");
    }

    return ERR_OK;
}

err_t anthropic_set_beta(provider_t* provider, const char* beta) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    anthropic_data_t* data = (anthropic_data_t*)provider->impl_data;
    free((void*)data->beta.data);

    if (beta) {
        data->beta = (str_t){ .data = strdup(beta), .len = strlen(beta) };
        // Update HTTP header
        http_client_add_header(provider->http, "anthropic-beta", beta);
    } else {
        data->beta = (str_t){0};
        // Remove beta header
        // TODO: Implement header removal in HTTP client
    }

    return ERR_OK;
}

err_t anthropic_set_max_tokens(provider_t* provider, uint32_t max_tokens) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    anthropic_data_t* data = (anthropic_data_t*)provider->impl_data;
    data->max_tokens = max_tokens;

    return ERR_OK;
}