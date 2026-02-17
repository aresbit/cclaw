// test_channels.c - Channel system integration tests for CClaw
// SPDX-License-Identifier: MIT

#include "cclaw.h"
#include "core/channel.h"
#include "core/types.h"
#include "core/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test utilities (copied from basic.c)
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

#define TEST_RUN(name, func) \
    do { \
        printf("Running test: %s... ", (name)); \
        fflush(stdout); \
        if (func()) { \
            printf("PASS\n"); \
            passed++; \
        } else { \
            printf("FAIL\n"); \
            failed++; \
        } \
        total++; \
    } while (0)

// Global variables for testing callbacks
static channel_message_t g_last_received_message = {0};
static bool g_message_received = false;

// Callback for testing message reception
static void test_message_callback(channel_message_t* msg, void* user_data) {
    (void)user_data;

    // Free any previously stored data
    free((void*)g_last_received_message.content.data);
    free((void*)g_last_received_message.sender.data);
    free((void*)g_last_received_message.channel.data);

    g_last_received_message.content.data = strdup(msg->content.data);
    g_last_received_message.content.len = msg->content.len;
    g_last_received_message.sender.data = strdup(msg->sender.data);
    g_last_received_message.sender.len = msg->sender.len;
    g_last_received_message.channel.data = strdup(msg->channel.data);
    g_last_received_message.channel.len = msg->channel.len;
    g_last_received_message.timestamp = msg->timestamp;

    g_message_received = true;
}

// Cleanup test message
static void cleanup_test_message(void) {
    free((void*)g_last_received_message.content.data);
    free((void*)g_last_received_message.sender.data);
    free((void*)g_last_received_message.channel.data);
    memset(&g_last_received_message, 0, sizeof(g_last_received_message));
    g_message_received = false;
}

// Test channel registry
static bool test_channel_registry(void) {
    err_t err = channel_registry_init();
    TEST_ASSERT(err == ERR_OK, "Failed to initialize channel registry");

    // List registered channels
    const char** names = NULL;
    uint32_t count = 0;
    err = channel_registry_list(&names, &count);
    TEST_ASSERT(err == ERR_OK, "Failed to list channels");

    // Should have at least webhook channel
    bool found_webhook = false;
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(names[i], "webhook") == 0) {
            found_webhook = true;
            break;
        }
    }
    TEST_ASSERT(found_webhook, "Webhook channel not registered");

    channel_registry_shutdown();
    return true;
}

// Test webhook channel creation
static bool test_webhook_creation(void) {
    err_t err = channel_registry_init();
    TEST_ASSERT(err == ERR_OK, "Failed to initialize channel registry");

    // Create webhook channel configuration
    channel_config_t config = channel_config_default();
    config.name = str_dup_cstr("test-webhook", NULL);
    config.type = str_dup_cstr("webhook", NULL);
    config.port = 9999; // Use test port
    config.host = str_dup_cstr("127.0.0.1", NULL);
    config.auto_start = false;

    // Create channel
    channel_t* channel = NULL;
    err = channel_create("webhook", &config, &channel);
    TEST_ASSERT(err == ERR_OK, "Failed to create webhook channel");
    TEST_ASSERT(channel != NULL, "Channel is NULL");

    // Initialize channel
    err = channel->vtable->init(channel);
    TEST_ASSERT(err == ERR_OK, "Failed to initialize webhook channel");

    // Check channel properties
    TEST_ASSERT(str_equal(channel->config.name, STR_LIT("test-webhook")),
                "Channel name mismatch");
    TEST_ASSERT(str_equal(channel->config.type, STR_LIT("webhook")),
                "Channel type mismatch");
    TEST_ASSERT(channel->config.port == 9999, "Channel port mismatch");

    // Cleanup
    channel->vtable->destroy(channel);
    channel_registry_shutdown();
    return true;
}

// Test webhook channel listening (without actual HTTP server)
static bool test_webhook_listening(void) {
    err_t err = channel_registry_init();
    TEST_ASSERT(err == ERR_OK, "Failed to initialize channel registry");

    // Create webhook channel
    channel_config_t config = channel_config_default();
    config.name = str_dup_cstr("test-listening", NULL);
    config.type = str_dup_cstr("webhook", NULL);
    config.port = 9998;
    config.host = str_dup_cstr("127.0.0.1", NULL);
    config.auto_start = false;

    channel_t* channel = NULL;
    err = channel_create("webhook", &config, &channel);
    TEST_ASSERT(err == ERR_OK, "Failed to create webhook channel");

    err = channel->vtable->init(channel);
    TEST_ASSERT(err == ERR_OK, "Failed to initialize webhook channel");

    // Start listening
    cleanup_test_message();
    err = channel->vtable->start_listening(channel, test_message_callback, NULL);
    TEST_ASSERT(err == ERR_OK, "Failed to start listening");

    // Check if listening
    bool is_listening = channel->vtable->is_listening(channel);
    TEST_ASSERT(is_listening, "Channel should be listening");

    // Give server a moment to start (even though it won't actually bind in test)
    usleep(100000); // 100ms

    // Stop listening
    err = channel->vtable->stop_listening(channel);
    TEST_ASSERT(err == ERR_OK, "Failed to stop listening");

    is_listening = channel->vtable->is_listening(channel);
    TEST_ASSERT(!is_listening, "Channel should not be listening after stop");

    // Cleanup
    channel->vtable->destroy(channel);
    cleanup_test_message();
    channel_registry_shutdown();
    return true;
}

// Test channel manager
static bool test_channel_manager(void) {
    err_t err = channel_registry_init();
    TEST_ASSERT(err == ERR_OK, "Failed to initialize channel registry");

    // Create channel manager
    channel_manager_t* manager = channel_manager_create();
    TEST_ASSERT(manager != NULL, "Failed to create channel manager");

    // Create a webhook channel
    channel_config_t config = channel_config_default();
    config.name = str_dup_cstr("manager-test", NULL);
    config.type = str_dup_cstr("webhook", NULL);
    config.port = 9997;
    config.host = str_dup_cstr("127.0.0.1", NULL);
    config.auto_start = false;

    channel_t* channel = NULL;
    err = channel_create("webhook", &config, &channel);
    TEST_ASSERT(err == ERR_OK, "Failed to create webhook channel");

    err = channel->vtable->init(channel);
    TEST_ASSERT(err == ERR_OK, "Failed to initialize webhook channel");

    // Add channel to manager
    err = channel_manager_add_channel(manager, channel);
    TEST_ASSERT(err == ERR_OK, "Failed to add channel to manager");

    // Start all channels
    cleanup_test_message();
    err = channel_manager_start_all(manager, test_message_callback, NULL);
    TEST_ASSERT(err == ERR_OK, "Failed to start all channels");

    // Stop all channels
    err = channel_manager_stop_all(manager);
    TEST_ASSERT(err == ERR_OK, "Failed to stop all channels");

    // Remove channel
    err = channel_manager_remove_channel(manager, &config.name);
    TEST_ASSERT(err == ERR_OK, "Failed to remove channel");

    // Destroy manager
    channel_manager_destroy(manager);
    cleanup_test_message();
    channel_registry_shutdown();
    return true;
}

// Test message sending (simulated)
static bool test_message_sending(void) {
    err_t err = channel_registry_init();
    TEST_ASSERT(err == ERR_OK, "Failed to initialize channel registry");

    // Create webhook channel with webhook URL (simulated)
    channel_config_t config = channel_config_default();
    config.name = str_dup_cstr("send-test", NULL);
    config.type = str_dup_cstr("webhook", NULL);
    config.webhook_url = str_dup_cstr("http://example.com/webhook", NULL);
    config.auto_start = false;

    channel_t* channel = NULL;
    err = channel_create("webhook", &config, &channel);
    TEST_ASSERT(err == ERR_OK, "Failed to create webhook channel");

    err = channel->vtable->init(channel);
    TEST_ASSERT(err == ERR_OK, "Failed to initialize webhook channel");

    // Send a message
    str_t message = STR_LIT("Hello from test!");
    err = channel->vtable->send(channel, &message, NULL);
    // This will fail because we don't have a real HTTP server, but that's OK
    // We just test that the function can be called

    // Create a channel message
    channel_message_t channel_msg = {0};
    channel_msg.content = STR_LIT("Test content");
    channel_msg.sender = STR_LIT("test-sender");
    channel_msg.channel = STR_LIT("test-channel");

    err = channel->vtable->send_message(channel, &channel_msg);
    // This should print a message but not actually send

    // Cleanup
    channel->vtable->destroy(channel);
    channel_registry_shutdown();
    return true;
}

// Test channel health check
static bool test_health_check(void) {
    err_t err = channel_registry_init();
    TEST_ASSERT(err == ERR_OK, "Failed to initialize channel registry");

    // Create webhook channel
    channel_config_t config = channel_config_default();
    config.name = str_dup_cstr("health-test", NULL);
    config.type = str_dup_cstr("webhook", NULL);
    config.port = 9996;
    config.host = str_dup_cstr("127.0.0.1", NULL);
    config.auto_start = false;

    channel_t* channel = NULL;
    err = channel_create("webhook", &config, &channel);
    TEST_ASSERT(err == ERR_OK, "Failed to create webhook channel");

    err = channel->vtable->init(channel);
    TEST_ASSERT(err == ERR_OK, "Failed to initialize webhook channel");

    // Check health
    bool healthy = false;
    err = channel->vtable->health_check(channel, &healthy);
    TEST_ASSERT(err == ERR_OK, "Failed to check health");
    TEST_ASSERT(healthy, "Channel should be healthy after initialization");

    // Get stats
    uint32_t sent = 0, received = 0, connections = 0;
    err = channel->vtable->get_stats(channel, &sent, &received, &connections);
    TEST_ASSERT(err == ERR_OK, "Failed to get stats");
    TEST_ASSERT(sent == 0, "No messages should be sent yet");
    TEST_ASSERT(received == 0, "No messages should be received yet");

    // Cleanup
    channel->vtable->destroy(channel);
    channel_registry_shutdown();
    return true;
}

// Main test runner
int main(void) {
    printf("CClaw Channel System Test Suite\n");
    printf("Version: %s\n", CCLAW_VERSION_STRING);
    printf("Platform: %s\n", cclaw_get_platform_name());
    printf("\n");

    int total = 0;
    int passed = 0;
    int failed = 0;

    // Run tests
    TEST_RUN("channel_registry", test_channel_registry);
    TEST_RUN("webhook_creation", test_webhook_creation);
    TEST_RUN("webhook_listening", test_webhook_listening);
    TEST_RUN("channel_manager", test_channel_manager);
    TEST_RUN("message_sending", test_message_sending);
    TEST_RUN("health_check", test_health_check);

    // Summary
    printf("\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", total);
    printf("  Passed: %d\n", passed);
    printf("  Failed: %d\n", failed);

    if (failed > 0) {
        printf("\nSome tests failed!\n");
        return 1;
    }

    printf("\nAll channel tests passed!\n");
    return 0;
}