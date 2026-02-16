// base.c - Provider base implementation for CClaw
// SPDX-License-Identifier: MIT

#include "providers/base.h"
#include "providers/openai.h"
#include "providers/anthropic.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Response helpers
chat_response_t* chat_response_create(void) {
    return calloc(1, sizeof(chat_response_t));
}

void chat_response_free(chat_response_t* response) {
    if (!response) return;

    free((void*)response->content.data);
    free((void*)response->finish_reason.data);
    free((void*)response->model.data);
    free((void*)response->tool_calls.data);

    free(response);
}

void chat_response_clear(chat_response_t* response) {
    if (!response) return;

    free((void*)response->content.data);
    free((void*)response->finish_reason.data);
    free((void*)response->model.data);
    free((void*)response->tool_calls.data);

    memset(response, 0, sizeof(chat_response_t));
}

// Message helpers
chat_message_t* chat_message_create(chat_role_t role, const char* content) {
    chat_message_t* msg = calloc(1, sizeof(chat_message_t));
    if (!msg) return NULL;

    msg->role = role;
    if (content) {
        msg->content.data = strdup(content);
        msg->content.len = strlen(content);
    }

    return msg;
}

void chat_message_free(chat_message_t* message) {
    if (!message) return;

    free((void*)message->content.data);
    free((void*)message->tool_calls.data);
    free((void*)message->tool_call_id.data);

    free(message);
}

void chat_message_array_free(chat_message_t* messages, uint32_t count) {
    if (!messages) return;

    for (uint32_t i = 0; i < count; i++) {
        free((void*)messages[i].content.data);
        free((void*)messages[i].tool_calls.data);
        free((void*)messages[i].tool_call_id.data);
    }

    free(messages);
}

// Tool helpers
tool_def_t* tool_def_create(const char* name, const char* description, const char* parameters) {
    tool_def_t* tool = calloc(1, sizeof(tool_def_t));
    if (!tool) return NULL;

    if (name) {
        tool->name.data = strdup(name);
        tool->name.len = strlen(name);
    }
    if (description) {
        tool->description.data = strdup(description);
        tool->description.len = strlen(description);
    }
    if (parameters) {
        tool->parameters.data = strdup(parameters);
        tool->parameters.len = strlen(parameters);
    }

    return tool;
}

void tool_def_free(tool_def_t* tool) {
    if (!tool) return;

    free((void*)tool->name.data);
    free((void*)tool->description.data);
    free((void*)tool->parameters.data);

    free(tool);
}

void tool_def_array_free(tool_def_t* tools, uint32_t count) {
    if (!tools) return;

    for (uint32_t i = 0; i < count; i++) {
        free((void*)tools[i].name.data);
        free((void*)tools[i].description.data);
        free((void*)tools[i].parameters.data);
    }

    free(tools);
}

// Provider registry (simple implementation)
typedef struct {
    const char* name;
    const provider_vtable_t* vtable;
} provider_entry_t;

#define MAX_PROVIDERS 16
static provider_entry_t g_registry[MAX_PROVIDERS];
static uint32_t g_provider_count = 0;
static bool g_registry_initialized = false;

err_t provider_registry_init(void) {
    if (g_registry_initialized) return ERR_OK;

    memset(g_registry, 0, sizeof(g_registry));
    g_provider_count = 0;
    g_registry_initialized = true;

    // Register built-in providers
    provider_register("openrouter", openrouter_get_vtable());
    provider_register("deepseek", deepseek_get_vtable());
    provider_register("kimi", kimi_get_vtable());
    provider_register("openai", openai_get_vtable());
    provider_register("anthropic", anthropic_get_vtable());

    return ERR_OK;
}

void provider_registry_shutdown(void) {
    memset(g_registry, 0, sizeof(g_registry));
    g_provider_count = 0;
    g_registry_initialized = false;
}

err_t provider_register(const char* name, const provider_vtable_t* vtable) {
    if (!name || !vtable) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) provider_registry_init();
    if (g_provider_count >= MAX_PROVIDERS) return ERR_OUT_OF_MEMORY;

    // Check for duplicates
    for (uint32_t i = 0; i < g_provider_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return ERR_INVALID_ARGUMENT; // Already registered
        }
    }

    g_registry[g_provider_count].name = name;
    g_registry[g_provider_count].vtable = vtable;
    g_provider_count++;

    return ERR_OK;
}

err_t provider_create(const char* name, const provider_config_t* config, provider_t** out_provider) {
    if (!name || !config || !out_provider) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) provider_registry_init();

    for (uint32_t i = 0; i < g_provider_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return g_registry[i].vtable->create(config, out_provider);
        }
    }

    return ERR_NOT_FOUND;
}

err_t provider_registry_list(const char*** out_names, uint32_t* out_count) {
    if (!out_names || !out_count) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) provider_registry_init();

    static const char* names[MAX_PROVIDERS];
    for (uint32_t i = 0; i < g_provider_count; i++) {
        names[i] = g_registry[i].name;
    }

    *out_names = names;
    *out_count = g_provider_count;

    return ERR_OK;
}

// Provider helpers
provider_t* provider_alloc(const provider_vtable_t* vtable) {
    provider_t* provider = calloc(1, sizeof(provider_t));
    if (provider) {
        provider->vtable = vtable;
    }
    return provider;
}

void provider_free(provider_t* provider) {
    if (!provider) return;
    if (provider->vtable && provider->vtable->destroy) {
        provider->vtable->destroy(provider);
    } else {
        free(provider);
    }
}

err_t provider_chat_with_retry(provider_t* provider,
                               const chat_message_t* messages,
                               uint32_t message_count,
                               const tool_def_t* tools,
                               uint32_t tool_count,
                               const char* model,
                               double temperature,
                               uint32_t max_retries,
                               uint64_t retry_delay_ms,
                               chat_response_t** out_response) {
    if (!provider || !provider->vtable || !provider->vtable->chat) {
        return ERR_INVALID_ARGUMENT;
    }

    if (max_retries == 0) {
        // No retry requested, just call the provider directly
        return provider->vtable->chat(provider, messages, message_count,
                                     tools, tool_count, model, temperature,
                                     out_response);
    }

    err_t last_error = ERR_OK;
    uint64_t current_delay = retry_delay_ms;

    for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
        // Wait before retry (except first attempt)
        if (attempt > 0) {
            // Simple sleep - in production would use async/non-blocking
            struct timespec ts = {
                .tv_sec = current_delay / 1000,
                .tv_nsec = (current_delay % 1000) * 1000000
            };
            nanosleep(&ts, NULL);

            // Exponential backoff
            current_delay *= 2;
        }

        // Try the chat request
        err_t err = provider->vtable->chat(provider, messages, message_count,
                                          tools, tool_count, model, temperature,
                                          out_response);

        if (err == ERR_OK) {
            return ERR_OK;
        }

        last_error = err;

        // Check if error is retryable
        // Some errors like invalid input shouldn't be retried
        switch (err) {
            case ERR_NETWORK:
            case ERR_TIMEOUT:
            case ERR_PROVIDER:
            case ERR_PROVIDER_UNAVAILABLE:
            case ERR_CONNECTION_FAILED:
            case ERR_CONNECTION_TIMEOUT:
            case ERR_HTTP_ERROR:
            case ERR_RATE_LIMITED:
                // These are retryable
                break;
            case ERR_INVALID_ARGUMENT:
            case ERR_OUT_OF_MEMORY:
            case ERR_PROVIDER_AUTH:
            case ERR_AUTH_FAILED:
            case ERR_INVALID_TOKEN:
            default:
                // Not retryable
                return err;
        }
    }

    return last_error;
}
