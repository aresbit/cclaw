// daemon.c - Daemon mode implementation for CClaw
// SPDX-License-Identifier: MIT

#include "runtime/daemon.h"
#include "runtime/agent_loop.h"
#include "core/alloc.h"
#include "cclaw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

// Global daemon instance for signal handling
static daemon_t* g_daemon = NULL;

// ============================================================================
// Signal Handlers
// ============================================================================

static void signal_handler(int sig) {
    if (!g_daemon) return;

    switch (sig) {
        case SIGTERM:
        case SIGINT:
            g_daemon->received_sigterm = 1;
            break;
        case SIGHUP:
            g_daemon->received_sighup = 1;
            break;
        case SIGUSR1:
            g_daemon->received_sigusr1 = 1;
            break;
    }
}

void daemon_setup_signals(daemon_t* daemon) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
}

void daemon_handle_signals(daemon_t* daemon) {
    if (daemon->received_sigterm) {
        daemon->received_sigterm = 0;
        daemon->running = false;
    }

    if (daemon->received_sighup) {
        daemon->received_sighup = 0;
        // Reload configuration
        daemon_reload(daemon);
    }

    if (daemon->received_sigusr1) {
        daemon->received_sigusr1 = 0;
        // Trigger health check
        daemon_health_update(daemon);
    }
}

// ============================================================================
// PID File Management
// ============================================================================

err_t pidfile_create(const char* path, pid_t pid) {
    if (!path) return ERR_INVALID_ARGUMENT;

    // Expand ~ to home directory
    char* expanded_path = NULL;
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) return ERR_INVALID_ARGUMENT;
        expanded_path = malloc(strlen(home) + strlen(path));
        sprintf(expanded_path, "%s%s", home, path + 1);
        path = expanded_path;
    }

    // Create parent directory if needed
    char* dir = strdup(path);
    char* last_slash = strrchr(dir, '/');
    if (last_slash && last_slash != dir) {
        *last_slash = '\0';
        mkdir(dir, 0755);
    }
    free(dir);

    // Write PID file
    FILE* f = fopen(path, "w");
    if (!f) {
        free(expanded_path);
        return ERR_IO;
    }

    fprintf(f, "%d\n", (int)pid);
    fclose(f);

    free(expanded_path);
    return ERR_OK;
}

err_t pidfile_remove(const char* path) {
    if (!path) return ERR_INVALID_ARGUMENT;

    char* expanded_path = NULL;
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) return ERR_INVALID_ARGUMENT;
        expanded_path = malloc(strlen(home) + strlen(path));
        sprintf(expanded_path, "%s%s", home, path + 1);
        path = expanded_path;
    }

    int result = unlink(path);
    free(expanded_path);

    return (result == 0) ? ERR_OK : ERR_IO;
}

err_t pidfile_read(const char* path, pid_t* out_pid) {
    if (!path || !out_pid) return ERR_INVALID_ARGUMENT;

    char* expanded_path = NULL;
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) return ERR_INVALID_ARGUMENT;
        expanded_path = malloc(strlen(home) + strlen(path));
        sprintf(expanded_path, "%s%s", home, path + 1);
        path = expanded_path;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        free(expanded_path);
        return ERR_NOT_FOUND;
    }

    int pid;
    if (fscanf(f, "%d", &pid) != 1) {
        fclose(f);
        free(expanded_path);
        return ERR_IO;
    }

    fclose(f);
    free(expanded_path);

    *out_pid = (pid_t)pid;
    return ERR_OK;
}

bool pidfile_exists(const char* path) {
    pid_t pid;
    return pidfile_read(path, &pid) == ERR_OK;
}

// ============================================================================
// Daemonization
// ============================================================================

err_t daemonize(const daemon_config_t* config) {
    // First fork
    pid_t pid = fork();
    if (pid < 0) {
        return ERR_FAILED;
    }
    if (pid > 0) {
        // Parent exits
        exit(0);
    }

    // Create new session
    if (setsid() < 0) {
        return ERR_FAILED;
    }

    // Second fork (optional but recommended)
    if (config->double_fork) {
        pid = fork();
        if (pid < 0) {
            return ERR_FAILED;
        }
        if (pid > 0) {
            exit(0);
        }
    }

    // Change working directory
    if (!str_empty(config->working_dir)) {
        char* wd = strndup(config->working_dir.data, config->working_dir.len);
        chdir(wd);
        free(wd);
    }

    // Set umask
    umask(config->umask);

    // Redirect stdio
    if (config->redirect_stdio) {
        int dev_null = open("/dev/null", O_RDWR);
        if (dev_null >= 0) {
            dup2(dev_null, STDIN_FILENO);
            if (!str_empty(config->log_file)) {
                char* log_path = strndup(config->log_file.data, config->log_file.len);
                int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                free(log_path);
                if (log_fd >= 0) {
                    dup2(log_fd, STDOUT_FILENO);
                    dup2(log_fd, STDERR_FILENO);
                    close(log_fd);
                } else {
                    dup2(dev_null, STDOUT_FILENO);
                    dup2(dev_null, STDERR_FILENO);
                }
            } else {
                dup2(dev_null, STDOUT_FILENO);
                dup2(dev_null, STDERR_FILENO);
            }
            close(dev_null);
        }
    }

    return ERR_OK;
}

bool daemon_is_running(const char* pid_file) {
    pid_t pid;
    if (pidfile_read(pid_file, &pid) != ERR_OK) {
        return false;
    }

    // Check if process exists
    return (kill(pid, 0) == 0);
}

err_t daemon_kill(const char* pid_file) {
    pid_t pid;
    err_t err = pidfile_read(pid_file, &pid);
    if (err != ERR_OK) {
        return err;
    }

    if (kill(pid, SIGTERM) != 0) {
        return ERR_FAILED;
    }

    // Wait for process to exit
    int retries = 10;
    while (retries-- > 0) {
        if (kill(pid, 0) != 0) {
            // Process exited
            pidfile_remove(pid_file);
            return ERR_OK;
        }
        usleep(100000); // 100ms
    }

    // Force kill
    kill(pid, SIGKILL);
    pidfile_remove(pid_file);
    return ERR_OK;
}

// ============================================================================
// Cron Expression Parsing
// ============================================================================

static bool parse_cron_field(const char* field, uint8_t min, uint8_t max, uint8_t* out_value) {
    if (strcmp(field, "*") == 0) {
        *out_value = 255; // Wildcard
        return true;
    }

    char* endptr;
    long val = strtol(field, &endptr, 10);
    if (*endptr != '\0' || val < min || val > max) {
        return false;
    }

    *out_value = (uint8_t)val;
    return true;
}

err_t cron_parse_expression(const str_t* expression, cron_job_t* out_job) {
    if (!expression || !out_job) return ERR_INVALID_ARGUMENT;

    char* expr = strndup(expression->data, expression->len);
    char* parts[5] = {0};
    int part_count = 0;

    // Split by space
    char* token = strtok(expr, " ");
    while (token && part_count < 5) {
        parts[part_count++] = token;
        token = strtok(NULL, " ");
    }

    if (part_count != 5) {
        free(expr);
        return ERR_INVALID_ARGUMENT;
    }

    // Parse: minute hour day_of_month month day_of_week
    if (!parse_cron_field(parts[0], 0, 59, &out_job->minute) ||
        !parse_cron_field(parts[1], 0, 23, &out_job->hour) ||
        !parse_cron_field(parts[2], 1, 31, &out_job->day_of_month) ||
        !parse_cron_field(parts[3], 1, 12, &out_job->month) ||
        !parse_cron_field(parts[4], 0, 6, &out_job->day_of_week)) {
        free(expr);
        return ERR_INVALID_ARGUMENT;
    }

    free(expr);
    return ERR_OK;
}

bool cron_should_run(const cron_job_t* job, uint64_t current_timestamp) {
    time_t t = (time_t)(current_timestamp / 1000);
    struct tm* tm_info = localtime(&t);

    if (job->minute != 255 && job->minute != tm_info->tm_min) return false;
    if (job->hour != 255 && job->hour != tm_info->tm_hour) return false;
    if (job->day_of_month != 255 && job->day_of_month != tm_info->tm_mday) return false;
    if (job->month != 255 && job->month != (tm_info->tm_mon + 1)) return false;
    if (job->day_of_week != 255 && job->day_of_week != tm_info->tm_wday) return false;

    return true;
}

// ============================================================================
// Daemon Lifecycle
// ============================================================================

daemon_config_t daemon_config_default(void) {
    return (daemon_config_t){
        .pid_file = STR_LIT(DAEMON_PID_FILE_USER),
        .log_file = STR_LIT(DAEMON_LOG_FILE_USER),
        .working_dir = STR_LIT("~"),
        .redirect_stdio = true,
        .double_fork = true,
        .umask = 022
    };
}

void daemon_config_free(daemon_config_t* config) {
    if (!config) return;
    free((void*)config->pid_file.data);
    free((void*)config->log_file.data);
    free((void*)config->working_dir.data);
}

err_t daemon_create(const daemon_config_t* config, daemon_t** out_daemon) {
    if (!out_daemon) return ERR_INVALID_ARGUMENT;

    daemon_t* daemon = calloc(1, sizeof(daemon_t));
    if (!daemon) return ERR_OUT_OF_MEMORY;

    daemon->config = config ? *config : daemon_config_default();
    daemon->running = false;
    daemon->start_time = 0;
    daemon->pid = getpid();

    // Initialize health status
    daemon->health.healthy = true;
    daemon->health.uptime_ms = 0;
    daemon->health.restart_count = 0;
    daemon->health.last_restart = 0;
    daemon->health.provider_healthy = false;
    daemon->health.memory_healthy = true;
    daemon->health.channel_healthy = true;

    g_daemon = daemon;
    *out_daemon = daemon;
    return ERR_OK;
}

void daemon_destroy(daemon_t* daemon) {
    if (!daemon) return;

    // Stop if running
    if (daemon->running) {
        daemon_stop(daemon);
    }

    // Free jobs
    for (uint32_t i = 0; i < daemon->job_count; i++) {
        cron_job_t* job = daemon->jobs[i];
        free((void*)job->id.data);
        free((void*)job->name.data);
        free((void*)job->expression.data);
        free((void*)job->command.data);
        free((void*)job->description.data);
        free(job);
    }
    free(daemon->jobs);

    daemon_config_free(&daemon->config);
    free((void*)daemon->health_socket_path.data);
    free(daemon);

    if (g_daemon == daemon) {
        g_daemon = NULL;
    }
}

err_t daemon_start(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;
    if (daemon->running) return ERR_ALREADY_EXISTS;

    // Check if already running
    char* pid_path = strndup(daemon->config.pid_file.data, daemon->config.pid_file.len);
    if (daemon_is_running(pid_path)) {
        free(pid_path);
        return ERR_ALREADY_EXISTS;
    }
    free(pid_path);

    // Daemonize
    err_t err = daemonize(&daemon->config);
    if (err != ERR_OK) {
        return err;
    }

    // Update PID
    daemon->pid = getpid();
    daemon->start_time = (uint64_t)time(NULL) * 1000;
    daemon->running = true;

    // Create PID file
    pid_path = strndup(daemon->config.pid_file.data, daemon->config.pid_file.len);
    pidfile_create(pid_path, daemon->pid);
    free(pid_path);

    // Setup signals
    daemon_setup_signals(daemon);

    // Start health server
    daemon_health_init(daemon);
    daemon_health_server_start(daemon);

    return ERR_OK;
}

err_t daemon_stop(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    daemon->running = false;

    // Stop health server
    daemon_health_server_stop(daemon);
    daemon_health_shutdown(daemon);

    // Remove PID file
    char* pid_path = strndup(daemon->config.pid_file.data, daemon->config.pid_file.len);
    pidfile_remove(pid_path);
    free(pid_path);

    return ERR_OK;
}

err_t daemon_reload(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    // Reload cron jobs
    // TODO: Reload from crontab file

    return ERR_OK;
}

// ============================================================================
// Main Loop
// ============================================================================

err_t daemon_run(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    while (daemon->running) {
        daemon_run_once(daemon);
        usleep(100000); // 100ms sleep
    }

    return ERR_OK;
}

err_t daemon_run_once(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    // Handle signals
    daemon_handle_signals(daemon);

    // Update health status
    daemon_health_update(daemon);

    // Run pending cron jobs
    daemon_cron_run_pending(daemon);

    // Update uptime
    if (daemon->start_time > 0) {
        daemon->health.uptime_ms = ((uint64_t)time(NULL) * 1000) - daemon->start_time;
    }

    return ERR_OK;
}

// ============================================================================
// Cron Job Management
// ============================================================================

str_t daemon_generate_job_id(void) {
    static uint32_t counter = 0;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "job-%u-%lu", ++counter, (unsigned long)time(NULL));
    return str_dup_cstr(buffer, NULL);
}

err_t daemon_cron_add(daemon_t* daemon, const cron_job_t* job) {
    if (!daemon || !job) return ERR_INVALID_ARGUMENT;

    // Check capacity
    if (daemon->job_count >= daemon->job_capacity) {
        uint32_t new_cap = daemon->job_capacity == 0 ? 8 : daemon->job_capacity * 2;
        cron_job_t** new_jobs = realloc(daemon->jobs, sizeof(cron_job_t*) * new_cap);
        if (!new_jobs) return ERR_OUT_OF_MEMORY;
        daemon->jobs = new_jobs;
        daemon->job_capacity = new_cap;
    }

    // Copy job
    cron_job_t* new_job = malloc(sizeof(cron_job_t));
    *new_job = *job;

    if (str_empty(new_job->id)) {
        new_job->id = daemon_generate_job_id();
    } else {
        new_job->id = str_dup(new_job->id, NULL);
    }

    new_job->name = str_dup(new_job->name, NULL);
    new_job->expression = str_dup(new_job->expression, NULL);
    new_job->command = str_dup(new_job->command, NULL);
    new_job->description = str_dup(new_job->description, NULL);

    daemon->jobs[daemon->job_count++] = new_job;
    return ERR_OK;
}

err_t daemon_cron_remove(daemon_t* daemon, const str_t* job_id) {
    if (!daemon || !job_id) return ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < daemon->job_count; i++) {
        if (str_equal(daemon->jobs[i]->id, *job_id)) {
            // Free job
            cron_job_t* job = daemon->jobs[i];
            free((void*)job->id.data);
            free((void*)job->name.data);
            free((void*)job->expression.data);
            free((void*)job->command.data);
            free((void*)job->description.data);
            free(job);

            // Shift remaining
            for (uint32_t j = i; j < daemon->job_count - 1; j++) {
                daemon->jobs[j] = daemon->jobs[j + 1];
            }
            daemon->job_count--;
            return ERR_OK;
        }
    }

    return ERR_NOT_FOUND;
}

err_t daemon_cron_list(daemon_t* daemon, cron_job_t*** out_jobs, uint32_t* out_count) {
    if (!daemon || !out_jobs || !out_count) return ERR_INVALID_ARGUMENT;

    *out_jobs = daemon->jobs;
    *out_count = daemon->job_count;
    return ERR_OK;
}

err_t daemon_cron_run_pending(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    uint64_t now = (uint64_t)time(NULL) * 1000;

    for (uint32_t i = 0; i < daemon->job_count; i++) {
        cron_job_t* job = daemon->jobs[i];

        if (!job->enabled) continue;
        if (job->next_run > now) continue;
        if (!cron_should_run(job, now)) continue;

        // Execute job
        job->last_run = now;
        job->run_count++;

        if (job->callback) {
            char* args = strndup(job->command.data, job->command.len);
            job->callback(args, job->user_data);
            free(args);
        }

        // Schedule next run (simple: next minute)
        job->next_run = now + 60000;
    }

    return ERR_OK;
}

// ============================================================================
// Health Checking
// ============================================================================

err_t daemon_health_init(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    daemon->health_socket_path = str_dup_cstr(DAEMON_HEALTH_SOCKET, NULL);
    daemon->health_fd = -1;

    return ERR_OK;
}

void daemon_health_shutdown(daemon_t* daemon) {
    if (!daemon) return;

    if (daemon->health_fd >= 0) {
        close(daemon->health_fd);
        daemon->health_fd = -1;
    }

    // Remove socket file
    char* path = strndup(daemon->health_socket_path.data, daemon->health_socket_path.len);
    unlink(path);
    free(path);
}

err_t daemon_health_update(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    // Overall health is the AND of all components
    daemon->health.healthy = daemon->health.provider_healthy &&
                             daemon->health.memory_healthy &&
                             daemon->health.channel_healthy;

    return ERR_OK;
}

err_t daemon_health_get(daemon_t* daemon, health_status_t* out_status) {
    if (!daemon || !out_status) return ERR_INVALID_ARGUMENT;

    *out_status = daemon->health;
    return ERR_OK;
}

err_t daemon_health_server_start(daemon_t* daemon) {
    if (!daemon) return ERR_INVALID_ARGUMENT;

    // Create Unix socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return ERR_FAILED;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    char* path = strndup(daemon->health_socket_path.data, daemon->health_socket_path.len);
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path); // Remove old socket
    free(path);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return ERR_FAILED;
    }

    if (listen(fd, 5) < 0) {
        close(fd);
        return ERR_FAILED;
    }

    daemon->health_fd = fd;
    return ERR_OK;
}

void daemon_health_server_stop(daemon_t* daemon) {
    if (!daemon) return;

    if (daemon->health_fd >= 0) {
        close(daemon->health_fd);
        daemon->health_fd = -1;
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

const char* daemon_status_string(daemon_t* daemon) {
    if (!daemon) return "unknown";
    if (daemon->running) return daemon->health.healthy ? "running" : "degraded";
    return "stopped";
}

uint64_t daemon_get_uptime_ms(daemon_t* daemon) {
    if (!daemon || daemon->start_time == 0) return 0;
    return ((uint64_t)time(NULL) * 1000) - daemon->start_time;
}
