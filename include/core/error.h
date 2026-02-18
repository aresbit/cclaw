// error.h - Error handling for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_ERROR_H
#define CCLAW_CORE_ERROR_H

#include "types.h"

// Error codes (inspired by Rust anyhow and thiserror)
typedef enum {
    ERR_OK = 0,

    // General errors
    ERR_FAILED,
    ERR_OUT_OF_MEMORY,
    ERR_INVALID_ARGUMENT,
    ERR_NOT_FOUND,
    ERR_ALREADY_EXISTS,
    ERR_PERMISSION_DENIED,
    ERR_TIMEOUT,
    ERR_CANCELLED,
    ERR_NOT_IMPLEMENTED,
    ERR_NOT_INITIALIZED,

    // I/O errors
    ERR_IO,
    ERR_FILE_NOT_FOUND,
    ERR_FILE_EXISTS,
    ERR_FILE_TOO_LARGE,
    ERR_READ_ONLY,
    ERR_WRITE_FAILED,

    // Network errors
    ERR_NETWORK,
    ERR_CONNECTION_FAILED,
    ERR_CONNECTION_TIMEOUT,
    ERR_DNS_FAILURE,
    ERR_SSL_ERROR,
    ERR_HTTP_ERROR,
    ERR_RATE_LIMITED,

    // Configuration errors
    ERR_CONFIG_INVALID,
    ERR_CONFIG_MISSING,
    ERR_CONFIG_PARSE,

    // Provider errors
    ERR_PROVIDER,
    ERR_PROVIDER_UNAVAILABLE,
    ERR_PROVIDER_AUTH,
    ERR_PROVIDER_RATE_LIMIT,
    ERR_PROVIDER_QUOTA_EXCEEDED,
    ERR_MODEL_NOT_FOUND,

    // Channel errors
    ERR_CHANNEL,
    ERR_CHANNEL_AUTH,
    ERR_CHANNEL_DISCONNECTED,
    ERR_CHANNEL_RATE_LIMIT,

    // Memory errors
    ERR_MEMORY,
    ERR_MEMORY_CORRUPT,
    ERR_MEMORY_FULL,
    ERR_EMBEDDING_FAILED,

    // Tool errors
    ERR_TOOL,
    ERR_TOOL_EXECUTION_FAILED,
    ERR_TOOL_NOT_ALLOWED,
    ERR_TOOL_TIMEOUT,

    // Security errors
    ERR_SECURITY,
    ERR_AUTH_FAILED,
    ERR_INVALID_TOKEN,
    ERR_PAIRING_REQUIRED,
    ERR_ACCESS_DENIED,

    // Runtime errors
    ERR_RUNTIME,
    ERR_INVALID_STATE,
    ERR_DOCKER_UNAVAILABLE,
    ERR_SANDBOX_FAILED,

    // Maximum error code (for bounds checking)
    ERR_MAX
} err_t;

// Error context structure
typedef struct error_ctx_t {
    err_t code;
    str_t message;
    str_t file;
    uint32_t line;
    struct error_ctx_t* cause;
} error_ctx_t;

// Error creation macros
#define ERR(code, msg) (error_ctx_t){ \
    .code = (code), \
    .message = STR_LIT(msg), \
    .file = STR_LIT(__FILE__), \
    .line = __LINE__, \
    .cause = NULL \
}

#define ERR_WITH_CAUSE(code, msg, cause) (error_ctx_t){ \
    .code = (code), \
    .message = STR_LIT(msg), \
    .file = STR_LIT(__FILE__), \
    .line = __LINE__, \
    .cause = (cause) \
}

// Error propagation macros
#define TRY(expr) \
    do { \
        err_t __err = (expr); \
        if (__err != ERR_OK) { \
            return __err; \
        } \
    } while (0)

#define TRY_MSG(expr, msg) \
    do { \
        err_t __err = (expr); \
        if (__err != ERR_OK) { \
            return error_set(__err, STR_LIT(msg), __FILE__, __LINE__); \
        } \
    } while (0)

// Error checking macros
#define REQUIRE(cond, err) \
    do { \
        if (!(cond)) { \
            return (err); \
        } \
    } while (0)

#define REQUIRE_MSG(cond, err, msg) \
    do { \
        if (!(cond)) { \
            return error_set((err), STR_LIT(msg), __FILE__, __LINE__); \
        } \
    } while (0)

// Error API functions
err_t error_set(err_t code, str_t message, const char* file, uint32_t line);
err_t error_set_with_cause(err_t code, str_t message, const char* file, uint32_t line, error_ctx_t* cause);

const char* error_to_string(err_t code);
str_t error_format(err_t code, str_t message);

void error_print(error_ctx_t* error);
void error_free(error_ctx_t* error);

// Error context stack (thread-local)
typedef struct error_stack_t {
    error_ctx_t* errors;
    uint32_t count;
    uint32_t capacity;
} error_stack_t;

error_stack_t* error_stack_get(void);
void error_stack_push(error_ctx_t error);
error_ctx_t* error_stack_pop(void);
void error_stack_clear(void);

#endif // CCLAW_CORE_ERROR_H