/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * undo.c - Undo/redo system implementation
 *
 * Provides operation recording, grouping, and replay
 * for undo/redo functionality.
 */

#include "edit.h"
#include "undo.h"
#include "../third_party/utflite-1.5.2/single_include/utflite.h"

/*****************************************************************************
 * Undo History Management
 *****************************************************************************/

/* Initialize an undo history structure. */
void undo_history_init(struct undo_history *history)
{
	history->groups = NULL;
	history->group_count = 0;
	history->group_capacity = 0;
	history->current_index = 0;
	history->recording = false;
	history->last_edit_time.tv_sec = 0;
	history->last_edit_time.tv_nsec = 0;
}

/* Free all resources used by an undo history. */
void undo_history_free(struct undo_history *history)
{
	for (uint32_t i = 0; i < history->group_count; i++) {
		struct undo_group *group = &history->groups[i];
		for (uint32_t j = 0; j < group->operation_count; j++) {
			free(group->operations[j].text);
		}
		free(group->operations);
	}
	free(history->groups);
	undo_history_init(history);
}

/*****************************************************************************
 * Undo Groups
 *****************************************************************************/

/*
 * Begin a new undo group. If within timeout of the last edit,
 * continues the existing group. Called before making edits.
 */
void undo_begin_group(struct buffer *buffer, uint32_t cursor_row, uint32_t cursor_col)
{
	struct undo_history *history = &buffer->undo_history;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	double dt = (double)(now.tv_sec - history->last_edit_time.tv_sec) +
	            (double)(now.tv_nsec - history->last_edit_time.tv_nsec) / 1.0e9;

	if (history->recording) {
		/* Already recording - check if we should continue or start new group */
		if (dt < UNDO_GROUP_TIMEOUT) {
			/* Within timeout - continue current group */
			history->last_edit_time = now;
			return;
		}
		/* Timeout passed - end current group and start new one */
		undo_end_group(buffer, cursor_row, cursor_col);
	}

	/* Check if we should continue the previous group (auto-grouping) */
	/* This applies when recording was false but we're within timeout of last edit */
	if (dt < UNDO_GROUP_TIMEOUT && history->current_index > 0 &&
	    history->current_index == history->group_count) {
		/* Continue the previous group */
		history->recording = true;
		history->last_edit_time = now;
		return;
	}

	/* Truncate any redo history (we're making a new edit) */
	for (uint32_t i = history->current_index; i < history->group_count; i++) {
		struct undo_group *group = &history->groups[i];
		for (uint32_t j = 0; j < group->operation_count; j++) {
			free(group->operations[j].text);
		}
		free(group->operations);
	}
	history->group_count = history->current_index;

	/* Grow groups array if needed */
	if (history->group_count >= history->group_capacity) {
		uint32_t new_capacity = history->group_capacity == 0 ?
		                        INITIAL_UNDO_CAPACITY : history->group_capacity * 2;
		struct undo_group *new_groups = realloc(history->groups,
		                                        new_capacity * sizeof(struct undo_group));
		if (new_groups == NULL) {
			/* Undo recording disabled due to OOM */
			WARN_ON_ONCE(1);
			return;
		}
		history->groups = new_groups;
		history->group_capacity = new_capacity;
	}

	/* Initialize new group */
	struct undo_group *group = &history->groups[history->group_count];
	group->operations = NULL;
	group->operation_count = 0;
	group->operation_capacity = 0;
	group->cursor_row_before = cursor_row;
	group->cursor_column_before = cursor_col;
	group->cursor_row_after = cursor_row;
	group->cursor_column_after = cursor_col;

	history->group_count++;
	history->current_index = history->group_count;
	history->recording = true;
	history->last_edit_time = now;
}

/*
 * End the current undo group. Records final cursor position.
 */
void undo_end_group(struct buffer *buffer, uint32_t cursor_row, uint32_t cursor_col)
{
	struct undo_history *history = &buffer->undo_history;

	if (!history->recording || history->group_count == 0) {
		return;
	}

	struct undo_group *group = &history->groups[history->group_count - 1];
	group->cursor_row_after = cursor_row;
	group->cursor_column_after = cursor_col;

	/* If group is empty, remove it */
	if (group->operation_count == 0) {
		history->group_count--;
		history->current_index = history->group_count;
	}

	history->recording = false;
}

/*****************************************************************************
 * Operation Recording
 *****************************************************************************/

/*
 * Add an operation to the current undo group.
 */
static void undo_record_operation(struct buffer *buffer, struct edit_operation *op)
{
	struct undo_history *history = &buffer->undo_history;

	if (!history->recording || history->group_count == 0) {
		return;
	}

	struct undo_group *group = &history->groups[history->group_count - 1];

	/* Grow operations array if needed */
	if (group->operation_count >= group->operation_capacity) {
		uint32_t new_capacity = group->operation_capacity == 0 ?
		                        INITIAL_OPERATION_CAPACITY : group->operation_capacity * 2;
		struct edit_operation *new_ops = realloc(group->operations,
		                                         new_capacity * sizeof(struct edit_operation));
		if (new_ops == NULL) {
			/* Undo operation lost due to OOM */
			WARN_ON_ONCE(1);
			return;
		}
		group->operations = new_ops;
		group->operation_capacity = new_capacity;
	}

	group->operations[group->operation_count++] = *op;
}

/* Record insertion of a single character. */
void undo_record_insert_char(struct buffer *buffer, uint32_t row,
                             uint32_t column, uint32_t codepoint)
{
	struct edit_operation op = {
		.type = EDIT_OP_INSERT_CHAR,
		.row = row,
		.column = column,
		.codepoint = codepoint,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record deletion of a single character. */
void undo_record_delete_char(struct buffer *buffer, uint32_t row,
                             uint32_t column, uint32_t codepoint)
{
	struct edit_operation op = {
		.type = EDIT_OP_DELETE_CHAR,
		.row = row,
		.column = column,
		.codepoint = codepoint,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record insertion of a newline. */
void undo_record_insert_newline(struct buffer *buffer, uint32_t row,
                                uint32_t column)
{
	struct edit_operation op = {
		.type = EDIT_OP_INSERT_NEWLINE,
		.row = row,
		.column = column,
		.codepoint = 0,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record deletion of a newline (line join). */
void undo_record_delete_newline(struct buffer *buffer, uint32_t row,
                                uint32_t column)
{
	struct edit_operation op = {
		.type = EDIT_OP_DELETE_NEWLINE,
		.row = row,
		.column = column,
		.codepoint = 0,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record deletion of multiple characters (selection delete). */
void undo_record_delete_text(struct buffer *buffer,
                             uint32_t start_row, uint32_t start_col,
                             uint32_t end_row, uint32_t end_col,
                             const char *text, size_t text_length)
{
	struct edit_operation op = {
		.type = EDIT_OP_DELETE_TEXT,
		.row = start_row,
		.column = start_col,
		.codepoint = 0,
		.text = NULL,
		.text_length = text_length,
		.end_row = end_row,
		.end_column = end_col
	};
	if (text_length > 0) {
		op.text = malloc(text_length + 1);
		if (op.text != NULL) {
			memcpy(op.text, text, text_length);
			op.text[text_length] = '\0';
		}
	}
	undo_record_operation(buffer, &op);
}

/*****************************************************************************
 * No-Record Buffer Operations (for undo/redo)
 *****************************************************************************/

/*
 * Insert a cell at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_insert_cell_no_record(struct buffer *buffer, uint32_t row,
                                         uint32_t column, uint32_t codepoint)
{
	if (row > buffer->line_count) {
		row = buffer->line_count;
	}

	if (row == buffer->line_count) {
		buffer_insert_empty_line(buffer, buffer->line_count);
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);
	line_insert_cell(line, column, codepoint);
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer->is_modified = true;

	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Delete a cell at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_delete_cell_no_record(struct buffer *buffer, uint32_t row,
                                         uint32_t column)
{
	if (row >= buffer->line_count) {
		return;
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);

	if (column >= line->cell_count) {
		return;
	}

	line_delete_cell(line, column);
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer->is_modified = true;

	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Insert a newline at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_insert_newline_no_record(struct buffer *buffer, uint32_t row,
                                            uint32_t column)
{
	if (row > buffer->line_count) {
		return;
	}

	if (row == buffer->line_count) {
		buffer_insert_empty_line(buffer, buffer->line_count);
		return;
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);

	if (column >= line->cell_count) {
		buffer_insert_empty_line(buffer, row + 1);
	} else {
		buffer_insert_empty_line(buffer, row + 1);
		struct line *new_line = &buffer->lines[row + 1];

		for (uint32_t i = column; i < line->cell_count; i++) {
			line_append_cell(new_line, line->cells[i].codepoint);
		}

		line->cell_count = column;
		line_set_temperature(line, LINE_TEMPERATURE_HOT);

		neighbor_compute_line(line);
		neighbor_compute_line(new_line);
		syntax_highlight_line(line, buffer, row);
		syntax_highlight_line(new_line, buffer, row + 1);
		line_invalidate_wrap_cache(line);
	}

	buffer->is_modified = true;
}

/*
 * Join line with the next line without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_join_lines_no_record(struct buffer *buffer, uint32_t row)
{
	if (row >= buffer->line_count - 1) {
		return;
	}

	struct line *line = &buffer->lines[row];
	struct line *next_line = &buffer->lines[row + 1];

	line_warm(line, buffer);
	line_warm(next_line, buffer);

	/* Append all cells from next line to current line */
	for (uint32_t i = 0; i < next_line->cell_count; i++) {
		line_append_cell(line, next_line->cells[i].codepoint);
	}

	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer_delete_line(buffer, row + 1);
	buffer->is_modified = true;

	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Insert UTF-8 text at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_insert_text_no_record(struct buffer *buffer, uint32_t row,
                                         uint32_t column, const char *text,
                                         size_t text_length)
{
	size_t offset = 0;
	uint32_t cur_row = row;
	uint32_t cur_col = column;

	while (offset < text_length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, text_length - offset, &codepoint);

		if (bytes <= 0) {
			offset++;
			continue;
		}

		if (codepoint == '\n') {
			buffer_insert_newline_no_record(buffer, cur_row, cur_col);
			cur_row++;
			cur_col = 0;
		} else if (codepoint != '\r') {
			buffer_insert_cell_no_record(buffer, cur_row, cur_col, codepoint);
			cur_col++;
		}

		offset += bytes;
	}
}

/*
 * Delete a range of text without recording to undo history.
 * Used during undo/redo operations and batch replacements.
 */
void buffer_delete_range_no_record(struct buffer *buffer,
                                   uint32_t start_row, uint32_t start_col,
                                   uint32_t end_row, uint32_t end_col)
{
	if (start_row == end_row) {
		/* Single line deletion */
		struct line *line = &buffer->lines[start_row];
		line_warm(line, buffer);

		for (uint32_t i = end_col; i > start_col; i--) {
			line_delete_cell(line, start_col);
		}
		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(line);
		syntax_highlight_line(line, buffer, start_row);
	} else {
		/* Multi-line deletion */
		struct line *start_line = &buffer->lines[start_row];
		struct line *end_line = &buffer->lines[end_row];

		line_warm(start_line, buffer);
		line_warm(end_line, buffer);

		/* Truncate start line at start_col */
		start_line->cell_count = start_col;

		/* Append content after end_col from end line */
		for (uint32_t i = end_col; i < end_line->cell_count; i++) {
			line_append_cell(start_line, end_line->cells[i].codepoint);
		}

		/* Delete lines from start_row+1 to end_row inclusive */
		for (uint32_t i = end_row; i > start_row; i--) {
			buffer_delete_line(buffer, i);
		}

		line_set_temperature(start_line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(start_line);
		buffer_compute_pairs(buffer);

		/* Re-highlight affected lines */
		for (uint32_t r = start_row; r < buffer->line_count; r++) {
			if (buffer->lines[r].temperature != LINE_TEMPERATURE_COLD) {
				syntax_highlight_line(&buffer->lines[r], buffer, r);
			}
		}
	}

	buffer->is_modified = true;
}

/*****************************************************************************
 * Operation Reversal and Application
 *****************************************************************************/

/*
 * Reverse an operation (for undo). Does not record to undo history.
 */
static void undo_reverse_operation(struct buffer *buffer, struct edit_operation *op)
{
	switch (op->type) {
		case EDIT_OP_INSERT_CHAR:
			/* Undo insert = delete */
			buffer_delete_cell_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_DELETE_CHAR:
			/* Undo delete = insert */
			buffer_insert_cell_no_record(buffer, op->row, op->column, op->codepoint);
			break;

		case EDIT_OP_INSERT_NEWLINE:
			/* Undo newline = join lines */
			buffer_join_lines_no_record(buffer, op->row);
			break;

		case EDIT_OP_DELETE_NEWLINE:
			/* Undo join = split line */
			buffer_insert_newline_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_DELETE_TEXT:
			/* Undo delete = insert the saved text */
			buffer_insert_text_no_record(buffer, op->row, op->column,
			                             op->text, op->text_length);
			break;
	}
}

/*
 * Apply an operation (for redo). Does not record to undo history.
 */
static void undo_apply_operation(struct buffer *buffer, struct edit_operation *op)
{
	switch (op->type) {
		case EDIT_OP_INSERT_CHAR:
			/* Insert the character */
			buffer_insert_cell_no_record(buffer, op->row, op->column, op->codepoint);
			break;

		case EDIT_OP_DELETE_CHAR:
			/* Delete the character */
			buffer_delete_cell_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_INSERT_NEWLINE:
			/* Split the line */
			buffer_insert_newline_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_DELETE_NEWLINE:
			/* Join lines */
			buffer_join_lines_no_record(buffer, op->row);
			break;

		case EDIT_OP_DELETE_TEXT:
			/* Delete text range */
			buffer_delete_range_no_record(buffer, op->row, op->column,
			                              op->end_row, op->end_column);
			break;
	}
}

/*****************************************************************************
 * Undo/Redo Execution
 *****************************************************************************/

/*
 * Perform undo on the most recent group.
 * Returns true if something was undone, false if nothing to undo.
 */
bool undo_perform(struct buffer *buffer, uint32_t *cursor_row, uint32_t *cursor_col)
{
	struct undo_history *history = &buffer->undo_history;

	if (history->current_index == 0) {
		return false;
	}

	history->current_index--;
	struct undo_group *group = &history->groups[history->current_index];

	/* Reverse operations in reverse order */
	for (int i = (int)group->operation_count - 1; i >= 0; i--) {
		undo_reverse_operation(buffer, &group->operations[i]);
	}

	/* Return cursor position to restore */
	*cursor_row = group->cursor_row_before;
	*cursor_col = group->cursor_column_before;

	/* Recompute syntax highlighting */
	buffer_compute_pairs(buffer);
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		if (buffer->lines[row].temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(&buffer->lines[row], buffer, row);
		}
	}

	/* Update modified flag */
	buffer->is_modified = (history->current_index > 0);

	return true;
}

/*
 * Perform redo on the most recently undone group.
 * Returns true if something was redone, false if nothing to redo.
 */
bool redo_perform(struct buffer *buffer, uint32_t *cursor_row, uint32_t *cursor_col)
{
	struct undo_history *history = &buffer->undo_history;

	if (history->current_index >= history->group_count) {
		return false;
	}

	struct undo_group *group = &history->groups[history->current_index];
	history->current_index++;

	/* Apply operations in order */
	for (uint32_t i = 0; i < group->operation_count; i++) {
		undo_apply_operation(buffer, &group->operations[i]);
	}

	/* Return cursor position to restore */
	*cursor_row = group->cursor_row_after;
	*cursor_col = group->cursor_column_after;

	/* Recompute syntax highlighting */
	buffer_compute_pairs(buffer);
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		if (buffer->lines[row].temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(&buffer->lines[row], buffer, row);
		}
	}

	/* Mark as modified */
	buffer->is_modified = true;

	return true;
}

/*
 * Check if undo is available.
 */
bool undo_can_undo(struct undo_history *history)
{
	return history->current_index > 0;
}

/*
 * Check if redo is available.
 */
bool undo_can_redo(struct undo_history *history)
{
	return history->current_index < history->group_count;
}
