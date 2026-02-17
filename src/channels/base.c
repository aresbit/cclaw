// base.c - Channel base implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Channel registry (similar to provider and memory registries)
typedef struct {
    const char* name;
    const channel_vtable_t* vtable;
} channel_entry_t;

#define MAX_CHANNELS 16
static channel_entry_t g_registry[MAX_CHANNELS];
static uint32_t g_channel_count = 0;
static bool g_registry_initialized = false;

err_t channel_registry_init(void) {
    if (g_registry_initialized) return ERR_OK;

    memset(g_registry, 0, sizeof(g_registry));
    g_channel_count = 0;
    g_registry_initialized = true;

    // Register built-in channels
    // These will be registered when their respective modules are implemented
    channel_register("cli", channel_cli_get_vtable());
    channel_register("telegram", channel_telegram_get_vtable());
    // channel_register("discord", channel_discord_get_vtable());
    channel_register("webhook", channel_webhook_get_vtable());

    return ERR_OK;
}

void channel_registry_shutdown(void) {
    memset(g_registry, 0, sizeof(g_registry));
    g_channel_count = 0;
    g_registry_initialized = false;
}

err_t channel_register(const char* name, const channel_vtable_t* vtable) {
    if (!name || !vtable) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) channel_registry_init();
    if (g_channel_count >= MAX_CHANNELS) return ERR_OUT_OF_MEMORY;

    // Check for duplicates
    for (uint32_t i = 0; i < g_channel_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return ERR_INVALID_ARGUMENT; // Already registered
        }
    }

    g_registry[g_channel_count].name = name;
    g_registry[g_channel_count].vtable = vtable;
    g_channel_count++;

    return ERR_OK;
}

err_t channel_create(const char* name, const channel_config_t* config, channel_t** out_channel) {
    if (!name || !config || !out_channel) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) channel_registry_init();

    for (uint32_t i = 0; i < g_channel_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            return g_registry[i].vtable->create(config, out_channel);
        }
    }

    return ERR_NOT_FOUND;
}

err_t channel_registry_list(const char*** out_names, uint32_t* out_count) {
    if (!out_names || !out_count) return ERR_INVALID_ARGUMENT;
    if (!g_registry_initialized) channel_registry_init();

    static const char* names[MAX_CHANNELS];
    for (uint32_t i = 0; i < g_channel_count; i++) {
        names[i] = g_registry[i].name;
    }

    *out_names = names;
    *out_count = g_channel_count;

    return ERR_OK;
}

// Channel helpers
channel_t* channel_alloc(const channel_vtable_t* vtable) {
    channel_t* channel = calloc(1, sizeof(channel_t));
    if (channel) {
        channel->vtable = vtable;
    }
    return channel;
}

void channel_free(channel_t* channel) {
    if (!channel) return;
    if (channel->vtable && channel->vtable->destroy) {
        channel->vtable->destroy(channel);
    } else {
        free(channel);
    }
}

// Message helpers
channel_message_t* channel_message_create(const str_t* id, const str_t* sender,
                                         const str_t* content, const str_t* channel) {
    channel_message_t* msg = calloc(1, sizeof(channel_message_t));
    if (!msg) return NULL;

    if (id && !str_empty(*id)) {
        msg->id.data = strdup(id->data);
        msg->id.len = id->len;
    }

    if (sender && !str_empty(*sender)) {
        msg->sender.data = strdup(sender->data);
        msg->sender.len = sender->len;
    }

    if (content && !str_empty(*content)) {
        msg->content.data = strdup(content->data);
        msg->content.len = content->len;
    }

    if (channel && !str_empty(*channel)) {
        msg->channel.data = strdup(channel->data);
        msg->channel.len = channel->len;
    }

    msg->timestamp = channel_get_current_timestamp();

    return msg;
}

void channel_message_free(channel_message_t* message) {
    if (!message) return;

    free((void*)message->id.data);
    free((void*)message->sender.data);
    free((void*)message->content.data);
    free((void*)message->channel.data);

    free(message);
}

void channel_message_array_free(channel_message_t* messages, uint32_t count) {
    if (!messages) return;

    for (uint32_t i = 0; i < count; i++) {
        free((void*)messages[i].id.data);
        free((void*)messages[i].sender.data);
        free((void*)messages[i].content.data);
        free((void*)messages[i].channel.data);
    }

    free(messages);
}

// Utility functions
str_t channel_generate_message_id(void) {
    // Simple UUID-like generation for now
    // TODO: Use proper UUID generation
    static uint32_t counter = 0;
    char buffer[64];
    time_t now = time(NULL);
    uint32_t seq = __sync_fetch_and_add(&counter, 1);

    snprintf(buffer, sizeof(buffer), "msg_%lu_%u_%u",
             (unsigned long)now, (unsigned)getpid(), seq);

    return str_dup_cstr(buffer, NULL);
}

uint64_t channel_get_current_timestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

channel_config_t channel_config_default(void) {
    return (channel_config_t){
        .name = STR_NULL,
        .type = STR_NULL,
        .auth_token = STR_NULL,
        .webhook_url = STR_NULL,
        .port = 8080,
        .host = STR_LIT("127.0.0.1"),
        .auto_start = true
    };
}

// Channel manager implementation
struct channel_manager_t {
    channel_t** channels;
    uint32_t channel_count;
    uint32_t channel_capacity;
};

channel_manager_t* channel_manager_create(void) {
    channel_manager_t* manager = calloc(1, sizeof(channel_manager_t));
    if (!manager) return NULL;

    manager->channel_capacity = 8;
    manager->channels = calloc(manager->channel_capacity, sizeof(channel_t*));
    if (!manager->channels) {
        free(manager);
        return NULL;
    }

    return manager;
}

void channel_manager_destroy(channel_manager_t* manager) {
    if (!manager) return;

    // Stop all channels first
    channel_manager_stop_all(manager);

    // Free all channels
    for (uint32_t i = 0; i < manager->channel_count; i++) {
        channel_free(manager->channels[i]);
    }

    free(manager->channels);
    free(manager);
}

err_t channel_manager_add_channel(channel_manager_t* manager, channel_t* channel) {
    if (!manager || !channel) return ERR_INVALID_ARGUMENT;

    // Check if channel already exists
    for (uint32_t i = 0; i < manager->channel_count; i++) {
        if (manager->channels[i] == channel) {
            return ERR_ALREADY_EXISTS;
        }
    }

    // Resize if needed
    if (manager->channel_count >= manager->channel_capacity) {
        uint32_t new_capacity = manager->channel_capacity * 2;
        channel_t** new_channels = realloc(manager->channels, new_capacity * sizeof(channel_t*));
        if (!new_channels) return ERR_OUT_OF_MEMORY;

        manager->channels = new_channels;
        manager->channel_capacity = new_capacity;
    }

    manager->channels[manager->channel_count] = channel;
    manager->channel_count++;

    return ERR_OK;
}

err_t channel_manager_remove_channel(channel_manager_t* manager, const str_t* channel_name) {
    if (!manager || !channel_name) return ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < manager->channel_count; i++) {
        channel_t* channel = manager->channels[i];
        if (str_equal(channel->config.name, *channel_name)) {
            // Stop the channel if it's listening
            if (channel->listening && channel->vtable->stop_listening) {
                channel->vtable->stop_listening(channel);
            }

            // Free the channel
            channel_free(channel);

            // Shift remaining channels
            for (uint32_t j = i; j < manager->channel_count - 1; j++) {
                manager->channels[j] = manager->channels[j + 1];
            }

            manager->channel_count--;
            return ERR_OK;
        }
    }

    return ERR_NOT_FOUND;
}

err_t channel_manager_send_to_all(channel_manager_t* manager, const str_t* message) {
    if (!manager || !message) return ERR_INVALID_ARGUMENT;

    err_t last_error = ERR_OK;

    for (uint32_t i = 0; i < manager->channel_count; i++) {
        channel_t* channel = manager->channels[i];
        if (channel->initialized && channel->vtable->send) {
            err_t err = channel->vtable->send(channel, message, NULL);
            if (err != ERR_OK) {
                last_error = err;
                // Continue trying other channels
            }
        }
    }

    return last_error;
}

err_t channel_manager_send_to_channel(channel_manager_t* manager, const str_t* channel_name,
                                      const str_t* message) {
    if (!manager || !channel_name || !message) return ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < manager->channel_count; i++) {
        channel_t* channel = manager->channels[i];
        if (str_equal(channel->config.name, *channel_name)) {
            if (channel->initialized && channel->vtable->send) {
                return channel->vtable->send(channel, message, NULL);
            }
            return ERR_CHANNEL;
        }
    }

    return ERR_NOT_FOUND;
}

err_t channel_manager_start_all(channel_manager_t* manager,
                               void (*on_message)(channel_message_t* msg, void* user_data),
                               void* user_data) {
    if (!manager) return ERR_INVALID_ARGUMENT;

    err_t last_error = ERR_OK;

    for (uint32_t i = 0; i < manager->channel_count; i++) {
        channel_t* channel = manager->channels[i];
        if (channel->initialized && !channel->listening && channel->vtable->start_listening) {
            err_t err = channel->vtable->start_listening(channel, on_message, user_data);
            if (err != ERR_OK) {
                last_error = err;
                // Continue trying other channels
            } else {
                channel->listening = true;
            }
        }
    }

    return last_error;
}

err_t channel_manager_stop_all(channel_manager_t* manager) {
    if (!manager) return ERR_INVALID_ARGUMENT;

    err_t last_error = ERR_OK;

    for (uint32_t i = 0; i < manager->channel_count; i++) {
        channel_t* channel = manager->channels[i];
        if (channel->listening && channel->vtable->stop_listening) {
            err_t err = channel->vtable->stop_listening(channel);
            if (err != ERR_OK) {
                last_error = err;
                // Continue trying other channels
            } else {
                channel->listening = false;
            }
        }
    }

    return last_error;
}