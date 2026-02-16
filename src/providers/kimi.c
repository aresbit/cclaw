// kimi.c - Kimi (Moonshot AI) Provider implementation
// SPDX-License-Identifier: MIT

#include "providers/kimi.h"
#include "providers/base.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Kimi provider instance data
typedef struct kimi_data_t {
    bool enable_search;
    uint32_t max_tokens;
} kimi_data_t;

// Forward declarations
static str_t kimi_get_name(void);
static str_t kimi_get_version(void);
err_t kimi_create(const provider_config_t* config, provider_t** out_provider);
void kimi_destroy(provider_t* provider);
static err_t kimi_connect(provider_t* provider);
static void kimi_disconnect(provider_t* provider);
static bool kimi_is_connected(provider_t* provider);
static err_t kimi_chat(provider_t* provider,
                       const chat_message_t* messages,
                       uint32_t message_count,
                       const tool_def_t* tools,
                       uint32_t tool_count,
                       const char* model,
                       double temperature,
                       chat_response_t** out_response);
static err_t kimi_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count);
static bool kimi_supports_model(provider_t* provider, const char* model);
static err_t kimi_health_check(provider_t* provider, bool* out_healthy);
static const char** kimi_get_available_models(uint32_t* out_count);

// VTable
static const provider_vtable_t kimi_vtable = {
    .get_name = kimi_get_name,
    .get_version = kimi_get_version,
    .create = kimi_create,
    .destroy = kimi_destroy,
    .connect = kimi_connect,
    .disconnect = kimi_disconnect,
    .is_connected = kimi_is_connected,
    .chat = kimi_chat,
    .chat_stream = NULL,  // TODO
    .list_models = kimi_list_models,
    .supports_model = kimi_supports_model,
    .health_check = kimi_health_check,
    .get_available_models = kimi_get_available_models
};

const provider_vtable_t* kimi_get_vtable(void) {
    return &kimi_vtable;
}

static str_t kimi_get_name(void) {
    return STR_LIT("kimi");
}

static str_t kimi_get_version(void) {
    return STR_LIT("1.0.0");
}

err_t kimi_create(const provider_config_t* config, provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = calloc(1, sizeof(provider_t));
    if (!provider) return ERR_OUT_OF_MEMORY;

    provider->vtable = &kimi_vtable;
    provider->config = *config;

    if (str_empty(config->base_url)) {
        provider->config.base_url = (str_t){ .data = KIMI_BASE_URL, .len = strlen(KIMI_BASE_URL) };
    }

    http_client_config_t http_config = http_client_default_config();
    http_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 60000;
    provider->http = http_client_create(&http_config);
    if (!provider->http) {
        free(provider);
        return ERR_NETWORK;
    }

    if (!str_empty(config->api_key)) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Bearer %.*s", (int)config->api_key.len, config->api_key.data);
        http_client_add_header(provider->http, "Authorization", auth_header);
    }

    http_client_add_header(provider->http, "Content-Type", "application/json");

    kimi_data_t* data = calloc(1, sizeof(kimi_data_t));
    if (data) {
        data->enable_search = false;
        data->max_tokens = 8192;
        provider->impl_data = data;
    }

    *out_provider = provider;
    return ERR_OK;
}

void kimi_destroy(provider_t* provider) {
    if (!provider) return;
    if (provider->http) http_client_destroy(provider->http);
    if (provider->impl_data) free(provider->impl_data);
    free(provider);
}

static err_t kimi_connect(provider_t* provider) {
    if (!provider) return ERR_INVALID_ARGUMENT;
    http_response_t* response = NULL;
    err_t err = http_get(provider->http, "https://api.moonshot.cn/v1/models", &response);
    if (err == ERR_OK && response) {
        provider->connected = http_response_is_success(response);
        http_response_free(response);
        return provider->connected ? ERR_OK : ERR_NETWORK;
    }
    provider->connected = false;
    return ERR_NETWORK;
}

static void kimi_disconnect(provider_t* provider) {
    if (provider) provider->connected = false;
}

static bool kimi_is_connected(provider_t* provider) {
    return provider && provider->connected;
}

static char* build_kimi_request(const provider_t* provider,
                                const chat_message_t* messages,
                                uint32_t message_count,
                                const char* model,
                                double temperature) {
    json_value_t* root = json_create_object();
    if (!root) return NULL;

    const char* model_name = model ? model : DEFAULT_KIMI_MODEL;
    json_object_set_string(root, "model", model_name);

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

    json_object_set_number(root, "temperature", temperature);

    kimi_data_t* data = (kimi_data_t*)provider->impl_data;
    if (data) {
        json_object_set_number(root, "max_tokens", data->max_tokens);
    }

    char* json_str = json_print(root, false);
    json_free(root);
    return json_str;
}

static err_t parse_kimi_response(const char* json_str, chat_response_t* response) {
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
                response->content = (str_t){ .data = strdup(content), .len = strlen(content) };
            }
            const char* finish = json_object_get_string(choice_obj, "finish_reason", "stop");
            response->finish_reason = (str_t){ .data = strdup(finish), .len = strlen(finish) };
        }
    }

    json_object_t* usage = json_object_get_object(obj, "usage");
    if (usage) {
        response->prompt_tokens = (uint32_t)json_object_get_number(usage, "prompt_tokens", 0);
        response->completion_tokens = (uint32_t)json_object_get_number(usage, "completion_tokens", 0);
        response->total_tokens = (uint32_t)json_object_get_number(usage, "total_tokens", 0);
    }

    const char* model = json_object_get_string(obj, "model", DEFAULT_KIMI_MODEL);
    response->model = (str_t){ .data = strdup(model), .len = strlen(model) };

    json_free(root);
    return ERR_OK;
}

static err_t kimi_chat(provider_t* provider,
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

    char* request_body = build_kimi_request(provider, messages, message_count, model, temperature);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", KIMI_BASE_URL);

    http_response_t* http_resp = NULL;
    err_t err = http_post_json(provider->http, url, request_body, &http_resp);
    free(request_body);

    if (err != ERR_OK) return err;
    if (!http_response_is_success(http_resp)) {
        http_response_free(http_resp);
        return ERR_PROVIDER;
    }

    chat_response_t* response = calloc(1, sizeof(chat_response_t));
    if (!response) {
        http_response_free(http_resp);
        return ERR_OUT_OF_MEMORY;
    }

    err = parse_kimi_response(http_resp->body.data, response);
    http_response_free(http_resp);

    if (err != ERR_OK) {
        chat_response_free(response);
        return err;
    }

    *out_response = response;
    return ERR_OK;
}

static err_t kimi_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count) {
    (void)provider;
    if (!out_models || !out_count) return ERR_INVALID_ARGUMENT;

    uint32_t count = 0;
    while (KIMI_MODELS[count]) count++;

    str_t* models = calloc(count, sizeof(str_t));
    if (!models) return ERR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < count; i++) {
        models[i] = (str_t){ .data = strdup(KIMI_MODELS[i]), .len = strlen(KIMI_MODELS[i]) };
    }

    *out_models = models;
    *out_count = count;
    return ERR_OK;
}

static bool kimi_supports_model(provider_t* provider, const char* model) {
    (void)provider;
    if (!model) return false;
    for (uint32_t i = 0; KIMI_MODELS[i]; i++) {
        if (strcmp(KIMI_MODELS[i], model) == 0) return true;
    }
    return false;
}

static err_t kimi_health_check(provider_t* provider, bool* out_healthy) {
    if (!provider || !out_healthy) return ERR_INVALID_ARGUMENT;
    http_response_t* response = NULL;
    err_t err = http_get(provider->http, "https://api.moonshot.cn/v1/models", &response);
    if (err == ERR_OK && response) {
        *out_healthy = http_response_is_success(response);
        http_response_free(response);
        return ERR_OK;
    }
    *out_healthy = false;
    return ERR_OK;
}

static const char** kimi_get_available_models(uint32_t* out_count) {
    if (out_count) {
        uint32_t count = 0;
        while (KIMI_MODELS[count]) count++;
        *out_count = count;
    }
    return (const char**)KIMI_MODELS;
}

err_t kimi_enable_search(provider_t* provider, bool enable) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;
    kimi_data_t* data = (kimi_data_t*)provider->impl_data;
    data->enable_search = enable;
    return ERR_OK;
}

err_t kimi_set_context_window(provider_t* provider, uint32_t tokens) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;
    kimi_data_t* data = (kimi_data_t*)provider->impl_data;
    data->max_tokens = tokens;
    return ERR_OK;
}
