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
 * Initialize a line as hot with empty cell array.
 */
void line_init(struct line *line);

/*
 * Free all memory associated with a line.
 */
void line_free(struct line *line);
/*
 * Invalidate wrap cache for a line.
 */
void line_invalidate_wrap_cache(struct line *line);

/*
 * Ensure line can hold at least 'required' cells.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_ensure_capacity_checked(struct line *line, uint32_t required);

/*
 * Ensure line can hold at least 'required' cells.
 * Crashes on allocation failure.
 */
void line_ensure_capacity(struct line *line, uint32_t required);

/*
 * Insert a cell at position.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_insert_cell_checked(struct line *line, uint32_t position, uint32_t codepoint);

/*
 * Insert a cell at position.
 * Crashes on allocation failure.
 */
void line_insert_cell(struct line *line, uint32_t position, uint32_t codepoint);

/*
 * Delete a cell at position.
 */
void line_delete_cell(struct line *line, uint32_t position);

/*
 * Append a cell to end of line.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_append_cell_checked(struct line *line, uint32_t codepoint);

/*
 * Append a cell to end of line.
 * Crashes on allocation failure.
 */
void line_append_cell(struct line *line, uint32_t codepoint);

/*
 * Append all cells from src line to dest line.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_append_cells_from_line_checked(struct line *dest, struct line *src);

/*
 * Append all cells from src line to dest line.
 * Crashes on allocation failure.
 */
void line_append_cells_from_line(struct line *dest, struct line *src);

/*
 * Warm a cold line by decoding UTF-8 to cells.
 * Returns 0 on success, -ENOMEM on failure.
 */
int line_warm_checked(struct line *line, struct buffer *buffer);

/*
 * Warm a cold line by decoding UTF-8 to cells.
 * Crashes on allocation failure.
 */
void line_warm(struct line *line, struct buffer *buffer);

/*
 * Get cell count for a line.
 * For cold lines, counts codepoints without allocating.
 */
uint32_t line_get_cell_count(struct line *line, struct buffer *buffer);

/*****************************************************************************
 * Grapheme Boundary Functions
 *****************************************************************************/

/*
 * Check if a codepoint is a combining mark.
 */
bool codepoint_is_combining_mark(uint32_t codepoint);

/*
 * Move cursor left to previous grapheme cluster.
 */
uint32_t cursor_prev_grapheme(struct line *line, struct buffer *buffer, uint32_t column);

/*
 * Move cursor right to next grapheme cluster.
 */
uint32_t cursor_next_grapheme(struct line *line, struct buffer *buffer, uint32_t column);

/*****************************************************************************
 * Buffer Operations
 *****************************************************************************/

/*
 * Initialize a buffer with starting capacity.
 */
void buffer_init(struct buffer *buffer);

/*
 * Free all buffer resources.
 */
void buffer_free(struct buffer *buffer);
/*
 * Load buffer content from a memory block (for stdin pipe input).
 * Content is parsed into HOT lines (fully in-memory, no mmap backing).
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
int buffer_load_from_memory(struct buffer *buffer, const char *content, size_t size);

/*
 * Ensure buffer can hold at least 'required' lines.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_ensure_capacity_checked(struct buffer *buffer, uint32_t required);

/*
 * Ensure buffer can hold at least 'required' lines.
 * Crashes on allocation failure.
 */
void buffer_ensure_capacity(struct buffer *buffer, uint32_t required);

/*
 * Insert an empty line at position.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_insert_empty_line_checked(struct buffer *buffer, uint32_t position);

/*
 * Insert an empty line at position.
 * Crashes on allocation failure.
 */
void buffer_insert_empty_line(struct buffer *buffer, uint32_t position);

/*
 * Delete line at position.
 */
void buffer_delete_line(struct buffer *buffer, uint32_t position);

/*
 * Insert a cell at row/column.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_insert_cell_at_column_checked(struct buffer *buffer, uint32_t row,
                                         uint32_t column, uint32_t codepoint);

/*
 * Insert a cell at row/column.
 * Crashes on allocation failure.
 */
void buffer_insert_cell_at_column(struct buffer *buffer, uint32_t row,
                                  uint32_t column, uint32_t codepoint);

/*
 * Delete grapheme at row/column.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_delete_grapheme_at_column_checked(struct buffer *buffer,
                                             uint32_t row, uint32_t column);

/*
 * Delete grapheme at row/column.
 * Crashes on allocation failure.
 */
void buffer_delete_grapheme_at_column(struct buffer *buffer, uint32_t row, uint32_t column);

/*
 * Insert newline at row/column.
 * Returns 0 on success, -ENOMEM on failure.
 */
int buffer_insert_newline_checked(struct buffer *buffer, uint32_t row, uint32_t column);

/*
 * Insert newline at row/column.
 * Crashes on allocation failure.
 */
void buffer_insert_newline(struct buffer *buffer, uint32_t row, uint32_t column);

/*
 * Invalidate wrap caches for all lines.
 */
void buffer_invalidate_all_wrap_caches(struct buffer *buffer);

/*
 * Swap two lines in the buffer.
 */
void buffer_swap_lines(struct buffer *buffer, uint32_t row1, uint32_t row2);

#endif /* EDIT_BUFFER_H */
