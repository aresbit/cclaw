// types.h - Core type definitions for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_TYPES_H
#define CCLAW_CORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Forward declarations
typedef struct allocator_t allocator_t;
typedef struct config_t config_t;
typedef struct provider_t provider_t;
typedef struct channel_t channel_t;
typedef struct memory_t memory_t;
typedef struct tool_t tool_t;

// String type (compatible with sp.h)
typedef struct str_t {
    const char* data;
    uint32_t len;
} str_t;

// Dynamic array macro (compatible with sp.h)
#define da(type) type*

// Hash table types
typedef struct ht_entry_t ht_entry_t;
typedef struct ht_t ht_t;

// Generic result type
typedef struct result_t {
    bool success;
    union {
        void* value;
        int64_t integer;
        double number;
        str_t string;
    };
    str_t error;
} result_t;

// Memory category enum (from Rust original)
typedef enum {
    MEMORY_CATEGORY_CORE,
    MEMORY_CATEGORY_DAILY,
    MEMORY_CATEGORY_CONVERSATION,
    MEMORY_CATEGORY_CUSTOM
} memory_category_t;

// Autonomy level (from Rust original)
typedef enum {
    AUTONOMY_LEVEL_READONLY,
    AUTONOMY_LEVEL_SUPERVISED,
    AUTONOMY_LEVEL_FULL
} autonomy_level_t;

// Runtime kind
typedef enum {
    RUNTIME_KIND_NATIVE,
    RUNTIME_KIND_DOCKER,
    RUNTIME_KIND_WASM
} runtime_kind_t;

// Provider types
typedef enum {
    PROVIDER_TYPE_OPENAI,
    PROVIDER_TYPE_ANTHROPIC,
    PROVIDER_TYPE_OPENROUTER,
    PROVIDER_TYPE_OLLAMA,
    PROVIDER_TYPE_GEMINI,
    PROVIDER_TYPE_CUSTOM
} provider_type_t;

// Channel types
typedef enum {
    CHANNEL_TYPE_CLI,
    CHANNEL_TYPE_TELEGRAM,
    CHANNEL_TYPE_DISCORD,
    CHANNEL_TYPE_SLACK,
    CHANNEL_TYPE_WHATSAPP,
    CHANNEL_TYPE_MATRIX,
    CHANNEL_TYPE_EMAIL,
    CHANNEL_TYPE_IRC
} channel_type_t;

// Tool types
typedef enum {
    TOOL_TYPE_SHELL,
    TOOL_TYPE_FILE_READ,
    TOOL_TYPE_FILE_WRITE,
    TOOL_TYPE_MEMORY_STORE,
    TOOL_TYPE_MEMORY_RECALL,
    TOOL_TYPE_MEMORY_FORGET,
    TOOL_TYPE_BROWSER_OPEN
} tool_type_t;

// Memory entry (from Rust original)
typedef struct memory_entry_t {
    str_t id;
    str_t key;
    str_t content;
    memory_category_t category;
    str_t timestamp;
    str_t session_id;
    double score;
} memory_entry_t;

// Chat role enum (used by provider system and conversation)
typedef enum {
    CHAT_ROLE_SYSTEM,
    CHAT_ROLE_USER,
    CHAT_ROLE_ASSISTANT,
    CHAT_ROLE_TOOL
} chat_role_t;

// Chat message (unified definition for provider and conversation systems)
typedef struct chat_message_t {
    chat_role_t role;
    str_t content;
    str_t tool_calls;      // JSON array of tool calls (for assistant)
    str_t tool_call_id;    // ID of tool call (for tool messages)
} chat_message_t;

// Tool call (from Rust original)
typedef struct tool_call_t {
    str_t id;
    str_t name;
    str_t arguments; // JSON string
} tool_call_t;

// Tool result from provider (from Rust original)
typedef struct provider_tool_result_t {
    str_t tool_call_id;
    str_t content;
} provider_tool_result_t;

// Conversation message (from Rust original)
typedef enum {
    CONVERSATION_MESSAGE_TYPE_CHAT,
    CONVERSATION_MESSAGE_TYPE_ASSISTANT_TOOL_CALLS,
    CONVERSATION_MESSAGE_TYPE_TOOL_RESULT
} conversation_message_type_t;

typedef struct conversation_message_t {
    conversation_message_type_t type;
    union {
        chat_message_t chat;
        struct {
            str_t text;
            tool_call_t* tool_calls;
            uint32_t tool_calls_count;
        } assistant_tool_calls;
        provider_tool_result_t tool_result;
    };
} conversation_message_t;

// Channel message (from Rust original)
typedef struct channel_message_t {
    str_t id;
    str_t sender;
    str_t content;
    str_t channel;
    uint64_t timestamp;
} channel_message_t;

// Tool specification (from Rust original)
typedef struct tool_spec_t {
    str_t name;
    str_t description;
    str_t parameters; // JSON schema
} tool_spec_t;

// Utility macros for string literals
#define STR_LIT(s) (str_t){.data = s, .len = (uint32_t)(sizeof(s)-1)}
#define STR_VIEW(s) (str_t){.data = s, .len = (uint32_t)strlen(s)}

// Null string constant
#define STR_NULL (str_t){.data = NULL, .len = 0}

// String comparison
static inline bool str_equal(str_t a, str_t b) {
    if (a.len != b.len) return false;
    if (a.data == b.data) return true;
    if (!a.data || !b.data) return false;
    return memcmp(a.data, b.data, a.len) == 0;
}

static inline bool str_equal_cstr(str_t a, const char* b) {
    if (!a.data || !b) return false;
    return strncmp(a.data, b, a.len) == 0 && b[a.len] == '\0';
}

// String empty check
static inline bool str_empty(str_t s) {
    return s.len == 0 || !s.data;
}

// String duplication (allocates memory)
str_t str_dup(str_t s, allocator_t* alloc);
str_t str_dup_cstr(const char* s, allocator_t* alloc);

// String formatting (allocates memory)
str_t str_format(allocator_t* alloc, const char* fmt, ...);

#endif // CCLAW_CORE_TYPES_H