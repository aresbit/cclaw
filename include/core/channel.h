// channel.h - Channel system interface for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_CHANNEL_H
#define CCLAW_CORE_CHANNEL_H

#include "core/types.h"
#include "core/error.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct channel_t channel_t;
typedef struct channel_vtable_t channel_vtable_t;

// Channel message structure (defined in core/types.h)
// typedef struct channel_message_t channel_message_t;

// Channel configuration
typedef struct channel_config_t {
    str_t name;         // Channel name/identifier
    str_t type;         // Channel type (cli, telegram, discord, etc.)
    str_t auth_token;   // Authentication token (if needed)
    str_t webhook_url;  // Webhook URL (for webhook channels)
    uint16_t port;      // Port to listen on (for server channels)
    str_t host;         // Host to bind to (for server channels)
    bool auto_start;    // Auto-start listening on initialization
} channel_config_t;

// Channel VTable - defines the channel interface
struct channel_vtable_t {
    // Channel identification
    str_t (*get_name)(void);
    str_t (*get_version)(void);
    str_t (*get_type)(void);  // "cli", "telegram", "discord", etc.

    // Lifecycle
    err_t (*create)(const channel_config_t* config, channel_t** out_channel);
    void (*destroy)(channel_t* channel);

    // Initialization and cleanup
    err_t (*init)(channel_t* channel);
    void (*cleanup)(channel_t* channel);

    // Message handling
    err_t (*send)(channel_t* channel, const str_t* message, const str_t* recipient);
    err_t (*send_message)(channel_t* channel, const channel_message_t* message);

    // Listening/connection management
    err_t (*start_listening)(channel_t* channel,
                            void (*on_message)(channel_message_t* msg, void* user_data),
                            void* user_data);
    err_t (*stop_listening)(channel_t* channel);
    bool (*is_listening)(channel_t* channel);

    // Health and status
    err_t (*health_check)(channel_t* channel, bool* out_healthy);
    err_t (*get_stats)(channel_t* channel, uint32_t* messages_sent,
                       uint32_t* messages_received, uint32_t* active_connections);
};

// Channel instance structure
struct channel_t {
    const channel_vtable_t* vtable;
    channel_config_t config;
    void* impl_data;      // Channel-specific data
    bool initialized;
    bool listening;
};

// Helper macros for channel implementation
#define CHANNEL_IMPLEMENT(name, vtable_ptr) \
    const channel_vtable_t* name##_get_vtable(void) { return vtable_ptr; }

// Global channel registry (similar to provider and memory registries)
err_t channel_registry_init(void);
void channel_registry_shutdown(void);
err_t channel_register(const char* name, const channel_vtable_t* vtable);
err_t channel_create(const char* name, const channel_config_t* config, channel_t** out_channel);
err_t channel_registry_list(const char*** out_names, uint32_t* out_count);

// Built-in channels
const channel_vtable_t* channel_cli_get_vtable(void);
const channel_vtable_t* channel_telegram_get_vtable(void);
const channel_vtable_t* channel_discord_get_vtable(void);
const channel_vtable_t* channel_webhook_get_vtable(void);

// Channel creation helpers
channel_t* channel_alloc(const channel_vtable_t* vtable);
void channel_free(channel_t* channel);

// Message helpers
channel_message_t* channel_message_create(const str_t* id, const str_t* sender,
                                         const str_t* content, const str_t* channel);
void channel_message_free(channel_message_t* message);
void channel_message_array_free(channel_message_t* messages, uint32_t count);

// Utility functions
str_t channel_generate_message_id(void);
uint64_t channel_get_current_timestamp(void);
channel_config_t channel_config_default(void);

// Channel manager (for handling multiple channels)
typedef struct channel_manager_t channel_manager_t;

channel_manager_t* channel_manager_create(void);
void channel_manager_destroy(channel_manager_t* manager);
err_t channel_manager_add_channel(channel_manager_t* manager, channel_t* channel);
err_t channel_manager_remove_channel(channel_manager_t* manager, const str_t* channel_name);
err_t channel_manager_send_to_all(channel_manager_t* manager, const str_t* message);
err_t channel_manager_send_to_channel(channel_manager_t* manager, const str_t* channel_name,
                                      const str_t* message);
err_t channel_manager_start_all(channel_manager_t* manager,
                               void (*on_message)(channel_message_t* msg, void* user_data),
                               void* user_data);
err_t channel_manager_stop_all(channel_manager_t* manager);

#endif // CCLAW_CORE_CHANNEL_H