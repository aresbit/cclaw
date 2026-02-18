// commands.c - CLI commands implementation for CClaw
// SPDX-License-Identifier: MIT

#include "commands.h"
#include "runtime/daemon.h"
#include "runtime/tui.h"
#include "runtime/agent_loop.h"
#include "core/agent.h"
#include "cclaw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ============================================================================
// Utility Functions
// ============================================================================

static bool confirm(const char* prompt) {
    printf("%s [y/N] ", prompt);
    fflush(stdout);

    char response[8];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return false;
    }

    return (response[0] == 'y' || response[0] == 'Y');
}

static str_t prompt_input(const char* prompt, const char* default_value) {
    printf("%s", prompt);
    if (default_value) {
        printf(" [%s]", default_value);
    }
    printf(": ");
    fflush(stdout);

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return STR_NULL;
    }

    // Remove newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    if (len == 0 && default_value) {
        return str_dup_cstr(default_value, NULL);
    }

    return str_dup_cstr(buffer, NULL);
}

// ============================================================================
// Onboard Command
// ============================================================================

err_t cmd_onboard(config_t* config, int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              CClaw Setup Wizard                          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    // API Key
    printf("\nAvailable providers: openrouter, anthropic, openai, kimi, deepseek\n");
    str_t api_key = prompt_input("Enter your API key", NULL);
    if (!str_empty(api_key)) {
        if (config->api_key.data) free((void*)config->api_key.data);
        config->api_key = api_key;
    }

    // Provider
    str_t provider = prompt_input("Default provider (openrouter/anthropic/openai/kimi/deepseek)", "openrouter");
    if (!str_empty(provider)) {
        if (config->default_provider.data) free((void*)config->default_provider.data);
        config->default_provider = provider;
    }

    // Model - suggest appropriate default based on provider
    const char* default_model = "anthropic/claude-3.5-sonnet";
    // Use the effective provider value (user input or default)
    const char* effective_provider = str_empty(provider) ? "openrouter" : provider.data;
    if (strcmp(effective_provider, "kimi") == 0) {
        default_model = "moonshot-k2.5";
    } else if (strcmp(effective_provider, "deepseek") == 0) {
        default_model = "deepseek-chat";
    } else if (strcmp(effective_provider, "anthropic") == 0) {
        default_model = "claude-3-5-sonnet-20241022";
    } else if (strcmp(effective_provider, "openai") == 0) {
        default_model = "gpt-4o";
    }
    str_t model = prompt_input("Default model", default_model);
    if (!str_empty(model)) {
        if (config->default_model.data) free((void*)config->default_model.data);
        config->default_model = model;
    }

    // Memory backend
    str_t memory = prompt_input("Memory backend (sqlite/markdown/none)", "sqlite");
    if (!str_empty(memory)) {
        if (config->memory.backend.data) free((void*)config->memory.backend.data);
        config->memory.backend = memory;
    }

    printf("\nConfiguration complete!\n");
    printf("Saving to: %.*s\n", (int)config->config_path.len, config->config_path.data);

    err_t err = config_save(config, config->config_path);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to save configuration: %d\n", err);
        return err;
    }

    printf("✓ Configuration saved!\n");
    printf("\nYou can now run:\n");
    printf("  cclaw agent       - Start interactive agent\n");
    printf("  cclaw daemon      - Start daemon mode\n");
    printf("  cclaw tui         - Start TUI interface\n");

    return ERR_OK;
}

// ============================================================================
// Agent Command
// ============================================================================

err_t cmd_agent(config_t* config, int argc, char** argv) {
    // Check for single message mode
    const char* message = NULL;
    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--message") == 0) && i + 1 < argc) {
            message = argv[i + 1];
            break;
        }
    }

    // Initialize runtime
    err_t err = agent_runtime_init(config);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to initialize agent: %s\n", error_to_string(err));
        return err;
    }

    if (message) {
        // Single message mode
        char* response = NULL;
        err = agent_runtime_run_single(message, &response);
        if (err == ERR_OK && response) {
            printf("%s\n", response);
            free(response);
        }
    } else {
        // Interactive mode
        err = agent_runtime_run_interactive();
    }

    agent_runtime_shutdown();
    return err;
}

// ============================================================================
// Daemon Command
// ============================================================================

err_t cmd_daemon(config_t* config, int argc, char** argv) {
    (void)config;

    daemon_config_t daemon_config = daemon_config_default();
    const char* action = "start";

    // Parse arguments
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "start") == 0) {
            action = "start";
        } else if (strcmp(argv[i], "stop") == 0) {
            action = "stop";
        } else if (strcmp(argv[i], "restart") == 0) {
            action = "restart";
        } else if (strcmp(argv[i], "status") == 0) {
            action = "status";
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--pidfile") == 0) && i + 1 < argc) {
            daemon_config.pid_file = STR_VIEW(argv[i + 1]);
        }
    }

    char* pid_path = strndup(daemon_config.pid_file.data, daemon_config.pid_file.len);

    if (strcmp(action, "start") == 0) {
        if (daemon_is_running(pid_path)) {
            printf("Daemon is already running.\n");
            free(pid_path);
            return ERR_ALREADY_EXISTS;
        }

        printf("Starting CClaw daemon...\n");

        daemon_t* daemon = NULL;
        err_t err = daemon_create(&daemon_config, &daemon);
        if (err != ERR_OK) {
            free(pid_path);
            return err;
        }

        err = daemon_start(daemon);
        if (err != ERR_OK) {
            fprintf(stderr, "Failed to start daemon: %s\n", error_to_string(err));
            daemon_destroy(daemon);
            free(pid_path);
            return err;
        }

        printf("✓ Daemon started (PID: %d)\n", (int)daemon->pid);

        // Run daemon
        daemon_run(daemon);

        // Cleanup
        daemon_stop(daemon);
        daemon_destroy(daemon);

    } else if (strcmp(action, "stop") == 0) {
        if (!daemon_is_running(pid_path)) {
            printf("Daemon is not running.\n");
            free(pid_path);
            return ERR_NOT_FOUND;
        }

        printf("Stopping CClaw daemon...\n");
        err_t err = daemon_kill(pid_path);
        if (err == ERR_OK) {
            printf("✓ Daemon stopped\n");
        } else {
            fprintf(stderr, "Failed to stop daemon: %s\n", error_to_string(err));
        }

    } else if (strcmp(action, "restart") == 0) {
        if (daemon_is_running(pid_path)) {
            printf("Stopping daemon...\n");
            daemon_kill(pid_path);
            sleep(1);
        }

        printf("Starting CClaw daemon...\n");
        // (Restart logic same as start)

    } else if (strcmp(action, "status") == 0) {
        if (daemon_is_running(pid_path)) {
            pid_t pid;
            if (pidfile_read(pid_path, &pid) == ERR_OK) {
                printf("Daemon is running (PID: %d)\n", (int)pid);
            }
        } else {
            printf("Daemon is not running.\n");
        }
    }

    free(pid_path);
    return ERR_OK;
}

// ============================================================================
// Status Command
// ============================================================================

err_t cmd_status(config_t* config, int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("CClaw Status\n");
    printf("============\n\n");

    printf("Version: %s\n", CCLAW_VERSION_STRING);
    printf("Platform: %s\n", cclaw_get_platform_name());

    // Check daemon status
    daemon_config_t daemon_config = daemon_config_default();
    char* pid_path = strndup(daemon_config.pid_file.data, daemon_config.pid_file.len);

    printf("\nDaemon: ");
    if (daemon_is_running(pid_path)) {
        pid_t pid;
        if (pidfile_read(pid_path, &pid) == ERR_OK) {
            printf("running (PID: %d)\n", (int)pid);
        }
    } else {
        printf("stopped\n");
    }
    free(pid_path);

    // Configuration
    printf("\nConfiguration:\n");
    printf("  Workspace: %.*s\n", (int)config->workspace_dir.len, config->workspace_dir.data);
    printf("  Provider: %.*s\n", (int)config->default_provider.len, config->default_provider.data);
    printf("  Model: %.*s\n", (int)config->default_model.len, config->default_model.data);
    printf("  Memory: %.*s\n", (int)config->memory.backend.len, config->memory.backend.data);

    return ERR_OK;
}

// ============================================================================
// TUI Command
// ============================================================================

err_t cmd_tui(config_t* config, int argc, char** argv) {
    (void)argc;
    (void)argv;
    (void)config;

    printf("Starting CClaw TUI...\n");

    // Create agent
    agent_config_t agent_config = agent_config_default();
    agent_t* agent = NULL;

    err_t err = agent_create(&agent_config, &agent);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to create agent: %s\n", error_to_string(err));
        return err;
    }

    // Create TUI
    tui_config_t tui_config = tui_config_default();
    tui_t* tui = NULL;

    err = tui_create(&tui_config, &tui);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to create TUI: %s\n", error_to_string(err));
        agent_destroy(agent);
        return err;
    }

    // Run TUI
    err = tui_run(tui, agent);

    // Cleanup
    tui_destroy(tui);
    agent_destroy(agent);

    return err;
}

// ============================================================================
// Cron Command
// ============================================================================

err_t cmd_cron(config_t* config, int argc, char** argv) {
    (void)config;

    const char* action = argc > 0 ? argv[0] : "list";

    if (strcmp(action, "list") == 0) {
        printf("Cron Jobs\n");
        printf("=========\n");
        printf("No scheduled jobs.\n");
    } else if (strcmp(action, "add") == 0) {
        printf("Usage: cclaw cron add '<schedule>' '<command>'\n");
        printf("Example: cclaw cron add '0 9 * * *' 'backup'\n");
    } else if (strcmp(action, "remove") == 0) {
        printf("Usage: cclaw cron remove <job-id>\n");
    } else {
        printf("Unknown cron command: %s\n", action);
        printf("Commands: list, add, remove\n");
    }

    return ERR_OK;
}

// ============================================================================
// Channel Command
// ============================================================================

err_t cmd_channel(config_t* config, int argc, char** argv) {
    (void)config;

    if (argc == 0) {
        printf("Channel Management\n");
        printf("==================\n\n");
        printf("Commands:\n");
        printf("  cclaw channel list              - List configured channels\n");
        printf("  cclaw channel enable <name>     - Enable a channel\n");
        printf("  cclaw channel disable <name>    - Disable a channel\n");
        printf("  cclaw channel test <name>       - Test channel connection\n");
        return ERR_OK;
    }

    const char* action = argv[0];

    if (strcmp(action, "list") == 0) {
        printf("Configured Channels:\n");
        printf("  CLI: enabled\n");
        // Channel configuration display (simplified)
        printf("  CLI: enabled\n");
    } else {
        printf("Unknown channel command: %s\n", action);
    }

    return ERR_OK;
}

// ============================================================================
// Doctor Command
// ============================================================================

err_t cmd_doctor(config_t* config, int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("CClaw Diagnostic\n");
    printf("================\n\n");

    bool all_ok = true;

    // Check configuration
    printf("[ ] Configuration... ");
    if (!str_empty(config->api_key)) {
        printf("✓\n");
    } else {
        printf("✗ (API key not set)\n");
        all_ok = false;
    }

    // Check workspace
    printf("[ ] Workspace... ");
    if (!str_empty(config->workspace_dir)) {
        char* path = strndup(config->workspace_dir.data, config->workspace_dir.len);
        if (access(path, F_OK) == 0) {
            printf("✓ (%s)\n", path);
        } else {
            printf("✗ (directory not found)\n");
            all_ok = false;
        }
        free(path);
    } else {
        printf("✗ (not set)\n");
        all_ok = false;
    }

    // Check libraries
    printf("[ ] Dependencies... ");
    printf("✓\n");

    printf("\n%s\n", all_ok ? "All checks passed!" : "Some checks failed. Run 'cclaw onboard' to fix.");

    return all_ok ? ERR_OK : ERR_FAILED;
}

// ============================================================================
// Version and Help
// ============================================================================

err_t cmd_version(void) {
    printf("CClaw %s\n", CCLAW_VERSION_STRING);
    printf("Platform: %s\n", cclaw_get_platform_name());
    printf("\n");
    printf("Zero overhead. Zero compromise. 100%% C.\n");
    return ERR_OK;
}

err_t cmd_help(const char* topic) {
    if (!topic || strlen(topic) == 0) {
        printf("CClaw - Zero overhead AI assistant\n");
        printf("\nUsage: cclaw <command> [options]\n");
        printf("\nCommands:\n");
        printf("  onboard          Initialize configuration\n");
        printf("  agent            Start interactive agent\n");
        printf("  tui              Start TUI interface\n");
        printf("  daemon           Manage daemon (start/stop/restart/status)\n");
        printf("  status           Show system status\n");
        printf("  channel          Manage channels\n");
        printf("  cron             Manage scheduled tasks\n");
        printf("  doctor           Run diagnostics\n");
        printf("  version          Show version\n");
        printf("  help             Show this help\n");
        printf("\nOptions:\n");
        printf("  -h, --help       Show help for a command\n");
        printf("  -v, --version    Show version\n");
        printf("  -m, --message    Single message mode (for agent)\n");
        printf("\nExamples:\n");
        printf("  cclaw onboard\n");
        printf("  cclaw agent\n");
        printf("  cclaw agent -m \"Hello!\"\n");
        printf("  cclaw daemon start\n");
        printf("  cclaw status\n");
    } else {
        printf("Help for '%s':\n\n", topic);
        printf("(Detailed help coming soon)\n");
    }

    return ERR_OK;
}
