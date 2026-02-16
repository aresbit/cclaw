// http.h - HTTP client interface for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_UTILS_HTTP_H
#define CCLAW_UTILS_HTTP_H

#include "core/types.h"
#include "core/error.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct http_client_t http_client_t;
typedef struct http_request_t http_request_t;
typedef struct http_response_t http_response_t;
typedef struct http_header_t http_header_t;

// HTTP header structure
typedef struct http_header_t {
    str_t name;
    str_t value;
} http_header_t;

// HTTP response structure
typedef struct http_response_t {
    uint32_t status_code;
    str_t status_text;
    str_t body;
    http_header_t* headers;
    uint32_t headers_count;
    double request_time_ms;
} http_response_t;

// HTTP client configuration
typedef struct http_client_config_t {
    uint32_t timeout_ms;
    uint32_t connect_timeout_ms;
    bool follow_redirects;
    uint32_t max_redirects;
    str_t user_agent;
    str_t base_url;
    // TLS/SSL options
    bool verify_ssl;
    str_t ca_cert_path;
    str_t client_cert_path;
    str_t client_key_path;
} http_client_config_t;

// HTTP client handle
struct http_client_t {
    void* curl;  // CURL* handle
    http_client_config_t config;
    http_header_t* default_headers;
    uint32_t default_headers_count;
};

// Initialize global HTTP subsystem
err_t http_init(void);
void http_shutdown(void);

// Client creation/destruction
http_client_t* http_client_create(const http_client_config_t* config);
void http_client_destroy(http_client_t* client);

// Default configuration
http_client_config_t http_client_default_config(void);

// Add default headers
void http_client_add_header(http_client_t* client, const char* name, const char* value);
void http_client_remove_header(http_client_t* client, const char* name);
void http_client_clear_headers(http_client_t* client);

// HTTP methods
err_t http_get(http_client_t* client, const char* url, http_response_t** out_response);
err_t http_post(http_client_t* client, const char* url, const char* body, http_response_t** out_response);
err_t http_post_json(http_client_t* client, const char* url, const char* json_body, http_response_t** out_response);
err_t http_put(http_client_t* client, const char* url, const char* body, http_response_t** out_response);
err_t http_patch(http_client_t* client, const char* url, const char* body, http_response_t** out_response);
err_t http_delete(http_client_t* client, const char* url, http_response_t** out_response);

// Generic request
err_t http_request(http_client_t* client, const char* method, const char* url,
                   const char* body, size_t body_len,
                   const char* content_type,
                   http_response_t** out_response);

// Response handling
void http_response_free(http_response_t* response);
const char* http_response_get_header(http_response_t* response, const char* name);
bool http_response_is_success(http_response_t* response);
bool http_response_is_redirect(http_response_t* response);
bool http_response_is_error(http_response_t* response);

// Response body helpers
str_t http_response_get_body_str(http_response_t* response);

// URL encoding/decoding
str_t http_url_encode(const char* str);
str_t http_url_decode(const char* str);

// Build query string from key-value pairs
str_t http_build_query(const char** keys, const char** values, uint32_t count);

// Async support (basic)
typedef void (*http_callback_t)(http_response_t* response, void* user_data);
err_t http_get_async(http_client_t* client, const char* url, http_callback_t callback, void* user_data);

// Streaming response support
typedef size_t (*http_write_callback_t)(const char* data, size_t len, void* user_data);
err_t http_get_stream(http_client_t* client, const char* url, http_write_callback_t callback, void* user_data);
err_t http_post_json_stream(http_client_t* client, const char* url, const char* json_body,
                           http_write_callback_t callback, void* user_data);

// Connection pool (for high-performance scenarios)
err_t http_client_set_pool_size(http_client_t* client, uint32_t size);
void http_client_drain_pool(http_client_t* client);

// Helper macros
#define HTTP_OK 200
#define HTTP_CREATED 201
#define HTTP_ACCEPTED 202
#define HTTP_NO_CONTENT 204
#define HTTP_BAD_REQUEST 400
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404
#define HTTP_TOO_MANY_REQUESTS 429
#define HTTP_INTERNAL_ERROR 500
#define HTTP_BAD_GATEWAY 502
#define HTTP_SERVICE_UNAVAILABLE 503

#endif // CCLAW_UTILS_HTTP_H
