// error.c - Error handling implementation
// SPDX-License-Identifier: MIT

#include "core/error.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Error code to string mapping
static const struct {
    err_t code;
    const char* string;
} error_strings[] = {
    {ERR_OK, "OK"},
    {ERR_FAILED, "Failed"},
    {ERR_OUT_OF_MEMORY, "Out of memory"},
    {ERR_INVALID_ARGUMENT, "Invalid argument"},
    {ERR_NOT_FOUND, "Not found"},
    {ERR_ALREADY_EXISTS, "Already exists"},
    {ERR_PERMISSION_DENIED, "Permission denied"},
    {ERR_TIMEOUT, "Timeout"},
    {ERR_CANCELLED, "Cancelled"},
    {ERR_NOT_IMPLEMENTED, "Not implemented"},
    {ERR_NOT_INITIALIZED, "Not initialized"},
    {ERR_IO, "I/O error"},
    {ERR_FILE_NOT_FOUND, "File not found"},
    {ERR_FILE_EXISTS, "File exists"},
    {ERR_FILE_TOO_LARGE, "File too large"},
    {ERR_READ_ONLY, "Read only"},
    {ERR_WRITE_FAILED, "Write failed"},
    {ERR_NETWORK, "Network error"},
    {ERR_CONNECTION_FAILED, "Connection failed"},
    {ERR_CONNECTION_TIMEOUT, "Connection timeout"},
    {ERR_DNS_FAILURE, "DNS failure"},
    {ERR_SSL_ERROR, "SSL error"},
    {ERR_HTTP_ERROR, "HTTP error"},
    {ERR_RATE_LIMITED, "Rate limited"},
    {ERR_CONFIG_INVALID, "Invalid configuration"},
    {ERR_CONFIG_MISSING, "Missing configuration"},
    {ERR_CONFIG_PARSE, "Configuration parse error"},
    {ERR_PROVIDER, "Provider error"},
    {ERR_PROVIDER_UNAVAILABLE, "Provider unavailable"},
    {ERR_PROVIDER_AUTH, "Provider authentication error"},
    {ERR_PROVIDER_RATE_LIMIT, "Provider rate limit"},
    {ERR_PROVIDER_QUOTA_EXCEEDED, "Provider quota exceeded"},
    {ERR_MODEL_NOT_FOUND, "Model not found"},
    {ERR_CHANNEL, "Channel error"},
    {ERR_CHANNEL_AUTH, "Channel authentication error"},
    {ERR_CHANNEL_DISCONNECTED, "Channel disconnected"},
    {ERR_CHANNEL_RATE_LIMIT, "Channel rate limit"},
    {ERR_MEMORY, "Memory error"},
    {ERR_MEMORY_CORRUPT, "Memory corrupt"},
    {ERR_MEMORY_FULL, "Memory full"},
    {ERR_EMBEDDING_FAILED, "Embedding failed"},
    {ERR_TOOL, "Tool error"},
    {ERR_TOOL_EXECUTION_FAILED, "Tool execution failed"},
    {ERR_TOOL_NOT_ALLOWED, "Tool not allowed"},
    {ERR_TOOL_TIMEOUT, "Tool timeout"},
    {ERR_SECURITY, "Security error"},
    {ERR_AUTH_FAILED, "Authentication failed"},
    {ERR_INVALID_TOKEN, "Invalid token"},
    {ERR_PAIRING_REQUIRED, "Pairing required"},
    {ERR_ACCESS_DENIED, "Access denied"},
    {ERR_RUNTIME, "Runtime error"},
    {ERR_DOCKER_UNAVAILABLE, "Docker unavailable"},
    {ERR_SANDBOX_FAILED, "Sandbox failed"},
};

const char* error_to_string(err_t code) {
    if (code < 0 || code >= ERR_MAX) {
        return "Unknown error";
    }

    for (size_t i = 0; i < sizeof(error_strings) / sizeof(error_strings[0]); i++) {
        if (error_strings[i].code == code) {
            return error_strings[i].string;
        }
    }

    return "Unknown error";
}

err_t error_set(err_t code, str_t message, const char* file, uint32_t line) {
    (void)message;
    (void)file;
    (void)line;
    return code;
}

err_t error_set_with_cause(err_t code, str_t message, const char* file, uint32_t line, error_ctx_t* cause) {
    (void)message;
    (void)file;
    (void)line;
    (void)cause;
    return code;
}

str_t error_format(err_t code, str_t message) {
    const char* code_str = error_to_string(code);
    size_t code_len = strlen(code_str);
    size_t message_len = message.len;

    // Simple implementation - just return the message
    if (!str_empty(message)) {
        return message;
    }

    // Otherwise return a string with the error code
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s", code_str);
    return STR_VIEW(buffer);
}

void error_print(error_ctx_t* error) {
    if (!error) return;
    fprintf(stderr, "Error: %s\n", error_to_string(error->code));
}

void error_free(error_ctx_t* error) {
    (void)error;
}

// Error context stack (simplified)
static error_stack_t g_error_stack = {0};

error_stack_t* error_stack_get(void) {
    return &g_error_stack;
}

void error_stack_push(error_ctx_t error) {
    (void)error;
}

error_ctx_t* error_stack_pop(void) {
    return NULL;
}

void error_stack_clear(void) {
}