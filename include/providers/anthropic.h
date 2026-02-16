// anthropic.h - Anthropic Provider for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_ANTHROPIC_H
#define CCLAW_PROVIDERS_ANTHROPIC_H

#include "providers/base.h"

// Anthropic-specific configuration
typedef struct anthropic_config_t {
    provider_config_t base;
    str_t version;               // API version (e.g., "2023-06-01")
    str_t beta;                  // Beta features (e.g., "max-tokens-2024-07-15")
    uint32_t max_tokens;         // Max tokens to generate
} anthropic_config_t;

// Anthropic provider creation/destruction
err_t anthropic_create(const provider_config_t* config, provider_t** out_provider);
void anthropic_destroy(provider_t* provider);

// Get Anthropic provider vtable
const provider_vtable_t* anthropic_get_vtable(void);

// Anthropic-specific functions
err_t anthropic_set_version(provider_t* provider, const char* version);
err_t anthropic_set_beta(provider_t* provider, const char* beta);
err_t anthropic_set_max_tokens(provider_t* provider, uint32_t max_tokens);

// Available Anthropic models
static const char* const ANTHROPIC_MODELS[] = {
    // Claude 3.5 series
    "claude-3-5-sonnet-20241022",
    "claude-3-5-haiku-20241022",

    // Claude 3 series
    "claude-3-opus-20240229",
    "claude-3-sonnet-20240229",
    "claude-3-haiku-20240307",

    // Claude 2 series
    "claude-2.1",
    "claude-2.0",

    // Claude Instant
    "claude-instant-1.2",

    NULL
};

#endif // CCLAW_PROVIDERS_ANTHROPIC_H