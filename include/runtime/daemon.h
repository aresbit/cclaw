// daemon.h - Daemon mode for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_RUNTIME_DAEMON_H
#define CCLAW_RUNTIME_DAEMON_H

#include "core/types.h"
#include "core/error.h"
#include "core/config.h"
#include "core/agent.h"

#include <stdint.h>
#include <stdbool.h>
#include <signal.h>

// Forward declarations
typedef struct daemon_t daemon_t;
typedef struct daemon_config_t daemon_config_t;
typedef struct cron_job_t cron_job_t;
typedef struct health_status_t health_status_t;

// Daemon configuration
struct daemon_config_t {
    str_t pid_file;           // PID file path
    str_t log_file;           // Log file path
    str_t working_dir;        // Working directory
    bool redirect_stdio;      // Redirect stdin/stdout/stderr
    bool double_fork;         // Use double fork technique
    uint32_t umask;           // File mode creation mask
};

// Cron job structure
typedef struct cron_job_t {
    str_t id;                 // Unique job ID
    str_t name;               // Job name
    str_t expression;         // Cron expression (e.g., "0 9 * * *")
    str_t command;            // Command to execute
    str_t description;        // Job description

    // Schedule parsing
    uint8_t minute;           // 0-59 or 255 for *
    uint8_t hour;             // 0-23 or 255 for *
    uint8_t day_of_month;     // 1-31 or 255 for *
    uint8_t month;            // 1-12 or 255 for *
    uint8_t day_of_week;      // 0-6 (0=Sunday) or 255 for *

    // State
    bool enabled;
    uint64_t last_run;
    uint64_t next_run;
    uint32_t run_count;
    uint32_t fail_count;

    // Callback for agent-based jobs
    void (*callback)(const char* args, void* user_data);
    void* user_data;
} cron_job_t;

// Health status
struct health_status_t {
    bool healthy;             // Overall health
    uint64_t uptime_ms;       // Daemon uptime
    uint32_t restart_count;   // Number of restarts
    uint64_t last_restart;    // Last restart timestamp

    // Component health
    bool provider_healthy;
    bool memory_healthy;
    bool channel_healthy;

    // Metrics
    uint32_t messages_processed;
    uint32_t api_calls_made;
    uint32_t errors_count;
    double avg_response_time_ms;
};

// Daemon structure
struct daemon_t {
    daemon_config_t config;
    health_status_t health;

    // Process state
    pid_t pid;
    bool running;
    uint64_t start_time;

    // Cron jobs
    cron_job_t** jobs;
    uint32_t job_count;
    uint32_t job_capacity;

    // Signal handling
    volatile sig_atomic_t received_sigterm;
    volatile sig_atomic_t received_sighup;
    volatile sig_atomic_t received_sigusr1;

    // Health check server
    int health_fd;            // Unix socket for health checks
    str_t health_socket_path;

    // Reference to agent
    agent_t* agent;
};

// ============================================================================
// Daemon Lifecycle
// ============================================================================

err_t daemon_create(const daemon_config_t* config, daemon_t** out_daemon);
void daemon_destroy(daemon_t* daemon);

err_t daemon_start(daemon_t* daemon);
err_t daemon_stop(daemon_t* daemon);
err_t daemon_reload(daemon_t* daemon);

// Run daemon main loop
err_t daemon_run(daemon_t* daemon);

// Single iteration of the main loop (for testing)
err_t daemon_run_once(daemon_t* daemon);

// ============================================================================
// Daemonization
// ============================================================================

err_t daemonize(const daemon_config_t* config);
bool daemon_is_running(const char* pid_file);
err_t daemon_kill(const char* pid_file);
err_t daemon_get_pid(const char* pid_file, pid_t* out_pid);

// ============================================================================
// PID File Management
// ============================================================================

err_t pidfile_create(const char* path, pid_t pid);
err_t pidfile_remove(const char* path);
err_t pidfile_read(const char* path, pid_t* out_pid);
bool pidfile_exists(const char* path);

// ============================================================================
// Cron Scheduler
// ============================================================================

// Cron expression parsing
err_t cron_parse_expression(const str_t* expression, cron_job_t* out_job);
err_t cron_compute_next_run(cron_job_t* job, uint64_t after_timestamp);
bool cron_should_run(const cron_job_t* job, uint64_t current_timestamp);

// Job management
err_t daemon_cron_add(daemon_t* daemon, const cron_job_t* job);
err_t daemon_cron_remove(daemon_t* daemon, const str_t* job_id);
err_t daemon_cron_list(daemon_t* daemon, cron_job_t*** out_jobs, uint32_t* out_count);
err_t daemon_cron_enable(daemon_t* daemon, const str_t* job_id, bool enable);

// Run due jobs
err_t daemon_cron_run_pending(daemon_t* daemon);

// ============================================================================
// Health Checking
// ============================================================================

err_t daemon_health_init(daemon_t* daemon);
void daemon_health_shutdown(daemon_t* daemon);
err_t daemon_health_update(daemon_t* daemon);
err_t daemon_health_get(daemon_t* daemon, health_status_t* out_status);

// Health check server
err_t daemon_health_server_start(daemon_t* daemon);
void daemon_health_server_stop(daemon_t* daemon);

// ============================================================================
// Signal Handling
// ============================================================================

void daemon_setup_signals(daemon_t* daemon);
void daemon_handle_signals(daemon_t* daemon);

// ============================================================================
// Configuration
// ============================================================================

daemon_config_t daemon_config_default(void);
void daemon_config_free(daemon_config_t* config);

// ============================================================================
// Utility Functions
// ============================================================================

const char* daemon_status_string(daemon_t* daemon);
str_t daemon_generate_job_id(void);
uint64_t daemon_get_uptime_ms(daemon_t* daemon);

// ============================================================================
// Constants
// ============================================================================

#define DAEMON_PID_FILE_DEFAULT "/var/run/cclaw.pid"
#define DAEMON_PID_FILE_USER "~/.cclaw/daemon.pid"
#define DAEMON_LOG_FILE_DEFAULT "/var/log/cclaw.log"
#define DAEMON_LOG_FILE_USER "~/.cclaw/daemon.log"
#define DAEMON_HEALTH_SOCKET "/tmp/cclaw-health.sock"

#define DAEMON_CONFIG_CRON_FILE ".cclaw/crontab"

#endif // CCLAW_RUNTIME_DAEMON_H
