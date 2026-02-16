// router.c - Provider routing and selection implementation
// SPDX-License-Identifier: MIT

#include "providers/router.h"
#include "providers/base.h"
#include "core/config.h"
#include "core/error.h"
#include "core/alloc.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_RETRIES 3
#define RETRY_DELAY_MS 1000

// Helper function to get provider names from config
static const char** get_fallback_providers(config_t* config, uint32_t* out_count) {
    if (!config || !out_count) return NULL;

    *out_count = config->reliability.fallback_providers_count;
    return (const char**)config->reliability.fallback_providers;
}

// Helper to check if provider supports model
static bool provider_supports_model_by_name(const char* provider_name, const char* model) {
    // This is a simplified check - in real implementation we would
    // create a provider instance and check its supports_model method
    if (!provider_name || !model) return false;

    // Check based on provider name and model prefix
    if (strcmp(provider_name, "openai") == 0) {
        return (strncmp(model, "gpt-", 4) == 0) ||
               (strncmp(model, "o1-", 3) == 0) ||
               (strncmp(model, "text-embedding-", 15) == 0);
    }
    else if (strcmp(provider_name, "anthropic") == 0) {
        return strncmp(model, "claude-", 7) == 0;
    }
    else if (strcmp(provider_name, "deepseek") == 0) {
        return strncmp(model, "deepseek-", 9) == 0;
    }
    else if (strcmp(provider_name, "kimi") == 0) {
        return strncmp(model, "moonshot-", 9) == 0;
    }
    else if (strcmp(provider_name, "openrouter") == 0) {
        // OpenRouter supports many models
        return true;
    }

    return false;
}

provider_router_t* provider_router_create(config_t* config) {
    if (!config) return NULL;

    provider_router_t* router = calloc(1, sizeof(provider_router_t));
    if (!router) return NULL;

    router->config = config;

    // Initialize provider array based on configuration
    // For now, we'll just allocate space for providers
    router->providers = NULL;
    router->provider_count = 0;
    router->current_index = 0;
    router->retry_count = 0;
    router->last_failure_time = 0;

    return router;
}

void provider_router_destroy(provider_router_t* router) {
    if (!router) return;

    // Destroy all providers
    for (uint32_t i = 0; i < router->provider_count; i++) {
        if (router->providers[i]) {
            provider_free(router->providers[i]);
        }
    }

    free(router->providers);
    free(router);
}

err_t provider_router_get_provider(provider_router_t* router,
                                   const char* model_hint,
                                   provider_t** out_provider) {
    if (!router || !router->config || !out_provider) {
        return ERR_INVALID_ARGUMENT;
    }

    // First, check model_routes for specific routing
    for (uint32_t i = 0; i < router->config->model_routes_count; i++) {
        const str_t* hint = &router->config->model_routes[i].hint;
        const str_t* provider_name = &router->config->model_routes[i].provider;

        if (!str_empty(*hint) && model_hint) {
            // Simple prefix matching for model hint
            if (strncmp(model_hint, hint->data, hint->len) == 0) {
                // Found a route for this model hint
                provider_config_t provider_config = {
                    .name = *provider_name,
                    .api_key = router->config->model_routes[i].api_key,
                    .default_model = router->config->model_routes[i].model,
                    .default_temperature = router->config->default_temperature,
                    .timeout_ms = 30000
                };

                // Use the configured API key if available, otherwise use default
                if (str_empty(provider_config.api_key)) {
                    provider_config.api_key = router->config->api_key;
                }

                return provider_create(provider_name->data, &provider_config, out_provider);
            }
        }
    }

    // No specific route found, use default provider
    const char* default_provider = NULL;
    if (!str_empty(router->config->default_provider)) {
        default_provider = router->config->default_provider.data;
    } else {
        // Fallback to first available provider
        default_provider = "openrouter"; // Default fallback
    }

    // Create default provider configuration
    provider_config_t provider_config = {
        .name = { .data = (char*)default_provider, .len = strlen(default_provider) },
        .api_key = router->config->api_key,
        .default_model = router->config->default_model,
        .default_temperature = router->config->default_temperature,
        .timeout_ms = 30000
    };

    return provider_create(default_provider, &provider_config, out_provider);
}

err_t provider_router_create_with_failover(config_t* config,
                                           const char* preferred_provider,
                                           provider_t** out_provider) {
    if (!config || !out_provider) return ERR_INVALID_ARGUMENT;

    provider_t* provider = NULL;
    err_t err = ERR_OK;

    // Try preferred provider first
    if (preferred_provider) {
        provider_config_t provider_config = {
            .name = { .data = (char*)preferred_provider, .len = strlen(preferred_provider) },
            .api_key = config->api_key,
            .default_model = config->default_model,
            .default_temperature = config->default_temperature,
            .timeout_ms = 30000
        };

        err = provider_create(preferred_provider, &provider_config, &provider);
        if (err == ERR_OK && provider) {
            // Do a quick health check
            bool healthy = false;
            if (provider->vtable->health_check) {
                err = provider->vtable->health_check(provider, &healthy);
                if (err == ERR_OK && healthy) {
                    *out_provider = provider;
                    return ERR_OK;
                }
            } else {
                // If no health check, assume it's healthy
                *out_provider = provider;
                return ERR_OK;
            }

            // Health check failed, clean up
            provider_free(provider);
            provider = NULL;
        }
    }

    // Try fallback providers
    uint32_t fallback_count = 0;
    const char** fallback_providers = get_fallback_providers(config, &fallback_count);

    if (fallback_providers && fallback_count > 0) {
        for (uint32_t i = 0; i < fallback_count; i++) {
            const char* fallback = fallback_providers[i];
            if (!fallback) continue;

            // Skip if it's the same as preferred provider we already tried
            if (preferred_provider && strcmp(fallback, preferred_provider) == 0) {
                continue;
            }

            provider_config_t provider_config = {
                .name = { .data = (char*)fallback, .len = strlen(fallback) },
                .api_key = config->api_key,
                .default_model = config->default_model,
                .default_temperature = config->default_temperature,
                .timeout_ms = 30000
            };

            err = provider_create(fallback, &provider_config, &provider);
            if (err == ERR_OK && provider) {
                *out_provider = provider;
                return ERR_OK;
            }
        }
    }

    // All providers failed, try to create any available provider
    const char* providers_to_try[] = {"openrouter", "openai", "anthropic", "deepseek", "kimi", NULL};

    for (uint32_t i = 0; providers_to_try[i]; i++) {
        // Skip already tried providers
        if (preferred_provider && strcmp(providers_to_try[i], preferred_provider) == 0) {
            continue;
        }

        if (fallback_providers) {
            bool already_tried = false;
            for (uint32_t j = 0; j < fallback_count; j++) {
                if (fallback_providers[j] && strcmp(providers_to_try[i], fallback_providers[j]) == 0) {
                    already_tried = true;
                    break;
                }
            }
            if (already_tried) continue;
        }

        provider_config_t provider_config = {
            .name = { .data = (char*)providers_to_try[i], .len = strlen(providers_to_try[i]) },
            .api_key = config->api_key,
            .default_model = config->default_model,
            .default_temperature = config->default_temperature,
            .timeout_ms = 30000
        };

        err = provider_create(providers_to_try[i], &provider_config, &provider);
        if (err == ERR_OK && provider) {
            *out_provider = provider;
            return ERR_OK;
        }
    }

    return ERR_PROVIDER;
}

err_t provider_router_health_check(provider_router_t* router,
                                   bool* out_all_healthy) {
    if (!router || !out_all_healthy) return ERR_INVALID_ARGUMENT;

    *out_all_healthy = true;

    // If no providers are instantiated yet, we can't check health
    if (router->provider_count == 0) {
        return ERR_OK;
    }

    for (uint32_t i = 0; i < router->provider_count; i++) {
        if (router->providers[i] && router->providers[i]->vtable->health_check) {
            bool healthy = false;
            err_t err = router->providers[i]->vtable->health_check(router->providers[i], &healthy);
            if (err != ERR_OK || !healthy) {
                *out_all_healthy = false;
                // Don't return early - check all providers
            }
        }
    }

    return ERR_OK;
}

err_t provider_router_retry_request(provider_router_t* router,
                                    err_t (*create_func)(const provider_config_t*, provider_t**),
                                    const provider_config_t* config,
                                    provider_t** out_provider) {
    if (!router || !create_func || !config || !out_provider) {
        return ERR_INVALID_ARGUMENT;
    }

    uint32_t max_retries = router->config->reliability.provider_retries;
    if (max_retries == 0) max_retries = MAX_RETRIES;

    uint64_t backoff_ms = router->config->reliability.provider_backoff_ms;
    if (backoff_ms == 0) backoff_ms = RETRY_DELAY_MS;

    err_t last_error = ERR_OK;

    for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
        // Wait before retry (except first attempt)
        if (attempt > 0) {
            // Simple sleep - in real implementation would use async/non-blocking
            struct timespec ts = {
                .tv_sec = backoff_ms / 1000,
                .tv_nsec = (backoff_ms % 1000) * 1000000
            };
            nanosleep(&ts, NULL);

            // Exponential backoff
            backoff_ms *= 2;
        }

        // Try to create provider
        provider_t* provider = NULL;
        err_t err = create_func(config, &provider);

        if (err == ERR_OK && provider) {
            // Optional: do a health check
            bool healthy = false;
            if (provider->vtable->health_check) {
                err = provider->vtable->health_check(provider, &healthy);
                if (err == ERR_OK && healthy) {
                    *out_provider = provider;
                    return ERR_OK;
                }
            } else {
                // No health check, assume success
                *out_provider = provider;
                return ERR_OK;
            }

            // Health check failed
            provider_free(provider);
            last_error = err;
        } else {
            last_error = err;
        }
    }

    return last_error;
}