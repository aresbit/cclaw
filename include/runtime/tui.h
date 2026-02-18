// tui.h - Terminal UI for CClaw
// SPDX-License-Identifier: MIT

#ifndef CCLAW_RUNTIME_TUI_H
#define CCLAW_RUNTIME_TUI_H

#include "core/types.h"
#include "core/agent.h"
#include "core/error.h"

#include <stdint.h>
#include <stdbool.h>
#include <termios.h>

// Forward declarations
typedef struct tui_t tui_t;
typedef struct tui_config_t tui_config_t;
typedef struct tui_panel_t tui_panel_t;
typedef struct tui_theme_t tui_theme_t;

// TUI Panel types
typedef enum {
    TUI_PANEL_CHAT,       // Main chat area
    TUI_PANEL_SIDEBAR,    // Session/conversation list
    TUI_PANEL_STATUS,     // Status bar
    TUI_PANEL_INPUT,      // Input area
    TUI_PANEL_TOOLBAR,    // Toolbar/commands
} tui_panel_type_t;

// TUI Theme
typedef struct tui_theme_t {
    // Colors (ANSI color codes)
    uint8_t color_bg;
    uint8_t color_fg;
    uint8_t color_primary;
    uint8_t color_secondary;
    uint8_t color_success;
    uint8_t color_warning;
    uint8_t color_error;
    uint8_t color_muted;

    // Styles
    bool use_bold;
    bool use_italic;
    bool use_unicode;     // Use Unicode box-drawing characters
} tui_theme_t;

// TUI Configuration
struct tui_config_t {
    uint16_t width;
    uint16_t height;
    bool use_color;
    bool use_mouse;
    bool show_token_count;
    bool show_timestamps;
    bool show_branch_indicator;
    tui_theme_t theme;
};

// Chat message for TUI display
typedef struct tui_message_t {
    char* text;
    char* sender;  // "user", "assistant", "system"
    uint64_t timestamp;
    struct tui_message_t* next;
} tui_message_t;

// TUI State
struct tui_t {
    tui_config_t config;
    agent_t* agent;
    agent_session_t* session;

    // Terminal state
    struct termios original_termios;
    bool raw_mode;

    // Screen buffer
    char** screen_buffer;
    uint16_t buffer_width;
    uint16_t buffer_height;

    // Input state
    char* input_buffer;
    uint32_t input_pos;
    uint32_t input_len;
    uint32_t input_capacity;

    // History
    char** history;
    uint32_t history_count;
    uint32_t history_pos;
    uint32_t history_capacity;

    // Chat messages (simple linked list)
    tui_message_t* messages;
    tui_message_t* messages_tail;
    uint32_t message_count;

    // Panels
    tui_panel_t* panels[5];
    tui_panel_type_t active_panel;

    // Session selection in sidebar
    uint32_t selected_session;

    // Scrolling
    uint32_t scroll_offset;

    // Running state
    bool running;
    bool needs_redraw;
};

// TUI Panel
struct tui_panel_t {
    tui_panel_type_t type;
    uint16_t x, y;
    uint16_t width, height;
    bool visible;
    bool focused;
};

// ============================================================================
// TUI Lifecycle
// ============================================================================

err_t tui_create(const tui_config_t* config, tui_t** out_tui);
void tui_destroy(tui_t* tui);

err_t tui_init_terminal(tui_t* tui);
void tui_restore_terminal(tui_t* tui);

err_t tui_run(tui_t* tui, agent_t* agent);
void tui_stop(tui_t* tui);

// ============================================================================
// Rendering
// ============================================================================

void tui_clear_screen(tui_t* tui);
void tui_refresh(tui_t* tui);
void tui_redraw(tui_t* tui);

void tui_draw_chat_panel(tui_t* tui);
void tui_draw_sidebar(tui_t* tui);
void tui_draw_status_bar(tui_t* tui);
void tui_draw_input_area(tui_t* tui);
void tui_draw_toolbar(tui_t* tui);

// Drawing primitives
void tui_move_cursor(uint16_t x, uint16_t y);
void tui_set_color(uint8_t fg, uint8_t bg);
void tui_reset_color(void);
void tui_draw_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* title);
void tui_draw_line(uint16_t x, uint16_t y, uint16_t len, bool horizontal);
void tui_draw_text(uint16_t x, uint16_t y, const char* text);
void tui_draw_text_truncated(uint16_t x, uint16_t y, uint16_t max_width, const char* text);

// ============================================================================
// Input Handling
// ============================================================================

err_t tui_process_input(tui_t* tui);
void tui_handle_key(tui_t* tui, int key);
void tui_handle_resize(tui_t* tui);

// Input buffer operations
void tui_input_clear(tui_t* tui);
void tui_input_insert(tui_t* tui, char c);
void tui_input_delete(tui_t* tui);
void tui_input_backspace(tui_t* tui);
void tui_input_move_left(tui_t* tui);
void tui_input_move_right(tui_t* tui);
void tui_input_move_home(tui_t* tui);
void tui_input_move_end(tui_t* tui);
const char* tui_input_get(tui_t* tui);

// ============================================================================
// History
// ============================================================================

void tui_history_add(tui_t* tui, const char* entry);
const char* tui_history_prev(tui_t* tui);
const char* tui_history_next(tui_t* tui);

// ============================================================================
// Chat Display
// ============================================================================

void tui_chat_add_message(tui_t* tui, agent_message_t* message);
void tui_chat_add_system_message(tui_t* tui, const char* text);
void tui_chat_add_user_message(tui_t* tui, const char* text);
void tui_chat_add_assistant_message(tui_t* tui, const char* text);
void tui_chat_add_tool_call(tui_t* tui, const char* tool_name, const char* args);
void tui_chat_add_tool_result(tui_t* tui, const char* tool_name, const char* result);
void tui_chat_scroll_up(tui_t* tui, uint32_t lines);
void tui_chat_scroll_down(tui_t* tui, uint32_t lines);

// ============================================================================
// Sidebar
// ============================================================================

void tui_sidebar_update(tui_t* tui);
void tui_sidebar_draw_sessions(tui_t* tui);
void tui_sidebar_draw_branches(tui_t* tui);

// ============================================================================
// Status Bar
// ============================================================================

void tui_status_update(tui_t* tui, const char* status);
void tui_status_update_model(tui_t* tui, const char* model);
void tui_status_update_tokens(tui_t* tui, uint32_t tokens);
void tui_status_update_position(tui_t* tui, uint32_t current, uint32_t total);

// ============================================================================
// Themes
// ============================================================================

tui_theme_t tui_theme_default(void);
tui_theme_t tui_theme_dark(void);
tui_theme_t tui_theme_light(void);
void tui_theme_apply(tui_t* tui, const tui_theme_t* theme);

// ============================================================================
// Utility Functions
// ============================================================================

void tui_get_terminal_size(uint16_t* out_width, uint16_t* out_height);
bool tui_supports_color(void);
bool tui_supports_unicode(void);

// ANSI escape codes
#define TUI_ESC "\033["
#define TUI_CLEAR_SCREEN TUI_ESC "2J"
#define TUI_CLEAR_LINE TUI_ESC "2K"
#define TUI_CURSOR_HOME TUI_ESC "H"
#define TUI_CURSOR_HIDE TUI_ESC "?25l"
#define TUI_CURSOR_SHOW TUI_ESC "?25h"
#define TUI_CURSOR_SAVE TUI_ESC "s"
#define TUI_CURSOR_RESTORE TUI_ESC "u"

#define TUI_COLOR_RESET TUI_ESC "0m"
#define TUI_COLOR_BOLD TUI_ESC "1m"
#define TUI_COLOR_DIM TUI_ESC "2m"
#define TUI_COLOR_ITALIC TUI_ESC "3m"
#define TUI_COLOR_UNDERLINE TUI_ESC "4m"

#define TUI_COLOR_FG(color) TUI_ESC "38;5;" #color "m"
#define TUI_COLOR_BG(color) TUI_ESC "48;5;" #color "m"

#define TUI_KEY_CTRL(c) ((c) & 0x1f)
#define TUI_KEY_ESC 27
#define TUI_KEY_ENTER 13
#define TUI_KEY_BACKSPACE 127
#define TUI_KEY_DELETE 126
#define TUI_KEY_TAB 9

// ============================================================================
// Configuration
// ============================================================================

tui_config_t tui_config_default(void);

// ============================================================================
// Constants
// ============================================================================

#define TUI_DEFAULT_WIDTH 80
#define TUI_DEFAULT_HEIGHT 24
#define TUI_MIN_WIDTH 40
#define TUI_MIN_HEIGHT 10
#define TUI_INPUT_HISTORY_SIZE 100
#define TUI_MAX_INPUT_LENGTH 4096

#endif // CCLAW_RUNTIME_TUI_H
