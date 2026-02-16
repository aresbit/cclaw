// http.c - HTTP client implementation using libcurl
// SPDX-License-Identifier: MIT

#include "utils/http.h"
#include "core/error.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

// Memory buffer for response
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} memory_buffer_t;

// Callback for libcurl to write response data
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    memory_buffer_t* mem = (memory_buffer_t*)userp;

    // Reallocate if needed
    if (mem->size + total_size + 1 > mem->capacity) {
        size_t new_capacity = mem->capacity * 2;
        if (new_capacity < mem->size + total_size + 1) {
            new_capacity = mem->size + total_size + 1;
        }
        char* new_data = realloc(mem->data, new_capacity);
        if (!new_data) return 0;  // Signal error to curl
        mem->data = new_data;
        mem->capacity = new_capacity;
    }

    memcpy(mem->data + mem->size, contents, total_size);
    mem->size += total_size;
    mem->data[mem->size] = '\0';

    return total_size;
}

// Callback for libcurl to write headers
static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userp) {
    (void)buffer;
    (void)userp;
    return size * nitems;  // Just discard for now, we'll parse later
}

// Initialize global HTTP subsystem
err_t http_init(void) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    return (res == CURLE_OK) ? ERR_OK : ERR_NETWORK;
}

void http_shutdown(void) {
    curl_global_cleanup();
}

// Default configuration
http_client_config_t http_client_default_config(void) {
    return (http_client_config_t){
        .timeout_ms = 30000,
        .connect_timeout_ms = 10000,
        .follow_redirects = true,
        .max_redirects = 10,
        .user_agent = STR_LIT("CClaw/0.1.0"),
        .base_url = STR_NULL,
        .verify_ssl = true,
        .ca_cert_path = STR_NULL,
        .client_cert_path = STR_NULL,
        .client_key_path = STR_NULL
    };
}

// Create HTTP client
http_client_t* http_client_create(const http_client_config_t* config) {
    http_client_t* client = calloc(1, sizeof(http_client_t));
    if (!client) return NULL;

    if (config) {
        client->config = *config;
    } else {
        client->config = http_client_default_config();
    }

    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        return NULL;
    }

    // Set default options
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, client->config.follow_redirects ? 1L : 0L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, (long)client->config.max_redirects);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT_MS, (long)client->config.timeout_ms);
    curl_easy_setopt(client->curl, CURLOPT_CONNECTTIMEOUT_MS, (long)client->config.connect_timeout_ms);
    curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYPEER, client->config.verify_ssl ? 1L : 0L);
    curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYHOST, client->config.verify_ssl ? 2L : 0L);

    // User agent
    if (!str_empty(client->config.user_agent)) {
        curl_easy_setopt(client->curl, CURLOPT_USERAGENT, client->config.user_agent.data);
    }

    return client;
}

// Destroy HTTP client
void http_client_destroy(http_client_t* client) {
    if (!client) return;

    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }

    // Free default headers
    if (client->default_headers) {
        for (uint32_t i = 0; i < client->default_headers_count; i++) {
            free((void*)client->default_headers[i].name.data);
            free((void*)client->default_headers[i].value.data);
        }
        free(client->default_headers);
    }

    free(client);
}

// Add default header
void http_client_add_header(http_client_t* client, const char* name, const char* value) {
    if (!client || !name || !value) return;

    client->default_headers = realloc(client->default_headers,
        sizeof(http_header_t) * (client->default_headers_count + 1));
    if (!client->default_headers) return;

    client->default_headers[client->default_headers_count].name =
        (str_t){ .data = strdup(name), .len = (uint32_t)strlen(name) };
    client->default_headers[client->default_headers_count].value =
        (str_t){ .data = strdup(value), .len = (uint32_t)strlen(value) };
    client->default_headers_count++;
}

// Clear default headers
void http_client_clear_headers(http_client_t* client) {
    if (!client || !client->default_headers) return;

    for (uint32_t i = 0; i < client->default_headers_count; i++) {
        free((void*)client->default_headers[i].name.data);
        free((void*)client->default_headers[i].value.data);
    }
    free(client->default_headers);
    client->default_headers = NULL;
    client->default_headers_count = 0;
}

// Free response
void http_response_free(http_response_t* response) {
    if (!response) return;

    free((void*)response->status_text.data);
    free((void*)response->body.data);

    if (response->headers) {
        for (uint32_t i = 0; i < response->headers_count; i++) {
            free((void*)response->headers[i].name.data);
            free((void*)response->headers[i].value.data);
        }
        free(response->headers);
    }

    free(response);
}

// Get header from response
const char* http_response_get_header(http_response_t* response, const char* name) {
    if (!response || !name) return NULL;

    for (uint32_t i = 0; i < response->headers_count; i++) {
        if (strcasecmp(response->headers[i].name.data, name) == 0) {
            return response->headers[i].value.data;
        }
    }
    return NULL;
}

// Check response status
bool http_response_is_success(http_response_t* response) {
    return response && response->status_code >= 200 && response->status_code < 300;
}

bool http_response_is_redirect(http_response_t* response) {
    return response && response->status_code >= 300 && response->status_code < 400;
}

bool http_response_is_error(http_response_t* response) {
    return response && response->status_code >= 400;
}

// Internal: Perform HTTP request
static err_t perform_request(http_client_t* client, const char* method, const char* url,
                             const char* body, size_t body_len,
                             const char* content_type,
                             http_response_t** out_response) {
    if (!client || !client->curl || !url || !out_response) return ERR_INVALID_ARGUMENT;

    // Build full URL
    char full_url[2048];
    if (client->config.base_url.data && strncmp(url, "http", 4) != 0) {
        snprintf(full_url, sizeof(full_url), "%.*s%s",
                 (int)client->config.base_url.len, client->config.base_url.data, url);
    } else {
        strncpy(full_url, url, sizeof(full_url) - 1);
        full_url[sizeof(full_url) - 1] = '\0';
    }

    // Reset curl handle
    curl_easy_reset(client->curl);

    // Set URL
    curl_easy_setopt(client->curl, CURLOPT_URL, full_url);

    // Set method and body
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
        if (body && body_len > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_UPLOAD, 0L);
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body && body_len > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (body && body_len > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    // Prepare response buffer
    memory_buffer_t response_buffer = { .data = malloc(4096), .size = 0, .capacity = 4096 };
    if (!response_buffer.data) return ERR_OUT_OF_MEMORY;
    response_buffer.data[0] = '\0';

    // Set write callback
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &response_buffer);

    // Set header callback (for now just discard)
    curl_easy_setopt(client->curl, CURLOPT_HEADERFUNCTION, header_callback);

    // Set headers
    struct curl_slist* headers = NULL;
    if (content_type) {
        char ct_header[256];
        snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, ct_header);
    }
    headers = curl_slist_append(headers, "Accept: application/json");

    // Add default headers
    for (uint32_t i = 0; i < client->default_headers_count; i++) {
        char header[1024];
        snprintf(header, sizeof(header), "%.*s: %.*s",
                 (int)client->default_headers[i].name.len, client->default_headers[i].name.data,
                 (int)client->default_headers[i].value.len, client->default_headers[i].value.data);
        headers = curl_slist_append(headers, header);
    }

    if (headers) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup headers
    if (headers) curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        free(response_buffer.data);
        return ERR_NETWORK;
    }

    // Create response object
    http_response_t* response = calloc(1, sizeof(http_response_t));
    if (!response) {
        free(response_buffer.data);
        return ERR_OUT_OF_MEMORY;
    }

    // Get response info
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    response->status_code = (uint32_t)http_code;

    // Move buffer to response
    response->body.data = response_buffer.data;
    response->body.len = (uint32_t)response_buffer.size;

    *out_response = response;
    return ERR_OK;
}

// HTTP GET
err_t http_get(http_client_t* client, const char* url, http_response_t** out_response) {
    return perform_request(client, "GET", url, NULL, 0, NULL, out_response);
}

// HTTP POST
err_t http_post(http_client_t* client, const char* url, const char* body, http_response_t** out_response) {
    return perform_request(client, "POST", url, body, body ? strlen(body) : 0,
                           "application/x-www-form-urlencoded", out_response);
}

// HTTP POST JSON
err_t http_post_json(http_client_t* client, const char* url, const char* json_body, http_response_t** out_response) {
    return perform_request(client, "POST", url, json_body, json_body ? strlen(json_body) : 0,
                           "application/json", out_response);
}

// HTTP PUT
err_t http_put(http_client_t* client, const char* url, const char* body, http_response_t** out_response) {
    return perform_request(client, "PUT", url, body, body ? strlen(body) : 0,
                           "application/json", out_response);
}

// HTTP PATCH
err_t http_patch(http_client_t* client, const char* url, const char* body, http_response_t** out_response) {
    return perform_request(client, "PATCH", url, body, body ? strlen(body) : 0,
                           "application/json", out_response);
}

// HTTP DELETE
err_t http_delete(http_client_t* client, const char* url, http_response_t** out_response) {
    return perform_request(client, "DELETE", url, NULL, 0, NULL, out_response);
}

// Generic request
err_t http_request(http_client_t* client, const char* method, const char* url,
                   const char* body, size_t body_len,
                   const char* content_type,
                   http_response_t** out_response) {
    return perform_request(client, method, url, body, body_len, content_type, out_response);
}

// URL encoding (basic implementation)
str_t http_url_encode(const char* str) {
    if (!str) return STR_NULL;

    size_t len = strlen(str);
    char* encoded = malloc(len * 3 + 1);  // Worst case: all chars need encoding
    if (!encoded) return STR_NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded[j++] = c;
        } else if (c == ' ') {
            encoded[j++] = '+';
        } else {
            sprintf(encoded + j, "%%%02X", c);
            j += 3;
        }
    }
    encoded[j] = '\0';

    return (str_t){ .data = encoded, .len = (uint32_t)j };
}

// URL decoding (basic implementation)
str_t http_url_decode(const char* str) {
    if (!str) return STR_NULL;

    size_t len = strlen(str);
    char* decoded = malloc(len + 1);
    if (!decoded) return STR_NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '+') {
            decoded[j++] = ' ';
        } else if (str[i] == '%' && i + 2 < len) {
            int hex;
            sscanf(str + i + 1, "%2x", &hex);
            decoded[j++] = (char)hex;
            i += 2;
        } else {
            decoded[j++] = str[i];
        }
    }
    decoded[j] = '\0';

    return (str_t){ .data = decoded, .len = (uint32_t)j };
}

// Build query string
str_t http_build_query(const char** keys, const char** values, uint32_t count) {
    if (!keys || !values || count == 0) return STR_NULL;

    size_t total_len = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (keys[i] && values[i]) {
            total_len += strlen(keys[i]) + strlen(values[i]) + 2;  // key=value&
        }
    }

    char* query = malloc(total_len + 1);
    if (!query) return STR_NULL;

    query[0] = '\0';
    size_t pos = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (keys[i] && values[i]) {
            if (pos > 0) {
                query[pos++] = '&';
            }
            str_t encoded_key = http_url_encode(keys[i]);
            str_t encoded_val = http_url_encode(values[i]);

            memcpy(query + pos, encoded_key.data, encoded_key.len);
            pos += encoded_key.len;
            query[pos++] = '=';
            memcpy(query + pos, encoded_val.data, encoded_val.len);
            pos += encoded_val.len;

            free((void*)encoded_key.data);
            free((void*)encoded_val.data);
        }
    }
    query[pos] = '\0';

    return (str_t){ .data = query, .len = (uint32_t)pos };
}

// Streaming response callback wrapper
typedef struct {
    http_write_callback_t user_callback;
    void* user_data;
    char* buffer;
    size_t buffer_size;
    size_t buffer_capacity;
} stream_context_t;

static size_t stream_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    stream_context_t* ctx = (stream_context_t*)userp;
    size_t total_size = size * nmemb;

    // Call user callback with the chunk
    if (ctx->user_callback) {
        size_t consumed = ctx->user_callback((const char*)contents, total_size, ctx->user_data);
        return consumed;  // Return number of bytes consumed
    }

    return total_size;  // Assume all bytes were consumed
}

// Perform streaming HTTP request
static err_t perform_stream_request(http_client_t* client, const char* method, const char* url,
                                   const char* body, size_t body_len,
                                   const char* content_type,
                                   http_write_callback_t callback, void* user_data) {
    if (!client || !client->curl || !url || !callback) return ERR_INVALID_ARGUMENT;

    // Build full URL
    char full_url[2048];
    if (client->config.base_url.data && strncmp(url, "http", 4) != 0) {
        snprintf(full_url, sizeof(full_url), "%.*s%s",
                 (int)client->config.base_url.len, client->config.base_url.data, url);
    } else {
        strncpy(full_url, url, sizeof(full_url) - 1);
        full_url[sizeof(full_url) - 1] = '\0';
    }

    // Reset curl handle
    curl_easy_reset(client->curl);

    // Set URL
    curl_easy_setopt(client->curl, CURLOPT_URL, full_url);

    // Set method and body
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
        if (body && body_len > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_UPLOAD, 0L);
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body && body_len > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (body && body_len > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    // Set streaming callback
    stream_context_t stream_ctx = {
        .user_callback = callback,
        .user_data = user_data,
        .buffer = NULL,
        .buffer_size = 0,
        .buffer_capacity = 0
    };

    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &stream_ctx);

    // Set headers
    struct curl_slist* headers = NULL;
    if (content_type) {
        char ct_header[256];
        snprintf(ct_header, sizeof(ct_header), "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, ct_header);
    }
    headers = curl_slist_append(headers, "Accept: application/json");

    // Accept SSE streams
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    // Add default headers
    for (uint32_t i = 0; i < client->default_headers_count; i++) {
        char header[1024];
        snprintf(header, sizeof(header), "%.*s: %.*s",
                 (int)client->default_headers[i].name.len, client->default_headers[i].name.data,
                 (int)client->default_headers[i].value.len, client->default_headers[i].value.data);
        headers = curl_slist_append(headers, header);
    }

    if (headers) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup headers
    if (headers) curl_slist_free_all(headers);

    // Cleanup buffer
    free(stream_ctx.buffer);

    if (res != CURLE_OK) {
        return ERR_NETWORK;
    }

    return ERR_OK;
}

// HTTP GET with streaming
err_t http_get_stream(http_client_t* client, const char* url,
                      http_write_callback_t callback, void* user_data) {
    return perform_stream_request(client, "GET", url, NULL, 0, NULL, callback, user_data);
}

// HTTP POST with streaming
err_t http_post_stream(http_client_t* client, const char* url, const char* body,
                       http_write_callback_t callback, void* user_data) {
    return perform_stream_request(client, "POST", url, body, body ? strlen(body) : 0,
                                  "application/x-www-form-urlencoded", callback, user_data);
}

// HTTP POST JSON with streaming
err_t http_post_json_stream(http_client_t* client, const char* url, const char* json_body,
                            http_write_callback_t callback, void* user_data) {
    return perform_stream_request(client, "POST", url, json_body, json_body ? strlen(json_body) : 0,
                                  "application/json", callback, user_data);
}
