// openai.c - OpenAI Provider implementation
// SPDX-License-Identifier: MIT

#include "providers/openai.h"
#include "providers/base.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// OpenAI provider instance data
typedef struct openai_data_t {
    str_t organization;          // OpenAI organization ID
    str_t project;               // OpenAI project ID
    bool include_reasoning;      // For o1 models
    uint32_t max_completion_tokens;
} openai_data_t;

// Forward declarations for vtable
static str_t openai_get_name(void);
static str_t openai_get_version(void);
err_t openai_create(const provider_config_t* config, provider_t** out_provider);
void openai_destroy(provider_t* provider);
static err_t openai_connect(provider_t* provider);
static void openai_disconnect(provider_t* provider);
static bool openai_is_connected(provider_t* provider);
static err_t openai_chat(provider_t* provider,
                         const chat_message_t* messages,
                         uint32_t message_count,
                         const tool_def_t* tools,
                         uint32_t tool_count,
                         const char* model,
                         double temperature,
                         chat_response_t** out_response);
static err_t openai_chat_stream(provider_t* provider,
                                const chat_message_t* messages,
                                uint32_t message_count,
                                const char* model,
                                double temperature,
                                void (*on_chunk)(const char* chunk, void* user_data),
                                void* user_data);
static err_t openai_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count);
static bool openai_supports_model(provider_t* provider, const char* model);
static err_t openai_health_check(provider_t* provider, bool* out_healthy);
static const char** openai_get_available_models(uint32_t* out_count);

// VTable definition
static const provider_vtable_t openai_vtable = {
    .get_name = openai_get_name,
    .get_version = openai_get_version,
    .create = openai_create,
    .destroy = openai_destroy,
    .connect = openai_connect,
    .disconnect = openai_disconnect,
    .is_connected = openai_is_connected,
    .chat = openai_chat,
    .chat_stream = openai_chat_stream,
    .list_models = openai_list_models,
    .supports_model = openai_supports_model,
    .health_check = openai_health_check,
    .get_available_models = openai_get_available_models
};

// Get vtable
const provider_vtable_t* openai_get_vtable(void) {
    return &openai_vtable;
}

static str_t openai_get_name(void) {
    return STR_LIT("openai");
}

static str_t openai_get_version(void) {
    return STR_LIT("1.0.0");
}

err_t openai_create(const provider_config_t* config, provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = calloc(1, sizeof(provider_t));
    if (!provider) return ERR_OUT_OF_MEMORY;

    provider->vtable = &openai_vtable;
    provider->config = *config;

    // Set default base URL if not provided
    if (str_empty(config->base_url)) {
        provider->config.base_url = (str_t){ .data = OPENAI_BASE_URL, .len = strlen(OPENAI_BASE_URL) };
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
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Bearer %.*s", (int)config->api_key.len, config->api_key.data);
        http_client_add_header(provider->http, "Authorization", auth_header);
    }

    // Add content type header
    http_client_add_header(provider->http, "Content-Type", "application/json");

    // Add OpenAI-specific headers if available
    openai_data_t* data = calloc(1, sizeof(openai_data_t));
    if (data) {
        provider->impl_data = data;
    }

    *out_provider = provider;
    return ERR_OK;
}

void openai_destroy(provider_t* provider) {
    if (!provider) return;

    if (provider->http) {
        http_client_destroy(provider->http);
    }

    if (provider->impl_data) {
        openai_data_t* data = (openai_data_t*)provider->impl_data;
        free((void*)data->organization.data);
        free((void*)data->project.data);
        free(data);
    }

    free(provider);
}

static err_t openai_connect(provider_t* provider) {
    if (!provider) return ERR_INVALID_ARGUMENT;

    // OpenAI health check: list models endpoint
    http_response_t* response = NULL;
    char url[512];
    snprintf(url, sizeof(url), "%s/models", OPENAI_BASE_URL);

    err_t err = http_get(provider->http, url, &response);
    if (err == ERR_OK && response) {
        provider->connected = http_response_is_success(response);
        http_response_free(response);
        return provider->connected ? ERR_OK : ERR_NETWORK;
    }

    provider->connected = false;
    return ERR_NETWORK;
}

static void openai_disconnect(provider_t* provider) {
    if (provider) {
        provider->connected = false;
    }
}

static bool openai_is_connected(provider_t* provider) {
    return provider && provider->connected;
}

static char* build_openai_request(const provider_t* provider,
                                  const chat_message_t* messages,
                                  uint32_t message_count,
                                  const tool_def_t* tools,
                                  uint32_t tool_count,
                                  const char* model,
                                  double temperature,
                                  bool stream) {
    json_value_t* root = json_create_object();
    if (!root) return NULL;

    const char* model_name = model ? model : DEFAULT_OPENAI_MODEL;
    json_object_set_string(root, "model", model_name);

    // Build messages array
    json_value_t* messages_arr = json_create_array();
    for (uint32_t i = 0; i < message_count; i++) {
        json_value_t* msg_obj = json_create_object();

        // Convert role to OpenAI format
        const char* role_str = "user";
        switch (messages[i].role) {
            case CHAT_ROLE_SYSTEM: role_str = "system"; break;
            case CHAT_ROLE_USER: role_str = "user"; break;
            case CHAT_ROLE_ASSISTANT: role_str = "assistant"; break;
            case CHAT_ROLE_TOOL: role_str = "tool"; break;
        }
        json_object_set_string(msg_obj, "role", role_str);

        // Set content
        if (messages[i].content.len > 0) {
            // Create a null-terminated copy for JSON
            char* content_copy = malloc(messages[i].content.len + 1);
            if (content_copy) {
                memcpy(content_copy, messages[i].content.data, messages[i].content.len);
                content_copy[messages[i].content.len] = '\0';
                json_object_set_string(msg_obj, "content", content_copy);
                free(content_copy);
            } else {
                json_object_set_string(msg_obj, "content", "");
            }
        }

        // TODO: Handle tool calls if present
        json_array_append(messages_arr, msg_obj);
    }
    json_object_set(root, "messages", messages_arr);

    // Set temperature
    if (temperature >= 0.0 && temperature <= 2.0) {
        json_object_set_number(root, "temperature", temperature);
    }

    // Set stream parameter
    if (stream) {
        json_object_set_bool(root, "stream", true);
    }

    // TODO: Add tools support
    (void)tools;
    (void)tool_count;

    // TODO: Add max_tokens, top_p, etc.
    if (provider->impl_data) {
        openai_data_t* data = (openai_data_t*)provider->impl_data;
        if (data->max_completion_tokens > 0) {
            json_object_set_number(root, "max_tokens", (double)data->max_completion_tokens);
        }
    }

    char* json_str = json_print(root, false);
    json_free(root);
    return json_str;
}

static err_t parse_openai_response(const char* json_str, chat_response_t* response) {
    json_value_t* root = json_parse(json_str);
    if (!root) return ERR_CONFIG_PARSE;

    json_object_t* obj = json_as_object(root);
    if (!obj) {
        json_free(root);
        return ERR_CONFIG_PARSE;
    }

    json_array_t* choices = json_object_get_array(obj, "choices");
    if (choices && json_array_length(choices) > 0) {
        json_value_t* first = json_array_get(choices, 0);
        json_object_t* choice_obj = json_as_object(first);
        if (choice_obj) {
            json_object_t* message = json_object_get_object(choice_obj, "message");
            if (message) {
                const char* content = json_object_get_string(message, "content", "");
                if (content) {
                    response->content = (str_t){ .data = strdup(content), .len = strlen(content) };
                }
            }
            const char* finish = json_object_get_string(choice_obj, "finish_reason", "stop");
            response->finish_reason = (str_t){ .data = strdup(finish), .len = strlen(finish) };
        }
    }

    const char* model = json_object_get_string(obj, "model", DEFAULT_OPENAI_MODEL);
    response->model = (str_t){ .data = strdup(model), .len = strlen(model) };

    // Parse token usage if available
    json_object_t* usage = json_object_get_object(obj, "usage");
    if (usage) {
        response->prompt_tokens = (uint32_t)json_object_get_number(usage, "prompt_tokens", 0);
        response->completion_tokens = (uint32_t)json_object_get_number(usage, "completion_tokens", 0);
        response->total_tokens = (uint32_t)json_object_get_number(usage, "total_tokens", 0);
    }

    json_free(root);
    return ERR_OK;
}

static err_t openai_chat(provider_t* provider,
                         const chat_message_t* messages,
                         uint32_t message_count,
                         const tool_def_t* tools,
                         uint32_t tool_count,
                         const char* model,
                         double temperature,
                         chat_response_t** out_response) {
    if (!provider || !provider->http || !out_response) return ERR_INVALID_ARGUMENT;

    // Build request JSON
    char* request_body = build_openai_request(provider, messages, message_count, tools, tool_count, model, temperature, false);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", OPENAI_BASE_URL);

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

    err = parse_openai_response(http_resp->body.data, response);
    http_response_free(http_resp);

    if (err != ERR_OK) {
        chat_response_free(response);
        return err;
    }

    *out_response = response;
    return ERR_OK;
}

static err_t openai_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count) {
    (void)provider;
    if (!out_models || !out_count) return ERR_INVALID_ARGUMENT;

    uint32_t count = 0;
    while (OPENAI_MODELS[count]) count++;

    str_t* models = calloc(count, sizeof(str_t));
    if (!models) return ERR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < count; i++) {
        models[i] = (str_t){ .data = strdup(OPENAI_MODELS[i]), .len = strlen(OPENAI_MODELS[i]) };
    }

    *out_models = models;
    *out_count = count;
    return ERR_OK;
}

static bool openai_supports_model(provider_t* provider, const char* model) {
    (void)provider;
    if (!model) return false;

    for (uint32_t i = 0; OPENAI_MODELS[i]; i++) {
        if (strcmp(OPENAI_MODELS[i], model) == 0) return true;
    }

    // OpenAI may support other models not in our static list
    // Check common prefixes
    if (strncmp(model, "gpt-", 4) == 0) return true;
    if (strncmp(model, "o1-", 3) == 0) return true;
    if (strncmp(model, "text-embedding-", 15) == 0) return true;

    return false;
}

static err_t openai_health_check(provider_t* provider, bool* out_healthy) {
    if (!provider || !out_healthy) return ERR_INVALID_ARGUMENT;

    http_response_t* response = NULL;
    char url[512];
    snprintf(url, sizeof(url), "%s/models", OPENAI_BASE_URL);

    err_t err = http_get(provider->http, url, &response);
    if (err == ERR_OK && response) {
        *out_healthy = http_response_is_success(response);
        http_response_free(response);
        return ERR_OK;
    }

    *out_healthy = false;
    return ERR_OK;
}

static const char** openai_get_available_models(uint32_t* out_count) {
    if (out_count) {
        uint32_t count = 0;
        while (OPENAI_MODELS[count]) count++;
        *out_count = count;
    }
    return (const char**)OPENAI_MODELS;
}

// OpenAI-specific functions
err_t openai_set_organization(provider_t* provider, const char* org_id) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    openai_data_t* data = (openai_data_t*)provider->impl_data;
    free((void*)data->organization.data);

    if (org_id) {
        data->organization = (str_t){ .data = strdup(org_id), .len = strlen(org_id) };
        // Update HTTP header
        char header[512];
        snprintf(header, sizeof(header), "OpenAI-Organization: %s", org_id);
        // TODO: Add or update header in HTTP client
    } else {
        data->organization = (str_t){0};
    }

    return ERR_OK;
}

err_t openai_set_project(provider_t* provider, const char* project_id) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    openai_data_t* data = (openai_data_t*)provider->impl_data;
    free((void*)data->project.data);

    if (project_id) {
        data->project = (str_t){ .data = strdup(project_id), .len = strlen(project_id) };
        // TODO: Update HTTP header if needed
    } else {
        data->project = (str_t){0};
    }

    return ERR_OK;
}

err_t openai_set_include_reasoning(provider_t* provider, bool include) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;

    openai_data_t* data = (openai_data_t*)provider->impl_data;
    data->include_reasoning = include;

    return ERR_OK;
}

// SSE parser context for OpenAI
typedef struct {
    void (*on_chunk)(const char* chunk, void* user_data);
    void* user_data;
    char* buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    size_t partial_line_len;
    char partial_line[8192];
} openai_sse_parser_t;

static size_t openai_sse_parser_write(const char* data, size_t len, void* userp) {
    openai_sse_parser_t* parser = (openai_sse_parser_t*)userp;
    size_t consumed = 0;

    while (consumed < len) {
        const char* newline = (const char*)memchr(data + consumed, '\n', len - consumed);
        size_t chunk_end = newline ? (size_t)(newline - data) : len;

        size_t line_len = chunk_end - consumed;
        if (parser->partial_line_len + line_len < sizeof(parser->partial_line)) {
            memcpy(parser->partial_line + parser->partial_line_len, data + consumed, line_len);
            parser->partial_line_len += line_len;
        }

        consumed = chunk_end;

        if (newline) {
            parser->partial_line[parser->partial_line_len] = '\0';

            // Check for SSE event line
            if (parser->partial_line_len >= 6 && strncmp(parser->partial_line, "data: ", 6) == 0) {
                const char* event_data = parser->partial_line + 6;

                // Check for "[DONE]" event
                if (strcmp(event_data, "[DONE]") == 0) {
                    parser->partial_line_len = 0;
                    consumed++;
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

            parser->partial_line_len = 0;
            consumed++;
        }
    }

    return len;
}

static err_t openai_chat_stream(provider_t* provider,
                                const chat_message_t* messages,
                                uint32_t message_count,
                                const char* model,
                                double temperature,
                                void (*on_chunk)(const char* chunk, void* user_data),
                                void* user_data) {
    if (!provider || !provider->http || !on_chunk) return ERR_INVALID_ARGUMENT;

    // Build streaming request
    char* request_body = build_openai_request(provider, messages, message_count, NULL, 0, model, temperature, true);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", OPENAI_BASE_URL);

    // Initialize SSE parser
    openai_sse_parser_t parser = {
        .on_chunk = on_chunk,
        .user_data = user_data,
        .buffer = NULL,
        .buffer_size = 0,
        .buffer_capacity = 0,
        .partial_line_len = 0
    };

    // Make streaming request
    err_t err = http_post_json_stream(provider->http, url, request_body, openai_sse_parser_write, &parser);
    free(request_body);

    return err;
}