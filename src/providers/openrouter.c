// openrouter.c - OpenRouter Provider implementation
// SPDX-License-Identifier: MIT

#include "providers/openrouter.h"
#include "providers/base.h"
#include "core/error.h"
#include "json_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// OpenRouter-specific data
typedef struct openrouter_data_t {
    str_t site_url;
    str_t site_name;
    str_t fallback_model;
} openrouter_data_t;

static str_t openrouter_get_name(void);
static str_t openrouter_get_version(void);
err_t openrouter_create(const provider_config_t* config, provider_t** out_provider);
void openrouter_destroy(provider_t* provider);
static err_t openrouter_connect(provider_t* provider);
static void openrouter_disconnect(provider_t* provider);
static bool openrouter_is_connected(provider_t* provider);
static err_t openrouter_chat(provider_t* provider,
                             const chat_message_t* messages,
                             uint32_t message_count,
                             const tool_def_t* tools,
                             uint32_t tool_count,
                             const char* model,
                             double temperature,
                             chat_response_t** out_response);
static err_t openrouter_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count);
static bool openrouter_supports_model(provider_t* provider, const char* model);
static err_t openrouter_health_check(provider_t* provider, bool* out_healthy);
static const char** openrouter_get_available_models(uint32_t* out_count);

static const provider_vtable_t openrouter_vtable = {
    .get_name = openrouter_get_name,
    .get_version = openrouter_get_version,
    .create = openrouter_create,
    .destroy = openrouter_destroy,
    .connect = openrouter_connect,
    .disconnect = openrouter_disconnect,
    .is_connected = openrouter_is_connected,
    .chat = openrouter_chat,
    .chat_stream = NULL,
    .list_models = openrouter_list_models,
    .supports_model = openrouter_supports_model,
    .health_check = openrouter_health_check,
    .get_available_models = openrouter_get_available_models
};

const provider_vtable_t* openrouter_get_vtable(void) {
    return &openrouter_vtable;
}

static str_t openrouter_get_name(void) {
    return STR_LIT("openrouter");
}

static str_t openrouter_get_version(void) {
    return STR_LIT("1.0.0");
}

err_t openrouter_create(const provider_config_t* config, provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = calloc(1, sizeof(provider_t));
    if (!provider) return ERR_OUT_OF_MEMORY;

    provider->vtable = &openrouter_vtable;
    provider->config = *config;

    if (str_empty(config->base_url)) {
        provider->config.base_url = (str_t){ .data = OPENROUTER_BASE_URL, .len = strlen(OPENROUTER_BASE_URL) };
    }

    http_client_config_t http_config = http_client_default_config();
    http_config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 60000;
    provider->http = http_client_create(&http_config);
    if (!provider->http) {
        free(provider);
        return ERR_NETWORK;
    }

    if (!str_empty(config->api_key)) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Bearer %.*s", (int)config->api_key.len, config->api_key.data);
        http_client_add_header(provider->http, "Authorization", auth_header);
    }

    http_client_add_header(provider->http, "Content-Type", "application/json");
    http_client_add_header(provider->http, "HTTP-Referer", "https://cclaw.local");
    http_client_add_header(provider->http, "X-Title", "CClaw");

    openrouter_data_t* data = calloc(1, sizeof(openrouter_data_t));
    if (data) {
        provider->impl_data = data;
    }

    *out_provider = provider;
    return ERR_OK;
}

void openrouter_destroy(provider_t* provider) {
    if (!provider) return;
    if (provider->http) http_client_destroy(provider->http);
    if (provider->impl_data) {
        openrouter_data_t* data = (openrouter_data_t*)provider->impl_data;
        free((void*)data->site_url.data);
        free((void*)data->site_name.data);
        free((void*)data->fallback_model.data);
        free(data);
    }
    free(provider);
}

static err_t openrouter_connect(provider_t* provider) {
    if (!provider) return ERR_INVALID_ARGUMENT;
    http_response_t* response = NULL;
    err_t err = http_get(provider->http, "https://openrouter.ai/api/v1/models", &response);
    if (err == ERR_OK && response) {
        provider->connected = http_response_is_success(response);
        http_response_free(response);
        return provider->connected ? ERR_OK : ERR_NETWORK;
    }
    provider->connected = false;
    return ERR_NETWORK;
}

static void openrouter_disconnect(provider_t* provider) {
    if (provider) provider->connected = false;
}

static bool openrouter_is_connected(provider_t* provider) {
    return provider && provider->connected;
}

static char* build_openrouter_request(const provider_t* provider,
                                      const chat_message_t* messages,
                                      uint32_t message_count,
                                      const char* model,
                                      double temperature) {
    json_value_t* root = json_create_object();
    if (!root) return NULL;

    const char* model_name = model ? model : DEFAULT_OPENROUTER_MODEL;
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

    char* json_str = json_print(root, false);
    json_free(root);
    return json_str;
}

static err_t parse_openrouter_response(const char* json_str, chat_response_t* response) {
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

    const char* model = json_object_get_string(obj, "model", DEFAULT_OPENROUTER_MODEL);
    response->model = (str_t){ .data = strdup(model), .len = strlen(model) };

    json_free(root);
    return ERR_OK;
}

static err_t openrouter_chat(provider_t* provider,
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

    char* request_body = build_openrouter_request(provider, messages, message_count, model, temperature);
    if (!request_body) return ERR_OUT_OF_MEMORY;

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", OPENROUTER_BASE_URL);

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

    err = parse_openrouter_response(http_resp->body.data, response);
    http_response_free(http_resp);

    if (err != ERR_OK) {
        chat_response_free(response);
        return err;
    }

    *out_response = response;
    return ERR_OK;
}

static err_t openrouter_list_models(provider_t* provider, str_t** out_models, uint32_t* out_count) {
    (void)provider;
    if (!out_models || !out_count) return ERR_INVALID_ARGUMENT;

    uint32_t count = 0;
    while (OPENROUTER_MODELS[count]) count++;

    str_t* models = calloc(count, sizeof(str_t));
    if (!models) return ERR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < count; i++) {
        models[i] = (str_t){ .data = strdup(OPENROUTER_MODELS[i]), .len = strlen(OPENROUTER_MODELS[i]) };
    }

    *out_models = models;
    *out_count = count;
    return ERR_OK;
}

static bool openrouter_supports_model(provider_t* provider, const char* model) {
    (void)provider;
    if (!model) return false;
    for (uint32_t i = 0; OPENROUTER_MODELS[i]; i++) {
        if (strcmp(OPENROUTER_MODELS[i], model) == 0) return true;
    }
    return true;  // OpenRouter supports many models, assume valid
}

static err_t openrouter_health_check(provider_t* provider, bool* out_healthy) {
    if (!provider || !out_healthy) return ERR_INVALID_ARGUMENT;
    http_response_t* response = NULL;
    err_t err = http_get(provider->http, "https://openrouter.ai/api/v1/models", &response);
    if (err == ERR_OK && response) {
        *out_healthy = http_response_is_success(response);
        http_response_free(response);
        return ERR_OK;
    }
    *out_healthy = false;
    return ERR_OK;
}

static const char** openrouter_get_available_models(uint32_t* out_count) {
    if (out_count) {
        uint32_t count = 0;
        while (OPENROUTER_MODELS[count]) count++;
        *out_count = count;
    }
    return (const char**)OPENROUTER_MODELS;
}

err_t openrouter_set_site_info(provider_t* provider, const char* url, const char* name) {
    if (!provider || !provider->impl_data) return ERR_INVALID_ARGUMENT;
    openrouter_data_t* data = (openrouter_data_t*)provider->impl_data;
    if (url) {
        free((void*)data->site_url.data);
        data->site_url = (str_t){ .data = strdup(url), .len = strlen(url) };
    }
    if (name) {
        free((void*)data->site_name.data);
        data->site_name = (str_t){ .data = strdup(name), .len = strlen(name) };
    }
    return ERR_OK;
}

err_t openrouter_get_generation_stats(provider_t* provider, const char* model, double* out_cost) {
    (void)provider;
    (void)model;
    if (out_cost) *out_cost = 0.0;
    return ERR_NOT_IMPLEMENTED;
}
