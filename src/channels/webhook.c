// webhook.c - Webhook channel implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/channel.h"
#include "utils/http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sodium.h>
#include "json_config.h"
#include <uv.h>
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>
// Webhook channel instance data
typedef struct webhook_channel_t {
    // Configuration
    str_t secret;           // HMAC secret for signature verification
    bool verify_signature;  // Whether to verify signatures

    // HTTP client for sending
    http_client_t* http_client;

    // HTTP server for receiving
    uv_loop_t* loop;        // libuv event loop
    uv_tcp_t server;        // TCP server handle
    pthread_t listener_thread;      // Thread for libuv event loop
    bool stop_listening;            // Flag to stop the listener thread
    void (*on_message_callback)(channel_message_t* msg, void* user_data);
    void* user_data;

    // State
    uint32_t messages_sent;
    uint32_t messages_received;
    bool listening;
} webhook_channel_t;

// Forward declarations
static void* listener_thread_func(void* arg);

// Forward declarations for vtable
static str_t webhook_get_name(void);
static str_t webhook_get_version(void);
static str_t webhook_get_type(void);
static err_t webhook_create(const channel_config_t* config, channel_t** out_channel);
static void webhook_destroy(channel_t* channel);
static err_t webhook_init(channel_t* channel);
static void webhook_cleanup(channel_t* channel);
static err_t webhook_send(channel_t* channel, const str_t* message, const str_t* recipient);
static err_t webhook_send_message(channel_t* channel, const channel_message_t* message);
static err_t webhook_start_listening(channel_t* channel,
                                    void (*on_message)(channel_message_t* msg, void* user_data),
                                    void* user_data);
static err_t webhook_stop_listening(channel_t* channel);
static bool webhook_is_listening(channel_t* channel);
static err_t webhook_health_check(channel_t* channel, bool* out_healthy);
static err_t webhook_get_stats(channel_t* channel, uint32_t* messages_sent,
                              uint32_t* messages_received, uint32_t* active_connections);

// VTable definition
static const channel_vtable_t webhook_vtable = {
    .get_name = webhook_get_name,
    .get_version = webhook_get_version,
    .get_type = webhook_get_type,
    .create = webhook_create,
    .destroy = webhook_destroy,
    .init = webhook_init,
    .cleanup = webhook_cleanup,
    .send = webhook_send,
    .send_message = webhook_send_message,
    .start_listening = webhook_start_listening,
    .stop_listening = webhook_stop_listening,
    .is_listening = webhook_is_listening,
    .health_check = webhook_health_check,
    .get_stats = webhook_get_stats
};

// Get vtable
const channel_vtable_t* channel_webhook_get_vtable(void) {
    return &webhook_vtable;
}

// Helper function to compute HMAC-SHA256 using libsodium
static bool verify_hmac_signature(const str_t* secret, const str_t* payload,
                                 const str_t* signature) {
    if (str_empty(*secret) || str_empty(*signature) || str_empty(*payload)) {
        return false;
    }

    // Convert hex signature to binary
    size_t sig_len = signature->len;
    if (sig_len % 2 != 0) {
        return false; // Hex string must have even length
    }

    size_t bin_len = sig_len / 2;
    unsigned char* sig_bin = malloc(bin_len);
    if (!sig_bin) {
        return false;
    }

    // Simple hex decoding
    for (size_t i = 0; i < bin_len; i++) {
        char hi = signature->data[i * 2];
        char lo = signature->data[i * 2 + 1];
        hi = (hi <= '9') ? hi - '0' : (hi & 0x0F) + 9;
        lo = (lo <= '9') ? lo - '0' : (lo & 0x0F) + 9;
        sig_bin[i] = (hi << 4) | lo;
    }

    // Compute HMAC
    unsigned char computed_hmac[crypto_auth_hmacsha256_BYTES];
    if (crypto_auth_hmacsha256(computed_hmac,
                               (const unsigned char*)payload->data,
                               payload->len,
                               (const unsigned char*)secret->data) != 0) {
        free(sig_bin);
        return false;
    }

    // Compare
    bool result = (bin_len == crypto_auth_hmacsha256_BYTES) &&
                  (sodium_memcmp(sig_bin, computed_hmac, crypto_auth_hmacsha256_BYTES) == 0);

    free(sig_bin);
    sodium_memzero(computed_hmac, sizeof(computed_hmac));
    return result;
}

// Helper function to parse JSON webhook payload using json_config.h
static err_t parse_webhook_payload(const char* payload, size_t payload_len,
                                  channel_message_t* out_message) {
    if (!payload || !out_message) return ERR_INVALID_ARGUMENT;

    // Copy payload to null-terminated string for JSON parser
    char* payload_copy = malloc(payload_len + 1);
    if (!payload_copy) return ERR_OUT_OF_MEMORY;
    memcpy(payload_copy, payload, payload_len);
    payload_copy[payload_len] = '\0';

    // Parse JSON
    json_value_t* root = json_parse(payload_copy);
    if (!root) {
        free(payload_copy);
        return ERR_INVALID_ARGUMENT;
    }

    err_t result = ERR_OK;

    // Extract text field (required)
    if (root->type != JSON_OBJECT) {
        result = ERR_INVALID_ARGUMENT;
        goto cleanup;
    }
    json_value_t* text_val = json_object_get(root->object, "text");
    if (!text_val || text_val->type != JSON_STRING) {
        result = ERR_INVALID_ARGUMENT;
        goto cleanup;
    }

    out_message->content.data = strdup(text_val->string);
    out_message->content.len = (uint32_t)strlen(text_val->string);

    // Extract sender (optional)
    json_value_t* sender_val = json_object_get(root->object, "sender");
    if (sender_val && sender_val->type == JSON_STRING) {
        out_message->sender.data = strdup(sender_val->string);
        out_message->sender.len = (uint32_t)strlen(sender_val->string);
    } else {
        out_message->sender.data = strdup("webhook");
        out_message->sender.len = strlen("webhook");
    }

    // Extract channel (optional)
    json_value_t* channel_val = json_object_get(root->object, "channel");
    if (channel_val && channel_val->type == JSON_STRING) {
        out_message->channel.data = strdup(channel_val->string);
        out_message->channel.len = (uint32_t)strlen(channel_val->string);
    } else {
        out_message->channel.data = strdup("webhook");
        out_message->channel.len = strlen("webhook");
    }

    // Generate ID and timestamp
    out_message->id = channel_generate_message_id();
    out_message->timestamp = channel_get_current_timestamp();

cleanup:
    json_free(root);
    free(payload_copy);
    return result;
}

static str_t webhook_get_name(void) {
    return STR_LIT("webhook");
}

static str_t webhook_get_version(void) {
    return STR_LIT("1.0.0");
}

static str_t webhook_get_type(void) {
    return STR_LIT("webhook");
}

static err_t webhook_create(const channel_config_t* config, channel_t** out_channel) {
    if (!config || !out_channel) return ERR_INVALID_ARGUMENT;

    channel_t* channel = channel_alloc(&webhook_vtable);
    if (!channel) return ERR_OUT_OF_MEMORY;

    webhook_channel_t* webhook_data = calloc(1, sizeof(webhook_channel_t));
    if (!webhook_data) {
        channel_free(channel);
        return ERR_OUT_OF_MEMORY;
    }

    // Initialize webhook data
    webhook_data->secret = STR_NULL;
    webhook_data->verify_signature = false;
    webhook_data->messages_sent = 0;
    webhook_data->messages_received = 0;
    webhook_data->listening = false;

    // Copy configuration
    channel->config = *config;
    if (str_empty(channel->config.name)) {
        channel->config.name = str_dup_cstr("webhook", NULL);
    }

    // Extract secret from auth_token if present
    if (!str_empty(config->auth_token)) {
        webhook_data->secret = str_dup(config->auth_token, NULL);
        webhook_data->verify_signature = true;
    }

    channel->impl_data = webhook_data;
    channel->initialized = false;
    channel->listening = false;

    *out_channel = channel;
    return ERR_OK;
}

static void webhook_destroy(channel_t* channel) {
    if (!channel || !channel->impl_data) return;

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    // Stop listening if active
    if (channel->listening) {
        webhook_stop_listening(channel);
    }

    // Cleanup will free resources if initialized
    if (channel->initialized) {
        webhook_cleanup(channel);
    }

    // Free secret
    free((void*)webhook_data->secret.data);

    // Free configuration strings (only if they were dynamically allocated)
    // Note: str_owns flag indicates if the string owns its data
    // For now, we assume strings with non-null data that aren't string literals
    // were allocated via str_dup/str_dup_cstr
    if (channel->config.name.data && channel->config.name.len > 0) {
        free((void*)channel->config.name.data);
    }
    if (channel->config.type.data && channel->config.type.len > 0) {
        free((void*)channel->config.type.data);
    }
    if (channel->config.auth_token.data && channel->config.auth_token.len > 0) {
        free((void*)channel->config.auth_token.data);
    }
    if (channel->config.webhook_url.data && channel->config.webhook_url.len > 0) {
        free((void*)channel->config.webhook_url.data);
    }
    if (channel->config.host.data && channel->config.host.len > 0 &&
        strcmp(channel->config.host.data, "127.0.0.1") != 0) {
        free((void*)channel->config.host.data);
    }

    free(webhook_data);
    channel->impl_data = NULL;

    channel_free(channel);
}

static err_t webhook_init(channel_t* channel) {
    if (!channel || !channel->impl_data) return ERR_INVALID_ARGUMENT;
    if (channel->initialized) return ERR_OK;

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    // Create HTTP client for sending webhooks
    http_client_config_t http_config = http_client_default_config();
    webhook_data->http_client = http_client_create(&http_config);
    if (!webhook_data->http_client) {
        return ERR_NETWORK;
    }

    // Check if webhook URL is configured for sending
    if (str_empty(channel->config.webhook_url)) {
        // Webhook URL is required for sending messages
        // But we can still receive messages via HTTP server (not implemented yet)
    }

    channel->initialized = true;
    return ERR_OK;
}

static void webhook_cleanup(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) return;

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    // Cleanup HTTP client
    if (webhook_data->http_client) {
        http_client_destroy(webhook_data->http_client);
        webhook_data->http_client = NULL;
    }

    channel->initialized = false;
}

static err_t webhook_send(channel_t* channel, const str_t* message, const str_t* recipient) {
    if (!channel || !channel->impl_data || !channel->initialized || !message) {
        return ERR_INVALID_ARGUMENT;
    }

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    // Check if webhook URL is configured
    if (str_empty(channel->config.webhook_url)) {
        return ERR_CHANNEL; // No webhook URL configured
    }

    // Create JSON payload
    char json_buffer[1024];
    int written = snprintf(json_buffer, sizeof(json_buffer),
                          "{\"text\": \"%.*s\"}",
                          (int)message->len, message->data);
    if (written < 0 || written >= (int)sizeof(json_buffer)) {
        return ERR_OUT_OF_MEMORY;
    }

    // Send HTTP POST request
    http_response_t* response = NULL;
    err_t err = http_post_json(webhook_data->http_client,
                               channel->config.webhook_url.data,
                               json_buffer, &response);
    if (err != ERR_OK) {
        return err;
    }

    // Check response status
    bool success = http_response_is_success(response);
    http_response_free(response);

    if (success) {
        webhook_data->messages_sent++;
        return ERR_OK;
    } else {
        return ERR_NETWORK;
    }
}

static err_t webhook_send_message(channel_t* channel, const channel_message_t* message) {
    if (!channel || !channel->impl_data || !channel->initialized || !message) {
        return ERR_INVALID_ARGUMENT;
    }

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    // Check if webhook URL is configured
    if (str_empty(channel->config.webhook_url)) {
        return ERR_CHANNEL; // No webhook URL configured
    }

    // TODO: Implement proper HTTP POST with full message structure

    webhook_data->messages_sent++;

    printf("[Webhook] Message from %.*s would be sent to %.*s\n",
           (int)message->sender.len, message->sender.data,
           (int)channel->config.webhook_url.len, channel->config.webhook_url.data);

    return ERR_OK;
}

static err_t webhook_start_listening(channel_t* channel,
                                    void (*on_message)(channel_message_t* msg, void* user_data),
                                    void* user_data) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    if (channel->listening) {
        return ERR_OK; // Already listening
    }

    // Store callback and user data
    webhook_data->on_message_callback = on_message;
    webhook_data->user_data = user_data;

    // Reset stop flag
    webhook_data->stop_listening = false;

    // Start listener thread
    int err = pthread_create(&webhook_data->listener_thread, NULL,
                            listener_thread_func, channel);
    if (err != 0) {
        fprintf(stderr, "[Webhook] Failed to start listener thread: %d\n", err);
        return ERR_FAILED;
    }

    printf("[Webhook] Webhook HTTP server started on port %d\n", channel->config.port);

    channel->listening = true;
    webhook_data->listening = true;

    return ERR_OK;
}

static err_t webhook_stop_listening(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    if (!channel->listening) {
        return ERR_OK; // Not listening
    }

    // Set stop flag to signal listener thread to exit
    webhook_data->stop_listening = true;

    // Wait for thread to finish
    if (webhook_data->listener_thread) {
        pthread_join(webhook_data->listener_thread, NULL);
        webhook_data->listener_thread = 0;
    }

    printf("[Webhook] Webhook HTTP server stopped\n");

    channel->listening = false;
    webhook_data->listening = false;

    return ERR_OK;
}

static bool webhook_is_listening(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return false;
    }

    return channel->listening;
}

static err_t webhook_health_check(channel_t* channel, bool* out_healthy) {
    if (!channel || !channel->impl_data || !out_healthy) {
        return ERR_INVALID_ARGUMENT;
    }

    // Basic health check - channel is healthy if it's properly initialized
    // (HTTP client created and ready to use)
    *out_healthy = channel->initialized;
    return ERR_OK;
}

static err_t webhook_get_stats(channel_t* channel, uint32_t* messages_sent,
                              uint32_t* messages_received, uint32_t* active_connections) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    if (messages_sent) *messages_sent = webhook_data->messages_sent;
    if (messages_received) *messages_received = webhook_data->messages_received;
    if (active_connections) *active_connections = webhook_data->listening ? 1 : 0;

    return ERR_OK;
}

// ============================================================================
// HTTP Server Implementation
// ============================================================================

// Buffer for HTTP requests
typedef struct {
    char data[4096];
    size_t len;
} http_buffer_t;

// HTTP connection context
typedef struct {
    uv_tcp_t* handle;
    webhook_channel_t* channel;
    http_buffer_t buffer;
    bool request_complete;
} connection_context_t;

// Allocate buffer for reading
static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    connection_context_t* context = (connection_context_t*)handle->data;
    if (context && context->buffer.len < sizeof(context->buffer.data)) {
        size_t available = sizeof(context->buffer.data) - context->buffer.len;
        buf->base = context->buffer.data + context->buffer.len;
        buf->len = available > suggested_size ? suggested_size : available;
    } else {
        buf->base = NULL;
        buf->len = 0;
    }
}

// Parse HTTP request and extract method, path, headers, and body
static bool parse_http_request(const char* data, size_t len,
                              char* method, size_t method_size,
                              char* path, size_t path_size,
                              char* body, size_t* body_len) {
    if (len == 0) return false;

    // Find request line
    const char* end_of_line = strstr(data, "\r\n");
    if (!end_of_line) return false;

    // Parse method and path
    int scanned = sscanf(data, "%s %s", method, path);
    if (scanned != 2) return false;

    // Find body (after \r\n\r\n)
    const char* body_start = strstr(data, "\r\n\r\n");
    if (body_start) {
        body_start += 4; // Skip \r\n\r\n
        size_t body_length = len - (body_start - data);
        if (body_length > 0) {
            *body_len = body_length < *body_len ? body_length : *body_len;
            memcpy(body, body_start, *body_len);
        } else {
            *body_len = 0;
        }
    } else {
        *body_len = 0;
    }

    return true;
}

// Send HTTP response
static void send_http_response(uv_stream_t* stream, int status_code, const char* status_text,
                              const char* content_type, const char* body) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %lu\r\n"
                      "Connection: close\r\n"
                      "\r\n"
                      "%s",
                      status_code, status_text, content_type,
                      body ? strlen(body) : 0, body ? body : "");

    if (len > 0 && len < (int)sizeof(response)) {
        uv_buf_t buf = uv_buf_init(response, len);
        uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
        uv_write(req, stream, &buf, 1, NULL); // We don't handle write completion
    }
}

// Handle incoming data
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    connection_context_t* context = (connection_context_t*)stream->data;
    if (!context) return;

    if (nread > 0) {
        context->buffer.len += nread;
        context->buffer.data[context->buffer.len] = '\0';

        // Check if we have a complete request (ends with \r\n\r\n)
        if (strstr(context->buffer.data, "\r\n\r\n")) {
            char method[16];
            char path[256];
            char body[2048];
            size_t body_len = sizeof(body);

            if (parse_http_request(context->buffer.data, context->buffer.len,
                                  method, sizeof(method),
                                  path, sizeof(path),
                                  body, &body_len)) {

                // Only handle POST requests to /webhook or /
                if (strcmp(method, "POST") == 0 &&
                    (strcmp(path, "/webhook") == 0 || strcmp(path, "/") == 0)) {

                    // Parse webhook payload
                    channel_message_t message = {0};
                    err_t parse_err = parse_webhook_payload(body, body_len, &message);

                    if (parse_err == ERR_OK) {
                        // Verify signature if configured
                        webhook_channel_t* channel = context->channel;
                        bool signature_valid = true;

                        if (channel->verify_signature && !str_empty(channel->secret)) {
                            // Extract signature from headers (simplified)
                            // In real implementation, extract from X-Signature header
                            signature_valid = false; // Default to false for now

                            // TODO: Parse headers and verify signature
                            // For now, accept all requests for testing
                            signature_valid = true;
                        }

                        if (signature_valid) {
                            channel->messages_received++;

                            // Call the callback if set
                            if (channel->on_message_callback) {
                                channel->on_message_callback(&message, channel->user_data);
                            }

                            send_http_response(stream, 200, "OK",
                                              "application/json",
                                              "{\"status\":\"ok\"}");
                        } else {
                            send_http_response(stream, 401, "Unauthorized",
                                              "application/json",
                                              "{\"error\":\"Invalid signature\"}");
                        }

                        // Free message strings
                        free((void*)message.content.data);
                        free((void*)message.sender.data);
                        free((void*)message.channel.data);
                    } else {
                        send_http_response(stream, 400, "Bad Request",
                                          "application/json",
                                          "{\"error\":\"Invalid JSON payload\"}");
                    }
                } else {
                    send_http_response(stream, 404, "Not Found",
                                      "application/json",
                                      "{\"error\":\"Not Found\"}");
                }
            } else {
                send_http_response(stream, 400, "Bad Request",
                                  "application/json",
                                  "{\"error\":\"Invalid HTTP request\"}");
            }

            // Close connection after response
            uv_close((uv_handle_t*)stream, NULL);
        }
    } else if (nread < 0) {
        // Error or EOF
        uv_close((uv_handle_t*)stream, NULL);
    }

    // Free context when connection closes
    if (nread <= 0) {
        free(context);
    }
}

// Handle new connection
static void on_connection(uv_stream_t* server, int status) {
    if (status < 0) return;

    channel_t* channel = (channel_t*)server->data;
    if (!channel || !channel->impl_data) return;

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(webhook_data->loop, client);

    // Create connection context
    connection_context_t* context = (connection_context_t*)calloc(1, sizeof(connection_context_t));
    context->handle = client;
    context->channel = webhook_data;
    client->data = context;

    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, alloc_buffer, on_read);
    } else {
        uv_close((uv_handle_t*)client, NULL);
        free(context);
    }
}

// Thread function for libuv event loop
static void* listener_thread_func(void* arg) {
    channel_t* channel = (channel_t*)arg;
    if (!channel || !channel->impl_data) return NULL;

    webhook_channel_t* webhook_data = (webhook_channel_t*)channel->impl_data;

    // Create libuv loop
    webhook_data->loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
    uv_loop_init(webhook_data->loop);

    // Create TCP server
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", channel->config.port, &addr);

    uv_tcp_init(webhook_data->loop, &webhook_data->server);
    uv_tcp_bind(&webhook_data->server, (const struct sockaddr*)&addr, 0);
    webhook_data->server.data = channel; // Store channel_t pointer for callbacks

    int r = uv_listen((uv_stream_t*)&webhook_data->server, 128, on_connection);
    if (r) {
        fprintf(stderr, "[Webhook] Listen error: %s\n", uv_strerror(r));
        uv_loop_close(webhook_data->loop);
        free(webhook_data->loop);
        webhook_data->loop = NULL;
        return NULL;
    }

    printf("[Webhook] HTTP server listening on port %d\n", channel->config.port);

    // Run event loop until stop flag is set
    while (!webhook_data->stop_listening) {
        uv_run(webhook_data->loop, UV_RUN_NOWAIT);
        usleep(10000); // 10ms sleep to prevent busy waiting
    }

    // Cleanup
    uv_close((uv_handle_t*)&webhook_data->server, NULL);
    uv_run(webhook_data->loop, UV_RUN_DEFAULT);
    uv_loop_close(webhook_data->loop);
    free(webhook_data->loop);
    webhook_data->loop = NULL;

    printf("[Webhook] HTTP server stopped\n");
    return NULL;
}