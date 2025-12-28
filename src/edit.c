/*****************************************************************************
 * edit - A minimal terminal text editor
 * Phase 3: Cell Architecture
 *****************************************************************************/

/*****************************************************************************
 * Includes and Configuration
 *****************************************************************************/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define UTFLITE_IMPLEMENTATION
#include "../third_party/utflite/single_include/utflite.h"

/* Editor constants */
#define EDIT_VERSION "0.3.0"
#define TAB_STOP_WIDTH 8
#define QUIT_CONFIRM_COUNT 3
#define STATUS_MESSAGE_TIMEOUT 5

/* Control key macro */
#define CONTROL_KEY(k) ((k) & 0x1f)

/* Initial capacities */
#define INITIAL_LINE_CAPACITY 128
#define INITIAL_BUFFER_CAPACITY 256
#define INITIAL_OUTPUT_CAPACITY 4096

/*****************************************************************************
 * Data Structures
 *****************************************************************************/

enum key_code {
	KEY_BACKSPACE = 127,
	/* Special keys use negative values to avoid collision with Unicode codepoints */
	KEY_ARROW_UP = -100,
	KEY_ARROW_DOWN = -99,
	KEY_ARROW_LEFT = -98,
	KEY_ARROW_RIGHT = -97,
	KEY_HOME = -96,
	KEY_END = -95,
	KEY_PAGE_UP = -94,
	KEY_PAGE_DOWN = -93,
	KEY_DELETE = -92,
	KEY_F2 = -91
};

struct cell {
	uint32_t codepoint;
};

struct line {
	struct cell *cells;
	uint32_t cell_count;
	uint32_t cell_capacity;
};

struct buffer {
	struct line *lines;
	uint32_t line_count;
	uint32_t line_capacity;
	char *filename;
	bool is_modified;
};

struct output_buffer {
	char *data;
	size_t length;
	size_t capacity;
};

struct editor_state {
	struct buffer buffer;
	uint32_t cursor_row;
	uint32_t cursor_column;
	uint32_t row_offset;
	uint32_t column_offset;
	uint32_t screen_rows;
	uint32_t screen_columns;
	uint32_t gutter_width;
	bool show_line_numbers;
	char status_message[256];
	time_t status_message_time;
	int quit_confirm_counter;
};

/*****************************************************************************
 * Global State
 *****************************************************************************/

static struct termios original_terminal_settings;
static struct editor_state editor;
static volatile sig_atomic_t terminal_resized = 0;

/*****************************************************************************
 * Terminal Handling
 *****************************************************************************/

static void terminal_disable_raw_mode(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_settings);
}

static void terminal_enable_raw_mode(void)
{
	tcgetattr(STDIN_FILENO, &original_terminal_settings);
	atexit(terminal_disable_raw_mode);

	struct termios raw = original_terminal_settings;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void terminal_handle_resize(int signal)
{
	(void)signal;
	terminal_resized = 1;
}

static bool terminal_get_size(uint32_t *rows, uint32_t *columns)
{
	struct winsize window_size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1 || window_size.ws_col == 0) {
		return false;
	}

	*columns = window_size.ws_col;
	*rows = window_size.ws_row;
	return true;
}

static void terminal_clear_screen(void)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/*****************************************************************************
 * Output Buffer
 *****************************************************************************/

static void output_buffer_init(struct output_buffer *output)
{
	output->data = malloc(INITIAL_OUTPUT_CAPACITY);
	output->length = 0;
	output->capacity = INITIAL_OUTPUT_CAPACITY;
}

static void output_buffer_append(struct output_buffer *output, const char *text, size_t length)
{
	if (output->length + length > output->capacity) {
		size_t new_capacity = output->capacity * 2;
		while (new_capacity < output->length + length) {
			new_capacity *= 2;
		}
		output->data = realloc(output->data, new_capacity);
		output->capacity = new_capacity;
	}

	memcpy(output->data + output->length, text, length);
	output->length += length;
}

static void output_buffer_append_string(struct output_buffer *output, const char *text)
{
	output_buffer_append(output, text, strlen(text));
}

static void output_buffer_flush(struct output_buffer *output)
{
	write(STDOUT_FILENO, output->data, output->length);
	output->length = 0;
}

static void output_buffer_free(struct output_buffer *output)
{
	free(output->data);
	output->data = NULL;
	output->length = 0;
	output->capacity = 0;
}

/*****************************************************************************
 * Input Handling
 *****************************************************************************/

static int input_read_key(void)
{
	int read_count;
	unsigned char character;

	while ((read_count = read(STDIN_FILENO, &character, 1)) != 1) {
		if (read_count == -1 && errno != EAGAIN) {
			return -1;
		}
		if (terminal_resized) {
			terminal_resized = 0;
			return -2;
		}
	}

	/* Handle escape sequences */
	if (character == '\x1b') {
		char sequence[4];

		if (read(STDIN_FILENO, &sequence[0], 1) != 1) {
			return '\x1b';
		}
		if (read(STDIN_FILENO, &sequence[1], 1) != 1) {
			return '\x1b';
		}

		if (sequence[0] == '[') {
			if (sequence[1] >= '0' && sequence[1] <= '9') {
				if (read(STDIN_FILENO, &sequence[2], 1) != 1) {
					return '\x1b';
				}
				if (sequence[2] == '~') {
					switch (sequence[1]) {
						case '1': return KEY_HOME;
						case '3': return KEY_DELETE;
						case '4': return KEY_END;
						case '5': return KEY_PAGE_UP;
						case '6': return KEY_PAGE_DOWN;
						case '7': return KEY_HOME;
						case '8': return KEY_END;
					}
				} else if (sequence[2] >= '0' && sequence[2] <= '9') {
					/* Two-digit escape sequence like \x1b[12~ for F2 */
					if (read(STDIN_FILENO, &sequence[3], 1) != 1) {
						return '\x1b';
					}
					if (sequence[3] == '~') {
						if (sequence[1] == '1' && sequence[2] == '2') {
							return KEY_F2;
						}
					}
				}
			} else {
				switch (sequence[1]) {
					case 'A': return KEY_ARROW_UP;
					case 'B': return KEY_ARROW_DOWN;
					case 'C': return KEY_ARROW_RIGHT;
					case 'D': return KEY_ARROW_LEFT;
					case 'H': return KEY_HOME;
					case 'F': return KEY_END;
				}
			}
		} else if (sequence[0] == 'O') {
			switch (sequence[1]) {
				case 'H': return KEY_HOME;
				case 'F': return KEY_END;
				case 'Q': return KEY_F2;
			}
		}

		return '\x1b';
	}

	/* Handle UTF-8 multi-byte sequences */
	if (character & 0x80) {
		char utf8_buffer[4];
		utf8_buffer[0] = character;
		int bytes_to_read = 0;

		/* Determine number of continuation bytes based on lead byte */
		if ((character & 0xE0) == 0xC0) {
			bytes_to_read = 1;  /* 2-byte sequence */
		} else if ((character & 0xF0) == 0xE0) {
			bytes_to_read = 2;  /* 3-byte sequence */
		} else if ((character & 0xF8) == 0xF0) {
			bytes_to_read = 3;  /* 4-byte sequence */
		} else {
			/* Invalid UTF-8 lead byte, return replacement character */
			return UTFLITE_REPLACEMENT_CHAR;
		}

		/* Read continuation bytes */
		for (int i = 0; i < bytes_to_read; i++) {
			if (read(STDIN_FILENO, &utf8_buffer[1 + i], 1) != 1) {
				return UTFLITE_REPLACEMENT_CHAR;
			}
			/* Verify continuation byte */
			if ((utf8_buffer[1 + i] & 0xC0) != 0x80) {
				return UTFLITE_REPLACEMENT_CHAR;
			}
		}

		/* Decode UTF-8 to codepoint */
		uint32_t codepoint;
		utflite_decode(utf8_buffer, bytes_to_read + 1, &codepoint);
		return (int)codepoint;
	}

	return character;
}

/*****************************************************************************
 * Line Operations
 *****************************************************************************/

static void line_init(struct line *line)
{
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
}

static void line_free(struct line *line)
{
	free(line->cells);
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
}

static void line_ensure_capacity(struct line *line, uint32_t required)
{
	if (required <= line->cell_capacity) {
		return;
	}

	uint32_t new_capacity = line->cell_capacity ? line->cell_capacity * 2 : INITIAL_LINE_CAPACITY;
	while (new_capacity < required) {
		new_capacity *= 2;
	}

	line->cells = realloc(line->cells, new_capacity * sizeof(struct cell));
	line->cell_capacity = new_capacity;
}

static void line_insert_cell(struct line *line, uint32_t position, uint32_t codepoint)
{
	if (position > line->cell_count) {
		position = line->cell_count;
	}

	line_ensure_capacity(line, line->cell_count + 1);

	if (position < line->cell_count) {
		memmove(&line->cells[position + 1], &line->cells[position],
		        (line->cell_count - position) * sizeof(struct cell));
	}

	line->cells[position] = (struct cell){.codepoint = codepoint};
	line->cell_count++;
}

static void line_delete_cell(struct line *line, uint32_t position)
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

static void line_append_cell(struct line *line, uint32_t codepoint)
{
	line_insert_cell(line, line->cell_count, codepoint);
}

static void line_append_cells_from_line(struct line *dest, struct line *src)
{
	for (uint32_t i = 0; i < src->cell_count; i++) {
		line_append_cell(dest, src->cells[i].codepoint);
	}
}

/*****************************************************************************
 * Grapheme Boundary Functions
 *****************************************************************************/

static bool codepoint_is_combining_mark(uint32_t codepoint)
{
	/* Combining Diacritical Marks: U+0300-U+036F */
	if (codepoint >= 0x0300 && codepoint <= 0x036F) return true;
	/* Combining Diacritical Marks Extended: U+1AB0-U+1AFF */
	if (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) return true;
	/* Combining Diacritical Marks Supplement: U+1DC0-U+1DFF */
	if (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) return true;
	/* Combining Diacritical Marks for Symbols: U+20D0-U+20FF */
	if (codepoint >= 0x20D0 && codepoint <= 0x20FF) return true;
	/* Combining Half Marks: U+FE20-U+FE2F */
	if (codepoint >= 0xFE20 && codepoint <= 0xFE2F) return true;
	return false;
}

static uint32_t cursor_prev_grapheme(struct line *line, uint32_t column)
{
	if (column == 0 || line->cell_count == 0) {
		return 0;
	}

	column--;
	while (column > 0 && codepoint_is_combining_mark(line->cells[column].codepoint)) {
		column--;
	}

	return column;
}

static uint32_t cursor_next_grapheme(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return line->cell_count;
	}

	column++;
	while (column < line->cell_count && codepoint_is_combining_mark(line->cells[column].codepoint)) {
		column++;
	}

	return column;
}

/*****************************************************************************
 * Buffer Operations
 *****************************************************************************/

static void buffer_init(struct buffer *buffer)
{
	buffer->lines = malloc(INITIAL_BUFFER_CAPACITY * sizeof(struct line));
	buffer->line_count = 0;
	buffer->line_capacity = INITIAL_BUFFER_CAPACITY;
	buffer->filename = NULL;
	buffer->is_modified = false;
}

static void buffer_free(struct buffer *buffer)
{
	for (uint32_t index = 0; index < buffer->line_count; index++) {
		line_free(&buffer->lines[index]);
	}
	free(buffer->lines);
	free(buffer->filename);
	buffer->lines = NULL;
	buffer->filename = NULL;
	buffer->line_count = 0;
	buffer->line_capacity = 0;
}

static void buffer_ensure_capacity(struct buffer *buffer, uint32_t required)
{
	if (required > buffer->line_capacity) {
		uint32_t new_capacity = buffer->line_capacity * 2;
		while (new_capacity < required) {
			new_capacity *= 2;
		}
		buffer->lines = realloc(buffer->lines, new_capacity * sizeof(struct line));
		buffer->line_capacity = new_capacity;
	}
}

static void buffer_insert_empty_line(struct buffer *buffer, uint32_t position)
{
	if (position > buffer->line_count) {
		position = buffer->line_count;
	}

	buffer_ensure_capacity(buffer, buffer->line_count + 1);

	if (position < buffer->line_count) {
		memmove(&buffer->lines[position + 1], &buffer->lines[position],
		        (buffer->line_count - position) * sizeof(struct line));
	}

	line_init(&buffer->lines[position]);
	buffer->line_count++;
	buffer->is_modified = true;
}

static void buffer_insert_line_from_utf8(struct buffer *buffer, uint32_t position,
                                         const char *text, size_t length)
{
	if (position > buffer->line_count) {
		position = buffer->line_count;
	}

	buffer_ensure_capacity(buffer, buffer->line_count + 1);

	if (position < buffer->line_count) {
		memmove(&buffer->lines[position + 1], &buffer->lines[position],
		        (buffer->line_count - position) * sizeof(struct line));
	}

	line_init(&buffer->lines[position]);

	/* Decode UTF-8 text into cells */
	size_t offset = 0;
	while (offset < length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, length - offset, &codepoint);
		line_append_cell(&buffer->lines[position], codepoint);
		offset += bytes;
	}

	buffer->line_count++;
	buffer->is_modified = true;
}

static void buffer_delete_line(struct buffer *buffer, uint32_t position)
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

static void buffer_insert_cell_at_column(struct buffer *buffer, uint32_t row, uint32_t column,
                                         uint32_t codepoint)
{
	if (row > buffer->line_count) {
		row = buffer->line_count;
	}

	if (row == buffer->line_count) {
		buffer_insert_empty_line(buffer, buffer->line_count);
	}

	struct line *line = &buffer->lines[row];
	line_insert_cell(line, column, codepoint);
	buffer->is_modified = true;
}

static void buffer_delete_grapheme_at_column(struct buffer *buffer, uint32_t row, uint32_t column)
{
	if (row >= buffer->line_count) {
		return;
	}

	struct line *line = &buffer->lines[row];

	if (column < line->cell_count) {
		/* Find the end of this grapheme (skip over combining marks) */
		uint32_t end = cursor_next_grapheme(line, column);
		/* Delete cells from end backwards */
		for (uint32_t i = end; i > column; i--) {
			line_delete_cell(line, column);
		}
		buffer->is_modified = true;
	} else if (row + 1 < buffer->line_count) {
		/* Join with next line */
		struct line *next_line = &buffer->lines[row + 1];
		line_append_cells_from_line(line, next_line);
		buffer_delete_line(buffer, row + 1);
	}
}

static void buffer_insert_newline(struct buffer *buffer, uint32_t row, uint32_t column)
{
	if (row > buffer->line_count) {
		return;
	}

	if (row == buffer->line_count) {
		buffer_insert_empty_line(buffer, buffer->line_count);
		return;
	}

	struct line *line = &buffer->lines[row];

	if (column >= line->cell_count) {
		buffer_insert_empty_line(buffer, row + 1);
	} else {
		/* Insert new line and copy cells after cursor */
		buffer_insert_empty_line(buffer, row + 1);
		struct line *new_line = &buffer->lines[row + 1];

		for (uint32_t i = column; i < line->cell_count; i++) {
			line_append_cell(new_line, line->cells[i].codepoint);
		}

		/* Truncate original line */
		line->cell_count = column;
	}
}

/*****************************************************************************
 * File Operations
 *****************************************************************************/

static void editor_set_status_message(const char *format, ...);

static bool file_open(struct buffer *buffer, const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		return false;
	}

	char *line_buffer = NULL;
	size_t line_buffer_capacity = 0;
	ssize_t line_length;

	while ((line_length = getline(&line_buffer, &line_buffer_capacity, file)) != -1) {
		/* Strip trailing newline and carriage return */
		while (line_length > 0 && (line_buffer[line_length - 1] == '\n' || line_buffer[line_length - 1] == '\r')) {
			line_length--;
		}
		buffer_insert_line_from_utf8(buffer, buffer->line_count, line_buffer, line_length);
	}

	free(line_buffer);
	fclose(file);

	buffer->filename = strdup(filename);
	buffer->is_modified = false;

	return true;
}

static bool file_save(struct buffer *buffer)
{
	if (buffer->filename == NULL) {
		return false;
	}

	FILE *file = fopen(buffer->filename, "w");
	if (file == NULL) {
		return false;
	}

	size_t total_bytes = 0;

	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		for (uint32_t col = 0; col < line->cell_count; col++) {
			char utf8_buffer[UTFLITE_MAX_BYTES];
			int bytes = utflite_encode(line->cells[col].codepoint, utf8_buffer);
			fwrite(utf8_buffer, 1, bytes, file);
			total_bytes += bytes;
		}

		fwrite("\n", 1, 1, file);
		total_bytes++;
	}

	fclose(file);
	buffer->is_modified = false;

	editor_set_status_message("%zu bytes written to disk", total_bytes);

	return true;
}

/*****************************************************************************
 * Editor Operations
 *****************************************************************************/

static void editor_init(void)
{
	buffer_init(&editor.buffer);
	editor.cursor_row = 0;
	editor.cursor_column = 0;
	editor.row_offset = 0;
	editor.column_offset = 0;
	editor.screen_rows = 0;
	editor.screen_columns = 0;
	editor.gutter_width = 0;
	editor.show_line_numbers = true;
	editor.status_message[0] = '\0';
	editor.status_message_time = 0;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

static void editor_update_gutter_width(void)
{
	if (!editor.show_line_numbers) {
		editor.gutter_width = 0;
		return;
	}

	/* Calculate digits needed for line count */
	uint32_t line_count = editor.buffer.line_count;
	if (line_count == 0) {
		line_count = 1;
	}

	uint32_t digits = 0;
	while (line_count > 0) {
		digits++;
		line_count /= 10;
	}

	/* Minimum 2 digits, plus 1 space after */
	if (digits < 2) {
		digits = 2;
	}
	editor.gutter_width = digits + 1;
}

static void editor_update_screen_size(void)
{
	if (!terminal_get_size(&editor.screen_rows, &editor.screen_columns)) {
		editor.screen_rows = 24;
		editor.screen_columns = 80;
	}
	/* Reserve space for status bar and message bar */
	editor.screen_rows -= 2;
}

static void editor_set_status_message(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(editor.status_message, sizeof(editor.status_message), format, arguments);
	va_end(arguments);
	editor.status_message_time = time(NULL);
}

static uint32_t editor_get_line_length(uint32_t row)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}
	return editor.buffer.lines[row].cell_count;
}

static uint32_t editor_get_render_column(uint32_t row, uint32_t column)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	uint32_t render_column = 0;

	for (uint32_t i = 0; i < column && i < line->cell_count; i++) {
		uint32_t codepoint = line->cells[i].codepoint;

		if (codepoint == '\t') {
			render_column += TAB_STOP_WIDTH - (render_column % TAB_STOP_WIDTH);
		} else {
			int width = utflite_codepoint_width(codepoint);
			if (width < 0) {
				width = 1;  /* Control characters */
			}
			render_column += width;
		}
	}

	return render_column;
}

static void editor_scroll(void)
{
	/* Vertical scrolling */
	if (editor.cursor_row < editor.row_offset) {
		editor.row_offset = editor.cursor_row;
	}
	if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
		editor.row_offset = editor.cursor_row - editor.screen_rows + 1;
	}

	/* Horizontal scrolling - use render column to account for tabs */
	uint32_t render_column = editor_get_render_column(editor.cursor_row, editor.cursor_column);

	uint32_t text_area_width = editor.screen_columns - editor.gutter_width;
	if (render_column < editor.column_offset) {
		editor.column_offset = render_column;
	}
	if (render_column >= editor.column_offset + text_area_width) {
		editor.column_offset = render_column - text_area_width + 1;
	}
}

static void editor_move_cursor(int key)
{
	uint32_t line_length = editor_get_line_length(editor.cursor_row);
	struct line *current_line = editor.cursor_row < editor.buffer.line_count
	                            ? &editor.buffer.lines[editor.cursor_row] : NULL;

	switch (key) {
		case KEY_ARROW_LEFT:
			if (editor.cursor_column > 0 && current_line) {
				editor.cursor_column = cursor_prev_grapheme(current_line, editor.cursor_column);
			} else if (editor.cursor_row > 0) {
				editor.cursor_row--;
				editor.cursor_column = editor_get_line_length(editor.cursor_row);
			}
			break;

		case KEY_ARROW_RIGHT:
			if (editor.cursor_column < line_length && current_line) {
				editor.cursor_column = cursor_next_grapheme(current_line, editor.cursor_column);
			} else if (editor.cursor_row < editor.buffer.line_count - 1) {
				editor.cursor_row++;
				editor.cursor_column = 0;
			}
			break;

		case KEY_ARROW_UP:
			if (editor.cursor_row > 0) {
				editor.cursor_row--;
			}
			break;

		case KEY_ARROW_DOWN:
			if (editor.cursor_row < editor.buffer.line_count - 1) {
				editor.cursor_row++;
			} else if (editor.buffer.line_count == 0) {
				/* Allow being on line 0 even with empty buffer */
			}
			break;

		case KEY_HOME:
			editor.cursor_column = 0;
			break;

		case KEY_END:
			editor.cursor_column = line_length;
			break;

		case KEY_PAGE_UP:
			if (editor.cursor_row > editor.screen_rows) {
				editor.cursor_row -= editor.screen_rows;
			} else {
				editor.cursor_row = 0;
			}
			break;

		case KEY_PAGE_DOWN:
			if (editor.cursor_row + editor.screen_rows < editor.buffer.line_count) {
				editor.cursor_row += editor.screen_rows;
			} else if (editor.buffer.line_count > 0) {
				editor.cursor_row = editor.buffer.line_count - 1;
			}
			break;
	}

	/* Snap cursor to end of line if it's past the line length */
	line_length = editor_get_line_length(editor.cursor_row);
	if (editor.cursor_column > line_length) {
		editor.cursor_column = line_length;
	}
}

static void editor_insert_character(uint32_t codepoint)
{
	buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	editor.cursor_column++;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

static void editor_insert_newline(void)
{
	buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	editor.cursor_row++;
	editor.cursor_column = 0;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

static void editor_delete_character(void)
{
	if (editor.cursor_row >= editor.buffer.line_count) {
		return;
	}

	buffer_delete_grapheme_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column);
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

static void editor_handle_backspace(void)
{
	if (editor.cursor_row == 0 && editor.cursor_column == 0) {
		return;
	}

	if (editor.cursor_column > 0) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		uint32_t new_column = cursor_prev_grapheme(line, editor.cursor_column);
		editor.cursor_column = new_column;
		buffer_delete_grapheme_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column);
	} else {
		/* Join with previous line */
		uint32_t previous_line_length = editor_get_line_length(editor.cursor_row - 1);
		struct line *previous_line = &editor.buffer.lines[editor.cursor_row - 1];
		struct line *current_line = &editor.buffer.lines[editor.cursor_row];
		line_append_cells_from_line(previous_line, current_line);
		buffer_delete_line(&editor.buffer, editor.cursor_row);
		editor.cursor_row--;
		editor.cursor_column = previous_line_length;
	}

	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

static void editor_save(void)
{
	if (editor.buffer.filename == NULL) {
		editor_set_status_message("No filename specified");
		return;
	}

	if (file_save(&editor.buffer)) {
		editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
	} else {
		editor_set_status_message("Error saving file: %s", strerror(errno));
	}
}

/*****************************************************************************
 * Rendering
 *****************************************************************************/

static void render_line_content(struct output_buffer *output, struct line *line,
                                uint32_t column_offset, int max_width)
{
	int visual_column = 0;
	uint32_t cell_index = 0;

	/* Skip to column_offset (for horizontal scrolling) */
	while (cell_index < line->cell_count && visual_column < (int)column_offset) {
		uint32_t codepoint = line->cells[cell_index].codepoint;

		int width;
		if (codepoint == '\t') {
			width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(codepoint);
			if (width < 0) {
				width = 1;  /* Control characters */
			}
		}

		visual_column += width;
		cell_index++;
	}

	/* Render visible content */
	int rendered_width = 0;
	while (cell_index < line->cell_count && rendered_width < max_width) {
		uint32_t codepoint = line->cells[cell_index].codepoint;

		int width;
		if (codepoint == '\t') {
			width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			/* Render spaces for tab */
			for (int i = 0; i < width && rendered_width < max_width; i++) {
				output_buffer_append_string(output, " ");
				rendered_width++;
			}
		} else {
			width = utflite_codepoint_width(codepoint);
			if (width < 0) {
				width = 1;  /* Control characters */
			}

			/* Only render if we have room for the full character width */
			if (rendered_width + width <= max_width) {
				char utf8_buffer[UTFLITE_MAX_BYTES];
				int bytes = utflite_encode(codepoint, utf8_buffer);
				output_buffer_append(output, utf8_buffer, bytes);
				rendered_width += width;
			} else {
				/* Not enough room for wide character, fill with spaces */
				while (rendered_width < max_width) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
			}
		}

		visual_column += width;
		cell_index++;
	}
}

static void render_draw_rows(struct output_buffer *output)
{
	/* Calculate where to show the welcome message (vertically centered) */
	uint32_t welcome_row = editor.screen_rows / 2;

	for (uint32_t screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
		/* Clear entire line first to handle gutter width changes */
		output_buffer_append_string(output, "\x1b[2K");

		uint32_t file_row = screen_row + editor.row_offset;

		/* Show line number 1 for empty buffer on first screen row */
		bool is_empty_buffer_first_line = (editor.buffer.line_count == 0 && file_row == 0);

		if (file_row >= editor.buffer.line_count && !is_empty_buffer_first_line) {
			/* Empty line past end of file */
			if (editor.buffer.line_count == 0 && screen_row == welcome_row) {
				/* Show centered welcome message for empty buffer */
				char welcome[64];
				int welcome_length = snprintf(welcome, sizeof(welcome), "edit v%s", EDIT_VERSION);

				/* Center horizontally */
				int text_area_width = editor.screen_columns - editor.gutter_width;
				int padding = (text_area_width - welcome_length) / 2;
				if (padding < 0) {
					padding = 0;
				}

				/* Add gutter space */
				for (uint32_t i = 0; i < editor.gutter_width; i++) {
					output_buffer_append_string(output, " ");
				}

				/* Add left padding */
				for (int i = 0; i < padding; i++) {
					output_buffer_append_string(output, " ");
				}

				/* Output welcome text */
				output_buffer_append(output, welcome, welcome_length);
			}
			/* else: empty line, just clear it */
		} else {
			/* Draw line number if enabled */
			if (editor.show_line_numbers && editor.gutter_width > 0) {
				char line_number_buffer[16];
				snprintf(line_number_buffer, sizeof(line_number_buffer), "%*u ",
				         editor.gutter_width - 1, file_row + 1);
				output_buffer_append(output, line_number_buffer, editor.gutter_width);
			}

			/* Draw line content with tab expansion */
			if (file_row < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[file_row];
				int text_area_width = editor.screen_columns - editor.gutter_width;
				render_line_content(output, line, editor.column_offset, text_area_width);
			}
		}

		output_buffer_append_string(output, "\r\n");
	}
}

static void render_draw_status_bar(struct output_buffer *output)
{
	/* Reverse video */
	output_buffer_append_string(output, "\x1b[7m");

	char left_status[256];
	char right_status[64];

	const char *filename = editor.buffer.filename ? editor.buffer.filename : "[No Name]";
	int left_length = snprintf(left_status, sizeof(left_status), " %.100s%s",
	                           filename, editor.buffer.is_modified ? " [+]" : "");

	int right_length = snprintf(right_status, sizeof(right_status), "%u/%u ",
	                            editor.cursor_row + 1, editor.buffer.line_count);

	if (left_length > (int)editor.screen_columns) {
		left_length = editor.screen_columns;
	}

	output_buffer_append(output, left_status, left_length);

	while (left_length < (int)editor.screen_columns) {
		if ((int)editor.screen_columns - left_length == right_length) {
			output_buffer_append(output, right_status, right_length);
			break;
		} else {
			output_buffer_append_string(output, " ");
			left_length++;
		}
	}

	/* Reset attributes */
	output_buffer_append_string(output, "\x1b[m");
	output_buffer_append_string(output, "\r\n");
}

static void render_draw_message_bar(struct output_buffer *output)
{
	output_buffer_append_string(output, "\x1b[K");

	int message_length = strlen(editor.status_message);

	if (message_length > (int)editor.screen_columns) {
		message_length = editor.screen_columns;
	}

	if (message_length > 0 && time(NULL) - editor.status_message_time < STATUS_MESSAGE_TIMEOUT) {
		output_buffer_append(output, editor.status_message, message_length);
	}
}

static void render_refresh_screen(void)
{
	editor_update_gutter_width();
	editor_scroll();

	struct output_buffer output;
	output_buffer_init(&output);

	/* Hide cursor */
	output_buffer_append_string(&output, "\x1b[?25l");
	/* Move cursor to home */
	output_buffer_append_string(&output, "\x1b[H");

	render_draw_rows(&output);
	render_draw_status_bar(&output);
	render_draw_message_bar(&output);

	/* Position cursor - use render column to account for tabs */
	uint32_t render_column = editor_get_render_column(editor.cursor_row, editor.cursor_column);
	char cursor_position[32];
	snprintf(cursor_position, sizeof(cursor_position), "\x1b[%u;%uH",
	         (editor.cursor_row - editor.row_offset) + 1,
	         (render_column - editor.column_offset) + editor.gutter_width + 1);
	output_buffer_append_string(&output, cursor_position);

	/* Show cursor */
	output_buffer_append_string(&output, "\x1b[?25h");

	output_buffer_flush(&output);
	output_buffer_free(&output);
}

/*****************************************************************************
 * Main Loop
 *****************************************************************************/

static void editor_process_keypress(void)
{
	int key = input_read_key();

	if (key == -1) {
		return;
	}

	if (key == -2) {
		/* Terminal resize */
		editor_update_screen_size();
		return;
	}

	switch (key) {
		case CONTROL_KEY('q'):
			if (editor.buffer.is_modified && editor.quit_confirm_counter > 1) {
				editor_set_status_message("WARNING: File has unsaved changes. "
				                          "Press Ctrl-Q %d more times to quit.", editor.quit_confirm_counter - 1);
				editor.quit_confirm_counter--;
				return;
			}
			terminal_clear_screen();
			buffer_free(&editor.buffer);
			exit(0);
			break;

		case CONTROL_KEY('s'):
			editor_save();
			break;

		case KEY_F2:
			editor.show_line_numbers = !editor.show_line_numbers;
			editor_update_gutter_width();
			editor_set_status_message("Line numbers %s", editor.show_line_numbers ? "on" : "off");
			break;

		case KEY_ARROW_UP:
		case KEY_ARROW_DOWN:
		case KEY_ARROW_LEFT:
		case KEY_ARROW_RIGHT:
		case KEY_HOME:
		case KEY_END:
		case KEY_PAGE_UP:
		case KEY_PAGE_DOWN:
			editor_move_cursor(key);
			break;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			editor_handle_backspace();
			break;

		case KEY_DELETE:
			editor_delete_character();
			break;

		case '\r':
			editor_insert_newline();
			break;

		case '\x1b':
		case CONTROL_KEY('l'):
			/* Escape and Ctrl-L: ignore */
			break;

		default:
			/* Insert printable ASCII (32-126) and Unicode codepoints (>= 128) */
			if ((key >= 32 && key < 127) || key >= 128) {
				editor_insert_character((uint32_t)key);
			}
			break;
	}
}

int main(int argument_count, char *argument_values[])
{
	terminal_enable_raw_mode();

	/* Set up signal handler for window resize */
	struct sigaction signal_action;
	signal_action.sa_handler = terminal_handle_resize;
	sigemptyset(&signal_action.sa_mask);
	signal_action.sa_flags = SA_RESTART;
	sigaction(SIGWINCH, &signal_action, NULL);

	editor_init();
	editor_update_screen_size();

	if (argument_count >= 2) {
		if (!file_open(&editor.buffer, argument_values[1])) {
			/* File doesn't exist yet, just set filename for new file */
			editor.buffer.filename = strdup(argument_values[1]);
			editor.buffer.is_modified = false;
		}
	}

	editor_update_gutter_width();
	editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit | F2 = toggle line numbers");

	while (1) {
		render_refresh_screen();
		editor_process_keypress();
	}

	return 0;
}
