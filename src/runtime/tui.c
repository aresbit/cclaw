// tui.c - Terminal UI implementation for CClaw
// SPDX-License-Identifier: MIT

#include "runtime/tui.h"
#include "core/alloc.h"

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
        tui->panels[i]->type = i;
        tui->panels[i]->visible = true;
    }

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

    // Save original terminal settings
    if (tcgetattr(STDIN_FILENO, &tui->original_termios) != 0) {
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
    if (!tui) return ERR_INVALID_ARGUMENT;

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

    tui_draw_box(0, 1, sidebar_w, sidebar_h, "Sessions");

    tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);

    // List sessions (placeholder)
    for (int i = 0; i < 5 && i < (int)sidebar_h - 3; i++) {
        tui_move_cursor(2, 3 + i);
        if (i == 0) {
            tui_set_color(tui->config.theme.color_primary, tui->config.theme.color_bg);
            printf("► main");
            tui_set_color(tui->config.theme.color_muted, tui->config.theme.color_bg);
        } else {
            printf("  session-%d", i);
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

    // Chat content area
    tui_set_color(tui->config.theme.color_fg, tui->config.theme.color_bg);

    // Placeholder messages
    const char* messages[] = {
        "Welcome to CClaw Agent!",
        "Type a message to start chatting.",
        "Use /help for commands.",
        NULL
    };

    for (int i = 0; messages[i] && i < (int)h - 2; i++) {
        tui_move_cursor(x + 2, y + 1 + i);
        printf("%s", messages[i]);
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
    snprintf(status, sizeof(status), " Model: %s  |  Tokens: %u  |  Branch: main ",
             "claude-3.5-sonnet", 1234);

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
                    {
                        const char* hist = tui_history_prev(tui);
                        if (hist) {
                            strncpy(tui->input_buffer, hist, tui->input_capacity - 1);
                            tui->input_len = strlen(tui->input_buffer);
                            tui->input_pos = tui->input_len;
                        }
                    }
                    break;
                case 'B': // Down arrow
                    {
                        const char* hist = tui_history_next(tui);
                        if (hist) {
                            strncpy(tui->input_buffer, hist, tui->input_capacity - 1);
                            tui->input_len = strlen(tui->input_buffer);
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
        tui_chat_add_system_message(tui, "Created new branch");
        tui->needs_redraw = true;
        return ERR_OK;
    }

    if (c == TUI_KEY_CTRL('l')) {
        tui_redraw(tui);
        return ERR_OK;
    }

    // Handle regular input
    switch (c) {
        case '\r':
        case '\n':
            // Submit input
            if (tui->input_len > 0) {
                tui_history_add(tui, tui->input_buffer);
                tui_chat_add_user_message(tui, tui->input_buffer);
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
            if (isprint(c)) {
                tui_input_insert(tui, c);
            }
            break;
    }

    tui->needs_redraw = true;
    return ERR_OK;
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

    // Shift characters left
    for (uint32_t i = tui->input_pos - 1; i < tui->input_len - 1; i++) {
        tui->input_buffer[i] = tui->input_buffer[i + 1];
    }

    tui->input_len--;
    tui->input_pos--;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_delete(tui_t* tui) {
    if (!tui || tui->input_pos >= tui->input_len) return;

    for (uint32_t i = tui->input_pos; i < tui->input_len - 1; i++) {
        tui->input_buffer[i] = tui->input_buffer[i + 1];
    }

    tui->input_len--;
    tui->input_buffer[tui->input_len] = '\0';
}

void tui_input_move_left(tui_t* tui) {
    if (tui && tui->input_pos > 0) {
        tui->input_pos--;
    }
}

void tui_input_move_right(tui_t* tui) {
    if (tui && tui->input_pos < tui->input_len) {
        tui->input_pos++;
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

void tui_chat_add_system_message(tui_t* tui, const char* text) {
    (void)tui;
    // In real implementation, add to message list
    printf("\r\n[System]: %s\r\n", text);
}

void tui_chat_add_user_message(tui_t* tui, const char* text) {
    (void)tui;
    printf("\r\n[User]: %s\r\n", text);
}

void tui_chat_add_assistant_message(tui_t* tui, const char* text) {
    (void)tui;
    printf("\r\n[Assistant]: %s\r\n", text);
}
