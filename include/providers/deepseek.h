// deepseek.h - DeepSeek AI Provider for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_DEEPSEEK_H
#define CCLAW_PROVIDERS_DEEPSEEK_H

#include "providers/base.h"

// DeepSeek-specific configuration
typedef struct deepseek_config_t {
    provider_config_t base;
    bool enable_search;        // Enable internet search
    str_t context_length;      // "4k", "8k", "32k", "128k"
} deepseek_config_t;

// DeepSeek provider creation/destruction
err_t deepseek_create(const provider_config_t* config, provider_t** out_provider);
void deepseek_destroy(provider_t* provider);

// Get DeepSeek provider vtable
const provider_vtable_t* deepseek_get_vtable(void);

// DeepSeek-specific functions
err_t deepseek_enable_search(provider_t* provider, bool enable);
err_t deepseek_set_context_length(provider_t* provider, const char* length);

// Available DeepSeek models
static const char* const DEEPSEEK_MODELS[] = {
    "deepseek-chat",           // General chat model
    "deepseek-reasoner",       // Reasoning model
    "deepseek-coder",          // Code generation
    NULL
};

#endif // CCLAW_PROVIDERS_DEEPSEEK_H
