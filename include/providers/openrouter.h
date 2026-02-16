// openrouter.h - OpenRouter Provider for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_OPENROUTER_H
#define CCLAW_PROVIDERS_OPENROUTER_H

#include "providers/base.h"

// OpenRouter-specific configuration
typedef struct openrouter_config_t {
    provider_config_t base;
    str_t site_url;            // For rankings
    str_t site_name;           // For rankings
    str_t fallback_model;      // Fallback if primary fails
} openrouter_config_t;

// OpenRouter provider creation/destruction
err_t openrouter_create(const provider_config_t* config, provider_t** out_provider);
void openrouter_destroy(provider_t* provider);

// Get OpenRouter provider vtable
const provider_vtable_t* openrouter_get_vtable(void);

// OpenRouter-specific functions
err_t openrouter_set_site_info(provider_t* provider, const char* url, const char* name);
err_t openrouter_get_generation_stats(provider_t* provider, const char* model, double* out_cost);

// Popular OpenRouter models
static const char* const OPENROUTER_MODELS[] = {
    "anthropic/claude-3.5-sonnet",
    "anthropic/claude-3.5-haiku",
    "anthropic/claude-3-opus",
    "openai/gpt-4o",
    "openai/gpt-4o-mini",
    "google/gemini-pro-1.5",
    "google/gemini-flash-1.5",
    "meta-llama/llama-3.1-405b-instruct",
    "mistralai/mistral-large",
    "nousresearch/hermes-3-llama-3.1-405b",
    "deepseek/deepseek-chat",
    "deepseek/deepseek-coder",
    NULL
};

#endif // CCLAW_PROVIDERS_OPENROUTER_H
