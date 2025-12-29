/*****************************************************************************
 * edit - A minimal terminal text editor
 * Phase 9: Clipboard Integration
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define UTFLITE_IMPLEMENTATION
#include "../third_party/utflite/single_include/utflite.h"

/* Current version of the editor, displayed in welcome message and status. */
#define EDIT_VERSION "0.9.0"

/* Number of spaces a tab character expands to when rendered. */
#define TAB_STOP_WIDTH 8

/* How many times Ctrl-Q must be pressed to quit with unsaved changes. */
#define QUIT_CONFIRM_COUNT 3

/* Seconds before status bar message disappears. */
#define STATUS_MESSAGE_TIMEOUT 5

/* Adaptive scroll configuration */
#define SCROLL_VELOCITY_DECAY 0.85      /* Exponential smoothing factor (0-1) */
#define SCROLL_MIN_LINES 1              /* Minimum lines to scroll */
#define SCROLL_MAX_LINES 20             /* Maximum lines to scroll */
#define SCROLL_VELOCITY_SLOW 4.0        /* Events/sec threshold for min scroll */
#define SCROLL_VELOCITY_FAST 18.0       /* Events/sec threshold for max scroll */
#define SCROLL_VELOCITY_TIMEOUT 0.4     /* Seconds of inactivity before velocity resets */

/* Converts a letter to its Ctrl+key equivalent (e.g., 'q' -> Ctrl-Q). */
#define CONTROL_KEY(k) ((k) & 0x1f)

/* Starting allocation size for a line's cell array. */
#define INITIAL_LINE_CAPACITY 128

/* Starting allocation size for the buffer's line array. */
#define INITIAL_BUFFER_CAPACITY 256

/* Starting allocation size for the output buffer used in rendering. */
#define INITIAL_OUTPUT_CAPACITY 4096

/*****************************************************************************
 * Data Structures
 *****************************************************************************/

/* Special key codes returned by input_read_key(). Negative values avoid
 * collision with Unicode codepoints which are all positive. */
enum key_code {
	/* Standard backspace key (ASCII 127). */
	KEY_BACKSPACE = 127,

	/* Arrow keys for cursor movement. */
	KEY_ARROW_UP = -100,
	KEY_ARROW_DOWN = -99,
	KEY_ARROW_LEFT = -98,
	KEY_ARROW_RIGHT = -97,

	/* Navigation keys. */
	KEY_HOME = -96,
	KEY_END = -95,
	KEY_PAGE_UP = -94,
	KEY_PAGE_DOWN = -93,
	KEY_DELETE = -92,

	/* Function keys. */
	KEY_F2 = -91,

	/* Ctrl+Arrow keys for word movement. */
	KEY_CTRL_ARROW_LEFT = -70,
	KEY_CTRL_ARROW_RIGHT = -69,

	/* Shift+Arrow keys for selection. */
	KEY_SHIFT_ARROW_UP = -60,
	KEY_SHIFT_ARROW_DOWN = -59,
	KEY_SHIFT_ARROW_LEFT = -58,
	KEY_SHIFT_ARROW_RIGHT = -57,
	KEY_SHIFT_HOME = -56,
	KEY_SHIFT_END = -55,
	KEY_SHIFT_PAGE_UP = -54,
	KEY_SHIFT_PAGE_DOWN = -53,

	/* Ctrl+Shift+Arrow for word selection. */
	KEY_CTRL_SHIFT_ARROW_LEFT = -52,
	KEY_CTRL_SHIFT_ARROW_RIGHT = -51,

	/* Mouse events (handled internally, not returned). */
	KEY_MOUSE_EVENT = -3
};

/* Line temperature indicates whether a line's content is backed by mmap
 * or has been materialized into cells. Cold lines use no memory for content. */
enum line_temperature {
	/* Line content is in mmap, no cells allocated. */
	LINE_TEMPERATURE_COLD = 0,

	/* Cells exist, decoded from mmap but not yet edited. */
	LINE_TEMPERATURE_WARM = 1,

	/* Line has been edited, mmap content is stale. */
	LINE_TEMPERATURE_HOT = 2
};

/* Token types for syntax highlighting. Each cell is tagged with its type
 * to determine the color used when rendering. */
enum syntax_token {
	SYNTAX_NORMAL = 0,    /* Default text */
	SYNTAX_KEYWORD,       /* if, else, for, while, return, etc. */
	SYNTAX_TYPE,          /* int, char, void, struct, etc. */
	SYNTAX_STRING,        /* "..." and '...' */
	SYNTAX_NUMBER,        /* 123, 0xFF, 3.14 */
	SYNTAX_COMMENT,       /* Line and block comments */
	SYNTAX_PREPROCESSOR,  /* #include, #define, etc. */
	SYNTAX_FUNCTION,      /* function names (identifier before '(') */
	SYNTAX_OPERATOR,      /* +, -, *, /, =, etc. */
	SYNTAX_BRACKET,       /* (), [], {} */
	SYNTAX_ESCAPE,        /* \n, \t, etc. inside strings */
	SYNTAX_TOKEN_COUNT    /* Number of token types */
};

/* Character classes for word boundary detection. */
enum character_class {
	CHAR_CLASS_WHITESPACE = 0,  /* Space, tab */
	CHAR_CLASS_LETTER = 1,      /* a-z, A-Z, unicode letters */
	CHAR_CLASS_DIGIT = 2,       /* 0-9 */
	CHAR_CLASS_UNDERSCORE = 3,  /* _ (often part of identifiers) */
	CHAR_CLASS_PUNCTUATION = 4, /* Operators, symbols */
	CHAR_CLASS_BRACKET = 5,     /* ()[]{}  */
	CHAR_CLASS_QUOTE = 6,       /* " ' ` */
	CHAR_CLASS_OTHER = 7        /* Everything else */
};

/* Token position within a word (sequence of same-class characters). */
enum token_position {
	TOKEN_POSITION_SOLO = 0,    /* Single character token: "(" or "+" */
	TOKEN_POSITION_START = 1,   /* First char of multi-char: "hello" -> 'h' */
	TOKEN_POSITION_MIDDLE = 2,  /* Middle char: "hello" -> 'e', 'l', 'l' */
	TOKEN_POSITION_END = 3      /* Last char: "hello" -> 'o' */
};

/* Pair types for matched delimiters. */
enum pair_type {
	PAIR_TYPE_NONE = 0,
	PAIR_TYPE_COMMENT,      /* Block comments */
	PAIR_TYPE_PAREN,        /* ( ... ) */
	PAIR_TYPE_BRACKET,      /* [ ... ] */
	PAIR_TYPE_BRACE,        /* { ... } */
	PAIR_TYPE_DOUBLE_QUOTE, /* " ... " */
	PAIR_TYPE_SINGLE_QUOTE  /* ' ... ' */
};

/* Role of a delimiter in a pair. */
enum pair_role {
	PAIR_ROLE_NONE = 0,
	PAIR_ROLE_OPENER = 1,   /* Opening delimiter */
	PAIR_ROLE_CLOSER = 2    /* Closing delimiter */
};

/* Mouse event types for input handling. */
enum mouse_event {
	MOUSE_NONE = 0,
	MOUSE_LEFT_PRESS,
	MOUSE_LEFT_RELEASE,
	MOUSE_LEFT_DRAG,
	MOUSE_SCROLL_UP,
	MOUSE_SCROLL_DOWN
};

/* Mouse input data from SGR mouse events. */
struct mouse_input {
	/* Type of mouse event. */
	enum mouse_event event;

	/* Screen row (0-based). */
	uint32_t row;

	/* Screen column (0-based). */
	uint32_t column;
};

/*
 * Neighbor field layout (8 bits):
 * Bits 0-2: Character class (0-7)
 * Bits 3-4: Token position (0-3)
 * Bits 5-7: Reserved
 */
#define NEIGHBOR_CLASS_MASK     0x07
#define NEIGHBOR_CLASS_SHIFT    0
#define NEIGHBOR_POSITION_MASK  0x18
#define NEIGHBOR_POSITION_SHIFT 3

/*
 * Context field layout (32 bits):
 * Bits 0-23:  Pair ID (up to 16 million unique pairs)
 * Bits 24-26: Pair type (0-7)
 * Bits 27-28: Pair role (0-3)
 * Bits 29-31: Reserved
 */
#define CONTEXT_PAIR_ID_MASK    0x00FFFFFF
#define CONTEXT_PAIR_TYPE_MASK  0x07000000
#define CONTEXT_PAIR_TYPE_SHIFT 24
#define CONTEXT_PAIR_ROLE_MASK  0x18000000
#define CONTEXT_PAIR_ROLE_SHIFT 27

/* RGB color for syntax highlighting. */
struct syntax_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

/* Tokyo Night color theme for syntax highlighting. */
static const struct syntax_color THEME_COLORS[] = {
	[SYNTAX_NORMAL]       = {0xc0, 0xca, 0xf5},  /* Light gray-blue */
	[SYNTAX_KEYWORD]      = {0xbb, 0x9a, 0xf7},  /* Purple */
	[SYNTAX_TYPE]         = {0x2a, 0xc3, 0xde},  /* Cyan */
	[SYNTAX_STRING]       = {0x9e, 0xce, 0x6a},  /* Green */
	[SYNTAX_NUMBER]       = {0xff, 0x9e, 0x64},  /* Orange */
	[SYNTAX_COMMENT]      = {0x56, 0x5f, 0x89},  /* Gray */
	[SYNTAX_PREPROCESSOR] = {0x7d, 0xcf, 0xff},  /* Light blue */
	[SYNTAX_FUNCTION]     = {0x7a, 0xa2, 0xf7},  /* Blue */
	[SYNTAX_OPERATOR]     = {0x89, 0xdd, 0xff},  /* Light cyan */
	[SYNTAX_BRACKET]      = {0xc0, 0xca, 0xf5},  /* Same as normal */
	[SYNTAX_ESCAPE]       = {0xff, 0x9e, 0x64},  /* Orange */
};

/* Background color for the editor. */
static const struct syntax_color THEME_BACKGROUND = {0x1a, 0x1b, 0x26};

/* Selection highlight background color (darker blue). */
static const struct syntax_color THEME_SELECTION = {0x28, 0x3b, 0x50};

/* Line number gutter colors. */
static const struct syntax_color THEME_LINE_NUMBER = {0x3b, 0x42, 0x61};
static const struct syntax_color THEME_LINE_NUMBER_ACTIVE = {0x73, 0x7a, 0xa2};

/* A single character cell storing one Unicode codepoint and metadata. */
struct cell {
	/* The Unicode codepoint stored in this cell. */
	uint32_t codepoint;

	/* Token type for syntax highlighting. */
	uint16_t syntax;

	/* Character class and token position for word boundaries. */
	uint8_t neighbor;

	/* Reserved for future use. */
	uint8_t flags;

	/* Pair ID and type for matched delimiters. */
	uint32_t context;
};

/* A single line of text. Cold lines reference mmap content directly.
 * Warm/hot lines have cells allocated. */
struct line {
	/* Dynamic array of cells containing the line's characters. */
	struct cell *cells;

	/* Number of cells currently in use. */
	uint32_t cell_count;

	/* Allocated capacity of the cells array. */
	uint32_t cell_capacity;

	/* Byte offset into mmap where this line's content starts. */
	size_t mmap_offset;

	/* Byte length of line content in mmap (excluding newline). */
	uint32_t mmap_length;

	/* Temperature indicates whether cells are allocated/edited. */
	enum line_temperature temperature;
};

/* The text buffer holding all lines of the file being edited. Manages
 * file I/O, mmap backing, and tracks modification state. */
struct buffer {
	/* Dynamic array of lines in the buffer. */
	struct line *lines;

	/* Number of lines currently in the buffer. */
	uint32_t line_count;

	/* Allocated capacity of the lines array. */
	uint32_t line_capacity;

	/* Path to the file on disk, or NULL for a new unsaved file. */
	char *filename;

	/* True if the buffer has unsaved changes. */
	bool is_modified;

	/* File descriptor for mmap, or -1 if no file mapped. */
	int file_descriptor;

	/* Base pointer to memory-mapped file content. */
	char *mmap_base;

	/* Size of the memory-mapped region in bytes. */
	size_t mmap_size;

	/* Counter for generating unique pair IDs. */
	uint32_t next_pair_id;
};

/* Accumulates output bytes before flushing to the terminal. Batching
 * writes reduces flicker and improves rendering performance. */
struct output_buffer {
	/* The accumulated output data. */
	char *data;

	/* Number of bytes currently in the buffer. */
	size_t length;

	/* Allocated capacity of the data array. */
	size_t capacity;
};

/* Global editor state including the buffer, cursor position, scroll
 * offsets, screen dimensions, and UI settings. */
struct editor_state {
	/* The text buffer being edited. */
	struct buffer buffer;

	/* Cursor position as line index (0-based). */
	uint32_t cursor_row;

	/* Cursor position as cell index within the line (0-based). */
	uint32_t cursor_column;

	/* First visible line (for vertical scrolling). */
	uint32_t row_offset;

	/* First visible column (for horizontal scrolling). */
	uint32_t column_offset;

	/* Number of text rows visible on screen (excludes status bars). */
	uint32_t screen_rows;

	/* Number of columns visible on screen. */
	uint32_t screen_columns;

	/* Width of the line number gutter in columns. */
	uint32_t gutter_width;

	/* Whether to display line numbers in the gutter. */
	bool show_line_numbers;

	/* Current status bar message text. */
	char status_message[256];

	/* When the status message was set (for timeout). */
	time_t status_message_time;

	/* Remaining Ctrl-Q presses needed to quit with unsaved changes. */
	int quit_confirm_counter;

	/* Selection anchor position (fixed point when extending selection). */
	uint32_t selection_anchor_row;
	uint32_t selection_anchor_column;

	/* Whether a selection is currently active. */
	bool selection_active;
};

/*****************************************************************************
 * Global State
 *****************************************************************************/

/* Saved terminal settings, restored when the editor exits. */
static struct termios original_terminal_settings;

/* The global editor state instance. */
static struct editor_state editor;

/* Flag set by SIGWINCH handler to indicate terminal was resized. */
static volatile sig_atomic_t terminal_resized = 0;

/* Mouse click tracking for double/triple-click detection. */
static time_t last_click_time = 0;
static uint32_t last_click_row = 0;
static uint32_t last_click_col = 0;
static int click_count = 0;

/* Scroll velocity tracking for adaptive scroll speed. */
static struct timespec last_scroll_time = {0, 0};
static double scroll_velocity = 0.0;
static int last_scroll_direction = 0;  /* -1 = up, 1 = down, 0 = none */

/* Internal clipboard buffer (fallback when system clipboard unavailable). */
static char *internal_clipboard = NULL;
static size_t internal_clipboard_length = 0;

/* Clipboard tool detection (cached on first use). */
enum clipboard_tool {
	CLIPBOARD_UNKNOWN = 0,
	CLIPBOARD_XCLIP,
	CLIPBOARD_XSEL,
	CLIPBOARD_WL,        /* wl-copy / wl-paste */
	CLIPBOARD_INTERNAL   /* Fallback */
};
static enum clipboard_tool detected_clipboard_tool = CLIPBOARD_UNKNOWN;

/* Forward declarations for functions used in input_read_key. */
static struct mouse_input input_parse_sgr_mouse(void);
static void editor_handle_mouse(struct mouse_input *mouse);

/*****************************************************************************
 * Neighbor Layer and Pair Entanglement
 *****************************************************************************/

/* Extract character class from neighbor field. */
static inline enum character_class neighbor_get_class(uint8_t neighbor)
{
	return (neighbor & NEIGHBOR_CLASS_MASK) >> NEIGHBOR_CLASS_SHIFT;
}

/* Extract token position from neighbor field. */
static inline enum token_position neighbor_get_position(uint8_t neighbor)
{
	return (neighbor & NEIGHBOR_POSITION_MASK) >> NEIGHBOR_POSITION_SHIFT;
}

/* Encode character class and token position into neighbor field. */
static inline uint8_t neighbor_encode(enum character_class class, enum token_position position)
{
	return (class << NEIGHBOR_CLASS_SHIFT) | (position << NEIGHBOR_POSITION_SHIFT);
}

/* Extract pair ID from context field. */
static inline uint32_t context_get_pair_id(uint32_t context)
{
	return context & CONTEXT_PAIR_ID_MASK;
}

/* Extract pair type from context field. */
static inline enum pair_type context_get_pair_type(uint32_t context)
{
	return (context & CONTEXT_PAIR_TYPE_MASK) >> CONTEXT_PAIR_TYPE_SHIFT;
}

/* Extract pair role from context field. */
static inline enum pair_role context_get_pair_role(uint32_t context)
{
	return (context & CONTEXT_PAIR_ROLE_MASK) >> CONTEXT_PAIR_ROLE_SHIFT;
}

/* Encode pair ID, type, and role into context field. */
static inline uint32_t context_encode(uint32_t pair_id, enum pair_type type, enum pair_role role)
{
	return (pair_id & CONTEXT_PAIR_ID_MASK) |
	       ((uint32_t)type << CONTEXT_PAIR_TYPE_SHIFT) |
	       ((uint32_t)role << CONTEXT_PAIR_ROLE_SHIFT);
}

/* Classify a codepoint into a character class. */
static enum character_class classify_codepoint(uint32_t cp)
{
	if (cp == ' ' || cp == '\t') {
		return CHAR_CLASS_WHITESPACE;
	}
	if (cp == '_') {
		return CHAR_CLASS_UNDERSCORE;
	}
	if (cp >= 'a' && cp <= 'z') {
		return CHAR_CLASS_LETTER;
	}
	if (cp >= 'A' && cp <= 'Z') {
		return CHAR_CLASS_LETTER;
	}
	if (cp >= '0' && cp <= '9') {
		return CHAR_CLASS_DIGIT;
	}
	if (cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
	    cp == '{' || cp == '}') {
		return CHAR_CLASS_BRACKET;
	}
	if (cp == '"' || cp == '\'' || cp == '`') {
		return CHAR_CLASS_QUOTE;
	}
	/* ASCII punctuation */
	if ((cp >= '!' && cp <= '/') || (cp >= ':' && cp <= '@') ||
	    (cp >= '[' && cp <= '`') || (cp >= '{' && cp <= '~')) {
		return CHAR_CLASS_PUNCTUATION;
	}
	/* Unicode letters (simplified - covers common ranges) */
	if (cp >= 0x00C0 && cp <= 0x024F) {
		return CHAR_CLASS_LETTER;  /* Latin Extended */
	}
	if (cp >= 0x0400 && cp <= 0x04FF) {
		return CHAR_CLASS_LETTER;  /* Cyrillic */
	}
	if (cp >= 0x4E00 && cp <= 0x9FFF) {
		return CHAR_CLASS_LETTER;  /* CJK */
	}

	return CHAR_CLASS_OTHER;
}

/* Check if two character classes form a word together. */
static bool classes_form_word(enum character_class a, enum character_class b)
{
	/* Letters, digits, and underscores form words together */
	bool a_is_word_char = (a == CHAR_CLASS_LETTER || a == CHAR_CLASS_DIGIT ||
	                       a == CHAR_CLASS_UNDERSCORE);
	bool b_is_word_char = (b == CHAR_CLASS_LETTER || b == CHAR_CLASS_DIGIT ||
	                       b == CHAR_CLASS_UNDERSCORE);
	return a_is_word_char && b_is_word_char;
}

/* Compute neighbor data (character class and token position) for a line. */
static void neighbor_compute_line(struct line *line)
{
	if (line->cell_count == 0) {
		return;
	}

	/* First pass: assign character classes */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		enum character_class class = classify_codepoint(line->cells[i].codepoint);
		line->cells[i].neighbor = neighbor_encode(class, TOKEN_POSITION_SOLO);
	}

	/* Second pass: compute token positions */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		enum character_class my_class = neighbor_get_class(line->cells[i].neighbor);

		bool has_prev = (i > 0);
		bool has_next = (i < line->cell_count - 1);

		enum character_class prev_class = has_prev ?
			neighbor_get_class(line->cells[i - 1].neighbor) : CHAR_CLASS_WHITESPACE;
		enum character_class next_class = has_next ?
			neighbor_get_class(line->cells[i + 1].neighbor) : CHAR_CLASS_WHITESPACE;

		bool joins_prev = has_prev && classes_form_word(prev_class, my_class);
		bool joins_next = has_next && classes_form_word(my_class, next_class);

		enum token_position position;
		if (!joins_prev && !joins_next) {
			position = TOKEN_POSITION_SOLO;
		} else if (!joins_prev && joins_next) {
			position = TOKEN_POSITION_START;
		} else if (joins_prev && joins_next) {
			position = TOKEN_POSITION_MIDDLE;
		} else {
			position = TOKEN_POSITION_END;
		}

		line->cells[i].neighbor = neighbor_encode(my_class, position);
	}
}

/* Is this cell at the start of a word? */
static bool cell_is_word_start(struct cell *cell)
{
	enum token_position pos = neighbor_get_position(cell->neighbor);
	return pos == TOKEN_POSITION_START || pos == TOKEN_POSITION_SOLO;
}

/* Is this cell at the end of a word? (Reserved for double-click selection) */
__attribute__((unused))
static bool cell_is_word_end(struct cell *cell)
{
	enum token_position pos = neighbor_get_position(cell->neighbor);
	return pos == TOKEN_POSITION_END || pos == TOKEN_POSITION_SOLO;
}

/* Find start of previous word. */
static uint32_t find_prev_word_start(struct line *line, uint32_t column)
{
	if (column == 0 || line->cell_count == 0) {
		return 0;
	}

	column--;

	/* Skip whitespace */
	while (column > 0 &&
	       neighbor_get_class(line->cells[column].neighbor) == CHAR_CLASS_WHITESPACE) {
		column--;
	}

	/* Find start of this word */
	while (column > 0 && !cell_is_word_start(&line->cells[column])) {
		column--;
	}

	return column;
}

/* Find start of next word. */
static uint32_t find_next_word_start(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return line->cell_count;
	}

	/* Move past current position */
	column++;

	/* Skip until we find a non-whitespace word start */
	while (column < line->cell_count) {
		if (neighbor_get_class(line->cells[column].neighbor) != CHAR_CLASS_WHITESPACE &&
		    cell_is_word_start(&line->cells[column])) {
			break;
		}
		column++;
	}

	return column;
}

/* Allocate a unique pair ID. */
static uint32_t buffer_allocate_pair_id(struct buffer *buffer)
{
	return ++buffer->next_pair_id;
}

/* Forward declarations for pair computation. */
static void line_warm(struct line *line, struct buffer *buffer);

/*
 * Scan the entire buffer to match pairs. This handles block comments,
 * and brackets: (), [], {}. Call after loading a file or after edits
 * that might affect pairs.
 */
static void buffer_compute_pairs(struct buffer *buffer)
{
	/* Reset all pair IDs and warm all lines first */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];
		if (line->temperature == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}
		for (uint32_t col = 0; col < line->cell_count; col++) {
			line->cells[col].context = 0;
		}
	}

	buffer->next_pair_id = 0;

	/* Stack for bracket matching */
	struct {
		uint32_t row;
		uint32_t col;
		uint32_t pair_id;
		enum pair_type type;
	} stack[256];
	int stack_top = 0;

	/* Are we inside a block comment? */
	bool in_block_comment = false;
	uint32_t comment_pair_id = 0;

	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		for (uint32_t col = 0; col < line->cell_count; col++) {
			uint32_t cp = line->cells[col].codepoint;

			/* Check for block comment start */
			if (!in_block_comment && cp == '/' && col + 1 < line->cell_count &&
			    line->cells[col + 1].codepoint == '*') {
				in_block_comment = true;
				comment_pair_id = buffer_allocate_pair_id(buffer);

				/* Mark the '/' and '*' as opener */
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_OPENER);
				col++;
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_OPENER);
				continue;
			}

			/* Check for block comment end */
			if (in_block_comment && cp == '*' && col + 1 < line->cell_count &&
			    line->cells[col + 1].codepoint == '/') {
				/* Mark the '*' and '/' as closer */
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_CLOSER);
				col++;
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_CLOSER);
				in_block_comment = false;
				continue;
			}

			/* Skip other processing if inside block comment */
			if (in_block_comment) {
				continue;
			}

			/* Opening brackets */
			if (cp == '(' || cp == '[' || cp == '{') {
				enum pair_type type = (cp == '(') ? PAIR_TYPE_PAREN :
				                      (cp == '[') ? PAIR_TYPE_BRACKET :
				                      PAIR_TYPE_BRACE;
				uint32_t pair_id = buffer_allocate_pair_id(buffer);

				line->cells[col].context = context_encode(pair_id, type,
					PAIR_ROLE_OPENER);

				if (stack_top < 256) {
					stack[stack_top].row = row;
					stack[stack_top].col = col;
					stack[stack_top].pair_id = pair_id;
					stack[stack_top].type = type;
					stack_top++;
				}
				continue;
			}

			/* Closing brackets */
			if (cp == ')' || cp == ']' || cp == '}') {
				enum pair_type type = (cp == ')') ? PAIR_TYPE_PAREN :
				                      (cp == ']') ? PAIR_TYPE_BRACKET :
				                      PAIR_TYPE_BRACE;

				/* Find matching opener on stack */
				int match = -1;
				for (int i = stack_top - 1; i >= 0; i--) {
					if (stack[i].type == type) {
						match = i;
						break;
					}
				}

				if (match >= 0) {
					uint32_t pair_id = stack[match].pair_id;
					line->cells[col].context = context_encode(pair_id, type,
						PAIR_ROLE_CLOSER);

					/* Remove from stack (and any unmatched openers above it) */
					stack_top = match;
				} else {
					/* Unmatched closer */
					line->cells[col].context = 0;
				}
				continue;
			}
		}
	}
}

/*
 * Given a cell with a pair context, find its matching partner.
 * Returns true if found, with partner position in out_row/out_col.
 */
static bool buffer_find_pair_partner(struct buffer *buffer,
                                     uint32_t row, uint32_t col,
                                     uint32_t *out_row, uint32_t *out_col)
{
	if (row >= buffer->line_count) {
		return false;
	}

	struct line *line = &buffer->lines[row];
	if (col >= line->cell_count) {
		return false;
	}

	uint32_t context = line->cells[col].context;
	uint32_t pair_id = context_get_pair_id(context);
	enum pair_role role = context_get_pair_role(context);

	if (pair_id == 0 || role == PAIR_ROLE_NONE) {
		return false;
	}

	/* Search direction depends on role */
	bool search_forward = (role == PAIR_ROLE_OPENER);

	if (search_forward) {
		/* Search forward for closer */
		for (uint32_t r = row; r < buffer->line_count; r++) {
			struct line *search_line = &buffer->lines[r];
			if (search_line->temperature == LINE_TEMPERATURE_COLD) {
				line_warm(search_line, buffer);
			}

			uint32_t start_col = (r == row) ? col + 1 : 0;
			for (uint32_t c = start_col; c < search_line->cell_count; c++) {
				if (context_get_pair_id(search_line->cells[c].context) == pair_id &&
				    context_get_pair_role(search_line->cells[c].context) == PAIR_ROLE_CLOSER) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	} else {
		/* Search backward for opener */
		for (int r = row; r >= 0; r--) {
			struct line *search_line = &buffer->lines[r];
			if (search_line->temperature == LINE_TEMPERATURE_COLD) {
				line_warm(search_line, buffer);
			}

			int start_col = (r == (int)row) ? (int)col - 1 :
				(int)search_line->cell_count - 1;
			for (int c = start_col; c >= 0; c--) {
				if (context_get_pair_id(search_line->cells[c].context) == pair_id &&
				    context_get_pair_role(search_line->cells[c].context) == PAIR_ROLE_OPENER) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	}

	return false;
}

/*
 * Check if a position is inside a block comment by examining pair context.
 * Scans backward for an unclosed comment opener.
 */
static bool syntax_is_in_block_comment(struct buffer *buffer, uint32_t row, uint32_t col)
{
	/* Scan backwards for an unclosed block comment opener */
	for (int r = row; r >= 0; r--) {
		struct line *line = &buffer->lines[r];
		if (line->temperature == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}

		int end_col = (r == (int)row) ? (int)col - 1 : (int)line->cell_count - 1;

		for (int c = end_col; c >= 0; c--) {
			uint32_t context = line->cells[c].context;
			enum pair_type type = context_get_pair_type(context);
			enum pair_role role = context_get_pair_role(context);

			if (type == PAIR_TYPE_COMMENT) {
				if (role == PAIR_ROLE_CLOSER) {
					/* Found a closer before us, so we're not in that comment */
					return false;
				}
				if (role == PAIR_ROLE_OPENER) {
					/* Found an opener - check if it closes after our position */
					uint32_t partner_row, partner_col;
					if (buffer_find_pair_partner(buffer, r, c,
					                             &partner_row, &partner_col)) {
						/* Has a closer - are we before it? */
						if (partner_row > row ||
						    (partner_row == row && partner_col >= col)) {
							return true;  /* We're inside this comment */
						}
						/* Closer is before us, keep searching */
					} else {
						/* Unclosed comment - we're inside it */
						return true;
					}
				}
			}
		}
	}

	return false;
}

/*****************************************************************************
 * Syntax Highlighting
 *****************************************************************************/

/* C language keywords - control flow and declarations. */
static const char *C_KEYWORDS[] = {
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "goto", "sizeof", "typedef",
	"struct", "union", "enum", "static", "const", "volatile",
	"extern", "register", "inline", "restrict", "_Atomic", "_Noreturn",
	NULL
};

/* C language type names and common typedefs. */
static const char *C_TYPES[] = {
	"int", "char", "short", "long", "float", "double", "void",
	"signed", "unsigned", "bool", "true", "false", "NULL",
	"int8_t", "int16_t", "int32_t", "int64_t",
	"uint8_t", "uint16_t", "uint32_t", "uint64_t",
	"size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
	"FILE", "va_list",
	NULL
};

/* Returns true if codepoint is an ASCII letter. */
static bool syntax_is_alpha(uint32_t cp)
{
	return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

/* Returns true if codepoint is an ASCII digit. */
static bool syntax_is_digit(uint32_t cp)
{
	return cp >= '0' && cp <= '9';
}

/* Returns true if codepoint is alphanumeric or underscore. */
static bool syntax_is_alnum(uint32_t cp)
{
	return syntax_is_alpha(cp) || syntax_is_digit(cp) || cp == '_';
}

/* Returns true if codepoint could be part of a number literal. */
static bool syntax_is_number_char(uint32_t cp)
{
	return syntax_is_digit(cp) || cp == '.' || cp == 'x' || cp == 'X' ||
	       (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F') ||
	       cp == 'u' || cp == 'U' || cp == 'l' || cp == 'L';
}

/* Returns true if codepoint is a C operator. */
static bool syntax_is_operator(uint32_t cp)
{
	return cp == '+' || cp == '-' || cp == '*' || cp == '/' ||
	       cp == '=' || cp == '<' || cp == '>' || cp == '!' ||
	       cp == '&' || cp == '|' || cp == '^' || cp == '~' ||
	       cp == '%' || cp == '?' || cp == ':' || cp == ';' ||
	       cp == ',' || cp == '.';
}

/* Returns true if codepoint is a bracket character. */
static bool syntax_is_bracket(uint32_t cp)
{
	return cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
	       cp == '{' || cp == '}';
}

/* Returns true if position is at line start (only whitespace before). */
static bool syntax_is_line_start(struct line *line, uint32_t pos)
{
	for (uint32_t i = 0; i < pos; i++) {
		uint32_t cp = line->cells[i].codepoint;
		if (cp != ' ' && cp != '\t') {
			return false;
		}
	}
	return true;
}

/* Extracts a word from cells into a buffer. Only ASCII characters. */
static void syntax_extract_word(struct line *line, uint32_t start, uint32_t end,
                                char *buffer, size_t buffer_size)
{
	size_t len = 0;
	for (uint32_t i = start; i < end && len < buffer_size - 1; i++) {
		uint32_t cp = line->cells[i].codepoint;
		if (cp < 128) {
			buffer[len++] = (char)cp;
		}
	}
	buffer[len] = '\0';
}

/* Returns true if word is a C keyword. */
static bool syntax_is_keyword(const char *word)
{
	for (int i = 0; C_KEYWORDS[i] != NULL; i++) {
		if (strcmp(word, C_KEYWORDS[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns true if word is a C type name. */
static bool syntax_is_type(const char *word)
{
	for (int i = 0; C_TYPES[i] != NULL; i++) {
		if (strcmp(word, C_TYPES[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns true if filename has a C/C++ extension. */
static bool syntax_is_c_file(const char *filename)
{
	if (filename == NULL) {
		return false;
	}

	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return false;
	}

	const char *ext = dot + 1;
	return strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0 ||
	       strcmp(ext, "cpp") == 0 || strcmp(ext, "hpp") == 0 ||
	       strcmp(ext, "cc") == 0 || strcmp(ext, "cxx") == 0;
}

/*
 * Apply syntax highlighting to a single line. Tokenizes the line and sets
 * the syntax field of each cell. Uses pair context for multiline comments.
 */
static void syntax_highlight_line(struct line *line, struct buffer *buffer,
                                  uint32_t row)
{
	/* Only highlight C files */
	if (!syntax_is_c_file(buffer->filename)) {
		return;
	}

	/* Must be warm/hot to highlight */
	if (line->temperature == LINE_TEMPERATURE_COLD) {
		return;
	}

	/* Reset all cells to normal */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		line->cells[i].syntax = SYNTAX_NORMAL;
	}

	/* Check if line starts inside a block comment */
	bool in_block_comment = syntax_is_in_block_comment(buffer, row, 0);

	uint32_t i = 0;
	while (i < line->cell_count) {
		uint32_t cp = line->cells[i].codepoint;

		/* If inside block comment, everything is comment until we exit */
		if (in_block_comment) {
			line->cells[i].syntax = SYNTAX_COMMENT;

			/* Check for block comment end */
			if (cp == '*' && i + 1 < line->cell_count &&
			    line->cells[i + 1].codepoint == '/') {
				line->cells[i].syntax = SYNTAX_COMMENT;
				i++;
				line->cells[i].syntax = SYNTAX_COMMENT;
				i++;
				in_block_comment = false;
				continue;
			}
			i++;
			continue;
		}

		/* Check for // comment */
		if (cp == '/' && i + 1 < line->cell_count &&
		    line->cells[i + 1].codepoint == '/') {
			while (i < line->cell_count) {
				line->cells[i].syntax = SYNTAX_COMMENT;
				i++;
			}
			break;
		}

		/* Check for block comment start */
		if (cp == '/' && i + 1 < line->cell_count &&
		    line->cells[i + 1].codepoint == '*') {
			in_block_comment = true;
			line->cells[i].syntax = SYNTAX_COMMENT;
			i++;
			line->cells[i].syntax = SYNTAX_COMMENT;
			i++;
			continue;
		}

		/* Check for string literal */
		if (cp == '"' || cp == '\'') {
			uint32_t quote = cp;
			line->cells[i].syntax = SYNTAX_STRING;
			i++;
			while (i < line->cell_count) {
				cp = line->cells[i].codepoint;
				if (cp == '\\' && i + 1 < line->cell_count) {
					line->cells[i].syntax = SYNTAX_ESCAPE;
					i++;
					line->cells[i].syntax = SYNTAX_ESCAPE;
					i++;
					continue;
				}
				line->cells[i].syntax = SYNTAX_STRING;
				if (cp == quote) {
					i++;
					break;
				}
				i++;
			}
			continue;
		}

		/* Check for preprocessor directive */
		if (cp == '#' && syntax_is_line_start(line, i)) {
			while (i < line->cell_count) {
				line->cells[i].syntax = SYNTAX_PREPROCESSOR;
				i++;
			}
			break;
		}

		/* Check for number literal */
		if (syntax_is_digit(cp) ||
		    (cp == '.' && i + 1 < line->cell_count &&
		     syntax_is_digit(line->cells[i + 1].codepoint))) {
			while (i < line->cell_count &&
			       syntax_is_number_char(line->cells[i].codepoint)) {
				line->cells[i].syntax = SYNTAX_NUMBER;
				i++;
			}
			continue;
		}

		/* Check for identifier (keyword, type, or function) */
		if (syntax_is_alpha(cp) || cp == '_') {
			uint32_t start = i;
			while (i < line->cell_count &&
			       syntax_is_alnum(line->cells[i].codepoint)) {
				i++;
			}

			/* Extract word and classify */
			char word[64];
			syntax_extract_word(line, start, i, word, sizeof(word));

			enum syntax_token type = SYNTAX_NORMAL;
			if (syntax_is_keyword(word)) {
				type = SYNTAX_KEYWORD;
			} else if (syntax_is_type(word)) {
				type = SYNTAX_TYPE;
			} else {
				/* Check if followed by '(' - it's a function */
				uint32_t j = i;
				while (j < line->cell_count &&
				       (line->cells[j].codepoint == ' ' ||
				        line->cells[j].codepoint == '\t')) {
					j++;
				}
				if (j < line->cell_count &&
				    line->cells[j].codepoint == '(') {
					type = SYNTAX_FUNCTION;
				}
			}

			for (uint32_t j = start; j < i; j++) {
				line->cells[j].syntax = type;
			}
			continue;
		}

		/* Check for operator */
		if (syntax_is_operator(cp)) {
			line->cells[i].syntax = SYNTAX_OPERATOR;
			i++;
			continue;
		}

		/* Check for bracket */
		if (syntax_is_bracket(cp)) {
			line->cells[i].syntax = SYNTAX_BRACKET;
			i++;
			continue;
		}

		/* Default: skip */
		i++;
	}
}

/*****************************************************************************
 * Terminal Handling
 *****************************************************************************/

/* Forward declaration for mouse cleanup. */
static void terminal_disable_mouse(void);

/* Restores the terminal to its original settings. Called automatically
 * at exit via atexit() to ensure the terminal is usable after the editor. */
static void terminal_disable_raw_mode(void)
{
	terminal_disable_mouse();
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_settings);
}

/* Puts the terminal into raw mode for character-by-character input.
 * Disables echo, canonical mode, and signal processing. Registers
 * terminal_disable_raw_mode() to run at exit. */
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

/* Signal handler for SIGWINCH (terminal resize). Sets a flag that the
 * main loop checks to update screen dimensions. */
static void terminal_handle_resize(int signal)
{
	(void)signal;
	terminal_resized = 1;
}

/* Queries the terminal for its current size in rows and columns.
 * Returns true on success, false if the size could not be determined. */
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

/* Clears the entire screen and moves the cursor to the home position. */
static void terminal_clear_screen(void)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/* Enables mouse tracking using SGR extended mode. This allows us to receive
 * click, drag, and scroll events with coordinates that work beyond column 223. */
static void terminal_enable_mouse(void)
{
	write(STDOUT_FILENO, "\x1b[?1000h", 8);  /* Enable button events */
	write(STDOUT_FILENO, "\x1b[?1002h", 8);  /* Enable button + drag events */
	write(STDOUT_FILENO, "\x1b[?1006h", 8);  /* Enable SGR extended mode */
}

/* Disables mouse tracking. Called at cleanup to restore terminal state. */
static void terminal_disable_mouse(void)
{
	write(STDOUT_FILENO, "\x1b[?1006l", 8);
	write(STDOUT_FILENO, "\x1b[?1002l", 8);
	write(STDOUT_FILENO, "\x1b[?1000l", 8);
}

/*****************************************************************************
 * Output Buffer
 *****************************************************************************/

/* Initializes an output buffer with starting capacity. */
static void output_buffer_init(struct output_buffer *output)
{
	output->data = malloc(INITIAL_OUTPUT_CAPACITY);
	output->length = 0;
	output->capacity = INITIAL_OUTPUT_CAPACITY;
}

/* Appends bytes to the output buffer, growing it if necessary. */
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

/* Appends a null-terminated string to the output buffer. */
static void output_buffer_append_string(struct output_buffer *output, const char *text)
{
	output_buffer_append(output, text, strlen(text));
}

/* Writes all buffered data to stdout and resets the buffer length. */
static void output_buffer_flush(struct output_buffer *output)
{
	write(STDOUT_FILENO, output->data, output->length);
	output->length = 0;
}

/* Frees the output buffer's memory and resets all fields. */
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

/* Reads a single keypress from stdin, handling escape sequences and UTF-8.
 * Returns the key code (positive for characters/codepoints, negative for
 * special keys like arrows). Returns -1 on error, -2 on terminal resize. */
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
				} else if (sequence[2] == ';') {
					/* Modified key sequences */
					char modifier, final;
					if (read(STDIN_FILENO, &modifier, 1) != 1) {
						return '\x1b';
					}
					if (read(STDIN_FILENO, &final, 1) != 1) {
						return '\x1b';
					}
					if (sequence[1] == '1') {
						/* \x1b[1;{mod}{key} - modified arrow/Home/End */
						if (modifier == '2') {  /* Shift */
							switch (final) {
								case 'A': return KEY_SHIFT_ARROW_UP;
								case 'B': return KEY_SHIFT_ARROW_DOWN;
								case 'C': return KEY_SHIFT_ARROW_RIGHT;
								case 'D': return KEY_SHIFT_ARROW_LEFT;
								case 'H': return KEY_SHIFT_HOME;
								case 'F': return KEY_SHIFT_END;
							}
						} else if (modifier == '5') {  /* Ctrl */
							switch (final) {
								case 'C': return KEY_CTRL_ARROW_RIGHT;
								case 'D': return KEY_CTRL_ARROW_LEFT;
							}
						} else if (modifier == '6') {  /* Ctrl+Shift */
							switch (final) {
								case 'C': return KEY_CTRL_SHIFT_ARROW_RIGHT;
								case 'D': return KEY_CTRL_SHIFT_ARROW_LEFT;
							}
						}
					} else if ((sequence[1] == '5' || sequence[1] == '6') &&
					           modifier == '2' && final == '~') {
						/* \x1b[5;2~ or \x1b[6;2~ - Shift+PageUp/Down */
						if (sequence[1] == '5') return KEY_SHIFT_PAGE_UP;
						if (sequence[1] == '6') return KEY_SHIFT_PAGE_DOWN;
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
			} else if (sequence[1] == '<') {
				/* SGR mouse event: \x1b[<button;column;row{M|m} */
				struct mouse_input mouse = input_parse_sgr_mouse();
				if (mouse.event != MOUSE_NONE) {
					editor_handle_mouse(&mouse);
				}
				return KEY_MOUSE_EVENT;
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

/* Initializes a line as hot with empty cell array. New lines start hot
 * since they have no mmap backing. */
static void line_init(struct line *line)
{
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
	line->mmap_offset = 0;
	line->mmap_length = 0;
	line->temperature = LINE_TEMPERATURE_HOT;
}

/* Frees all memory associated with a line and resets its fields. */
static void line_free(struct line *line)
{
	free(line->cells);
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
	line->mmap_offset = 0;
	line->mmap_length = 0;
	line->temperature = LINE_TEMPERATURE_COLD;
}

/* Ensures the line can hold at least 'required' cells, reallocating if needed. */
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

/* Inserts a cell with the given codepoint at the specified position.
 * Shifts existing cells to the right. Position is clamped to cell_count. */
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

/* Deletes the cell at the specified position, shifting cells left. */
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

/* Appends a cell with the given codepoint to the end of the line. */
static void line_append_cell(struct line *line, uint32_t codepoint)
{
	line_insert_cell(line, line->cell_count, codepoint);
}

/* Appends all cells from src line to the end of dest line. Both lines
 * must already be warm or hot. */
static void line_append_cells_from_line(struct line *dest, struct line *src)
{
	for (uint32_t i = 0; i < src->cell_count; i++) {
		line_append_cell(dest, src->cells[i].codepoint);
	}
}

/*
 * Warms a cold line by decoding its UTF-8 content from mmap into cells.
 * Does nothing if the line is already warm or hot. After warming, the
 * line's cells array contains the decoded codepoints and syntax highlighting
 * is applied.
 */
static void line_warm(struct line *line, struct buffer *buffer)
{
	if (line->temperature != LINE_TEMPERATURE_COLD) {
		return;
	}

	const char *text = buffer->mmap_base + line->mmap_offset;
	size_t length = line->mmap_length;

	/* Decode UTF-8 to cells */
	size_t offset = 0;
	while (offset < length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, length - offset, &codepoint);
		line_append_cell(line, codepoint);
		offset += bytes;
	}

	line->temperature = LINE_TEMPERATURE_WARM;

	/* Compute neighbor data for word boundaries */
	neighbor_compute_line(line);

	/* Note: syntax highlighting is called separately after pairs are computed */
}

/*
 * Get the cell count for a line. For cold lines, counts codepoints without
 * allocating cells. For warm/hot lines, returns the stored count.
 */
static uint32_t line_get_cell_count(struct line *line, struct buffer *buffer)
{
	if (line->temperature == LINE_TEMPERATURE_COLD) {
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

/* Returns true if the codepoint is a combining mark (zero-width character
 * that modifies the previous base character, like accents). */
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

/* Moves the cursor left to the start of the previous grapheme cluster,
 * skipping over any combining marks. Warms the line if cold. */
static uint32_t cursor_prev_grapheme(struct line *line, struct buffer *buffer, uint32_t column)
{
	line_warm(line, buffer);

	if (column == 0 || line->cell_count == 0) {
		return 0;
	}

	column--;
	while (column > 0 && codepoint_is_combining_mark(line->cells[column].codepoint)) {
		column--;
	}

	return column;
}

/* Moves the cursor right to the start of the next grapheme cluster,
 * skipping over any combining marks. Warms the line if cold. */
static uint32_t cursor_next_grapheme(struct line *line, struct buffer *buffer, uint32_t column)
{
	line_warm(line, buffer);

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

/* Initializes a buffer with starting line capacity and no file. */
static void buffer_init(struct buffer *buffer)
{
	buffer->lines = malloc(INITIAL_BUFFER_CAPACITY * sizeof(struct line));
	buffer->line_count = 0;
	buffer->line_capacity = INITIAL_BUFFER_CAPACITY;
	buffer->filename = NULL;
	buffer->is_modified = false;
	buffer->file_descriptor = -1;
	buffer->mmap_base = NULL;
	buffer->mmap_size = 0;
}

/* Frees all lines and memory associated with the buffer, including
 * unmapping any memory-mapped file. */
static void buffer_free(struct buffer *buffer)
{
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

/* Ensures the buffer can hold at least 'required' lines. */
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

/* Inserts a new empty line at the specified position. */
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

/* Deletes the line at the specified position, freeing its memory. */
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

/* Inserts a single codepoint at the specified row and column. Warms the
 * line if cold and marks it as hot since content is being modified. */
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
	line_warm(line, buffer);
	line_insert_cell(line, column, codepoint);
	line->temperature = LINE_TEMPERATURE_HOT;
	buffer->is_modified = true;

	/* Recompute neighbors for this line */
	neighbor_compute_line(line);

	/* Re-highlight the modified line */
	syntax_highlight_line(line, buffer, row);
}

/* Deletes the grapheme cluster at the specified position. If at end of line,
 * joins with the next line instead. Warms lines and marks as hot. */
static void buffer_delete_grapheme_at_column(struct buffer *buffer, uint32_t row, uint32_t column)
{
	if (row >= buffer->line_count) {
		return;
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);

	if (column < line->cell_count) {
		/* Find the end of this grapheme (skip over combining marks) */
		uint32_t end = cursor_next_grapheme(line, buffer, column);
		/* Delete cells from end backwards */
		for (uint32_t i = end; i > column; i--) {
			line_delete_cell(line, column);
		}
		line->temperature = LINE_TEMPERATURE_HOT;
		buffer->is_modified = true;
		/* Recompute neighbors and re-highlight */
		neighbor_compute_line(line);
		syntax_highlight_line(line, buffer, row);
	} else if (row + 1 < buffer->line_count) {
		/* Join with next line */
		struct line *next_line = &buffer->lines[row + 1];
		line_warm(next_line, buffer);
		line_append_cells_from_line(line, next_line);
		line->temperature = LINE_TEMPERATURE_HOT;
		buffer_delete_line(buffer, row + 1);
		/* Recompute neighbors and re-highlight */
		neighbor_compute_line(line);
		syntax_highlight_line(line, buffer, row);
	}
}

/*
 * Split a line at the given column position, creating a new line.
 * The portion of the line after the cursor moves to the new line below.
 * If cursor is at end of line, creates an empty line. If row equals
 * line_count, appends a new empty line at the end. Warms line and marks hot.
 */
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
	line_warm(line, buffer);

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
		line->temperature = LINE_TEMPERATURE_HOT;

		/* Recompute neighbors and re-highlight both lines */
		neighbor_compute_line(line);
		neighbor_compute_line(new_line);
		syntax_highlight_line(line, buffer, row);
		syntax_highlight_line(new_line, buffer, row + 1);
	}
}

/*****************************************************************************
 * File Operations
 *****************************************************************************/

/* Forward declaration - status messages used by file operations */
static void editor_set_status_message(const char *format, ...);

/*
 * Build the line index by scanning the mmap for newlines. Each line is
 * created as cold with just offset and length - no cells allocated.
 */
static void file_build_line_index(struct buffer *buffer)
{
	if (buffer->mmap_size == 0) {
		/* Empty file - no lines to index */
		return;
	}

	size_t line_start = 0;

	for (size_t i = 0; i <= buffer->mmap_size; i++) {
		bool is_newline = (i < buffer->mmap_size && buffer->mmap_base[i] == '\n');
		bool is_eof = (i == buffer->mmap_size);

		if (is_newline || is_eof) {
			/* Found end of line - strip trailing CR if present */
			size_t line_end = i;
			if (line_end > line_start && buffer->mmap_base[line_end - 1] == '\r') {
				line_end--;
			}

			buffer_ensure_capacity(buffer, buffer->line_count + 1);

			struct line *line = &buffer->lines[buffer->line_count];
			line->cells = NULL;
			line->cell_count = 0;
			line->cell_capacity = 0;
			line->mmap_offset = line_start;
			line->mmap_length = line_end - line_start;
			line->temperature = LINE_TEMPERATURE_COLD;

			buffer->line_count++;
			line_start = i + 1;
		}
	}
}

/*
 * Load a file from disk into a buffer using mmap. The file is memory-mapped
 * and lines are created as cold references into the mmap. Only lines that
 * are accessed will have cells allocated. Returns true on success, false
 * if the file couldn't be opened.
 */
static bool file_open(struct buffer *buffer, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		return false;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return false;
	}

	size_t file_size = st.st_size;
	char *mapped = NULL;

	if (file_size > 0) {
		mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mapped == MAP_FAILED) {
			close(fd);
			return false;
		}
		/* Hint: we'll access this randomly as user scrolls */
		madvise(mapped, file_size, MADV_RANDOM);
	}

	buffer->file_descriptor = fd;
	buffer->mmap_base = mapped;
	buffer->mmap_size = file_size;

	/* Build line index by scanning for newlines */
	file_build_line_index(buffer);

	buffer->filename = strdup(filename);
	buffer->is_modified = false;

	/* Compute pairs across entire buffer (warms all lines) */
	buffer_compute_pairs(buffer);

	/* Apply syntax highlighting with pair context */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		syntax_highlight_line(&buffer->lines[row], buffer, row);
	}

	return true;
}

/*
 * Write a buffer's contents to disk. All lines are warmed first since
 * opening the file for writing will invalidate any mmap. The mmap is
 * then unmapped before writing. Updates the status message with bytes
 * written. Returns true on success, false if save fails.
 */
static bool file_save(struct buffer *buffer)
{
	if (buffer->filename == NULL) {
		return false;
	}

	/* Warm all cold lines before we invalidate the mmap */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		line_warm(&buffer->lines[row], buffer);
	}

	/* Unmap file before overwriting - opening with "w" truncates it */
	if (buffer->mmap_base != NULL) {
		munmap(buffer->mmap_base, buffer->mmap_size);
		buffer->mmap_base = NULL;
		buffer->mmap_size = 0;
	}
	if (buffer->file_descriptor >= 0) {
		close(buffer->file_descriptor);
		buffer->file_descriptor = -1;
	}

	FILE *file = fopen(buffer->filename, "w");
	if (file == NULL) {
		return false;
	}

	size_t total_bytes = 0;

	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		/* All lines are warm/hot now - write from cells */
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

/*
 * Initialize the global editor state. Sets up an empty buffer and zeroes
 * all cursor positions, scroll offsets, and screen dimensions. Line numbers
 * are shown by default. The quit confirmation counter starts at its maximum.
 */
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
	editor.selection_anchor_row = 0;
	editor.selection_anchor_column = 0;
	editor.selection_active = false;
}

/* Start a new selection at the current cursor position. The anchor is set
 * to the cursor position and selection becomes active. */
static void selection_start(void)
{
	editor.selection_anchor_row = editor.cursor_row;
	editor.selection_anchor_column = editor.cursor_column;
	editor.selection_active = true;
}

/* Clear the current selection. After this, no text is selected. */
static void selection_clear(void)
{
	editor.selection_active = false;
}

/* Get the normalized selection range (start always before end). The selection
 * spans from (start_row, start_col) to (end_row, end_col) where start <= end. */
static void selection_get_range(uint32_t *start_row, uint32_t *start_col,
                                uint32_t *end_row, uint32_t *end_col)
{
	uint32_t anchor_row = editor.selection_anchor_row;
	uint32_t anchor_col = editor.selection_anchor_column;
	uint32_t cursor_row = editor.cursor_row;
	uint32_t cursor_col = editor.cursor_column;

	if (anchor_row < cursor_row ||
	    (anchor_row == cursor_row && anchor_col <= cursor_col)) {
		*start_row = anchor_row;
		*start_col = anchor_col;
		*end_row = cursor_row;
		*end_col = cursor_col;
	} else {
		*start_row = cursor_row;
		*start_col = cursor_col;
		*end_row = anchor_row;
		*end_col = anchor_col;
	}
}

/* Check if a cell position is within the selection. Returns false if no
 * selection is active or if the position is outside the selected range. */
static bool selection_contains(uint32_t row, uint32_t column)
{
	if (!editor.selection_active) {
		return false;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Empty selection */
	if (start_row == end_row && start_col == end_col) {
		return false;
	}

	if (row < start_row || row > end_row) {
		return false;
	}

	if (row == start_row && row == end_row) {
		/* Single line selection */
		return column >= start_col && column < end_col;
	}

	if (row == start_row) {
		return column >= start_col;
	}

	if (row == end_row) {
		return column < end_col;
	}

	/* Row is between start and end */
	return true;
}

/* Check if the current selection is empty (anchor equals cursor). */
static bool selection_is_empty(void)
{
	if (!editor.selection_active) {
		return true;
	}
	return editor.selection_anchor_row == editor.cursor_row &&
	       editor.selection_anchor_column == editor.cursor_column;
}

/*****************************************************************************
 * Clipboard Integration
 *****************************************************************************/

/*
 * Detect which clipboard tool is available on the system.
 * Checks for wl-copy (Wayland), xclip, and xsel in order of preference.
 * Result is cached after first call.
 */
static enum clipboard_tool clipboard_detect_tool(void)
{
	if (detected_clipboard_tool != CLIPBOARD_UNKNOWN) {
		return detected_clipboard_tool;
	}

	/* Check for Wayland first if WAYLAND_DISPLAY is set */
	if (getenv("WAYLAND_DISPLAY") != NULL) {
		if (system("command -v wl-copy >/dev/null 2>&1") == 0) {
			detected_clipboard_tool = CLIPBOARD_WL;
			return detected_clipboard_tool;
		}
	}

	/* Check for X11 tools */
	if (system("command -v xclip >/dev/null 2>&1") == 0) {
		detected_clipboard_tool = CLIPBOARD_XCLIP;
		return detected_clipboard_tool;
	}

	if (system("command -v xsel >/dev/null 2>&1") == 0) {
		detected_clipboard_tool = CLIPBOARD_XSEL;
		return detected_clipboard_tool;
	}

	detected_clipboard_tool = CLIPBOARD_INTERNAL;
	return detected_clipboard_tool;
}

/*
 * Copy the given text to the system clipboard. Falls back to internal
 * buffer if no clipboard tool is available. Returns true on success.
 */
static bool clipboard_copy(const char *text, size_t length)
{
	if (text == NULL || length == 0) {
		return false;
	}

	enum clipboard_tool tool = clipboard_detect_tool();

	if (tool == CLIPBOARD_INTERNAL) {
		/* Use internal buffer */
		free(internal_clipboard);
		internal_clipboard = malloc(length + 1);
		if (internal_clipboard == NULL) {
			return false;
		}
		memcpy(internal_clipboard, text, length);
		internal_clipboard[length] = '\0';
		internal_clipboard_length = length;
		return true;
	}

	/* Use system clipboard */
	const char *command = NULL;
	switch (tool) {
		case CLIPBOARD_XCLIP:
			command = "xclip -selection clipboard";
			break;
		case CLIPBOARD_XSEL:
			command = "xsel --clipboard --input";
			break;
		case CLIPBOARD_WL:
			command = "wl-copy";
			break;
		default:
			return false;
	}

	FILE *pipe = popen(command, "w");
	if (pipe == NULL) {
		return false;
	}

	size_t written = fwrite(text, 1, length, pipe);
	int status = pclose(pipe);

	return written == length && status == 0;
}

/*
 * Paste from the system clipboard. Returns a newly allocated string
 * that the caller must free, or NULL on failure. Sets *out_length
 * to the length of the returned string (excluding null terminator).
 */
static char *clipboard_paste(size_t *out_length)
{
	enum clipboard_tool tool = clipboard_detect_tool();

	if (tool == CLIPBOARD_INTERNAL) {
		/* Use internal buffer */
		if (internal_clipboard == NULL || internal_clipboard_length == 0) {
			*out_length = 0;
			return NULL;
		}
		char *copy = malloc(internal_clipboard_length + 1);
		if (copy == NULL) {
			*out_length = 0;
			return NULL;
		}
		memcpy(copy, internal_clipboard, internal_clipboard_length);
		copy[internal_clipboard_length] = '\0';
		*out_length = internal_clipboard_length;
		return copy;
	}

	/* Use system clipboard */
	const char *command = NULL;
	switch (tool) {
		case CLIPBOARD_XCLIP:
			command = "xclip -selection clipboard -o";
			break;
		case CLIPBOARD_XSEL:
			command = "xsel --clipboard --output";
			break;
		case CLIPBOARD_WL:
			command = "wl-paste -n";  /* -n: no trailing newline */
			break;
		default:
			*out_length = 0;
			return NULL;
	}

	FILE *pipe = popen(command, "r");
	if (pipe == NULL) {
		*out_length = 0;
		return NULL;
	}

	/* Read clipboard contents dynamically */
	size_t capacity = 4096;
	size_t length = 0;
	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		pclose(pipe);
		*out_length = 0;
		return NULL;
	}

	while (!feof(pipe)) {
		if (length + 1024 > capacity) {
			capacity *= 2;
			char *new_buffer = realloc(buffer, capacity);
			if (new_buffer == NULL) {
				free(buffer);
				pclose(pipe);
				*out_length = 0;
				return NULL;
			}
			buffer = new_buffer;
		}

		size_t read_count = fread(buffer + length, 1, 1024, pipe);
		length += read_count;

		if (read_count == 0) {
			break;
		}
	}

	pclose(pipe);

	buffer[length] = '\0';
	*out_length = length;
	return buffer;
}

/*
 * Extract the currently selected text as a UTF-8 string.
 * Returns a newly allocated string that the caller must free,
 * or NULL if no selection. Sets *out_length to string length.
 */
static char *selection_get_text(size_t *out_length)
{
	if (!editor.selection_active || selection_is_empty()) {
		*out_length = 0;
		return NULL;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Estimate buffer size (4 bytes per codepoint max + newlines) */
	size_t capacity = 0;
	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);
		capacity += line->cell_count * 4 + 1;
	}
	capacity += 1;  /* Null terminator */

	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		*out_length = 0;
		return NULL;
	}

	size_t offset = 0;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		uint32_t col_start = (row == start_row) ? start_col : 0;
		uint32_t col_end = (row == end_row) ? end_col : line->cell_count;

		for (uint32_t col = col_start; col < col_end; col++) {
			uint32_t codepoint = line->cells[col].codepoint;
			char utf8[4];
			int bytes = utflite_encode(codepoint, utf8);
			if (bytes > 0) {
				memcpy(buffer + offset, utf8, bytes);
				offset += bytes;
			}
		}

		/* Add newline between lines (not after last line) */
		if (row < end_row) {
			buffer[offset++] = '\n';
		}
	}

	buffer[offset] = '\0';
	*out_length = offset;
	return buffer;
}

/*
 * Calculate the width of the line number gutter. The gutter expands to
 * fit the number of digits needed for the highest line number, with a
 * minimum of 2 digits plus one space for padding. Set to 0 when line
 * numbers are disabled.
 */
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

/*
 * Query the terminal size and update editor dimensions. Falls back to
 * 80x24 if the size cannot be determined. Reserves 2 rows at the bottom
 * for the status bar and message bar.
 */
static void editor_update_screen_size(void)
{
	if (!terminal_get_size(&editor.screen_rows, &editor.screen_columns)) {
		editor.screen_rows = 24;
		editor.screen_columns = 80;
	}
	/* Reserve space for status bar and message bar */
	editor.screen_rows -= 2;
}

/*
 * Set a formatted status message to display at the bottom of the screen.
 * Uses printf-style formatting. The message timestamp is recorded so it
 * can be cleared after a timeout.
 */
static void editor_set_status_message(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(editor.status_message, sizeof(editor.status_message), format, arguments);
	va_end(arguments);
	editor.status_message_time = time(NULL);
}

/*
 * Return the number of cells in a line. This is the logical length used
 * for cursor positioning, not the rendered width. For cold lines, counts
 * codepoints without allocating cells. Returns 0 for invalid row numbers.
 */
static uint32_t editor_get_line_length(uint32_t row)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}
	return line_get_cell_count(&editor.buffer.lines[row], &editor.buffer);
}

/*
 * Convert a cell column position to a rendered screen column. Accounts
 * for tab expansion and character display widths (CJK characters take 2
 * columns, combining marks take 0). Warms the line to access cells.
 */
static uint32_t editor_get_render_column(uint32_t row, uint32_t column)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);
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

/*
 * Adjust scroll offsets to keep the cursor visible on screen. Handles
 * both vertical scrolling (row_offset) and horizontal scrolling
 * (column_offset). Horizontal scroll uses rendered column to account
 * for variable-width characters and tabs.
 */
static void editor_scroll(void)
{
	/* Vertical scrolling (skip if selection active - allow cursor off-screen) */
	if (!editor.selection_active) {
		if (editor.cursor_row < editor.row_offset) {
			editor.row_offset = editor.cursor_row;
		}
		if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
			editor.row_offset = editor.cursor_row - editor.screen_rows + 1;
		}
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

/*
 * Handle cursor movement from arrow keys, Home, End, Page Up/Down.
 * Left/Right navigate by grapheme cluster within a line and wrap to
 * adjacent lines at boundaries. Up/Down move vertically. After movement,
 * the cursor is snapped to end of line if it would be past the line length.
 * Shift+key extends selection, plain movement clears selection.
 */
static void editor_move_cursor(int key)
{
	/* Determine if this is a selection-extending key and get base key */
	bool extend_selection = false;
	int base_key = key;

	switch (key) {
		case KEY_SHIFT_ARROW_UP:
			extend_selection = true;
			base_key = KEY_ARROW_UP;
			break;
		case KEY_SHIFT_ARROW_DOWN:
			extend_selection = true;
			base_key = KEY_ARROW_DOWN;
			break;
		case KEY_SHIFT_ARROW_LEFT:
			extend_selection = true;
			base_key = KEY_ARROW_LEFT;
			break;
		case KEY_SHIFT_ARROW_RIGHT:
			extend_selection = true;
			base_key = KEY_ARROW_RIGHT;
			break;
		case KEY_SHIFT_HOME:
			extend_selection = true;
			base_key = KEY_HOME;
			break;
		case KEY_SHIFT_END:
			extend_selection = true;
			base_key = KEY_END;
			break;
		case KEY_SHIFT_PAGE_UP:
			extend_selection = true;
			base_key = KEY_PAGE_UP;
			break;
		case KEY_SHIFT_PAGE_DOWN:
			extend_selection = true;
			base_key = KEY_PAGE_DOWN;
			break;
		case KEY_CTRL_SHIFT_ARROW_LEFT:
			extend_selection = true;
			base_key = KEY_CTRL_ARROW_LEFT;
			break;
		case KEY_CTRL_SHIFT_ARROW_RIGHT:
			extend_selection = true;
			base_key = KEY_CTRL_ARROW_RIGHT;
			break;
	}

	/* Handle selection start/clear */
	if (extend_selection) {
		if (!editor.selection_active) {
			selection_start();
		}
	} else {
		selection_clear();
	}

	uint32_t line_length = editor_get_line_length(editor.cursor_row);
	struct line *current_line = editor.cursor_row < editor.buffer.line_count
	                            ? &editor.buffer.lines[editor.cursor_row] : NULL;

	switch (base_key) {
		case KEY_ARROW_LEFT:
			if (editor.cursor_column > 0 && current_line) {
				editor.cursor_column = cursor_prev_grapheme(current_line, &editor.buffer, editor.cursor_column);
			} else if (editor.cursor_row > 0) {
				editor.cursor_row--;
				editor.cursor_column = editor_get_line_length(editor.cursor_row);
			}
			break;

		case KEY_ARROW_RIGHT:
			if (editor.cursor_column < line_length && current_line) {
				editor.cursor_column = cursor_next_grapheme(current_line, &editor.buffer, editor.cursor_column);
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

		case KEY_CTRL_ARROW_LEFT:
			if (current_line) {
				line_warm(current_line, &editor.buffer);
				uint32_t old_col = editor.cursor_column;
				editor.cursor_column = find_prev_word_start(current_line,
					editor.cursor_column);
				/* If stuck at start of line, wrap to previous line */
				if (editor.cursor_column == 0 && old_col == 0 &&
				    editor.cursor_row > 0) {
					editor.cursor_row--;
					struct line *prev = &editor.buffer.lines[editor.cursor_row];
					line_warm(prev, &editor.buffer);
					editor.cursor_column = prev->cell_count;
				}
			}
			break;

		case KEY_CTRL_ARROW_RIGHT:
			if (current_line) {
				line_warm(current_line, &editor.buffer);
				uint32_t old_col = editor.cursor_column;
				uint32_t len = current_line->cell_count;
				editor.cursor_column = find_next_word_start(current_line,
					editor.cursor_column);
				/* If stuck at end of line, wrap to next line */
				if (editor.cursor_column == len && old_col == len &&
				    editor.cursor_row < editor.buffer.line_count - 1) {
					editor.cursor_row++;
					editor.cursor_column = 0;
				}
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

/*
 * Delete the currently selected text. Handles both single-line and
 * multi-line selections. Moves cursor to the start of the deleted region.
 */
static void editor_delete_selection(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		return;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	if (start_row == end_row) {
		/* Single line selection - delete cells from end to start */
		struct line *line = &editor.buffer.lines[start_row];
		line_warm(line, &editor.buffer);

		for (uint32_t i = end_col; i > start_col; i--) {
			line_delete_cell(line, start_col);
		}
		line->temperature = LINE_TEMPERATURE_HOT;
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, start_row);
	} else {
		/* Multi-line selection */
		/* 1. Truncate start line at start_col */
		struct line *start_line = &editor.buffer.lines[start_row];
		line_warm(start_line, &editor.buffer);
		start_line->cell_count = start_col;

		/* 2. Append content after end_col from end line */
		struct line *end_line = &editor.buffer.lines[end_row];
		line_warm(end_line, &editor.buffer);

		for (uint32_t i = end_col; i < end_line->cell_count; i++) {
			line_append_cell(start_line, end_line->cells[i].codepoint);
		}

		/* 3. Delete lines from start_row+1 to end_row inclusive */
		for (uint32_t i = end_row; i > start_row; i--) {
			buffer_delete_line(&editor.buffer, i);
		}

		start_line->temperature = LINE_TEMPERATURE_HOT;
		neighbor_compute_line(start_line);
		buffer_compute_pairs(&editor.buffer);

		/* Re-highlight affected lines */
		for (uint32_t row = start_row; row < editor.buffer.line_count; row++) {
			if (editor.buffer.lines[row].temperature != LINE_TEMPERATURE_COLD) {
				syntax_highlight_line(&editor.buffer.lines[row], &editor.buffer, row);
			}
		}
	}

	/* Move cursor to start of deleted region */
	editor.cursor_row = start_row;
	editor.cursor_column = start_col;

	editor.buffer.is_modified = true;
	selection_clear();
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

/*
 * Copy the current selection to the clipboard without deleting it.
 */
static void editor_copy(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		editor_set_status_message("Nothing to copy");
		return;
	}

	size_t length;
	char *text = selection_get_text(&length);
	if (text == NULL) {
		editor_set_status_message("Copy failed");
		return;
	}

	if (clipboard_copy(text, length)) {
		editor_set_status_message("Copied %zu bytes", length);
	} else {
		editor_set_status_message("Copy to clipboard failed");
	}

	free(text);
}

/*
 * Cut the current selection: copy to clipboard and delete.
 */
static void editor_cut(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		editor_set_status_message("Nothing to cut");
		return;
	}

	size_t length;
	char *text = selection_get_text(&length);
	if (text == NULL) {
		editor_set_status_message("Cut failed");
		return;
	}

	if (clipboard_copy(text, length)) {
		editor_delete_selection();
		editor_set_status_message("Cut %zu bytes", length);
	} else {
		editor_set_status_message("Cut to clipboard failed");
	}

	free(text);
}

/*
 * Paste from clipboard at current cursor position.
 * If there's a selection, replaces it with pasted content.
 */
static void editor_paste(void)
{
	size_t length;
	char *text = clipboard_paste(&length);

	if (text == NULL || length == 0) {
		editor_set_status_message("Clipboard empty");
		free(text);
		return;
	}

	/* Delete selection if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	/* Track starting row for re-highlighting */
	uint32_t start_row = editor.cursor_row;

	/* Insert text character by character, handling newlines */
	size_t offset = 0;
	uint32_t chars_inserted = 0;

	while (offset < length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, length - offset, &codepoint);

		if (bytes <= 0) {
			/* Invalid UTF-8, skip one byte */
			offset++;
			continue;
		}

		if (codepoint == '\n') {
			buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
			editor.cursor_row++;
			editor.cursor_column = 0;
		} else if (codepoint == '\r') {
			/* Skip carriage returns (Windows line endings) */
		} else {
			buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row,
			                             editor.cursor_column, codepoint);
			editor.cursor_column++;
		}

		chars_inserted++;
		offset += bytes;
	}

	/* Recompute pairs and re-highlight affected lines */
	buffer_compute_pairs(&editor.buffer);
	for (uint32_t row = start_row; row <= editor.cursor_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		if (line->temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(line, &editor.buffer, row);
		}
	}

	free(text);
	editor.buffer.is_modified = true;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
	editor_set_status_message("Pasted %u characters", chars_inserted);
}

/*
 * Insert a character at the current cursor position and advance the
 * cursor. If there's an active selection, deletes it first. Resets
 * the quit confirmation counter since the buffer was modified.
 */
static void editor_insert_character(uint32_t codepoint)
{
	/* Delete selection first if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	editor.cursor_column++;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

/*
 * Handle Enter key by splitting the current line at the cursor position.
 * Moves cursor to the beginning of the newly created line below. If there's
 * an active selection, deletes it first.
 */
static void editor_insert_newline(void)
{
	/* Delete selection first if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	editor.cursor_row++;
	editor.cursor_column = 0;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

/*
 * Handle Delete key by removing the grapheme cluster at the cursor
 * position. If there's an active selection, deletes the selection instead.
 * Does nothing if cursor is past the end of the buffer.
 */
static void editor_delete_character(void)
{
	/* Delete selection if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
		return;
	}

	if (editor.cursor_row >= editor.buffer.line_count) {
		return;
	}

	buffer_delete_grapheme_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column);
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

/*
 * Handle Backspace key. If there's an active selection, deletes the
 * selection. Otherwise, if within a line, deletes the grapheme cluster
 * before the cursor. If at the start of a line, joins this line with
 * the previous line. Does nothing at the start of the buffer.
 */
static void editor_handle_backspace(void)
{
	/* Delete selection if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
		return;
	}

	if (editor.cursor_row == 0 && editor.cursor_column == 0) {
		return;
	}

	if (editor.cursor_column > 0) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		uint32_t new_column = cursor_prev_grapheme(line, &editor.buffer, editor.cursor_column);
		editor.cursor_column = new_column;
		buffer_delete_grapheme_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column);
	} else {
		/* Join with previous line */
		uint32_t previous_line_length = editor_get_line_length(editor.cursor_row - 1);
		struct line *previous_line = &editor.buffer.lines[editor.cursor_row - 1];
		struct line *current_line = &editor.buffer.lines[editor.cursor_row];
		line_warm(previous_line, &editor.buffer);
		line_warm(current_line, &editor.buffer);
		line_append_cells_from_line(previous_line, current_line);
		previous_line->temperature = LINE_TEMPERATURE_HOT;
		buffer_delete_line(&editor.buffer, editor.cursor_row);
		editor.cursor_row--;
		editor.cursor_column = previous_line_length;
	}

	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;
}

/*
 * Save the buffer to disk using Ctrl-S. Displays an error in the status
 * bar if no filename is set or if the save fails. Resets the quit
 * confirmation counter on success.
 */
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

/*
 * Convert a visual screen column to a cell index within a line. Accounts
 * for tab expansion and character widths. Used for mouse click positioning.
 */
static uint32_t screen_column_to_cell(uint32_t row, uint32_t target_visual_column)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	uint32_t visual_column = 0;
	uint32_t cell_index = 0;

	while (cell_index < line->cell_count && visual_column < target_visual_column) {
		uint32_t cp = line->cells[cell_index].codepoint;
		int width;

		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) {
				width = 1;
			}
		}

		/* If clicking in the middle of a wide character, round to nearest */
		if (visual_column + width > target_visual_column) {
			if (target_visual_column - visual_column > (uint32_t)width / 2) {
				cell_index++;
			}
			break;
		}

		visual_column += width;
		cell_index++;
	}

	return cell_index;
}

/*
 * Select the word at the given position using neighbor layer data.
 * Does nothing if position is whitespace.
 */
static void editor_select_word(uint32_t row, uint32_t column)
{
	if (row >= editor.buffer.line_count) {
		return;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	if (line->cell_count == 0) {
		return;
	}

	if (column >= line->cell_count) {
		column = line->cell_count - 1;
	}

	/* Use neighbor layer to find word boundaries */
	enum character_class click_class = neighbor_get_class(line->cells[column].neighbor);

	/* Don't select whitespace as a "word" */
	if (click_class == CHAR_CLASS_WHITESPACE) {
		editor.cursor_column = column;
		selection_clear();
		return;
	}

	/* Find word start */
	uint32_t word_start = column;
	while (word_start > 0) {
		enum character_class prev_class = neighbor_get_class(line->cells[word_start - 1].neighbor);
		if (!classes_form_word(prev_class, click_class) && prev_class != click_class) {
			break;
		}
		word_start--;
	}

	/* Find word end */
	uint32_t word_end = column;
	while (word_end < line->cell_count - 1) {
		enum character_class next_class = neighbor_get_class(line->cells[word_end + 1].neighbor);
		if (!classes_form_word(click_class, next_class) && next_class != click_class) {
			break;
		}
		word_end++;
	}
	word_end++;  /* End is exclusive */

	/* Set selection */
	editor.selection_anchor_row = row;
	editor.selection_anchor_column = word_start;
	editor.cursor_row = row;
	editor.cursor_column = word_end;
	editor.selection_active = true;
}

/*
 * Select an entire line including trailing newline conceptually.
 */
static void editor_select_line(uint32_t row)
{
	if (row >= editor.buffer.line_count) {
		return;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	editor.selection_anchor_row = row;
	editor.selection_anchor_column = 0;
	editor.cursor_row = row;
	editor.cursor_column = line->cell_count;
	editor.selection_active = true;
}

/*
 * Parse SGR mouse event after reading \x1b[<. Returns the parsed mouse
 * input or sets event to MOUSE_NONE on parse failure.
 */
static struct mouse_input input_parse_sgr_mouse(void)
{
	struct mouse_input mouse = {.event = MOUSE_NONE, .row = 0, .column = 0};
	char buffer[32];
	int len = 0;

	/* Read until 'M' (press) or 'm' (release) */
	while (len < 31) {
		if (read(STDIN_FILENO, &buffer[len], 1) != 1) {
			return mouse;
		}
		if (buffer[len] == 'M' || buffer[len] == 'm') {
			break;
		}
		len++;
	}

	char final = buffer[len];
	buffer[len] = '\0';

	/* Parse button;column;row */
	int button, col, row;
	if (sscanf(buffer, "%d;%d;%d", &button, &col, &row) != 3) {
		return mouse;
	}

	/* Convert to 0-based coordinates */
	mouse.column = (col > 0) ? (uint32_t)(col - 1) : 0;
	mouse.row = (row > 0) ? (uint32_t)(row - 1) : 0;

	/* Decode button field */
	int button_number = button & 0x03;
	bool is_drag = (button & 0x20) != 0;
	bool is_scroll = (button & 0x40) != 0;

	if (is_scroll) {
		mouse.event = (button_number == 0) ? MOUSE_SCROLL_UP : MOUSE_SCROLL_DOWN;
	} else if (button_number == 0) {
		if (is_drag) {
			mouse.event = MOUSE_LEFT_DRAG;
		} else if (final == 'M') {
			mouse.event = MOUSE_LEFT_PRESS;
		} else {
			mouse.event = MOUSE_LEFT_RELEASE;
		}
	}

	return mouse;
}

/*
 * Calculate adaptive scroll amount based on scroll velocity.
 * Tracks time between scroll events and uses exponential smoothing
 * to determine if user is scrolling slowly (precision) or quickly
 * (navigation). Returns number of lines to scroll.
 *
 * The algorithm:
 * 1. Measure time since last scroll event
 * 2. Calculate instantaneous velocity (events per second)
 * 3. Apply exponential moving average for smoothing
 * 4. Map velocity to scroll amount via linear interpolation
 * 5. Reset on direction change or timeout
 */
static uint32_t calculate_adaptive_scroll(int direction)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Calculate time delta in seconds */
	double dt = (double)(now.tv_sec - last_scroll_time.tv_sec) +
	            (double)(now.tv_nsec - last_scroll_time.tv_nsec) / 1.0e9;

	/* Check if this is the first scroll (timestamp was never set) */
	bool first_scroll = (last_scroll_time.tv_sec == 0 && last_scroll_time.tv_nsec == 0);

	/* Update timestamp */
	last_scroll_time = now;

	/* Reset velocity on direction change, first scroll, or timeout */
	if (direction != last_scroll_direction ||
	    dt > SCROLL_VELOCITY_TIMEOUT ||
	    dt <= 0 ||
	    first_scroll) {
		scroll_velocity = SCROLL_VELOCITY_SLOW;
		last_scroll_direction = direction;
		return SCROLL_MIN_LINES;
	}

	last_scroll_direction = direction;

	/* Calculate instantaneous velocity (events per second) */
	double instant_velocity = 1.0 / dt;

	/* Clamp instant velocity to reasonable bounds to avoid spikes */
	if (instant_velocity > 100.0) {
		instant_velocity = 100.0;
	}

	/* Exponential moving average for smoothing */
	scroll_velocity = SCROLL_VELOCITY_DECAY * scroll_velocity +
	                  (1.0 - SCROLL_VELOCITY_DECAY) * instant_velocity;

	/* Map velocity to scroll amount */
	if (scroll_velocity <= SCROLL_VELOCITY_SLOW) {
		return SCROLL_MIN_LINES;
	}

	if (scroll_velocity >= SCROLL_VELOCITY_FAST) {
		return SCROLL_MAX_LINES;
	}

	/* Smoothstep interpolation between min and max (eases in and out) */
	double t = (scroll_velocity - SCROLL_VELOCITY_SLOW) /
	           (SCROLL_VELOCITY_FAST - SCROLL_VELOCITY_SLOW);
	t = t * t * (3.0 - 2.0 * t);  /* smoothstep: zero derivative at endpoints */

	return SCROLL_MIN_LINES + (uint32_t)(t * (SCROLL_MAX_LINES - SCROLL_MIN_LINES));
}

/*
 * Handle a mouse event by updating cursor position and selection state.
 * Handles click, drag, scroll, and multi-click for word/line selection.
 */
static void editor_handle_mouse(struct mouse_input *mouse)
{
	switch (mouse->event) {
		case MOUSE_LEFT_PRESS: {
			/* Convert screen position to buffer position */
			uint32_t file_row = mouse->row + editor.row_offset;
			uint32_t screen_col = mouse->column;

			/* Account for gutter */
			if (screen_col < editor.gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= editor.gutter_width;
			}

			/* Clamp to valid row */
			if (file_row >= editor.buffer.line_count) {
				if (editor.buffer.line_count > 0) {
					file_row = editor.buffer.line_count - 1;
				} else {
					file_row = 0;
				}
			}

			uint32_t cell_col = screen_column_to_cell(file_row, screen_col + editor.column_offset);

			/* Detect double/triple click */
			time_t now = time(NULL);
			if (now - last_click_time <= 1 &&
			    last_click_row == file_row &&
			    last_click_col == cell_col) {
				click_count++;
			} else {
				click_count = 1;
			}
			last_click_time = now;
			last_click_row = file_row;
			last_click_col = cell_col;

			if (click_count == 2) {
				/* Double-click: select word using neighbor layer */
				editor.cursor_row = file_row;
				editor_select_word(file_row, cell_col);
			} else if (click_count >= 3) {
				/* Triple-click: select entire line */
				editor_select_line(file_row);
				click_count = 0;  /* Reset */
			} else {
				/* Single click: position cursor and start selection */
				editor.cursor_row = file_row;
				editor.cursor_column = cell_col;
				selection_start();
			}
			break;
		}

		case MOUSE_LEFT_DRAG: {
			/* Update cursor position; anchor stays fixed */
			uint32_t file_row = mouse->row + editor.row_offset;
			uint32_t screen_col = mouse->column;

			if (screen_col < editor.gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= editor.gutter_width;
			}

			if (file_row >= editor.buffer.line_count && editor.buffer.line_count > 0) {
				file_row = editor.buffer.line_count - 1;
			}

			editor.cursor_row = file_row;
			editor.cursor_column = screen_column_to_cell(file_row, screen_col + editor.column_offset);

			/* Ensure selection is active during drag */
			if (!editor.selection_active) {
				selection_start();
			}
			break;
		}

		case MOUSE_LEFT_RELEASE:
			/* Selection complete; leave it active */
			break;

		case MOUSE_SCROLL_UP: {
			uint32_t scroll_amount = calculate_adaptive_scroll(-1);

			if (editor.row_offset >= scroll_amount) {
				editor.row_offset -= scroll_amount;
			} else {
				editor.row_offset = 0;
			}

			/* Keep cursor on screen (only if no selection) */
			if (!editor.selection_active) {
				if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
					editor.cursor_row = editor.row_offset + editor.screen_rows - 1;
					if (editor.cursor_row >= editor.buffer.line_count && editor.buffer.line_count > 0) {
						editor.cursor_row = editor.buffer.line_count - 1;
					}
				}
			}
			break;
		}

		case MOUSE_SCROLL_DOWN: {
			uint32_t scroll_amount = calculate_adaptive_scroll(1);

			/* Calculate maximum valid offset */
			uint32_t max_offset = 0;
			if (editor.buffer.line_count > editor.screen_rows) {
				max_offset = editor.buffer.line_count - editor.screen_rows;
			}

			if (editor.row_offset + scroll_amount <= max_offset) {
				editor.row_offset += scroll_amount;
			} else {
				editor.row_offset = max_offset;
			}

			/* Keep cursor on screen (only if no selection) */
			if (!editor.selection_active) {
				if (editor.cursor_row < editor.row_offset) {
					editor.cursor_row = editor.row_offset;
				}
			}
			break;
		}

		default:
			break;
	}
}

/*****************************************************************************
 * Rendering
 *****************************************************************************/

/*
 * Output an ANSI true-color escape sequence for the given syntax token type.
 */
static void render_set_syntax_color(struct output_buffer *output, enum syntax_token type)
{
	struct syntax_color color = THEME_COLORS[type];
	char escape[32];
	int len = snprintf(escape, sizeof(escape), "\x1b[38;2;%d;%d;%dm",
	                   color.red, color.green, color.blue);
	output_buffer_append(output, escape, len);
}

/*
 * Render a single line's content to the output buffer. Warms the line if
 * cold. Handles horizontal scrolling by skipping to column_offset, expands
 * tabs to spaces, and encodes each cell's codepoint to UTF-8. Wide
 * characters that don't fit are replaced with spaces. Uses syntax colors
 * and highlights selected cells with a distinct background.
 */
static void render_line_content(struct output_buffer *output, struct line *line,
                                struct buffer *buffer, uint32_t file_row,
                                uint32_t column_offset, int max_width)
{
	line_warm(line, buffer);

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

	/* Track current state to minimize escape sequences */
	enum syntax_token current_syntax = SYNTAX_NORMAL;
	bool current_selected = false;

	/* Set initial foreground color */
	render_set_syntax_color(output, current_syntax);

	/* Render visible content */
	int rendered_width = 0;
	while (cell_index < line->cell_count && rendered_width < max_width) {
		uint32_t codepoint = line->cells[cell_index].codepoint;
		enum syntax_token syntax = line->cells[cell_index].syntax;
		bool selected = selection_contains(file_row, cell_index);

		/* Change colors if syntax or selection changed */
		if (syntax != current_syntax || selected != current_selected) {
			char escape[64];
			struct syntax_color fg = THEME_COLORS[syntax];
			if (selected) {
				/* Selection: selection background + syntax foreground */
				snprintf(escape, sizeof(escape),
				         "\x1b[48;2;%d;%d;%dm\x1b[38;2;%d;%d;%dm",
				         THEME_SELECTION.red, THEME_SELECTION.green, THEME_SELECTION.blue,
				         fg.red, fg.green, fg.blue);
			} else {
				/* Normal: normal background + syntax foreground */
				snprintf(escape, sizeof(escape),
				         "\x1b[48;2;%d;%d;%dm\x1b[38;2;%d;%d;%dm",
				         THEME_BACKGROUND.red, THEME_BACKGROUND.green, THEME_BACKGROUND.blue,
				         fg.red, fg.green, fg.blue);
			}
			output_buffer_append_string(output, escape);
			current_syntax = syntax;
			current_selected = selected;
		}

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

	/* Reset background to normal to prevent bleeding into next element */
	char reset[32];
	snprintf(reset, sizeof(reset), "\x1b[48;2;%d;%d;%dm",
	         THEME_BACKGROUND.red, THEME_BACKGROUND.green, THEME_BACKGROUND.blue);
	output_buffer_append_string(output, reset);
	render_set_syntax_color(output, SYNTAX_NORMAL);
}

/*
 * Render all visible rows of the editor. For each screen row, draws
 * the line number gutter (if enabled) and line content. Empty rows past
 * the end of the file are blank. Shows a centered welcome message for
 * empty buffers.
 */
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
				/* Use brighter color for current line */
				struct syntax_color ln_color = (file_row == editor.cursor_row)
					? THEME_LINE_NUMBER_ACTIVE : THEME_LINE_NUMBER;
				char color_escape[32];
				snprintf(color_escape, sizeof(color_escape), "\x1b[38;2;%d;%d;%dm",
				         ln_color.red, ln_color.green, ln_color.blue);
				output_buffer_append_string(output, color_escape);

				char line_number_buffer[16];
				snprintf(line_number_buffer, sizeof(line_number_buffer), "%*u ",
				         editor.gutter_width - 1, file_row + 1);
				output_buffer_append(output, line_number_buffer, editor.gutter_width);
			}

			/* Draw line content with tab expansion */
			if (file_row < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[file_row];
				int text_area_width = editor.screen_columns - editor.gutter_width;
				render_line_content(output, line, &editor.buffer, file_row, editor.column_offset, text_area_width);
			}
		}

		output_buffer_append_string(output, "\r\n");
	}
}

/*
 * Draw the status bar with inverted colors. Shows the filename (or
 * "[No Name]") on the left with a [+] indicator if modified, and the
 * cursor position (current line / total lines) on the right.
 */
static void render_draw_status_bar(struct output_buffer *output)
{
	/* Reset all attributes then set reverse video */
	output_buffer_append_string(output, "\x1b[0m\x1b[7m");

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

/*
 * Draw the message bar at the bottom of the screen. Shows the current
 * status message if one was set within the last 5 seconds. Clears the
 * line before drawing.
 */
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

/*
 * Refresh the entire screen. Updates the gutter width and scroll offsets,
 * then redraws all rows, the status bar, and message bar. Positions the
 * cursor using rendered column to account for tabs and wide characters.
 * The cursor is hidden during drawing to avoid flicker.
 */
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

	/* Set background color for the entire screen */
	char bg_escape[32];
	snprintf(bg_escape, sizeof(bg_escape), "\x1b[48;2;%d;%d;%dm",
	         THEME_BACKGROUND.red, THEME_BACKGROUND.green, THEME_BACKGROUND.blue);
	output_buffer_append_string(&output, bg_escape);

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

/*
 * Process a single keypress and dispatch to the appropriate handler.
 * Handles Ctrl-Q (quit with confirmation for unsaved changes), Ctrl-S
 * (save), F2 (toggle line numbers), arrow keys, and character insertion.
 * Terminal resize signals are handled by updating screen dimensions.
 */
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
			free(internal_clipboard);
			buffer_free(&editor.buffer);
			exit(0);
			break;

		case CONTROL_KEY('s'):
			editor_save();
			break;

		case CONTROL_KEY('c'):
			editor_copy();
			break;

		case CONTROL_KEY('x'):
			editor_cut();
			break;

		case CONTROL_KEY('v'):
			editor_paste();
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
		case KEY_CTRL_ARROW_LEFT:
		case KEY_CTRL_ARROW_RIGHT:
		case KEY_HOME:
		case KEY_END:
		case KEY_PAGE_UP:
		case KEY_PAGE_DOWN:
		case KEY_SHIFT_ARROW_UP:
		case KEY_SHIFT_ARROW_DOWN:
		case KEY_SHIFT_ARROW_LEFT:
		case KEY_SHIFT_ARROW_RIGHT:
		case KEY_SHIFT_HOME:
		case KEY_SHIFT_END:
		case KEY_SHIFT_PAGE_UP:
		case KEY_SHIFT_PAGE_DOWN:
		case KEY_CTRL_SHIFT_ARROW_LEFT:
		case KEY_CTRL_SHIFT_ARROW_RIGHT:
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
			/* Escape clears selection */
			selection_clear();
			break;

		case CONTROL_KEY('l'):
			/* Ctrl-L: ignore (refresh) */
			break;

		case KEY_MOUSE_EVENT:
			/* Mouse events are handled in input_read_key via editor_handle_mouse */
			break;

		default:
			/* Insert printable ASCII (32-126) and Unicode codepoints (>= 128) */
			if ((key >= 32 && key < 127) || key >= 128) {
				editor_insert_character((uint32_t)key);
			}
			break;
	}
}

/*
 * Program entry point. Initializes the terminal in raw mode, sets up
 * the window resize signal handler, and opens the file specified on
 * the command line (or starts with an empty buffer). Enters the main
 * loop which alternates between refreshing the screen and processing
 * keypresses.
 */
int main(int argument_count, char *argument_values[])
{
	terminal_enable_raw_mode();
	terminal_enable_mouse();

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
