// openai.h - OpenAI Provider for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_PROVIDERS_OPENAI_H
#define CCLAW_PROVIDERS_OPENAI_H

#include "providers/base.h"

// OpenAI-specific configuration
typedef struct openai_config_t {
    provider_config_t base;
    str_t organization;          // OpenAI organization ID (optional)
    str_t project;               // OpenAI project ID (optional)
    bool include_reasoning;      // For o1 models with reasoning
    uint32_t max_completion_tokens; // Max tokens in completion
} openai_config_t;

// OpenAI provider creation/destruction
err_t openai_create(const provider_config_t* config, provider_t** out_provider);
void openai_destroy(provider_t* provider);

// Get OpenAI provider vtable
const provider_vtable_t* openai_get_vtable(void);

// OpenAI-specific functions
err_t openai_set_organization(provider_t* provider, const char* org_id);
err_t openai_set_project(provider_t* provider, const char* project_id);
err_t openai_set_include_reasoning(provider_t* provider, bool include);

// Available OpenAI models
static const char* const OPENAI_MODELS[] = {
    // GPT-4 series
    "gpt-4o",
    "gpt-4o-mini",
    "gpt-4-turbo",
    "gpt-4",

    // o1 series (reasoning models)
    "o1-preview",
    "o1-mini",

    // GPT-3.5 series
    "gpt-3.5-turbo",

    // Embeddings
    "text-embedding-3-small",
    "text-embedding-3-large",
    "text-embedding-ada-002",

    NULL
};

#endif // CCLAW_PROVIDERS_OPENAI_H