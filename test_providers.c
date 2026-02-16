// test_providers.c - Test AI Provider implementations
#include "providers/base.h"
#include "providers/deepseek.h"
#include "providers/kimi.h"
#include "providers/openrouter.h"
#include "utils/http.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("AI Provider Test\n");
    printf("================\n\n");

    // Initialize HTTP
    http_init();
    printf("✓ HTTP initialized\n\n");

    // Test DeepSeek
    printf("=== DeepSeek Provider ===\n");
    {
        provider_config_t config = {
            .api_key = STR_LIT("sk-test-key"),  // Replace with real key to test
            .default_model = STR_LIT(DEFAULT_DEEPSEEK_MODEL),
            .timeout_ms = 30000
        };

        provider_t* deepseek = NULL;
        err_t err = deepseek_create(&config, &deepseek);

        if (err == ERR_OK && deepseek) {
            printf("✓ DeepSeek provider created\n");
            printf("  Name: %.*s\n", (int)deepseek->vtable->get_name().len, deepseek->vtable->get_name().data);
            printf("  Version: %.*s\n", (int)deepseek->vtable->get_version().len, deepseek->vtable->get_version().data);

            // Test health check
            bool healthy = false;
            err = deepseek->vtable->health_check(deepseek, &healthy);
            printf("  Health check: %s\n", healthy ? "✓ healthy" : "✗ unavailable (expected without valid API key)");

            // Test list models
            str_t* models = NULL;
            uint32_t model_count = 0;
            err = deepseek->vtable->list_models(deepseek, &models, &model_count);
            if (err == ERR_OK && models) {
                printf("  Available models (%u):\n", model_count);
                for (uint32_t i = 0; i < model_count && i < 3; i++) {
                    printf("    - %.*s\n", (int)models[i].len, models[i].data);
                }
                for (uint32_t i = 0; i < model_count; i++) free((void*)models[i].data);
                free(models);
            }

            deepseek_destroy(deepseek);
            printf("✓ DeepSeek provider destroyed\n");
        } else {
            printf("✗ Failed to create DeepSeek provider: %d\n", err);
        }
    }
    printf("\n");

    // Test Kimi
    printf("=== Kimi (Moonshot) Provider ===\n");
    {
        provider_config_t config = {
            .api_key = STR_LIT("sk-test-key"),
            .default_model = STR_LIT(DEFAULT_KIMI_MODEL),
            .timeout_ms = 30000
        };

        provider_t* kimi = NULL;
        err_t err = kimi_create(&config, &kimi);

        if (err == ERR_OK && kimi) {
            printf("✓ Kimi provider created\n");
            printf("  Name: %.*s\n", (int)kimi->vtable->get_name().len, kimi->vtable->get_name().data);

            str_t* models = NULL;
            uint32_t model_count = 0;
            err = kimi->vtable->list_models(kimi, &models, &model_count);
            if (err == ERR_OK && models) {
                printf("  Available models (%u):\n", model_count);
                for (uint32_t i = 0; i < model_count; i++) {
                    printf("    - %.*s\n", (int)models[i].len, models[i].data);
                }
                for (uint32_t i = 0; i < model_count; i++) free((void*)models[i].data);
                free(models);
            }

            kimi_destroy(kimi);
            printf("✓ Kimi provider destroyed\n");
        } else {
            printf("✗ Failed to create Kimi provider: %d\n", err);
        }
    }
    printf("\n");

    // Test OpenRouter
    printf("=== OpenRouter Provider ===\n");
    {
        provider_config_t config = {
            .api_key = STR_LIT("sk-or-test-key"),
            .default_model = STR_LIT(DEFAULT_OPENROUTER_MODEL),
            .timeout_ms = 30000
        };

        provider_t* openrouter = NULL;
        err_t err = openrouter_create(&config, &openrouter);

        if (err == ERR_OK && openrouter) {
            printf("✓ OpenRouter provider created\n");
            printf("  Name: %.*s\n", (int)openrouter->vtable->get_name().len, openrouter->vtable->get_name().data);

            str_t* models = NULL;
            uint32_t model_count = 0;
            err = openrouter->vtable->list_models(openrouter, &models, &model_count);
            if (err == ERR_OK && models) {
                printf("  Popular models (%u shown):\n", model_count > 5 ? 5 : model_count);
                for (uint32_t i = 0; i < model_count && i < 5; i++) {
                    printf("    - %.*s\n", (int)models[i].len, models[i].data);
                }
                for (uint32_t i = 0; i < model_count; i++) free((void*)models[i].data);
                free(models);
            }

            openrouter_destroy(openrouter);
            printf("✓ OpenRouter provider destroyed\n");
        } else {
            printf("✗ Failed to create OpenRouter provider: %d\n", err);
        }
    }
    printf("\n");

    http_shutdown();
    printf("✓ All provider tests completed\n");
    return 0;
}
