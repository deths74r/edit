/*****************************************************************************
 * edit - A minimal terminal text editor
 * Phase 17: Visual Enhancements
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
#include <math.h>
#include <time.h>
#include <unistd.h>

#define UTFLITE_IMPLEMENTATION
#include "../third_party/utflite/single_include/utflite.h"

/* Current version of the editor, displayed in welcome message and status. */
#define EDIT_VERSION "0.17.0"

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

/* Undo/redo configuration */
#define UNDO_GROUP_TIMEOUT 1.0          /* Seconds before starting new undo group */
#define INITIAL_UNDO_CAPACITY 64        /* Initial capacity for undo groups */
#define INITIAL_OPERATION_CAPACITY 16   /* Initial capacity for ops within a group */

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
	KEY_F3 = -76,
	KEY_F4 = -75,

	/* Alt key combinations. */
	KEY_ALT_N = -88,
	KEY_ALT_P = -87,
	KEY_ALT_Z = -86,
	KEY_ALT_SHIFT_Z = -85,
	KEY_ALT_K = -84,
	KEY_ALT_D = -83,
	KEY_ALT_ARROW_UP = -82,
	KEY_ALT_ARROW_DOWN = -81,
	KEY_ALT_SLASH = -80,
	KEY_ALT_A = -79,

	/* Shift+Tab (backtab). */
	KEY_SHIFT_TAB = -78,

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

/* Wrap mode for handling long lines. */
enum wrap_mode {
	WRAP_NONE = 0,    /* No wrapping - horizontal scroll only */
	WRAP_WORD,        /* Wrap at word boundaries */
	WRAP_CHAR         /* Wrap at any character */
};

/* Visual indicator style for wrapped line continuations. */
enum wrap_indicator {
	WRAP_INDICATOR_NONE = 0,   /* Blank gutter on continuations */
	WRAP_INDICATOR_CORNER,     /* ⎿ */
	WRAP_INDICATOR_HOOK,       /* ↪ */
	WRAP_INDICATOR_ARROW,      /* → */
	WRAP_INDICATOR_DOT,        /* · */
	WRAP_INDICATOR_FLOOR,      /* ⌊ */
	WRAP_INDICATOR_BOTTOM,     /* ⌞ */
	WRAP_INDICATOR_RETURN,     /* ↳ */
	WRAP_INDICATOR_BOX         /* └ */
};

/* Types of edit operations that can be undone/redone. */
enum edit_operation_type {
	EDIT_OP_INSERT_CHAR,    /* Single character inserted */
	EDIT_OP_DELETE_CHAR,    /* Single character deleted */
	EDIT_OP_INSERT_NEWLINE, /* Line split (Enter key) */
	EDIT_OP_DELETE_NEWLINE, /* Lines joined (Backspace/Delete at line boundary) */
	EDIT_OP_DELETE_TEXT     /* Multi-character delete (selection delete) */
};

/* A single edit operation. */
struct edit_operation {
	/* Type of operation */
	enum edit_operation_type type;

	/* Position where operation occurred */
	uint32_t row;
	uint32_t column;

	/* For single char operations */
	uint32_t codepoint;

	/* For multi-char operations (selection delete) */
	char *text;           /* UTF-8 encoded text */
	size_t text_length;   /* Length in bytes */

	/* End position for selection operations */
	uint32_t end_row;
	uint32_t end_column;
};

/* A group of operations that should be undone together. */
struct undo_group {
	/* Array of operations in this group */
	struct edit_operation *operations;
	uint32_t operation_count;
	uint32_t operation_capacity;

	/* Cursor position before this group (to restore on undo) */
	uint32_t cursor_row_before;
	uint32_t cursor_column_before;

	/* Cursor position after this group (to restore on redo) */
	uint32_t cursor_row_after;
	uint32_t cursor_column_after;
};

/* The undo/redo history. */
struct undo_history {
	/* Array of undo groups */
	struct undo_group *groups;
	uint32_t group_count;
	uint32_t group_capacity;

	/* Current position in history (for redo) */
	uint32_t current_index;

	/* Whether we're currently recording a group */
	bool recording;

	/* Timestamp of last edit (for auto-grouping) */
	struct timespec last_edit_time;
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

/* Tritanopia-friendly dark theme with WCAG AA compliance.
 *
 * Design principles:
 * - Luminance is the primary differentiator (works for all color vision)
 * - Red-cyan color axis (visible to Tritanopia)
 * - Avoids blue-yellow distinctions (not visible to Tritanopia)
 * - All colors have >= 4.5:1 contrast with background
 */
static const struct syntax_color THEME_COLORS[] = {
	[SYNTAX_NORMAL]       = {0xE0, 0xE0, 0xE0},  /* Light gray - neutral, high contrast */
	[SYNTAX_KEYWORD]      = {0xFF, 0x79, 0xC6},  /* Bright magenta - control flow stands out */
	[SYNTAX_TYPE]         = {0x8B, 0xE9, 0xFD},  /* Bright cyan - distinct from magenta */
	[SYNTAX_STRING]       = {0xFF, 0x95, 0x80},  /* Coral/salmon - warm, readable */
	[SYNTAX_NUMBER]       = {0xFF, 0xB0, 0x90},  /* Light coral/peach - related to string */
	[SYNTAX_COMMENT]      = {0x90, 0x90, 0x90},  /* Medium gray - subdued but WCAG compliant */
	[SYNTAX_PREPROCESSOR] = {0xFF, 0xB3, 0xD9},  /* Light pink/rose - preprocessor visible */
	[SYNTAX_FUNCTION]     = {0xA0, 0xF0, 0xF0},  /* Light cyan/aqua - functions stand out */
	[SYNTAX_OPERATOR]     = {0xFF, 0xFF, 0xFF},  /* White - maximum visibility for operators */
	[SYNTAX_BRACKET]      = {0xD0, 0xD0, 0xD0},  /* Light gray - same family as normal */
	[SYNTAX_ESCAPE]       = {0xFF, 0x80, 0x80},  /* Light red - escapes pop in strings */
};

/* Background: Near-black for maximum contrast with all foreground colors. */
static const struct syntax_color THEME_BACKGROUND = {0x12, 0x12, 0x12};

/* Selection: Dark teal - cool tone, distinct from search highlights. */
static const struct syntax_color THEME_SELECTION = {0x2D, 0x4F, 0x4F};

/* Search match highlights use luminance difference for Tritanopia visibility.
 * Current match is warmer and lighter than other matches. */
static const struct syntax_color THEME_SEARCH_MATCH = {0x3D, 0x3D, 0x5D};   /* Dark purple-gray */
static const struct syntax_color THEME_SEARCH_CURRENT = {0x5D, 0x4D, 0x3D}; /* Warm tan - brighter */

/* Line numbers: Gray scale for minimal distraction, still readable. */
static const struct syntax_color THEME_LINE_NUMBER = {0x60, 0x60, 0x60};
static const struct syntax_color THEME_LINE_NUMBER_ACTIVE = {0xA0, 0xA0, 0xA0};

/* Cursor line: Highlight for the line containing the cursor.
 * Noticeably lighter than background (#121212) for clear visibility. */
static const struct syntax_color THEME_CURSOR_LINE = {0x28, 0x28, 0x28};

/* Whitespace indicators: Subtle but visible when show_whitespace is on. */
static const struct syntax_color THEME_WHITESPACE = {0x50, 0x50, 0x50};

/* Trailing whitespace: Warning background for unwanted trailing spaces. */
static const struct syntax_color THEME_TRAILING_WHITESPACE = {0x50, 0x30, 0x30};

/* Color column: Subtle vertical line at specified column. */
static const struct syntax_color THEME_COLOR_COLUMN = {0x2A, 0x2A, 0x2A};

/*****************************************************************************
 * WCAG Color Contrast Utilities
 *****************************************************************************/

/* Minimum contrast ratio for WCAG AA compliance (normal text). */
#define WCAG_MIN_CONTRAST 4.5

/*
 * Linearize an sRGB component (0-255) for luminance calculation.
 * Applies inverse gamma correction per sRGB specification.
 */
static double color_linearize(uint8_t value)
{
	double srgb = value / 255.0;
	if (srgb <= 0.03928) {
		return srgb / 12.92;
	}
	return pow((srgb + 0.055) / 1.055, 2.4);
}

/*
 * Calculate relative luminance of an RGB color per WCAG 2.1.
 * Returns a value between 0.0 (black) and 1.0 (white).
 */
static double color_luminance(struct syntax_color color)
{
	double r = color_linearize(color.red);
	double g = color_linearize(color.green);
	double b = color_linearize(color.blue);
	return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/*
 * Calculate contrast ratio between two colors per WCAG 2.1.
 * Returns a value >= 1.0, where 1.0 means identical colors
 * and 21.0 is the maximum (black on white).
 */
static double color_contrast_ratio(struct syntax_color fg, struct syntax_color bg)
{
	double lum_fg = color_luminance(fg);
	double lum_bg = color_luminance(bg);

	double lighter = lum_fg > lum_bg ? lum_fg : lum_bg;
	double darker = lum_fg > lum_bg ? lum_bg : lum_fg;

	return (lighter + 0.05) / (darker + 0.05);
}

/*
 * Adjust a single color channel toward a target to improve contrast.
 * Returns the new channel value.
 */
static uint8_t color_adjust_channel(uint8_t value, bool make_lighter)
{
	if (make_lighter) {
		/* Move toward 255 */
		int new_val = value + (255 - value) / 2;
		if (new_val > 255) new_val = 255;
		if (new_val == value && value < 255) new_val = value + 1;
		return (uint8_t)new_val;
	} else {
		/* Move toward 0 */
		int new_val = value / 2;
		if (new_val == value && value > 0) new_val = value - 1;
		return (uint8_t)new_val;
	}
}

/*
 * Get a WCAG-compliant foreground color for the given background.
 * If the original foreground has sufficient contrast, returns it unchanged.
 * Otherwise, adjusts the foreground (lighter or darker) to meet WCAG AA.
 */
static struct syntax_color color_ensure_contrast(struct syntax_color fg,
                                                  struct syntax_color bg)
{
	double ratio = color_contrast_ratio(fg, bg);

	/* Already compliant */
	if (ratio >= WCAG_MIN_CONTRAST) {
		return fg;
	}

	/* Determine whether to lighten or darken the foreground.
	 * Choose the direction that increases contrast with the background. */
	double bg_lum = color_luminance(bg);
	bool make_lighter = bg_lum < 0.5;  /* Dark bg = lighter text */

	struct syntax_color adjusted = fg;
	int iterations = 0;
	const int max_iterations = 20;  /* Prevent infinite loops */

	while (ratio < WCAG_MIN_CONTRAST && iterations < max_iterations) {
		adjusted.red = color_adjust_channel(adjusted.red, make_lighter);
		adjusted.green = color_adjust_channel(adjusted.green, make_lighter);
		adjusted.blue = color_adjust_channel(adjusted.blue, make_lighter);

		ratio = color_contrast_ratio(adjusted, bg);
		iterations++;
	}

	/* If we couldn't achieve compliance going one direction, try the other */
	if (ratio < WCAG_MIN_CONTRAST) {
		adjusted = fg;
		make_lighter = !make_lighter;
		iterations = 0;

		while (ratio < WCAG_MIN_CONTRAST && iterations < max_iterations) {
			adjusted.red = color_adjust_channel(adjusted.red, make_lighter);
			adjusted.green = color_adjust_channel(adjusted.green, make_lighter);
			adjusted.blue = color_adjust_channel(adjusted.blue, make_lighter);

			ratio = color_contrast_ratio(adjusted, bg);
			iterations++;
		}
	}

	/* Last resort: use pure black or white */
	if (ratio < WCAG_MIN_CONTRAST) {
		if (bg_lum < 0.5) {
			adjusted = (struct syntax_color){0xff, 0xff, 0xff};  /* White */
		} else {
			adjusted = (struct syntax_color){0x00, 0x00, 0x00};  /* Black */
		}
	}

	return adjusted;
}

/*****************************************************************************
 * Soft Wrap
 *****************************************************************************/

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

	/*
	 * Wrap cache - computed on demand, invalidated on edit/resize.
	 * wrap_columns[i] is the column index where segment i STARTS.
	 * wrap_columns[0] is always 0 (first segment starts at column 0).
	 */
	uint32_t *wrap_columns;

	/* Number of visual segments (1 = no wrap, 2+ = wrapped). */
	uint16_t wrap_segment_count;

	/* Text area width when wrap was computed (0 = cache invalid). */
	uint16_t wrap_cache_width;

	/* Wrap mode when computed. */
	enum wrap_mode wrap_cache_mode;
};

/*
 * Find the best column to break a line for wrapping.
 *
 * Searches backward from max_width to find an appropriate break point.
 * Uses the neighbor layer to prefer breaking at word boundaries.
 *
 * Parameters:
 *   line        - The line to find a wrap point in (must be warm)
 *   start_col   - Column where this segment starts
 *   max_width   - Maximum visual width for this segment
 *   mode        - WRAP_WORD (prefer word boundaries) or WRAP_CHAR (any column)
 *
 * Returns:
 *   Column index where the segment should END (exclusive).
 *   Next segment starts at this column.
 *   Returns line->cell_count if no wrap needed.
 */
static uint32_t line_find_wrap_point(struct line *line, uint32_t start_col,
                                      uint32_t max_width, enum wrap_mode mode);

/* Cycle through wrap modes: NONE -> WORD -> CHAR -> NONE */
static void editor_cycle_wrap_mode(void);

/* Cycle through wrap indicators. */
static void editor_cycle_wrap_indicator(void);

/* Get the UTF-8 string for a wrap indicator. */
static const char *wrap_indicator_string(enum wrap_indicator ind);

/* Invalidate wrap cache for a single line. */
static void line_invalidate_wrap_cache(struct line *line);

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

	/* Undo/redo history. */
	struct undo_history undo_history;
};

/* Compute wrap points for a line. Populates the wrap cache fields. */
static void line_compute_wrap_points(struct line *line, struct buffer *buffer,
                                     uint16_t text_width, enum wrap_mode mode);

/* Invalidate wrap cache for all lines in the buffer. */
static void buffer_invalidate_all_wrap_caches(struct buffer *buffer);

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

	/* Soft wrap settings. */
	enum wrap_mode wrap_mode;
	enum wrap_indicator wrap_indicator;

	/* Visual display settings. */
	bool show_whitespace;        /* Render whitespace characters visibly */
	uint32_t color_column;       /* Column to highlight (0 = off) */
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

/* Incremental search state. */
struct search_state {
	bool active;                    /* Whether search mode is active */
	char query[256];                /* The search query (UTF-8) */
	uint32_t query_length;          /* Length of query in bytes */
	uint32_t saved_cursor_row;      /* Cursor position when search started */
	uint32_t saved_cursor_column;
	uint32_t saved_row_offset;      /* Scroll position when search started */
	uint32_t saved_column_offset;
	uint32_t match_row;             /* Current match position */
	uint32_t match_column;
	bool has_match;                 /* Whether current query has a match */
	int direction;                  /* 1 = forward, -1 = backward */

	/* Replace mode fields */
	bool replace_mode;              /* true = replace mode, false = search only */
	char replace_text[256];         /* Replacement text (UTF-8) */
	uint32_t replace_length;        /* Length of replacement text in bytes */
	bool editing_replace;           /* true = editing replace field */
};
static struct search_state search = {0};

/* Go-to-line mode state. */
struct goto_state {
	bool active;
	char input[16];             /* Line number input buffer */
	uint32_t input_length;
	uint32_t saved_cursor_row;
	uint32_t saved_cursor_column;
	uint32_t saved_row_offset;
};
static struct goto_state goto_line = {0};

/* Forward declarations for functions used in input_read_key. */
static struct mouse_input input_parse_sgr_mouse(void);
static void editor_handle_mouse(struct mouse_input *mouse);

/* Forward declarations for search functions used in mouse handling. */
static bool search_find_next(bool wrap);
static bool search_find_previous(bool wrap);

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

/*
 * Check if a cell is trailing whitespace (whitespace with no non-whitespace
 * content after it on the line). Used for visual highlighting.
 */
static bool is_trailing_whitespace(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return false;
	}

	/* First check if current cell is whitespace. */
	enum character_class current_class =
		neighbor_get_class(line->cells[column].neighbor);
	if (current_class != CHAR_CLASS_WHITESPACE) {
		return false;
	}

	/* Check if there's any non-whitespace after this position. */
	for (uint32_t i = column + 1; i < line->cell_count; i++) {
		enum character_class cell_class =
			neighbor_get_class(line->cells[i].neighbor);
		if (cell_class != CHAR_CLASS_WHITESPACE) {
			return false;
		}
	}

	return true;
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

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1) {
		return false;
	}

	/*
	 * Sanity check: reject unreasonably small dimensions.
	 * When stdout is a pipe (not a TTY), ioctl may succeed but
	 * return garbage values. Minimum usable size is 10x10.
	 */
	if (window_size.ws_col < 10 || window_size.ws_row < 10) {
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

		/* Check for Alt+key (Meta sends ESC followed by letter) */
		if (sequence[0] != '[' && sequence[0] != 'O') {
			switch (sequence[0]) {
				case 'n': case 'N': return KEY_ALT_N;
				case 'p': case 'P': return KEY_ALT_P;
				case 'z': return KEY_ALT_Z;
				case 'Z': return KEY_ALT_SHIFT_Z;
				case 'k': case 'K': return KEY_ALT_K;
				case 'd': case 'D': return KEY_ALT_D;
				case '/': return KEY_ALT_SLASH;
				case 'a': case 'A': return KEY_ALT_A;
				default: return '\x1b';
			}
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
						} else if (modifier == '3') {  /* Alt */
							switch (final) {
								case 'A': return KEY_ALT_ARROW_UP;
								case 'B': return KEY_ALT_ARROW_DOWN;
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
						} else if (sequence[1] == '1' && sequence[2] == '3') {
							return KEY_F3;
						} else if (sequence[1] == '1' && sequence[2] == '4') {
							return KEY_F4;
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
					case 'Z': return KEY_SHIFT_TAB;
				}
			}
		} else if (sequence[0] == 'O') {
			switch (sequence[1]) {
				case 'H': return KEY_HOME;
				case 'F': return KEY_END;
				case 'Q': return KEY_F2;
				case 'R': return KEY_F3;
				case 'S': return KEY_F4;
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
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;
	line->wrap_cache_mode = WRAP_NONE;
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
	free(line->wrap_columns);
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;
	line->wrap_cache_mode = WRAP_NONE;
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
	buffer->next_pair_id = 1;

	/* Initialize undo history */
	buffer->undo_history.groups = NULL;
	buffer->undo_history.group_count = 0;
	buffer->undo_history.group_capacity = 0;
	buffer->undo_history.current_index = 0;
	buffer->undo_history.recording = false;
	buffer->undo_history.last_edit_time.tv_sec = 0;
	buffer->undo_history.last_edit_time.tv_nsec = 0;
}

/* Forward declaration for undo_history_free */
static void undo_history_free(struct undo_history *history);

/* Frees all lines and memory associated with the buffer, including
 * unmapping any memory-mapped file. */
static void buffer_free(struct buffer *buffer)
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

	/* Invalidate wrap cache since line content changed. */
	line_invalidate_wrap_cache(line);
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
		/* Invalidate wrap cache since line content changed. */
		line_invalidate_wrap_cache(line);
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
		/* Invalidate wrap cache since line content changed. */
		line_invalidate_wrap_cache(line);
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

		/* Invalidate wrap cache for truncated line. */
		line_invalidate_wrap_cache(line);
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
	editor.wrap_mode = WRAP_WORD;
	editor.wrap_indicator = WRAP_INDICATOR_RETURN;
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
 * Undo/Redo History
 *****************************************************************************/

/* Initialize an undo history structure. */
static void undo_history_init(struct undo_history *history)
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
static void undo_history_free(struct undo_history *history)
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

/* Forward declaration */
static void undo_end_group(struct buffer *buffer);

/*
 * Begin a new undo group. If within timeout of the last edit,
 * continues the existing group. Called before making edits.
 */
static void undo_begin_group(struct buffer *buffer)
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
		undo_end_group(buffer);
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
	group->cursor_row_before = editor.cursor_row;
	group->cursor_column_before = editor.cursor_column;
	group->cursor_row_after = editor.cursor_row;
	group->cursor_column_after = editor.cursor_column;

	history->group_count++;
	history->current_index = history->group_count;
	history->recording = true;
	history->last_edit_time = now;
}

/*
 * End the current undo group. Records final cursor position.
 */
static void undo_end_group(struct buffer *buffer)
{
	struct undo_history *history = &buffer->undo_history;

	if (!history->recording || history->group_count == 0) {
		return;
	}

	struct undo_group *group = &history->groups[history->group_count - 1];
	group->cursor_row_after = editor.cursor_row;
	group->cursor_column_after = editor.cursor_column;

	/* If group is empty, remove it */
	if (group->operation_count == 0) {
		history->group_count--;
		history->current_index = history->group_count;
	}

	history->recording = false;
}

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
			return;
		}
		group->operations = new_ops;
		group->operation_capacity = new_capacity;
	}

	group->operations[group->operation_count++] = *op;
}

/* Record insertion of a single character. */
static void undo_record_insert_char(struct buffer *buffer, uint32_t row,
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
static void undo_record_delete_char(struct buffer *buffer, uint32_t row,
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
static void undo_record_insert_newline(struct buffer *buffer, uint32_t row,
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
static void undo_record_delete_newline(struct buffer *buffer, uint32_t row,
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
static void undo_record_delete_text(struct buffer *buffer,
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
	line->temperature = LINE_TEMPERATURE_HOT;
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
	line->temperature = LINE_TEMPERATURE_HOT;
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
		line->temperature = LINE_TEMPERATURE_HOT;

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

	line->temperature = LINE_TEMPERATURE_HOT;
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
 * Used during undo/redo operations.
 */
static void buffer_delete_range_no_record(struct buffer *buffer,
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
		line->temperature = LINE_TEMPERATURE_HOT;
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

		start_line->temperature = LINE_TEMPERATURE_HOT;
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

/*
 * Undo the most recent group of operations.
 */
static void editor_undo(void)
{
	struct undo_history *history = &editor.buffer.undo_history;

	/* End any in-progress group */
	undo_end_group(&editor.buffer);

	if (history->current_index == 0) {
		editor_set_status_message("Nothing to undo");
		return;
	}

	history->current_index--;
	struct undo_group *group = &history->groups[history->current_index];

	/* Reverse operations in reverse order */
	for (int i = (int)group->operation_count - 1; i >= 0; i--) {
		undo_reverse_operation(&editor.buffer, &group->operations[i]);
	}

	/* Restore cursor position */
	editor.cursor_row = group->cursor_row_before;
	editor.cursor_column = group->cursor_column_before;

	/* Clear selection */
	selection_clear();

	/* Recompute syntax highlighting */
	buffer_compute_pairs(&editor.buffer);
	for (uint32_t row = 0; row < editor.buffer.line_count; row++) {
		if (editor.buffer.lines[row].temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(&editor.buffer.lines[row], &editor.buffer, row);
		}
	}

	/* Update modified flag */
	editor.buffer.is_modified = (history->current_index > 0);

	editor_set_status_message("Undo");
}

/*
 * Redo the most recently undone group of operations.
 */
static void editor_redo(void)
{
	struct undo_history *history = &editor.buffer.undo_history;

	/* End any in-progress group */
	undo_end_group(&editor.buffer);

	if (history->current_index >= history->group_count) {
		editor_set_status_message("Nothing to redo");
		return;
	}

	struct undo_group *group = &history->groups[history->current_index];
	history->current_index++;

	/* Apply operations in order */
	for (uint32_t i = 0; i < group->operation_count; i++) {
		undo_apply_operation(&editor.buffer, &group->operations[i]);
	}

	/* Restore cursor position */
	editor.cursor_row = group->cursor_row_after;
	editor.cursor_column = group->cursor_column_after;

	/* Clear selection */
	selection_clear();

	/* Recompute syntax highlighting */
	buffer_compute_pairs(&editor.buffer);
	for (uint32_t row = 0; row < editor.buffer.line_count; row++) {
		if (editor.buffer.lines[row].temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(&editor.buffer.lines[row], &editor.buffer, row);
		}
	}

	/* Mark as modified */
	editor.buffer.is_modified = true;

	editor_set_status_message("Redo");
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

	/* Terminal width changed, invalidate all wrap caches. */
	buffer_invalidate_all_wrap_caches(&editor.buffer);
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

/*****************************************************************************
 * Soft Wrap Implementation
 *****************************************************************************/

static uint32_t line_find_wrap_point(struct line *line, uint32_t start_col,
                                      uint32_t max_width, enum wrap_mode mode)
{
	if (mode == WRAP_NONE) {
		return line->cell_count;  /* No wrapping */
	}

	/* Calculate visual width from start_col */
	uint32_t visual_width = 0;
	uint32_t col = start_col;

	while (col < line->cell_count) {
		uint32_t cp = line->cells[col].codepoint;
		int width;

		if (cp == '\t') {
			width = TAB_STOP_WIDTH - ((visual_width) % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}

		if (visual_width + (uint32_t)width > max_width) {
			/* This character would exceed max_width */
			break;
		}

		visual_width += (uint32_t)width;
		col++;
	}

	/* If we fit the whole line (from start_col), no wrap needed */
	if (col >= line->cell_count) {
		return line->cell_count;
	}

	/* col is now the first character that doesn't fit */
	uint32_t hard_break = col;

	/* For character wrap, just break at the edge */
	if (mode == WRAP_CHAR) {
		/* Don't break at column 0 of segment (infinite loop) */
		return (hard_break > start_col) ? hard_break : start_col + 1;
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
			/* Keep looking for whitespace */
		}

		/* Acceptable: break at word boundary (class transition) */
		if (!found_break && i < hard_break) {
			uint8_t next_neighbor = line->cells[i].neighbor;
			enum character_class next_cls = neighbor_get_class(next_neighbor);
			if (cls != next_cls && cls != CHAR_CLASS_WHITESPACE) {
				best_break = i;
				found_break = true;
				/* Keep looking for better options */
			}
		}
	}

	/* If no good break found, fall back to hard break */
	if (!found_break || best_break <= start_col) {
		best_break = hard_break;
	}

	/* Safety: never return start_col (would cause infinite loop) */
	if (best_break <= start_col) {
		best_break = start_col + 1;
	}

	return best_break;
}

static void editor_cycle_wrap_mode(void)
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
	buffer_invalidate_all_wrap_caches(&editor.buffer);
}

static void editor_cycle_wrap_indicator(void)
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

static const char *wrap_indicator_string(enum wrap_indicator ind)
{
	switch (ind) {
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

/*
 * Invalidate the wrap cache for a single line.
 * Called when line content changes or when wrap settings change.
 */
static void line_invalidate_wrap_cache(struct line *line)
{
	free(line->wrap_columns);
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;
	line->wrap_cache_mode = WRAP_NONE;
}

/*
 * Invalidate wrap caches for all lines in the buffer.
 * Called when terminal is resized or wrap mode changes.
 */
static void buffer_invalidate_all_wrap_caches(struct buffer *buffer)
{
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		line_invalidate_wrap_cache(&buffer->lines[row]);
	}
}

/*
 * Compute wrap points for a line and populate the wrap cache.
 *
 * This function calculates where a line should wrap based on the text
 * area width and wrap mode. Results are cached in the line struct to
 * avoid recomputation during scrolling and rendering.
 *
 * Parameters:
 *   line       - The line to compute wrap points for
 *   buffer     - Parent buffer (needed to warm cold lines)
 *   text_width - Available width for text (screen width minus gutter)
 *   mode       - Wrap mode (NONE, WORD, or CHAR)
 */
static void line_compute_wrap_points(struct line *line, struct buffer *buffer,
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
		uint32_t wrap_point = line_find_wrap_point(line, column,
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
		uint32_t wrap_point = line_find_wrap_point(line, column,
		                                           text_width, mode);
		line->wrap_columns[segment] = wrap_point;
		column = wrap_point;
	}

	line->wrap_segment_count = segment_count;
	line->wrap_cache_width = text_width;
	line->wrap_cache_mode = mode;
}

/*
 * Get the text area width (screen width minus gutter).
 * Used for computing wrap points.
 */
static uint16_t editor_get_text_width(void)
{
	return editor.screen_columns > editor.gutter_width
		? editor.screen_columns - editor.gutter_width
		: 1;
}

/*
 * Ensure wrap cache is computed for a line.
 * Uses current editor wrap settings and text width.
 */
static void line_ensure_wrap_cache(struct line *line, struct buffer *buffer)
{
	uint16_t text_width = editor_get_text_width();
	line_compute_wrap_points(line, buffer, text_width, editor.wrap_mode);
}

/*
 * Find which segment of a line contains the given column.
 * Ensures wrap cache is computed first.
 * Returns 0 for first segment, up to segment_count-1 for last.
 */
static uint16_t line_get_segment_for_column(struct line *line,
                                            struct buffer *buffer,
                                            uint32_t column)
{
	line_ensure_wrap_cache(line, buffer);

	if (line->wrap_segment_count <= 1) {
		return 0;
	}

	/*
	 * Binary search for the segment containing this column.
	 * wrap_columns[i] is where segment i starts, so we want
	 * the largest i where wrap_columns[i] <= column.
	 */
	uint16_t low = 0;
	uint16_t high = line->wrap_segment_count - 1;

	while (low < high) {
		uint16_t mid = (low + high + 1) / 2;
		if (line->wrap_columns[mid] <= column) {
			low = mid;
		} else {
			high = mid - 1;
		}
	}

	return low;
}

/*
 * Get the start column for a specific segment of a line.
 * Ensures wrap cache is computed first.
 */
static uint32_t line_get_segment_start(struct line *line,
                                       struct buffer *buffer,
                                       uint16_t segment)
{
	line_ensure_wrap_cache(line, buffer);

	if (segment >= line->wrap_segment_count) {
		segment = line->wrap_segment_count - 1;
	}

	return line->wrap_columns[segment];
}

/*
 * Get the end column (exclusive) for a specific segment of a line.
 * Ensures wrap cache is computed first.
 */
static uint32_t line_get_segment_end(struct line *line,
                                     struct buffer *buffer,
                                     uint16_t segment)
{
	line_ensure_wrap_cache(line, buffer);

	if (segment >= line->wrap_segment_count) {
		return line->cell_count;
	}

	if (segment + 1 < line->wrap_segment_count) {
		return line->wrap_columns[segment + 1];
	}

	return line->cell_count;
}

/*
 * Get total number of screen rows needed to display all lines
 * with current wrap settings. Used for scroll calculations.
 */
static uint32_t buffer_get_total_screen_rows(struct buffer *buffer)
{
	if (editor.wrap_mode == WRAP_NONE) {
		return buffer->line_count;
	}

	uint32_t total = 0;
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		line_ensure_wrap_cache(&buffer->lines[row], buffer);
		total += buffer->lines[row].wrap_segment_count;
	}

	return total > 0 ? total : 1;
}

/*
 * Calculate the visual column within a segment for a given cell column.
 * Returns the rendered width from segment start to the cell.
 */
static uint32_t line_get_visual_column_in_segment(struct line *line,
                                                   struct buffer *buffer,
                                                   uint16_t segment,
                                                   uint32_t cell_column)
{
	line_ensure_wrap_cache(line, buffer);
	line_warm(line, buffer);

	uint32_t segment_start = line_get_segment_start(line, buffer, segment);
	uint32_t segment_end = line_get_segment_end(line, buffer, segment);

	/* Clamp cell_column to segment bounds */
	if (cell_column < segment_start) {
		cell_column = segment_start;
	}
	if (cell_column > segment_end) {
		cell_column = segment_end;
	}

	/* Calculate visual width from segment start to cell_column */
	uint32_t visual_col = 0;
	uint32_t absolute_visual = 0;

	/* First calculate visual column at segment start (for tab alignment) */
	for (uint32_t i = 0; i < segment_start && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		absolute_visual += width;
	}

	/* Now calculate visual width within segment */
	for (uint32_t i = segment_start; i < cell_column && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		visual_col += width;
		absolute_visual += width;
	}

	return visual_col;
}

/*
 * Find the cell column at a given visual column within a segment.
 * Used for maintaining visual position when moving between segments.
 */
static uint32_t line_find_column_at_visual(struct line *line,
                                           struct buffer *buffer,
                                           uint16_t segment,
                                           uint32_t target_visual)
{
	line_ensure_wrap_cache(line, buffer);
	line_warm(line, buffer);

	uint32_t segment_start = line_get_segment_start(line, buffer, segment);
	uint32_t segment_end = line_get_segment_end(line, buffer, segment);

	/* Calculate visual column at segment start (for tab alignment) */
	uint32_t absolute_visual = 0;
	for (uint32_t i = 0; i < segment_start && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		absolute_visual += width;
	}

	/* Find the column at target_visual within the segment */
	uint32_t visual_col = 0;
	uint32_t col = segment_start;

	while (col < segment_end && col < line->cell_count) {
		if (visual_col >= target_visual) {
			break;
		}
		uint32_t cp = line->cells[col].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		visual_col += width;
		absolute_visual += width;
		col++;
	}

	return col;
}

/*
 * Map a screen row (relative to row_offset) to logical line and segment.
 * Used for converting mouse click positions to buffer positions.
 *
 * Returns true if the screen row maps to valid content, false if it's
 * past the end of the buffer. On success, sets *out_line and *out_segment.
 */
static bool screen_row_to_line_segment(uint32_t screen_row,
                                       uint32_t *out_line,
                                       uint16_t *out_segment)
{
	if (editor.wrap_mode == WRAP_NONE) {
		/* No wrap: direct mapping */
		uint32_t file_row = screen_row + editor.row_offset;
		if (file_row >= editor.buffer.line_count) {
			return false;
		}
		*out_line = file_row;
		*out_segment = 0;
		return true;
	}

	/* With wrap: iterate through lines and segments */
	uint32_t file_row = editor.row_offset;
	uint16_t segment = 0;
	uint32_t screen_pos = 0;

	while (file_row < editor.buffer.line_count && screen_pos <= screen_row) {
		struct line *line = &editor.buffer.lines[file_row];
		line_ensure_wrap_cache(line, &editor.buffer);

		/* Check each segment of this line */
		for (segment = 0; segment < line->wrap_segment_count; segment++) {
			if (screen_pos == screen_row) {
				*out_line = file_row;
				*out_segment = segment;
				return true;
			}
			screen_pos++;
		}
		file_row++;
	}

	return false;
}

/*
 * Calculate the maximum row_offset that ensures all content is reachable.
 * In wrap mode, finds the first logical line such that the total screen
 * rows from that line to the end fill at most screen_rows.
 */
static uint32_t calculate_max_row_offset(void)
{
	if (editor.wrap_mode == WRAP_NONE) {
		/* Simple case: 1 line = 1 screen row */
		if (editor.buffer.line_count > editor.screen_rows) {
			return editor.buffer.line_count - editor.screen_rows;
		}
		return 0;
	}

	/*
	 * With wrap: work backwards from last line, summing screen rows
	 * until we exceed screen_rows. The line after that is max_offset.
	 */
	uint32_t screen_rows_from_end = 0;
	uint32_t candidate = editor.buffer.line_count;

	while (candidate > 0) {
		candidate--;
		struct line *line = &editor.buffer.lines[candidate];
		line_ensure_wrap_cache(line, &editor.buffer);
		screen_rows_from_end += line->wrap_segment_count;

		if (screen_rows_from_end >= editor.screen_rows) {
			/* This line and all after fill the screen */
			return candidate;
		}
	}

	/* All content fits on one screen */
	return 0;
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
 * (column_offset). In wrap mode, accounts for multi-segment lines.
 */
static void editor_scroll(void)
{
	/* Vertical scrolling (skip if selection active - allow cursor off-screen) */
	if (!editor.selection_active) {
		if (editor.cursor_row < editor.row_offset) {
			editor.row_offset = editor.cursor_row;
		}

		if (editor.wrap_mode == WRAP_NONE) {
			/* No wrap: simple check */
			if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
				editor.row_offset = editor.cursor_row - editor.screen_rows + 1;
			}
		} else {
			/*
			 * Wrap enabled: calculate screen row of cursor and check
			 * if it's past the visible area.
			 */
			uint32_t screen_row = 0;
			for (uint32_t row = editor.row_offset;
			     row <= editor.cursor_row && row < editor.buffer.line_count;
			     row++) {
				struct line *line = &editor.buffer.lines[row];
				line_ensure_wrap_cache(line, &editor.buffer);
				if (row == editor.cursor_row) {
					/* Add cursor's segment within this line */
					uint16_t cursor_segment = line_get_segment_for_column(
						line, &editor.buffer, editor.cursor_column);
					screen_row += cursor_segment + 1;
				} else {
					screen_row += line->wrap_segment_count;
				}
			}

			/* If cursor is past visible area, scroll down */
			while (screen_row > editor.screen_rows &&
			       editor.row_offset < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[editor.row_offset];
				line_ensure_wrap_cache(line, &editor.buffer);
				screen_row -= line->wrap_segment_count;
				editor.row_offset++;
			}
		}
	}

	/* Horizontal scrolling - only applies in WRAP_NONE mode */
	if (editor.wrap_mode == WRAP_NONE) {
		uint32_t render_column = editor_get_render_column(
			editor.cursor_row, editor.cursor_column);
		uint32_t text_area_width = editor.screen_columns - editor.gutter_width;
		if (render_column < editor.column_offset) {
			editor.column_offset = render_column;
		}
		if (render_column >= editor.column_offset + text_area_width) {
			editor.column_offset = render_column - text_area_width + 1;
		}
	} else {
		/* In wrap mode, no horizontal scrolling needed */
		editor.column_offset = 0;
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
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: move by logical line */
				if (editor.cursor_row > 0) {
					editor.cursor_row--;
				}
			} else if (current_line) {
				/* Wrap enabled: move by screen row (segment) */
				uint16_t cur_segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t visual_col = line_get_visual_column_in_segment(
					current_line, &editor.buffer, cur_segment,
					editor.cursor_column);

				if (cur_segment > 0) {
					/* Move to previous segment of same line */
					uint32_t new_col = line_find_column_at_visual(
						current_line, &editor.buffer,
						cur_segment - 1, visual_col);
					editor.cursor_column = new_col;
				} else if (editor.cursor_row > 0) {
					/* Move to last segment of previous line */
					editor.cursor_row--;
					struct line *prev_line = &editor.buffer.lines[editor.cursor_row];
					line_ensure_wrap_cache(prev_line, &editor.buffer);
					uint16_t last_segment = prev_line->wrap_segment_count - 1;
					uint32_t new_col = line_find_column_at_visual(
						prev_line, &editor.buffer, last_segment, visual_col);
					editor.cursor_column = new_col;
				}
			}
			break;

		case KEY_ARROW_DOWN:
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: move by logical line */
				if (editor.cursor_row < editor.buffer.line_count - 1) {
					editor.cursor_row++;
				}
			} else if (current_line) {
				/* Wrap enabled: move by screen row (segment) */
				line_ensure_wrap_cache(current_line, &editor.buffer);
				uint16_t cur_segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t visual_col = line_get_visual_column_in_segment(
					current_line, &editor.buffer, cur_segment,
					editor.cursor_column);

				if (cur_segment + 1 < current_line->wrap_segment_count) {
					/* Move to next segment of same line */
					uint32_t new_col = line_find_column_at_visual(
						current_line, &editor.buffer,
						cur_segment + 1, visual_col);
					editor.cursor_column = new_col;
				} else if (editor.cursor_row < editor.buffer.line_count - 1) {
					/* Move to first segment of next line */
					editor.cursor_row++;
					struct line *next_line = &editor.buffer.lines[editor.cursor_row];
					uint32_t new_col = line_find_column_at_visual(
						next_line, &editor.buffer, 0, visual_col);
					editor.cursor_column = new_col;
				}
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
			if (editor.wrap_mode == WRAP_NONE || !current_line) {
				/* No wrap: go to start of logical line */
				editor.cursor_column = 0;
			} else {
				/* Wrap enabled: go to start of current segment */
				uint16_t segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t segment_start = line_get_segment_start(
					current_line, &editor.buffer, segment);
				if (editor.cursor_column == segment_start && segment > 0) {
					/* Already at segment start, go to previous segment start */
					segment_start = line_get_segment_start(
						current_line, &editor.buffer, segment - 1);
				}
				editor.cursor_column = segment_start;
			}
			break;

		case KEY_END:
			if (editor.wrap_mode == WRAP_NONE || !current_line) {
				/* No wrap: go to end of logical line */
				editor.cursor_column = line_length;
			} else {
				/* Wrap enabled: go to end of current segment */
				line_ensure_wrap_cache(current_line, &editor.buffer);
				uint16_t segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t segment_end = line_get_segment_end(
					current_line, &editor.buffer, segment);
				if (editor.cursor_column == segment_end &&
				    segment + 1 < current_line->wrap_segment_count) {
					/* Already at segment end, go to next segment end */
					segment_end = line_get_segment_end(
						current_line, &editor.buffer, segment + 1);
				}
				editor.cursor_column = segment_end;
			}
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

	/* Save text before deleting for undo */
	size_t text_length;
	char *text = selection_get_text(&text_length);
	if (text != NULL) {
		undo_record_delete_text(&editor.buffer, start_row, start_col,
		                        end_row, end_col, text, text_length);
		free(text);
	}

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

	undo_begin_group(&editor.buffer);

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
			undo_record_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
			buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
			editor.cursor_row++;
			editor.cursor_column = 0;
		} else if (codepoint == '\r') {
			/* Skip carriage returns (Windows line endings) */
		} else {
			undo_record_insert_char(&editor.buffer, editor.cursor_row,
			                        editor.cursor_column, codepoint);
			buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row,
			                             editor.cursor_column, codepoint);
			editor.cursor_column++;
		}

		chars_inserted++;
		offset += bytes;
	}

	undo_end_group(&editor.buffer);

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
	undo_begin_group(&editor.buffer);

	/* Delete selection first if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	undo_record_insert_char(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	editor.cursor_column++;
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;

	undo_end_group(&editor.buffer);
}

/*
 * Handle Enter key by splitting the current line at the cursor position.
 * Moves cursor to the beginning of the newly created line below. If there's
 * an active selection, deletes it first. The new line inherits the leading
 * whitespace (tabs and spaces) from the original line for auto-indentation.
 */
static void editor_insert_newline(void)
{
	undo_begin_group(&editor.buffer);

	/*
	 * Capture leading whitespace before any modifications. We limit the
	 * indent to the cursor column since content before cursor stays on
	 * the current line.
	 */
	uint32_t indent_count = 0;
	uint32_t indent_chars[256];

	if (editor.cursor_row < editor.buffer.line_count) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		line_warm(line, &editor.buffer);

		while (indent_count < line->cell_count && indent_count < 256) {
			uint32_t codepoint = line->cells[indent_count].codepoint;
			if (codepoint != ' ' && codepoint != '\t') {
				break;
			}
			indent_chars[indent_count] = codepoint;
			indent_count++;
		}

		/* Don't copy more indent than cursor position */
		if (indent_count > editor.cursor_column) {
			indent_count = editor.cursor_column;
		}
	}

	/* Delete selection first if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
		/* After deletion, re-evaluate indent based on new cursor position */
		indent_count = 0;
		if (editor.cursor_row < editor.buffer.line_count) {
			struct line *line = &editor.buffer.lines[editor.cursor_row];
			line_warm(line, &editor.buffer);

			while (indent_count < line->cell_count && indent_count < 256) {
				uint32_t codepoint = line->cells[indent_count].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				indent_chars[indent_count] = codepoint;
				indent_count++;
			}

			if (indent_count > editor.cursor_column) {
				indent_count = editor.cursor_column;
			}
		}
	}

	undo_record_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	editor.cursor_row++;
	editor.cursor_column = 0;

	/* Insert auto-indent characters on the new line */
	for (uint32_t i = 0; i < indent_count; i++) {
		undo_record_insert_char(&editor.buffer, editor.cursor_row, editor.cursor_column, indent_chars[i]);
		buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column, indent_chars[i]);
		editor.cursor_column++;
	}

	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;

	undo_end_group(&editor.buffer);
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
		undo_begin_group(&editor.buffer);
		editor_delete_selection();
		undo_end_group(&editor.buffer);
		return;
	}

	if (editor.cursor_row >= editor.buffer.line_count) {
		return;
	}

	undo_begin_group(&editor.buffer);

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	if (editor.cursor_column < line->cell_count) {
		/* Deleting a character - record it before deletion */
		uint32_t codepoint = line->cells[editor.cursor_column].codepoint;
		undo_record_delete_char(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	} else if (editor.cursor_row + 1 < editor.buffer.line_count) {
		/* Joining with next line - record newline deletion */
		undo_record_delete_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	}

	buffer_delete_grapheme_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column);
	editor.quit_confirm_counter = QUIT_CONFIRM_COUNT;

	undo_end_group(&editor.buffer);
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
		undo_begin_group(&editor.buffer);
		editor_delete_selection();
		undo_end_group(&editor.buffer);
		return;
	}

	if (editor.cursor_row == 0 && editor.cursor_column == 0) {
		return;
	}

	undo_begin_group(&editor.buffer);

	if (editor.cursor_column > 0) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		line_warm(line, &editor.buffer);
		uint32_t new_column = cursor_prev_grapheme(line, &editor.buffer, editor.cursor_column);
		/* Record the character we're about to delete */
		uint32_t codepoint = line->cells[new_column].codepoint;
		undo_record_delete_char(&editor.buffer, editor.cursor_row, new_column, codepoint);
		editor.cursor_column = new_column;
		buffer_delete_grapheme_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column);
	} else {
		/* Join with previous line - record newline deletion at end of previous line */
		uint32_t previous_line_length = editor_get_line_length(editor.cursor_row - 1);
		undo_record_delete_newline(&editor.buffer, editor.cursor_row - 1, previous_line_length);
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

	undo_end_group(&editor.buffer);

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
			uint32_t file_row;
			uint16_t segment;
			uint32_t cell_col;

			/* Account for gutter in screen column */
			uint32_t screen_col = mouse->column;
			if (screen_col < editor.gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= editor.gutter_width;
			}

			/* Map screen row to logical line and segment */
			if (screen_row_to_line_segment(mouse->row, &file_row, &segment)) {
				/*
				 * Convert visual position to cell column.
				 * In wrap mode, visual position is just screen_col.
				 * In no-wrap mode, add column_offset.
				 */
				struct line *line = &editor.buffer.lines[file_row];
				if (editor.wrap_mode != WRAP_NONE) {
					cell_col = line_find_column_at_visual(
						line, &editor.buffer, segment, screen_col);
				} else {
					cell_col = screen_column_to_cell(
						file_row, screen_col + editor.column_offset);
				}
			} else {
				/* Click below content: go to last line */
				if (editor.buffer.line_count > 0) {
					file_row = editor.buffer.line_count - 1;
				} else {
					file_row = 0;
				}
				segment = 0;
				cell_col = 0;
			}

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
			uint32_t file_row;
			uint16_t segment;
			uint32_t cell_col;

			uint32_t screen_col = mouse->column;
			if (screen_col < editor.gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= editor.gutter_width;
			}

			/* Map screen row to logical line and segment */
			if (screen_row_to_line_segment(mouse->row, &file_row, &segment)) {
				struct line *line = &editor.buffer.lines[file_row];
				if (editor.wrap_mode != WRAP_NONE) {
					cell_col = line_find_column_at_visual(
						line, &editor.buffer, segment, screen_col);
				} else {
					cell_col = screen_column_to_cell(
						file_row, screen_col + editor.column_offset);
				}
			} else {
				/* Drag below content: clamp to last line */
				if (editor.buffer.line_count > 0) {
					file_row = editor.buffer.line_count - 1;
				} else {
					file_row = 0;
				}
				cell_col = 0;
			}

			editor.cursor_row = file_row;
			editor.cursor_column = cell_col;

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
			/* In search mode, find previous match */
			if (search.active) {
				search.direction = -1;
				if (!search_find_previous(true)) {
					editor_set_status_message("No more matches");
				}
				break;
			}

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
			/* In search mode, find next match */
			if (search.active) {
				search.direction = 1;
				if (!search_find_next(true)) {
					editor_set_status_message("No more matches");
				}
				break;
			}

			uint32_t scroll_amount = calculate_adaptive_scroll(1);

			/* Calculate maximum valid offset (wrap-aware) */
			uint32_t max_offset = calculate_max_row_offset();

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
 * Incremental Search
 *****************************************************************************/

/*
 * Enter search mode. Saves current cursor position and initializes search state.
 */
static void search_enter(void)
{
	search.active = true;
	search.replace_mode = false;
	search.query[0] = '\0';
	search.query_length = 0;
	search.replace_text[0] = '\0';
	search.replace_length = 0;
	search.editing_replace = false;
	search.saved_cursor_row = editor.cursor_row;
	search.saved_cursor_column = editor.cursor_column;
	search.saved_row_offset = editor.row_offset;
	search.saved_column_offset = editor.column_offset;
	search.has_match = false;
	search.direction = 1;
}

/*
 * Enter find & replace mode. Similar to search_enter but enables replace UI.
 */
static void replace_enter(void)
{
	search.active = true;
	search.replace_mode = true;
	search.query[0] = '\0';
	search.query_length = 0;
	search.replace_text[0] = '\0';
	search.replace_length = 0;
	search.editing_replace = false;
	search.saved_cursor_row = editor.cursor_row;
	search.saved_cursor_column = editor.cursor_column;
	search.saved_row_offset = editor.row_offset;
	search.saved_column_offset = editor.column_offset;
	search.has_match = false;
	search.direction = 1;
}

/*
 * Exit search mode, optionally restoring cursor position.
 */
static void search_exit(bool restore_position)
{
	if (restore_position) {
		editor.cursor_row = search.saved_cursor_row;
		editor.cursor_column = search.saved_cursor_column;
		editor.row_offset = search.saved_row_offset;
		editor.column_offset = search.saved_column_offset;
	}
	search.active = false;
	search.replace_mode = false;
	search.has_match = false;
}

/*
 * Check if the query matches at a specific position in a line.
 * Returns true if query matches starting at the given column.
 * Matching is case-insensitive for ASCII letters.
 */
static bool search_matches_at(struct line *line, struct buffer *buffer,
                              uint32_t column, const char *query, uint32_t query_len)
{
	if (query_len == 0) {
		return false;
	}

	line_warm(line, buffer);

	uint32_t query_offset = 0;
	uint32_t cell_index = column;

	while (query_offset < query_len && cell_index < line->cell_count) {
		/* Decode next codepoint from query */
		uint32_t query_cp;
		int bytes = utflite_decode(query + query_offset, query_len - query_offset, &query_cp);
		if (bytes <= 0) break;

		uint32_t cell_cp = line->cells[cell_index].codepoint;

		/* Case-insensitive comparison for ASCII */
		if (query_cp >= 'A' && query_cp <= 'Z') query_cp += 32;
		if (cell_cp >= 'A' && cell_cp <= 'Z') cell_cp += 32;

		if (query_cp != cell_cp) {
			return false;
		}

		query_offset += bytes;
		cell_index++;
	}

	/* Match if we consumed the entire query */
	return query_offset >= query_len;
}

/*
 * Count the number of cells the query occupies (for highlighting width).
 */
static uint32_t search_query_cell_count(const char *query, uint32_t query_len)
{
	uint32_t count = 0;
	uint32_t offset = 0;
	while (offset < query_len) {
		uint32_t cp;
		int bytes = utflite_decode(query + offset, query_len - offset, &cp);
		if (bytes <= 0) break;
		count++;
		offset += bytes;
	}
	return count;
}

/*
 * Center the viewport vertically on the current match.
 * Called after finding a match to snap it to the middle of the screen.
 */
static void search_center_on_match(void)
{
	if (!search.has_match) {
		return;
	}

	/* Calculate the row offset that would center the match */
	uint32_t target_row = search.match_row;
	uint32_t half_screen = editor.screen_rows / 2;

	if (target_row >= half_screen) {
		editor.row_offset = target_row - half_screen;
	} else {
		editor.row_offset = 0;
	}

	/* Clamp to valid range (wrap-aware) */
	uint32_t max_offset = calculate_max_row_offset();
	if (editor.row_offset > max_offset) {
		editor.row_offset = max_offset;
	}
}

/*
 * Find the next match starting from current position.
 * If wrap is true, wraps around to beginning of file.
 * Returns true if a match was found.
 */
static bool search_find_next(bool wrap)
{
	if (search.query_length == 0) {
		return false;
	}

	uint32_t start_row = editor.cursor_row;
	uint32_t start_col = editor.cursor_column + 1;

	/* Search from current position to end of file */
	for (uint32_t row = start_row; row < editor.buffer.line_count; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		uint32_t col_start = (row == start_row) ? start_col : 0;

		for (uint32_t col = col_start; col < line->cell_count; col++) {
			if (search_matches_at(line, &editor.buffer, col, search.query, search.query_length)) {
				editor.cursor_row = row;
				editor.cursor_column = col;
				search.match_row = row;
				search.match_column = col;
				search.has_match = true;
				search_center_on_match();
				return true;
			}
		}
	}

	/* Wrap around to beginning */
	if (wrap) {
		for (uint32_t row = 0; row <= start_row; row++) {
			struct line *line = &editor.buffer.lines[row];
			line_warm(line, &editor.buffer);

			uint32_t col_end = (row == start_row) ? start_col : line->cell_count;

			for (uint32_t col = 0; col < col_end; col++) {
				if (search_matches_at(line, &editor.buffer, col, search.query, search.query_length)) {
					editor.cursor_row = row;
					editor.cursor_column = col;
					search.match_row = row;
					search.match_column = col;
					search.has_match = true;
					search_center_on_match();
					return true;
				}
			}
		}
	}

	search.has_match = false;
	return false;
}

/*
 * Find the previous match starting from current position.
 * If wrap is true, wraps around to end of file.
 */
static bool search_find_previous(bool wrap)
{
	if (search.query_length == 0) {
		return false;
	}

	int32_t start_row = (int32_t)editor.cursor_row;
	int32_t start_col = (int32_t)editor.cursor_column - 1;

	/* Search from current position to beginning of file */
	for (int32_t row = start_row; row >= 0; row--) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		int32_t col_start = (row == start_row) ? start_col : (int32_t)line->cell_count - 1;
		if (col_start < 0) continue;

		for (int32_t col = col_start; col >= 0; col--) {
			if (search_matches_at(line, &editor.buffer, (uint32_t)col, search.query, search.query_length)) {
				editor.cursor_row = (uint32_t)row;
				editor.cursor_column = (uint32_t)col;
				search.match_row = (uint32_t)row;
				search.match_column = (uint32_t)col;
				search.has_match = true;
				search_center_on_match();
				return true;
			}
		}
	}

	/* Wrap around to end */
	if (wrap && editor.buffer.line_count > 0) {
		for (int32_t row = (int32_t)editor.buffer.line_count - 1; row >= start_row; row--) {
			struct line *line = &editor.buffer.lines[row];
			line_warm(line, &editor.buffer);

			int32_t col_start = (row == start_row) ? start_col : (int32_t)line->cell_count - 1;
			if (col_start < 0) continue;

			for (int32_t col = col_start; col >= 0; col--) {
				if (search_matches_at(line, &editor.buffer, (uint32_t)col, search.query, search.query_length)) {
					editor.cursor_row = (uint32_t)row;
					editor.cursor_column = (uint32_t)col;
					search.match_row = (uint32_t)row;
					search.match_column = (uint32_t)col;
					search.has_match = true;
					search_center_on_match();
					return true;
				}
			}
		}
	}

	search.has_match = false;
	return false;
}

/*
 * Update search results when query changes. Finds first match from saved position.
 */
static void search_update(void)
{
	if (search.query_length == 0) {
		/* No query - restore to saved position */
		editor.cursor_row = search.saved_cursor_row;
		editor.cursor_column = search.saved_cursor_column;
		search.has_match = false;
		return;
	}

	/* Start search from saved position */
	editor.cursor_row = search.saved_cursor_row;
	editor.cursor_column = search.saved_cursor_column;

	/* First check if there's a match at current position */
	if (editor.cursor_row < editor.buffer.line_count) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		if (search_matches_at(line, &editor.buffer, editor.cursor_column,
		                      search.query, search.query_length)) {
			search.match_row = editor.cursor_row;
			search.match_column = editor.cursor_column;
			search.has_match = true;
			search_center_on_match();
			return;
		}
	}

	/* Otherwise find next match (with wrap) */
	/* Note: search_find_next already calls search_center_on_match */
	search_find_next(true);
}

/*
 * Count the number of cells (grapheme clusters) in a UTF-8 string.
 */
static uint32_t replace_count_cells(const char *text, uint32_t length)
{
	uint32_t count = 0;
	const char *p = text;
	const char *end = text + length;

	while (p < end) {
		uint32_t codepoint;
		int bytes = utflite_decode(p, end - p, &codepoint);
		if (bytes <= 0) {
			break;
		}
		p += bytes;
		count++;
	}

	return count;
}

/*
 * Replace the current match with the replacement text.
 * Returns true if a replacement was made.
 */
static bool search_replace_current(void)
{
	if (!search.has_match || search.query_length == 0) {
		return false;
	}

	if (editor.cursor_row >= editor.buffer.line_count) {
		return false;
	}

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	/* Verify match still exists at cursor position */
	if (!search_matches_at(line, &editor.buffer, editor.cursor_column,
	                       search.query, search.query_length)) {
		return false;
	}

	undo_begin_group(&editor.buffer);

	/* Calculate the number of cells in the match */
	uint32_t match_cells = search_query_cell_count(search.query, search.query_length);

	/* Delete the match characters (from end to start for correct undo) */
	for (uint32_t i = 0; i < match_cells; i++) {
		uint32_t delete_position = editor.cursor_column + match_cells - 1 - i;
		if (delete_position < line->cell_count) {
			uint32_t codepoint = line->cells[delete_position].codepoint;
			undo_record_delete_char(&editor.buffer, editor.cursor_row,
			                        delete_position, codepoint);

			/* Shift cells left */
			if (delete_position < line->cell_count - 1) {
				memmove(&line->cells[delete_position],
					&line->cells[delete_position + 1],
					(line->cell_count - delete_position - 1) * sizeof(struct cell));
			}
			line->cell_count--;
		}
	}

	/* Insert replacement text */
	const char *r = search.replace_text;
	const char *r_end = search.replace_text + search.replace_length;
	uint32_t insert_position = editor.cursor_column;

	while (r < r_end) {
		uint32_t codepoint;
		int bytes = utflite_decode(r, r_end - r, &codepoint);
		if (bytes <= 0) {
			break;
		}
		r += bytes;

		undo_record_insert_char(&editor.buffer, editor.cursor_row,
		                        insert_position, codepoint);

		/* Make room and insert */
		line_ensure_capacity(line, line->cell_count + 1);

		if (insert_position < line->cell_count) {
			memmove(&line->cells[insert_position + 1],
				&line->cells[insert_position],
				(line->cell_count - insert_position) * sizeof(struct cell));
		}

		line->cells[insert_position].codepoint = codepoint;
		line->cells[insert_position].syntax = SYNTAX_NORMAL;
		line->cells[insert_position].context = 0;
		line->cells[insert_position].neighbor = 0;
		line->cell_count++;
		insert_position++;
	}

	/* Recompute line metadata */
	line->temperature = LINE_TEMPERATURE_HOT;
	neighbor_compute_line(line);
	syntax_highlight_line(line, &editor.buffer, editor.cursor_row);
	line_invalidate_wrap_cache(line);

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	return true;
}

/*
 * Replace current match and find next.
 */
static void search_replace_and_next(void)
{
	if (search_replace_current()) {
		/* Move cursor past the replacement */
		uint32_t replace_cells = replace_count_cells(search.replace_text,
		                                              search.replace_length);
		editor.cursor_column += replace_cells;

		/* Find next match */
		if (!search_find_next(true)) {
			editor_set_status_message("Replaced. No more matches.");
		} else {
			editor_set_status_message("Replaced.");
		}
	}
}

/*
 * Replace all matches in the buffer.
 */
static void search_replace_all(void)
{
	if (search.query_length == 0) {
		return;
	}

	undo_begin_group(&editor.buffer);

	uint32_t count = 0;

	/* Save current position */
	uint32_t saved_row = editor.cursor_row;
	uint32_t saved_column = editor.cursor_column;

	/* Start from beginning of file */
	editor.cursor_row = 0;
	editor.cursor_column = 0;

	/* Find and replace all matches without wrapping */
	while (search_find_next(false)) {
		if (search_replace_current()) {
			count++;

			/* Move past replacement to avoid infinite loop */
			uint32_t replace_cells = replace_count_cells(search.replace_text,
			                                              search.replace_length);
			editor.cursor_column += replace_cells;
		} else {
			/* No replacement made, move forward to avoid infinite loop */
			editor.cursor_column++;
			if (editor.cursor_row < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[editor.cursor_row];
				line_warm(line, &editor.buffer);
				if (editor.cursor_column >= line->cell_count) {
					editor.cursor_row++;
					editor.cursor_column = 0;
				}
			}
		}
	}

	undo_end_group(&editor.buffer);

	/* Restore cursor to beginning if no replacements, or keep at last position */
	if (count == 0) {
		editor.cursor_row = saved_row;
		editor.cursor_column = saved_column;
	}

	search.has_match = false;
	editor_set_status_message("Replaced %u occurrence%s", count, count != 1 ? "s" : "");
}

/*
 * Check if a cell is part of a search match.
 * Returns: 0 = not a match, 1 = other match, 2 = current match
 */
static int search_match_type(uint32_t row, uint32_t column)
{
	if (!search.active || search.query_length == 0) {
		return 0;
	}

	/* Cache match_len to avoid O(n²) per render */
	static char cached_query[256];
	static uint32_t cached_query_length = 0;
	static uint32_t cached_match_len = 0;

	uint32_t match_len;
	if (cached_query_length == search.query_length &&
	    memcmp(cached_query, search.query, search.query_length) == 0) {
		match_len = cached_match_len;
	} else {
		match_len = search_query_cell_count(search.query, search.query_length);
		memcpy(cached_query, search.query, search.query_length);
		cached_query_length = search.query_length;
		cached_match_len = match_len;
	}

	/* Check if this cell is part of the current match */
	if (search.has_match && row == search.match_row &&
	    column >= search.match_column && column < search.match_column + match_len) {
		return 2;  /* Current match */
	}

	/* Check if this cell is part of any match */
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	/* Look backward to see if a match starts before this column */
	uint32_t check_start = (column >= match_len) ? column - match_len + 1 : 0;

	for (uint32_t col = check_start; col <= column; col++) {
		if (search_matches_at(line, &editor.buffer, col, search.query, search.query_length)) {
			if (column < col + match_len) {
				return 1;  /* Other match */
			}
		}
	}

	return 0;
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
 * Render a segment of a line's content to the output buffer.
 *
 * Two rendering modes:
 * 1. Segment mode (end_cell < UINT32_MAX): Render cells from start_cell to end_cell.
 *    Used for wrapped line segments.
 * 2. Scroll mode (end_cell == UINT32_MAX): Horizontal scroll - start_cell is a
 *    visual column offset, skip to that position then render. Used for WRAP_NONE.
 *
 * Parameters:
 *   output        - Output buffer to append rendered content to
 *   line          - The line to render (will be warmed if cold)
 *   buffer        - Parent buffer
 *   file_row      - Logical line number in file (for search/selection)
 *   start_cell    - First cell index, OR visual column offset if end_cell==UINT32_MAX
 *   end_cell      - Last cell index (exclusive), or UINT32_MAX for scroll mode
 *   max_width     - Maximum visual width to render
 *   is_cursor_line - True if cursor is on this line (for background highlight)
 */
static void render_line_content(struct output_buffer *output, struct line *line,
                                struct buffer *buffer, uint32_t file_row,
                                uint32_t start_cell, uint32_t end_cell,
                                int max_width, bool is_cursor_line)
{
	line_warm(line, buffer);

	int visual_column = 0;
	uint32_t cell_index = 0;

	/*
	 * Handle the two modes differently:
	 * - Scroll mode (UINT32_MAX): skip cells until we reach the visual column
	 * - Segment mode: jump directly to start_cell, computing visual column
	 */
	if (end_cell == UINT32_MAX) {
		/* Scroll mode: start_cell is actually a visual column offset */
		uint32_t column_offset = start_cell;
		while (cell_index < line->cell_count && visual_column < (int)column_offset) {
			uint32_t codepoint = line->cells[cell_index].codepoint;
			int width;
			if (codepoint == '\t') {
				width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			} else {
				width = utflite_codepoint_width(codepoint);
				if (width < 0) width = 1;
			}
			visual_column += width;
			cell_index++;
		}
		end_cell = line->cell_count;
	} else {
		/* Segment mode: compute visual column at start of segment */
		for (uint32_t i = 0; i < start_cell && i < line->cell_count; i++) {
			uint32_t codepoint = line->cells[i].codepoint;
			int width;
			if (codepoint == '\t') {
				width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			} else {
				width = utflite_codepoint_width(codepoint);
				if (width < 0) width = 1;
			}
			visual_column += width;
		}
		cell_index = start_cell;
		if (end_cell > line->cell_count) {
			end_cell = line->cell_count;
		}
	}

	/* Track current state to minimize escape sequences */
	enum syntax_token current_syntax = SYNTAX_NORMAL;
	int current_highlight = 0;  /* 0=normal, 1=selected, 2=search_other, 3=search_current */

	/* Set initial foreground color */
	render_set_syntax_color(output, current_syntax);

	/* Render visible content up to end_cell or max_width */
	int rendered_width = 0;
	while (cell_index < end_cell && rendered_width < max_width) {
		uint32_t codepoint = line->cells[cell_index].codepoint;
		enum syntax_token syntax = line->cells[cell_index].syntax;

		/*
		 * Determine highlight type with priority:
		 * search current > search other > selection > trailing ws > cursor line
		 */
		int highlight = 0;
		int match_type = search_match_type(file_row, cell_index);
		if (match_type == 2) {
			highlight = 3;  /* Current search match */
		} else if (match_type == 1) {
			highlight = 2;  /* Other search match */
		} else if (selection_contains(file_row, cell_index)) {
			highlight = 1;  /* Selection */
		} else if (is_trailing_whitespace(line, cell_index)) {
			highlight = 4;  /* Trailing whitespace */
		}

		/* Change colors if syntax or highlight changed */
		if (syntax != current_syntax || highlight != current_highlight) {
			char escape[64];
			struct syntax_color fg = THEME_COLORS[syntax];
			struct syntax_color bg;

			switch (highlight) {
				case 4:  /* Trailing whitespace - warning red */
					bg = THEME_TRAILING_WHITESPACE;
					break;
				case 3:  /* Current search match - gold */
					bg = THEME_SEARCH_CURRENT;
					break;
				case 2:  /* Other search match - blue */
					bg = THEME_SEARCH_MATCH;
					break;
				case 1:  /* Selection */
					bg = THEME_SELECTION;
					break;
				default: /* Normal or cursor line */
					bg = is_cursor_line ? THEME_CURSOR_LINE : THEME_BACKGROUND;
					break;
			}

			/* Ensure foreground has sufficient contrast with background */
			struct syntax_color adjusted_fg = color_ensure_contrast(fg, bg);

			snprintf(escape, sizeof(escape),
			         "\x1b[48;2;%d;%d;%dm\x1b[38;2;%d;%d;%dm",
			         bg.red, bg.green, bg.blue,
			         adjusted_fg.red, adjusted_fg.green, adjusted_fg.blue);
			output_buffer_append_string(output, escape);
			current_syntax = syntax;
			current_highlight = highlight;
		}

		int width;
		if (codepoint == '\t') {
			width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			/* Render tab with optional visible indicator */
			if (editor.show_whitespace) {
				/* Show → in subtle gray followed by spaces */
				char ws_escape[48];
				snprintf(ws_escape, sizeof(ws_escape),
				         "\x1b[38;2;%d;%d;%dm",
				         THEME_WHITESPACE.red, THEME_WHITESPACE.green,
				         THEME_WHITESPACE.blue);
				output_buffer_append_string(output, ws_escape);
				output_buffer_append_string(output, "→");
				rendered_width++;
				for (int i = 1; i < width && rendered_width < max_width; i++) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
				/* Restore syntax color */
				render_set_syntax_color(output, current_syntax);
			} else {
				/* Render spaces for tab */
				for (int i = 0; i < width && rendered_width < max_width; i++) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
			}
		} else if (codepoint == ' ' && editor.show_whitespace) {
			/* Show space as middle dot in subtle gray */
			char ws_escape[48];
			snprintf(ws_escape, sizeof(ws_escape),
			         "\x1b[38;2;%d;%d;%dm",
			         THEME_WHITESPACE.red, THEME_WHITESPACE.green,
			         THEME_WHITESPACE.blue);
			output_buffer_append_string(output, ws_escape);
			output_buffer_append_string(output, "·");
			render_set_syntax_color(output, current_syntax);
			rendered_width++;
			width = 1;
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

	/* Reset background - use cursor line color if on cursor line */
	struct syntax_color reset_bg = is_cursor_line ? THEME_CURSOR_LINE : THEME_BACKGROUND;
	char reset[32];
	snprintf(reset, sizeof(reset), "\x1b[48;2;%d;%d;%dm",
	         reset_bg.red, reset_bg.green, reset_bg.blue);
	output_buffer_append_string(output, reset);
	render_set_syntax_color(output, SYNTAX_NORMAL);
}

/*
 * Render all visible rows of the editor. For each screen row, draws
 * the line number gutter (if enabled) and line content. Handles soft
 * wrap by mapping screen rows to (logical_line, segment) pairs. Empty
 * rows past the end of the file are blank. Shows a centered welcome
 * message for empty buffers.
 */
static void render_draw_rows(struct output_buffer *output)
{
	uint32_t welcome_row = editor.screen_rows / 2;
	int text_area_width = editor.screen_columns - editor.gutter_width;

	/*
	 * Track current position in the buffer.
	 * file_row = logical line index
	 * segment = which segment of that line (0 = first/only)
	 */
	uint32_t file_row = editor.row_offset;
	uint16_t segment = 0;

	/*
	 * Cursor segment is needed to determine if we should highlight
	 * the current screen row as "cursor line".
	 */
	uint16_t cursor_segment = 0;
	if (editor.cursor_row < editor.buffer.line_count) {
		struct line *cursor_line = &editor.buffer.lines[editor.cursor_row];
		cursor_segment = line_get_segment_for_column(cursor_line,
		                                             &editor.buffer,
		                                             editor.cursor_column);
	}

	for (uint32_t screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
		output_buffer_append_string(output, "\x1b[2K");

		bool is_empty_buffer_first_line = (editor.buffer.line_count == 0 && file_row == 0);

		if (file_row >= editor.buffer.line_count && !is_empty_buffer_first_line) {
			/* Empty line past end of file */
			if (editor.buffer.line_count == 0 && screen_row == welcome_row) {
				char welcome[64];
				int welcome_length = snprintf(welcome, sizeof(welcome),
				                              "edit v%s", EDIT_VERSION);
				int padding = (text_area_width - welcome_length) / 2;
				if (padding < 0) padding = 0;

				for (uint32_t i = 0; i < editor.gutter_width; i++) {
					output_buffer_append_string(output, " ");
				}
				for (int i = 0; i < padding; i++) {
					output_buffer_append_string(output, " ");
				}
				output_buffer_append(output, welcome, welcome_length);
			} else if (editor.color_column > 0) {
				/* Draw color column marker on empty lines */
				uint32_t col_pos = editor.color_column - 1;
				if (col_pos < (uint32_t)text_area_width) {
					for (uint32_t i = 0; i < editor.gutter_width + col_pos; i++) {
						output_buffer_append_string(output, " ");
					}
					char col_escape[64];
					snprintf(col_escape, sizeof(col_escape),
					         "\x1b[48;2;%d;%d;%dm \x1b[48;2;%d;%d;%dm",
					         THEME_COLOR_COLUMN.red, THEME_COLOR_COLUMN.green,
					         THEME_COLOR_COLUMN.blue,
					         THEME_BACKGROUND.red, THEME_BACKGROUND.green,
					         THEME_BACKGROUND.blue);
					output_buffer_append_string(output, col_escape);
				}
			}
		} else if (file_row < editor.buffer.line_count) {
			struct line *line = &editor.buffer.lines[file_row];
			line_ensure_wrap_cache(line, &editor.buffer);

			/*
			 * Determine if this screen row should have cursor line highlight.
			 * Only highlight the segment containing the cursor.
			 */
			bool is_cursor_line_segment =
				(file_row == editor.cursor_row && segment == cursor_segment);

			/* Draw gutter: line number for segment 0, indicator for continuations */
			if (editor.show_line_numbers && editor.gutter_width > 0) {
				struct syntax_color ln_color = is_cursor_line_segment
					? THEME_LINE_NUMBER_ACTIVE : THEME_LINE_NUMBER;
				struct syntax_color ln_bg = is_cursor_line_segment
					? THEME_CURSOR_LINE : THEME_BACKGROUND;

				char color_escape[48];
				snprintf(color_escape, sizeof(color_escape),
				         "\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm",
				         ln_color.red, ln_color.green, ln_color.blue,
				         ln_bg.red, ln_bg.green, ln_bg.blue);
				output_buffer_append_string(output, color_escape);

				if (segment == 0) {
					/* First segment: show line number */
					char line_number_buffer[16];
					snprintf(line_number_buffer, sizeof(line_number_buffer),
					         "%*u ", editor.gutter_width - 1, file_row + 1);
					output_buffer_append(output, line_number_buffer,
					                     editor.gutter_width);
				} else {
					/* Continuation: show wrap indicator */
					const char *indicator = wrap_indicator_string(editor.wrap_indicator);
					/* Pad to align indicator same as line numbers (with trailing space) */
					for (uint32_t i = 0; i < editor.gutter_width - 2; i++) {
						output_buffer_append_string(output, " ");
					}
					output_buffer_append_string(output, indicator);
					output_buffer_append_string(output, " ");
				}
			}

			/* Calculate segment bounds */
			uint32_t start_cell = line_get_segment_start(line, &editor.buffer, segment);
			uint32_t end_cell = line_get_segment_end(line, &editor.buffer, segment);

			/*
			 * For WRAP_NONE mode, use horizontal scrolling.
			 * For wrap modes, render the segment directly.
			 */
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: use column_offset for horizontal scrolling */
				render_line_content(output, line, &editor.buffer, file_row,
				                    editor.column_offset, UINT32_MAX,
				                    text_area_width, is_cursor_line_segment);
			} else {
				/* Wrap enabled: render this segment */
				render_line_content(output, line, &editor.buffer, file_row,
				                    start_cell, end_cell,
				                    text_area_width, is_cursor_line_segment);
			}

			/* Fill rest of line with appropriate background */
			if (is_cursor_line_segment) {
				if (editor.color_column > 0) {
					/*
					 * Cursor line with color column: calculate position
					 * and draw the column marker with cursor line background.
					 */
					uint32_t line_visual_width = 0;
					for (uint32_t i = 0; i < line->cell_count; i++) {
						uint32_t cp = line->cells[i].codepoint;
						if (cp == '\t') {
							line_visual_width += TAB_STOP_WIDTH -
								(line_visual_width % TAB_STOP_WIDTH);
						} else {
							int w = utflite_codepoint_width(cp);
							line_visual_width += (w > 0) ? w : 1;
						}
					}
					uint32_t col_pos = editor.color_column - 1;
					if (col_pos >= line_visual_width &&
					    col_pos < line_visual_width + (uint32_t)text_area_width) {
						/* Set cursor line bg and fill to column */
						char cursor_bg[48];
						snprintf(cursor_bg, sizeof(cursor_bg),
						         "\x1b[48;2;%d;%d;%dm",
						         THEME_CURSOR_LINE.red,
						         THEME_CURSOR_LINE.green,
						         THEME_CURSOR_LINE.blue);
						output_buffer_append_string(output, cursor_bg);
						uint32_t spaces_before = col_pos - line_visual_width;
						for (uint32_t i = 0; i < spaces_before; i++) {
							output_buffer_append_string(output, " ");
						}
						/* Draw color column marker */
						char col_escape[64];
						snprintf(col_escape, sizeof(col_escape),
						         "\x1b[48;2;%d;%d;%dm \x1b[48;2;%d;%d;%dm\x1b[K",
						         THEME_COLOR_COLUMN.red,
						         THEME_COLOR_COLUMN.green,
						         THEME_COLOR_COLUMN.blue,
						         THEME_CURSOR_LINE.red,
						         THEME_CURSOR_LINE.green,
						         THEME_CURSOR_LINE.blue);
						output_buffer_append_string(output, col_escape);
					} else {
						/* Column not in visible area, just fill */
						char fill_escape[64];
						snprintf(fill_escape, sizeof(fill_escape),
						         "\x1b[48;2;%d;%d;%dm\x1b[K",
						         THEME_CURSOR_LINE.red,
						         THEME_CURSOR_LINE.green,
						         THEME_CURSOR_LINE.blue);
						output_buffer_append_string(output, fill_escape);
					}
				} else {
					char fill_escape[64];
					snprintf(fill_escape, sizeof(fill_escape),
					         "\x1b[48;2;%d;%d;%dm\x1b[K",
					         THEME_CURSOR_LINE.red,
					         THEME_CURSOR_LINE.green,
					         THEME_CURSOR_LINE.blue);
					output_buffer_append_string(output, fill_escape);
				}
				/* Reset to normal background */
				char reset_bg[48];
				snprintf(reset_bg, sizeof(reset_bg),
				         "\x1b[48;2;%d;%d;%dm",
				         THEME_BACKGROUND.red, THEME_BACKGROUND.green,
				         THEME_BACKGROUND.blue);
				output_buffer_append_string(output, reset_bg);
			} else if (editor.color_column > 0) {
				/*
				 * Draw color column marker in empty area if applicable.
				 * Calculate where we are and if the color column is visible.
				 */
				uint32_t line_visual_width = 0;
				line_warm(line, &editor.buffer);
				for (uint32_t i = 0; i < line->cell_count; i++) {
					uint32_t cp = line->cells[i].codepoint;
					if (cp == '\t') {
						line_visual_width += TAB_STOP_WIDTH -
							(line_visual_width % TAB_STOP_WIDTH);
					} else {
						int w = utflite_codepoint_width(cp);
						line_visual_width += (w > 0) ? w : 1;
					}
				}
				uint32_t col_pos = editor.color_column - 1;
				if (col_pos >= line_visual_width &&
				    col_pos < line_visual_width + (uint32_t)text_area_width) {
					uint32_t spaces_before = col_pos - line_visual_width;
					for (uint32_t i = 0; i < spaces_before; i++) {
						output_buffer_append_string(output, " ");
					}
					char col_escape[64];
					snprintf(col_escape, sizeof(col_escape),
					         "\x1b[48;2;%d;%d;%dm \x1b[48;2;%d;%d;%dm\x1b[K",
					         THEME_COLOR_COLUMN.red, THEME_COLOR_COLUMN.green,
					         THEME_COLOR_COLUMN.blue,
					         THEME_BACKGROUND.red, THEME_BACKGROUND.green,
					         THEME_BACKGROUND.blue);
					output_buffer_append_string(output, col_escape);
				}
			}

			/* Advance to next segment or line */
			segment++;
			if (segment >= line->wrap_segment_count) {
				segment = 0;
				file_row++;
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

	if (search.active) {
		char prompt[512];
		int len;

		if (search.replace_mode) {
			/*
			 * Replace mode: show both search and replace fields.
			 * Active field is indicated by brackets.
			 */
			const char *match_status = "";
			if (search.query_length > 0 && !search.has_match) {
				match_status = " (no match)";
			}

			if (search.editing_replace) {
				len = snprintf(prompt, sizeof(prompt),
				               "Find: %s%s | Replace: [%s]",
				               search.query, match_status, search.replace_text);
			} else {
				len = snprintf(prompt, sizeof(prompt),
				               "Find: [%s]%s | Replace: %s",
				               search.query, match_status, search.replace_text);
			}
		} else {
			/* Search-only mode */
			if (search.has_match) {
				len = snprintf(prompt, sizeof(prompt), "Search: %s", search.query);
			} else if (search.query_length > 0) {
				len = snprintf(prompt, sizeof(prompt), "Search: %s (no match)", search.query);
			} else {
				len = snprintf(prompt, sizeof(prompt), "Search: ");
			}
		}

		if (len > (int)editor.screen_columns) {
			len = editor.screen_columns;
		}
		output_buffer_append(output, prompt, len);
		return;
	}

	if (goto_line.active) {
		/* Draw go-to-line prompt */
		char prompt[64];
		int len = snprintf(prompt, sizeof(prompt), "Go to line: %s", goto_line.input);
		if (len > (int)editor.screen_columns) {
			len = editor.screen_columns;
		}
		output_buffer_append(output, prompt, len);
		return;
	}

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

	/* Position cursor - account for wrapped segments in wrap mode */
	char cursor_position[32];
	uint32_t cursor_screen_row;
	uint32_t cursor_screen_col;

	if (editor.wrap_mode == WRAP_NONE) {
		/* No wrap: simple calculation */
		cursor_screen_row = (editor.cursor_row - editor.row_offset) + 1;
		uint32_t render_column = editor_get_render_column(
			editor.cursor_row, editor.cursor_column);
		cursor_screen_col = (render_column - editor.column_offset) +
		                    editor.gutter_width + 1;
	} else {
		/*
		 * Wrap enabled: sum screen rows from row_offset to cursor_row,
		 * then add the cursor's segment within its line.
		 */
		cursor_screen_row = 1;  /* 1-based terminal rows */
		for (uint32_t row = editor.row_offset; row < editor.cursor_row &&
		     row < editor.buffer.line_count; row++) {
			struct line *line = &editor.buffer.lines[row];
			line_ensure_wrap_cache(line, &editor.buffer);
			cursor_screen_row += line->wrap_segment_count;
		}

		/* Add the segment offset within cursor's line */
		if (editor.cursor_row < editor.buffer.line_count) {
			struct line *cursor_line = &editor.buffer.lines[editor.cursor_row];
			line_ensure_wrap_cache(cursor_line, &editor.buffer);
			uint16_t cursor_segment = line_get_segment_for_column(
				cursor_line, &editor.buffer, editor.cursor_column);
			cursor_screen_row += cursor_segment;

			/* Column is visual position within segment */
			uint32_t visual_col = line_get_visual_column_in_segment(
				cursor_line, &editor.buffer, cursor_segment,
				editor.cursor_column);
			cursor_screen_col = visual_col + editor.gutter_width + 1;
		} else {
			cursor_screen_col = editor.gutter_width + 1;
		}
	}

	snprintf(cursor_position, sizeof(cursor_position), "\x1b[%u;%uH",
	         cursor_screen_row, cursor_screen_col);
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
/*
 * Handle a keypress in search mode. Returns true if the key was handled.
 * In replace mode, handles Tab to toggle fields, Enter to replace, and
 * Alt+A to replace all.
 */
static bool search_handle_key(int key)
{
	if (!search.active) {
		return false;
	}

	switch (key) {
		case '\x1b':  /* Escape - cancel search */
			search_exit(true);  /* Restore position */
			editor_set_status_message(search.replace_mode ? "Replace cancelled" : "Search cancelled");
			return true;

		case '\r':  /* Enter */
			if (search.replace_mode && search.has_match) {
				/* Replace current match and find next */
				search_replace_and_next();
			} else {
				/* Exit search, keep current position */
				search_exit(false);
				if (search.query_length > 0) {
					editor_set_status_message("Found: %s", search.query);
				}
			}
			return true;

		case '\t':  /* Tab */
			if (search.replace_mode) {
				/* Toggle between search and replace fields */
				search.editing_replace = !search.editing_replace;
			} else if (search.has_match) {
				/* In search-only mode, skip to next match */
				search.direction = 1;
				if (!search_find_next(true)) {
					editor_set_status_message("No more matches");
				}
			}
			return true;

		case KEY_ALT_A:
			/* Replace all (only in replace mode) */
			if (search.replace_mode) {
				search_replace_all();
			}
			return true;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			if (search.replace_mode && search.editing_replace) {
				/* Delete from replace field */
				if (search.replace_length > 0) {
					uint32_t i = search.replace_length - 1;
					while (i > 0 && (search.replace_text[i] & 0xC0) == 0x80) {
						i--;
					}
					search.replace_length = i;
					search.replace_text[search.replace_length] = '\0';
				}
			} else {
				/* Delete from search field */
				if (search.query_length > 0) {
					uint32_t i = search.query_length - 1;
					while (i > 0 && (search.query[i] & 0xC0) == 0x80) {
						i--;
					}
					search.query_length = i;
					search.query[search.query_length] = '\0';
					search_update();
				}
			}
			return true;

		case KEY_ALT_N:
		case KEY_ARROW_DOWN:
		case KEY_ARROW_RIGHT:
			/* Find next match */
			search.direction = 1;
			if (!search_find_next(true)) {
				editor_set_status_message("No more matches");
			}
			return true;

		case KEY_ALT_P:
		case KEY_ARROW_UP:
		case KEY_ARROW_LEFT:
			/* Find previous match */
			search.direction = -1;
			if (!search_find_previous(true)) {
				editor_set_status_message("No more matches");
			}
			return true;

		default:
			/* Insert printable character */
			if ((key >= 32 && key < 127) || key >= 128) {
				char utf8[4];
				int bytes = utflite_encode((uint32_t)key, utf8);

				if (search.replace_mode && search.editing_replace) {
					/* Insert into replace field */
					if (bytes > 0 && search.replace_length + (uint32_t)bytes < sizeof(search.replace_text) - 1) {
						memcpy(search.replace_text + search.replace_length, utf8, bytes);
						search.replace_length += bytes;
						search.replace_text[search.replace_length] = '\0';
					}
				} else {
					/* Insert into search field */
					if (bytes > 0 && search.query_length + (uint32_t)bytes < sizeof(search.query) - 1) {
						memcpy(search.query + search.query_length, utf8, bytes);
						search.query_length += bytes;
						search.query[search.query_length] = '\0';
						search_update();
					}
				}
				return true;
			}
			break;
	}

	return false;
}

/*****************************************************************************
 * Go to Line
 *****************************************************************************/

/*
 * Enter go-to-line mode. Saves cursor position for cancel.
 */
static void goto_enter(void)
{
	goto_line.active = true;
	goto_line.input[0] = '\0';
	goto_line.input_length = 0;
	goto_line.saved_cursor_row = editor.cursor_row;
	goto_line.saved_cursor_column = editor.cursor_column;
	goto_line.saved_row_offset = editor.row_offset;
	editor_set_status_message("");
}

/*
 * Exit go-to-line mode, optionally restoring cursor position.
 */
static void goto_exit(bool restore)
{
	if (restore) {
		editor.cursor_row = goto_line.saved_cursor_row;
		editor.cursor_column = goto_line.saved_cursor_column;
		editor.row_offset = goto_line.saved_row_offset;
	}
	goto_line.active = false;
}

/*
 * Execute the go-to-line command. Jumps to the line number in input.
 */
static void goto_execute(void)
{
	if (goto_line.input_length == 0) {
		goto_exit(true);
		return;
	}

	/* Parse line number */
	long line_num = strtol(goto_line.input, NULL, 10);

	if (line_num < 1) {
		line_num = 1;
	}
	if (line_num > (long)editor.buffer.line_count) {
		line_num = editor.buffer.line_count;
	}
	if (editor.buffer.line_count == 0) {
		line_num = 1;
	}

	/* Jump to line (1-indexed input, 0-indexed internal) */
	editor.cursor_row = (uint32_t)(line_num - 1);
	editor.cursor_column = 0;

	/* Center the line on screen */
	uint32_t half_screen = editor.screen_rows / 2;
	if (editor.cursor_row >= half_screen) {
		editor.row_offset = editor.cursor_row - half_screen;
	} else {
		editor.row_offset = 0;
	}

	/* Clamp row_offset (wrap-aware) */
	uint32_t max_offset = calculate_max_row_offset();
	if (editor.row_offset > max_offset) {
		editor.row_offset = max_offset;
	}

	selection_clear();
	goto_exit(false);
	editor_set_status_message("Line %ld", line_num);
}

/*
 * Handle keypress in go-to-line mode. Returns true if handled.
 */
static bool goto_handle_key(int key)
{
	if (!goto_line.active) {
		return false;
	}

	switch (key) {
		case '\x1b':  /* Escape - cancel */
			goto_exit(true);
			editor_set_status_message("Cancelled");
			return true;

		case '\r':  /* Enter - execute */
			goto_execute();
			return true;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			if (goto_line.input_length > 0) {
				goto_line.input_length--;
				goto_line.input[goto_line.input_length] = '\0';
			}
			return true;

		default:
			/* Accept digits only */
			if (key >= '0' && key <= '9') {
				if (goto_line.input_length < sizeof(goto_line.input) - 1) {
					goto_line.input[goto_line.input_length++] = (char)key;
					goto_line.input[goto_line.input_length] = '\0';

					/* Live preview: jump as user types */
					long line_num = strtol(goto_line.input, NULL, 10);
					if (line_num >= 1 && line_num <= (long)editor.buffer.line_count) {
						editor.cursor_row = (uint32_t)(line_num - 1);
						editor.cursor_column = 0;

						/* Center on screen */
						uint32_t half_screen = editor.screen_rows / 2;
						if (editor.cursor_row >= half_screen) {
							editor.row_offset = editor.cursor_row - half_screen;
						} else {
							editor.row_offset = 0;
						}
					}
				}
				return true;
			}
			break;
	}

	return true;  /* Consume all keys in goto mode */
}

/*****************************************************************************
 * Line Operations - Delete and Duplicate
 *****************************************************************************/

/*
 * Select all text in the buffer.
 */
static void editor_select_all(void)
{
	if (editor.buffer.line_count == 0) {
		return;
	}

	/* Anchor at start */
	editor.selection_anchor_row = 0;
	editor.selection_anchor_column = 0;

	/* Cursor at end */
	editor.cursor_row = editor.buffer.line_count - 1;
	struct line *last_line = &editor.buffer.lines[editor.cursor_row];
	line_warm(last_line, &editor.buffer);
	editor.cursor_column = last_line->cell_count;

	editor.selection_active = true;
	editor_set_status_message("Selected all");
}

/*
 * Delete the current line. Records undo operation for the deleted content.
 */
static void editor_delete_line(void)
{
	if (editor.buffer.line_count == 0) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row >= editor.buffer.line_count) {
		row = editor.buffer.line_count - 1;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	undo_begin_group(&editor.buffer);

	/* Build text to save for undo: line content + newline (if not last line) */
	size_t text_capacity = line->cell_count * 4 + 2;
	char *text = malloc(text_capacity);
	size_t text_len = 0;

	if (text != NULL) {
		for (uint32_t i = 0; i < line->cell_count; i++) {
			char utf8[4];
			int bytes = utflite_encode(line->cells[i].codepoint, utf8);
			if (bytes > 0) {
				memcpy(text + text_len, utf8, bytes);
				text_len += bytes;
			}
		}
		if (row < editor.buffer.line_count - 1) {
			text[text_len++] = '\n';
		}
		text[text_len] = '\0';

		/* Record the deletion - from start of this line to start of next line */
		uint32_t end_row = (row < editor.buffer.line_count - 1) ? row + 1 : row;
		uint32_t end_col = (row < editor.buffer.line_count - 1) ? 0 : line->cell_count;
		undo_record_delete_text(&editor.buffer, row, 0, end_row, end_col, text, text_len);
		free(text);
	}

	/* Delete the line */
	buffer_delete_line(&editor.buffer, row);

	/* Handle empty buffer */
	if (editor.buffer.line_count == 0) {
		buffer_ensure_capacity(&editor.buffer, 1);
		line_init(&editor.buffer.lines[0]);
		editor.buffer.line_count = 1;
	}

	/* Adjust cursor */
	if (editor.cursor_row >= editor.buffer.line_count) {
		editor.cursor_row = editor.buffer.line_count - 1;
	}
	editor.cursor_column = 0;

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);
	selection_clear();

	editor_set_status_message("Line deleted");
}

/*
 * Duplicate the current line. Inserts a copy below the current line.
 */
static void editor_duplicate_line(void)
{
	if (editor.buffer.line_count == 0) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row >= editor.buffer.line_count) {
		row = editor.buffer.line_count - 1;
	}

	struct line *source = &editor.buffer.lines[row];
	line_warm(source, &editor.buffer);

	undo_begin_group(&editor.buffer);

	/* Save cursor at end of line, insert newline (creates a new line) */
	uint32_t saved_col = editor.cursor_column;

	editor.cursor_row = row;
	editor.cursor_column = source->cell_count;

	/* Record and insert newline */
	undo_record_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);

	/* Move to the new line */
	editor.cursor_row = row + 1;
	editor.cursor_column = 0;

	/* Re-get source pointer (may have moved due to realloc) */
	source = &editor.buffer.lines[row];

	/* Copy each character from source into the new line */
	for (uint32_t i = 0; i < source->cell_count; i++) {
		uint32_t codepoint = source->cells[i].codepoint;
		undo_record_insert_char(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
		buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
		editor.cursor_column++;
	}

	/* Restore cursor column on the duplicated line */
	struct line *dest = &editor.buffer.lines[row + 1];
	editor.cursor_column = saved_col;
	if (editor.cursor_column > dest->cell_count) {
		editor.cursor_column = dest->cell_count;
	}

	undo_end_group(&editor.buffer);

	editor_set_status_message("Line duplicated");
}

/*
 * Check if a line starts with a comment (// with optional leading whitespace).
 * Returns the column where the comment starts, or UINT32_MAX if not commented.
 */
static uint32_t line_comment_start(struct line *line)
{
	uint32_t column = 0;

	/* Skip leading whitespace */
	while (column < line->cell_count) {
		uint32_t codepoint = line->cells[column].codepoint;
		if (codepoint != ' ' && codepoint != '\t') {
			break;
		}
		column++;
	}

	/* Check for // */
	if (column + 1 < line->cell_count &&
	    line->cells[column].codepoint == '/' &&
	    line->cells[column + 1].codepoint == '/') {
		return column;
	}

	return UINT32_MAX;
}

/*
 * Toggle line comments on the current line or selection. Uses C-style //
 * comments. If all affected lines are commented, removes comments. Otherwise,
 * adds comments to all lines at the minimum indent level for alignment.
 */
static void editor_toggle_comment(void)
{
	uint32_t start_row, end_row;

	if (editor.selection_active && !selection_is_empty()) {
		uint32_t start_column, end_column;
		selection_get_range(&start_row, &start_column, &end_row, &end_column);
	} else {
		start_row = editor.cursor_row;
		end_row = editor.cursor_row;
	}

	if (start_row >= editor.buffer.line_count) {
		return;
	}
	if (end_row >= editor.buffer.line_count) {
		end_row = editor.buffer.line_count - 1;
	}

	/*
	 * Determine action: if ALL non-empty lines are commented, uncomment.
	 * Otherwise, comment all lines.
	 */
	bool all_commented = true;
	bool has_content = false;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		/* Skip empty lines for this check */
		uint32_t first_non_whitespace = 0;
		while (first_non_whitespace < line->cell_count) {
			uint32_t codepoint = line->cells[first_non_whitespace].codepoint;
			if (codepoint != ' ' && codepoint != '\t') {
				break;
			}
			first_non_whitespace++;
		}

		if (first_non_whitespace < line->cell_count) {
			has_content = true;
			if (line_comment_start(line) == UINT32_MAX) {
				all_commented = false;
				break;
			}
		}
	}

	/* If no content, nothing to do */
	if (!has_content) {
		return;
	}

	bool should_comment = !all_commented;

	undo_begin_group(&editor.buffer);

	/*
	 * Find the minimum indent across all lines with content. We insert
	 * comments at this position for visual alignment.
	 */
	uint32_t min_indent = UINT32_MAX;
	if (should_comment) {
		for (uint32_t row = start_row; row <= end_row; row++) {
			struct line *line = &editor.buffer.lines[row];

			/* Skip empty lines */
			if (line->cell_count == 0) {
				continue;
			}

			uint32_t indent = 0;
			while (indent < line->cell_count) {
				uint32_t codepoint = line->cells[indent].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				indent++;
			}

			/* Only count lines with content after whitespace */
			if (indent < line->cell_count && indent < min_indent) {
				min_indent = indent;
			}
		}
		if (min_indent == UINT32_MAX) {
			min_indent = 0;
		}
	}

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		if (should_comment) {
			/* Skip empty lines */
			if (line->cell_count == 0) {
				continue;
			}

			/* Check if line has content after whitespace */
			uint32_t first_content = 0;
			while (first_content < line->cell_count) {
				uint32_t codepoint = line->cells[first_content].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				first_content++;
			}
			if (first_content >= line->cell_count) {
				continue;
			}

			/* Insert "// " at min_indent position */
			uint32_t insert_position = min_indent;

			/* Make room and insert the three characters */
			line_ensure_capacity(line, line->cell_count + 3);

			/* Shift existing cells right by 3 */
			memmove(&line->cells[insert_position + 3],
				&line->cells[insert_position],
				(line->cell_count - insert_position) * sizeof(struct cell));

			/* Insert // and space */
			line->cells[insert_position].codepoint = '/';
			line->cells[insert_position].syntax = SYNTAX_COMMENT;
			line->cells[insert_position].context = 0;
			line->cells[insert_position].neighbor = 0;

			line->cells[insert_position + 1].codepoint = '/';
			line->cells[insert_position + 1].syntax = SYNTAX_COMMENT;
			line->cells[insert_position + 1].context = 0;
			line->cells[insert_position + 1].neighbor = 0;

			line->cells[insert_position + 2].codepoint = ' ';
			line->cells[insert_position + 2].syntax = SYNTAX_COMMENT;
			line->cells[insert_position + 2].context = 0;
			line->cells[insert_position + 2].neighbor = 0;

			line->cell_count += 3;
			line->temperature = LINE_TEMPERATURE_HOT;

			/* Record for undo (in reverse order for correct undo sequence) */
			undo_record_insert_char(&editor.buffer, row, insert_position, '/');
			undo_record_insert_char(&editor.buffer, row, insert_position + 1, '/');
			undo_record_insert_char(&editor.buffer, row, insert_position + 2, ' ');

			/* Adjust cursor if on this line */
			if (row == editor.cursor_row && editor.cursor_column >= insert_position) {
				editor.cursor_column += 3;
			}
		} else {
			/* Remove // (and optional trailing space) */
			uint32_t comment_start = line_comment_start(line);
			if (comment_start == UINT32_MAX) {
				continue;
			}

			/* Check if there's a space after // */
			uint32_t chars_to_remove = 2;
			if (comment_start + 2 < line->cell_count &&
			    line->cells[comment_start + 2].codepoint == ' ') {
				chars_to_remove = 3;
			}

			/* Record deletions for undo (from end to start) */
			for (uint32_t i = chars_to_remove; i > 0; i--) {
				uint32_t delete_position = comment_start + i - 1;
				uint32_t codepoint = line->cells[delete_position].codepoint;
				undo_record_delete_char(&editor.buffer, row, delete_position, codepoint);
			}

			/* Shift cells left to remove the comment */
			memmove(&line->cells[comment_start],
				&line->cells[comment_start + chars_to_remove],
				(line->cell_count - comment_start - chars_to_remove) * sizeof(struct cell));

			line->cell_count -= chars_to_remove;
			line->temperature = LINE_TEMPERATURE_HOT;

			/* Adjust cursor if on this line */
			if (row == editor.cursor_row && editor.cursor_column > comment_start) {
				if (editor.cursor_column >= comment_start + chars_to_remove) {
					editor.cursor_column -= chars_to_remove;
				} else {
					editor.cursor_column = comment_start;
				}
			}
		}

		/* Recompute line metadata */
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, row);
		line_invalidate_wrap_cache(line);
	}

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	uint32_t count = end_row - start_row + 1;
	editor_set_status_message("%s %u line%s",
				  should_comment ? "Commented" : "Uncommented",
				  count, count > 1 ? "s" : "");
}

/*
 * Swap two lines in the buffer. Does not record undo.
 */
static void buffer_swap_lines(struct buffer *buffer, uint32_t row1, uint32_t row2)
{
	if (row1 >= buffer->line_count || row2 >= buffer->line_count) {
		return;
	}
	struct line temp = buffer->lines[row1];
	buffer->lines[row1] = buffer->lines[row2];
	buffer->lines[row2] = temp;
}

/*
 * Move the current line up one position.
 */
static void editor_move_line_up(void)
{
	if (editor.buffer.line_count < 2) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row == 0) {
		return;
	}
	if (row >= editor.buffer.line_count) {
		row = editor.buffer.line_count - 1;
	}

	undo_begin_group(&editor.buffer);

	/* Swap with line above */
	buffer_swap_lines(&editor.buffer, row, row - 1);

	/* Invalidate wrap caches */
	line_invalidate_wrap_cache(&editor.buffer.lines[row]);
	line_invalidate_wrap_cache(&editor.buffer.lines[row - 1]);

	/* Move cursor up */
	editor.cursor_row--;

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	editor_set_status_message("Line moved up");
}

/*
 * Move the current line down one position.
 */
static void editor_move_line_down(void)
{
	if (editor.buffer.line_count < 2) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row >= editor.buffer.line_count - 1) {
		return;
	}

	undo_begin_group(&editor.buffer);

	/* Swap with line below */
	buffer_swap_lines(&editor.buffer, row, row + 1);

	/* Invalidate wrap caches */
	line_invalidate_wrap_cache(&editor.buffer.lines[row]);
	line_invalidate_wrap_cache(&editor.buffer.lines[row + 1]);

	/* Move cursor down */
	editor.cursor_row++;

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	editor_set_status_message("Line moved down");
}

/*
 * Indent the current line or all lines in selection.
 * Inserts a tab character at the start of each line.
 */
static void editor_indent_lines(void)
{
	uint32_t start_row, end_row;

	if (editor.selection_active && !selection_is_empty()) {
		uint32_t start_col, end_col;
		selection_get_range(&start_row, &start_col, &end_row, &end_col);
	} else {
		start_row = editor.cursor_row;
		end_row = editor.cursor_row;
	}

	if (start_row >= editor.buffer.line_count) {
		return;
	}
	if (end_row >= editor.buffer.line_count) {
		end_row = editor.buffer.line_count - 1;
	}

	undo_begin_group(&editor.buffer);

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		/* Skip empty lines */
		if (line->cell_count == 0) {
			continue;
		}

		/* Record for undo before inserting */
		undo_record_insert_char(&editor.buffer, row, 0, '\t');

		/* Insert tab at position 0 */
		line_insert_cell(line, 0, '\t');
		line->temperature = LINE_TEMPERATURE_HOT;

		/* Recompute line metadata */
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, row);
		line_invalidate_wrap_cache(line);
	}

	/* Adjust cursor column to account for inserted tab */
	if (editor.cursor_row >= start_row && editor.cursor_row <= end_row) {
		editor.cursor_column++;
	}

	/* Adjust selection anchor if needed */
	if (editor.selection_active) {
		if (editor.selection_anchor_row >= start_row &&
		    editor.selection_anchor_row <= end_row) {
			editor.selection_anchor_column++;
		}
	}

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	uint32_t count = end_row - start_row + 1;
	editor_set_status_message("Indented %u line%s", count, count > 1 ? "s" : "");
}

/*
 * Outdent the current line or all lines in selection.
 * Removes leading tab or spaces from each line.
 */
static void editor_outdent_lines(void)
{
	uint32_t start_row, end_row;

	if (editor.selection_active && !selection_is_empty()) {
		uint32_t start_col, end_col;
		selection_get_range(&start_row, &start_col, &end_row, &end_col);
	} else {
		start_row = editor.cursor_row;
		end_row = editor.cursor_row;
	}

	if (start_row >= editor.buffer.line_count) {
		return;
	}
	if (end_row >= editor.buffer.line_count) {
		end_row = editor.buffer.line_count - 1;
	}

	undo_begin_group(&editor.buffer);

	uint32_t lines_modified = 0;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		if (line->cell_count == 0) {
			continue;
		}

		uint32_t chars_to_remove = 0;

		/* Check what's at the start of the line */
		if (line->cells[0].codepoint == '\t') {
			chars_to_remove = 1;
		} else if (line->cells[0].codepoint == ' ') {
			/* Remove up to TAB_STOP_WIDTH spaces */
			while (chars_to_remove < (uint32_t)TAB_STOP_WIDTH &&
			       chars_to_remove < line->cell_count &&
			       line->cells[chars_to_remove].codepoint == ' ') {
				chars_to_remove++;
			}
		}

		if (chars_to_remove == 0) {
			continue;
		}

		/* Record and delete each character */
		for (uint32_t i = 0; i < chars_to_remove; i++) {
			undo_record_delete_char(&editor.buffer, row, 0, line->cells[0].codepoint);
			line_delete_cell(line, 0);
		}

		line->temperature = LINE_TEMPERATURE_HOT;
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, row);
		line_invalidate_wrap_cache(line);
		lines_modified++;

		/* Adjust cursor column */
		if (row == editor.cursor_row) {
			if (editor.cursor_column >= chars_to_remove) {
				editor.cursor_column -= chars_to_remove;
			} else {
				editor.cursor_column = 0;
			}
		}

		/* Adjust selection anchor if needed */
		if (editor.selection_active && row == editor.selection_anchor_row) {
			if (editor.selection_anchor_column >= chars_to_remove) {
				editor.selection_anchor_column -= chars_to_remove;
			} else {
				editor.selection_anchor_column = 0;
			}
		}
	}

	if (lines_modified > 0) {
		editor.buffer.is_modified = true;
	}

	undo_end_group(&editor.buffer);

	editor_set_status_message("Outdented %u line%s", lines_modified,
	                          lines_modified != 1 ? "s" : "");
}

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

	/* Handle search mode input first */
	if (search_handle_key(key)) {
		return;
	}

	/* Handle go-to-line mode input */
	if (goto_handle_key(key)) {
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

		case CONTROL_KEY('z'):
			editor_undo();
			break;

		case CONTROL_KEY('y'):
			editor_redo();
			break;

		case KEY_F2:
			editor.show_line_numbers = !editor.show_line_numbers;
			editor_update_gutter_width();
			editor_set_status_message("Line numbers %s", editor.show_line_numbers ? "on" : "off");
			break;

		case KEY_F3:
			editor.show_whitespace = !editor.show_whitespace;
			editor_set_status_message("Whitespace %s", editor.show_whitespace ? "visible" : "hidden");
			break;

		case KEY_F4:
			/* Cycle color column: 0 -> 80 -> 120 -> 0 */
			if (editor.color_column == 0) {
				editor.color_column = 80;
			} else if (editor.color_column == 80) {
				editor.color_column = 120;
			} else {
				editor.color_column = 0;
			}
			if (editor.color_column > 0) {
				editor_set_status_message("Color column at %u", editor.color_column);
			} else {
				editor_set_status_message("Color column off");
			}
			break;

		case KEY_ALT_Z:
			editor_cycle_wrap_mode();
			break;

		case KEY_ALT_SHIFT_Z:
			editor_cycle_wrap_indicator();
			break;

		case CONTROL_KEY('f'):
			search_enter();
			break;

		case CONTROL_KEY('r'):
			replace_enter();
			break;

		case CONTROL_KEY('g'):
			goto_enter();
			break;

		case CONTROL_KEY('a'):
			editor_select_all();
			break;

		case KEY_ALT_K:
			editor_delete_line();
			break;

		case KEY_ALT_D:
			editor_duplicate_line();
			break;

		case KEY_ALT_ARROW_UP:
			editor_move_line_up();
			break;

		case KEY_ALT_ARROW_DOWN:
			editor_move_line_down();
			break;

		case 0x1f:  /* Ctrl+/ in many terminals */
		case KEY_ALT_SLASH:
			editor_toggle_comment();
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

		case '\t':
			/* Tab indents when selection active, otherwise inserts tab */
			if (editor.selection_active && !selection_is_empty()) {
				editor_indent_lines();
			} else {
				editor_insert_character('\t');
			}
			break;

		case KEY_SHIFT_TAB:
			editor_outdent_lines();
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
