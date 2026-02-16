// router.h - Provider routing and selection logic for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_ROUTER_H
#define CCLAW_PROVIDERS_ROUTER_H

#include "providers/base.h"
#include "core/config.h"

// Provider router for load balancing and failover
typedef struct provider_router_t {
    config_t* config;
    provider_t** providers;
    uint32_t provider_count;
    uint32_t current_index;
    uint32_t retry_count;
    uint64_t last_failure_time;
} provider_router_t;

// Create a provider router from configuration
provider_router_t* provider_router_create(config_t* config);

// Destroy provider router
void provider_router_destroy(provider_router_t* router);

// Get the best provider for a given model/request
// Implements failover and load balancing
err_t provider_router_get_provider(provider_router_t* router,
                                   const char* model_hint,
                                   provider_t** out_provider);

// Create a provider with automatic failover
// Tries primary provider first, then fallbacks
err_t provider_router_create_with_failover(config_t* config,
                                           const char* preferred_provider,
                                           provider_t** out_provider);

// Health check all providers in router
err_t provider_router_health_check(provider_router_t* router,
                                   bool* out_all_healthy);

// Retry logic for failed requests
err_t provider_router_retry_request(provider_router_t* router,
                                    err_t (*create_func)(const provider_config_t*, provider_t**),
                                    const provider_config_t* config,
                                    provider_t** out_provider);

#endif // CCLAW_PROVIDERS_ROUTER_H