// commands.h - CLI commands for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CLI_COMMANDS_H
#define CCLAW_CLI_COMMANDS_H

#include "core/types.h"
#include "core/error.h"
#include "core/config.h"

// Command handlers
err_t cmd_onboard(config_t* config, int argc, char** argv);
err_t cmd_agent(config_t* config, int argc, char** argv);
err_t cmd_daemon(config_t* config, int argc, char** argv);
err_t cmd_status(config_t* config, int argc, char** argv);
err_t cmd_channel(config_t* config, int argc, char** argv);
err_t cmd_cron(config_t* config, int argc, char** argv);
err_t cmd_doctor(config_t* config, int argc, char** argv);
err_t cmd_tui(config_t* config, int argc, char** argv);

// Utility commands
err_t cmd_version(void);
err_t cmd_help(const char* topic);

#endif // CCLAW_CLI_COMMANDS_H
