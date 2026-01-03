/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * buffer.c - Buffer and line management implementation
 */

#define _GNU_SOURCE
#include <stdio.h>
#include "../lib/utflite-1.5.1/single_include/utflite.h"

#include "edit.h"
#include "buffer.h"

/*
 * Forward declarations for functions defined elsewhere.
 * These will be linked from edit.c (and later syntax.c).
 */
extern void neighbor_compute_line(struct line *line);
extern void syntax_highlight_line(struct line *line, struct buffer *buffer, uint32_t row);
extern void undo_history_free(struct undo_history *history);

/*****************************************************************************
 * Line Operations
 *****************************************************************************/

/*
 * Initialize a line as hot with empty cell array.
 * New lines start hot since they have no mmap backing.
 */
void line_init(struct line *line)
{
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
	line->mmap_offset = 0;
	line->mmap_length = 0;
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;
	line->wrap_cache_mode = WRAP_NONE;
}

/*
 * Free all memory associated with a line and reset its fields.
 */
void line_free(struct line *line)
{
	free(line->cells);
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
	line->mmap_offset = 0;
	line->mmap_length = 0;
	line_set_temperature(line, LINE_TEMPERATURE_COLD);
	free(line->wrap_columns);
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;
	line->wrap_cache_mode = WRAP_NONE;
}

/*
 * Invalidate the wrap cache for a single line.
 * Called when line content changes or when wrap settings change.
 */
void line_invalidate_wrap_cache(struct line *line)
{
	free(line->wrap_columns);
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;
	line->wrap_cache_mode = WRAP_NONE;
}

/*
 * Ensure line can hold at least 'required' cells.
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
int line_ensure_capacity_checked(struct line *line, uint32_t required)
{
	if (required <= line->cell_capacity)
		return 0;

	uint32_t new_capacity = line->cell_capacity ? line->cell_capacity * 2 : INITIAL_LINE_CAPACITY;
	while (new_capacity < required)
		new_capacity *= 2;

	void *new_cells = edit_realloc(line->cells, new_capacity * sizeof(struct cell));
	if (IS_ERR(new_cells))
		return (int)PTR_ERR(new_cells);

	line->cells = new_cells;
	line->cell_capacity = new_capacity;
	return 0;
}

/*
 * Ensure line can hold at least 'required' cells.
 * Crashes on allocation failure.
 */
void line_ensure_capacity(struct line *line, uint32_t required)
{
	int ret = line_ensure_capacity_checked(line, required);
	BUG_ON(ret);
}

/*
 * Insert a cell with the given codepoint at the specified position.
 * Shifts existing cells to the right. Position is clamped to cell_count.
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
int line_insert_cell_checked(struct line *line, uint32_t position, uint32_t codepoint)
{
	if (position > line->cell_count)
		position = line->cell_count;

	PROPAGATE(line_ensure_capacity_checked(line, line->cell_count + 1));

	if (position < line->cell_count) {
		memmove(&line->cells[position + 1], &line->cells[position],
		        (line->cell_count - position) * sizeof(struct cell));
	}

	line->cells[position] = (struct cell){.codepoint = codepoint};
	line->cell_count++;
	return 0;
}

/*
 * Insert a cell at position.
 * Crashes on allocation failure.
 */
void line_insert_cell(struct line *line, uint32_t position, uint32_t codepoint)
{
	int ret = line_insert_cell_checked(line, position, codepoint);
	BUG_ON(ret);
}

/*
 * Delete the cell at the specified position, shifting cells left.
 */
void line_delete_cell(struct line *line, uint32_t position)
{
	if (position >= line->cell_count) {
		return;
	}

	if (position < line->cell_count - 1) {
		memmove(&line->cells[position], &line->cells[position + 1],
		        (line->cell_count - position - 1) * sizeof(struct cell));
	}

	line->cell_count--;
}

/*
 * Append a cell to end of line.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_append_cell_checked(struct line *line, uint32_t codepoint)
{
	return line_insert_cell_checked(line, line->cell_count, codepoint);
}

/*
 * Append a cell to end of line.
 * Crashes on allocation failure.
 */
void line_append_cell(struct line *line, uint32_t codepoint)
{
	int ret = line_append_cell_checked(line, codepoint);
	BUG_ON(ret);
}

/*
 * Append all cells from src line to dest line.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_append_cells_from_line_checked(struct line *dest, struct line *src)
{
	for (uint32_t i = 0; i < src->cell_count; i++) {
		PROPAGATE(line_append_cell_checked(dest, src->cells[i].codepoint));
	}
	return 0;
}

/*
 * Append all cells from src line to dest line.
 * Crashes on allocation failure.
 */
void line_append_cells_from_line(struct line *dest, struct line *src)
{
	int ret = line_append_cells_from_line_checked(dest, src);
	BUG_ON(ret);
}

/*
 * Warm a cold line by decoding UTF-8 content from mmap into cells.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_warm_checked(struct line *line, struct buffer *buffer)
{
	if (line_get_temperature(line) != LINE_TEMPERATURE_COLD) {
		return 0;
	}

	const char *text = buffer->mmap_base + line->mmap_offset;
	size_t length = line->mmap_length;

	/* Decode UTF-8 to cells */
	size_t offset = 0;
	while (offset < length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, length - offset, &codepoint);
		PROPAGATE(line_append_cell_checked(line, codepoint));
		offset += bytes;
	}

	line_set_temperature(line, LINE_TEMPERATURE_WARM);

	/* Compute neighbor data for word boundaries */
	neighbor_compute_line(line);

	return 0;
}

/*
 * Warm a cold line by decoding UTF-8 to cells.
 * Crashes on allocation failure.
 */
void line_warm(struct line *line, struct buffer *buffer)
{
	int ret = line_warm_checked(line, buffer);
	BUG_ON(ret);
}

/*
 * Get cell count for a line.
 * For cold lines, counts codepoints without allocating cells.
 */
uint32_t line_get_cell_count(struct line *line, struct buffer *buffer)
{
	if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
		/* Count codepoints without allocating cells */
		const char *text = buffer->mmap_base + line->mmap_offset;
		size_t length = line->mmap_length;
		uint32_t count = 0;
		size_t offset = 0;

		while (offset < length) {
			uint32_t codepoint;
			int bytes = utflite_decode(text + offset, length - offset, &codepoint);
			count++;
			offset += bytes;
		}

		return count;
	}

	return line->cell_count;
}

/*****************************************************************************
 * Grapheme Boundary Functions
 *****************************************************************************/

/*
 * Maximum codepoints to encode when finding grapheme boundaries.
 * Covers the longest possible grapheme cluster (complex emoji sequences).
 */
#define GRAPHEME_LOOKAHEAD 32

/*
 * Move cursor left to previous grapheme cluster using UAX #29 rules.
 * Handles emoji sequences, combining marks, flags, and Hangul.
 */
uint32_t cursor_prev_grapheme(struct line *line, struct buffer *buffer, uint32_t column)
{
	line_warm(line, buffer);

	if (column == 0 || line->cell_count == 0) {
		return 0;
	}

	/*
	 * Encode cells from start (or limited lookback) to current position
	 * into UTF-8 buffer for grapheme boundary detection.
	 */
	char utf8_buf[512];
	int byte_len = 0;
	uint32_t lookback = (column < 128) ? column : 128;
	uint32_t start_column = column - lookback;

	for (uint32_t i = start_column; i < column; i++) {
		int bytes = utflite_encode(line->cells[i].codepoint, utf8_buf + byte_len);
		byte_len += bytes;
	}

	/* Find previous grapheme boundary in UTF-8 */
	int prev_byte = utflite_prev_grapheme(utf8_buf, byte_len);

	/* Count codepoints from boundary to end of buffer */
	int offset = prev_byte;
	int codepoints_after = 0;
	while (offset < byte_len) {
		uint32_t cp;
		offset += utflite_decode(utf8_buf + offset, byte_len - offset, &cp);
		codepoints_after++;
	}

	return column - codepoints_after;
}

/*
 * Move cursor right to next grapheme cluster using UAX #29 rules.
 * Handles emoji sequences, combining marks, flags, and Hangul.
 */
uint32_t cursor_next_grapheme(struct line *line, struct buffer *buffer, uint32_t column)
{
	line_warm(line, buffer);

	if (column >= line->cell_count) {
		return line->cell_count;
	}

	/*
	 * Encode cells from current position into UTF-8 buffer
	 * for grapheme boundary detection.
	 */
	char utf8_buf[128];
	int byte_len = 0;
	int codepoints_encoded = 0;

	for (uint32_t i = column; i < line->cell_count && codepoints_encoded < GRAPHEME_LOOKAHEAD; i++) {
		int bytes = utflite_encode(line->cells[i].codepoint, utf8_buf + byte_len);
		byte_len += bytes;
		codepoints_encoded++;
	}

	/* Find next grapheme boundary in UTF-8 */
	int next_byte = utflite_next_grapheme(utf8_buf, byte_len, 0);

	/* Count codepoints consumed to reach that boundary */
	int offset = 0;
	int codepoints_in_grapheme = 0;
	while (offset < next_byte) {
		uint32_t cp;
		offset += utflite_decode(utf8_buf + offset, byte_len - offset, &cp);
		codepoints_in_grapheme++;
	}

	return column + codepoints_in_grapheme;
}

/*****************************************************************************
 * Buffer Operations
 *****************************************************************************/

/*
 * Initialize a buffer with starting capacity and no file.
 */
void buffer_init(struct buffer *buffer)
{
	buffer->lines = malloc(INITIAL_BUFFER_CAPACITY * sizeof(struct line));
	buffer->line_count = 0;
	buffer->line_capacity = INITIAL_BUFFER_CAPACITY;
	buffer->filename = NULL;
	buffer->is_modified = false;
	buffer->file_descriptor = -1;
	buffer->mmap_base = NULL;
	buffer->mmap_size = 0;
	buffer->next_pair_id = 1;

	/* Initialize undo history */
	buffer->undo_history.groups = NULL;
	buffer->undo_history.group_count = 0;
	buffer->undo_history.group_capacity = 0;
	buffer->undo_history.current_index = 0;
	buffer->undo_history.recording = false;
	buffer->undo_history.last_edit_time.tv_sec = 0;
	buffer->undo_history.last_edit_time.tv_nsec = 0;

	/* Start with one empty line so cursor has somewhere to be */
	line_init(&buffer->lines[0]);
	buffer->line_count = 1;
}

/*
 * Free all buffer resources including unmapping memory-mapped file.
 */
void buffer_free(struct buffer *buffer)
{
	/* Free undo history */
	undo_history_free(&buffer->undo_history);

	for (uint32_t index = 0; index < buffer->line_count; index++) {
		line_free(&buffer->lines[index]);
	}
	free(buffer->lines);
	free(buffer->filename);

	/* Unmap file if mapped */
	if (buffer->mmap_base != NULL) {
		munmap(buffer->mmap_base, buffer->mmap_size);
	}
	if (buffer->file_descriptor >= 0) {
		close(buffer->file_descriptor);
	}

	buffer->lines = NULL;
	buffer->filename = NULL;
	buffer->line_count = 0;
	buffer->line_capacity = 0;
	buffer->file_descriptor = -1;
	buffer->mmap_base = NULL;
	buffer->mmap_size = 0;
}

/*
 * Invalidate wrap caches for all lines in the buffer.
 * Called when terminal is resized or wrap mode changes.
 */
void buffer_invalidate_all_wrap_caches(struct buffer *buffer)
{
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		line_invalidate_wrap_cache(&buffer->lines[row]);
	}
}

/*
 * Ensure buffer can hold at least 'required' lines.
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
int buffer_ensure_capacity_checked(struct buffer *buffer, uint32_t required)
{
	if (required <= buffer->line_capacity)
		return 0;

	uint32_t new_capacity = buffer->line_capacity ? buffer->line_capacity * 2 : INITIAL_BUFFER_CAPACITY;
	while (new_capacity < required)
		new_capacity *= 2;

	void *new_lines = edit_realloc(buffer->lines, new_capacity * sizeof(struct line));
	if (IS_ERR(new_lines))
		return (int)PTR_ERR(new_lines);

	buffer->lines = new_lines;
	buffer->line_capacity = new_capacity;
	return 0;
}

/*
 * Ensure buffer can hold at least 'required' lines.
 * Crashes on allocation failure.
 */
void buffer_ensure_capacity(struct buffer *buffer, uint32_t required)
{
	int ret = buffer_ensure_capacity_checked(buffer, required);
	BUG_ON(ret);
}

/*
 * Insert an empty line at position.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_insert_empty_line_checked(struct buffer *buffer, uint32_t position)
{
	if (position > buffer->line_count)
		position = buffer->line_count;

	PROPAGATE(buffer_ensure_capacity_checked(buffer, buffer->line_count + 1));

	if (position < buffer->line_count) {
		memmove(&buffer->lines[position + 1], &buffer->lines[position],
		        (buffer->line_count - position) * sizeof(struct line));
	}

	line_init(&buffer->lines[position]);
	buffer->line_count++;
	buffer->is_modified = true;
	return 0;
}

/*
 * Insert an empty line at position.
 * Crashes on allocation failure.
 */
void buffer_insert_empty_line(struct buffer *buffer, uint32_t position)
{
	int ret = buffer_insert_empty_line_checked(buffer, position);
	BUG_ON(ret);
}

/*
 * Delete line at position.
 */
void buffer_delete_line(struct buffer *buffer, uint32_t position)
{
	if (position >= buffer->line_count) {
		return;
	}

	line_free(&buffer->lines[position]);

	if (position < buffer->line_count - 1) {
		memmove(&buffer->lines[position], &buffer->lines[position + 1],
		        (buffer->line_count - position - 1) * sizeof(struct line));
	}

	buffer->line_count--;
	buffer->is_modified = true;
}

/*
 * Insert a cell at row/column.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_insert_cell_at_column_checked(struct buffer *buffer, uint32_t row,
                                         uint32_t column, uint32_t codepoint)
{
	if (row > buffer->line_count)
		row = buffer->line_count;

	if (row == buffer->line_count) {
		PROPAGATE(buffer_insert_empty_line_checked(buffer, buffer->line_count));
	}

	struct line *line = &buffer->lines[row];
	PROPAGATE(line_warm_checked(line, buffer));
	PROPAGATE(line_insert_cell_checked(line, column, codepoint));
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer->is_modified = true;

	/* Recompute neighbors for this line */
	neighbor_compute_line(line);

	/* Re-highlight the modified line */
	syntax_highlight_line(line, buffer, row);

	/* Invalidate wrap cache since line content changed. */
	line_invalidate_wrap_cache(line);
	return 0;
}

/*
 * Insert a cell at row/column.
 * Crashes on allocation failure.
 */
void buffer_insert_cell_at_column(struct buffer *buffer, uint32_t row,
                                  uint32_t column, uint32_t codepoint)
{
	int ret = buffer_insert_cell_at_column_checked(buffer, row, column, codepoint);
	BUG_ON(ret);
}

/*
 * Delete grapheme at row/column.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_delete_grapheme_at_column_checked(struct buffer *buffer,
                                             uint32_t row, uint32_t column)
{
	if (row >= buffer->line_count)
		return 0;

	struct line *line = &buffer->lines[row];
	PROPAGATE(line_warm_checked(line, buffer));

	if (column < line->cell_count) {
		/* Find the end of this grapheme (skip over combining marks) */
		uint32_t end = cursor_next_grapheme(line, buffer, column);
		/* Delete cells from end backwards */
		for (uint32_t i = end; i > column; i--) {
			line_delete_cell(line, column);
		}
		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		buffer->is_modified = true;
		/* Recompute neighbors and re-highlight */
		neighbor_compute_line(line);
		syntax_highlight_line(line, buffer, row);
		/* Invalidate wrap cache since line content changed. */
		line_invalidate_wrap_cache(line);
	} else if (row + 1 < buffer->line_count) {
		/* Join with next line */
		struct line *next_line = &buffer->lines[row + 1];
		PROPAGATE(line_warm_checked(next_line, buffer));
		PROPAGATE(line_append_cells_from_line_checked(line, next_line));
		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		buffer_delete_line(buffer, row + 1);
		/* Recompute neighbors and re-highlight */
		neighbor_compute_line(line);
		syntax_highlight_line(line, buffer, row);
		/* Invalidate wrap cache since line content changed. */
		line_invalidate_wrap_cache(line);
	}
	return 0;
}

/*
 * Delete grapheme at row/column.
 * Crashes on allocation failure.
 */
void buffer_delete_grapheme_at_column(struct buffer *buffer, uint32_t row, uint32_t column)
{
	int ret = buffer_delete_grapheme_at_column_checked(buffer, row, column);
	BUG_ON(ret);
}

/*
 * Insert newline at row/column.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_insert_newline_checked(struct buffer *buffer, uint32_t row, uint32_t column)
{
	if (row > buffer->line_count)
		return 0;

	if (row == buffer->line_count)
		return buffer_insert_empty_line_checked(buffer, buffer->line_count);

	struct line *line = &buffer->lines[row];
	PROPAGATE(line_warm_checked(line, buffer));

	if (column >= line->cell_count) {
		return buffer_insert_empty_line_checked(buffer, row + 1);
	}

	/* Insert new line and copy cells after cursor */
	PROPAGATE(buffer_insert_empty_line_checked(buffer, row + 1));
	struct line *new_line = &buffer->lines[row + 1];

	for (uint32_t i = column; i < line->cell_count; i++) {
		PROPAGATE(line_append_cell_checked(new_line, line->cells[i].codepoint));
	}

	/* Truncate original line */
	line->cell_count = column;
	line_set_temperature(line, LINE_TEMPERATURE_HOT);

	/* Recompute neighbors and re-highlight both lines */
	neighbor_compute_line(line);
	neighbor_compute_line(new_line);
	syntax_highlight_line(line, buffer, row);
	syntax_highlight_line(new_line, buffer, row + 1);

	/* Invalidate wrap cache for truncated line. */
	line_invalidate_wrap_cache(line);
	return 0;
}

/*
 * Insert newline at row/column.
 * Crashes on allocation failure.
 */
void buffer_insert_newline(struct buffer *buffer, uint32_t row, uint32_t column)
{
	int ret = buffer_insert_newline_checked(buffer, row, column);
	BUG_ON(ret);
}

/*
 * Swap two lines in the buffer. Does not record undo.
 */
void buffer_swap_lines(struct buffer *buffer, uint32_t row1, uint32_t row2)
{
	if (row1 >= buffer->line_count || row2 >= buffer->line_count) {
		return;
	}
	struct line temp = buffer->lines[row1];
	buffer->lines[row1] = buffer->lines[row2];
	buffer->lines[row2] = temp;
}
/*
 * Load buffer content from a memory block (for stdin pipe input).
 * Content is parsed into HOT lines (fully in-memory, no mmap backing).
 * The caller retains ownership of the content pointer.
 *
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
int buffer_load_from_memory(struct buffer *buffer, const char *content, size_t size)
{
	buffer_init(buffer);
	if (size == 0) {
		/* Empty input - create single empty line */
		int ret = buffer_ensure_capacity_checked(buffer, 1);
		if (ret)
			return ret;
		line_init(&buffer->lines[0]);
		buffer->line_count = 1;
		return 0;
	}
	/* Count lines (number of '\n' characters, plus 1 for last line) */
	size_t line_count = 1;
	for (size_t i = 0; i < size; i++) {
		if (content[i] == '\n')
			line_count++;
	}
	/* Handle trailing newline - don't create empty line after it */
	if (size > 0 && content[size - 1] == '\n')
		line_count--;
	if (line_count == 0)
		line_count = 1;
	int ret = buffer_ensure_capacity_checked(buffer, line_count);
	if (ret)
		return ret;
	/* Parse lines and decode UTF-8 into cells */
	const char *line_start = content;
	uint32_t line_index = 0;
	for (size_t i = 0; i <= size; i++) {
		bool is_end = (i == size);
		bool is_newline = (i < size && content[i] == '\n');
		if (is_end || is_newline) {
			size_t line_length = (content + i) - line_start;
			/* Skip final empty line after trailing newline */
			if (is_end && line_length == 0 && line_index > 0)
				break;
			struct line *line = &buffer->lines[line_index];
			line_init(line);
			/* Decode UTF-8 content into cells */
			size_t offset = 0;
			while (offset < line_length) {
				uint32_t codepoint;
				int bytes = utflite_decode(line_start + offset,
				                           line_length - offset,
				                           &codepoint);
				ret = line_append_cell_checked(line, codepoint);
				if (ret) {
					/* Cleanup on failure */
					for (uint32_t j = 0; j <= line_index; j++)
						line_free(&buffer->lines[j]);
					buffer->line_count = 0;
					return ret;
				}
				offset += bytes;
			}
			/* Mark as HOT (no mmap backing) */
			line_set_temperature(line, LINE_TEMPERATURE_HOT);
			/* Compute neighbor data for word boundaries */
			neighbor_compute_line(line);
			line_start = content + i + 1;
			line_index++;
		}
	}
	buffer->line_count = line_index;
	buffer->file_descriptor = -1;
	buffer->mmap_base = NULL;
	buffer->mmap_size = 0;
	return 0;
}
