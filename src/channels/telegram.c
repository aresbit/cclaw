// telegram.c - Telegram channel implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/channel.h"
#include "utils/http.h"
#include "json_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

// Telegram channel instance data
typedef struct telegram_channel_t {
    // Configuration
    str_t bot_token;        // Telegram bot token
    str_t base_url;         // Telegram API base URL

    // HTTP client
    http_client_t* http_client;

    // Thread management
    pthread_t listener_thread;      // Thread for long polling
    bool stop_listening;            // Flag to stop the listener thread
    void (*on_message_callback)(channel_message_t* msg, void* user_data);
    void* user_data;

    // State
    uint32_t messages_sent;
    uint32_t messages_received;
    bool listening;
    uint32_t last_update_id; // Last processed update ID
} telegram_channel_t;

// Forward declarations for vtable
static str_t telegram_get_name(void);
static str_t telegram_get_version(void);
static str_t telegram_get_type(void);
static err_t telegram_create(const channel_config_t* config, channel_t** out_channel);
static void telegram_destroy(channel_t* channel);
static err_t telegram_init(channel_t* channel);
static void telegram_cleanup(channel_t* channel);
static err_t telegram_send(channel_t* channel, const str_t* message, const str_t* recipient);
static err_t telegram_send_message(channel_t* channel, const channel_message_t* message);
static err_t telegram_start_listening(channel_t* channel,
                                    void (*on_message)(channel_message_t* msg, void* user_data),
                                    void* user_data);
static err_t telegram_stop_listening(channel_t* channel);
static bool telegram_is_listening(channel_t* channel);
static err_t telegram_health_check(channel_t* channel, bool* out_healthy);
static err_t telegram_get_stats(channel_t* channel, uint32_t* messages_sent,
                              uint32_t* messages_received, uint32_t* active_connections);

// Helper functions
static err_t fetch_telegram_updates(telegram_channel_t* tg, uint32_t timeout_seconds);

// VTable definition
static const channel_vtable_t telegram_vtable = {
    .get_name = telegram_get_name,
    .get_version = telegram_get_version,
    .get_type = telegram_get_type,
    .create = telegram_create,
    .destroy = telegram_destroy,
    .init = telegram_init,
    .cleanup = telegram_cleanup,
    .send = telegram_send,
    .send_message = telegram_send_message,
    .start_listening = telegram_start_listening,
    .stop_listening = telegram_stop_listening,
    .is_listening = telegram_is_listening,
    .health_check = telegram_health_check,
    .get_stats = telegram_get_stats
};

// Get vtable
const channel_vtable_t* channel_telegram_get_vtable(void) {
    return &telegram_vtable;
}

// Helper function to build Telegram API URL
static str_t build_telegram_url(const telegram_channel_t* tg, const char* method) {
    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%.*s/%s",
             (int)tg->bot_token.len, tg->bot_token.data, method);
    return str_dup_cstr(url, NULL);
}

// Helper function to parse Telegram update and create channel message
static err_t parse_telegram_update(json_value_t* update, channel_message_t* out_msg) {
    if (!update || !out_msg || update->type != JSON_OBJECT) {
        return ERR_INVALID_ARGUMENT;
    }

    // Get update_id (required)
    json_value_t* update_id_val = json_object_get(update->object, "update_id");
    if (!update_id_val || update_id_val->type != JSON_NUMBER) {
        return ERR_INVALID_ARGUMENT;
    }

    // Get message object
    json_value_t* message_val = json_object_get(update->object, "message");
    if (!message_val || message_val->type != JSON_OBJECT) {
        // Could also be "edited_message", "channel_post", etc.
        // For now, only handle regular messages
        return ERR_NOT_IMPLEMENTED;
    }

    // Get text from message
    json_value_t* text_val = json_object_get(message_val->object, "text");
    if (!text_val || text_val->type != JSON_STRING) {
        return ERR_INVALID_ARGUMENT; // Not a text message
    }

    // Extract text content
    out_msg->content.data = strdup(text_val->string);
    out_msg->content.len = (uint32_t)strlen(text_val->string);

    // Extract sender info
    json_value_t* from_val = json_object_get(message_val->object, "from");
    if (from_val && from_val->type == JSON_OBJECT) {
        json_value_t* username_val = json_object_get(from_val->object, "username");
        json_value_t* id_val = json_object_get(from_val->object, "id");

        char sender_buf[128];
        if (username_val && username_val->type == JSON_STRING) {
            snprintf(sender_buf, sizeof(sender_buf), "@%s", username_val->string);
        } else if (id_val && id_val->type == JSON_NUMBER) {
            snprintf(sender_buf, sizeof(sender_buf), "user_%.0f", id_val->number);
        } else {
            strcpy(sender_buf, "telegram_user");
        }

        out_msg->sender.data = strdup(sender_buf);
        out_msg->sender.len = (uint32_t)strlen(sender_buf);
    } else {
        out_msg->sender.data = strdup("telegram_user");
        out_msg->sender.len = strlen("telegram_user");
    }

    // Extract chat/channel info
    json_value_t* chat_val = json_object_get(message_val->object, "chat");
    if (chat_val && chat_val->type == JSON_OBJECT) {
        json_value_t* chat_id_val = json_object_get(chat_val->object, "id");
        if (chat_id_val && chat_id_val->type == JSON_NUMBER) {
            char channel_buf[128];
            snprintf(channel_buf, sizeof(channel_buf), "telegram_%.0f", chat_id_val->number);
            out_msg->channel.data = strdup(channel_buf);
            out_msg->channel.len = (uint32_t)strlen(channel_buf);
        } else {
            out_msg->channel.data = strdup("telegram");
            out_msg->channel.len = strlen("telegram");
        }
    } else {
        out_msg->channel.data = strdup("telegram");
        out_msg->channel.len = strlen("telegram");
    }

    // Generate message ID from update_id
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "tg_%.0f", update_id_val->number);
    out_msg->id.data = strdup(id_buf);
    out_msg->id.len = (uint32_t)strlen(id_buf);

    // Extract timestamp (message date)
    json_value_t* date_val = json_object_get(message_val->object, "date");
    if (date_val && date_val->type == JSON_NUMBER) {
        // Telegram date is Unix timestamp in seconds, convert to milliseconds
        out_msg->timestamp = (uint64_t)(date_val->number * 1000);
    } else {
        out_msg->timestamp = channel_get_current_timestamp();
    }

    return ERR_OK;
}

// Listener thread function for Telegram long polling
static void* telegram_listener_thread(void* arg) {
    channel_t* channel = (channel_t*)arg;
    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    const uint32_t POLL_TIMEOUT = 30; // seconds
    const uint32_t ERROR_RETRY_DELAY = 5; // seconds

    while (!tg_data->stop_listening) {
        // Build URL for getUpdates method
        str_t url = build_telegram_url(tg_data, "getUpdates");
        if (str_empty(url)) {
            // Sleep and retry
            sleep(ERROR_RETRY_DELAY);
            continue;
        }

        // Build query parameters
        char query[256];
        snprintf(query, sizeof(query), "?timeout=%u&offset=%u",
                POLL_TIMEOUT, tg_data->last_update_id + 1);

        // Append query to URL
        char full_url[768];
        snprintf(full_url, sizeof(full_url), "%.*s%s",
                (int)url.len, url.data, query);
        free((void*)url.data);

        // Send HTTP GET request
        http_response_t* response = NULL;
        err_t err = http_get(tg_data->http_client, full_url, &response);

        if (err != ERR_OK) {
            // Network error, sleep and retry
            sleep(ERROR_RETRY_DELAY);
            continue;
        }

        if (!response || !http_response_is_success(response)) {
            if (response) {
                http_response_free(response);
            }
            sleep(ERROR_RETRY_DELAY);
            continue;
        }

        // Parse JSON response
        char* response_body = (char*)response->body.data;
        size_t body_len = response->body.len;
        char* body_copy = malloc(body_len + 1);
        if (!body_copy) {
            http_response_free(response);
            sleep(ERROR_RETRY_DELAY);
            continue;
        }
        memcpy(body_copy, response_body, body_len);
        body_copy[body_len] = '\0';

        json_value_t* root = json_parse(body_copy);
        http_response_free(response);

        if (!root || root->type != JSON_OBJECT) {
            free(body_copy);
            sleep(ERROR_RETRY_DELAY);
            continue;
        }

        // Get updates array
        json_value_t* result_val = json_object_get(root->object, "result");
        if (result_val && result_val->type == JSON_ARRAY) {
            json_array_t* array = result_val->array;
            uint32_t update_count = 0;
            uint32_t highest_update_id = tg_data->last_update_id;

            // Process each update
            while (array) {
                channel_message_t msg = {0};
                err_t parse_err = parse_telegram_update(&array->value, &msg);

                if (parse_err == ERR_OK) {
                    tg_data->messages_received++;

                    // Call callback if set
                    if (tg_data->on_message_callback) {
                        tg_data->on_message_callback(&msg, tg_data->user_data);
                    }

                    // Update highest update_id
                    // Extract update_id from message ID (format: tg_123456)
                    if (msg.id.data && msg.id.len > 3) {
                        const char* id_str = msg.id.data + 3; // Skip "tg_"
                        char* endptr;
                        uint32_t update_id = (uint32_t)strtoul(id_str, &endptr, 10);
                        if (endptr != id_str && update_id > highest_update_id) {
                            highest_update_id = update_id;
                        }
                    }

                    // Free message strings
                    free((void*)msg.id.data);
                    free((void*)msg.sender.data);
                    free((void*)msg.content.data);
                    free((void*)msg.channel.data);
                }

                array = array->next;
                update_count++;
            }

            // Update last_update_id if we processed any updates
            if (update_count > 0 && highest_update_id > tg_data->last_update_id) {
                tg_data->last_update_id = highest_update_id;
            }
        }

        json_free(root);
        free(body_copy);

        // If no updates were received (long poll timeout), loop continues
    }

    return NULL;
}

static str_t telegram_get_name(void) {
    return STR_LIT("telegram");
}

static str_t telegram_get_version(void) {
    return STR_LIT("1.0.0");
}

static str_t telegram_get_type(void) {
    return STR_LIT("telegram");
}

static err_t telegram_create(const channel_config_t* config, channel_t** out_channel) {
    if (!config || !out_channel) return ERR_INVALID_ARGUMENT;

    channel_t* channel = channel_alloc(&telegram_vtable);
    if (!channel) return ERR_OUT_OF_MEMORY;

    telegram_channel_t* tg_data = calloc(1, sizeof(telegram_channel_t));
    if (!tg_data) {
        channel_free(channel);
        return ERR_OUT_OF_MEMORY;
    }

    // Initialize telegram data
    tg_data->bot_token = STR_NULL;
    tg_data->base_url = STR_LIT("https://api.telegram.org");
    tg_data->http_client = NULL;
    tg_data->listener_thread = 0;
    tg_data->stop_listening = true;
    tg_data->on_message_callback = NULL;
    tg_data->user_data = NULL;
    tg_data->messages_sent = 0;
    tg_data->messages_received = 0;
    tg_data->listening = false;
    tg_data->last_update_id = 0;

    // Copy configuration
    channel->config = *config;
    if (str_empty(channel->config.name)) {
        channel->config.name = str_dup_cstr("telegram", NULL);
    }

    // Extract bot token from auth_token
    if (!str_empty(config->auth_token)) {
        tg_data->bot_token = str_dup(config->auth_token, NULL);
    }

    channel->impl_data = tg_data;
    channel->initialized = false;
    channel->listening = false;

    *out_channel = channel;
    return ERR_OK;
}

static void telegram_destroy(channel_t* channel) {
    if (!channel || !channel->impl_data) return;

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    // Stop listening if active
    if (channel->listening) {
        telegram_stop_listening(channel);
    }

    // Cleanup will free resources if initialized
    if (channel->initialized) {
        telegram_cleanup(channel);
    }

    // Free bot token
    free((void*)tg_data->bot_token.data);

    // Free configuration strings
    free((void*)channel->config.name.data);
    free((void*)channel->config.type.data);
    free((void*)channel->config.auth_token.data);
    free((void*)channel->config.webhook_url.data);
    free((void*)channel->config.host.data);

    free(tg_data);
    channel->impl_data = NULL;

    channel_free(channel);
}

static err_t telegram_init(channel_t* channel) {
    if (!channel || !channel->impl_data) return ERR_INVALID_ARGUMENT;
    if (channel->initialized) return ERR_OK;

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    // Create HTTP client
    http_client_config_t http_config = http_client_default_config();
    tg_data->http_client = http_client_create(&http_config);
    if (!tg_data->http_client) {
        return ERR_NETWORK;
    }

    // Check if bot token is configured
    if (str_empty(tg_data->bot_token)) {
        // Try to get from config auth_token again
        if (!str_empty(channel->config.auth_token)) {
            tg_data->bot_token = str_dup(channel->config.auth_token, NULL);
        }
    }

    channel->initialized = true;
    return ERR_OK;
}

static void telegram_cleanup(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) return;

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    // Cleanup HTTP client
    if (tg_data->http_client) {
        http_client_destroy(tg_data->http_client);
        tg_data->http_client = NULL;
    }

    channel->initialized = false;
}

static err_t telegram_send(channel_t* channel, const str_t* message, const str_t* recipient) {
    if (!channel || !channel->impl_data || !channel->initialized || !message) {
        return ERR_INVALID_ARGUMENT;
    }

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    // Check if bot token is configured
    if (str_empty(tg_data->bot_token)) {
        return ERR_CHANNEL; // No bot token configured
    }

    // Build URL for sendMessage method
    str_t url = build_telegram_url(tg_data, "sendMessage");
    if (str_empty(url)) {
        return ERR_OUT_OF_MEMORY;
    }

    // Build JSON payload
    char json_buffer[2048];
    int written;
    if (recipient && !str_empty(*recipient)) {
        // Send to specific chat ID
        written = snprintf(json_buffer, sizeof(json_buffer),
                          "{\"chat_id\": \"%.*s\", \"text\": \"%.*s\"}",
                          (int)recipient->len, recipient->data,
                          (int)message->len, message->data);
    } else {
        // No recipient specified - need default chat ID
        // TODO: Use default chat ID from configuration
        free((void*)url.data);
        return ERR_INVALID_ARGUMENT;
    }

    if (written < 0 || written >= (int)sizeof(json_buffer)) {
        free((void*)url.data);
        return ERR_OUT_OF_MEMORY;
    }

    // Send HTTP POST request
    http_response_t* response = NULL;
    err_t err = http_post_json(tg_data->http_client, url.data, json_buffer, &response);
    free((void*)url.data);

    if (err != ERR_OK) {
        return err;
    }

    // Check response status
    bool success = http_response_is_success(response);
    http_response_free(response);

    if (success) {
        tg_data->messages_sent++;
        return ERR_OK;
    } else {
        return ERR_NETWORK;
    }
}

static err_t telegram_send_message(channel_t* channel, const channel_message_t* message) {
    if (!channel || !channel->impl_data || !channel->initialized || !message) {
        return ERR_INVALID_ARGUMENT;
    }

    // For now, just use sender as recipient? Or extract from message
    // Telegram messages need chat_id
    return telegram_send(channel, &message->content, &message->sender);
}

static err_t telegram_start_listening(channel_t* channel,
                                    void (*on_message)(channel_message_t* msg, void* user_data),
                                    void* user_data) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    if (channel->listening) {
        return ERR_OK; // Already listening
    }

    // Set callback
    tg_data->on_message_callback = on_message;
    tg_data->user_data = user_data;
    tg_data->stop_listening = false;

    // Create listener thread
    int result = pthread_create(&tg_data->listener_thread, NULL,
                                telegram_listener_thread, channel);
    if (result != 0) {
        tg_data->stop_listening = true;
        tg_data->on_message_callback = NULL;
        tg_data->user_data = NULL;
        return ERR_CHANNEL;
    }

    channel->listening = true;
    tg_data->listening = true;

    return ERR_OK;
}

static err_t telegram_stop_listening(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    if (!channel->listening) {
        return ERR_OK; // Not listening
    }

    // Signal thread to stop
    tg_data->stop_listening = true;

    // Wait for thread to finish
    if (tg_data->listener_thread) {
        pthread_join(tg_data->listener_thread, NULL);
        tg_data->listener_thread = 0;
    }

    // Clear callback
    tg_data->on_message_callback = NULL;
    tg_data->user_data = NULL;

    channel->listening = false;
    tg_data->listening = false;

    return ERR_OK;
}

static bool telegram_is_listening(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return false;
    }

    return channel->listening;
}

static err_t telegram_health_check(channel_t* channel, bool* out_healthy) {
    if (!channel || !channel->impl_data || !channel->initialized || !out_healthy) {
        return ERR_INVALID_ARGUMENT;
    }

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    // Basic health check: bot token present and HTTP client initialized
    bool healthy = channel->initialized && tg_data->http_client != NULL &&
                   !str_empty(tg_data->bot_token);

    *out_healthy = healthy;
    return ERR_OK;
}

static err_t telegram_get_stats(channel_t* channel, uint32_t* messages_sent,
                              uint32_t* messages_received, uint32_t* active_connections) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    telegram_channel_t* tg_data = (telegram_channel_t*)channel->impl_data;

    if (messages_sent) *messages_sent = tg_data->messages_sent;
    if (messages_received) *messages_received = tg_data->messages_received;
    if (active_connections) *active_connections = tg_data->listening ? 1 : 0;

    return ERR_OK;
}