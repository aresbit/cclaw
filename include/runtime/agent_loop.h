// agent_loop.h - Agent runtime loop for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_RUNTIME_AGENT_LOOP_H
#define CCLAW_RUNTIME_AGENT_LOOP_H

#include "core/error.h"
#include "core/config.h"

// Initialize agent runtime with configuration
err_t agent_runtime_init(config_t* config);

// Shutdown agent runtime
void agent_runtime_shutdown(void);

// Run interactive agent loop (Pi-style conversation)
err_t agent_runtime_run_interactive(void);

// Run single message mode (non-interactive)
err_t agent_runtime_run_single(const char* message, char** out_response);

#endif // CCLAW_RUNTIME_AGENT_LOOP_H
