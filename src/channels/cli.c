// cli.c - CLI channel implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/channel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

// CLI channel instance data
typedef struct cli_channel_t {
    pthread_t listener_thread;      // Thread for listening to stdin
    bool stop_listening;            // Flag to stop the listener thread
    void (*on_message_callback)(channel_message_t* msg, void* user_data);
    void* user_data;
    int pipe_fds[2];                // Pipe for thread communication
} cli_channel_t;

// Forward declarations for vtable
static str_t cli_get_name(void);
static str_t cli_get_version(void);
static str_t cli_get_type(void);
static err_t cli_create(const channel_config_t* config, channel_t** out_channel);
static void cli_destroy(channel_t* channel);
static err_t cli_init(channel_t* channel);
static void cli_cleanup(channel_t* channel);
static err_t cli_send(channel_t* channel, const str_t* message, const str_t* recipient);
static err_t cli_send_message(channel_t* channel, const channel_message_t* message);
static err_t cli_start_listening(channel_t* channel,
                                void (*on_message)(channel_message_t* msg, void* user_data),
                                void* user_data);
static err_t cli_stop_listening(channel_t* channel);
static bool cli_is_listening(channel_t* channel);
static err_t cli_health_check(channel_t* channel, bool* out_healthy);
static err_t cli_get_stats(channel_t* channel, uint32_t* messages_sent,
                          uint32_t* messages_received, uint32_t* active_connections);

// VTable definition
static const channel_vtable_t cli_vtable = {
    .get_name = cli_get_name,
    .get_version = cli_get_version,
    .get_type = cli_get_type,
    .create = cli_create,
    .destroy = cli_destroy,
    .init = cli_init,
    .cleanup = cli_cleanup,
    .send = cli_send,
    .send_message = cli_send_message,
    .start_listening = cli_start_listening,
    .stop_listening = cli_stop_listening,
    .is_listening = cli_is_listening,
    .health_check = cli_health_check,
    .get_stats = cli_get_stats
};

// Get vtable
const channel_vtable_t* channel_cli_get_vtable(void) {
    return &cli_vtable;
}

// Listener thread function
static void* cli_listener_thread(void* arg) {
    channel_t* channel = (channel_t*)arg;
    cli_channel_t* cli_data = (cli_channel_t*)channel->impl_data;

    char buffer[4096];
    fd_set read_fds;

    while (!cli_data->stop_listening) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(cli_data->pipe_fds[0], &read_fds);

        int max_fd = (STDIN_FILENO > cli_data->pipe_fds[0]) ? STDIN_FILENO : cli_data->pipe_fds[0];

        // Wait for input with timeout (100ms)
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 100000 };
        int result = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (result < 0) {
            // Error
            if (errno == EINTR) continue;
            break;
        }

        if (result == 0) {
            // Timeout, check stop flag
            continue;
        }

        // Check pipe first (for stop signal)
        if (FD_ISSET(cli_data->pipe_fds[0], &read_fds)) {
            char dummy;
            read(cli_data->pipe_fds[0], &dummy, 1);
            break;
        }

        // Check stdin
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                // Null-terminate and trim newline
                buffer[bytes_read] = '\0';
                if (bytes_read > 0 && buffer[bytes_read - 1] == '\n') {
                    buffer[bytes_read - 1] = '\0';
                    bytes_read--;
                }
                if (bytes_read > 0 && buffer[bytes_read - 1] == '\r') {
                    buffer[bytes_read - 1] = '\0';
                    bytes_read--;
                }

                if (bytes_read > 0) {
                    // Create message
                    str_t content = { .data = buffer, .len = (uint32_t)bytes_read };
                    str_t sender = STR_LIT("user");
                    str_t channel_name = channel->config.name;

                    if (str_empty(channel_name)) {
                        channel_name = STR_LIT("cli");
                    }

                    channel_message_t* msg = channel_message_create(NULL, &sender, &content, &channel_name);
                    if (msg && cli_data->on_message_callback) {
                        cli_data->on_message_callback(msg, cli_data->user_data);
                    }
                    channel_message_free(msg);
                }
            } else if (bytes_read == 0) {
                // EOF (stdin closed)
                break;
            }
            // else error, continue
        }
    }

    return NULL;
}

static str_t cli_get_name(void) {
    return STR_LIT("cli");
}

static str_t cli_get_version(void) {
    return STR_LIT("1.0.0");
}

static str_t cli_get_type(void) {
    return STR_LIT("cli");
}

static err_t cli_create(const channel_config_t* config, channel_t** out_channel) {
    if (!config || !out_channel) return ERR_INVALID_ARGUMENT;

    channel_t* channel = channel_alloc(&cli_vtable);
    if (!channel) return ERR_OUT_OF_MEMORY;

    cli_channel_t* cli_data = calloc(1, sizeof(cli_channel_t));
    if (!cli_data) {
        channel_free(channel);
        return ERR_OUT_OF_MEMORY;
    }

    // Initialize pipe for thread communication
    if (pipe(cli_data->pipe_fds) != 0) {
        free(cli_data);
        channel_free(channel);
        return ERR_IO;
    }

    // Set pipe to non-blocking
    fcntl(cli_data->pipe_fds[0], F_SETFL, O_NONBLOCK);
    fcntl(cli_data->pipe_fds[1], F_SETFL, O_NONBLOCK);

    cli_data->listener_thread = 0;
    cli_data->stop_listening = true;
    cli_data->on_message_callback = NULL;
    cli_data->user_data = NULL;

    // Copy configuration
    channel->config = *config;
    if (str_empty(channel->config.name)) {
        channel->config.name = str_dup_cstr("cli", NULL);
    }

    channel->impl_data = cli_data;
    channel->initialized = false;
    channel->listening = false;

    *out_channel = channel;
    return ERR_OK;
}

static void cli_destroy(channel_t* channel) {
    if (!channel || !channel->impl_data) return;

    cli_channel_t* cli_data = (cli_channel_t*)channel->impl_data;

    // Stop listening if active
    if (channel->listening) {
        cli_stop_listening(channel);
    }

    // Cleanup will free resources if initialized
    if (channel->initialized) {
        cli_cleanup(channel);
    }

    // Close pipe
    close(cli_data->pipe_fds[0]);
    close(cli_data->pipe_fds[1]);

    // Free configuration strings
    free((void*)channel->config.name.data);
    free((void*)channel->config.type.data);
    free((void*)channel->config.auth_token.data);
    free((void*)channel->config.webhook_url.data);
    free((void*)channel->config.host.data);

    free(cli_data);
    channel->impl_data = NULL;

    channel_free(channel);
}

static err_t cli_init(channel_t* channel) {
    if (!channel || !channel->impl_data) return ERR_INVALID_ARGUMENT;
    if (channel->initialized) return ERR_OK;

    // CLI channel doesn't need special initialization
    channel->initialized = true;
    return ERR_OK;
}

static void cli_cleanup(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) return;

    // CLI channel doesn't need special cleanup
    channel->initialized = false;
}

static err_t cli_send(channel_t* channel, const str_t* message, const str_t* recipient) {
    if (!channel || !channel->impl_data || !channel->initialized || !message) {
        return ERR_INVALID_ARGUMENT;
    }

    (void)recipient; // CLI doesn't use recipient

    // Print message to stdout
    printf("%.*s\n", (int)message->len, message->data);
    fflush(stdout);

    return ERR_OK;
}

static err_t cli_send_message(channel_t* channel, const channel_message_t* message) {
    if (!channel || !channel->impl_data || !channel->initialized || !message) {
        return ERR_INVALID_ARGUMENT;
    }

    // Format: [sender] content
    printf("[%.*s] %.*s\n",
           (int)message->sender.len, message->sender.data,
           (int)message->content.len, message->content.data);
    fflush(stdout);

    return ERR_OK;
}

static err_t cli_start_listening(channel_t* channel,
                                void (*on_message)(channel_message_t* msg, void* user_data),
                                void* user_data) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    cli_channel_t* cli_data = (cli_channel_t*)channel->impl_data;

    if (channel->listening) {
        return ERR_OK; // Already listening
    }

    // Set callback
    cli_data->on_message_callback = on_message;
    cli_data->user_data = user_data;
    cli_data->stop_listening = false;

    // Create listener thread
    int result = pthread_create(&cli_data->listener_thread, NULL,
                                cli_listener_thread, channel);
    if (result != 0) {
        cli_data->stop_listening = true;
        cli_data->on_message_callback = NULL;
        cli_data->user_data = NULL;
        return ERR_CHANNEL;
    }

    channel->listening = true;
    return ERR_OK;
}

static err_t cli_stop_listening(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    cli_channel_t* cli_data = (cli_channel_t*)channel->impl_data;

    if (!channel->listening) {
        return ERR_OK; // Not listening
    }

    // Signal thread to stop
    cli_data->stop_listening = true;

    // Write to pipe to wake up select
    char dummy = 0;
    write(cli_data->pipe_fds[1], &dummy, 1);

    // Wait for thread to finish
    if (cli_data->listener_thread) {
        pthread_join(cli_data->listener_thread, NULL);
        cli_data->listener_thread = 0;
    }

    // Clear callback
    cli_data->on_message_callback = NULL;
    cli_data->user_data = NULL;

    channel->listening = false;
    return ERR_OK;
}

static bool cli_is_listening(channel_t* channel) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return false;
    }

    return channel->listening;
}

static err_t cli_health_check(channel_t* channel, bool* out_healthy) {
    if (!channel || !channel->impl_data || !channel->initialized || !out_healthy) {
        return ERR_INVALID_ARGUMENT;
    }

    // CLI channel is always healthy as long as stdout/stdin are available
    *out_healthy = true;
    return ERR_OK;
}

static err_t cli_get_stats(channel_t* channel, uint32_t* messages_sent,
                          uint32_t* messages_received, uint32_t* active_connections) {
    if (!channel || !channel->impl_data || !channel->initialized) {
        return ERR_INVALID_ARGUMENT;
    }

    // CLI channel doesn't track statistics
    if (messages_sent) *messages_sent = 0;
    if (messages_received) *messages_received = 0;
    if (active_connections) *active_connections = 1; // Always 1 connection (stdin/stdout)

    return ERR_OK;
}