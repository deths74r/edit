# Edit Text Editor Modularization Guide

## Phases 25B through 25O

This document contains detailed implementation guides for modularizing the `edit` text editor from a single 16,000+ line `edit.c` file into separate, maintainable modules.

**Prerequisites:** Phase 25A (Foundation Headers) must be complete. You should have:
- `types.h` - All shared type definitions
- `edit.h` - Master header
- `error.h` / `error.c` - Error handling (already existed)
- `edit.c` - Main implementation (updated to use new headers)

**Build Command (use after each phase):**
```bash
cc -std=c17 -Wall -Wextra -Wpedantic -O2 -o edit *.c -lm -pthread
```

**General Rules for All Phases:**
1. Create the `.h` file first with declarations
2. Create the `.c` file with implementations moved from `edit.c`
3. Update `edit.h` to include the new module header
4. Remove the moved code from `edit.c`
5. Add necessary `#include` statements
6. Test compilation and functionality after each phase

---

## Phase 25B: Terminal Module

**Goal:** Extract terminal handling into standalone module.

**Files to Create:**
- `terminal.h`
- `terminal.c`

**Estimated Lines:** ~250

### terminal.h

```c
/*
 * terminal.h - Terminal handling for edit
 *
 * Provides raw mode terminal I/O, window size detection,
 * and cursor position queries.
 */

#ifndef EDIT_TERMINAL_H
#define EDIT_TERMINAL_H

#include "types.h"
#include <termios.h>

/*
 * Enable raw mode for terminal input.
 * Disables canonical mode, echo, and signal generation.
 * Returns 0 on success, negative error code on failure.
 */
int terminal_enable_raw_mode(void);

/*
 * Restore terminal to original settings.
 * Safe to call multiple times or if raw mode was never enabled.
 */
void terminal_disable_raw_mode(void);

/*
 * Check if terminal is currently in raw mode.
 */
bool terminal_is_raw_mode(void);

/*
 * Get the current terminal window size.
 * Returns 0 on success, negative error code on failure.
 */
int terminal_get_window_size(int *rows, int *cols);

/*
 * Get the current cursor position.
 * Returns 0 on success, negative error code on failure.
 */
int terminal_get_cursor_position(int *row, int *col);

#endif /* EDIT_TERMINAL_H */
```

### terminal.c

```c
/*
 * terminal.c - Terminal handling implementation
 */

#include "edit.h"
#include "terminal.h"

/* Original terminal settings for restoration */
static struct termios original_terminal_settings;
static bool raw_mode_enabled = false;
static bool settings_saved = false;

int terminal_enable_raw_mode(void)
{
    if (raw_mode_enabled) {
        return 0;
    }

    if (!isatty(STDIN_FILENO)) {
        return -EEDIT_NOTTY;
    }

    if (tcgetattr(STDIN_FILENO, &original_terminal_settings) == -1) {
        return -EEDIT_TERMRAW;
    }
    settings_saved = true;

    struct termios raw = original_terminal_settings;

    /* Input flags: disable break, CR to NL, parity, strip, flow control */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* Output flags: disable post-processing */
    raw.c_oflag &= ~(OPOST);

    /* Control flags: set 8-bit chars */
    raw.c_cflag |= (CS8);

    /* Local flags: disable echo, canonical, extensions, signals */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* Control chars: set read timeout */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -EEDIT_TERMRAW;
    }

    raw_mode_enabled = true;
    return 0;
}

void terminal_disable_raw_mode(void)
{
    if (settings_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_settings);
    }
    raw_mode_enabled = false;
}

bool terminal_is_raw_mode(void)
{
    return raw_mode_enabled;
}

int terminal_get_window_size(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* Fallback: query cursor position after moving to bottom-right */
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -EEDIT_TERMSIZE;
        }
        return terminal_get_cursor_position(rows, cols);
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

int terminal_get_cursor_position(int *row, int *col)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -EEDIT_TERMSIZE;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -EEDIT_TERMSIZE;
    }
    if (sscanf(&buf[2], "%d;%d", row, col) != 2) {
        return -EEDIT_TERMSIZE;
    }

    return 0;
}
```

### Changes to edit.c

1. Remove the terminal functions and their static variables
2. Remove `#include <termios.h>` (now in terminal.c)
3. Replace calls to local `terminal_*` functions with the module versions

### Update edit.h

Add after other includes:
```c
#include "terminal.h"
```

---

## Phase 25C: Theme Module

**Goal:** Extract theme system, color utilities, style rendering.

**Files to Create:**
- `theme.h`
- `theme.c`

**Estimated Lines:** ~900

### theme.h

```c
/*
 * theme.h - Theme and color system for edit
 *
 * Provides theme loading, color parsing, contrast adjustment,
 * and style rendering functions.
 */

#ifndef EDIT_THEME_H
#define EDIT_THEME_H

#include "types.h"

/*****************************************************************************
 * Theme Management
 *****************************************************************************/

/*
 * Create the default built-in theme.
 */
struct theme theme_create_default(void);

/*
 * Create the monochrome white theme.
 */
struct theme theme_create_mono_white(void);

/*
 * Parse a theme from a .edit file.
 * Returns newly allocated theme, or NULL on error.
 * Caller must free with free().
 */
struct theme *theme_parse_file(const char *filepath);

/*
 * Load all themes from ~/.edit/themes/ directory.
 * Populates the global theme list.
 */
void themes_load(void);

/*
 * Free all loaded themes.
 */
void themes_free(void);

/*
 * Apply a theme by index in the loaded themes list.
 * Computes contrast-adjusted colors.
 */
void theme_apply_by_index(int index);

/*
 * Apply a theme by name.
 * Returns true if theme was found and applied.
 */
bool theme_apply_by_name(const char *name);

/*
 * Get the currently active theme.
 */
struct theme *theme_get_active(void);

/*
 * Get the list of loaded themes and count.
 */
struct theme *theme_get_list(int *count);

/*
 * Get the index of the currently active theme.
 */
int theme_get_active_index(void);

/*****************************************************************************
 * Color Utilities
 *****************************************************************************/

/*
 * Parse a hex color string (e.g., "FF79C6") into a syntax_color.
 * Returns true on success.
 */
bool color_parse_hex(const char *hex, struct syntax_color *color);

/*
 * Ensure foreground color has sufficient contrast against background.
 * Uses WCAG 2.1 guidelines (4.5:1 ratio for normal text).
 */
struct syntax_color color_ensure_contrast(struct syntax_color fg,
                                          struct syntax_color bg);

/*
 * Compute relative luminance of a color (for contrast calculations).
 */
double color_luminance(struct syntax_color c);

/*
 * Compute contrast ratio between two colors.
 */
double color_contrast_ratio(struct syntax_color c1, struct syntax_color c2);

/*****************************************************************************
 * Style Rendering
 *****************************************************************************/

/*
 * Render opening style escape codes to buffer.
 * Returns number of bytes written.
 */
int style_render_open(char *buf, size_t size, struct style style);

/*
 * Render closing/reset escape codes to buffer.
 * Returns number of bytes written.
 */
int style_render_close(char *buf, size_t size);

/*
 * Render foreground color escape code.
 */
int style_render_fg(char *buf, size_t size, struct syntax_color color);

/*
 * Render background color escape code.
 */
int style_render_bg(char *buf, size_t size, struct syntax_color color);

/*
 * Reset all attributes.
 */
int style_render_reset(char *buf, size_t size);

/*****************************************************************************
 * Configuration Persistence
 *****************************************************************************/

/*
 * Load configuration from ~/.editrc
 */
void config_load(void);

/*
 * Save configuration to ~/.editrc
 */
void config_save(void);

#endif /* EDIT_THEME_H */
```

### theme.c Structure

Move the following from edit.c:

1. **Global state:**
   - `loaded_themes`, `loaded_theme_count`, `active_theme`, `active_theme_index`

2. **Color functions:**
   - `color_parse_hex()`
   - `color_luminance()`
   - `color_contrast_ratio()`
   - `color_ensure_contrast()`
   - `color_adjust_lightness()`

3. **Theme functions:**
   - `theme_create_default()`
   - `theme_create_mono_white()`
   - `theme_parse_file()`
   - `themes_load()`
   - `themes_free()`
   - `theme_apply()` (rename to `theme_apply_by_index`)
   - `theme_apply_by_name()`

4. **Style rendering functions:**
   - `style_render_open()`
   - `style_render_close()`
   - `style_render_fg()`
   - `style_render_bg()`
   - `style_render_reset()`

5. **Config functions:**
   - `config_load()`
   - `config_save()`

### Access Pattern

Since `active_theme` needs to be accessed from other modules, provide accessor:

```c
/* In theme.c */
static struct theme active_theme;

struct theme *theme_get_active(void)
{
    return &active_theme;
}
```

In other modules, use `theme_get_active()->foreground` instead of `active_theme.foreground`.

---

## Phase 25D: Buffer Module

**Goal:** Extract core buffer, line, and cell operations.

**Files to Create:**
- `buffer.h`
- `buffer.c`

**Estimated Lines:** ~1,200

### buffer.h

```c
/*
 * buffer.h - Buffer and line management for edit
 *
 * Provides buffer initialization, file loading/saving,
 * line operations, and text manipulation.
 */

#ifndef EDIT_BUFFER_H
#define EDIT_BUFFER_H

#include "types.h"

/*****************************************************************************
 * Line Temperature (Thread-Safe Access)
 *****************************************************************************/

/*
 * Get line temperature atomically.
 */
static inline int line_get_temperature(struct line *line)
{
    return atomic_load_explicit(&line->temperature, memory_order_acquire);
}

/*
 * Set line temperature atomically.
 */
static inline void line_set_temperature(struct line *line, int temp)
{
    atomic_store_explicit(&line->temperature, temp, memory_order_release);
}

/*
 * Try to claim a line for warming (returns true if claimed).
 */
static inline bool line_try_claim_warming(struct line *line)
{
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(
        &line->warming_in_progress,
        &expected,
        true,
        memory_order_acq_rel,
        memory_order_acquire
    );
}

/*
 * Release warming claim on a line.
 */
static inline void line_release_warming(struct line *line)
{
    atomic_store_explicit(&line->warming_in_progress, false, memory_order_release);
}

/*****************************************************************************
 * Line Operations
 *****************************************************************************/

/*
 * Initialize a line with content from mmap.
 */
void line_init(struct line *line, size_t mmap_offset, uint32_t mmap_length);

/*
 * Initialize an empty line (for new lines).
 */
void line_init_empty(struct line *line);

/*
 * Free line resources.
 */
void line_free(struct line *line);

/*
 * Warm a cold line (decode UTF-8 to cells).
 * Main thread version.
 */
void line_warm(struct buffer *buffer, uint32_t row);

/*
 * Warm a line from worker thread (thread-safe version).
 * Returns 0 on success, negative error code on failure.
 */
int line_warm_from_worker(struct line *line, struct buffer *buffer);

/*
 * Ensure line has capacity for at least n cells.
 */
void line_ensure_capacity(struct line *line, uint32_t capacity);

/*
 * Insert a cell at position.
 */
void line_insert_cell(struct line *line, uint32_t pos, struct cell cell);

/*
 * Delete a cell at position.
 */
void line_delete_cell(struct line *line, uint32_t pos);

/*****************************************************************************
 * Buffer Operations
 *****************************************************************************/

/*
 * Initialize a buffer.
 */
void buffer_init(struct buffer *buffer);

/*
 * Free buffer resources.
 */
void buffer_free(struct buffer *buffer);

/*
 * Load a file into the buffer.
 * Returns true on success.
 */
bool buffer_load(struct buffer *buffer, const char *filename);

/*
 * Save buffer to file.
 * Returns true on success.
 */
bool buffer_save(struct buffer *buffer);

/*
 * Save buffer to a specific file.
 * Returns true on success.
 */
bool buffer_save_as(struct buffer *buffer, const char *filename);

/*****************************************************************************
 * Text Manipulation
 *****************************************************************************/

/*
 * Insert a character at position.
 */
void buffer_insert_character(struct buffer *buffer, uint32_t row, uint32_t col,
                             uint32_t codepoint);

/*
 * Delete a character at position.
 */
void buffer_delete_character(struct buffer *buffer, uint32_t row, uint32_t col);

/*
 * Insert a newline at position.
 */
void buffer_insert_newline(struct buffer *buffer, uint32_t row, uint32_t col);

/*
 * Delete a line.
 */
void buffer_delete_line(struct buffer *buffer, uint32_t row);

/*
 * Insert an empty line at position.
 */
void buffer_insert_line(struct buffer *buffer, uint32_t row);

/*
 * Delete a range of text.
 */
void buffer_delete_range(struct buffer *buffer,
                         uint32_t start_row, uint32_t start_col,
                         uint32_t end_row, uint32_t end_col);

/*
 * Get text in a range as a newly allocated string.
 * Caller must free the result.
 */
char *buffer_get_text_range(struct buffer *buffer,
                            uint32_t start_row, uint32_t start_col,
                            uint32_t end_row, uint32_t end_col);

/*
 * Insert text at position (handles multi-line).
 */
void buffer_insert_text(struct buffer *buffer, uint32_t row, uint32_t col,
                        const char *text, size_t length);

/*****************************************************************************
 * Grapheme Boundaries
 *****************************************************************************/

/*
 * Check if position is at a grapheme cluster boundary.
 */
bool is_grapheme_boundary(struct line *line, uint32_t col);

/*
 * Find next grapheme cluster boundary.
 */
uint32_t next_grapheme_boundary(struct line *line, uint32_t col);

/*
 * Find previous grapheme cluster boundary.
 */
uint32_t prev_grapheme_boundary(struct line *line, uint32_t col);

#endif /* EDIT_BUFFER_H */
```

### buffer.c Structure

Move from edit.c:
- All `line_*()` functions
- All `buffer_*()` functions
- Grapheme boundary functions
- The wrap cache functions (or keep with soft wrap - decide based on coupling)

### Dependencies

buffer.c will need:
```c
#include "edit.h"
#include "buffer.h"
```

It will also need access to `utflite.h` for UTF-8 decoding.

---

## Phase 25E: Syntax Module

**Goal:** Extract syntax highlighting and neighbor layer.

**Files to Create:**
- `syntax.h`
- `syntax.c`

**Estimated Lines:** ~600

### syntax.h

```c
/*
 * syntax.h - Syntax highlighting for edit
 *
 * Provides language detection, syntax highlighting,
 * and the neighbor layer for bracket matching.
 */

#ifndef EDIT_SYNTAX_H
#define EDIT_SYNTAX_H

#include "types.h"

/*****************************************************************************
 * Language Detection
 *****************************************************************************/

/*
 * Detect language from filename extension.
 */
enum language_type syntax_detect_language(const char *filename);

/*
 * Get file extensions for a language.
 */
const char **syntax_get_extensions(enum language_type lang, int *count);

/*****************************************************************************
 * Syntax Highlighting
 *****************************************************************************/

/*
 * Highlight a single line.
 * Sets the syntax field of each cell.
 */
void syntax_highlight_line(struct line *line, struct buffer *buffer, uint32_t row);

/*
 * Highlight all lines in buffer.
 */
void syntax_highlight_buffer(struct buffer *buffer);

/*
 * Check if a codepoint is part of a keyword character.
 */
bool syntax_is_keyword_char(uint32_t codepoint);

/*****************************************************************************
 * Neighbor Layer
 *****************************************************************************/

/*
 * Compute neighbor layer for a line.
 * Sets character class and token position data.
 */
void neighbor_compute_line(struct line *line);

/*
 * Compute neighbor layer for entire buffer.
 */
void neighbor_compute_buffer(struct buffer *buffer);

/*
 * Find matching bracket for position.
 * Returns true if match found, with match position in out parameters.
 */
bool neighbor_find_matching_bracket(struct buffer *buffer,
                                    uint32_t row, uint32_t col,
                                    uint32_t *match_row, uint32_t *match_col);

/*
 * Get character class from neighbor data.
 */
int neighbor_get_class(uint8_t neighbor);

/*
 * Check if position is at word boundary.
 */
bool neighbor_is_word_boundary(struct line *line, uint32_t col);

#endif /* EDIT_SYNTAX_H */
```

### syntax.c Structure

Move from edit.c:
- `syntax_detect_language()`
- `syntax_highlight_line()`
- `syntax_highlight_buffer()`
- All `neighbor_*()` functions
- Keyword tables for each language
- Character classification functions

---

## Phase 25F: Undo Module

**Goal:** Extract undo/redo system.

**Files to Create:**
- `undo.h`
- `undo.c`

**Estimated Lines:** ~650

### undo.h

```c
/*
 * undo.h - Undo/redo system for edit
 *
 * Provides operation recording, grouping, and replay
 * for undo/redo functionality.
 */

#ifndef EDIT_UNDO_H
#define EDIT_UNDO_H

#include "types.h"

/*****************************************************************************
 * Undo History Management
 *****************************************************************************/

/*
 * Initialize undo history for a buffer.
 */
void undo_init(struct undo_history *history);

/*
 * Free all undo history.
 */
void undo_free(struct undo_history *history);

/*
 * Clear the redo stack (called after new edits).
 */
void undo_clear_redo_stack(struct undo_history *history);

/*****************************************************************************
 * Operation Recording
 *****************************************************************************/

/*
 * Record an insert operation.
 */
void undo_record_insert(struct buffer *buffer, uint32_t row, uint32_t col,
                        uint32_t codepoint);

/*
 * Record a delete operation.
 */
void undo_record_delete(struct buffer *buffer, uint32_t row, uint32_t col,
                        uint32_t codepoint);

/*
 * Record a newline insert.
 */
void undo_record_insert_newline(struct buffer *buffer, uint32_t row, uint32_t col);

/*
 * Record a newline delete (line join).
 */
void undo_record_delete_newline(struct buffer *buffer, uint32_t row);

/*
 * Record a range delete.
 */
void undo_record_delete_range(struct buffer *buffer,
                              uint32_t start_row, uint32_t start_col,
                              uint32_t end_row, uint32_t end_col,
                              const uint32_t *codepoints, uint32_t count);

/*****************************************************************************
 * Undo Groups
 *****************************************************************************/

/*
 * Begin a new undo group.
 * All operations until end_group are undone/redone together.
 */
void undo_begin_group(struct buffer *buffer);

/*
 * End the current undo group.
 */
void undo_end_group(struct buffer *buffer);

/*
 * Check if we should merge with previous operation.
 * Used for coalescing rapid keystrokes.
 */
bool undo_should_merge(struct undo_history *history, enum edit_op_type type,
                       uint32_t row, uint32_t col);

/*****************************************************************************
 * Undo/Redo Execution
 *****************************************************************************/

/*
 * Undo the last operation/group.
 * Returns true if something was undone.
 */
bool undo_undo(struct buffer *buffer, uint32_t *cursor_row, uint32_t *cursor_col);

/*
 * Redo the last undone operation/group.
 * Returns true if something was redone.
 */
bool undo_redo(struct buffer *buffer, uint32_t *cursor_row, uint32_t *cursor_col);

/*
 * Check if undo is available.
 */
bool undo_can_undo(struct undo_history *history);

/*
 * Check if redo is available.
 */
bool undo_can_redo(struct undo_history *history);

#endif /* EDIT_UNDO_H */
```

### undo.c Structure

Move from edit.c:
- All `undo_*()` functions
- Operation creation/freeing helpers
- Group management logic

---

## Phase 25G: Input Module

**Goal:** Extract input handling.

**Files to Create:**
- `input.h`
- `input.c`

**Estimated Lines:** ~400

### input.h

```c
/*
 * input.h - Input handling for edit
 *
 * Provides keyboard and mouse input parsing.
 */

#ifndef EDIT_INPUT_H
#define EDIT_INPUT_H

#include "types.h"

/*****************************************************************************
 * Key Input
 *****************************************************************************/

/*
 * Read a key from stdin.
 * Blocks until input is available.
 * Returns a key code (may be a special KEY_* value).
 */
int input_read_key(void);

/*
 * Check if input is available without blocking.
 */
bool input_available(void);

/*****************************************************************************
 * Mouse Input
 *****************************************************************************/

/*
 * Get the last mouse input event.
 */
struct mouse_input input_get_last_mouse(void);

/*
 * Enable mouse tracking.
 */
void input_enable_mouse(void);

/*
 * Disable mouse tracking.
 */
void input_disable_mouse(void);

/*
 * Check if a key code is a mouse event.
 */
bool input_is_mouse_event(int key);

/*
 * Parse SGR mouse escape sequence.
 * Called internally by input_read_key.
 */
struct mouse_input input_parse_sgr_mouse(void);

/*****************************************************************************
 * Dialog Mouse Mode
 *****************************************************************************/

/*
 * Set dialog mouse mode (intercepts mouse for dialogs).
 */
void input_set_dialog_mouse_mode(bool enabled);

/*
 * Check if dialog mouse mode is active.
 */
bool input_get_dialog_mouse_mode(void);

#endif /* EDIT_INPUT_H */
```

### input.c Structure

Move from edit.c:
- `input_read_key()`
- `input_parse_sgr_mouse()`
- Mouse tracking enable/disable
- Related static state (last_mouse, dialog_mouse_mode)

---

## Phase 25H: Render Module

**Goal:** Extract rendering system.

**Files to Create:**
- `render.h`
- `render.c`

**Estimated Lines:** ~1,100

### render.h

```c
/*
 * render.h - Screen rendering for edit
 *
 * Provides screen refresh, status bar, message bar,
 * and soft-wrap rendering.
 */

#ifndef EDIT_RENDER_H
#define EDIT_RENDER_H

#include "types.h"

/*****************************************************************************
 * Output Buffer
 *****************************************************************************/

/*
 * Initialize an output buffer.
 */
void output_buffer_init(struct output_buffer *buf);

/*
 * Append data to output buffer.
 */
void output_buffer_append(struct output_buffer *buf, const char *s, size_t len);

/*
 * Append formatted string to output buffer.
 */
void output_buffer_appendf(struct output_buffer *buf, const char *fmt, ...);

/*
 * Free output buffer resources.
 */
void output_buffer_free(struct output_buffer *buf);

/*
 * Write output buffer to file descriptor.
 */
void output_buffer_write(struct output_buffer *buf, int fd);

/*****************************************************************************
 * Screen Rendering
 *****************************************************************************/

/*
 * Refresh the entire screen.
 */
void render_refresh_screen(void);

/*
 * Draw the text rows.
 */
void render_draw_rows(struct output_buffer *buf);

/*
 * Draw the status bar.
 */
void render_draw_status_bar(struct output_buffer *buf);

/*
 * Draw the message bar.
 */
void render_draw_message_bar(struct output_buffer *buf);

/*****************************************************************************
 * Soft Wrap
 *****************************************************************************/

/*
 * Compute wrap points for a line.
 */
void wrap_compute_line(struct line *line, uint32_t width);

/*
 * Invalidate wrap cache for a line.
 */
void wrap_invalidate_line(struct line *line);

/*
 * Invalidate wrap cache for entire buffer.
 */
void wrap_invalidate_buffer(struct buffer *buffer);

/*
 * Get number of screen rows a line occupies.
 */
uint32_t wrap_get_screen_rows(struct line *line, uint32_t width);

/*
 * Convert buffer position to screen position.
 */
void wrap_buffer_to_screen(struct line *line, uint32_t col, uint32_t width,
                           uint32_t *screen_row, uint32_t *screen_col);

/*
 * Convert screen position to buffer position.
 */
uint32_t wrap_screen_to_buffer(struct line *line, uint32_t screen_row,
                               uint32_t screen_col, uint32_t width);

/*****************************************************************************
 * Cursor Display
 *****************************************************************************/

/*
 * Get cursor screen position (accounting for wrapping).
 */
void render_get_cursor_screen_pos(uint32_t *screen_x, uint32_t *screen_y);

/*
 * Set cursor visibility.
 */
void render_set_cursor_visible(bool visible);

#endif /* EDIT_RENDER_H */
```

### render.c Structure

Move from edit.c:
- Output buffer functions
- `render_refresh_screen()`
- `render_draw_rows()`
- `render_draw_status_bar()`
- `render_draw_message_bar()`
- All soft wrap functions
- Related rendering helpers

**Note:** This module has significant dependencies on:
- theme (for colors)
- buffer (for line data)
- syntax (for highlighting)
- editor state (for cursor, scroll position)

You may need to pass these as parameters or use accessor functions.

---

## Phase 25I: Worker Module

**Goal:** Extract background thread infrastructure.

**Files to Create:**
- `worker.h`
- `worker.c`

**Estimated Lines:** ~1,600

### worker.h

```c
/*
 * worker.h - Background worker thread for edit
 *
 * Provides task queue, result queue, and worker thread
 * for background operations like search and line warming.
 */

#ifndef EDIT_WORKER_H
#define EDIT_WORKER_H

#include "types.h"

/*****************************************************************************
 * Worker Lifecycle
 *****************************************************************************/

/*
 * Initialize the worker thread.
 * Returns 0 on success, negative error code on failure.
 */
int worker_init(void);

/*
 * Shutdown the worker thread.
 */
void worker_shutdown(void);

/*
 * Check if worker is initialized.
 */
bool worker_is_initialized(void);

/*****************************************************************************
 * Task Management
 *****************************************************************************/

/*
 * Generate a unique task ID.
 */
uint64_t task_generate_id(void);

/*
 * Submit a task to the worker.
 * Returns 0 on success, negative error code on failure.
 */
int task_queue_push(struct task *task);

/*
 * Cancel a pending or running task.
 * Returns true if task was found and cancelled.
 */
bool task_cancel(uint64_t task_id);

/*
 * Cancel all tasks of a specific type.
 */
void task_cancel_all_of_type(enum task_type type);

/*
 * Check if a task has been cancelled.
 */
static inline bool task_is_cancelled(struct task *task)
{
    return atomic_load_explicit(&task->cancelled, memory_order_relaxed);
}

/*****************************************************************************
 * Result Processing
 *****************************************************************************/

/*
 * Process pending results from worker.
 * Call periodically from main loop.
 */
void worker_process_results(void);

/*
 * Check if there are pending results.
 */
bool worker_has_pending_results(void);

#endif /* EDIT_WORKER_H */
```

### worker.c Structure

Move from edit.c:
- `struct worker_state` and global instance
- Task queue functions
- Result queue functions
- `worker_thread_main()`
- `worker_init()` / `worker_shutdown()`
- `worker_process_results()`
- Task processing functions (warm, search, replace, autosave)

**Note:** The task processing functions (`worker_process_warm_lines`, etc.) need access to the buffer. You may want to keep them here but have them call into other modules, or move them to their respective modules (search.c, autosave.c).

---

## Phase 25J: Search Module

**Goal:** Extract search and replace functionality.

**Files to Create:**
- `search.h`
- `search.c`

**Estimated Lines:** ~1,100

### search.h

```c
/*
 * search.h - Search and replace for edit
 *
 * Provides incremental search, regex support,
 * and background search for large files.
 */

#ifndef EDIT_SEARCH_H
#define EDIT_SEARCH_H

#include "types.h"

/*****************************************************************************
 * Search State
 *****************************************************************************/

/*
 * Get the search state.
 */
struct search_state *search_get_state(void);

/*
 * Initialize search system.
 */
int search_init(void);

/*
 * Cleanup search resources.
 */
void search_cleanup(void);

/*****************************************************************************
 * Search Operations
 *****************************************************************************/

/*
 * Start a new search.
 */
void search_start(void);

/*
 * Start search and replace mode.
 */
void search_start_replace(void);

/*
 * Cancel search.
 */
void search_cancel(void);

/*
 * Execute search with current query.
 */
void search_execute(void);

/*
 * Find next match.
 */
bool search_find_next(void);

/*
 * Find previous match.
 */
bool search_find_prev(void);

/*
 * Replace current match.
 */
bool search_replace_current(void);

/*
 * Replace all matches.
 */
void search_replace_all(void);

/*****************************************************************************
 * Search Options
 *****************************************************************************/

/*
 * Toggle case sensitivity.
 */
void search_toggle_case_sensitive(void);

/*
 * Toggle regex mode.
 */
void search_toggle_regex(void);

/*
 * Toggle whole word matching.
 */
void search_toggle_whole_word(void);

/*****************************************************************************
 * Async Search
 *****************************************************************************/

/*
 * Check if async search should be used.
 */
bool search_should_use_async(void);

/*
 * Start async search.
 */
void search_async_start(const char *pattern, bool use_regex,
                        bool case_sensitive, bool whole_word);

/*
 * Cancel async search.
 */
void search_async_cancel(void);

/*
 * Get async search progress.
 */
uint32_t search_async_get_progress(bool *complete, uint32_t *rows_searched,
                                   uint32_t *total_rows);

/*
 * Navigate to next async match.
 */
bool search_async_next_match(void);

/*
 * Navigate to previous async match.
 */
bool search_async_prev_match(void);

/*
 * Check if a cell is in a search match.
 * Returns: 0 = no match, 1 = match, 2 = current match
 */
int search_get_match_state(uint32_t row, uint32_t col);

/*****************************************************************************
 * Async Replace
 *****************************************************************************/

/*
 * Start async replace all.
 */
void search_async_replace_start(const char *pattern, const char *replacement,
                                bool use_regex, bool case_sensitive,
                                bool whole_word);

/*
 * Cancel async replace.
 */
void search_async_replace_cancel(void);

/*
 * Get async replace progress.
 */
uint32_t search_async_replace_get_progress(bool *search_complete,
                                           bool *apply_complete);

#endif /* EDIT_SEARCH_H */
```

### search.c Structure

Move from edit.c:
- `struct search_state` and global instance
- `struct async_search_state` and global instance
- `struct async_replace_state` and global instance
- All `search_*()` functions
- All `async_search_*()` functions
- All `async_replace_*()` functions
- Search results storage

---

## Phase 25K: Autosave Module

**Goal:** Extract auto-save and crash recovery.

**Files to Create:**
- `autosave.h`
- `autosave.c`

**Estimated Lines:** ~700

### autosave.h

```c
/*
 * autosave.h - Auto-save and crash recovery for edit
 *
 * Provides periodic automatic saves and swap file recovery.
 */

#ifndef EDIT_AUTOSAVE_H
#define EDIT_AUTOSAVE_H

#include "types.h"

/*****************************************************************************
 * Autosave Configuration
 *****************************************************************************/

/*
 * Enable or disable autosave.
 */
void autosave_set_enabled(bool enabled);

/*
 * Check if autosave is enabled.
 */
bool autosave_is_enabled(void);

/*****************************************************************************
 * Autosave Operations
 *****************************************************************************/

/*
 * Check if autosave should run and trigger if needed.
 * Call periodically from main loop.
 */
void autosave_check(void);

/*
 * Mark buffer as modified (for autosave tracking).
 */
void autosave_mark_modified(void);

/*
 * Update swap file path after filename change.
 */
void autosave_update_path(void);

/*
 * Remove swap file (after clean save or exit).
 */
void autosave_remove_swap(void);

/*****************************************************************************
 * Crash Recovery
 *****************************************************************************/

/*
 * Check if a swap file exists for the given filename.
 * Returns the swap file path if found, NULL otherwise.
 */
const char *autosave_check_recovery(const char *filename);

/*
 * Prompt user for recovery decision.
 * Returns true if user chose to recover.
 */
bool autosave_prompt_recovery(const char *filename, const char *swap_path);

/*
 * Open a file with recovery check.
 * Returns true if file was loaded.
 */
bool autosave_open_with_recovery(const char *filename);

/*****************************************************************************
 * Buffer Snapshot
 *****************************************************************************/

/*
 * Create a snapshot of current buffer for background saving.
 * Returns NULL on failure.
 */
struct buffer_snapshot *buffer_snapshot_create(void);

/*
 * Free a buffer snapshot.
 */
void buffer_snapshot_free(struct buffer_snapshot *snapshot);

#endif /* EDIT_AUTOSAVE_H */
```

### autosave.c Structure

Move from edit.c:
- `struct autosave_state` and global instance
- `struct buffer_snapshot`
- All `autosave_*()` functions
- `buffer_snapshot_create()` / `buffer_snapshot_free()`
- Swap path generation

---

## Phase 25L: Dialog Module

**Goal:** Extract dialog system, file browser, theme picker.

**Files to Create:**
- `dialog.h`
- `dialog.c`

**Estimated Lines:** ~800

### dialog.h

```c
/*
 * dialog.h - Dialog panels for edit
 *
 * Provides modal dialog infrastructure, file browser,
 * and theme picker.
 */

#ifndef EDIT_DIALOG_H
#define EDIT_DIALOG_H

#include "types.h"

/*****************************************************************************
 * Dialog State Management
 *****************************************************************************/

/*
 * Initialize a dialog state.
 */
void dialog_init(struct dialog_state *dialog, int item_count);

/*
 * Calculate dialog panel dimensions.
 */
void dialog_calculate_dimensions(struct dialog_state *dialog,
                                 int screen_rows, int screen_cols,
                                 int width_percent, int height_percent);

/*****************************************************************************
 * Dialog Rendering
 *****************************************************************************/

/*
 * Draw dialog header.
 */
void dialog_draw_header(struct output_buffer *buf, struct dialog_state *dialog,
                        const char *title);

/*
 * Draw dialog footer with help text.
 */
void dialog_draw_footer(struct output_buffer *buf, struct dialog_state *dialog,
                        const char *help_text);

/*
 * Draw a list item.
 */
void dialog_draw_list_item(struct output_buffer *buf, struct dialog_state *dialog,
                           int index, const char *text, bool is_selected,
                           bool is_directory);

/*
 * Draw empty rows to fill panel.
 */
void dialog_draw_empty_rows(struct output_buffer *buf, struct dialog_state *dialog,
                            int start_row, int end_row);

/*****************************************************************************
 * Dialog Input Handling
 *****************************************************************************/

/*
 * Handle keyboard input for dialog.
 * Returns true if dialog should close.
 */
bool dialog_handle_key(struct dialog_state *dialog, int key, int *selected);

/*
 * Handle mouse input for dialog.
 * Returns action: 0 = none, 1 = select, 2 = double-click
 */
int dialog_handle_mouse(struct dialog_state *dialog, struct mouse_input *mouse);

/*****************************************************************************
 * File Browser
 *****************************************************************************/

/*
 * Open the file browser dialog.
 */
void file_browser_open(void);

/*
 * Close the file browser.
 */
void file_browser_close(void);

/*
 * Check if file browser is active.
 */
bool file_browser_is_active(void);

/*
 * Handle file browser input.
 */
void file_browser_handle_key(int key);

/*
 * Render file browser.
 */
void file_browser_render(struct output_buffer *buf);

/*****************************************************************************
 * Theme Picker
 *****************************************************************************/

/*
 * Open the theme picker dialog.
 */
void theme_picker_open(void);

/*
 * Close the theme picker.
 */
void theme_picker_close(void);

/*
 * Check if theme picker is active.
 */
bool theme_picker_is_active(void);

/*
 * Handle theme picker input.
 */
void theme_picker_handle_key(int key);

/*
 * Render theme picker.
 */
void theme_picker_render(struct output_buffer *buf);

/*****************************************************************************
 * File List Utilities
 *****************************************************************************/

/*
 * Read directory contents into file list.
 * Returns array of items, caller must free.
 */
struct file_list_item *file_list_read_directory(const char *path, int *count);

/*
 * Free file list.
 */
void file_list_free(struct file_list_item *items, int count);

/*
 * Path manipulation utilities.
 */
char *path_get_parent(const char *path);
char *path_join(const char *dir, const char *name);

#endif /* EDIT_DIALOG_H */
```

### dialog.c Structure

Move from edit.c:
- `struct dialog_state` helpers
- `struct file_list_item` utilities
- `struct open_file_state` and global instance
- `struct theme_picker_state` and global instance
- All dialog drawing functions
- File browser functions
- Theme picker functions
- Path utilities

---

## Phase 25M: Clipboard Module

**Goal:** Extract clipboard integration.

**Files to Create:**
- `clipboard.h`
- `clipboard.c`

**Estimated Lines:** ~350

### clipboard.h

```c
/*
 * clipboard.h - System clipboard integration for edit
 *
 * Provides copy, cut, and paste with system clipboard.
 */

#ifndef EDIT_CLIPBOARD_H
#define EDIT_CLIPBOARD_H

#include "types.h"

/*****************************************************************************
 * Clipboard Operations
 *****************************************************************************/

/*
 * Copy text to system clipboard.
 * Returns true on success.
 */
bool clipboard_copy(const char *text, size_t length);

/*
 * Get text from system clipboard.
 * Returns newly allocated string, caller must free.
 * Returns NULL if clipboard is empty or on error.
 */
char *clipboard_paste(void);

/*
 * Check if clipboard has text content.
 */
bool clipboard_has_text(void);

/*****************************************************************************
 * Editor Clipboard Operations
 *****************************************************************************/

/*
 * Copy current selection to clipboard.
 */
void editor_clipboard_copy(void);

/*
 * Cut current selection to clipboard.
 */
void editor_clipboard_cut(void);

/*
 * Paste from clipboard at cursor.
 */
void editor_clipboard_paste(void);

/*****************************************************************************
 * Internal Clipboard (fallback)
 *****************************************************************************/

/*
 * Store text in internal clipboard.
 */
void clipboard_internal_store(const char *text, size_t length);

/*
 * Get text from internal clipboard.
 */
const char *clipboard_internal_get(size_t *length);

/*
 * Clear internal clipboard.
 */
void clipboard_internal_clear(void);

#endif /* EDIT_CLIPBOARD_H */
```

### clipboard.c Structure

Move from edit.c:
- System clipboard detection (xclip, xsel, pbcopy, wl-copy)
- `clipboard_copy()` / `clipboard_paste()`
- Internal clipboard fallback
- Editor clipboard operations

---

## Phase 25N: Editor Module

**Goal:** Extract core editor operations and state.

**Files to Create:**
- `editor.h`
- `editor.c`

**Estimated Lines:** ~1,500

### editor.h

```c
/*
 * editor.h - Core editor state and operations
 *
 * Provides the main editor state structure and
 * high-level editing operations.
 */

#ifndef EDIT_EDITOR_H
#define EDIT_EDITOR_H

#include "types.h"

/*****************************************************************************
 * Editor State Structure
 *****************************************************************************/

struct editor_state {
    /* Buffer */
    struct buffer buffer;
    
    /* Display dimensions */
    uint32_t screen_rows;
    uint32_t screen_columns;
    
    /* Viewport */
    uint32_t row_offset;
    uint32_t col_offset;
    
    /* Primary cursor */
    uint32_t cursor_row;
    uint32_t cursor_column;
    uint32_t preferred_column;
    
    /* Selection */
    bool has_selection;
    uint32_t sel_anchor_row;
    uint32_t sel_anchor_col;
    
    /* Multi-cursor */
    struct cursor cursors[MAX_CURSORS];
    int cursor_count;
    
    /* Status message */
    char status_message[256];
    time_t status_message_time;
    
    /* Mode flags */
    bool soft_wrap_enabled;
    bool line_numbers_enabled;
    bool show_whitespace;
    
    /* Quit confirmation */
    int quit_confirm_counter;
};

/*****************************************************************************
 * Global Editor Access
 *****************************************************************************/

/*
 * Get the editor state.
 */
struct editor_state *editor_get_state(void);

/*
 * Shorthand for common accesses.
 */
#define E (editor_get_state())

/*****************************************************************************
 * Editor Lifecycle
 *****************************************************************************/

/*
 * Initialize the editor.
 */
int editor_init(void);

/*
 * Cleanup editor resources.
 */
void editor_cleanup(void);

/*
 * Open a file in the editor.
 */
bool editor_open(const char *filename);

/*
 * Create a new empty buffer.
 */
void editor_new(void);

/*****************************************************************************
 * Cursor Movement
 *****************************************************************************/

/*
 * Move cursor in direction.
 */
void editor_move_cursor(int key);

/*
 * Move cursor to specific position.
 */
void editor_set_cursor(uint32_t row, uint32_t col);

/*
 * Scroll viewport to keep cursor visible.
 */
void editor_scroll(void);

/*
 * Page up/down.
 */
void editor_page_up(void);
void editor_page_down(void);

/*
 * Go to start/end of line.
 */
void editor_home(void);
void editor_end(void);

/*
 * Go to start/end of buffer.
 */
void editor_go_to_start(void);
void editor_go_to_end(void);

/*
 * Go to specific line number.
 */
void editor_go_to_line(uint32_t line);

/*****************************************************************************
 * Text Editing
 *****************************************************************************/

/*
 * Insert character at cursor.
 */
void editor_insert_char(uint32_t codepoint);

/*
 * Delete character at cursor (Delete key).
 */
void editor_delete_char(void);

/*
 * Delete character before cursor (Backspace).
 */
void editor_backspace(void);

/*
 * Insert newline at cursor.
 */
void editor_insert_newline(void);

/*
 * Delete current line.
 */
void editor_delete_line(void);

/*
 * Duplicate current line.
 */
void editor_duplicate_line(void);

/*****************************************************************************
 * Selection
 *****************************************************************************/

/*
 * Start selection at current cursor.
 */
void editor_start_selection(void);

/*
 * Clear selection.
 */
void editor_clear_selection(void);

/*
 * Extend selection to cursor.
 */
void editor_extend_selection(void);

/*
 * Select all text.
 */
void editor_select_all(void);

/*
 * Select current word.
 */
void editor_select_word(void);

/*
 * Select current line.
 */
void editor_select_line(void);

/*
 * Get selection bounds (normalized).
 */
void editor_get_selection(uint32_t *start_row, uint32_t *start_col,
                          uint32_t *end_row, uint32_t *end_col);

/*
 * Check if position is in selection.
 */
bool editor_pos_in_selection(uint32_t row, uint32_t col);

/*****************************************************************************
 * Multi-Cursor
 *****************************************************************************/

/*
 * Add cursor at position.
 */
void editor_add_cursor(uint32_t row, uint32_t col);

/*
 * Add cursor for next occurrence of selection.
 */
void editor_add_cursor_next_match(void);

/*
 * Clear all secondary cursors.
 */
void editor_clear_cursors(void);

/*
 * Merge overlapping cursors.
 */
void editor_merge_cursors(void);

/*****************************************************************************
 * File Operations
 *****************************************************************************/

/*
 * Save current buffer.
 */
bool editor_save(void);

/*
 * Save buffer with new name.
 */
bool editor_save_as(const char *filename);

/*
 * Check if buffer has unsaved changes.
 */
bool editor_is_modified(void);

/*****************************************************************************
 * Status Message
 *****************************************************************************/

/*
 * Set status bar message.
 */
void editor_set_status_message(const char *fmt, ...);

/*
 * Clear status message.
 */
void editor_clear_status_message(void);

/*****************************************************************************
 * Mode Toggles
 *****************************************************************************/

/*
 * Toggle soft wrap.
 */
void editor_toggle_wrap(void);

/*
 * Toggle line numbers.
 */
void editor_toggle_line_numbers(void);

/*
 * Toggle whitespace visibility.
 */
void editor_toggle_whitespace(void);

#endif /* EDIT_EDITOR_H */
```

### editor.c Structure

Move from edit.c:
- `struct editor_state` global instance
- All `editor_*()` functions
- Multi-cursor management
- Selection functions
- Cursor movement
- Status message handling
- Go-to-line dialog
- Save-as dialog
- Quit prompt

---

## Phase 25O: Main Module & Build System

**Goal:** Create main.c and Makefile for final integration.

**Files to Create:**
- `main.c`
- `Makefile`

**Estimated Lines:** ~400

### main.c

```c
/*
 * main.c - Entry point for edit text editor
 */

#include "edit.h"
#include "terminal.h"
#include "theme.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "worker.h"
#include "search.h"
#include "autosave.h"
#include "dialog.h"

#include <signal.h>

/*****************************************************************************
 * Signal Handling
 *****************************************************************************/

static volatile sig_atomic_t window_size_changed = 0;

static void handle_sigwinch(int sig)
{
    (void)sig;
    window_size_changed = 1;
}

static void handle_sigterm(int sig)
{
    (void)sig;
    terminal_disable_raw_mode();
    autosave_remove_swap();
    _exit(0);
}

static void setup_signal_handlers(void)
{
    struct sigaction sa;
    
    /* SIGWINCH - window resize */
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);
    
    /* SIGTERM/SIGINT - graceful shutdown */
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

/*****************************************************************************
 * Main Loop
 *****************************************************************************/

static void process_keypress(void)
{
    int key = input_read_key();
    
    /* Handle window resize */
    if (window_size_changed) {
        window_size_changed = 0;
        terminal_get_window_size((int *)&E->screen_rows, (int *)&E->screen_columns);
        E->screen_rows -= 2;  /* Status + message bars */
        wrap_invalidate_buffer(&E->buffer);
    }
    
    /* Handle dialogs first */
    if (file_browser_is_active()) {
        file_browser_handle_key(key);
        return;
    }
    
    if (theme_picker_is_active()) {
        theme_picker_handle_key(key);
        return;
    }
    
    if (search_get_state()->active) {
        /* Search mode handling... */
        return;
    }
    
    /* Normal mode key handling */
    switch (key) {
    case CONTROL_KEY('q'):
        /* Quit logic... */
        break;
        
    case CONTROL_KEY('s'):
        editor_save();
        break;
        
    case CONTROL_KEY('o'):
        file_browser_open();
        break;
        
    case CONTROL_KEY('f'):
        search_start();
        break;
        
    case KEY_F5:
        theme_picker_open();
        break;
        
    /* ... other key handlers ... */
    
    default:
        if (key >= 32 && key < 127) {
            editor_insert_char(key);
        }
        break;
    }
}

/*****************************************************************************
 * Entry Point
 *****************************************************************************/

int main(int argc, char *argv[])
{
    /* Initialize terminal */
    if (terminal_enable_raw_mode() != 0) {
        fprintf(stderr, "Failed to enable raw mode\n");
        return 1;
    }
    
    /* Set up signal handlers */
    setup_signal_handlers();
    
    /* Enable mouse */
    input_enable_mouse();
    
    /* Initialize subsystems */
    if (editor_init() != 0) {
        terminal_disable_raw_mode();
        fprintf(stderr, "Failed to initialize editor\n");
        return 1;
    }
    
    themes_load();
    config_load();
    
    worker_init();
    search_init();
    
    /* Open file if provided */
    if (argc >= 2) {
        if (!autosave_open_with_recovery(argv[1])) {
            editor_open(argv[1]);
        }
    } else {
        editor_new();
    }
    
    /* Main loop */
    time_t last_autosave_check = time(NULL);
    
    while (1) {
        render_refresh_screen();
        process_keypress();
        
        /* Process worker results */
        worker_process_results();
        
        /* Check autosave */
        time_t now = time(NULL);
        if (now - last_autosave_check >= 5) {
            autosave_check();
            last_autosave_check = now;
        }
    }
    
    /* Cleanup (not normally reached) */
    search_cleanup();
    worker_shutdown();
    themes_free();
    editor_cleanup();
    input_disable_mouse();
    terminal_disable_raw_mode();
    
    return 0;
}
```

### Makefile

```makefile
# Makefile for edit text editor

CC = cc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2
LDFLAGS = -lm -pthread

# Debug build
ifdef DEBUG
CFLAGS += -g -DDEBUG -DLOG_LEVEL=LOG_DEBUG
endif

# Source files
SRCS = main.c \
       editor.c \
       buffer.c \
       syntax.c \
       theme.c \
       render.c \
       input.c \
       terminal.c \
       undo.c \
       clipboard.c \
       search.c \
       worker.c \
       autosave.c \
       dialog.c \
       error.c

OBJS = $(SRCS:.c=.o)
DEPS = edit.h types.h error.h terminal.h theme.h buffer.h syntax.h \
       undo.h input.h render.h worker.h search.h autosave.h clipboard.h \
       dialog.h editor.h

TARGET = edit

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean
clean:
	rm -f $(TARGET) $(OBJS)

# Install
install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Format check
check:
	@echo "Checking code..."
	@$(CC) $(CFLAGS) -fsyntax-only $(SRCS)

# Line counts
stats:
	@echo "Line counts:"
	@wc -l *.c *.h | sort -n

.PHONY: all clean install uninstall check stats
```

---

## Final Project Structure

After completing all phases:

```
edit/
├── Makefile
├── edit.h              # Master header
├── types.h             # Shared types
├── error.h             # Error codes and macros
├── error.c             # Error string function
├── terminal.h          # Terminal handling
├── terminal.c
├── theme.h             # Theme system
├── theme.c
├── buffer.h            # Buffer/line operations
├── buffer.c
├── syntax.h            # Syntax highlighting
├── syntax.c
├── undo.h              # Undo/redo
├── undo.c
├── input.h             # Input handling
├── input.c
├── render.h            # Screen rendering
├── render.c
├── worker.h            # Background threads
├── worker.c
├── search.h            # Search/replace
├── search.c
├── autosave.h          # Auto-save
├── autosave.c
├── clipboard.h         # Clipboard
├── clipboard.c
├── dialog.h            # Dialogs
├── dialog.c
├── editor.h            # Editor state
├── editor.c
├── main.c              # Entry point
└── third_party/
    └── utflite/
```

---

## Tips for Implementation

### Handling Global State

The original `edit.c` has many global variables. Options:

1. **Accessor functions:** Create `module_get_state()` functions
2. **Pass as parameters:** Modify functions to take explicit parameters
3. **Extern declarations:** Declare globals in headers (less clean)

Recommendation: Use accessor functions for encapsulation.

### Handling Dependencies

When module A needs functionality from module B:

1. Include B's header in A's source file
2. Ensure B is listed before A in Makefile SRCS (for link order)
3. Add B.h to A's dependencies

### Testing Each Phase

After each phase:

```bash
make clean
make
./edit test.txt
```

Test key functionality:
- Opening/saving files
- Editing text
- Search/replace
- Theme switching
- Undo/redo

### Common Issues

1. **Missing includes:** Add required headers
2. **Undefined references:** Check link order in Makefile
3. **Duplicate definitions:** Ensure structs are only in types.h
4. **Static function access:** Either make public or move together

---

## Version Updates

After completing all phases, update version to `1.0.0`:

```c
/* In types.h */
#define EDIT_VERSION "1.0.0"
```

---

## Summary

| Phase | Module | Dependencies |
|-------|--------|--------------|
| 25B | terminal | types |
| 25C | theme | types |
| 25D | buffer | types, error |
| 25E | syntax | buffer |
| 25F | undo | buffer |
| 25G | input | terminal |
| 25H | render | buffer, theme, syntax |
| 25I | worker | buffer, error |
| 25J | search | buffer, worker |
| 25K | autosave | buffer, worker |
| 25L | dialog | theme, input, render |
| 25M | clipboard | buffer |
| 25N | editor | all above |
| 25O | main | all above |

Start with leaf modules (25B, 25C) and work toward the center (25N, 25O).

**Good luck with the modularization!**
