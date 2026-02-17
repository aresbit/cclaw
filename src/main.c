// main.c - CClaw entry point
// SPDX-License-Identifier: MIT

#define SP_IMPLEMENTATION
#include "sp.h"

#include "cclaw.h"
#include "core/error.h"
#include "core/config.h"
#include "core/alloc.h"
#include "core/channel.h"
#include "cli/commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Command line interface structure
typedef struct cli_args_t {
    bool help;
    bool version;
    str_t command;
    int sub_argc;
    char** sub_argv;
} cli_args_t;

// Forward declarations
static void print_help(void);
static void print_version(void);
static err_t parse_args(int argc, char** argv, cli_args_t* args);
static err_t handle_command(cli_args_t* args, config_t* config);

// Main entry point
int main(int argc, char** argv) {
    // Disable tagged pointers on Android (requires ARM64)
    #if defined(__ANDROID__) && defined(__aarch64__)
    setenv("MTE_ENABLED", "0", 1);
    #endif

    // Parse command line arguments
    cli_args_t args = {0};

    err_t err = parse_args(argc, argv, &args);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to parse arguments\n");
        return 1;
    }

    // Handle help and version requests
    if (args.help) {
        if (args.sub_argc > 0) {
            cmd_help(args.sub_argv[0]);
        } else {
            print_help();
        }
        return 0;
    }

    if (args.version) {
        print_version();
        return 0;
    }

    // Initialize CClaw
    err = cclaw_init();
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to initialize CClaw: %s\n", error_to_string(err));
        return 1;
    }

    // Load or create configuration
    config_t* config = NULL;
    err = config_load(STR_NULL, &config);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to load configuration: %s\n", error_to_string(err));
        cclaw_shutdown();
        return 1;
    }

    // Apply environment variable overrides
    config_apply_env_overrides(config);

    // Handle the command
    err = handle_command(&args, config);
    if (err != ERR_OK) {
        if (err != ERR_OK) {  // Don't print error for help/version
            fprintf(stderr, "Command failed: %s\n", error_to_string(err));
        }
        config_destroy(config);
        cclaw_shutdown();
        return 1;
    }

    // Cleanup
    config_destroy(config);
    cclaw_shutdown();

    return 0;
}

// Print help message
static void print_help(void) {
    printf("CClaw - Zero overhead AI assistant (C port of ZeroClaw)\n");
    printf("Version: %s\n", CCLAW_VERSION_STRING);
    printf("\n");
    printf("Usage: cclaw [OPTIONS] <COMMAND>\n");
    printf("\n");
    printf("Commands:\n");
    printf("  onboard          Initialize workspace and configuration\n");
    printf("  agent            Start the AI agent loop\n");
    printf("  tui              Start TUI interface\n");
    printf("  daemon           Manage daemon (start/stop/restart/status)\n");
    printf("  status           Show system status\n");
    printf("  doctor           Run diagnostics\n");
    printf("  channel          Manage channels\n");
    printf("  cron             Manage scheduled tasks\n");
    printf("  version          Show version information\n");
    printf("  help             Show this help message\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help       Show this help message\n");
    printf("  -v, --version    Show version information\n");
    printf("\n");
    printf("Examples:\n");
    printf("  cclaw onboard\n");
    printf("  cclaw agent\n");
    printf("  cclaw agent -m \"Hello!\"\n");
    printf("  cclaw daemon start\n");
    printf("  cclaw status\n");
    printf("\n");
}

// Print version information
static void print_version(void) {
    cmd_version();
}

// Parse command line arguments
static err_t parse_args(int argc, char** argv, cli_args_t* args) {
    if (argc < 2) {
        args->help = true;
        return ERR_OK;
    }

    int i = 1;

    // Parse global options
    while (i < argc) {
        str_t arg = STR_VIEW(argv[i]);

        if (str_equal_cstr(arg, "-h") || str_equal_cstr(arg, "--help")) {
            args->help = true;
            i++;
        }
        else if (str_equal_cstr(arg, "-v") || str_equal_cstr(arg, "--version")) {
            args->version = true;
            i++;
        }
        else if (str_equal_cstr(arg, "--")) {
            i++;
            break;
        }
        else if (argv[i][0] == '-') {
            // Unknown option - store as sub-arg for command to handle
            break;
        }
        else {
            // This is the command
            args->command = arg;
            i++;
            break;
        }
    }

    // Remaining args are sub-command args
    args->sub_argc = argc - i;
    args->sub_argv = argv + i;

    return ERR_OK;
}

// Handle command execution
static err_t handle_command(cli_args_t* args, config_t* config) {
    // Get command name
    char cmd[64] = {0};
    if (!str_empty(args->command)) {
        int len = args->command.len < 63 ? args->command.len : 63;
        memcpy(cmd, args->command.data, len);
        cmd[len] = '\0';
    } else {
        args->help = true;
        return ERR_OK;
    }

    // Route to appropriate command handler
    if (strcmp(cmd, "onboard") == 0) {
        return cmd_onboard(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "agent") == 0) {
        return cmd_agent(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "tui") == 0) {
        return cmd_tui(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "daemon") == 0) {
        return cmd_daemon(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "status") == 0) {
        return cmd_status(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "doctor") == 0) {
        return cmd_doctor(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "channel") == 0) {
        return cmd_channel(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "cron") == 0) {
        return cmd_cron(config, args->sub_argc, args->sub_argv);
    }
    else if (strcmp(cmd, "version") == 0) {
        return cmd_version();
    }
    else if (strcmp(cmd, "help") == 0) {
        if (args->sub_argc > 0) {
            return cmd_help(args->sub_argv[0]);
        } else {
            print_help();
            return ERR_OK;
        }
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        fprintf(stderr, "Run 'cclaw help' for usage information.\n");
        return ERR_INVALID_ARGUMENT;
    }
}

// Note: cclaw_init, cclaw_shutdown, cclaw_get_version, cclaw_get_platform_name
// and related functions are defined in src/core/agent.c
