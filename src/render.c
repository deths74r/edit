/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * render.c - Screen rendering for edit
 *
 * Handles output buffering, screen refresh, status bar,
 * message bar, and soft-wrap rendering.
 */

#include <stdio.h>
#include "edit.h"
#include "syntax.h"
#include "../third_party/utflite-1.5.2/single_include/utflite.h"

/* Access to global editor state (defined in edit.c) */
extern struct editor_state editor;

/* Access to search state for highlighting (defined in edit.c) */
extern struct search_state search;

/*****************************************************************************
 * Output Buffer
 *****************************************************************************/

int output_buffer_init_checked(struct output_buffer *output)
{
	void *data = edit_malloc(INITIAL_OUTPUT_CAPACITY);
	if (IS_ERR(data))
		return (int)PTR_ERR(data);

	output->data = data;
	output->length = 0;
	output->capacity = INITIAL_OUTPUT_CAPACITY;
	return 0;
}

void output_buffer_init(struct output_buffer *output)
{
	int ret = output_buffer_init_checked(output);
	BUG_ON(ret);
}

int output_buffer_append_checked(struct output_buffer *output,
                                 const char *text, size_t length)
{
	if (output->length + length > output->capacity) {
		size_t new_capacity = output->capacity ? output->capacity * 2 : 256;
		while (new_capacity < output->length + length)
			new_capacity *= 2;

		void *new_data = edit_realloc(output->data, new_capacity);
		if (IS_ERR(new_data))
			return (int)PTR_ERR(new_data);

		output->data = new_data;
		output->capacity = new_capacity;
	}

	memcpy(output->data + output->length, text, length);
	output->length += length;
	return 0;
}

void output_buffer_append(struct output_buffer *output, const char *text, size_t length)
{
	int ret = output_buffer_append_checked(output, text, length);
	BUG_ON(ret);
}

void output_buffer_append_string(struct output_buffer *output, const char *text)
{
	output_buffer_append(output, text, strlen(text));
}

void output_buffer_append_char(struct output_buffer *output, char character)
{
	output_buffer_append(output, &character, 1);
}

void output_buffer_flush(struct output_buffer *output)
{
	write(STDOUT_FILENO, output->data, output->length);
	output->length = 0;
}

void output_buffer_free(struct output_buffer *output)
{
	free(output->data);
	output->data = NULL;
	output->length = 0;
	output->capacity = 0;
}

/*****************************************************************************
 * Soft Wrap
 *****************************************************************************/

uint32_t line_find_wrap_point(struct line *line, struct buffer *buffer,
                              uint32_t start_col,
                              uint32_t max_width, enum wrap_mode mode)
{
	if (mode == WRAP_NONE) {
		return line->cell_count;
	}

	/*
	 * Calculate visual width from start_col, iterating by grapheme
	 * to correctly handle multi-codepoint clusters like ZWJ sequences.
	 */
	uint32_t visual_width = 0;
	uint32_t col = start_col;

	while (col < line->cell_count) {
		uint32_t grapheme_end = cursor_next_grapheme(line, buffer, col);

		/* Calculate width of this grapheme */
		uint32_t cp = line->cells[col].codepoint;
		int width;

		if (cp == '\t') {
			width = editor.tab_width - ((visual_width) % editor.tab_width);
		} else {
			/* Find first non-zero-width codepoint in grapheme */
			width = 0;
			for (uint32_t j = col; j < grapheme_end && j < line->cell_count; j++) {
				width = utflite_codepoint_width(line->cells[j].codepoint);
				if (width > 0) break;
			}
			if (width <= 0) width = 1;
		}

		if (visual_width + (uint32_t)width > max_width) {
			break;
		}

		visual_width += (uint32_t)width;
		col = grapheme_end;
	}

	/* If we fit the whole line, no wrap needed */
	if (col >= line->cell_count) {
		return line->cell_count;
	}

	uint32_t hard_break = col;

	/* For character wrap, just break at the edge (grapheme boundary) */
	if (mode == WRAP_CHAR) {
		return (hard_break > start_col) ? hard_break : cursor_next_grapheme(line, buffer, start_col);
	}

	/* For word wrap, search backward for a good break point */
	uint32_t best_break = hard_break;
	bool found_break = false;

	for (uint32_t i = hard_break; i > start_col; i--) {
		uint8_t neighbor = line->cells[i - 1].neighbor;
		enum character_class cls = neighbor_get_class(neighbor);
		enum token_position pos = neighbor_get_position(neighbor);

		/* Best: break after whitespace */
		if (cls == CHAR_CLASS_WHITESPACE) {
			best_break = i;
			found_break = true;
			break;
		}

		/* Good: break after punctuation at end of token */
		if (cls == CHAR_CLASS_PUNCTUATION &&
		    (pos == TOKEN_POSITION_END || pos == TOKEN_POSITION_SOLO)) {
			best_break = i;
			found_break = true;
		}

		/* Acceptable: break at word boundary */
		if (!found_break && i < hard_break) {
			uint8_t next_neighbor = line->cells[i].neighbor;
			enum character_class next_cls = neighbor_get_class(next_neighbor);
			if (cls != next_cls && cls != CHAR_CLASS_WHITESPACE) {
				best_break = i;
				found_break = true;
			}
		}
	}

	/* Fall back to hard break if no good break found */
	if (!found_break || best_break <= start_col) {
		best_break = hard_break;
	}

	/* Safety: never return start_col (infinite loop) */
	if (best_break <= start_col) {
		best_break = cursor_next_grapheme(line, buffer, start_col);
	}

	return best_break;
}

void line_compute_wrap_points(struct line *line, struct buffer *buffer,
                              uint16_t text_width, enum wrap_mode mode)
{
	/*
	 * Check if cache is still valid. A cache hit requires matching
	 * width, mode, and the cache must have been computed at least once.
	 */
	if (line->wrap_cache_width == text_width &&
	    line->wrap_cache_mode == mode &&
	    line->wrap_segment_count > 0) {
		return;
	}

	/* Invalidate any stale cache data before recomputing. */
	line_invalidate_wrap_cache(line);

	/* For no-wrap mode, line is a single segment. */
	if (mode == WRAP_NONE || text_width == 0) {
		line->wrap_columns = malloc(sizeof(uint32_t));
		line->wrap_columns[0] = 0;
		line->wrap_segment_count = 1;
		line->wrap_cache_width = text_width;
		line->wrap_cache_mode = mode;
		return;
	}

	/* Ensure line is warm so we can access cells. */
	line_warm(line, buffer);

	/*
	 * First pass: count how many segments we need.
	 * Start with segment 0 at column 0, then find each wrap point.
	 */
	uint32_t segment_count = 1;
	uint32_t column = 0;

	while (column < line->cell_count) {
		uint32_t wrap_point = line_find_wrap_point(line, buffer, column,
		                                           text_width, mode);
		if (wrap_point >= line->cell_count) {
			break;
		}
		segment_count++;
		column = wrap_point;
	}

	/* Allocate array for segment start columns. */
	line->wrap_columns = malloc(segment_count * sizeof(uint32_t));

	/*
	 * Second pass: record the actual wrap columns.
	 * wrap_columns[i] is where segment i starts.
	 */
	line->wrap_columns[0] = 0;
	column = 0;

	for (uint16_t segment = 1; segment < segment_count; segment++) {
		uint32_t wrap_point = line_find_wrap_point(line, buffer, column,
		                                           text_width, mode);
		line->wrap_columns[segment] = wrap_point;
		column = wrap_point;
	}

	line->wrap_segment_count = segment_count;
	line->wrap_cache_width = text_width;
	line->wrap_cache_mode = mode;
}

/*****************************************************************************
 * Wrap Mode Helpers
 *****************************************************************************/

/* Forward declaration - editor_set_status_message is in edit.c */
extern void editor_set_status_message(const char *format, ...);

void editor_cycle_wrap_mode(void)
{
	switch (editor.wrap_mode) {
		case WRAP_NONE:
			editor.wrap_mode = WRAP_WORD;
			editor_set_status_message("Wrap: Word");
			break;
		case WRAP_WORD:
			editor.wrap_mode = WRAP_CHAR;
			editor_set_status_message("Wrap: Character");
			break;
		case WRAP_CHAR:
			editor.wrap_mode = WRAP_NONE;
			editor_set_status_message("Wrap: Off");
			break;
	}

	/* Invalidate all wrap caches */
	buffer_invalidate_all_wrap_caches(&editor.buffer);
}

void editor_cycle_wrap_indicator(void)
{
	switch (editor.wrap_indicator) {
		case WRAP_INDICATOR_NONE:
			editor.wrap_indicator = WRAP_INDICATOR_CORNER;
			editor_set_status_message("Wrap indicator: ⎿");
			break;
		case WRAP_INDICATOR_CORNER:
			editor.wrap_indicator = WRAP_INDICATOR_HOOK;
			editor_set_status_message("Wrap indicator: ↪");
			break;
		case WRAP_INDICATOR_HOOK:
			editor.wrap_indicator = WRAP_INDICATOR_ARROW;
			editor_set_status_message("Wrap indicator: →");
			break;
		case WRAP_INDICATOR_ARROW:
			editor.wrap_indicator = WRAP_INDICATOR_DOT;
			editor_set_status_message("Wrap indicator: ·");
			break;
		case WRAP_INDICATOR_DOT:
			editor.wrap_indicator = WRAP_INDICATOR_FLOOR;
			editor_set_status_message("Wrap indicator: ⌊");
			break;
		case WRAP_INDICATOR_FLOOR:
			editor.wrap_indicator = WRAP_INDICATOR_BOTTOM;
			editor_set_status_message("Wrap indicator: ⌞");
			break;
		case WRAP_INDICATOR_BOTTOM:
			editor.wrap_indicator = WRAP_INDICATOR_RETURN;
			editor_set_status_message("Wrap indicator: ↳");
			break;
		case WRAP_INDICATOR_RETURN:
			editor.wrap_indicator = WRAP_INDICATOR_BOX;
			editor_set_status_message("Wrap indicator: └");
			break;
		case WRAP_INDICATOR_BOX:
			editor.wrap_indicator = WRAP_INDICATOR_NONE;
			editor_set_status_message("Wrap indicator: None");
			break;
	}
}

const char *wrap_indicator_string(enum wrap_indicator indicator)
{
	switch (indicator) {
		case WRAP_INDICATOR_CORNER: return "⎿";
		case WRAP_INDICATOR_HOOK:   return "↪";
		case WRAP_INDICATOR_ARROW:  return "→";
		case WRAP_INDICATOR_DOT:    return "·";
		case WRAP_INDICATOR_FLOOR:  return "⌊";
		case WRAP_INDICATOR_BOTTOM: return "⌞";
		case WRAP_INDICATOR_RETURN: return "↳";
		case WRAP_INDICATOR_BOX:    return "└";
		default:                    return " ";
	}
}

/*****************************************************************************
 * Render Column Calculation
 *****************************************************************************/

uint32_t editor_get_render_column(uint32_t row, uint32_t column)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	/*
	 * Hybrid mode: compute reveal range if this is the cursor line.
	 * Cells in the reveal range are counted even if hideable.
	 */
	bool hybrid_active = editor.hybrid_mode &&
	                     syntax_is_markdown_file(editor.buffer.filename);
	uint32_t reveal_start = UINT32_MAX;
	uint32_t reveal_end = 0;
	if (hybrid_active && row == editor.cursor_row) {
		md_should_reveal_element(line, column, &reveal_start, &reveal_end);
	}

	/*
	 * Iterate by grapheme cluster to correctly handle multi-codepoint
	 * characters like emoji with skin tone modifiers and ZWJ sequences.
	 */
	uint32_t render_column = 0;
	uint32_t i = 0;

	while (i < column && i < line->cell_count) {
		/*
		 * Hybrid mode: skip hidden cells when counting render column.
		 * Cells in the reveal range are counted normally.
		 */
		if (hybrid_active && (line->cells[i].flags & CELL_FLAG_HIDEABLE)) {
			bool in_reveal = (i >= reveal_start && i < reveal_end);
			if (!in_reveal) {
				i++;
				continue;
			}
		}

		uint32_t grapheme_end = cursor_next_grapheme(line, &editor.buffer, i);

		/* Don't count grapheme if cursor is in the middle of it */
		if (grapheme_end > column) {
			break;
		}

		/* Get width of first codepoint (base character) */
		uint32_t cp = line->cells[i].codepoint;
		if (cp == '\t') {
			render_column += editor.tab_width - (render_column % editor.tab_width);
		} else {
			/* Find first non-zero-width codepoint in grapheme */
			int width = 0;
			for (uint32_t j = i; j < grapheme_end && j < line->cell_count; j++) {
				width = utflite_codepoint_width(line->cells[j].codepoint);
				if (width > 0) {
					break;
				}
			}
			render_column += (width > 0) ? (uint32_t)width : 1;
		}

		i = grapheme_end;
	}

	return render_column;
}
