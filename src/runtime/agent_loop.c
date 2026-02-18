// agent_loop.c - Agent runtime loop for CClaw
// Pi-style interactive agent runtime
// SPDX-License-Identifier: MIT

#include "core/agent.h"
#include "core/config.h"
#include "providers/router.h"
#include "cclaw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

// Runtime state
static struct {
    agent_t* agent;
    agent_session_t* session;
    bool running;
    struct termios original_termios;
} g_runtime = {0};

// Signal handler
static void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\n\n[Received interrupt, saving session...]\n");
        g_runtime.running = false;
    }
}

// Setup raw mode for interactive input
static void setup_terminal(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &g_runtime.original_termios);
    raw = g_runtime.original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_runtime.original_termios);
}

// Print message with formatting
static void print_user_prompt(const char* workspace) {
    printf("\033[36m[%s]\033[0m ", workspace ? workspace : "cclaw");
    printf("\033[1m>\033[0m ");
    fflush(stdout);
}

static void print_assistant_response(const char* response) {
    printf("\n\033[32mAgent:\033[0m %s\n", response);
}

static void print_tool_call(const char* tool_name, const char* args) {
    printf("\033[33m[Tool: %s]\033[0m ", tool_name);
    if (args && strlen(args) < 80) {
        printf("%s\n", args);
    } else {
        printf("(...)\n");
    }
}

static void print_error(const char* msg) {
    fprintf(stderr, "\033[31mError: %s\033[0m\n", msg);
}

// Read a line from stdin (with proper allocation)
static char* read_line(void) {
    size_t capacity = 256;
    size_t length = 0;
    char* buffer = malloc(capacity);

    if (!buffer) return NULL;

    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char* new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
        buffer[length++] = (char)c;
    }

    buffer[length] = '\0';
    return buffer;
}

// Built-in commands
static bool handle_builtin_command(const char* input, agent_t* agent, agent_session_t* session) {
    if (strcmp(input, "/quit") == 0 || strcmp(input, "/q") == 0) {
        return false; // Signal to exit
    }

    if (strcmp(input, "/help") == 0 || strcmp(input, "/?") == 0) {
        printf("\n\033[1mCommands:\033[0m\n");
        printf("  /help, /?       Show this help\n");
        printf("  /quit, /q       Exit the agent\n");
        printf("  /new            Start a new conversation branch\n");
        printf("  /back           Go back to parent message\n");
        printf("  /sessions       List active sessions\n");
        printf("  /clear          Clear screen\n");
        printf("  /tools          List available tools\n");
        printf("  /model <name>   Switch model\n");
        printf("  /temp <0-2>     Set temperature\n");
        printf("\n");
        return true;
    }

    if (strcmp(input, "/clear") == 0) {
        printf("\033[2J\033[H");
        return true;
    }

    if (strcmp(input, "/new") == 0) {
        agent_message_t* branch;
        err_t err = agent_create_branch(agent, session->current, &branch);
        if (err == ERR_OK) {
            session->current = branch;
            printf("\033[32m[Created new branch]\033[0m\n");
        }
        return true;
    }

    if (strcmp(input, "/back") == 0) {
        err_t err = agent_navigate_to_parent(agent);
        if (err == ERR_OK) {
            printf("\033[32m[Navigated back]\033[0m\n");
        }
        return true;
    }

    if (strcmp(input, "/tools") == 0) {
        str_t* names = NULL;
        uint32_t count = 0;
        err_t err = agent_tool_list_available(agent, &names, &count);

        if (err == ERR_OK) {
            printf("\n\033[1mAvailable Tools:\033[0m\n");
            for (uint32_t i = 0; i < count; i++) {
                printf("  - %.*s\n", (int)names[i].len, names[i].data);
                free((void*)names[i].data);
            }
            printf("\n");
            free(names);
        }
        return true;
    }

    if (strncmp(input, "/model ", 7) == 0) {
        const char* model = input + 7;
        free((void*)session->model.data);
        session->model = str_dup_cstr(model, NULL);
        printf("\033[32m[Model set to: %s]\033[0m\n", model);
        return true;
    }

    if (strncmp(input, "/temp ", 6) == 0) {
        double temp = atof(input + 6);
        if (temp >= 0.0 && temp <= 2.0) {
            session->temperature = temp;
            printf("\033[32m[Temperature set to: %.2f]\033[0m\n", temp);
        } else {
            print_error("Temperature must be between 0.0 and 2.0");
        }
        return true;
    }

    return false; // Not a builtin command
}

// Initialize agent runtime
err_t agent_runtime_init(config_t* config) {
    if (!config) return ERR_INVALID_ARGUMENT;

    // Create agent configuration
    agent_config_t agent_config = agent_config_default();
    agent_config.autonomy_level = config->autonomy.level;
    agent_config.enable_shell_tool = true;  // Default enable
    agent_config.workspace_root = str_dup(config->workspace_dir, NULL);

    // Create agent
    err_t err = agent_create(&agent_config, &g_runtime.agent);
    if (err != ERR_OK) {
        return err;
    }

    // Create default session
    str_t session_name = STR_LIT("default");
    err = agent_session_create(g_runtime.agent, &session_name, &g_runtime.session);
    if (err != ERR_OK) {
        agent_destroy(g_runtime.agent);
        return err;
    }

    // Set default model for session
    if (!str_empty(config->default_model)) {
        g_runtime.session->model = str_dup(config->default_model, NULL);
    }

    // Set up signal handler
    signal(SIGINT, signal_handler);

    // Initialize provider
    if (!str_empty(config->api_key)) {
        // Initialize provider registry
        provider_registry_init();

        // Create provider configuration
        provider_config_t provider_config = {
            .name = config->default_provider,
            .api_key = config->api_key,
            .base_url = STR_NULL,
            .default_model = config->default_model,
            .default_temperature = config->default_temperature,
            .max_tokens = 4096,
            .timeout_ms = 60000,
            .stream = false,
            .max_retries = 3,
            .retry_delay_ms = 1000
        };

        // Get provider name
        const char* provider_name = str_empty(config->default_provider) ? "openrouter" : config->default_provider.data;

        // Create provider
        provider_t* provider = NULL;
        err_t provider_err = provider_create(provider_name, &provider_config, &provider);
        if (provider_err == ERR_OK) {
            g_runtime.agent->ctx->provider = provider;
        } else {
            fprintf(stderr, "Warning: Failed to initialize provider '%s': %d\n", provider_name, provider_err);
        }
    }

    g_runtime.running = true;

    return ERR_OK;
}

// Shutdown agent runtime
void agent_runtime_shutdown(void) {
    if (g_runtime.agent) {
        agent_destroy(g_runtime.agent);
        g_runtime.agent = NULL;
    }
    g_runtime.session = NULL;
    g_runtime.running = false;
}

// Run interactive agent loop (Pi-style)
err_t agent_runtime_run_interactive(void) {
    if (!g_runtime.agent || !g_runtime.session) {
        return ERR_INVALID_STATE;
    }

    printf("\033[2J\033[H"); // Clear screen
    printf("\033[1m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║           CClaw Agent (Pi-Style Conversation)            ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Type /help for commands  |  /quit to exit               ║\n");
    printf("║  Branches supported: use /new to create branches         ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    char* workspace = strndup(g_runtime.session->working_directory.data,
                              g_runtime.session->working_directory.len);

    while (g_runtime.running) {
        print_user_prompt(workspace);

        char* input = read_line();
        if (!input) {
            break;
        }

        // Skip empty lines
        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        // Handle builtin commands
        if (input[0] == '/') {
            if (!handle_builtin_command(input, g_runtime.agent, g_runtime.session)) {
                free(input);
                break; // Exit requested
            }
            free(input);
            continue;
        }

        // Process user message
        str_t user_msg = STR_VIEW(input);
        str_t response = STR_NULL;

        printf("\033[90m[thinking...]\033[0m\r");
        fflush(stdout);

        err_t err = agent_process_message(g_runtime.agent, g_runtime.session, &user_msg, &response);

        printf("\033[K"); // Clear line

        if (err == ERR_OK) {
            print_assistant_response(response.data);
            free((void*)response.data);
        } else {
            print_error("Failed to process message");
        }

        free(input);
    }

    free(workspace);

    printf("\n\033[32m[Session saved. Goodbye!]\033[0m\n");
    return ERR_OK;
}

// Run single message mode (non-interactive)
err_t agent_runtime_run_single(const char* message, char** out_response) {
    if (!g_runtime.agent || !g_runtime.session || !message || !out_response) {
        return ERR_INVALID_ARGUMENT;
    }

    str_t user_msg = STR_VIEW(message);
    str_t response = STR_NULL;

    err_t err = agent_process_message(g_runtime.agent, g_runtime.session, &user_msg, &response);

    if (err == ERR_OK) {
        *out_response = strndup(response.data, response.len);
        free((void*)response.data);
    }

    return err;
}
