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
 * Initialize an output buffer with starting capacity.
 * Returns 0 on success, negative error code on failure.
 */
int output_buffer_init_checked(struct output_buffer *output);

/*
 * Initialize an output buffer.
 * Crashes on allocation failure.
 */
void output_buffer_init(struct output_buffer *output);

/*
 * Append bytes to the output buffer, growing if needed.
 * Returns 0 on success, negative error code on failure.
 */
int output_buffer_append_checked(struct output_buffer *output,
                                 const char *text, size_t length);

/*
 * Append bytes to the output buffer.
 * Crashes on allocation failure.
 */
void output_buffer_append(struct output_buffer *output,
                          const char *text, size_t length);

/*
 * Append a null-terminated string to the output buffer.
 */
void output_buffer_append_string(struct output_buffer *output, const char *text);

/*
 * Append a single character to the output buffer.
 */
void output_buffer_append_char(struct output_buffer *output, char character);

/*
 * Write all buffered data to stdout and reset buffer length.
 */
void output_buffer_flush(struct output_buffer *output);

/*
 * Free the output buffer's memory and reset all fields.
 */
void output_buffer_free(struct output_buffer *output);

/*****************************************************************************
 * Soft Wrap
 *****************************************************************************/

/*
 * Find the best column to break a line for wrapping.
 * Returns the column where the segment should end.
 */
uint32_t line_find_wrap_point(struct line *line, uint32_t start_col,
                              uint32_t max_width, enum wrap_mode mode);

/*
 * Compute wrap points for a line.
 * Populates the line's wrap cache fields.
 */
void line_compute_wrap_points(struct line *line, struct buffer *buffer,
                              uint16_t text_width, enum wrap_mode mode);

/*
 * Get the render column for a given buffer position.
 * Accounts for tabs and wide characters.
 */
uint32_t editor_get_render_column(uint32_t row, uint32_t column);

/*****************************************************************************
 * Wrap Mode Helpers
 *****************************************************************************/

/*
 * Cycle through wrap modes: NONE -> WORD -> CHAR -> NONE
 */
void editor_cycle_wrap_mode(void);

/*
 * Cycle through wrap indicators.
 */
void editor_cycle_wrap_indicator(void);

/*
 * Get the UTF-8 string for a wrap indicator.
 */
const char *wrap_indicator_string(enum wrap_indicator indicator);

/*****************************************************************************
 * Screen Refresh
 *****************************************************************************/

/*
 * Refresh the screen.
 * Calculates gutter width, scrolls to keep cursor visible, and redraws
 * all rows, status bar, and message bar.
 */
int render_refresh_screen(void);

#endif /* EDIT_RENDER_H */
