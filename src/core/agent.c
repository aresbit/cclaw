// agent.c - Agent core implementation for CClaw
// Inspired by Pi Agent Framework (https://github.com/badlogic/pi-mono)
// SPDX-License-Identifier: MIT

#include "core/agent.h"
#include "core/alloc.h"
#include "core/channel.h"
#include "cclaw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

// ============================================================================
// Internal Helpers
// ============================================================================

static str_t generate_uuid(void) {
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate_random(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    return str_dup_cstr(uuid_str, NULL);
}

static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char* message_type_to_string(agent_message_type_t type) {
    switch (type) {
        case AGENT_MSG_USER: return "user";
        case AGENT_MSG_ASSISTANT: return "assistant";
        case AGENT_MSG_TOOL_CALL: return "tool_call";
        case AGENT_MSG_TOOL_RESULT: return "tool_result";
        case AGENT_MSG_SYSTEM: return "system";
        case AGENT_MSG_SUMMARY: return "summary";
        default: return "unknown";
    }
}

// ============================================================================
// Message Operations
// ============================================================================

agent_message_t* agent_message_create(agent_message_type_t type, const str_t* content) {
    agent_message_t* msg = calloc(1, sizeof(agent_message_t));
    if (!msg) return NULL;

    msg->id = generate_uuid();
    msg->type = type;
    msg->content = content ? str_dup(*content, NULL) : STR_NULL;
    msg->timestamp = get_timestamp_ms();
    msg->is_complete = true;
    msg->children = NULL;
    msg->child_count = 0;
    msg->child_capacity = 0;

    return msg;
}

void agent_message_free(agent_message_t* message) {
    if (!message) return;

    free((void*)message->id.data);
    free((void*)message->content.data);
    free((void*)message->tool_name.data);
    free((void*)message->tool_args.data);
    free((void*)message->tool_result.data);
    free((void*)message->model.data);

    free(message->children);
    free(message);
}

void agent_message_add_child(agent_message_t* parent, agent_message_t* child) {
    if (!parent || !child) return;

    // Ensure capacity
    if (parent->child_count >= parent->child_capacity) {
        uint32_t new_capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        agent_message_t** new_children = realloc(parent->children,
                                                  sizeof(agent_message_t*) * new_capacity);
        if (!new_children) return;

        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }

    // Add child
    parent->children[parent->child_count] = child;
    child->parent = parent;

    // Link siblings
    if (parent->child_count > 0) {
        agent_message_t* prev = parent->children[parent->child_count - 1];
        prev->next_sibling = child;
        child->prev_sibling = prev;
    }

    parent->child_count++;
}

void agent_message_tree_free(agent_message_t* root) {
    if (!root) return;

    // Recursively free all children first
    for (uint32_t i = 0; i < root->child_count; i++) {
        agent_message_tree_free(root->children[i]);
    }

    agent_message_free(root);
}

err_t agent_message_get_path(agent_message_t* from_root, agent_message_t* to_message,
                             agent_message_t*** out_path, uint32_t* out_count) {
    if (!from_root || !to_message || !out_path || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }

    // Count depth
    uint32_t depth = 0;
    agent_message_t* current = to_message;
    while (current && current != from_root) {
        depth++;
        current = current->parent;
    }

    if (current != from_root) {
        return ERR_NOT_FOUND; // to_message is not a descendant of from_root
    }

    // Allocate path
    agent_message_t** path = calloc(depth, sizeof(agent_message_t*));
    if (!path) return ERR_OUT_OF_MEMORY;

    // Fill path (reverse order)
    current = to_message;
    for (int i = depth - 1; i >= 0; i--) {
        path[i] = current;
        current = current->parent;
    }

    *out_path = path;
    *out_count = depth;
    return ERR_OK;
}

// ============================================================================
// Session Operations
// ============================================================================

static agent_session_t* session_create_internal(const str_t* name) {
    agent_session_t* session = calloc(1, sizeof(agent_session_t));
    if (!session) return NULL;

    session->id = generate_uuid();
    session->name = name ? str_dup(*name, NULL) : str_dup(session->id, NULL);
    session->created_at = get_timestamp_ms();
    session->last_active = session->created_at;
    session->is_active = true;
    session->working_directory = STR_NULL;
    session->provider_name = STR_NULL;
    session->model = STR_NULL;
    session->temperature = 0.7;

    return session;
}

static void session_free(agent_session_t* session) {
    if (!session) return;

    free((void*)session->id.data);
    free((void*)session->name.data);
    free((void*)session->working_directory.data);
    free((void*)session->provider_name.data);
    free((void*)session->model.data);

    if (session->root) {
        agent_message_tree_free(session->root);
    }

    free(session->history);
    free(session);
}

// ============================================================================
// Configuration
// ============================================================================

agent_config_t agent_config_default(void) {
    return (agent_config_t){
        .max_iterations = AGENT_MAX_ITERATIONS_DEFAULT,
        .max_tokens_per_request = 4096,
        .auto_confirm = false,
        .autonomy_level = AUTONOMY_LEVEL_SUPERVISED,

        .max_context_messages = AGENT_MAX_CONTEXT_MESSAGES_DEFAULT,
        .context_window_tokens = AGENT_CONTEXT_WINDOW_TOKENS_DEFAULT,
        .enable_summarization = true,

        .enable_shell_tool = true,
        .enable_file_tools = true,
        .enable_memory_tools = true,
        .allowed_shell_commands = STR_NULL,
        .workspace_root = STR_NULL,

        .enable_extensions = true,
        .extensions_dir = STR_NULL,
        .hot_reload_extensions = true,

        .stream_responses = true,
        .show_token_usage = false,
        .show_tool_calls = true,
    };
}

void agent_config_free(agent_config_t* config) {
    if (!config) return;

    free((void*)config->allowed_shell_commands.data);
    free((void*)config->workspace_root.data);
    free((void*)config->extensions_dir.data);
}

// ============================================================================
// Context Building
// ============================================================================

static err_t build_context_messages(agent_t* agent, agent_session_t* session,
                                    chat_message_t** out_messages, uint32_t* out_count) {
    if (!agent || !session || !out_messages || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }

    // Count messages in current path
    uint32_t path_count = 0;
    agent_message_t* current = session->current;
    while (current) {
        path_count++;
        current = current->parent;
    }

    // Allocate chat messages (+1 for system prompt)
    chat_message_t* messages = calloc(path_count + 1, sizeof(chat_message_t));
    if (!messages) return ERR_OUT_OF_MEMORY;

    // System prompt
    messages[0].role = CHAT_ROLE_SYSTEM;
    // TODO: Build dynamic system prompt
    messages[0].content = str_dup_cstr(AGENT_SYSTEM_PROMPT_EXTENDED, NULL);

    // Build path from root to current
    uint32_t idx = 1;
    current = session->root;
    while (current && idx <= path_count) {
        chat_role_t role;
        switch (current->type) {
            case AGENT_MSG_USER:
                role = CHAT_ROLE_USER;
                break;
            case AGENT_MSG_ASSISTANT:
            case AGENT_MSG_SUMMARY:
                role = CHAT_ROLE_ASSISTANT;
                break;
            case AGENT_MSG_TOOL_CALL:
            case AGENT_MSG_TOOL_RESULT:
                role = CHAT_ROLE_TOOL;
                break;
            default:
                role = CHAT_ROLE_SYSTEM;
                break;
        }

        messages[idx].role = role;
        messages[idx].content = str_dup(current->content, NULL);

        // Navigate to next in path
        if (current == session->current) break;

        // Find which child leads to current
        for (uint32_t i = 0; i < current->child_count; i++) {
            agent_message_t* child = current->children[i];
            agent_message_t* desc = child;
            while (desc && desc != session->current) {
                desc = desc->children && desc->child_count > 0 ? desc->children[0] : NULL;
            }
            if (desc == session->current || child == session->current) {
                current = child;
                break;
            }
        }
        idx++;
    }

    *out_messages = messages;
    *out_count = path_count + 1;
    return ERR_OK;
}

// ============================================================================
// Tool Execution
// ============================================================================

static err_t parse_tool_calls(const str_t* content, tool_call_t** out_calls, uint32_t* out_count) {
    // TODO: Parse JSON tool calls from assistant response
    // This is a simplified placeholder
    *out_calls = NULL;
    *out_count = 0;
    return ERR_OK;
}

static err_t execute_tool_call(agent_t* agent, tool_call_t* call, str_t* out_result) {
    if (!agent || !call || !out_result) return ERR_INVALID_ARGUMENT;

    agent_context_t* ctx = agent->ctx;

    // Find the tool
    for (uint32_t i = 0; i < ctx->tool_count; i++) {
        tool_t* tool = ctx->tools[i];
        str_t tool_name = tool->vtable->get_name();

        if (str_equal(tool_name, call->name)) {
            tool_result_t result = tool_result_create();
            err_t err = tool->vtable->execute(tool, &call->arguments, &result);

            if (err == ERR_OK && result.success) {
                *out_result = str_dup(result.content, NULL);
            } else {
                *out_result = str_dup(result.error_message, NULL);
            }

            tool_result_free(&result);
            return err;
        }
    }

    return ERR_NOT_FOUND;
}

// ============================================================================
// Core Agent Loop
// ============================================================================

static err_t agent_loop_iteration(agent_t* agent, agent_session_t* session,
                                  chat_message_t* messages, uint32_t message_count,
                                  agent_message_t** out_response) {
    if (!agent || !session || !out_response) return ERR_INVALID_ARGUMENT;

    agent_context_t* ctx = agent->ctx;

    // Check provider is initialized
    if (!ctx->provider) {
        return ERR_NOT_INITIALIZED;
    }

    // Call LLM
    chat_response_t* llm_response = NULL;
    const char* model = str_empty(session->model) ? NULL : session->model.data;

    err_t err = ctx->provider->vtable->chat(
        ctx->provider,
        messages, message_count,
        NULL, 0, // TODO: Pass tool definitions
        model,
        session->temperature,
        &llm_response
    );

    if (err != ERR_OK) {
        return err;
    }

    // Create assistant message
    agent_message_t* assistant_msg = agent_message_create(AGENT_MSG_ASSISTANT, &llm_response->content);
    assistant_msg->model = str_dup_cstr(llm_response->model.data ? llm_response->model.data : "unknown", NULL);
    assistant_msg->tokens_input = llm_response->prompt_tokens;
    assistant_msg->tokens_output = llm_response->completion_tokens;

    // Check for tool calls
    if (!str_empty(llm_response->tool_calls)) {
        assistant_msg->type = AGENT_MSG_TOOL_CALL;

        // Parse and execute tool calls
        tool_call_t* tool_calls = NULL;
        uint32_t tool_call_count = 0;
        err = parse_tool_calls(&llm_response->tool_calls, &tool_calls, &tool_call_count);

        if (err == ERR_OK && tool_call_count > 0) {
            // Execute each tool call
            for (uint32_t i = 0; i < tool_call_count; i++) {
                str_t result = STR_NULL;
                err = execute_tool_call(agent, &tool_calls[i], &result);

                // Create tool result message
                agent_message_t* result_msg = agent_message_create(AGENT_MSG_TOOL_RESULT, &result);
                result_msg->tool_name = str_dup(tool_calls[i].name, NULL);

                // Add to tree
                agent_message_add_child(assistant_msg, result_msg);

                free((void*)result.data);
            }
        }

        free(tool_calls);
    }

    chat_response_free(llm_response);

    // Add to conversation tree
    if (session->current) {
        agent_message_add_child(session->current, assistant_msg);
    } else {
        session->root = assistant_msg;
    }
    session->current = assistant_msg;

    *out_response = assistant_msg;
    return ERR_OK;
}

err_t agent_process_message(agent_t* agent, agent_session_t* session,
                           const str_t* user_input, str_t* out_response) {
    if (!agent || !session || !user_input || !out_response) {
        return ERR_INVALID_ARGUMENT;
    }

    // Create user message
    agent_message_t* user_msg = agent_message_create(AGENT_MSG_USER, user_input);

    // Add to conversation tree
    if (session->current) {
        agent_message_add_child(session->current, user_msg);
    } else {
        session->root = user_msg;
    }
    session->current = user_msg;
    session->total_messages++;
    session->last_active = get_timestamp_ms();

    // Build context
    chat_message_t* messages = NULL;
    uint32_t message_count = 0;
    err_t err = build_context_messages(agent, session, &messages, &message_count);
    if (err != ERR_OK) {
        return err;
    }

    // Agent loop with iteration limit
    agent_message_t* response = NULL;
    uint32_t iterations = 0;

    while (iterations < agent->ctx->config.max_iterations) {
        err = agent_loop_iteration(agent, session, messages, message_count, &response);
        if (err != ERR_OK) break;

        // If no tool calls, we're done
        if (response->type != AGENT_MSG_TOOL_CALL) {
            break;
        }

        // Rebuild context with tool results
        free(messages);
        err = build_context_messages(agent, session, &messages, &message_count);
        if (err != ERR_OK) break;

        iterations++;
    }

    free(messages);

    if (response && response->type == AGENT_MSG_ASSISTANT) {
        *out_response = str_dup(response->content, NULL);
        return ERR_OK;
    }

    return err;
}

err_t agent_run(agent_t* agent, agent_session_t* session) {
    if (!agent || !session) return ERR_INVALID_ARGUMENT;

    agent_context_t* ctx = agent->ctx;
    ctx->is_running = true;

    while (ctx->is_running && session->is_active) {
        // This would typically integrate with a channel for input
        // For now, this is a placeholder for the main loop
        break;
    }

    return ERR_OK;
}

// ============================================================================
// Session Management (API)
// ============================================================================

err_t agent_session_create(agent_t* agent, const str_t* name, agent_session_t** out_session) {
    if (!agent || !out_session) return ERR_INVALID_ARGUMENT;

    agent_session_t* session = session_create_internal(name);
    if (!session) return ERR_OUT_OF_MEMORY;

    // Add to agent's session list
    agent_context_t* ctx = agent->ctx;

    if (ctx->session_count >= ctx->session_capacity) {
        uint32_t new_capacity = ctx->session_capacity == 0 ? 4 : ctx->session_capacity * 2;
        agent_session_t** new_sessions = realloc(ctx->sessions,
                                                  sizeof(agent_session_t*) * new_capacity);
        if (!new_sessions) {
            session_free(session);
            return ERR_OUT_OF_MEMORY;
        }
        ctx->sessions = new_sessions;
        ctx->session_capacity = new_capacity;
    }

    ctx->sessions[ctx->session_count++] = session;

    if (!ctx->active_session) {
        ctx->active_session = session;
    }

    *out_session = session;
    return ERR_OK;
}

void agent_session_close(agent_t* agent, agent_session_t* session) {
    if (!agent || !session) return;

    agent_context_t* ctx = agent->ctx;

    // Remove from active sessions
    for (uint32_t i = 0; i < ctx->session_count; i++) {
        if (ctx->sessions[i] == session) {
            // Shift remaining
            for (uint32_t j = i; j < ctx->session_count - 1; j++) {
                ctx->sessions[j] = ctx->sessions[j + 1];
            }
            ctx->session_count--;
            break;
        }
    }

    // Update active session if needed
    if (ctx->active_session == session) {
        ctx->active_session = ctx->session_count > 0 ? ctx->sessions[0] : NULL;
    }

    session->is_active = false;
    session_free(session);
}

agent_session_t* agent_session_get_active(agent_t* agent) {
    if (!agent || !agent->ctx) return NULL;
    return agent->ctx->active_session;
}

err_t agent_session_set_active(agent_t* agent, agent_session_t* session) {
    if (!agent) return ERR_INVALID_ARGUMENT;
    agent->ctx->active_session = session;
    return ERR_OK;
}

// ============================================================================
// Tree Navigation
// ============================================================================

err_t agent_navigate_to(agent_t* agent, agent_message_t* message) {
    if (!agent || !message) return ERR_INVALID_ARGUMENT;

    agent_context_t* ctx = agent->ctx;
    agent_session_t* session = ctx->active_session;
    if (!session) return ERR_INVALID_STATE;

    // Add to history
    if (session->history_pos < session->history_count) {
        session->history[session->history_pos++] = session->current;
    }

    session->current = message;
    return ERR_OK;
}

err_t agent_navigate_back(agent_t* agent) {
    if (!agent) return ERR_INVALID_ARGUMENT;

    agent_context_t* ctx = agent->ctx;
    agent_session_t* session = ctx->active_session;
    if (!session || session->history_pos == 0) return ERR_INVALID_STATE;

    session->history_pos--;
    session->current = session->history[session->history_pos];
    return ERR_OK;
}

err_t agent_navigate_to_parent(agent_t* agent) {
    if (!agent) return ERR_INVALID_ARGUMENT;

    agent_context_t* ctx = agent->ctx;
    agent_session_t* session = ctx->active_session;
    if (!session || !session->current || !session->current->parent) return ERR_INVALID_STATE;

    session->current = session->current->parent;
    return ERR_OK;
}

err_t agent_tool_list_available(agent_t* agent, str_t** out_names, uint32_t* out_count) {
    if (!agent || !out_names || !out_count) return ERR_INVALID_ARGUMENT;

    // Return empty list for now (would query tool registry in full implementation)
    *out_names = NULL;
    *out_count = 0;
    return ERR_OK;
}

err_t agent_create_branch(agent_t* agent, agent_message_t* from_message, agent_message_t** out_branch_root) {
    if (!agent || !from_message || !out_branch_root) return ERR_INVALID_ARGUMENT;

    // Create a new "branch point" message
    agent_message_t* branch_root = agent_message_create(AGENT_MSG_SYSTEM, NULL);
    if (!branch_root) return ERR_OUT_OF_MEMORY;

    // Link to parent
    branch_root->parent = from_message->parent;
    agent_message_add_child(from_message->parent, branch_root);

    // Copy relevant state from from_message
    branch_root->content = str_dup(from_message->content, NULL);

    *out_branch_root = branch_root;
    return ERR_OK;
}

// ============================================================================
// Agent Lifecycle
// ============================================================================

err_t agent_create(const agent_config_t* config, agent_t** out_agent) {
    if (!out_agent) return ERR_INVALID_ARGUMENT;

    agent_t* agent = calloc(1, sizeof(agent_t));
    if (!agent) return ERR_OUT_OF_MEMORY;

    agent_context_t* ctx = calloc(1, sizeof(agent_context_t));
    if (!ctx) {
        free(agent);
        return ERR_OUT_OF_MEMORY;
    }

    // Copy configuration
    ctx->config = config ? *config : agent_config_default();
    ctx->start_time = get_timestamp_ms();
    ctx->is_running = false;

    // TODO: Initialize provider, memory, tools
    ctx->provider = NULL;
    ctx->memory = NULL;
    ctx->tools = NULL;
    ctx->tool_count = 0;

    agent->ctx = ctx;
    agent->vtable = agent_get_default_vtable();

    *out_agent = agent;
    return ERR_OK;
}

void agent_destroy(agent_t* agent) {
    if (!agent) return;

    agent_context_t* ctx = agent->ctx;
    if (ctx) {
        // Close all sessions
        for (uint32_t i = 0; i < ctx->session_count; i++) {
            session_free(ctx->sessions[i]);
        }
        free(ctx->sessions);

        // Free tools
        for (uint32_t i = 0; i < ctx->tool_count; i++) {
            tool_free(ctx->tools[i]);
        }
        free(ctx->tools);

        // Free extensions list
        for (uint32_t i = 0; i < ctx->extension_count; i++) {
            free((void*)ctx->loaded_extensions[i].data);
        }
        free(ctx->loaded_extensions);

        agent_config_free(&ctx->config);
        free((void*)ctx->system_prompt.data);

        free(ctx);
    }

    free(agent);
}

// ============================================================================
// VTable Implementation
// ============================================================================

static str_t agent_get_name_impl(void) { return STR_LIT("cclaw-agent"); }
static str_t agent_get_version_impl(void) { return STR_LIT("0.1.0"); }

static err_t agent_create_impl(const agent_config_t* config, agent_t** out_agent) {
    return agent_create(config, out_agent);
}

static void agent_destroy_impl(agent_t* agent) {
    agent_destroy(agent);
}

static err_t agent_session_create_impl(agent_t* agent, const str_t* name, agent_session_t** out_session) {
    return agent_session_create(agent, name, out_session);
}

static err_t agent_session_close_impl(agent_t* agent, agent_session_t* session) {
    agent_session_close(agent, session);
    return ERR_OK;
}

static err_t agent_navigate_to_impl(agent_t* agent, agent_message_t* message) {
    return agent_navigate_to(agent, message);
}

static err_t agent_navigate_back_impl(agent_t* agent) {
    return agent_navigate_back(agent);
}

static err_t agent_create_branch_impl(agent_t* agent, agent_message_t* from_message) {
    agent_message_t* branch;
    return agent_create_branch(agent, from_message, &branch);
}

static err_t agent_run_impl(agent_t* agent, agent_session_t* session) {
    return agent_run(agent, session);
}

static err_t agent_process_message_impl(agent_t* agent, agent_session_t* session,
                                       const str_t* user_input, agent_message_t** out_response) {
    str_t response = STR_NULL;
    err_t err = agent_process_message(agent, session, user_input, &response);

    if (err == ERR_OK && out_response) {
        *out_response = agent_message_create(AGENT_MSG_ASSISTANT, &response);
    }

    free((void*)response.data);
    return err;
}

static err_t agent_rebuild_system_prompt_impl(agent_t* agent, str_t* out_prompt) {
    if (!agent || !out_prompt) return ERR_INVALID_ARGUMENT;

    // Build minimal but effective system prompt (Pi philosophy)
    str_t prompt = str_dup_cstr(AGENT_SYSTEM_PROMPT_EXTENDED, NULL);

    // TODO: Add dynamic tool descriptions
    // TODO: Add extension capabilities

    *out_prompt = prompt;
    return ERR_OK;
}

static const agent_vtable_t g_default_vtable = {
    .get_name = agent_get_name_impl,
    .get_version = agent_get_version_impl,
    .create = agent_create_impl,
    .destroy = agent_destroy_impl,
    .session_create = agent_session_create_impl,
    .session_close = agent_session_close_impl,
    .navigate_to = agent_navigate_to_impl,
    .navigate_back = agent_navigate_back_impl,
    .create_branch = agent_create_branch_impl,
    .run = agent_run_impl,
    .process_message = agent_process_message_impl,
    .rebuild_system_prompt = agent_rebuild_system_prompt_impl,
};

const agent_vtable_t* agent_get_default_vtable(void) {
    return &g_default_vtable;
}

// ============================================================================
// Core CClaw API
// ============================================================================

err_t cclaw_init(void) {
    fprintf(stderr, "Initializing CClaw v%s\n", CCLAW_VERSION_STRING);

    // Initialize subsystems
    err_t err = channel_registry_init();
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to initialize channel registry: %s\n", error_to_string(err));
        return err;
    }

    return ERR_OK;
}

void cclaw_shutdown(void) {
    fprintf(stderr, "Shutting down CClaw\n");
    channel_registry_shutdown();
}

void cclaw_get_version(uint32_t* major, uint32_t* minor, uint32_t* patch) {
    if (major) *major = CCLAW_VERSION_MAJOR;
    if (minor) *minor = CCLAW_VERSION_MINOR;
    if (patch) *patch = CCLAW_VERSION_PATCH;
}

const char* cclaw_get_version_string(void) {
    return CCLAW_VERSION_STRING;
}

const char* cclaw_get_platform_name(void) {
    if (CCLAW_PLATFORM_WINDOWS) return "Windows";
    if (CCLAW_PLATFORM_LINUX) return "Linux";
    if (CCLAW_PLATFORM_MACOS) return "macOS";
    if (CCLAW_PLATFORM_ANDROID) return "Android";
    return "Unknown";
}

bool cclaw_is_platform_windows(void) { return CCLAW_PLATFORM_WINDOWS; }
bool cclaw_is_platform_linux(void) { return CCLAW_PLATFORM_LINUX; }
bool cclaw_is_platform_macos(void) { return CCLAW_PLATFORM_MACOS; }
bool cclaw_is_platform_android(void) { return CCLAW_PLATFORM_ANDROID; }
