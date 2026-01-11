/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

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
 * Initialize an undo history structure.
 */
void undo_history_init(struct undo_history *history);

/*
 * Free all undo history resources.
 */
void undo_history_free(struct undo_history *history);

/*****************************************************************************
 * Undo Groups
 *****************************************************************************/

/*
 * Begin a new undo group. If within timeout of the last edit,
 * continues the existing group. Called before making edits.
 *
 * cursor_row/cursor_col: current cursor position (for restore on undo)
 */
void undo_begin_group(struct buffer *buffer, uint32_t cursor_row, uint32_t cursor_col);

/*
 * End the current undo group. Records final cursor position.
 *
 * cursor_row/cursor_col: current cursor position (for restore on redo)
 */
void undo_end_group(struct buffer *buffer, uint32_t cursor_row, uint32_t cursor_col);

/*****************************************************************************
 * Operation Recording
 *****************************************************************************/

/*
 * Record insertion of a single character.
 */
void undo_record_insert_char(struct buffer *buffer, uint32_t row,
                             uint32_t column, uint32_t codepoint);

/*
 * Record deletion of a single character.
 */
void undo_record_delete_char(struct buffer *buffer, uint32_t row,
                             uint32_t column, uint32_t codepoint);

/*
 * Record insertion of a newline.
 */
void undo_record_insert_newline(struct buffer *buffer, uint32_t row,
                                uint32_t column);

/*
 * Record deletion of a newline (line join).
 */
void undo_record_delete_newline(struct buffer *buffer, uint32_t row,
                                uint32_t column);

/*
 * Record deletion of multiple characters (selection delete).
 */
void undo_record_delete_text(struct buffer *buffer,
                             uint32_t start_row, uint32_t start_col,
                             uint32_t end_row, uint32_t end_col,
                             const char *text, size_t text_length);

/*****************************************************************************
 * Undo/Redo Execution
 *****************************************************************************/

/*
 * Perform undo on the most recent group.
 * Returns true if something was undone, false if nothing to undo.
 *
 * cursor_row/cursor_col: outputs for restored cursor position
 */
bool undo_perform(struct buffer *buffer, uint32_t *cursor_row, uint32_t *cursor_col);

/*
 * Perform redo on the most recently undone group.
 * Returns true if something was redone, false if nothing to redo.
 *
 * cursor_row/cursor_col: outputs for restored cursor position
 */
bool redo_perform(struct buffer *buffer, uint32_t *cursor_row, uint32_t *cursor_col);

/*
 * Check if undo is available.
 */
bool undo_can_undo(struct undo_history *history);

/*
 * Check if redo is available.
 */
bool undo_can_redo(struct undo_history *history);

/*****************************************************************************
 * No-Record Buffer Operations (for undo/redo and batch operations)
 *****************************************************************************/

/*
 * Delete a range of text without recording to undo history.
 * Used during undo/redo operations and batch replacements.
 */
void buffer_delete_range_no_record(struct buffer *buffer,
                                   uint32_t start_row, uint32_t start_col,
                                   uint32_t end_row, uint32_t end_col);

#endif /* EDIT_UNDO_H */
