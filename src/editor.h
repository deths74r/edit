/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * editor.h - Core editor state and operations for edit
 *
 * Provides initialization, status messages, screen management,
 * selection handling, multi-cursor support, and dialogs.
 */

#ifndef EDIT_EDITOR_H
#define EDIT_EDITOR_H

#include "types.h"

/*****************************************************************************
 * Editor Lifecycle
 *****************************************************************************/

/*
 * Initialize the editor state.
 * Sets up buffer, loads themes, initializes worker thread.
 */
void editor_init(void);

/*
 * Perform clean exit: clear screen, remove swap file, free resources.
 */
void editor_perform_exit(void);

/*****************************************************************************
 * Buffer Context Management
 *****************************************************************************/
/*
 * Create a new editor context (buffer slot).
 * Returns the index of the new context, or -1 if MAX_CONTEXTS reached.
 */
int editor_context_new(void);
/*
 * Close the context at the given index.
 * Prompts to save if modified. Returns false if user cancelled.
 */
bool editor_context_close(uint32_t index);
/*
 * Safely get the active buffer with bounds checking.
 * Returns NULL if no valid buffer exists.
 */
struct buffer *editor_get_active_buffer(void);
/*
 * Switch to the context at the given index.
 */
void editor_context_switch(uint32_t index);
/*
 * Switch to the previous context (wraps around).
 */
void editor_context_prev(void);
/*
 * Switch to the next context (wraps around).
 */
void editor_context_next(void);
/*****************************************************************************
 * Status Messages
 *****************************************************************************/

/*
 * Set a formatted status message to display at the bottom of the screen.
 * Uses printf-style formatting.
 */
void editor_set_status_message(const char *format, ...);

/*****************************************************************************
 * Screen and Viewport Management
 *****************************************************************************/

/*
 * Update gutter width based on line count.
 */
void editor_update_gutter_width(void);

/*
 * Update screen dimensions from terminal.
 */
void editor_update_screen_size(void);

/*
 * Get the text area width (screen width minus gutter).
 */
uint16_t editor_get_text_width(void);

/*****************************************************************************
 * Color Column
 *****************************************************************************/

/*
 * Get the UTF-8 string for a color column style.
 */
const char *color_column_char(enum color_column_style style);

/*
 * Get human-readable name for color column style.
 */
const char *color_column_style_name(enum color_column_style style);

/*
 * Cycle to the next color column style.
 */
void editor_cycle_color_column_style(void);

/*****************************************************************************
 * Selection
 *****************************************************************************/

/*
 * Start a new selection at the current cursor position.
 */
void selection_start(void);

/*
 * Clear the current selection.
 */
void selection_clear(void);

/*
 * Check if the current selection is empty.
 */
bool selection_is_empty(void);

/*
 * Get the normalized selection range (start <= end).
 */
void selection_get_range(uint32_t *start_row, uint32_t *start_col,
                         uint32_t *end_row, uint32_t *end_col);

/*
 * Extract the currently selected text as a UTF-8 string.
 * Returns a newly allocated string that the caller must free.
 */
char *selection_get_text(size_t *out_length);

/*
 * Delete the currently selected text.
 */
void editor_delete_selection(void);

/*****************************************************************************
 * Multi-Cursor
 *****************************************************************************/

/*
 * Exit multi-cursor mode, keeping only primary cursor.
 */
void editor_multi_cursor_exit(void);

/*
 * Check if we have multiple cursors.
 */
bool editor_has_multi_cursor(void);

/*
 * Sort and merge overlapping cursors.
 */
void editor_cursors_sort_and_merge(void);

/*
 * Add a cursor at position.
 */
void editor_add_cursor(uint32_t row, uint32_t col);

/*****************************************************************************
 * Undo/Redo
 *****************************************************************************/

/*
 * Undo the most recent operation.
 */
void editor_undo(void);

/*
 * Redo the most recently undone operation.
 */
void editor_redo(void);

/*****************************************************************************
 * Go-To-Line Dialog
 *****************************************************************************/

/*
 * Enter go-to-line mode.
 */
void goto_line_enter(void);

/*
 * Handle input in go-to-line mode.
 * Returns true if key was handled.
 */
bool goto_handle_key(int key);

/*
 * Check if go-to-line mode is active.
 */
bool goto_line_is_active(void);

/*
 * Get the current go-to-line input string.
 */
const char *goto_line_get_input(void);

/*****************************************************************************
 * Save As Dialog
 *****************************************************************************/

/*
 * Enter save-as mode.
 */
void save_as_enter(void);

/*
 * Handle input in save-as mode.
 * Returns true if key was handled.
 */
bool save_as_handle_key(int key);

/*
 * Check if save-as mode is active.
 */
bool save_as_is_active(void);

/*
 * Check if save-as is prompting for overwrite confirmation.
 */
bool save_as_is_confirm_overwrite(void);

/*
 * Get the current path in save-as dialog.
 */
const char *save_as_get_path(void);

/*****************************************************************************
 * Quit Prompt
 *****************************************************************************/

/*
 * Enter quit prompt mode.
 */
void quit_prompt_enter(void);

/*
 * Handle input in quit prompt mode.
 * Returns true if key was handled.
 */
bool quit_prompt_handle_key(int key);

/*****************************************************************************
 * Reload Prompt
 *****************************************************************************/

/*
 * Enter reload prompt mode when file changes externally.
 */
void reload_prompt_enter(void);

/*
 * Handle input in reload prompt mode.
 * Returns true if key was handled.
 */
bool reload_prompt_handle_key(int key);

/*
 * Check if reload prompt is currently active.
 */
bool reload_prompt_is_active(void);

/*
 * Reload the current file from disk, preserving cursor position.
 */
void editor_reload_file(void);

/*****************************************************************************
 * External File Change Detection
 *****************************************************************************/

/*
 * Check if the file on disk has been modified since we loaded it.
 * Returns true if external changes detected.
 */
bool file_check_external_change(struct buffer *buffer);

/*
 * Open a file and load it into a buffer.
 * Returns 0 on success, negative error code on failure.
 */
int __must_check file_open(struct buffer *buffer, const char *filename);

/*
 * Check for updates and handle the full update flow.
 * Called when user presses Alt+U.
 */
void editor_check_for_updates(void);

#endif /* EDIT_EDITOR_H */
