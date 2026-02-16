// agent.h - Agent core framework for CClaw
// Inspired by Pi Agent Framework (https://github.com/badlogic/pi-mono)
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_AGENT_H
#define CCLAW_CORE_AGENT_H

#include "core/types.h"
#include "core/error.h"
#include "core/tool.h"
#include "core/memory.h"
#include "providers/base.h"
#include "core/channel.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct agent_vtable_t agent_vtable_t;
typedef struct agent_t agent_t;
typedef struct agent_session_t agent_session_t;
typedef struct agent_message_t agent_message_t;
typedef struct agent_context_t agent_context_t;
typedef struct agent_config_t agent_config_t;

// Agent message types (inspired by Pi's conversation model)
typedef enum {
    AGENT_MSG_USER,          // User input
    AGENT_MSG_ASSISTANT,     // Assistant response
    AGENT_MSG_TOOL_CALL,     // Tool call request
    AGENT_MSG_TOOL_RESULT,   // Tool execution result
    AGENT_MSG_SYSTEM,        // System message (not sent to LLM)
    AGENT_MSG_SUMMARY,       // Conversation summary (Pi-style)
} agent_message_type_t;

// Agent message (tree-structured for Pi-style branching)
struct agent_message_t {
    str_t id;                    // Unique message ID
    agent_message_type_t type;
    str_t content;               // Text content

    // For tool calls
    str_t tool_name;
    str_t tool_args;             // JSON arguments
    str_t tool_result;           // Execution result

    // Tree structure (Pi's conversation branching)
    agent_message_t* parent;     // Parent message (NULL for root)
    agent_message_t** children;  // Array of child messages (branches)
    uint32_t child_count;
    uint32_t child_capacity;

    // Navigation
    agent_message_t* prev_sibling;
    agent_message_t* next_sibling;

    // Metadata
    uint64_t timestamp;
    str_t model;                 // Which model generated this
    uint32_t tokens_input;
    uint32_t tokens_output;

    // For partial/streaming content
    bool is_complete;
};

// Agent session (Pi-style conversation tree)
struct agent_session_t {
    str_t id;                        // Session ID
    str_t name;                      // Session name/topic

    // Tree root and current position
    agent_message_t* root;           // First message in conversation
    agent_message_t* current;        // Current position in tree

    // Navigation history
    agent_message_t** history;       // Back/forward navigation stack
    uint32_t history_pos;
    uint32_t history_count;

    // Session metadata
    uint64_t created_at;
    uint64_t last_active;
    uint32_t total_messages;
    uint32_t total_tokens;

    // Session state
    bool is_active;
    str_t working_directory;         // Current working directory for this session

    // Provider settings (per-session override)
    str_t provider_name;
    str_t model;
    double temperature;
};

// Agent configuration
struct agent_config_t {
    // Core behavior
    uint32_t max_iterations;         // Max tool call iterations per request
    uint32_t max_tokens_per_request;
    bool auto_confirm;               // Skip confirmation for safe operations
    autonomy_level_t autonomy_level;

    // Context management
    uint32_t max_context_messages;   // Max messages to include in context
    uint32_t context_window_tokens;  // Token budget for context
    bool enable_summarization;       // Auto-summarize old context (Pi-style)

    // Tool configuration
    bool enable_shell_tool;
    bool enable_file_tools;
    bool enable_memory_tools;
    str_t allowed_shell_commands;    // Comma-separated whitelist
    str_t workspace_root;            // Restrict file operations to this dir

    // Extension system (Pi philosophy: agent extends itself)
    bool enable_extensions;          // Allow agent to create extensions
    str_t extensions_dir;            // Where extensions are stored
    bool hot_reload_extensions;      // Auto-reload on file change

    // UI preferences
    bool stream_responses;           // Stream LLM output
    bool show_token_usage;           // Display token counts
    bool show_tool_calls;            // Display tool execution
};

// Agent context (runtime state)
struct agent_context_t {
    // Subsystems
    provider_t* provider;
    memory_t* memory;
    tool_t** tools;
    uint32_t tool_count;

    // Session management
    agent_session_t** sessions;
    uint32_t session_count;
    uint32_t session_capacity;
    agent_session_t* active_session;

    // Configuration
    agent_config_t config;

    // Extensions (Pi-style self-extension)
    str_t* loaded_extensions;
    uint32_t extension_count;

    // Runtime state
    bool is_running;
    str_t system_prompt;             // Current system prompt
    uint64_t start_time;
};

// Agent structure
struct agent_t {
    agent_context_t* ctx;
    const agent_vtable_t* vtable;
};

// Agent VTable (definition)
struct agent_vtable_t {
    str_t (*get_name)(void);
    str_t (*get_version)(void);

    // Lifecycle
    err_t (*create)(const agent_config_t* config, agent_t** out_agent);
    void (*destroy)(agent_t* agent);

    // Session management (Pi-style tree navigation)
    err_t (*session_create)(agent_t* agent, const str_t* name, agent_session_t** out_session);
    err_t (*session_load)(agent_t* agent, const str_t* session_id, agent_session_t** out_session);
    err_t (*session_save)(agent_t* agent, agent_session_t* session);
    err_t (*session_close)(agent_t* agent, agent_session_t* session);
    err_t (*session_list)(agent_t* agent, str_t** out_ids, uint32_t* out_count);

    // Tree navigation (Pi-style)
    err_t (*navigate_to)(agent_t* agent, agent_message_t* message);
    err_t (*navigate_back)(agent_t* agent);
    err_t (*navigate_forward)(agent_t* agent);
    err_t (*navigate_up)(agent_t* agent);       // To parent
    err_t (*navigate_down)(agent_t* agent, uint32_t child_index);
    err_t (*create_branch)(agent_t* agent, agent_message_t* from_message);

    // Core agent loop
    err_t (*run)(agent_t* agent, agent_session_t* session);
    err_t (*process_message)(agent_t* agent, agent_session_t* session,
                            const str_t* user_input, agent_message_t** out_response);

    // Tool execution
    err_t (*execute_tool)(agent_t* agent, const str_t* tool_name,
                         const str_t* args, str_t* out_result);

    // Extension management (Pi philosophy)
    err_t (*extension_load)(agent_t* agent, const str_t* extension_path);
    err_t (*extension_reload)(agent_t* agent, const str_t* extension_name);
    err_t (*extension_unload)(agent_t* agent, const str_t* extension_name);

    // System prompt management
    err_t (*rebuild_system_prompt)(agent_t* agent, str_t* out_prompt);
};

// ============================================================================
// Agent Lifecycle
// ============================================================================

err_t agent_create(const agent_config_t* config, agent_t** out_agent);
void agent_destroy(agent_t* agent);

// ============================================================================
// Session Management
// ============================================================================

err_t agent_session_create(agent_t* agent, const str_t* name, agent_session_t** out_session);
err_t agent_session_load(agent_t* agent, const str_t* session_id, agent_session_t** out_session);
err_t agent_session_save(agent_t* agent, agent_session_t* session);
void agent_session_close(agent_t* agent, agent_session_t* session);
err_t agent_session_list(agent_t* agent, str_t** out_ids, uint32_t* out_count);

// Session helpers
agent_session_t* agent_session_get_active(agent_t* agent);
err_t agent_session_set_active(agent_t* agent, agent_session_t* session);

// ============================================================================
// Tree Navigation (Pi-style Conversation Tree)
// ============================================================================

err_t agent_navigate_to(agent_t* agent, agent_message_t* message);
err_t agent_navigate_back(agent_t* agent);
err_t agent_navigate_forward(agent_t* agent);
err_t agent_navigate_to_parent(agent_t* agent);
err_t agent_navigate_to_child(agent_t* agent, uint32_t child_index);
err_t agent_create_branch(agent_t* agent, agent_message_t* from_message, agent_message_t** out_branch_root);

// ============================================================================
// Core Agent Loop
// ============================================================================

err_t agent_run(agent_t* agent, agent_session_t* session);
err_t agent_process_message(agent_t* agent, agent_session_t* session,
                           const str_t* user_input, str_t* out_response);
err_t agent_process_single_turn(agent_t* agent, agent_session_t* session,
                                const str_t* user_input, agent_message_t** out_message);

// ============================================================================
// Tool Execution
// ============================================================================

err_t agent_execute_tool(agent_t* agent, const str_t* tool_name,
                        const str_t* args, str_t* out_result);
bool agent_tool_is_available(agent_t* agent, const str_t* tool_name);
err_t agent_tool_list_available(agent_t* agent, str_t** out_names, uint32_t* out_count);

// ============================================================================
// Extension System (Pi Philosophy)
// ============================================================================

err_t agent_extension_load(agent_t* agent, const str_t* extension_path);
err_t agent_extension_reload(agent_t* agent, const str_t* extension_name);
err_t agent_extension_unload(agent_t* agent, const str_t* extension_name);
err_t agent_extension_list_loaded(agent_t* agent, str_t** out_names, uint32_t* out_count);
bool agent_extension_is_loaded(agent_t* agent, const str_t* extension_name);

// ============================================================================
// System Prompt Management
// ============================================================================

err_t agent_rebuild_system_prompt(agent_t* agent, str_t* out_prompt);
err_t agent_rebuild_tools_description(agent_t* agent, str_t* out_description);

// ============================================================================
// Message Helpers
// ============================================================================

agent_message_t* agent_message_create(agent_message_type_t type, const str_t* content);
void agent_message_free(agent_message_t* message);
void agent_message_add_child(agent_message_t* parent, agent_message_t* child);
void agent_message_tree_free(agent_message_t* root);

// Get conversation path from root to current message
err_t agent_message_get_path(agent_message_t* from_root, agent_message_t* to_message,
                             agent_message_t*** out_path, uint32_t* out_count);

// Convert conversation to chat messages for LLM
err_t agent_session_to_chat_messages(agent_session_t* session,
                                     chat_message_t** out_messages,
                                     uint32_t* out_count);

// ============================================================================
// Configuration
// ============================================================================

agent_config_t agent_config_default(void);
void agent_config_free(agent_config_t* config);

// ============================================================================
// Pi-Style Summarization
// ============================================================================

err_t agent_summarize_conversation(agent_t* agent, agent_session_t* session,
                                   agent_message_t* from_message,
                                   agent_message_t* to_message,
                                   str_t* out_summary);
err_t agent_prune_context(agent_t* agent, agent_session_t* session,
                          uint32_t target_messages);

// ============================================================================
// Built-in Agent Implementation
// ============================================================================

const agent_vtable_t* agent_get_default_vtable(void);

// ============================================================================
// Constants
// ============================================================================

#define AGENT_MAX_ITERATIONS_DEFAULT 32
#define AGENT_MAX_CONTEXT_MESSAGES_DEFAULT 50
#define AGENT_CONTEXT_WINDOW_TOKENS_DEFAULT 8000
#define AGENT_EXTENSION_DIR_DEFAULT ".cclaw/extensions"

// Minimal system prompt (Pi philosophy: shortest possible)
#define AGENT_SYSTEM_PROMPT_MINIMAL \
    "You are a helpful AI assistant with access to tools. " \
    "Use tools when needed to help the user. Be concise and direct."

// Extended system prompt with extension philosophy
#define AGENT_SYSTEM_PROMPT_EXTENDED \
    "You are a helpful AI assistant with access to tools.\n\n" \
    "CORE PRINCIPLES:\n" \
    "1. Use tools when they help accomplish the task\n" \
    "2. Read files before editing them\n" \
    "3. Be concise and direct\n\n" \
    "EXTENSION PHILOSOPHY:\n" \
    "- You can extend your own capabilities by writing code\n" \
    "- Extensions are stored in " AGENT_EXTENSION_DIR_DEFAULT "\n" \
    "- You can reload extensions to apply changes\n" \
    "- The system will hot-reload extensions automatically"

#endif // CCLAW_CORE_AGENT_H
