// deepseek.c - DeepSeek AI Provider implementation
// SPDX-License-Identifier: MIT

#include "providers/deepseek.h"
#include "providers/base.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// DeepSeek provider instance data
typedef struct deepseek_data_t {
    bool enable_search;
    str_t context_length;
} deepseek_data_t;

// Forward declarations for vtable (defined as public API in header)
static str_t deepseek_get_name(void);
static str_t deepseek_get_version(void);
err_t deepseek_create(const provider_config_t* config, provider_t** out_provider);
void deepseek_destroy(provider_t* provider);
static err_t deepseek_connect(provider_t* provider);
static void deepseek_disconnect(provider_t* provider);
static bool deepseek_is_connected(provider_t* provider);
static err_t deepseek_chat(provider_t* provider,
                           const chat_message_t* messages,
                           uint32_t message_count,
                           const tool_def_t* tools,
                           uint32_t tool_count,
                           const char* model,
                           double temperature,
                           chat_response_t** out_response);
static err_t deepseek_chat_stream(provider_t* provider,
                                  const chat_message_t* messages,
                                  uint32_t message_count,
                                  const char* model,
                                  double temperature,
                                  void (*on_chunk)(const char* chunk, void* user_data),
                                  void* user_data);
static err_t deepseek_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count);
static bool deepseek_supports_model(provider_t* provider, const char* model);
static err_t deepseek_health_check(provider_t* provider, bool* out_healthy);
static const char** deepseek_get_available_models(uint32_t* out_count);

// VTable definition
static const provider_vtable_t deepseek_vtable = {
    .get_name = deepseek_get_name,
    .get_version = deepseek_get_version,
    .create = deepseek_create,
    .destroy = deepseek_destroy,
    .connect = deepseek_connect,
    .disconnect = deepseek_disconnect,
    .is_connected = deepseek_is_connected,
    .chat = deepseek_chat,
    .chat_stream = deepseek_chat_stream,
    .list_models = deepseek_list_models,
    .supports_model = deepseek_supports_model,
    .health_check = deepseek_health_check,
    .get_available_models = deepseek_get_available_models
};

// Get vtable
const provider_vtable_t* deepseek_get_vtable(void) {
    return &deepseek_vtable;
}

static str_t deepseek_get_name(void) {
    return STR_LIT("deepseek");
}

static str_t deepseek_get_version(void) {
    return STR_LIT("1.0.0");
}

err_t deepseek_create(const provider_config_t* config, provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = calloc(1, sizeof(provider_t));
    if (!provider) return ERR_OUT_OF_MEMORY;

    provider->vtable = &deepseek_vtable;
    provider->config = *config;

    // Set default base URL
    if (str_empty(config->base_url)) {
        provider->config.base_url = (str_t){ .data = DEEPSEEK_BASE_URL, .len = strlen(DEEPSEEK_BASE_URL) };
    }

    // Create HTTP client
    http_client_config_t http_config = http_client_default_config();
    http_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 60000;
    provider->http = http_client_create(&http_config);
    if (!provider->http) {
        free(provider);
        return ERR_NETWORK;
    }

    // Add authorization header
    if (!str_empty(config->api_key)) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Bearer %.*s", (int)config->api_key.len, config->api_key.data);
        http_client_add_header(provider->http, "Authorization", auth_header);
    }

    // Add content type
    http_client_add_header(provider->http, "Content-Type", "application/json");

    // Initialize provider-specific data
    deepseek_data_t* data = calloc(1, sizeof(deepseek_data_t));
    if (data) {
        data->enable_search = false;
        data->context_length = STR_LIT("8k");
        provider->impl_data = data;
    }

    *out_provider = provider;
    return ERR_OK;
}

void deepseek_destroy(provider_t* provider) {
    if (!provider) return;

    if (provider->http) {
        http_client_destroy(provider->http);
    }

    if (provider->impl_data) {
        deepseek_data_t* data = (deepseek_data_t*)provider->impl_data;
        free((void*)data->context_length.data);
        free(data);
    }

    free(provider);
}

static err_t deepseek_connect(provider_t* provider) {
    if (!provider) return ERR_INVALID_ARGUMENT;

    // Test connection with a simple request
    http_response_t* response = NULL;
    err_t err = http_get(provider->http, "https://api.deepseek.com/models", &response);

    if (err == ERR_OK && response) {
        provider->connected = http_response_is_success(response);
        http_response_free(response);
        return provider->connected ? ERR_OK : ERR_NETWORK;
    }

    provider->connected = false;
    return ERR_NETWORK;
}

static void deepseek_disconnect(provider_t* provider) {
    if (provider) {
        provider->connected = false;
    }
}

static bool deepseek_is_connected(provider_t* provider) {
    return provider && provider->connected;
}

// Build DeepSeek chat request JSON
static char* build_deepseek_request(const provider_t* provider,
                                    const chat_message_t* messages,
                                    uint32_t message_count,
                                    const char* model,
                                    double temperature,
                                    bool stream) {
    json_value_t* root = json_create_object();
    if (!root) return NULL;

    // Model
    const char* model_name = model ? model : DEFAULT_DEEPSEEK_MODEL;
    json_object_set_string(root, "model", model_name);

    // Messages array
    json_value_t* messages_arr = json_create_array();
    for (uint32_t i = 0; i < message_count; i++) {
        json_value_t* msg_obj = json_create_object();

        const char* role_str = "user";
        switch (messages[i].role) {
            case CHAT_ROLE_SYSTEM: role_str = "system"; break;
            case CHAT_ROLE_USER: role_str = "user"; break;
            case CHAT_ROLE_ASSISTANT: role_str = "assistant"; break;
            case CHAT_ROLE_TOOL: role_str = "tool"; break;
        }

        json_object_set_string(msg_obj, "role", role_str);
        json_object_set_string(msg_obj, "content", messages[i].content.data);
        json_array_append(messages_arr, msg_obj);
    }
    json_object_set(root, "messages", messages_arr);

    // Temperature
    json_object_set_number(root, "temperature", temperature);

    // Stream
    json_object_set_bool(root, "stream", stream);

    // Enable search if configured
    deepseek_data_t* data = (deepseek_data_t*)provider->impl_data;
    if (data && data->enable_search) {
        json_value_t* search_options = json_create_object();
        json_object_set_bool(search_options, "enabled", true);
        json_object_set(root, "search_options", search_options);
    }

    char* json_str = json_print(root, false);
    json_free(root);

    return json_str;
}

// Parse DeepSeek chat response
static err_t parse_deepseek_response(const char* json_str, chat_response_t* response) {
    json_value_t* root = json_parse(json_str);
    if (!root) return ERR_CONFIG_PARSE;

    json_object_t* obj = json_as_object(root);
    if (!obj) {
        json_free(root);
        return ERR_CONFIG_PARSE;
    }

    // Get choices array
    json_array_t* choices = json_object_get_array(obj, "choices");
    if (choices && json_array_length(choices) > 0) {
        json_value_t* first_choice = json_array_get(choices, 0);
        json_object_t* choice_obj = json_as_object(first_choice);

        if (choice_obj) {
            // Get message
            json_object_t* message = json_object_get_object(choice_obj, "message");
            if (message) {
                const char* content = json_object_get_string(message, "content", "");
                response->content = (str_t){ .data = strdup(content), .len = strlen(content) };
            }

            // Get finish reason
            const char* finish_reason = json_object_get_string(choice_obj, "finish_reason", "stop");
            response->finish_reason = (str_t){ .data = strdup(finish_reason), .len = strlen(finish_reason) };
        }
    }

    // Get usage
    json_object_t* usage = json_object_get_object(obj, "usage");
    if (usage) {
        response->prompt_tokens = (uint32_t)json_object_get_number(usage, "prompt_tokens", 0);
        response->completion_tokens = (uint32_t)json_object_get_number(usage, "completion_tokens", 0);
        response->total_tokens = (uint32_t)json_object_get_number(usage, "total_tokens", 0);
    }

    // Get model
    const char* model = json_object_get_string(obj, "model", DEFAULT_DEEPSEEK_MODEL);
    response->model = (str_t){ .data = strdup(model), .len = strlen(model) };

    json_free(root);
    return ERR_OK;
}

static err_t deepseek_chat(provider_t* provider,
                           const chat_message_t* messages,
                           uint32_t message_count,
                           const tool_def_t* tools,
                           uint32_t tool_count,
                           const char* model,
                           double temperature,
                           chat_response_t** out_response) {
    (void)tools;
    (void)tool_count;

    if (!provider || !provider->http || !out_response) return ERR_INVALID_ARGUMENT;

    // Build request
    char* request_body = build_deepseek_request(provider, messages, message_count, model, temperature, false);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    // Make request
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", DEEPSEEK_BASE_URL);

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

    err = parse_deepseek_response(http_resp->body.data, response);
    http_response_free(http_resp);

    if (err != ERR_OK) {
        chat_response_free(response);
        return err;
    }

    *out_response = response;
    return ERR_OK;
}

// SSE parser context
typedef struct {
    void (*on_chunk)(const char* chunk, void* user_data);
    void* user_data;
    char* buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    size_t partial_line_len;
    char partial_line[8192];  // Buffer for partial SSE lines
} sse_parser_t;

static size_t sse_parser_write(const char* data, size_t len, void* userp) {
    sse_parser_t* parser = (sse_parser_t*)userp;
    size_t consumed = 0;

    while (consumed < len) {
        // Find next newline
        const char* newline = (const char*)memchr(data + consumed, '\n', len - consumed);
        size_t chunk_end = newline ? (size_t)(newline - data) : len;

        // Add to partial line buffer
        size_t line_len = chunk_end - consumed;
        if (parser->partial_line_len + line_len < sizeof(parser->partial_line)) {
            memcpy(parser->partial_line + parser->partial_line_len, data + consumed, line_len);
            parser->partial_line_len += line_len;
        }

        consumed = chunk_end;

        // If we found a newline, process the line
        if (newline) {
            parser->partial_line[parser->partial_line_len] = '\0';

            // Check if it's an SSE event line
            if (parser->partial_line_len >= 6 && strncmp(parser->partial_line, "data: ", 6) == 0) {
                // Skip "data: " prefix
                const char* event_data = parser->partial_line + 6;

                // Check for "[DONE]" event
                if (strcmp(event_data, "[DONE]") == 0) {
                    // End of stream
                    parser->partial_line_len = 0;
                    consumed++;  // Skip the newline
                    continue;
                }

                // Parse JSON from event data
                json_value_t* root = json_parse(event_data);
                if (root) {
                    json_object_t* obj = json_as_object(root);
                    if (obj) {
                        json_array_t* choices = json_object_get_array(obj, "choices");
                        if (choices && json_array_length(choices) > 0) {
                            json_value_t* first = json_array_get(choices, 0);
                            json_object_t* choice_obj = json_as_object(first);
                            if (choice_obj) {
                                json_object_t* delta = json_object_get_object(choice_obj, "delta");
                                if (delta) {
                                    const char* content = json_object_get_string(delta, "content", "");
                                    if (content && parser->on_chunk) {
                                        parser->on_chunk(content, parser->user_data);
                                    }
                                }
                            }
                        }
                    }
                    json_free(root);
                }
            }

            // Reset for next line
            parser->partial_line_len = 0;
            consumed++;  // Skip the newline
        }
    }

    return len;  // Consume all data
}

static err_t deepseek_chat_stream(provider_t* provider,
                                  const chat_message_t* messages,
                                  uint32_t message_count,
                                  const char* model,
                                  double temperature,
                                  void (*on_chunk)(const char* chunk, void* user_data),
                                  void* user_data) {
    if (!provider || !provider->http || !on_chunk) return ERR_INVALID_ARGUMENT;

    // Build streaming request
    char* request_body = build_deepseek_request(provider, messages, message_count, model, temperature, true);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", DEEPSEEK_BASE_URL);

    // Initialize SSE parser
    sse_parser_t parser = {
        .on_chunk = on_chunk,
        .user_data = user_data,
        .buffer = NULL,
        .buffer_size = 0,
        .buffer_capacity = 0,
        .partial_line_len = 0
    };

    // Make streaming request
    err_t err = http_post_json_stream(provider->http, url, request_body, sse_parser_write, &parser);
    free(request_body);

    return err;
}

static err_t deepseek_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count) {
    (void)provider;

    if (!out_models || !out_count) return ERR_INVALID_ARGUMENT;

    uint32_t count = 0;
    while (DEEPSEEK_MODELS[count]) count++;

    str_t* models = calloc(count, sizeof(str_t));
    if (!models) return ERR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < count; i++) {
        models[i] = (str_t){ .data = strdup(DEEPSEEK_MODELS[i]), .len = strlen(DEEPSEEK_MODELS[i]) };
    }

    *out_models = models;
    *out_count = count;
    return ERR_OK;
}

static bool deepseek_supports_model(provider_t* provider, const char* model) {
    (void)provider;
    if (!model) return false;

    for (uint32_t i = 0; DEEPSEEK_MODELS[i]; i++) {
        if (strcmp(DEEPSEEK_MODELS[i], model) == 0) {
            return true;
        }
    }
    return false;
}

static err_t deepseek_health_check(provider_t* provider, bool* out_healthy) {
    if (!provider || !out_healthy) return ERR_INVALID_ARGUMENT;

    http_response_t* response = NULL;
    err_t err = http_get(provider->http, "https://api.deepseek.com/models", &response);

    if (err == ERR_OK && response) {
        *out_healthy = http_response_is_success(response);
        http_response_free(response);
        return ERR_OK;
    }

    *out_healthy = false;
    return ERR_OK;
}

static const char** deepseek_get_available_models(uint32_t* out_count) {
    if (out_count) {
        uint32_t count = 0;
        while (DEEPSEEK_MODELS[count]) count++;
        *out_count = count;
    }
    return (const char**)DEEPSEEK_MODELS;
}

// DeepSeek-specific functions
err_t deepseek_enable_search(provider_t* provider, bool enable) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    deepseek_data_t* data = (deepseek_data_t*)provider->impl_data;
    data->enable_search = enable;
    return ERR_OK;
}

err_t deepseek_set_context_length(provider_t* provider, const char* length) {
    if (!provider || !provider->impl_data || !length) return ERR_INVALID_ARGUMENT;

    deepseek_data_t* data = (deepseek_data_t*)provider->impl_data;
    free((void*)data->context_length.data);
    data->context_length = (str_t){ .data = strdup(length), .len = strlen(length) };
    return ERR_OK;
}
