// kimi.h - Kimi (Moonshot AI) Provider for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_KIMI_H
#define CCLAW_PROVIDERS_KIMI_H

#include "providers/base.h"

// Kimi-specific configuration
typedef struct kimi_config_t {
    provider_config_t base;
    bool enable_search;        // Kimi search capability
    uint32_t max_tokens;       // Context window: 8k, 32k, 128k, 256k
} kimi_config_t;

// Kimi provider creation/destruction
err_t kimi_create(const provider_config_t* config, provider_t** out_provider);
void kimi_destroy(provider_t* provider);

// Get Kimi provider vtable
const provider_vtable_t* kimi_get_vtable(void);

// Kimi-specific functions
err_t kimi_enable_search(provider_t* provider, bool enable);
err_t kimi_set_context_window(provider_t* provider, uint32_t tokens);

// Available Kimi models
static const char* const KIMI_MODELS[] = {
    "kimi-k2-0905-Preview",
    "kimi-k2.5",
    "kimi-k2-turbo-preview",
    "kimi-k2-thinking",
    "moonshot-v1-128k",        // 128K context
    "moonshot-v1-256k",        // 256K context (for long documents)
    "moonshot-v1-8k-vision",   // Vision capable
    "moonshot-v1-32k-vision",  // Vision capable
    NULL
};

#endif // CCLAW_PROVIDERS_KIMI_H
