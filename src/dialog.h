/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

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
 * Dialog Rendering Helpers
 *****************************************************************************/

/*
 * Move cursor to dialog row and column (1-based terminal coordinates).
 */
void dialog_goto(struct output_buffer *output, int row, int column);

/*
 * Set full style (fg, bg, attributes) for dialog output.
 */
void dialog_set_style(struct output_buffer *output, const struct style *style);

/*
 * Draw dialog header with title centered.
 */
void dialog_draw_header(struct output_buffer *output,
                        struct dialog_state *dialog,
                        const char *title);

/*
 * Draw dialog footer with hint text.
 */
void dialog_draw_footer(struct output_buffer *output,
                        struct dialog_state *dialog,
                        const char *hint);

/*****************************************************************************
 * Dialog State Management
 *****************************************************************************/

/*
 * Calculate dialog panel dimensions based on screen size.
 * Panel is centered, 50% height, 70% width, with minimum sizes.
 */
void dialog_calculate_dimensions(struct dialog_state *dialog);

/*
 * Ensure the selected item is visible by adjusting scroll offset.
 */
void dialog_ensure_visible(struct dialog_state *dialog);

/*
 * Clamp selection index to valid range and ensure visibility.
 */
void dialog_clamp_selection(struct dialog_state *dialog);

/*
 * Close the dialog and restore normal editor state.
 */
void dialog_close(struct dialog_state *dialog);

/*****************************************************************************
 * Dialog Input Handling
 *****************************************************************************/

/*
 * Handle keyboard input for dialog navigation.
 * Returns the action to take.
 */
enum dialog_result dialog_handle_key(struct dialog_state *dialog, int key);

/*
 * Handle mouse input for dialog interaction.
 * Returns the action to take.
 */
enum dialog_result dialog_handle_mouse(struct dialog_state *dialog,
                                       struct mouse_input *mouse);

/*****************************************************************************
 * Dialog Drawing Helpers
 *****************************************************************************/

/*
 * Draw a single list item in the dialog.
 */
void dialog_draw_list_item(struct output_buffer *output,
                           struct dialog_state *dialog,
                           int row_index,
                           const char *text,
                           bool is_selected,
                           bool is_directory);

/*
 * Draw an empty row in the dialog content area.
 */
void dialog_draw_empty_row(struct output_buffer *output,
                           struct dialog_state *dialog,
                           int row_index);

/*
 * Set foreground color for dialog output.
 */
void dialog_set_fg(struct output_buffer *output, struct syntax_color color);

/*
 * Set background color for dialog output.
 */
void dialog_set_bg(struct output_buffer *output, struct syntax_color color);

/*****************************************************************************
 * File List Utilities
 *****************************************************************************/

/*
 * Free a single file list item and its allocated strings.
 */
void file_list_item_free(struct file_list_item *item);

/*
 * Free an array of file list items.
 */
void file_list_free(struct file_list_item *items, int count);

/*
 * Read directory contents and return sorted file list.
 * Caller must free the returned array with file_list_free().
 * Returns NULL on error and sets count to 0.
 */
struct file_list_item *file_list_read_directory(const char *path, int *count);

/*****************************************************************************
 * Path Utilities
 *****************************************************************************/

/*
 * Get the parent directory of a path.
 * Returns newly allocated string, caller must free.
 * Returns "/" for root path or paths without parent.
 */
char *path_get_parent(const char *path);

/*
 * Join a directory path and filename.
 * Returns newly allocated string, caller must free.
 */
char *path_join(const char *directory, const char *filename);

/*****************************************************************************
 * Open File Dialog
 *****************************************************************************/

/*
 * Show the Open File dialog.
 * Returns allocated file path if user selected a file, NULL if cancelled.
 * Caller must free the returned path.
 */
char *open_file_dialog(void);

/*****************************************************************************
 * Theme Picker Dialog
 *****************************************************************************/

/*
 * Show the Theme Picker dialog with live preview.
 * Returns selected theme index, or -1 if cancelled.
 */
int theme_picker_dialog(void);

/*****************************************************************************
 * Help Dialog
 *****************************************************************************/

/*
 * Show the Help dialog with all keyboard shortcuts.
 */
void help_dialog(void);

#endif /* EDIT_DIALOG_H */
