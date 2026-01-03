/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * types.h - Shared type definitions for edit
 *
 * This header contains all struct definitions, enums, and constants
 * that are shared across multiple modules. Include this header
 * (or edit.h) rather than defining types locally.
 */

#ifndef EDIT_TYPES_H
#define EDIT_TYPES_H

/* Feature test macros for PATH_MAX and other POSIX definitions */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <regex.h>

/*****************************************************************************
 * Version and Configuration Constants
 *****************************************************************************/

/* Current version of the editor, displayed in welcome message and status. */
#define EDIT_VERSION "0.2.4"

/* Number of spaces a tab character expands to when rendered. */
#define TAB_STOP_WIDTH 8

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

/* Maximum number of simultaneous cursors for multi-cursor editing. */
#define MAX_CURSORS 100

/* Theme directory and config file locations (relative to HOME) */
#define THEME_DIR "/.edit/themes/"
#define CONFIG_FILE "/.editrc"

/* Double-click timing threshold for dialogs (milliseconds) */
#define DIALOG_DOUBLE_CLICK_MS 400

/* Maximum matches to store (prevent memory explosion on huge files) */
#define MAX_SEARCH_MATCHES 100000

/* Threshold: use async search for files larger than this many lines */
#define ASYNC_SEARCH_THRESHOLD 5000

/* Worker thread queue sizes */
#define TASK_QUEUE_SIZE     32
#define RESULT_QUEUE_SIZE   64

/* Auto-save interval in seconds */
#define AUTOSAVE_INTERVAL 30

/* Maximum swap file size (skip auto-save for huge files) */
#define AUTOSAVE_MAX_SIZE (50 * 1024 * 1024)  /* 50 MB */

/* Minimum contrast ratio for WCAG AA compliance (normal text). */
#define WCAG_MIN_CONTRAST 4.5
/*****************************************************************************
 * Terminal Escape Sequences
 *
 * Named constants for ANSI escape sequences used throughout the editor.
 * Using constants instead of inline strings improves readability and
 * makes it easier to verify correct sequence lengths.
 *****************************************************************************/
/* Screen control */
#define ESCAPE_CLEAR_SCREEN "\x1b[2J"
#define ESCAPE_CLEAR_SCREEN_LENGTH 4
#define ESCAPE_CURSOR_HOME "\x1b[H"
#define ESCAPE_CURSOR_HOME_LENGTH 3
#define ESCAPE_CLEAR_SCREEN_HOME "\x1b[2J\x1b[H"
#define ESCAPE_CLEAR_SCREEN_HOME_LENGTH 7
/* Cursor visibility */
#define ESCAPE_CURSOR_SHOW "\x1b[?25h"
#define ESCAPE_CURSOR_SHOW_LENGTH 6
#define ESCAPE_CURSOR_HIDE "\x1b[?25l"
#define ESCAPE_CURSOR_HIDE_LENGTH 6
#define ESCAPE_CURSOR_POSITION_QUERY "\x1b[6n"
#define ESCAPE_CURSOR_POSITION_QUERY_LENGTH 4
/* Mouse tracking enable sequences */
#define ESCAPE_MOUSE_BUTTON_ENABLE "\x1b[?1000h"
#define ESCAPE_MOUSE_DRAG_ENABLE "\x1b[?1002h"
#define ESCAPE_MOUSE_SGR_ENABLE "\x1b[?1006h"
#define ESCAPE_MOUSE_SEQUENCE_LENGTH 8
/* Mouse tracking disable sequences */
#define ESCAPE_MOUSE_SGR_DISABLE "\x1b[?1006l"
#define ESCAPE_MOUSE_DRAG_DISABLE "\x1b[?1002l"
#define ESCAPE_MOUSE_BUTTON_DISABLE "\x1b[?1000l"
/* Text attribute reset */
#define ESCAPE_RESET "\x1b[0m"
#define ESCAPE_RESET_LENGTH 4
/* Line clearing */
#define ESCAPE_CLEAR_LINE "\x1b[2K"
#define ESCAPE_CLEAR_LINE_LENGTH 4
#define ESCAPE_CLEAR_TO_EOL "\x1b[K"
#define ESCAPE_CLEAR_TO_EOL_LENGTH 3
/*****************************************************************************
 * Numeric Constants
 *
 * Named constants for magic numbers used throughout the codebase.
 * Per CODING_STANDARDS.md, all numeric literals should be named constants.
 *****************************************************************************/
/* Time intervals */
#define AUTOSAVE_CHECK_INTERVAL_SECONDS 5
/* Window constraints */
#define MINIMUM_WINDOW_SIZE 10
#define STATUS_BAR_ROWS 2
/* Gutter (line number column) */
#define MINIMUM_GUTTER_DIGITS 1
#define GUTTER_PADDING 1
#define DECIMAL_BASE 10
/* Dialog dimensions */
#define DIALOG_MIN_WIDTH 40
#define DIALOG_MIN_HEIGHT 10
#define DIALOG_WIDTH_PERCENT 70
#define DIALOG_HEIGHT_PERCENT 50
#define DIALOG_SCREEN_MARGIN 2
/* Clipboard buffer sizes */
#define CLIPBOARD_INITIAL_CAPACITY 4096
#define CLIPBOARD_READ_CHUNK_SIZE 1024
/* Stdin reading */
#define STDIN_INITIAL_CAPACITY 65536
#define STDIN_READ_CHUNK_SIZE 4096
/* Syntax parsing */
#define BRACKET_STACK_SIZE 256
/* Color parsing */
#define HEX_COLOR_LENGTH 6
#define MAX_CONTRAST_ITERATIONS 20
/* Time conversion (nanoseconds) */
#define NSEC_PER_MSEC 1000000
#define NSEC_PER_SEC 1000000000
#define MSEC_PER_SEC 1000

/*****************************************************************************
 * Neighbor Layer Bit Field Layout
 *
 * Neighbor field layout (8 bits):
 * Bits 0-2: Character class (0-7)
 * Bits 3-4: Token position (0-3)
 * Bits 5-7: Reserved
 *****************************************************************************/

#define NEIGHBOR_CLASS_MASK     0x07
#define NEIGHBOR_CLASS_SHIFT    0
#define NEIGHBOR_POSITION_MASK  0x18
#define NEIGHBOR_POSITION_SHIFT 3

/*****************************************************************************
 * Context Field Bit Layout (Pair Entanglement)
 *
 * Context field layout (32 bits):
 * Bits 0-23:  Pair ID (up to 16 million unique pairs)
 * Bits 24-26: Pair type (0-7)
 * Bits 27-28: Pair role (0-3)
 * Bits 29-31: Reserved
 *****************************************************************************/

#define CONTEXT_PAIR_ID_MASK    0x00FFFFFF
#define CONTEXT_PAIR_TYPE_MASK  0x07000000
#define CONTEXT_PAIR_TYPE_SHIFT 24
#define CONTEXT_PAIR_ROLE_MASK  0x18000000
#define CONTEXT_PAIR_ROLE_SHIFT 27

/*****************************************************************************
 * Text Attributes
 *
 * Bit flags for text styling. Can be combined (e.g., ATTR_BOLD | ATTR_ITALIC).
 * These map to ANSI SGR (Select Graphic Rendition) codes.
 *****************************************************************************/

typedef uint8_t text_attr;

#define ATTR_NONE       0
#define ATTR_BOLD       (1 << 0)   /* SGR 1  - Bold/increased intensity */
#define ATTR_DIM        (1 << 1)   /* SGR 2  - Dim/decreased intensity */
#define ATTR_ITALIC     (1 << 2)   /* SGR 3  - Italic */
#define ATTR_UNDERLINE  (1 << 3)   /* SGR 4  - Single underline */
#define ATTR_REVERSE    (1 << 4)   /* SGR 7  - Swap fg/bg */
#define ATTR_STRIKE     (1 << 5)   /* SGR 9  - Strikethrough */
#define ATTR_CURLY      (1 << 6)   /* SGR 4:3 - Curly underline (modern terminals) */
#define ATTR_OVERLINE   (1 << 7)   /* SGR 53 - Overline (limited support) */

/*****************************************************************************
 * Key Codes
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

	/* Ctrl key combinations. */
	KEY_ALT_SHIFT_S = -64,
	KEY_CTRL_O = -63,
	KEY_CTRL_T = -62,
	KEY_CTRL_N = -61,
	KEY_CTRL_HOME = -50,
	KEY_CTRL_END = -49,

	/* Function keys. */
	KEY_F1 = -45,
	KEY_F3 = -48,
	KEY_SHIFT_F3 = -47,

	/* Alt key combinations. */
	KEY_ALT_L = -91,
	KEY_ALT_SHIFT_W = -90,
	KEY_ALT_SHIFT_C = -89,
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
	KEY_ALT_BRACKET = -73,
	KEY_ALT_C = -68,
	KEY_ALT_W = -67,
	KEY_ALT_R = -66,
	KEY_ALT_U = -65,

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

	/* Terminal resize event. */
	KEY_RESIZE = -2,

	/* Mouse events (handled internally, not returned). */
	KEY_MOUSE_EVENT = -3
};

/*****************************************************************************
 * Line Temperature (Lazy Loading)
 *****************************************************************************/

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

/*****************************************************************************
 * Syntax Highlighting
 *****************************************************************************/

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

/*****************************************************************************
 * Character Classes (Neighbor Layer)
 *****************************************************************************/

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

/*****************************************************************************
 * Pair Matching (Context Layer)
 *****************************************************************************/

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

/*****************************************************************************
 * Mouse Input
 *****************************************************************************/

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

/*****************************************************************************
 * Display Settings
 *****************************************************************************/

/* Wrap mode for handling long lines. */
enum wrap_mode {
	WRAP_NONE = 0,    /* No wrapping - horizontal scroll only */
	WRAP_WORD,        /* Wrap at word boundaries */
	WRAP_CHAR         /* Wrap at any character */
};

/* Visual indicator style for wrapped line continuations. */
enum wrap_indicator {
	WRAP_INDICATOR_NONE = 0,   /* Blank gutter on continuations */
	WRAP_INDICATOR_CORNER,     /* */
	WRAP_INDICATOR_HOOK,       /* */
	WRAP_INDICATOR_ARROW,      /* */
	WRAP_INDICATOR_DOT,        /* */
	WRAP_INDICATOR_FLOOR,      /* */
	WRAP_INDICATOR_BOTTOM,     /* */
	WRAP_INDICATOR_RETURN,     /* */
	WRAP_INDICATOR_BOX         /* */
};

/* Color column display style for the vertical ruler. */
enum color_column_style {
	COLOR_COLUMN_BACKGROUND = 0, /* Subtle background tint only */
	COLOR_COLUMN_SOLID,          /* U+2502 */
	COLOR_COLUMN_DASHED,         /* U+2506 */
	COLOR_COLUMN_DOTTED,         /* U+250A */
	COLOR_COLUMN_HEAVY           /* U+2503 */
};

/* Theme selector indicator styles for the theme picker. */
enum theme_indicator {
	THEME_INDICATOR_ASTERISK = 0, /* * */
	THEME_INDICATOR_BULLET,       /* ● */
	THEME_INDICATOR_DIAMOND,      /* ◆ */
	THEME_INDICATOR_TRIANGLE,     /* ▶ */
	THEME_INDICATOR_CHECK,        /* ✓ */
	THEME_INDICATOR_ARROW,        /* → */
	THEME_INDICATOR_DOT,          /* • */
	THEME_INDICATOR_CHEVRON       /* ❯ */
};

/*****************************************************************************
 * Color and Theme Types
 *****************************************************************************/

/* RGB color for syntax highlighting. */
struct syntax_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

/*
 * Complete style definition combining color and text attributes.
 * Used for styled UI elements that can have fg, bg, and text attributes.
 */
struct style {
	struct syntax_color fg;       /* Foreground color */
	struct syntax_color bg;       /* Background color */
	text_attr attr;             /* Text attributes (ATTR_* flags) */
};

/*
 * Complete theme definition with all UI and syntax colors.
 * Colors are stored as syntax_color structs (RGB).
 * Styled elements use struct style for fg + bg + attributes.
 */
struct theme {
	char *name;                           /* Theme name (from file or built-in) */

	/* =========================================================
	 * COLOR-ONLY FIELDS (backgrounds, no text attributes needed)
	 * ========================================================= */
	struct syntax_color background;       /* Main editor background */
	struct syntax_color foreground;       /* Default text foreground */
	struct syntax_color selection;        /* Selected text background */
	struct syntax_color search_match;     /* Search match background */
	struct syntax_color search_current;   /* Current search match background */
	struct syntax_color cursor_line;      /* Cursor line background */
	struct syntax_color color_column;     /* Color column background */
	struct syntax_color color_column_line; /* Color column line character */
	struct syntax_color trailing_ws;      /* Trailing whitespace warning bg */

	/* =========================================================
	 * STYLED FIELDS (fg + bg + attributes)
	 * ========================================================= */

	/* Line numbers */
	struct style line_number;             /* Inactive line numbers */
	struct style line_number_active;      /* Current line number */

	/* Gutter */
	struct style gutter;                  /* Line number area */
	struct style gutter_active;           /* Active line gutter */

	/* Status bar */
	struct style status;                  /* Status bar base style */
	struct style status_filename;         /* Filename display */
	struct style status_modified;         /* [+] modified indicator */
	struct style status_position;         /* Line/column position */

	/* Message bar */
	struct style message;                 /* Message bar base style */

	/* Prompt components */
	struct style prompt_label;            /* "Search:", "Save as:", etc. */
	struct style prompt_input;            /* User input text */
	struct style prompt_bracket;          /* Active field [ ] brackets */
	struct style prompt_warning;          /* Warning prompts */

	/* Search feedback */
	struct style search_options;          /* [CWR] options display */
	struct style search_nomatch;          /* "(no match)" text */
	struct style search_error;            /* Regex error indicator */

	/* Whitespace indicators */
	struct style whitespace;              /* Base whitespace style */
	struct style whitespace_tab;          /* Tab indicator */
	struct style whitespace_space;        /* Space indicator */

	/* Wrap and special lines */
	struct style wrap_indicator;          /* Wrap continuation marker */
	struct style empty_line;              /* Lines past EOF */
	struct style welcome;                 /* Welcome message */

	/* Bracket matching */
	struct style bracket_match;           /* Matching bracket highlight */

	/* Multi-cursor */
	struct style multicursor;             /* Secondary cursors */

	/* Dialog panel */
	struct style dialog;                  /* Dialog base style */
	struct style dialog_header;           /* Header/title bar */
	struct style dialog_footer;           /* Footer/hint bar */
	struct style dialog_highlight;        /* Selected/highlighted item */
	struct style dialog_directory;        /* Directory entries */

	/* Syntax highlighting (indexed by enum syntax_token) */
	struct style syntax[SYNTAX_TOKEN_COUNT];
	bool syntax_bg_set[SYNTAX_TOKEN_COUNT]; /* Track if bg was explicitly set */
};

/*****************************************************************************
 * Dialog State Types
 *****************************************************************************/

/*
 * Generic dialog state for modal popups.
 * Used by file browser, theme picker, and future dialogs.
 */
struct dialog_state {
	bool active;                    /* Is dialog currently open? */
	int selected_index;             /* Currently highlighted item */
	int scroll_offset;              /* First visible item index */
	int item_count;                 /* Total number of items */
	int visible_rows;               /* Number of visible list rows */
	int content_offset;             /* Rows from panel_top to list (default 1) */

	/* Panel dimensions (calculated on draw) */
	int panel_top;
	int panel_left;
	int panel_width;
	int panel_height;

	/* Mouse interaction */
	bool mouse_down;                /* Is left button currently held? */
	struct timespec last_click;     /* For double-click detection */
	int last_click_index;           /* Item index of last click */
};

/*
 * Represents a file or directory entry in the file browser.
 */
struct file_list_item {
	char *display_name;     /* Name with trailing / for directories */
	char *actual_name;      /* Actual filesystem name */
	bool is_directory;      /* True if this is a directory */
};

/*
 * Result codes for dialog input handling.
 */
enum dialog_result {
	DIALOG_CONTINUE,    /* Continue dialog loop */
	DIALOG_CONFIRM,     /* User confirmed selection (Enter or double-click) */
	DIALOG_CANCEL,      /* User cancelled (Escape) */
	DIALOG_NAVIGATE     /* Navigation occurred, redraw needed */
};

/*
 * State for the Open File dialog.
 */
struct open_file_state {
	struct dialog_state dialog;
	char current_path[PATH_MAX];
	struct file_list_item *items;
	int item_count;

	/* Fuzzy filter state. */
	char query[256];             /* Search query (UTF-8) */
	int query_length;            /* Length of query in bytes */
	int *filtered_indices;       /* Indices into items[] that match query */
	int *filtered_scores;        /* Score for each filtered item */
	int filtered_count;          /* Number of matching items */
};

/*
 * State for the Theme Picker dialog.
 */
struct theme_picker_state {
	struct dialog_state dialog;
	int restore_index;      /* Theme index to restore if cancelled */
};

/*****************************************************************************
 * Undo/Redo Types
 *****************************************************************************/

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

/*****************************************************************************
 * Cell and Line Structures
 *****************************************************************************/

/* Single character cell with metadata. */
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

	/* Temperature - MUST use atomic operations for thread safety. */
	_Atomic int temperature;

	/* Flag to prevent concurrent warming of the same line. */
	_Atomic bool warming_in_progress;

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

/*****************************************************************************
 * Buffer Structure
 *****************************************************************************/

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

	/* Modification time when file was loaded, for external change detection. */
	time_t file_mtime;

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

/*****************************************************************************
 * Output Buffer
 *****************************************************************************/

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

/*****************************************************************************
 * Cursor and Selection
 *****************************************************************************/

/*
 * A single cursor with optional selection for multi-cursor editing.
 * When cursor_count > 0, we use the cursors array instead of the
 * legacy cursor_row/cursor_column fields.
 */
struct cursor {
	uint32_t row;
	uint32_t column;
	uint32_t anchor_row;      /* Selection anchor (same as row/col if no selection) */
	uint32_t anchor_column;
	bool has_selection;
};

/*****************************************************************************
 * Editor State
 *****************************************************************************/

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
	bool show_file_icons;        /* Show file/folder icons in dialogs */
	bool show_hidden_files;      /* Show hidden files in file dialogs */
	int tab_width;               /* Tab stop width in columns (default: 8) */
	uint32_t color_column;       /* Column to highlight (0 = off) */
	enum color_column_style color_column_style;  /* Visual style for column */
	enum theme_indicator theme_indicator;  /* Current theme marker style */

	/* Fuzzy finder settings for file open dialog. */
	int fuzzy_max_depth;         /* Max directory recursion depth (default: 10) */
	int fuzzy_max_files;         /* Max files to index (default: 10000) */
	bool fuzzy_case_sensitive;   /* Case-sensitive matching (default: false) */

	/* Update check state. */
	bool update_available;       /* True if a newer version was found */
	char update_version[32];     /* Latest version string (e.g., "0.3.0") */

	/* Multi-cursor support. When cursor_count > 0, cursors[] is used
	 * instead of cursor_row/cursor_column/selection_* fields. */
	struct cursor cursors[MAX_CURSORS];
	uint32_t cursor_count;       /* Number of active cursors (0 = single cursor mode) */
	uint32_t primary_cursor;     /* Index of main cursor for scrolling */
};

/*****************************************************************************
 * Search State
 *****************************************************************************/

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

	/* Search options */
	bool case_sensitive;            /* Match exact case */
	bool whole_word;                /* Match complete words only */
	bool use_regex;                 /* Use regular expressions */

	/* Compiled regex state */
	regex_t compiled_regex;         /* Compiled pattern */
	bool regex_compiled;            /* True if compiled_regex is valid */
	char regex_error[128];          /* Error message if compilation failed */
};

/*
 * A single search match location.
 */
struct search_match {
	uint32_t row;
	uint32_t start_col;         /* Starting column (cell index) */
	uint32_t end_col;           /* Ending column (exclusive) */
};

/*
 * Search results from background search.
 * Protected by mutex for thread-safe access.
 */
struct search_results {
	struct search_match *matches;
	uint32_t match_count;
	uint32_t match_capacity;

	/* Progress tracking */
	uint32_t rows_searched;
	uint32_t total_rows;
	bool complete;

	/* The pattern these results are for */
	char pattern[256];
	bool use_regex;
	bool case_sensitive;
	bool whole_word;
};

/*
 * Background search state.
 */
struct async_search_state {
	/* Active search task */
	uint64_t task_id;
	bool active;

	/* Results (accessed from both threads) */
	struct search_results results;
	pthread_mutex_t results_mutex;
	bool mutex_initialized;

	/* Current match index for navigation */
	int32_t current_match_index;
};

/*****************************************************************************
 * Replace All State
 *****************************************************************************/

/*
 * A replacement to be applied.
 * Stores the match location and what to replace it with.
 */
struct replacement {
	uint32_t row;
	uint32_t start_col;
	uint32_t end_col;
	char *replacement_text;         /* Expanded replacement (with backrefs) */
	uint32_t replacement_len;       /* Length in bytes */
};

/*
 * Results from background replace-all search phase.
 */
struct replace_results {
	struct replacement *replacements;
	uint32_t count;
	uint32_t capacity;

	/* Progress */
	uint32_t rows_searched;
	uint32_t total_rows;
	bool search_complete;

	/* Apply progress (main thread) */
	uint32_t applied_count;
	bool apply_complete;
};

/*
 * Background replace-all state.
 */
struct async_replace_state {
	uint64_t task_id;
	bool active;

	/* Results (worker writes, main reads/applies) */
	struct replace_results results;
	pthread_mutex_t results_mutex;
	bool mutex_initialized;

	/* Original search parameters */
	char pattern[256];
	char replacement[256];
	bool use_regex;
	bool case_sensitive;
	bool whole_word;
};

/*****************************************************************************
 * Auto-Save and Crash Recovery
 *****************************************************************************/

/*
 * Auto-save state.
 */
struct autosave_state {
	char swap_path[PATH_MAX];       /* Path to swap file */
	bool swap_exists;               /* Is there an active swap file? */
	time_t last_save_time;          /* When we last auto-saved */
	time_t last_modify_time;        /* When buffer was last modified */
	uint64_t task_id;               /* Current auto-save task ID */
	bool save_pending;              /* Is a save in progress? */
	bool enabled;                   /* Is auto-save enabled? */
};

/*
 * A snapshot of buffer content for background saving.
 * Created by main thread, consumed by worker thread.
 */
struct buffer_snapshot {
	char **lines;           /* Array of UTF-8 line strings */
	uint32_t line_count;    /* Number of lines */
	char swap_path[PATH_MAX];
};

/*****************************************************************************
 * Mode States
 *****************************************************************************/

/* Go-to-line mode state. */
struct goto_state {
	bool active;
	char input[16];             /* Line number input buffer */
	uint32_t input_length;
	uint32_t saved_cursor_row;
	uint32_t saved_cursor_column;
	uint32_t saved_row_offset;
};

/* Save As mode state. */
struct save_as_state {
	bool active;
	char path[PATH_MAX];        /* Current path being edited */
	uint32_t path_length;       /* Length in bytes */
	uint32_t cursor_position;   /* Cursor position within path */
	bool confirm_overwrite;     /* Waiting for overwrite confirmation */
};

/* Quit prompt state - shown when quitting with unsaved changes. */
struct quit_prompt_state {
	bool active;
};

/* Reload prompt state - shown when file changes externally. */
struct reload_prompt_state {
	bool active;
};

/*****************************************************************************
 * Clipboard
 *****************************************************************************/

/* Clipboard tool detection (cached on first use). */
enum clipboard_tool {
	CLIPBOARD_UNKNOWN = 0,
	CLIPBOARD_XCLIP,
	CLIPBOARD_XSEL,
	CLIPBOARD_WL,        /* wl-copy / wl-paste */
	CLIPBOARD_INTERNAL   /* Fallback */
};

/*****************************************************************************
 * Worker Thread Types
 *****************************************************************************/

/*
 * Task types for background processing.
 */
enum task_type {
	TASK_NONE = 0,
	TASK_WARM_LINES,      /* Warm a range of lines (decode + syntax) */
	TASK_SEARCH,          /* Search for pattern in buffer */
	TASK_REPLACE_ALL,     /* Replace all occurrences */
	TASK_AUTOSAVE,        /* Write buffer to swap file */
	TASK_SHUTDOWN         /* Signal worker to exit */
};

/*
 * A task submitted to the worker thread.
 */
struct task {
	enum task_type type;
	uint64_t task_id;                   /* Unique ID for matching results */
	_Atomic bool cancelled;             /* Set by main thread to cancel */

	union {
		/* TASK_WARM_LINES */
		struct {
			uint32_t start_row;
			uint32_t end_row;           /* Exclusive */
			int priority;               /* Higher = more urgent */
		} warm;

		/* TASK_SEARCH */
		struct {
			char pattern[256];
			uint32_t start_row;
			uint32_t end_row;           /* Exclusive, 0 = entire buffer */
			bool use_regex;
			bool case_sensitive;
			bool whole_word;
		} search;

		/* TASK_REPLACE_ALL */
		struct {
			char pattern[256];
			char replacement[256];
			bool use_regex;
			bool case_sensitive;
			bool whole_word;
		} replace;

		/* TASK_AUTOSAVE */
		struct {
			char swap_path[PATH_MAX];
			/* Buffer snapshot is accessed via pending_snapshot */
		} autosave;
	};
};

/*
 * Result from a completed task.
 */
struct task_result {
	uint64_t task_id;
	enum task_type type;
	int error;                          /* 0 = success, negative = EEDIT_* */

	union {
		/* TASK_WARM_LINES result */
		struct {
			uint32_t lines_warmed;
			uint32_t lines_skipped;     /* Already warm */
		} warm;

		/* TASK_SEARCH result */
		struct {
			uint32_t match_count;
			uint32_t rows_searched;
			bool complete;              /* False if cancelled mid-search */
		} search;

		/* TASK_REPLACE_ALL result */
		struct {
			uint32_t replacements;
			bool complete;
		} replace;

		/* TASK_AUTOSAVE result */
		struct {
			bool success;
			size_t bytes_written;
		} autosave;
	};
};

/*
 * Global worker thread state.
 */
struct worker_state {
	/* Thread handle */
	pthread_t thread;
	bool initialized;

	/* Shutdown flag */
	_Atomic bool shutdown;

	/* Task queue (ring buffer) */
	struct task *task_queue;
	uint32_t task_head;                 /* Next slot to read */
	uint32_t task_tail;                 /* Next slot to write */
	uint32_t task_count;

	/* Result queue (ring buffer) */
	struct task_result *result_queue;
	uint32_t result_head;
	uint32_t result_tail;
	uint32_t result_count;

	/* Synchronization */
	pthread_mutex_t task_mutex;
	pthread_mutex_t result_mutex;
	pthread_cond_t task_cond;           /* Signal when task available */

	/* Task ID counter */
	_Atomic uint64_t next_task_id;

	/* Currently executing task (for cancellation) */
	struct task *current_task;
};

#endif /* EDIT_TYPES_H */
