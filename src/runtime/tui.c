// tui.c - Terminal UI implementation for CClaw
// SPDX-License-Identifier: MIT

#include "runtime/tui.h"
#include "core/alloc.h"
#include "core/agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>

// Global TUI instance for signal handling
static tui_t* g_tui = NULL;

// ============================================================================
// Terminal Control
// ============================================================================

void tui_get_terminal_size(uint16_t* out_width, uint16_t* out_height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *out_width = ws.ws_col;
        *out_height = ws.ws_row;
    } else {
        *out_width = TUI_DEFAULT_WIDTH;
        *out_height = TUI_DEFAULT_HEIGHT;
    }
}

bool tui_supports_color(void) {
    const char* term = getenv("TERM");
    if (!term) return false;
    return strstr(term, "color") != NULL ||
           strcmp(term, "xterm") == 0 ||
           strcmp(term, "screen") == 0 ||
           strcmp(term, "tmux") == 0;
}

bool tui_supports_unicode(void) {
    const char* lang = getenv("LANG");
    if (lang && strstr(lang, "UTF-8")) return true;
    return false;
}

// ============================================================================
// Theme
// ============================================================================

tui_theme_t tui_theme_default(void) {
    return (tui_theme_t){
        .color_bg = 0,
        .color_fg = 7,
        .color_primary = 4,     // Blue
        .color_secondary = 6,   // Cyan
        .color_success = 2,     // Green
        .color_warning = 3,     // Yellow
        .color_error = 1,       // Red
        .color_muted = 8,       // Gray
        .use_bold = true,
        .use_italic = false,
        .use_unicode = true
    };
}

tui_theme_t tui_theme_dark(void) {
    return tui_theme_default();
}

tui_theme_t tui_theme_light(void) {
    return (tui_theme_t){
        .color_bg = 15,
        .color_fg = 0,
        .color_primary = 4,
        .color_secondary = 6,
        .color_success = 2,
        .color_warning = 3,
        .color_error = 1,
        .color_muted = 8,
        .use_bold = true,
        .use_italic = false,
        .use_unicode = true
    };
}

void tui_theme_apply(tui_t* tui, const tui_theme_t* theme) {
    if (tui && theme) {
        tui->config.theme = *theme;
    }
}

// ============================================================================
// ANSI Drawing
// ============================================================================

void tui_move_cursor(uint16_t x, uint16_t y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}

void tui_set_color(uint8_t fg, uint8_t bg) {
    printf("\033[38;5;%dm\033[48;5;%dm", fg, bg);
}

void tui_reset_color(void) {
    printf("\033[0m");
}

void tui_clear_screen(tui_t* tui) {
    (void)tui;
    printf(TUI_CLEAR_SCREEN TUI_CURSOR_HOME);
    fflush(stdout);
}

void tui_draw_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* title) {
    // Draw corners and borders
    const char* ul = "┌";
    const char* ur = "┐";
    const char* ll = "└";
    const char* lr = "┘";
    const char* hline = "─";
    const char* vline = "│";

    // Top border
    tui_move_cursor(x, y);
    printf("%s", ul);
    for (uint16_t i = 0; i < w - 2; i++) printf("%s", hline);
    printf("%s", ur);

    // Title
    if (title && strlen(title) > 0) {
        tui_move_cursor(x + 2, y);
        printf(" %s ", title);
    }

    // Side borders
    for (uint16_t i = 1; i < h - 1; i++) {
        tui_move_cursor(x, y + i);
        printf("%s", vline);
        tui_move_cursor(x + w - 1, y + i);
        printf("%s", vline);
    }

    // Bottom border
    tui_move_cursor(x, y + h - 1);
    printf("%s", ll);
    for (uint16_t i = 0; i < w - 2; i++) printf("%s", hline);
    printf("%s", lr);
}

void tui_draw_line(uint16_t x, uint16_t y, uint16_t len, bool horizontal) {
    tui_move_cursor(x, y);
    const char* hline = "─";
    const char* vline = "│";

    if (horizontal) {
        for (uint16_t i = 0; i < len; i++) {
            printf("%s", hline);
        }
    } else {
        for (uint16_t i = 0; i < len; i++) {
            tui_move_cursor(x, y + i);
            printf("%s", vline);
        }
    }
}

void tui_draw_text(uint16_t x, uint16_t y, const char* text) {
    tui_move_cursor(x, y);
    printf("%s", text);
}

void tui_draw_text_truncated(uint16_t x, uint16_t y, uint16_t max_width, const char* text) {
    tui_move_cursor(x, y);
    size_t len = strlen(text);
    if (len > max_width) {
        printf("%.*s...", (int)max_width - 3, text);
    } else {
        printf("%s", text);
    }
}

// ============================================================================
// TUI Lifecycle
// ============================================================================

tui_config_t tui_config_default(void) {
    uint16_t w, h;
    tui_get_terminal_size(&w, &h);

    return (tui_config_t){
        .width = w,
        .height = h,
        .use_color = tui_supports_color(),
        .use_mouse = false,
        .show_token_count = true,
        .show_timestamps = false,
        .show_branch_indicator = true,
        .theme = tui_theme_default()
    };
}

err_t tui_create(const tui_config_t* config, tui_t** out_tui) {
    if (!out_tui) return ERR_INVALID_ARGUMENT;

    tui_t* tui = calloc(1, sizeof(tui_t));
    if (!tui) return ERR_OUT_OF_MEMORY;

    tui->config = config ? *config : tui_config_default();
    tui->running = false;
    tui->needs_redraw = true;

    // Allocate input buffer
    tui->input_capacity = TUI_MAX_INPUT_LENGTH;
    tui->input_buffer = malloc(tui->input_capacity);
    if (!tui->input_buffer) {
        free(tui);
        return ERR_OUT_OF_MEMORY;
    }
    tui->input_buffer[0] = '\0';

    // Allocate history
    tui->history_capacity = TUI_INPUT_HISTORY_SIZE;
    tui->history = calloc(tui->history_capacity, sizeof(char*));
    if (!tui->history) {
        free(tui->input_buffer);
        free(tui);
        return ERR_OUT_OF_MEMORY;
    }

    // Create panels
    for (int i = 0; i < 5; i++) {
        tui->panels[i] = calloc(1, sizeof(tui_panel_t));
        if (!tui->panels[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                free(tui->panels[j]);
            }
            free(tui->history);
            free(tui->input_buffer);
            free(tui);
            return ERR_OUT_OF_MEMORY;
        }
        tui->panels[i]->type = i;
        tui->panels[i]->visible = true;
    }

    // Initialize message list
    tui->messages = NULL;
    tui->messages_tail = NULL;
    tui->message_count = 0;
    tui->selected_session = 0;

    g_tui = tui;
    *out_tui = tui;
    return ERR_OK;
}

void tui_destroy(tui_t* tui) {
    if (!tui) return;

    tui_restore_terminal(tui);

    free(tui->input_buffer);

    for (uint32_t i = 0; i < tui->history_count; i++) {
        free(tui->history[i]);
    }
    free(tui->history);

    // Free messages
    tui_message_t* msg = tui->messages;
    while (msg) {
        tui_message_t* next = msg->next;
        free(msg->text);
        free(msg->sender);
        free(msg);
        msg = next;
    }

    for (int i = 0; i < 5; i++) {
        free(tui->panels[i]);
    }

    free(tui);

    if (g_tui == tui) {
        g_tui = NULL;
    }
}

err_t tui_init_terminal(tui_t* tui) {
    if (!tui) return ERR_INVALID_ARGUMENT;

    // Check if stdin is a TTY
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: TUI requires an interactive terminal (TTY)\n");
        return ERR_FAILED;
    }

    // Save original terminal settings
    if (tcgetattr(STDIN_FILENO, &tui->original_termios) != 0) {
        perror("tcgetattr failed");
        return ERR_FAILED;
    }

    // Set raw mode
    struct termios raw = tui->original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return ERR_FAILED;
    }

    tui->raw_mode = true;

    // Hide cursor
    printf(TUI_CURSOR_HIDE);
    fflush(stdout);

    return ERR_OK;
}

void tui_restore_terminal(tui_t* tui) {
    if (!tui || !tui->raw_mode) return;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tui->original_termios);
    tui->raw_mode = false;

    // Show cursor
    printf(TUI_CURSOR_SHOW TUI_COLOR_RESET "\n");
    fflush(stdout);
}

static void resize_handler(int sig) {
    (void)sig;
    if (g_tui) {
        tui_get_terminal_size(&g_tui->config.width, &g_tui->config.height);
        g_tui->needs_redraw = true;
    }
}

err_t tui_run(tui_t* tui, agent_t* agent) {
    if (!tui || !agent) return ERR_INVALID_ARGUMENT;

    tui->agent = agent;
    tui->running = true;

    // Initialize terminal
    err_t err = tui_init_terminal(tui);
    if (err != ERR_OK) {
        return err;
    }

    // Setup resize handler
    signal(SIGWINCH, resize_handler);

    // Initial draw
    tui_redraw(tui);

    // Main loop
    while (tui->running) {
        if (tui->needs_redraw) {
            tui_redraw(tui);
            tui->needs_redraw = false;
        }

        tui_process_input(tui);
    }

    return ERR_OK;
}

void tui_stop(tui_t* tui) {
    if (tui) {
        tui->running = false;
    }
}

// ============================================================================
// Rendering
// ============================================================================

void tui_refresh(tui_t* tui) {
    fflush(stdout);
}

void tui_redraw(tui_t* tui) {
    if (!tui) return;

    tui_clear_screen(tui);

    // Calculate panel sizes
    uint16_t sidebar_width = 25;
    uint16_t status_height = 1;
    uint16_t input_height = 3;
    uint16_t toolbar_height = 1;

    uint16_t chat_x = sidebar_width;
    uint16_t chat_y = toolbar_height;
    uint16_t chat_w = tui->config.width - sidebar_width;
    uint16_t chat_h = tui->config.height - toolbar_height - status_height - input_height;

    // Draw panels
    tui_draw_toolbar(tui);
    tui_draw_sidebar(tui);
    tui_draw_chat_panel(tui);
    tui_draw_status_bar(tui);
    tui_draw_input_area(tui);

    tui_refresh(tui);
}

void tui_draw_toolbar(tui_t* tui) {
    // Top toolbar with key hints
    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_primary);
    tui_move_cursor(0, 0);

    for (uint16_t i = 0; i < tui->config.width; i++) {
        printf(" ");
    }

    tui_move_cursor(1, 0);
    printf("CClaw Agent  |  Ctrl+H: Help  |  Ctrl+N: New  |  Ctrl+B: Branch  |  Ctrl+Q: Quit");

    tui_reset_color();
}

void tui_draw_sidebar(tui_t* tui) {
    uint16_t sidebar_w = 25;
    uint16_t sidebar_h = tui->config.height - 1;

    const char* title = (tui->active_panel == TUI_PANEL_SIDEBAR) ? "Sessions (*)" : "Sessions";
    tui_draw_box(0, 1, sidebar_w, sidebar_h, title);

    tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);

    // List sessions from agent
    uint32_t session_count = tui->agent ? tui->agent->ctx->session_count : 0;
    uint32_t max_display = (sidebar_h > 3) ? sidebar_h - 3 : 0;

    for (uint32_t i = 0; i < max_display; i++) {
        tui_move_cursor(2, 3 + i);
        
        bool is_selected = (i == tui->selected_session);
        bool is_active = false;
        
        if (i < session_count && tui->agent) {
            is_active = (tui->agent->ctx->active_session == tui->agent->ctx->sessions[i]);
        }
        
        // Highlight selected session
        if (is_selected && tui->active_panel == TUI_PANEL_SIDEBAR) {
            tui_set_color(tui->config.theme.color_bg, tui->config.theme.color_primary);
        } else if (is_active) {
            tui_set_color(tui->config.theme.color_primary, tui->config.theme.color_bg);
        } else {
            tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
        }
        
        if (i < session_count) {
            printf("%s %s", is_active ? ">" : " ", tui->agent->ctx->sessions[i]->name.data ? tui->agent->ctx->sessions[i]->name.data : "unnamed");
        } else if (i == 0 && session_count == 0) {
            printf("  (no sessions)");
        } else {
            break;
        }
    }

    tui_reset_color();
}

void tui_draw_chat_panel(tui_t* tui) {
    uint16_t x = 25;
    uint16_t y = 1;
    uint16_t w = tui->config.width - 25;
    uint16_t h = tui->config.height - 5;

    // Draw border
    tui_draw_box(x, y, w, h, NULL);

    // Chat content area - render messages
    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);

    // Calculate visible message area
    uint16_t max_lines = h - 2;
    uint16_t line_y = y + 1;

    // Show placeholder if no messages
    if (!tui->messages) {
        const char* placeholder[] = {
            "Welcome to CClaw Agent!",
            "Type a message to start chatting.",
            "Use /help for commands.",
            NULL
        };
        for (int i = 0; placeholder[i] && i < (int)max_lines; i++) {
            tui_move_cursor(x + 2, line_y + i);
            printf("%s", placeholder[i]);
        }
    } else {
        // Render messages from linked list
        uint16_t lines_used = 0;
        tui_message_t* msg = tui->messages;
        
        // Count total messages to show from the end
        uint32_t msg_count = 0;
        while (msg) {
            msg_count++;
            msg = msg->next;
        }

        // Show last N messages that fit
        uint32_t skip = (msg_count > max_lines) ? msg_count - max_lines : 0;
        msg = tui->messages;
        for (uint32_t i = 0; i < skip && msg; i++) {
            msg = msg->next;
        }

        // Render visible messages
        while (msg && lines_used < max_lines) {
            tui_move_cursor(x + 2, line_y + lines_used);
            
            // Color by sender
            if (strcmp(msg->sender, "user") == 0) {
                tui_set_color(tui->config.theme.color_success, tui->config.theme.color_bg);
                printf("[You]: ");
            } else if (strcmp(msg->sender, "assistant") == 0) {
                tui_set_color(tui->config.theme.color_primary, tui->config.theme.color_bg);
                printf("[AI]: ");
            } else {
                tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
                printf("[%s]: ", msg->sender);
            }
            
            tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
            
            // Print message text (truncate if too long)
            uint16_t max_text_width = w - 10;
            if (strlen(msg->text) > max_text_width) {
                printf("%.*s...", max_text_width, msg->text);
            } else {
                printf("%s", msg->text);
            }
            
            lines_used++;
            msg = msg->next;
        }
    }

    tui_reset_color();
}

void tui_draw_status_bar(tui_t* tui) {
    uint16_t y = tui->config.height - 4;

    tui_set_color(15, tui->config.theme.color_primary);
    tui_move_cursor(0, y);

    for (uint16_t i = 0; i < tui->config.width; i++) {
        printf(" ");
    }

    char status[256];
    const char* model_name = "unknown";
    if (tui->agent && tui->agent->ctx && tui->agent->ctx->provider) {
        model_name = tui->agent->ctx->provider->config.default_model.data;
        if (!model_name) model_name = "unknown";
    }
    snprintf(status, sizeof(status), " Model: %s  |  Tokens: %u  |  Branch: main ",
             model_name, 0);

    tui_move_cursor(1, y);
    printf("%s", status);

    tui_reset_color();
}

void tui_draw_input_area(tui_t* tui) {
    uint16_t y = tui->config.height - 3;

    // Clear input area
    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);

    for (uint16_t i = 0; i < 3; i++) {
        tui_move_cursor(0, y + i);
        for (uint16_t j = 0; j < tui->config.width; j++) {
            printf(" ");
        }
    }

    // Draw prompt
    tui_set_color(tui->config.theme.color_success, tui->config.theme.color_bg);
    tui_move_cursor(0, y + 1);
    printf(" > ");

    // Draw input text
    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);
    printf("%s", tui->input_buffer);

    // Position cursor
    tui_move_cursor(3 + tui->input_pos, y + 1);

    tui_reset_color();
}

// ============================================================================
// Input Handling
// ============================================================================

err_t tui_process_input(tui_t* tui) {
    if (!tui) return ERR_INVALID_ARGUMENT;

    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);

    if (n <= 0) {
        return ERR_OK; // No input
    }

    // Handle escape sequences
    if (c == '\033') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return ERR_OK;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return ERR_OK;

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': // Up arrow
                    if (tui->active_panel == TUI_PANEL_SIDEBAR) {
                        // Navigate up in session list
                        if (tui->selected_session > 0) {
                            tui->selected_session--;
                        }
                    } else {
                        // Normal history navigation
                        const char* hist = tui_history_prev(tui);
                        if (hist) {
                            size_t hist_len = strlen(hist);
                            if (hist_len >= tui->input_capacity) {
                                hist_len = tui->input_capacity - 1;
                            }
                            memcpy(tui->input_buffer, hist, hist_len);
                            tui->input_buffer[hist_len] = '\0';
                            tui->input_len = (uint32_t)hist_len;
                            tui->input_pos = tui->input_len;
                        }
                    }
                    break;
                case 'B': // Down arrow
                    if (tui->active_panel == TUI_PANEL_SIDEBAR) {
                        // Navigate down in session list
                        if (tui->agent && tui->selected_session + 1 < tui->agent->ctx->session_count) {
                            tui->selected_session++;
                        }
                    } else {
                        // Normal history navigation
                        const char* hist = tui_history_next(tui);
                        if (hist) {
                            size_t hist_len = strlen(hist);
                            if (hist_len >= tui->input_capacity) {
                                hist_len = tui->input_capacity - 1;
                            }
                            memcpy(tui->input_buffer, hist, hist_len);
                            tui->input_buffer[hist_len] = '\0';
                            tui->input_len = (uint32_t)hist_len;
                            tui->input_pos = tui->input_len;
                        } else {
                            tui_input_clear(tui);
                        }
                    }
                    break;
                case 'C': tui_input_move_right(tui); break; // Right
                case 'D': tui_input_move_left(tui); break;  // Left
                case '3': // Delete
                    read(STDIN_FILENO, &c, 1); // consume ~
                    tui_input_delete(tui);
                    break;
            }
        }
        tui->needs_redraw = true;
        return ERR_OK;
    }

    // Handle control characters
    if (c == TUI_KEY_CTRL('c') || c == TUI_KEY_CTRL('q')) {
        tui->running = false;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('h')) {
        tui_chat_add_system_message(tui, "Help: /new=branch /quit=exit /clear=clear");
        tui->needs_redraw = true;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('n')) {
        // Create new session
        if (tui->agent) {
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "session-%u", tui->agent->ctx->session_count + 1);
            str_t session_name = str_dup_cstr(name_buf, NULL);
            agent_session_t* new_session = NULL;
            err_t err = agent_session_create(tui->agent, &session_name, &new_session);
            if (err == ERR_OK && new_session) {
                // Copy model from active session or use default
                if (tui->agent->ctx->active_session && !str_empty(tui->agent->ctx->active_session->model)) {
                    new_session->model = str_dup(tui->agent->ctx->active_session->model, NULL);
                }
                tui->agent->ctx->active_session = new_session;
                tui_chat_add_system_message(tui, "Created new session");
            } else {
                tui_chat_add_system_message(tui, "Error: Failed to create session");
            }
            free((void*)session_name.data);
            tui->needs_redraw = true;
        }
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('b')) {
        // Create new branch (similar to new session but with branch semantics)
        if (tui->agent && tui->agent->ctx->active_session) {
            char name_buf[64];
            snprintf(name_buf, sizeof(name_buf), "branch-%u", tui->agent->ctx->session_count + 1);
            str_t branch_name = str_dup_cstr(name_buf, NULL);
            // For now, just create a new session as branch
            agent_session_t* new_branch = NULL;
            err_t err = agent_session_create(tui->agent, &branch_name, &new_branch);
            if (err == ERR_OK && new_branch) {
                if (!str_empty(tui->agent->ctx->active_session->model)) {
                    new_branch->model = str_dup(tui->agent->ctx->active_session->model, NULL);
                }
                tui->agent->ctx->active_session = new_branch;
                tui_chat_add_system_message(tui, "Created new branch");
            } else {
                tui_chat_add_system_message(tui, "Error: Failed to create branch");
            }
            free((void*)branch_name.data);
            tui->needs_redraw = true;
        } else {
            tui_chat_add_system_message(tui, "Error: No active session to branch from");
        }
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('l')) {
        tui_redraw(tui);
        return ERR_OK;
    }

    // Handle Tab key to switch panels
    if (c == TUI_KEY_TAB) {
        tui->active_panel = (tui->active_panel == TUI_PANEL_CHAT) ? TUI_PANEL_SIDEBAR : TUI_PANEL_CHAT;
        tui->needs_redraw = true;
        return ERR_OK;
    }

    // Handle regular input
    switch (c) {
        case '\r':
        case '\n':
            // If in sidebar, activate selected session
            if (tui->active_panel == TUI_PANEL_SIDEBAR) {
                if (tui->agent && tui->selected_session < tui->agent->ctx->session_count) {
                    tui->agent->ctx->active_session = tui->agent->ctx->sessions[tui->selected_session];
                    tui_chat_add_system_message(tui, "Switched session");
                    tui->needs_redraw = true;
                }
                return ERR_OK;
            }
            
            // Submit input
            if (tui->input_len > 0) {
                tui_history_add(tui, tui->input_buffer);
                tui_chat_add_user_message(tui, tui->input_buffer);
                
                // Process message with agent if available
                if (tui->agent && tui->agent->ctx && tui->agent->ctx->provider) {
                    str_t user_input = str_dup_cstr(tui->input_buffer, NULL);
                    str_t response = STR_NULL;
                    
                    // Find active session or create one
                    agent_session_t* session = NULL;
                    if (tui->agent->ctx->active_session) {
                        session = tui->agent->ctx->active_session;
                    } else if (tui->agent->ctx->session_count > 0) {
                        session = tui->agent->ctx->sessions[0];
                    }
                    
                    if (session) {
                        err_t err = agent_process_message(tui->agent, session, &user_input, &response);
                        if (err == ERR_OK && response.data) {
                            tui_chat_add_assistant_message(tui, response.data);
                            free((void*)response.data);
                        } else {
                            tui_chat_add_system_message(tui, "Error: Failed to get response");
                        }
                    } else {
                        tui_chat_add_system_message(tui, "Error: No active session");
                    }
                    
                    free((void*)user_input.data);
                } else {
                    tui_chat_add_system_message(tui, "Warning: No provider configured");
                }
                
                tui_input_clear(tui);
            }
            break;
        case TUI_KEY_BACKSPACE:
            tui_input_backspace(tui);
            break;
        case TUI_KEY_CTRL('a'):
        case TUI_KEY_ESC:
            tui_input_move_home(tui);
            break;
        case TUI_KEY_CTRL('e'):
            tui_input_move_end(tui);
            break;
        case TUI_KEY_CTRL('u'):
            tui_input_clear(tui);
            break;
        default:
            // Handle UTF-8 input
            {
                unsigned char uc = (unsigned char)c;
                // Check if this is a valid UTF-8 start byte or ASCII
                if ((uc & 0x80) == 0) {
                    // ASCII character (0xxxxxxx)
                    if (isprint(c) || c == ' ') {
                        tui_input_insert(tui, c);
                    }
                } else if ((uc & 0xE0) == 0xC0) {
                    // 2-byte UTF-8 sequence (110xxxxx)
                    char utf8_seq[3] = {c, 0, 0};
                    if (read(STDIN_FILENO, &utf8_seq[1], 1) == 1) {
                        tui_input_insert(tui, utf8_seq[0]);
                        tui_input_insert(tui, utf8_seq[1]);
                    }
                } else if ((uc & 0xF0) == 0xE0) {
                    // 3-byte UTF-8 sequence (1110xxxx) - Chinese characters
                    char utf8_seq[4] = {c, 0, 0, 0};
                    if (read(STDIN_FILENO, &utf8_seq[1], 1) == 1 &&
                        read(STDIN_FILENO, &utf8_seq[2], 1) == 1) {
                        tui_input_insert(tui, utf8_seq[0]);
                        tui_input_insert(tui, utf8_seq[1]);
                        tui_input_insert(tui, utf8_seq[2]);
                    }
                } else if ((uc & 0xF8) == 0xF0) {
                    // 4-byte UTF-8 sequence (11110xxx)
                    char utf8_seq[5] = {c, 0, 0, 0, 0};
                    if (read(STDIN_FILENO, &utf8_seq[1], 1) == 1 &&
                        read(STDIN_FILENO, &utf8_seq[2], 1) == 1 &&
                        read(STDIN_FILENO, &utf8_seq[3], 1) == 1) {
                        tui_input_insert(tui, utf8_seq[0]);
                        tui_input_insert(tui, utf8_seq[1]);
                        tui_input_insert(tui, utf8_seq[2]);
                        tui_input_insert(tui, utf8_seq[3]);
                    }
                }
                // Invalid UTF-8 bytes are ignored
            }
            break;
    }

    tui->needs_redraw = true;
    return ERR_OK;
}

// ============================================================================
// UTF-8 Helper Functions
// ============================================================================

// Check if a byte is a UTF-8 continuation byte (10xxxxxx)
static bool is_utf8_continuation(char c) {
    return (c & 0xC0) == 0x80;
}

// Get the length of a UTF-8 character starting at position pos
static uint32_t utf8_char_len(const char* str, uint32_t pos) {
    unsigned char c = (unsigned char)str[pos];
    if ((c & 0x80) == 0) return 1;        // 0xxxxxxx - ASCII (1 byte)
    if ((c & 0xE0) == 0xC0) return 2;    // 110xxxxx - 2 bytes
    if ((c & 0xF0) == 0xE0) return 3;    // 1110xxxx - 3 bytes (Chinese)
    if ((c & 0xF8) == 0xF0) return 4;    // 11110xxx - 4 bytes
    return 1; // Invalid UTF-8, treat as single byte
}

// Find the start position of the previous UTF-8 character
static uint32_t utf8_prev_char(const char* str, uint32_t pos) {
    if (pos == 0) return 0;
    // Move back until we find a non-continuation byte
    do {
        pos--;
    } while (pos > 0 && is_utf8_continuation(str[pos]));
    return pos;
}

// Count UTF-8 characters (not bytes) from start to end
static uint32_t utf8_char_count(const char* str, uint32_t len) {
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < len) {
        i += utf8_char_len(str, i);
        count++;
    }
    return count;
}

// ============================================================================
// Input Buffer Operations
// ============================================================================

void tui_input_clear(tui_t* tui) {
    if (!tui) return;
    tui->input_buffer[0] = '\0';
    tui->input_len = 0;
    tui->input_pos = 0;
}

void tui_input_insert(tui_t* tui, char c) {
    if (!tui || tui->input_len >= tui->input_capacity - 1) return;

    // Make room for new character
    for (uint32_t i = tui->input_len; i > tui->input_pos; i--) {
        tui->input_buffer[i] = tui->input_buffer[i - 1];
    }

    tui->input_buffer[tui->input_pos] = c;
    tui->input_len++;
    tui->input_pos++;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_backspace(tui_t* tui) {
    if (!tui || tui->input_pos == 0) return;

    // Find the start of the previous UTF-8 character
    uint32_t prev_pos = utf8_prev_char(tui->input_buffer, tui->input_pos);
    uint32_t char_len = tui->input_pos - prev_pos;

    // Shift remaining characters left by char_len bytes
    for (uint32_t i = prev_pos; i < tui->input_len - char_len; i++) {
        tui->input_buffer[i] = tui->input_buffer[i + char_len];
    }

    tui->input_len -= char_len;
    tui->input_pos = prev_pos;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_delete(tui_t* tui) {
    if (!tui || tui->input_pos >= tui->input_len) return;

    // Get the length of the UTF-8 character at current position
    uint32_t char_len = utf8_char_len(tui->input_buffer, tui->input_pos);

    // Shift remaining characters left
    for (uint32_t i = tui->input_pos; i < tui->input_len - char_len; i++) {
        tui->input_buffer[i] = tui->input_buffer[i + char_len];
    }

    tui->input_len -= char_len;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_move_left(tui_t* tui) {
    if (tui && tui->input_pos > 0) {
        // Move to the start of the previous UTF-8 character
        tui->input_pos = utf8_prev_char(tui->input_buffer, tui->input_pos);
    }
}

void tui_input_move_right(tui_t* tui) {
    if (tui && tui->input_pos < tui->input_len) {
        // Move to the next UTF-8 character
        tui->input_pos += utf8_char_len(tui->input_buffer, tui->input_pos);
    }
}

void tui_input_move_home(tui_t* tui) {
    if (tui) {
        tui->input_pos = 0;
    }
}

void tui_input_move_end(tui_t* tui) {
    if (tui) {
        tui->input_pos = tui->input_len;
    }
}

const char* tui_input_get(tui_t* tui) {
    return tui ? tui->input_buffer : NULL;
}

// ============================================================================
// History
// ============================================================================

void tui_history_add(tui_t* tui, const char* entry) {
    if (!tui || !entry || strlen(entry) == 0) return;

    // Don't add duplicates
    if (tui->history_count > 0 && strcmp(tui->history[0], entry) == 0) {
        return;
    }

    // Shift history
    if (tui->history_count >= tui->history_capacity) {
        free(tui->history[tui->history_capacity - 1]);
        tui->history_count--;
    }

    for (uint32_t i = tui->history_count; i > 0; i--) {
        tui->history[i] = tui->history[i - 1];
    }

    tui->history[0] = strdup(entry);
    tui->history_count++;
    tui->history_pos = (uint32_t)-1;
}

const char* tui_history_prev(tui_t* tui) {
    if (!tui || tui->history_count == 0) return NULL;

    if (tui->history_pos + 1 < tui->history_count) {
        tui->history_pos++;
        return tui->history[tui->history_pos];
    }

    return NULL;
}

const char* tui_history_next(tui_t* tui) {
    if (!tui || tui->history_count == 0) return NULL;

    if (tui->history_pos > 0) {
        tui->history_pos--;
        return tui->history[tui->history_pos];
    }

    tui->history_pos = (uint32_t)-1;
    return NULL;
}

// ============================================================================
// Chat Display
// ============================================================================

static void tui_chat_add_message_internal(tui_t* tui, const char* sender, const char* text) {
    if (!tui || !text) return;

    tui_message_t* msg = calloc(1, sizeof(tui_message_t));
    if (!msg) return;

    msg->sender = strdup(sender);
    msg->text = strdup(text);
    msg->timestamp = 0; // TODO: get actual timestamp
    msg->next = NULL;

    // Add to linked list
    if (tui->messages_tail) {
        tui->messages_tail->next = msg;
    } else {
        tui->messages = msg;
    }
    tui->messages_tail = msg;
    tui->message_count++;

    // Limit message count to prevent memory issues
    if (tui->message_count > 1000) {
        tui_message_t* old = tui->messages;
        tui->messages = old->next;
        free(old->text);
        free(old->sender);
        free(old);
        tui->message_count--;
    }
}

void tui_chat_add_system_message(tui_t* tui, const char* text) {
    tui_chat_add_message_internal(tui, "system", text);
}

void tui_chat_add_user_message(tui_t* tui, const char* text) {
    tui_chat_add_message_internal(tui, "user", text);
}

void tui_chat_add_assistant_message(tui_t* tui, const char* text) {
    tui_chat_add_message_internal(tui, "assistant", text);
}
