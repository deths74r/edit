/*** Includes ***/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "gstr.h"

/*** Defines ***/

#ifndef EDIT_VERSION
#define EDIT_VERSION "unknown"
#endif

/* Error codes for operations that can fail gracefully. */
enum editor_error {
	EDITOR_OK = 0,
	EDITOR_ERROR_IO,
	EDITOR_ERROR_MEMORY,
	EDITOR_ERROR_TERMINAL,
	EDITOR_ERROR_FILE_NOT_FOUND,
	EDITOR_ERROR_PERMISSION_DENIED
};

/* Number of spaces used to render a tab character. */
#define EDIT_TAB_STOP 8
/* Maps a letter key to its Ctrl+key equivalent by masking the upper bits. */
#define CTRL_KEY(k) ((k) & 0x1f)

/* Encodes an Alt+key combination by adding an offset above special keys. */
#define ALT_KEY(k) ((k) + 2000)

/* Special key codes returned by terminal_decode_key(). Values above 127 avoid
 * collisions with normal ASCII characters. */
enum editor_key {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	MOUSE_LEFT_BUTTON_PRESSED,
	MOUSE_MIDDLE_BUTTON_PRESSED,
	MOUSE_RIGHT_BUTTON_PRESSED,
	MOUSE_SCROLL_UP,
	MOUSE_SCROLL_DOWN,
	F11_KEY,
	SHIFT_ARROW_LEFT,
	SHIFT_ARROW_RIGHT,
	SHIFT_ARROW_UP,
	SHIFT_ARROW_DOWN,
	SHIFT_HOME_KEY,
	SHIFT_END_KEY,
	CTRL_ARROW_LEFT,
	CTRL_ARROW_RIGHT,
	SHIFT_CTRL_ARROW_LEFT,
	SHIFT_CTRL_ARROW_RIGHT,
	MOUSE_LEFT_BUTTON_DRAG,
	MOUSE_BUTTON_RELEASED,
	SHIFT_TAB
};

/* Syntax highlight categories assigned to each rendered character.
 * Used to determine which color to apply during rendering. */
enum editor_highlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

/* Bit flags for enabling syntax highlighting features. */
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)
/* Line temperature levels for mmap lazy loading. */
enum line_temperature {
	LINE_COLD = 0,   /* Content in mmap only, no cells allocated */
	LINE_WARM = 1,   /* Cells decoded from mmap, not yet edited */
	LINE_HOT  = 2    /* Cells edited, mmap content stale */
};
/* Initial capacity for a line's cell array. Doubled on each grow. */
#define LINE_INITIAL_CAPACITY 128
/* A single character cell carrying its own metadata inline. */
struct cell {
	/* Unicode codepoint for this character. */
	uint32_t codepoint;
	/* Syntax highlight type (editor_highlight enum). */
	uint16_t syntax;
	/* Word boundary info (reserved, zeroed). */
	uint8_t neighbor;
	/* Rendering flags (reserved, zeroed). */
	uint8_t flags;
	/* Pair matching ID (reserved, zeroed). */
	uint32_t context;
};
/* A single line of text represented as an array of cells. */
struct line {
	/* Dynamic array of cells for this line. */
	struct cell *cells;
	/* Number of cells currently in use. */
	uint32_t cell_count;
	/* Allocated capacity of the cells array. */
	uint32_t cell_capacity;
	/* Zero-based line number in the file. */
	int line_index;
	/* True if this line is inside an unclosed multi-line comment. */
	int open_comment;
	/* Byte offset of this line's content in the mmap region. */
	size_t mmap_offset;
	/* Byte length of this line's content in the mmap (excluding newline). */
	uint32_t mmap_length;
	/* Temperature level for lazy loading (line_temperature enum). */
	int temperature;
	/* When set, syntax highlighting must be recomputed before rendering.
	 * Avoids warming all lines just to highlight them at file open. */
	int syntax_stale;
};

/* Bit flags stored in struct cell.flags for rendering state. */
enum cell_flag {
	CELL_FLAG_SELECTED = (1 << 0)
};

/* Tracks the current text selection region. */
struct selection_state {
	int active;
	int anchor_y;
	int anchor_x;
};

/* Internal clipboard storage for cut/copy/paste. */
struct clipboard {
	char **lines;
	size_t *line_lengths;
	int line_count;
	int is_line_mode;
};

/* Maximum size in bytes for OSC 52 clipboard payloads. */
#define OSC52_MAX_PAYLOAD_BYTES 74994

/* Types of undoable operations. Each maps to a specific edit action
 * that can be reversed. */
enum undo_operation_type {
	UNDO_INSERT_CHAR,
	UNDO_DELETE_CHAR,
	UNDO_INSERT_LINE,
	UNDO_DELETE_LINE,
	UNDO_SPLIT_LINE,
	UNDO_JOIN_LINE,
};

/* A single undoable edit operation. Stores enough state to reverse
 * the operation and replay it for redo. */
struct undo_entry {
	enum undo_operation_type type;
	int line_index;
	int cell_index;
	struct cell saved_cell;
	struct cell *saved_cells;
	uint32_t saved_cell_count;
	int cursor_x_before;
	int cursor_y_before;
};

/* A group of undo entries that are undone/redone as a single unit. */
struct undo_group {
	struct undo_entry *entries;
	int entry_count;
	int entry_capacity;
	struct timeval timestamp;
};

/* Maximum number of undo groups retained. */
#define UNDO_MAX_GROUPS 256
/* Time threshold in microseconds for grouping consecutive edits. */
#define UNDO_GROUP_THRESHOLD_US 500000

/* The undo/redo stack. Groups below current are undoable; groups at
 * or above current are redoable. */
struct undo_stack {
	struct undo_group *groups;
	int group_count;
	int current;
	int group_capacity;
};

/* Editor mode determines which input handler processes keypresses. */
enum editor_mode {
	MODE_NORMAL = 0,
	MODE_PROMPT,
	MODE_CONFIRM
};

/* ASCII escape character used to begin terminal escape sequences. */
#define ESC_KEY '\x1b'

/* ANSI escape sequence to clear the entire screen. */
#define CLEAR_SCREEN "\x1b[2J"
/* ANSI escape sequence to move the cursor to the top-left corner. */
#define CURSOR_HOME "\x1b[H"
/* Enables SGR mouse reporting and basic mouse tracking in the terminal. */
#define ENABLE_MOUSE_REPORTING "\x1b[?1006h\x1b[?1002h"
/* Disables SGR mouse reporting and button-event tracking. */
#define DISABLE_MOUSE_REPORTING "\x1b[?1006l\x1b[?1002l"
/* ANSI escape sequence to hide the terminal cursor. */
#define HIDE_CURSOR "\x1b[?25l"
/* ANSI escape sequence to show the terminal cursor. */
#define SHOW_CURSOR "\x1b[?25h"
/* ANSI escape sequence to query the current cursor position. */
#define CURSOR_POSITION "\x1b[6n"
/* ANSI escape format string to move the cursor to row and column. */
#define CURSOR_MOVE "\x1b[%d;%dH"
/* ANSI escape sequence to clear from the cursor to the end of the line. */
#define CLEAR_LINE "\x1b[K"

/* ANSI escape sequence to enable inverse/reverse video. */
#define INVERT_COLOR "\x1b[7m"
/* ANSI escape sequence to reset all text attributes. */
#define RESET_ALL_ATTRIBUTES "\x1b[m"
/* Moves the cursor to the bottom-right corner for window size detection. */
#define CURSOR_BOTTOM_RIGHT "\x1b[999C\x1b[999B"
/* Format string for setting 24-bit foreground color via ANSI escape. */
#define COLOR_FG_FORMAT "\x1b[38;2;%d;%d;%dm"
/* Format string for setting 24-bit background color via ANSI escape. */
#define COLOR_BG_FORMAT "\x1b[48;2;%d;%d;%dm"

/* Buffer sizes for various string formatting operations. */
#define STATUS_MESSAGE_SIZE 80
#define STATUS_BUFFER_SIZE 120
#define CURSOR_BUFFER_SIZE 32
#define RESPONSE_BUFFER_SIZE 32
#define MOUSE_SEQUENCE_SIZE 32
#define COLOR_SEQUENCE_SIZE 20
#define LINE_NUMBER_BUFFER_SIZE 16
#define ESCAPE_SEQUENCE_SIZE 6
#define PROMPT_INITIAL_SIZE 128
#define INPUT_BUFFER_SIZE 256

/* Default Unix file permission mode for newly created files (rw-r--r--). */
#define FILE_PERMISSION_DEFAULT 0644

/* Seconds before the status bar message auto-clears. */
#define STATUS_MESSAGE_TIMEOUT_SECONDS 5
/* Microseconds threshold for fast scroll acceleration. */
#define SCROLL_ACCELERATION_FAST_US 50000
/* Microseconds threshold for scroll speed deceleration. */
#define SCROLL_DECELERATION_SLOW_US 200000

/* Maximum scroll speed multiplier for accelerated scrolling. */
#define SCROLL_SPEED_MAX 10

/* Number of lines to keep visible above and below the cursor. */
#define SCROLL_MARGIN 5

/* Highest ASCII value for Ctrl+letter key combinations (A through Z). */
#define CONTROL_CHAR_MAX 26
/* Upper bound of the standard ASCII character range. */
#define ASCII_MAX 128

/* Number of microseconds in one second, for timeval arithmetic. */
#define MICROSECONDS_PER_SECOND 1000000

/* Carriage return + line feed sequence for terminal line endings. */
#define CRLF "\r\n"

/*** Data ***/

/* Color theme configuration for the editor. Each field holds a hex color
 * code (e.g., "FFFFFF" for white) used for different UI elements. */
struct editor_theme {
	/* Human-readable name displayed when switching themes. */
	const char *name;
	/* Background color for the main editing area. */
	char *background;
	/* Default text foreground color. */
	char *foreground;
	/* Color for line numbers in the gutter. */
	char *line_number;
	/* Background color for the status bar. */
	char *status_bar;
	/* Text color for the status bar. */
	char *status_bar_text;
	/* Color for the message bar at the bottom. */
	char *message_bar;
	/* Background color for highlighted/selected text. */
	char *highlight_background;
	/* Foreground color for highlighted/selected text. */
	char *highlight_foreground;
	/* Color for comments in syntax highlighting. */
	char *comment;
	/* Color for primary keywords (control flow, etc.). */
	char *keyword1;
	/* Color for secondary keywords (types, etc.). */
	char *keyword2;
	/* Color for string literals. */
	char *string;
	/* Color for numeric literals. */
	char *number;
	/* Color for search match highlighting. */
	char *match;
};

/* Syntax highlighting rules for a specific file type. Contains patterns
 * for keywords, comments, and strings used by the highlighting engine. */
struct editor_syntax {
	/* File type name displayed in status bar (e.g., "c", "python"). */
	char *filetype;
	/* NULL-terminated array of file extensions (e.g., ".c", ".h"). */
	char **filematch;
	/* NULL-terminated array of keywords. Type keywords end with '|'. */
	char **keywords;
	/* Pre-computed lengths of each keyword string, populated when syntax
	 * is selected. Avoids calling strlen() on every word boundary. */
	int *keyword_lengths;
	/* Characters that begin a single-line comment (e.g., "//"). */
	char *singleline_comment_start;
	/* Characters that begin a multi-line comment. */
	char *multiline_comment_start;
	/* Characters that end a multi-line comment. */
	char *multiline_comment_end;
	/* Bit flags controlling highlighting behavior (HL_HIGHLIGHT_*). */
	int flags;
	/* Additional string delimiter character (e.g., '`' for Go/JS template literals).
	 * Set to 0 for languages that only use ' and ". */
	char extra_string_delimiter;
};

/* A decoded input event from the terminal. Keyboard events carry only
 * a key code; mouse events also carry screen coordinates. */
struct input_event {
	int key;
	int mouse_x;
	int mouse_y;
};

/* Buffered input from stdin. Filled with a single non-blocking read()
 * and drained byte-by-byte during key decoding. */
struct input_buffer {
	unsigned char data[INPUT_BUFFER_SIZE];
	int read_position;
	int count;
};

/* State for the prompt line (search, save-as, jump-to-line, etc).
 * Active when editor.mode == MODE_PROMPT. */
struct prompt_state {
	char *format;
	char *buffer;
	size_t buffer_length;
	size_t buffer_capacity;
	void (*per_key_callback)(char *, int);
	void (*on_accept)(char *);
	void (*on_cancel)(void);
};

/* Snapshot of buffer state for temporary views (help screen, etc).
 * Allows saving and restoring the full editing context without a
 * multi-buffer architecture. */
struct editor_snapshot {
	struct line *lines;
	int line_count;
	int line_capacity;
	int cursor_x, cursor_y;
	int row_offset, column_offset;
	int dirty;
	char *filename;
	struct editor_syntax *syntax;
	int file_descriptor;
	char *mmap_base;
	size_t mmap_size;
};

/* Global editor state containing cursor position, file content, display
 * settings, and terminal configuration. Single instance used throughout. */
struct editor_state {
	/* Cursor position in character coordinates (0-based). */
	int cursor_x, cursor_y;
	/* Rendered column position accounting for tab expansion. */
	int render_x;
	/* First visible row (vertical scroll position). */
	int row_offset;
	/* First visible column (horizontal scroll position). */
	int column_offset;
	/* Terminal height in rows (excluding status/message bars). */
	int screen_rows;
	/* Terminal width in columns. */
	int screen_columns;
	/* Total number of lines in the file. */
	int line_count;
	/* Allocated capacity of the lines array (number of struct line slots). */
	int line_capacity;
	/* Dynamic array of line structures. */
	struct line *lines;
	/* True if file has unsaved modifications. */
	int dirty;
	/* Current filename, or NULL for new file. */
	char *filename;
	/* Status message displayed at bottom of screen. */
	char status_message[STATUS_MESSAGE_SIZE];
	/* Timestamp when status message was set (for auto-clear). */
	time_t status_message_time;
	/* Current syntax highlighting rules, or NULL. */
	struct editor_syntax *syntax;
	/* Terminal settings to restore on exit. */
	struct termios original_termios;
	/* Timestamp of last scroll event for acceleration. */
	struct timeval last_scroll_time;
	/* Current color theme. */
	struct editor_theme theme;
	/* Current scroll speed (1 to SCROLL_SPEED_MAX). */
	int scroll_speed;
	/* Whether line numbers are displayed in the gutter. */
	int show_line_numbers;
	/* Number of columns reserved for the line number gutter (digits + space). */
	int line_number_width;
	/* Buffered input from stdin. */
	struct input_buffer input;
	/* File descriptor for mmap, or -1 if not using mmap. */
	int file_descriptor;
	/* Base address of the mmap region, or NULL. */
	char *mmap_base;
	/* Size of the mmap region in bytes. */
	size_t mmap_size;
	/* Current input mode (normal editing, prompt, or confirm). */
	enum editor_mode mode;
	/* Prompt state for interactive prompts (search, save-as, etc). */
	struct prompt_state prompt;
	/* When set, editor exits after a successful save completes. */
	int quit_after_save;
	/* Callback for MODE_CONFIRM single-key responses. */
	void (*confirm_callback)(int key);
	/* Saved cursor/viewport for search cancellation. */
	int saved_cursor_x, saved_cursor_y;
	int saved_column_offset, saved_row_offset;
	/* Search state (replaces static locals in find callback). */
	int search_last_match;
	int search_last_match_offset;
	int search_direction;
	int search_saved_highlight_line;
	uint16_t *search_saved_syntax;
	uint32_t search_saved_syntax_count;
	/* When set, search and replace ignore letter case. Toggled
	 * with Alt+C during an active search prompt. */
	int search_case_insensitive;
	/* Desired render column preserved across vertical movement. */
	int preferred_column;
	/* When set, editor_scroll() skips the margin and uses simple
	 * boundary checks. Set by mouse wheel and Page Up/Down to avoid
	 * fighting with their own viewport positioning. Auto-clears. */
	int suppress_scroll_margin;
	/* Current text selection state. */
	struct selection_state selection;
	/* Internal clipboard for cut/copy/paste. */
	struct clipboard clipboard;
	/* Undo/redo history for the current file. */
	struct undo_stack undo;
	/* Search string for find-and-replace, or NULL when inactive. */
	char *replace_query;
	/* Replacement string for find-and-replace, or NULL when inactive. */
	char *replace_with;
	/* Number of replacements performed in the current replace-all pass. */
	int replace_count;
	/* True when displaying a temporary read-only buffer (help text). */
	int viewing_help;
	/* Saved state for returning from a temporary view. */
	struct editor_snapshot snapshot;
	/* Reusable scratch buffer for building byte strings during syntax
	 * highlighting. Grows as needed, never freed during normal operation. */
	char *scratch_buffer;
	/* Allocated size of the scratch buffer in bytes. */
	size_t scratch_capacity;
	/* Reusable scratch array mapping cell indices to byte offsets during
	 * syntax highlighting. Grows as needed, never freed during normal operation. */
	int *scratch_offsets;
	/* Allocated capacity of the scratch offsets array (number of int slots). */
	size_t scratch_offsets_capacity;
	/* Previous frame's cursor and viewport state, used to detect when
	 * only the cursor moved so the full redraw can be skipped. */
	int prev_cursor_x, prev_cursor_y;
	int prev_render_x;
	int prev_row_offset, prev_column_offset;
	/* Previous frame's line count, used to detect structural changes. */
	int prev_line_count;
	/* Previous frame's status message, used to detect message changes. */
	char prev_status_message[STATUS_MESSAGE_SIZE];
	/* Previous frame's file-modified flag, for status bar change detection. */
	int prev_dirty;
	/* When set, forces a full screen redraw on the next refresh
	 * regardless of cursor/viewport state. Cleared after each refresh. */
	int force_full_redraw;
};

/* Global editor state. */
struct editor_state editor;

/* Forward declarations for functions used before their definitions. */
void editor_insert_char(int character);
void editor_insert_newline(void);
void editor_line_delete(int position);
void editor_line_insert(int position, const char *string, size_t length);
void syntax_propagate(int start_line);
void line_ensure_warm(struct line *line);
void line_ensure_capacity(struct line *line, uint32_t needed);
void line_insert_cell(struct line *line, uint32_t position, struct cell c);
void line_delete_cell(struct line *line, uint32_t position);
void line_append_cells(struct line *dest, struct line *src, uint32_t from);
char *line_to_bytes(struct line *line, size_t *out_length);
int line_render_column_to_cell(struct line *line, int render_column);
int line_cell_to_render_column(struct line *line, int cell_index);
int line_render_width(struct line *line);
int cursor_prev_grapheme(struct line *line, int cell_index);
int cursor_next_grapheme(struct line *line, int cell_index);
int cell_display_width(struct cell *c, int current_column);
int syntax_is_separator(int character);
void editor_update_gutter_width(void);
void line_free(struct line *line);
void line_init(struct line *line, int index);
void terminal_die(const char *message);

/*** Filetypes ***/

/* File extensions recognized as C source files for syntax highlighting. */
char *c_highlight_extensions[] = {".c", ".h", ".cpp", NULL};

/* C language keywords for syntax highlighting. Keywords ending with '|'
 * are type keywords and get a different highlight color. */
char *c_highlight_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return",
	"else", "case", "struct", "union", "typedef", "static", "enum",
	"class", "int|", "long|", "double|", "float|", "char|",
	"unsigned|", "signed|", "void|", NULL
};

/* File extensions recognized as Python source files. */
char *python_highlight_extensions[] = {".py", NULL};

/* Python keywords and built-in type names. */
char *python_highlight_keywords[] = {
	"and", "as", "assert", "async", "await", "break", "class",
	"continue", "def", "del", "elif", "else", "except", "finally",
	"for", "from", "global", "if", "import", "in", "is", "lambda",
	"nonlocal", "not", "or", "pass", "raise", "return", "try",
	"while", "with", "yield",
	"True|", "False|", "None|", "int|", "float|", "str|", "list|",
	"dict|", "tuple|", "set|", "bool|", "bytes|", "type|", NULL
};

/* File extensions recognized as JavaScript source files. */
char *javascript_highlight_extensions[] = {".js", ".jsx", ".mjs", NULL};

/* JavaScript keywords and built-in type names. */
char *javascript_highlight_keywords[] = {
	"break", "case", "catch", "class", "const", "continue", "debugger",
	"default", "delete", "do", "else", "export", "extends", "finally",
	"for", "function", "if", "import", "in", "instanceof", "new", "of",
	"return", "super", "switch", "this", "throw", "try", "typeof",
	"var", "void", "while", "with", "yield", "async", "await", "let",
	"static",
	"true|", "false|", "null|", "undefined|", "NaN|", "Infinity|",
	"Array|", "Object|", "String|", "Number|", "Boolean|", "Map|",
	"Set|", "Promise|", "Symbol|", NULL
};

/* File extensions recognized as Go source files. */
char *go_highlight_extensions[] = {".go", NULL};

/* Go keywords and built-in type names. */
char *go_highlight_keywords[] = {
	"break", "case", "chan", "const", "continue", "default", "defer",
	"else", "fallthrough", "for", "func", "go", "goto", "if", "import",
	"interface", "map", "package", "range", "return", "select", "struct",
	"switch", "type", "var",
	"bool|", "byte|", "complex64|", "complex128|", "error|", "float32|",
	"float64|", "int|", "int8|", "int16|", "int32|", "int64|", "rune|",
	"string|", "uint|", "uint8|", "uint16|", "uint32|", "uint64|",
	"uintptr|", "true|", "false|", "nil|", "iota|", NULL
};

/* File extensions recognized as Rust source files. */
char *rust_highlight_extensions[] = {".rs", NULL};

/* Rust keywords and built-in type names. */
char *rust_highlight_keywords[] = {
	"as", "async", "await", "break", "const", "continue", "crate",
	"dyn", "else", "enum", "extern", "fn", "for", "if", "impl", "in",
	"let", "loop", "match", "mod", "move", "mut", "pub", "ref",
	"return", "self", "static", "struct", "super", "trait", "type",
	"unsafe", "use", "where", "while", "yield",
	"bool|", "char|", "f32|", "f64|", "i8|", "i16|", "i32|", "i64|",
	"i128|", "isize|", "str|", "u8|", "u16|", "u32|", "u64|", "u128|",
	"usize|", "String|", "Vec|", "Option|", "Result|", "Box|", "Self|",
	"true|", "false|", NULL
};

/* File extensions recognized as Bash shell scripts. */
char *bash_highlight_extensions[] = {".sh", ".bash", NULL};

/* Bash keywords and common builtins. */
char *bash_highlight_keywords[] = {
	"case", "do", "done", "elif", "else", "esac", "fi", "for",
	"function", "if", "in", "select", "then", "until", "while",
	"break", "continue", "return", "exit",
	"echo|", "printf|", "read|", "export|", "local|", "declare|",
	"unset|", "shift|", "source|", "eval|", "exec|", "trap|", "set|",
	"cd|", "test|", NULL
};

/* File extensions recognized as JSON data files. */
char *json_highlight_extensions[] = {".json", NULL};

/* JSON has no keywords, only type-highlighted literals. */
char *json_highlight_keywords[] = {
	"true|", "false|", "null|", NULL
};

/* File extensions recognized as YAML data files. */
char *yaml_highlight_extensions[] = {".yml", ".yaml", NULL};

/* YAML type-highlighted boolean and null literals. */
char *yaml_highlight_keywords[] = {
	"true|", "false|", "null|", "yes|", "no|", "on|", "off|", NULL
};

/* File extensions recognized as Markdown documents. */
char *markdown_highlight_extensions[] = {".md", ".markdown", NULL};

/* Markdown has no keywords; headers are highlighted via '#' comment syntax. */
char *markdown_highlight_keywords[] = {NULL};

/* Registry of all supported file types and their highlighting rules. */
struct editor_syntax syntax_highlight_database[] = {
	{"c", c_highlight_extensions, c_highlight_keywords, NULL, "//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = 0},
	{"python", python_highlight_extensions, python_highlight_keywords, NULL, "#", "\"\"\"", "\"\"\"",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = 0},
	{"javascript", javascript_highlight_extensions, javascript_highlight_keywords, NULL, "//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = '`'},
	{"go", go_highlight_extensions, go_highlight_keywords, NULL, "//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = '`'},
	{"rust", rust_highlight_extensions, rust_highlight_keywords, NULL, "//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = 0},
	{"bash", bash_highlight_extensions, bash_highlight_keywords, NULL, "#", NULL, NULL,
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = 0},
	{"json", json_highlight_extensions, json_highlight_keywords, NULL, NULL, NULL, NULL,
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = 0},
	{"yaml", yaml_highlight_extensions, yaml_highlight_keywords, NULL, "#", NULL, NULL,
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = 0},
	{"markdown", markdown_highlight_extensions, markdown_highlight_keywords, NULL, "#", NULL, NULL,
		HL_HIGHLIGHT_STRINGS, .extra_string_delimiter = '`'},
};

/* Number of entries in the syntax highlighting database. */
#define SYNTAX_HIGHLIGHT_DATABASE_ENTRIES (sizeof(syntax_highlight_database) / sizeof(syntax_highlight_database[0]))

/*** Prototypes ***/

/* Sets the status bar message using printf-style formatting. */
void editor_set_status_message(const char *format, ...);
/* Redraws the entire screen from the append buffer in a single write. */
void editor_refresh_screen(void);
/* Queries terminal size and returns it via the rows/columns out-parameters. */
int terminal_get_window_size(int *rows, int *columns);
/* Concatenates all editor lines into a single newline-separated string. */
char *editor_rows_to_string(size_t *buffer_length);
/* Recalculates the gutter width based on the current line count. */
void editor_update_gutter_width(void);
/* Recalculates syntax highlighting for a line given previous open_comment state. */
int line_update_syntax(struct line *line, int prev_open_comment);
/* Opens a prompt in the status bar for user input. */
void prompt_open(char *format, void (*per_key_callback)(char *, int),
		 void (*on_accept)(char *), void (*on_cancel)(void));
/* Handles a keypress while in MODE_PROMPT. */
void prompt_handle_key(struct input_event event);
/* Closes the current prompt and returns to MODE_NORMAL. */
void prompt_close(void);
/* Opens a single-key confirmation dialog. */
void confirm_open(const char *message, void (*callback)(int key));
/* Handles a keypress while in MODE_CONFIRM. */
void editor_handle_confirm(struct input_event event);
/* Writes the current file to disk. */
int editor_save_write(void);
/* Starts the save flow (prompts for filename if needed). */
void editor_save_start(void);
/* Starts incremental search. */
void editor_find_start(void);
/* Starts the find-and-replace flow. */
void editor_replace_start(void);
/* Reads all data from stdin when piped, returning a malloc'd buffer.
 * Stores the total bytes read in *out_length. */
char *editor_read_stdin_pipe(size_t *out_length);

/*** Editor Theme ***/

/* Array of all available editor themes. */
struct editor_theme editor_themes[] = {

	/* Cyberpunk - Dark neon */
	{.name = "Cyberpunk", .background = "0A0A0C", .foreground = "D0D8E0",
	 .line_number = "404048", .status_bar = "101014", .status_bar_text = "00FFFF",
	 .message_bar = "FF00FF", .highlight_background = "151518",
	 .highlight_foreground = "FFFFFF", .comment = "505060", .keyword1 = "FF00FF",
	 .keyword2 = "00FFFF", .string = "00FF80", .number = "FFFF00", .match = "FF0080"},

	/* Nightwatch - Monochrome dark */
	{.name = "Nightwatch", .background = "0A0A0A", .foreground = "D0D0D0",
	 .line_number = "505050", .status_bar = "1A1A1A", .status_bar_text = "A0A0A0",
	 .message_bar = "808080", .highlight_background = "1A1A1A",
	 .highlight_foreground = "E0E0E0", .comment = "606060", .keyword1 = "FFFFFF",
	 .keyword2 = "B0B0B0", .string = "909090", .number = "C0C0C0", .match = "404040"},

	/* Daywatch - Monochrome light */
	{.name = "Daywatch", .background = "F5F5F5", .foreground = "303030",
	 .line_number = "A0A0A0", .status_bar = "E5E5E5", .status_bar_text = "505050",
	 .message_bar = "707070", .highlight_background = "E0E0E0",
	 .highlight_foreground = "202020", .comment = "808080", .keyword1 = "000000",
	 .keyword2 = "404040", .string = "505050", .number = "303030", .match = "C0C0C0"},

	/* Tokyo Night */
	{.name = "Tokyo Night", .background = "1A1B26", .foreground = "C0CAF5",
	 .line_number = "3B4261", .status_bar = "16161E", .status_bar_text = "7AA2F7",
	 .message_bar = "BB9AF7", .highlight_background = "292E42",
	 .highlight_foreground = "C0CAF5", .comment = "565F89", .keyword1 = "BB9AF7",
	 .keyword2 = "7DCFFF", .string = "9ECE6A", .number = "FF9E64", .match = "E0AF68"},

	/* Akira - Neo-Tokyo red/cyan */
	{.name = "Akira", .background = "0C0608", .foreground = "F0E4E8",
	 .line_number = "584048", .status_bar = "1C1018", .status_bar_text = "E0CCD4",
	 .message_bar = "D4C0C8", .highlight_background = "1C1014",
	 .highlight_foreground = "F0E4E8", .comment = "685060", .keyword1 = "FF3050",
	 .keyword2 = "40D0E8", .string = "F88080", .number = "E06878", .match = "103840"},
	/* Tokyo Night Cyberpunk - Neon accents on Tokyo Night's deep indigo base */
	{.name = "Tokyo Cyberpunk", .background = "13141F", .foreground = "D5DEFF",
	 .line_number = "2E3456", .status_bar = "0E0F18", .status_bar_text = "00FFFF",
	 .message_bar = "FF44CC", .highlight_background = "1E2036",
	 .highlight_foreground = "FFFFFF", .comment = "4A5380", .keyword1 = "FF44CC",
	 .keyword2 = "00FFFF", .string = "7AFF8E", .number = "FFB86C", .match = "E0AF68"},
};

/* Index of the currently active theme in editor_themes[]. */
int current_theme_index = 0;

/* Applies the theme at the given index in editor_themes[]. */
void editor_set_theme(int index)
{
	editor.theme = editor_themes[index];
}

/* Cycles to the next theme in editor_themes[] and displays its name. */
void editor_switch_theme(void)
{
	editor.force_full_redraw = 1;
	current_theme_index = (current_theme_index + 1) %
		(int)(sizeof(editor_themes) / sizeof(editor_themes[0]));
	editor_set_theme(current_theme_index);
	editor_set_status_message("Theme: %s", editor.theme.name);
}
/* Toggles line number gutter visibility and updates the gutter width. */
void editor_toggle_line_numbers(void)
{
	editor.force_full_redraw = 1;
	editor.show_line_numbers = !editor.show_line_numbers;
	editor_update_gutter_width();
	editor_set_status_message(
			"Line numbers: %s", editor.show_line_numbers ? "on" : "off");
}

/*** Terminal ***/

/* Forward declaration so early terminal functions can call terminal_die(). */
void terminal_die(const char *message);

/* Attempts to save the current buffer to an emergency recovery file before
 * the editor exits due to a fatal error. Only saves if there are unsaved
 * changes. Returns 0 on success, -1 on failure. */
int editor_emergency_save(void)
{
	if (!editor.dirty || editor.line_count == 0)
		return 0;

	char recovery_filename[256];
	snprintf(recovery_filename, sizeof(recovery_filename),
		 ".edit_recovery_%d", getpid());

	int fd = open(recovery_filename, O_WRONLY | O_CREAT | O_TRUNC,
		      FILE_PERMISSION_DEFAULT);
	if (fd == -1)
		return -1;

	size_t file_length;
	char *file_content = editor_rows_to_string(&file_length);
	if (!file_content) {
		close(fd);
		return -1;
	}

	size_t bytes_written = 0;
	while (bytes_written < file_length) {
		ssize_t result = write(fd, file_content + bytes_written,
				       file_length - bytes_written);
		if (result == -1) {
			if (errno == EINTR)
				continue;
			free(file_content);
			close(fd);
			return -1;
		}
		bytes_written += (size_t)result;
	}

	free(file_content);
	close(fd);

	/* Inform the user about the recovery file */
	char message[512];
	snprintf(message, sizeof(message),
		 "\r\nUnsaved changes saved to: %s\r\n", recovery_filename);
	write(STDOUT_FILENO, message, strlen(message));

	return 0;
}

/* Guard flag preventing terminal_die() from recursing into itself.
 * Uses volatile sig_atomic_t because terminal_die() may be called
 * from signal handlers. */
static volatile sig_atomic_t terminal_die_in_progress = 0;

/* Prints an error message and exits. Clears the screen first to leave the
 * terminal in a clean state before displaying the error via perror().
 * Attempts to save unsaved changes to a recovery file before exiting. */
void terminal_die(const char *message)
{
	/* Reentrancy guard: if we're already dying, just exit immediately.
	 * This prevents infinite recursion when malloc fails inside
	 * editor_emergency_save(). */
	if (terminal_die_in_progress)
		_exit(1);
	terminal_die_in_progress = 1;

	write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
	write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));

	/* Try to save unsaved work before dying */
	editor_emergency_save();

	perror(message);
	_exit(1);
}

/* Restores the terminal to its original settings saved before entering
 * raw mode. Called automatically at exit via atexit(). */
void terminal_disable_raw_mode(void)
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.original_termios) == -1) {
		terminal_die("tcsetattr");
	}
}

/* Puts the terminal into raw mode by disabling echo, canonical input,
 * and signal processing. Saves the original settings for later restoration.
 * Registers terminal_disable_raw_mode() to run at exit. */
void terminal_enable_raw_mode(void)
{
	if (tcgetattr(STDIN_FILENO, &editor.original_termios) == -1) {
		terminal_die("tcgetattr");
	}
	atexit(terminal_disable_raw_mode);

	struct termios raw = editor.original_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		terminal_die("tcsetattr");
	}
}

/* Sends escape sequences to disable SGR mouse tracking in the terminal. */
void terminal_disable_mouse_reporting(void)
{
	write(STDOUT_FILENO, DISABLE_MOUSE_REPORTING,
				strlen(DISABLE_MOUSE_REPORTING));
}

/* Sends escape sequences to enable SGR mouse tracking in the terminal.
 * This allows the editor to receive click, scroll, and movement events.
 * Returns 0 on success, -1 on failure (non-fatal). */
int terminal_enable_mouse_reporting(void)
{
	ssize_t result = write(STDOUT_FILENO, ENABLE_MOUSE_REPORTING,
			       strlen(ENABLE_MOUSE_REPORTING));
	return (result == (ssize_t)strlen(ENABLE_MOUSE_REPORTING)) ? 0 : -1;
}
/* Flag set by the SIGWINCH handler to signal the main loop that a resize
 * occurred. Using volatile sig_atomic_t ensures async-signal-safety. */
volatile sig_atomic_t resize_pending = 0;

/* Sets the resize flag on SIGWINCH. Only uses async-signal-safe operations. */
void terminal_handle_resize(int signal_number)
{
	(void)signal_number;
	resize_pending = 1;
}

/* Handles SIGTERM and SIGHUP by attempting an emergency save and exiting
 * cleanly. Restores the terminal before exit. Only uses async-signal-safe
 * operations where possible. */
void terminal_handle_terminate(int signal_number)
{
	(void)signal_number;
	write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
	write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));
	write(STDOUT_FILENO, DISABLE_MOUSE_REPORTING,
	      strlen(DISABLE_MOUSE_REPORTING));
	editor_emergency_save();
	_exit(1);
}

/* Processes a pending terminal resize by re-querying the terminal size,
 * clamping the cursor to stay within bounds, and redrawing the screen.
 * Called from the main loop when resize_pending is set. */
void terminal_process_resize(void)
{
	editor.force_full_redraw = 1;
	resize_pending = 0;
	int new_rows, new_columns;
	if (terminal_get_window_size(&new_rows, &new_columns) == -1)
		return;
	editor.screen_rows = new_rows - 2;
	editor.screen_columns = new_columns;
	if (editor.cursor_y >= editor.line_count)
		editor.cursor_y = editor.line_count > 0 ? editor.line_count - 1 : 0;
}

/* Returns the number of bytes currently available in the input buffer. */
int input_buffer_available(void)
{
	return editor.input.count;
}

/* Consumes one byte from the input buffer. Returns 1 on success, 0 if empty.
 * Resets read_position when the buffer is fully drained. */
int input_buffer_read_byte(unsigned char *out)
{
	if (editor.input.count == 0)
		return 0;
	*out = editor.input.data[editor.input.read_position];
	editor.input.read_position++;
	editor.input.count--;
	if (editor.input.count == 0)
		editor.input.read_position = 0;
	return 1;
}

/* Fills the input buffer with a single non-blocking read() from stdin.
 * Compacts the buffer first if the read position has advanced. Returns
 * the number of bytes read, or 0 if nothing was available. */
int input_buffer_fill(void)
{
	/* Compact: move unread data to the front */
	if (editor.input.read_position > 0 && editor.input.count > 0) {
		memmove(editor.input.data,
			editor.input.data + editor.input.read_position,
			editor.input.count);
		editor.input.read_position = 0;
	} else if (editor.input.count == 0) {
		editor.input.read_position = 0;
	}
	int space = INPUT_BUFFER_SIZE - editor.input.read_position - editor.input.count;
	if (space <= 0)
		return 0;
	int bytes_read = read(STDIN_FILENO,
			      editor.input.data + editor.input.read_position + editor.input.count,
			      space);
	if (bytes_read > 0) {
		editor.input.count += bytes_read;
	}
	return bytes_read > 0 ? bytes_read : 0;
}

/* Sets VMIN=0, VTIME=0 for fully non-blocking reads from stdin.
 * Called after editor_init() and editor_open() so the startup
 * terminal_get_cursor_position() fallback still works with VTIME=1. */
void terminal_set_nonblocking(void)
{
	struct termios raw;
	if (tcgetattr(STDIN_FILENO, &raw) == -1)
		terminal_die("tcgetattr");
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		terminal_die("tcsetattr");
}

/* Decodes a single keypress from the input buffer. Returns the decoded
 * event by value. Key is -1 if the buffer is empty, ESC_KEY if an
 * escape sequence is incomplete, UTF8_REPLACEMENT_CHAR for broken UTF-8.
 * Mouse events carry screen coordinates in mouse_x/mouse_y. */
struct input_event terminal_decode_key(void)
{
	unsigned char character;
	if (!input_buffer_read_byte(&character))
		return (struct input_event){.key = -1};

	if (character == ESC_KEY) {
		unsigned char sequence[ESCAPE_SEQUENCE_SIZE];

		if (!input_buffer_read_byte(&sequence[0]))
			return (struct input_event){.key = ESC_KEY};

		if (sequence[0] == '[') {
			if (!input_buffer_read_byte(&sequence[1]))
				return (struct input_event){.key = ESC_KEY};

			if (sequence[1] >= '0' && sequence[1] <= '9') {
				if (!input_buffer_read_byte(&sequence[2]))
					return (struct input_event){.key = ESC_KEY};
				if (sequence[2] == '~') {
					switch (sequence[1]) {
					case '1':
						return (struct input_event){.key = HOME_KEY};
					case '3':
						return (struct input_event){.key = DEL_KEY};
					case '4':
						return (struct input_event){.key = END_KEY};
					case '5':
						return (struct input_event){.key = PAGE_UP};
					case '6':
						return (struct input_event){.key = PAGE_DOWN};
					case '7':
						return (struct input_event){.key = HOME_KEY};
					case '8':
						return (struct input_event){.key = END_KEY};
					}
				} else if (sequence[2] == ';') {
					/* CSI N;modifierX — Shift/Ctrl modified keys.
					 * N is in sequence[1], modifier follows ';',
					 * final byte is letter (arrows/home/end) or
					 * '~' (rxvt-style function keys). */
					unsigned char modifier_byte, final_byte;
					if (!input_buffer_read_byte(&modifier_byte))
						return (struct input_event){.key = ESC_KEY};
					if (!input_buffer_read_byte(&final_byte))
						return (struct input_event){.key = ESC_KEY};
					int modifier = modifier_byte - '0';
					/* Handle ~-terminated sequences: \x1b[N;M~
					 * Map N to the base key, then apply modifier. */
					if (final_byte == '~' && modifier == 2) {
						switch (sequence[1]) {
						case '1': case '7':
							return (struct input_event){.key = SHIFT_HOME_KEY};
						case '4': case '8':
							return (struct input_event){.key = SHIFT_END_KEY};
						}
					}
					/* Handle letter-terminated sequences: \x1b[1;MX */
					if (modifier == 2) {
						switch (final_byte) {
						case 'A': return (struct input_event){.key = SHIFT_ARROW_UP};
						case 'B': return (struct input_event){.key = SHIFT_ARROW_DOWN};
						case 'C': return (struct input_event){.key = SHIFT_ARROW_RIGHT};
						case 'D': return (struct input_event){.key = SHIFT_ARROW_LEFT};
						case 'H': return (struct input_event){.key = SHIFT_HOME_KEY};
						case 'F': return (struct input_event){.key = SHIFT_END_KEY};
						}
					} else if (modifier == 5) {
						switch (final_byte) {
						case 'C': return (struct input_event){.key = CTRL_ARROW_RIGHT};
						case 'D': return (struct input_event){.key = CTRL_ARROW_LEFT};
						}
					} else if (modifier == 6) {
						switch (final_byte) {
						case 'C': return (struct input_event){.key = SHIFT_CTRL_ARROW_RIGHT};
						case 'D': return (struct input_event){.key = SHIFT_CTRL_ARROW_LEFT};
						}
					}
				} else if (sequence[2] >= '0' && sequence[2] <= '9') {
					unsigned char terminator;
					if (!input_buffer_read_byte(&terminator))
						return (struct input_event){.key = ESC_KEY};
					if (terminator == '~') {
						int code = (sequence[1] - '0') * 10
							+ (sequence[2] - '0');
						switch (code) {
						case 23:
							return (struct input_event){.key = F11_KEY};
						}
					}
				}
			} else if (sequence[1] == '<') {

				char mouse_sequence[MOUSE_SEQUENCE_SIZE];
				int mouse_bytes = 0;

				/* Read mouse sequence bytes from the input buffer */
				while (mouse_bytes < (int)sizeof(mouse_sequence) - 1) {
					unsigned char mb;
					if (!input_buffer_read_byte(&mb))
						break;
					mouse_sequence[mouse_bytes++] = (char)mb;
					if (mb == 'M' || mb == 'm')
						break;
				}
				mouse_sequence[mouse_bytes] = '\0';

				int button, column, row;
				char pressed;
				if (sscanf(mouse_sequence, "%d;%d;%d%c", &button, &column,
									 &row, &pressed) == 4) {
					column = column - editor.line_number_width - 1;
					if (column < 0)
						column = 0;
					row = row - 1;

					switch (button) {
					case 0:
						if (pressed == 'M') {
							return (struct input_event){
								.key = MOUSE_LEFT_BUTTON_PRESSED,
								.mouse_x = column,
								.mouse_y = row
							};
						} else if (pressed == 'm') {
							return (struct input_event){
								.key = MOUSE_BUTTON_RELEASED,
								.mouse_x = column,
								.mouse_y = row
							};
						}
						break;
					case 1:
						break;
					case 2:
						break;
					case 32:
						if (pressed == 'M') {
							return (struct input_event){
								.key = MOUSE_LEFT_BUTTON_DRAG,
								.mouse_x = column,
								.mouse_y = row
							};
						}
						break;
					case 35:
						break;
					case 64:
						return (struct input_event){.key = MOUSE_SCROLL_UP};
					case 65:
						return (struct input_event){.key = MOUSE_SCROLL_DOWN};
					}
				}

				return (struct input_event){.key = ESC_KEY};

			} else {
				switch (sequence[1]) {
				case 'A':
					return (struct input_event){.key = ARROW_UP};
				case 'B':
					return (struct input_event){.key = ARROW_DOWN};
				case 'C':
					return (struct input_event){.key = ARROW_RIGHT};
				case 'D':
					return (struct input_event){.key = ARROW_LEFT};
				case 'H':
					return (struct input_event){.key = HOME_KEY};
				case 'F':
					return (struct input_event){.key = END_KEY};
				case 'Z':
					return (struct input_event){.key = SHIFT_TAB};
				}
			}
		} else if (sequence[0] == 'O') {
			if (!input_buffer_read_byte(&sequence[1]))
				return (struct input_event){.key = ESC_KEY};

			switch (sequence[1]) {
			case 'H':
				return (struct input_event){.key = HOME_KEY};
			case 'F':
				return (struct input_event){.key = END_KEY};
			}
		} else {
			return (struct input_event){.key = ALT_KEY(sequence[0])};
		}
		return (struct input_event){.key = ESC_KEY};
	} else if (character >= 0x80) {
		/* Multi-byte UTF-8 sequence: determine expected length from
		 * the leading byte, read continuation bytes, then decode. */
		unsigned char lead = character;
		int expected;
		if ((lead & 0xE0) == 0xC0) expected = 2;
		else if ((lead & 0xF0) == 0xE0) expected = 3;
		else if ((lead & 0xF8) == 0xF0) expected = 4;
		else return (struct input_event){.key = UTF8_REPLACEMENT_CHAR};

		char utf8_buf[4];
		utf8_buf[0] = (char)character;
		for (int i = 1; i < expected; i++) {
			unsigned char cb;
			if (!input_buffer_read_byte(&cb))
				return (struct input_event){.key = UTF8_REPLACEMENT_CHAR};
			utf8_buf[i] = (char)cb;
		}
		uint32_t codepoint;
		int consumed = utf8_decode(utf8_buf, expected, &codepoint);
		if (consumed <= 0)
			return (struct input_event){.key = UTF8_REPLACEMENT_CHAR};
		return (struct input_event){.key = (int)codepoint};
	} else {
		return (struct input_event){.key = (int)character};
	}
}

/* Queries the terminal for the current cursor position by sending a
 * device status report escape sequence. Parses the response and stores
 * the row and column values. Returns 0 on success, -1 on failure. */
int terminal_get_cursor_position(int *rows, int *columns)
{
	char response[RESPONSE_BUFFER_SIZE];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, CURSOR_POSITION, strlen(CURSOR_POSITION)) != (ssize_t)strlen(CURSOR_POSITION))
		return -1;

	while (i < sizeof(response) - 1) {
		if (read(STDIN_FILENO, &response[i], 1) != 1)
			break;
		if (response[i] == 'R')
			break;
		i++;
	}
	response[i] = '\0';

	if (response[0] != ESC_KEY || response[1] != '[')
		return -1;
	if (sscanf(&response[2], "%d;%d", rows, columns) != 2)
		return -1;

	return 0;
}

/* Determines the terminal dimensions. Tries ioctl first for speed; falls
 * back to moving the cursor to the bottom-right corner and querying its
 * position. If all methods fail, uses sensible defaults (80x24).
 * Returns 0 on success, -1 if defaults were used. */
int terminal_get_window_size(int *rows, int *columns)
{
	struct winsize window;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == -1 || window.ws_col == 0) {
		if (write(STDOUT_FILENO, CURSOR_BOTTOM_RIGHT, strlen(CURSOR_BOTTOM_RIGHT)) != (ssize_t)strlen(CURSOR_BOTTOM_RIGHT))
			goto use_defaults;
		if (terminal_get_cursor_position(rows, columns) == -1)
			goto use_defaults;
		return 0;
	} else {
		*columns = window.ws_col;
		*rows = window.ws_row;
		return 0;
	}

use_defaults:
	/* Use standard 80x24 terminal size as fallback */
	*columns = 80;
	*rows = 24;
	return -1;
}

/*** Syntax highlighting ***/

/* Returns true if the character is a separator for syntax highlighting
 * purposes: whitespace, null byte, or one of the common punctuation
 * characters that separate tokens in C code. */
int syntax_is_separator(int character)
{
	if (character == '\0')
		return 1;
	if (character < 0 || character > 127)
		return 0;
	return isspace(character) || strchr(",.()+-/*=~%<>[];", character) != NULL;
}

/* Maps a highlight type enum value to its hex color string from the current
 * theme. Returns the foreground color for unrecognized highlight types. */
char *syntax_to_color(int highlight_type)
{
	switch (highlight_type) {
	case HL_COMMENT:
	case HL_MLCOMMENT:
		return editor.theme.comment;
	case HL_KEYWORD1:
		return editor.theme.keyword1;
	case HL_KEYWORD2:
		return editor.theme.keyword2;
	case HL_STRING:
		return editor.theme.string;
	case HL_NUMBER:
		return editor.theme.number;
	case HL_MATCH:
		return editor.theme.match;
	default:
		return editor.theme.foreground;
	}
}

/* Selects the syntax highlighting rules for the current file based on its
 * filename or extension. Searches the syntax_highlight_database for a match
 * and updates all rows if a match is found. Sets editor.syntax to NULL if
 * no matching file type is found. */
void syntax_select_highlight(void)
{
	editor.syntax = NULL;
	if (editor.filename == NULL)
		return;
	char *extension = strrchr(editor.filename, '.');
	for (unsigned int j = 0; j < SYNTAX_HIGHLIGHT_DATABASE_ENTRIES; j++) {
		struct editor_syntax *syntax = &syntax_highlight_database[j];
		unsigned int i = 0;
		while (syntax->filematch[i]) {
			int is_extension = (syntax->filematch[i][0] == '.');
			if ((is_extension && extension && !strcmp(extension, syntax->filematch[i])) ||
					(!is_extension && strstr(editor.filename, syntax->filematch[i]))) {
				editor.syntax = syntax;

				/* Pre-compute keyword lengths so line_update_syntax()
				 * can skip strlen() on every word boundary. */
				free(syntax->keyword_lengths);
				int keyword_count = 0;
				while (syntax->keywords[keyword_count])
					keyword_count++;
				syntax->keyword_lengths = keyword_count
					? malloc(sizeof(int) * keyword_count) : NULL;
				if (keyword_count && syntax->keyword_lengths == NULL)
					terminal_die("malloc");
				for (int k = 0; k < keyword_count; k++)
					syntax->keyword_lengths[k] = (int)strlen(syntax->keywords[k]);

				/* Mark all lines as needing syntax recomputation.
				 * Actual highlighting is deferred to line_ensure_warm()
				 * so only visible lines pay the cost. */
				for (int file_row = 0; file_row < editor.line_count; file_row++)
					editor.lines[file_row].syntax_stale = 1;

				return;
			}
			i++;
		}
	}
}

/*** Line operations ***/
/* Initializes a line with an empty cell array at LINE_INITIAL_CAPACITY. */
void line_init(struct line *line, int index)
{
	line->cells = malloc(sizeof(struct cell) * LINE_INITIAL_CAPACITY);
	if (line->cells == NULL)
		terminal_die("malloc");
	line->cell_count = 0;
	line->cell_capacity = LINE_INITIAL_CAPACITY;
	line->line_index = index;
	line->open_comment = 0;
	line->mmap_offset = 0;
	line->mmap_length = 0;
	line->temperature = LINE_HOT;
	line->syntax_stale = 1;
}
/* Frees the cell array and zeroes all fields. */
void line_free(struct line *line)
{
	if (line->temperature != LINE_COLD)
		free(line->cells);
	line->cells = NULL;
	line->cell_count = 0;
	line->cell_capacity = 0;
	line->temperature = LINE_COLD;
	line->syntax_stale = 1;
}
void line_ensure_warm(struct line *line);
/* Ensures the cell array has room for at least 'needed' cells.
 * Grows with a doubling strategy when more space is required. */
void line_ensure_capacity(struct line *line, uint32_t needed)
{
	if (line->temperature == LINE_COLD)
		line_ensure_warm(line);
	if (needed <= line->cell_capacity)
		return;
	uint32_t new_capacity = line->cell_capacity;
	while (new_capacity < needed)
		new_capacity *= 2;
	line->cells = realloc(line->cells, sizeof(struct cell) * new_capacity);
	if (line->cells == NULL)
		terminal_die("realloc");
	line->cell_capacity = new_capacity;
}
/* Populates a line's cells from a UTF-8 byte string. Each decoded codepoint
 * becomes one cell. Invalid sequences produce U+FFFD replacement characters. */
void line_populate_from_bytes(struct line *line, const char *bytes, size_t length)
{
	line_ensure_capacity(line, (uint32_t)length);
	uint32_t count = 0;
	size_t pos = 0;
	while (pos < length) {
		uint32_t cp;
		int consumed = utf8_decode(&bytes[pos], (int)(length - pos), &cp);
		if (consumed <= 0) {
			cp = UTF8_REPLACEMENT_CHAR;
			consumed = 1;
		}
		line_ensure_capacity(line, count + 1);
		line->cells[count] = (struct cell){
			.codepoint = cp,
			.syntax = HL_NORMAL,
			.neighbor = 0,
			.flags = 0,
			.context = 0,
		};
		count++;
		pos += consumed;
	}
	line->cell_count = count;
}

/* Warms a COLD line by decoding its mmap bytes into cells. Also applies
 * deferred syntax highlighting if the line is marked stale. For correct
 * multi-line comment state, ensures the previous line's open_comment is
 * available by warming it first if needed (bounded by comment regions). */
void line_ensure_warm(struct line *line)
{
	if (line->temperature == LINE_COLD) {
		line->cells = malloc(sizeof(struct cell) * LINE_INITIAL_CAPACITY);
		if (line->cells == NULL)
			terminal_die("malloc");
		line->cell_capacity = LINE_INITIAL_CAPACITY;
		line->cell_count = 0;
		line->temperature = LINE_WARM;
		line_populate_from_bytes(line, editor.mmap_base + line->mmap_offset,
					line->mmap_length);
	}

	/* Apply deferred syntax highlighting on first access. Resolves the
	 * previous line's open_comment state so multi-line comments render
	 * correctly even when lines are warmed out of order. Clear the flag
	 * before calling line_update_syntax() to prevent re-entry: that
	 * function calls line_ensure_warm() on this same line. */
	if (line->syntax_stale && editor.syntax) {
		line->syntax_stale = 0;
		int index = line->line_index;
		int prev_open = 0;
		if (index > 0) {
			struct line *prev = &editor.lines[index - 1];
			/* Warming the previous line recursively resolves its
			 * own syntax and open_comment state. In practice the
			 * recursion is bounded by multi-line comment length. */
			if (prev->syntax_stale)
				line_ensure_warm(prev);
			prev_open = prev->open_comment;
		}
		line_update_syntax(line, prev_open);
	}
}

/* Converts a line's cells to a UTF-8 byte string. Caller frees. */
char *line_to_bytes(struct line *line, size_t *out_length)
{
	line_ensure_warm(line);
	/* Worst case: each codepoint is UTF8_MAX_BYTES */
	size_t max_size = (size_t)line->cell_count * UTF8_MAX_BYTES + 1;
	char *bytes = malloc(max_size);
	if (bytes == NULL)
		terminal_die("malloc");
	size_t pos = 0;
	for (uint32_t i = 0; i < line->cell_count; i++) {
		int written = utf8_encode(line->cells[i].codepoint, &bytes[pos]);
		pos += written;
	}
	bytes[pos] = '\0';
	*out_length = pos;
	return bytes;
}
/* Inserts a cell at the given position, shifting cells to the right. */
void line_insert_cell(struct line *line, uint32_t pos, struct cell c)
{
	line_ensure_warm(line);
	line->temperature = LINE_HOT;
	if (pos > line->cell_count)
		pos = line->cell_count;
	line_ensure_capacity(line, line->cell_count + 1);
	memmove(&line->cells[pos + 1], &line->cells[pos],
			sizeof(struct cell) * (line->cell_count - pos));
	line->cells[pos] = c;
	line->cell_count++;
}
/* Deletes the cell at the given position, shifting cells to the left. */
void line_delete_cell(struct line *line, uint32_t pos)
{
	line_ensure_warm(line);
	line->temperature = LINE_HOT;
	if (pos >= line->cell_count)
		return;
	memmove(&line->cells[pos], &line->cells[pos + 1],
			sizeof(struct cell) * (line->cell_count - pos - 1));
	line->cell_count--;
}
/* Appends cells from src[from..] to the end of dest. Used for line joining. */
void line_append_cells(struct line *dest, struct line *src, uint32_t from)
{
	line_ensure_warm(dest);
	line_ensure_warm(src);
	dest->temperature = LINE_HOT;
	if (from >= src->cell_count)
		return;
	uint32_t count = src->cell_count - from;
	line_ensure_capacity(dest, dest->cell_count + count);
	memcpy(&dest->cells[dest->cell_count], &src->cells[from],
		   sizeof(struct cell) * count);
	dest->cell_count += count;
}
/* Returns the display width of a single cell at the given column position.
 * Tabs expand to the next tab stop; wide/CJK characters take 2 columns;
 * control and zero-width characters take 1 column (rendered as symbols). */
int cell_display_width(struct cell *c, int current_column)
{
	if (c->codepoint == '\t')
		return EDIT_TAB_STOP - (current_column % EDIT_TAB_STOP);
	int w = utf8_cpwidth(c->codepoint);
	return (w < 1) ? 1 : w;
}
/* Maximum number of cells in a grapheme cluster (flag emoji = 2 regional
 * indicators, ZWJ family = 7+ codepoints). 32 provides generous headroom. */
#define GRAPHEME_MAX_CELLS 32
/* Returns the cell index of the next grapheme cluster boundary starting from
 * cell_index in the given line. Encodes a window of cells into a temporary
 * UTF-8 buffer and uses utf8_next_grapheme() to find the boundary, then
 * counts how many codepoints that spans. */
int cursor_next_grapheme(struct line *line, int cell_index)
{
	line_ensure_warm(line);
	if (cell_index >= (int)line->cell_count)
		return (int)line->cell_count;
	/* Encode cells from cell_index into a temporary UTF-8 buffer */
	int remaining = (int)line->cell_count - cell_index;
	if (remaining > GRAPHEME_MAX_CELLS)
		remaining = GRAPHEME_MAX_CELLS;
	char buf[GRAPHEME_MAX_CELLS * UTF8_MAX_BYTES];
	int byte_offsets[GRAPHEME_MAX_CELLS + 1];
	int byte_len = 0;
	for (int i = 0; i < remaining; i++) {
		byte_offsets[i] = byte_len;
		byte_len += utf8_encode(line->cells[cell_index + i].codepoint,
								buf + byte_len);
	}
	byte_offsets[remaining] = byte_len;
	/* Find the next grapheme boundary in byte offsets */
	int next_byte = utf8_next_grapheme(buf, byte_len, 0);
	/* Convert byte offset back to cell count */
	for (int i = 1; i <= remaining; i++) {
		if (byte_offsets[i] >= next_byte)
			return cell_index + i;
	}
	return cell_index + remaining;
}
/* Returns the cell index of the previous grapheme cluster boundary before
 * cell_index. Encodes a window of cells before the position and uses
 * utf8_prev_grapheme() to find the boundary. */
int cursor_prev_grapheme(struct line *line, int cell_index)
{
	line_ensure_warm(line);
	if (cell_index <= 0)
		return 0;
	/* Encode cells leading up to cell_index into a temporary buffer */
	int start = cell_index - GRAPHEME_MAX_CELLS;
	if (start < 0)
		start = 0;
	int count = cell_index - start;
	char buf[GRAPHEME_MAX_CELLS * UTF8_MAX_BYTES];
	int byte_offsets[GRAPHEME_MAX_CELLS + 1];
	int byte_len = 0;
	for (int i = 0; i < count; i++) {
		byte_offsets[i] = byte_len;
		byte_len += utf8_encode(line->cells[start + i].codepoint,
								buf + byte_len);
	}
	byte_offsets[count] = byte_len;
	/* Find the previous grapheme boundary in byte offsets */
	int prev_byte = utf8_prev_grapheme(buf, byte_len, byte_len);
	/* Convert byte offset back to cell index */
	for (int i = 0; i < count; i++) {
		if (byte_offsets[i] >= prev_byte)
			return start + i;
	}
	return start;
}
/* Returns the display width of a grapheme cluster spanning cells
 * [start_cell, end_cell) in the given line. Checks for VS-16 (U+FE0F emoji
 * presentation selector) which forces width 2. Otherwise returns the width
 * of the first codepoint with nonzero width. */
int grapheme_display_width(struct line *line, int start_cell, int end_cell)
{
	if (start_cell >= end_cell)
		return 0;
	/* Single-cell graphemes use cell_display_width directly */
	if (end_cell - start_cell == 1)
		return (utf8_cpwidth(line->cells[start_cell].codepoint) < 1)
				? 1
				: utf8_cpwidth(line->cells[start_cell].codepoint);
	/* Multi-cell grapheme cluster: check for VS-16 (emoji presentation) */
	for (int i = start_cell; i < end_cell; i++) {
		if (line->cells[i].codepoint == 0xFE0F)
			return 2;
	}
	/* Regional indicator pairs (flags) are width 2 */
	uint32_t first_cp = line->cells[start_cell].codepoint;
	if (first_cp >= 0x1F1E6 && first_cp <= 0x1F1FF)
		return 2;
	/* Extended pictographic (emoji) clusters are width 2 */
	int w = utf8_cpwidth(first_cp);
	if (w >= 2)
		return w;
	/* ZWJ sequences with emoji base are width 2 */
	for (int i = start_cell; i < end_cell; i++) {
		if (line->cells[i].codepoint == 0x200D)
			return 2;
	}
	/* Fallback: width of first nonzero-width codepoint */
	for (int i = start_cell; i < end_cell; i++) {
		w = utf8_cpwidth(line->cells[i].codepoint);
		if (w > 0)
			return w;
	}
	return 1;
}

/* Converts a cell index to its display column position (grapheme-aware).
 * Iterates by grapheme cluster, using grapheme_display_width for multi-cell
 * clusters and cell_display_width for single-cell graphemes (tabs). */
int line_cell_to_render_column(struct line *line, int cell_index)
{
	line_ensure_warm(line);
	int column = 0;
	int i = 0;
	while (i < cell_index && i < (int)line->cell_count) {
		int next = cursor_next_grapheme(line, i);
		if (next > cell_index)
			next = cell_index;
		if (line->cells[i].codepoint == '\t') {
			column += cell_display_width(&line->cells[i], column);
			i++;
		} else {
			column += grapheme_display_width(line, i, next);
			i = next;
		}
	}
	return column;
}

/* Converts a display column to the corresponding cell index (grapheme-aware).
 * Returns the cell index at the start of the grapheme cluster containing the
 * given render column. */
int line_render_column_to_cell(struct line *line, int render_col)
{
	line_ensure_warm(line);
	int current_col = 0;
	int i = 0;
	while (i < (int)line->cell_count) {
		int next = cursor_next_grapheme(line, i);
		int w;
		if (line->cells[i].codepoint == '\t') {
			w = cell_display_width(&line->cells[i], current_col);
			next = i + 1;
		} else {
			w = grapheme_display_width(line, i, next);
		}
		current_col += w;
		if (current_col > render_col)
			return i;
		i = next;
	}
	return (int)line->cell_count;
}

/* Returns the total display width of a line (tab and width aware). */
int line_render_width(struct line *line)
{
	line_ensure_warm(line);
	return line_cell_to_render_column(line, (int)line->cell_count);
}
/* Recalculates syntax highlighting for a line. Walks through each cell
 * and assigns a syntax type based on the active syntax rules.
 * prev_open_comment indicates whether the previous line ends in an unclosed
 * multi-line comment. Returns true if this line's open_comment state changed,
 * signaling the caller should update the next line. */
int line_update_syntax(struct line *line, int prev_open_comment)
{
	line_ensure_warm(line);
	if (editor.syntax == NULL)
		return 0;

	/* Reset all cells to HL_NORMAL */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		line->cells[i].syntax = HL_NORMAL;
	}

	char **keywords = editor.syntax->keywords;

	char *single_comment = editor.syntax->singleline_comment_start;
	char *multi_comment_start = editor.syntax->multiline_comment_start;
	char *multi_comment_end = editor.syntax->multiline_comment_end;

	int single_comment_length = single_comment ? strlen(single_comment) : 0;
	int multi_start_length = multi_comment_start ? strlen(multi_comment_start) : 0;
	int multi_end_length = multi_comment_end ? strlen(multi_comment_end) : 0;

	int previous_separator = 1;
	int in_string = 0;
	int in_comment = prev_open_comment;

	/* Check if every codepoint on this line is ASCII. When true, each
	 * cell maps 1:1 to a byte and we can skip utf8_encode entirely. */
	int all_ascii = 1;
	for (uint32_t k = 0; k < line->cell_count; k++) {
		if (line->cells[k].codepoint >= ASCII_MAX) {
			all_ascii = 0;
			break;
		}
	}

	/* Build a byte string for keyword/comment matching, reusing the
	 * editor's scratch buffer to avoid malloc/free on every line. */
	size_t max_byte_size = all_ascii
		? (size_t)line->cell_count + 1
		: (size_t)line->cell_count * UTF8_MAX_BYTES + 1;
	if (max_byte_size > editor.scratch_capacity) {
		editor.scratch_capacity = max_byte_size * 2;
		editor.scratch_buffer = realloc(editor.scratch_buffer,
						editor.scratch_capacity);
		if (editor.scratch_buffer == NULL)
			terminal_die("realloc");
	}
	char *render = editor.scratch_buffer;

	/* Ensure the byte_offsets scratch array has room for cell_count + 1. */
	size_t offsets_needed = (size_t)line->cell_count + 1;
	if (offsets_needed > editor.scratch_offsets_capacity) {
		editor.scratch_offsets_capacity = offsets_needed * 2;
		editor.scratch_offsets = realloc(editor.scratch_offsets,
						 sizeof(int) * editor.scratch_offsets_capacity);
		if (editor.scratch_offsets == NULL)
			terminal_die("realloc");
	}
	int *byte_offsets = editor.scratch_offsets;

	if (all_ascii) {
		/* ASCII fast path: cast each codepoint directly to a byte.
		 * The byte_offsets array is a trivial 1:1 identity mapping. */
		for (uint32_t k = 0; k < line->cell_count; k++) {
			render[k] = (char)line->cells[k].codepoint;
			byte_offsets[k] = (int)k;
		}
		render[line->cell_count] = '\0';
		byte_offsets[line->cell_count] = (int)line->cell_count;
	} else {
		/* General UTF-8 path: encode each codepoint and build the
		 * cell-to-byte offset map. Multi-byte characters make cell
		 * indices diverge from byte offsets. */
		size_t byte_len = 0;
		for (uint32_t k = 0; k < line->cell_count; k++) {
			int written = utf8_encode(line->cells[k].codepoint,
						  &render[byte_len]);
			byte_len += written;
		}
		render[byte_len] = '\0';

		int bpos = 0;
		for (uint32_t k = 0; k < line->cell_count; k++) {
			byte_offsets[k] = bpos;
			char tmp[UTF8_MAX_BYTES];
			bpos += utf8_encode(line->cells[k].codepoint, tmp);
		}
		byte_offsets[line->cell_count] = bpos;
	}

	int i = 0;
	while (i < (int)line->cell_count) {
		uint32_t current_cp = line->cells[i].codepoint;
		uint16_t previous_syntax = (i > 0) ? line->cells[i - 1].syntax : HL_NORMAL;

		if (single_comment_length && !in_string && !in_comment) {
			if (!strncmp(&render[byte_offsets[i]], single_comment, single_comment_length)) {
				for (int k = i; k < (int)line->cell_count; k++)
					line->cells[k].syntax = HL_COMMENT;
				break;
			}
		}

		if (multi_start_length && multi_end_length && !in_string) {
			if (in_comment) {
				line->cells[i].syntax = HL_MLCOMMENT;
				if (!strncmp(&render[byte_offsets[i]], multi_comment_end, multi_end_length)) {
					for (int k = i; k < i + multi_end_length && k < (int)line->cell_count; k++)
						line->cells[k].syntax = HL_MLCOMMENT;
					i += multi_end_length;
					in_comment = 0;
					previous_separator = 1;
					continue;
				} else {
					i++;
					continue;
				}
			} else if (!strncmp(&render[byte_offsets[i]], multi_comment_start, multi_start_length)) {
				for (int k = i; k < i + multi_start_length && k < (int)line->cell_count; k++)
					line->cells[k].syntax = HL_MLCOMMENT;
				i += multi_start_length;
				in_comment = 1;
				continue;
			}
		}

		if (editor.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				line->cells[i].syntax = HL_STRING;
				if (current_cp == '\\' && i + 1 < (int)line->cell_count) {
					line->cells[i + 1].syntax = HL_STRING;
					i += 2;
					continue;
				}
				if ((int)current_cp == in_string)
					in_string = 0;
				i++;
				previous_separator = 1;
				continue;
			} else {
				if (current_cp == '"' || current_cp == '\'' ||
			    (editor.syntax->extra_string_delimiter &&
			     (int)current_cp == editor.syntax->extra_string_delimiter)) {
					in_string = current_cp;
					line->cells[i].syntax = HL_STRING;
					i++;
					continue;
				}
			}
		}

		if (editor.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(current_cp) && (previous_separator || previous_syntax == HL_NUMBER)) ||
					(current_cp == '.' && previous_syntax == HL_NUMBER)) {
				line->cells[i].syntax = HL_NUMBER;
				i++;
				previous_separator = 0;
				continue;
			}
		}

		if (previous_separator) {
			int j;
			int *keyword_lengths = editor.syntax->keyword_lengths;
			for (j = 0; keywords[j]; j++) {
				int keyword_length = keyword_lengths[j];
				int is_type_keyword = keywords[j][keyword_length - 1] == '|';
				if (is_type_keyword)
					keyword_length--;
				if (i + keyword_length <= (int)line->cell_count &&
						!strncmp(&render[byte_offsets[i]], keywords[j], keyword_length) &&
						syntax_is_separator(render[byte_offsets[i + keyword_length]])) {
					for (int k = i; k < i + keyword_length && k < (int)line->cell_count; k++)
						line->cells[k].syntax = is_type_keyword ? HL_KEYWORD2 : HL_KEYWORD1;
					i += keyword_length;
					break;
				}
			}
			if (keywords[j] != NULL) {
				previous_separator = 0;
				continue;
			}
		}

		previous_separator = syntax_is_separator(current_cp);
		i++;
	}

	int changed = (line->open_comment != in_comment);
	line->open_comment = in_comment;
	return changed;
}
/* Propagates syntax highlighting forward from the given line until
 * the open_comment state stabilizes. For WARM/HOT lines, runs the
 * full syntax update. For COLD lines, updates open_comment directly
 * and marks them stale so highlighting happens when they are warmed.
 * Stops at COLD lines whose open_comment already matches because
 * the input comment state hasn't changed so no further propagation
 * is needed -- the line will be re-highlighted when it warms. */
void syntax_propagate(int from_line)
{
	for (int i = from_line; i < editor.line_count; i++) {
		int prev_open = (i > 0) ? editor.lines[i - 1].open_comment : 0;
		struct line *ln = &editor.lines[i];

		if (ln->temperature == LINE_COLD) {
			/* If the incoming comment state already matches what
			 * this COLD line expects, the propagation has reached
			 * a stable point. The line will compute its own syntax
			 * correctly when eventually warmed. */
			if (ln->open_comment == prev_open)
				break;
			ln->open_comment = prev_open;
			ln->syntax_stale = 1;
			continue;
		}

		if (!line_update_syntax(ln, prev_open))
			break;
		ln->syntax_stale = 0;
	}
}
/* Recalculates the number of columns reserved for the line number gutter
 * based on the current row count. The width equals the number of digits
 * needed to display the highest line number, plus one trailing space. */
void editor_update_gutter_width(void)
{
	if (!editor.show_line_numbers) {
		editor.line_number_width = 0;
		return;
	}
	int line_count = editor.line_count > 0 ? editor.line_count : 1;
	int digits = 0;
	while (line_count > 0) {
		line_count /= 10;
		digits++;
	}
	editor.line_number_width = digits + 1;
}

/* Ensures the lines array has room for at least 'needed' entries.
 * Uses a doubling strategy starting from 16 to amortize realloc costs. */
void editor_lines_ensure_capacity(int needed)
{
	if (needed <= editor.line_capacity)
		return;
	int new_capacity = editor.line_capacity ? editor.line_capacity : 16;
	while (new_capacity < needed)
		new_capacity *= 2;
	editor.lines = realloc(editor.lines,
			       sizeof(struct line) * new_capacity);
	if (editor.lines == NULL)
		terminal_die("realloc");
	editor.line_capacity = new_capacity;
}

/* Inserts a new line at the given position in the editor's lines array.
 * Populates the line from the provided byte string, initializes the line
 * fields, and triggers a syntax update. Shifts existing lines down and
 * increments their line_index values. */
void editor_line_insert(int position, const char *string, size_t length)
{
	if (position < 0 || position > editor.line_count)
		return;

	editor_lines_ensure_capacity(editor.line_count + 1);
	memmove(&editor.lines[position + 1], &editor.lines[position],
				sizeof(struct line) * (editor.line_count - position));
	for (int j = position + 1; j <= editor.line_count; j++)
		editor.lines[j].line_index++;

	line_init(&editor.lines[position], position);
	line_populate_from_bytes(&editor.lines[position], string, length);

	editor.line_count++;
	editor_update_gutter_width();
	editor.dirty++;

	/* Run syntax highlighting on the new line and propagate changes */
	syntax_propagate(position);
}

/* Removes the line at the given position. Frees its memory, shifts remaining
 * lines up, and decrements their line_index values. */
void editor_line_delete(int position)
{
	if (position < 0 || position >= editor.line_count)
		return;
	line_free(&editor.lines[position]);
	memmove(&editor.lines[position], &editor.lines[position + 1],
				sizeof(struct line) * (editor.line_count - position - 1));
	for (int j = position; j < editor.line_count - 1; j++)
		editor.lines[j].line_index--;
	editor.line_count--;
	editor_update_gutter_width();
	editor.dirty++;
}

/*** Undo ***/

void undo_entry_free(struct undo_entry *entry)
{
	free(entry->saved_cells);
	entry->saved_cells = NULL;
}

void undo_group_free(struct undo_group *group)
{
	for (int i = 0; i < group->entry_count; i++)
		undo_entry_free(&group->entries[i]);
	free(group->entries);
	group->entries = NULL;
	group->entry_count = 0;
	group->entry_capacity = 0;
}

void undo_stack_init(struct undo_stack *stack)
{
	stack->groups = NULL;
	stack->group_count = 0;
	stack->current = 0;
	stack->group_capacity = 0;
}

void undo_stack_destroy(struct undo_stack *stack)
{
	for (int i = 0; i < stack->group_count; i++)
		undo_group_free(&stack->groups[i]);
	free(stack->groups);
	*stack = (struct undo_stack){0};
}

/* Discards all redo groups (everything from current to group_count). */
void undo_stack_discard_redo(struct undo_stack *stack)
{
	for (int i = stack->current; i < stack->group_count; i++)
		undo_group_free(&stack->groups[i]);
	stack->group_count = stack->current;
}

/* Returns a pointer to the current undo group, creating a new one if
 * the stack is empty, the time threshold has elapsed, or force_new
 * is set. Evicts the oldest group when UNDO_MAX_GROUPS is reached. */
struct undo_group *undo_stack_current_group(struct undo_stack *stack,
					    int force_new)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	int need_new = force_new || (stack->current == 0);

	if (!need_new && stack->current > 0) {
		struct undo_group *last = &stack->groups[stack->current - 1];
		long elapsed = (now.tv_sec - last->timestamp.tv_sec) *
				       MICROSECONDS_PER_SECOND +
			       (now.tv_usec - last->timestamp.tv_usec);
		if (elapsed > UNDO_GROUP_THRESHOLD_US)
			need_new = 1;
	}

	if (!need_new)
		return &stack->groups[stack->current - 1];

	undo_stack_discard_redo(stack);

	/* Evict oldest group if at capacity */
	if (stack->current >= UNDO_MAX_GROUPS) {
		undo_group_free(&stack->groups[0]);
		memmove(&stack->groups[0], &stack->groups[1],
			sizeof(struct undo_group) * (stack->current - 1));
		stack->current--;
		stack->group_count--;
	}

	if (stack->group_count >= stack->group_capacity) {
		int new_capacity = stack->group_capacity
					  ? stack->group_capacity * 2
					  : 16;
		stack->groups = realloc(stack->groups,
				       sizeof(struct undo_group) * new_capacity);
		if (stack->groups == NULL)
			terminal_die("realloc");
		stack->group_capacity = new_capacity;
	}

	struct undo_group *group = &stack->groups[stack->current];
	group->entries = NULL;
	group->entry_count = 0;
	group->entry_capacity = 0;
	group->timestamp = now;

	stack->current++;
	stack->group_count = stack->current;

	return group;
}

void undo_group_push_entry(struct undo_group *group, struct undo_entry entry)
{
	if (group->entry_count >= group->entry_capacity) {
		int new_capacity = group->entry_capacity
					  ? group->entry_capacity * 2
					  : 8;
		group->entries = realloc(group->entries,
				       sizeof(struct undo_entry) * new_capacity);
		if (group->entries == NULL)
			terminal_die("realloc");
		group->entry_capacity = new_capacity;
	}
	group->entries[group->entry_count++] = entry;
}

/* Records an undo entry. force_new_group causes a new group boundary
 * (used for structural operations like Enter and line joins).
 * Also forces a new group when the cursor has moved to a different
 * line since the last entry — edits on different lines should not
 * be grouped together. */
void undo_push(struct undo_entry entry, int force_new_group)
{
	if (!force_new_group && editor.undo.current > 0) {
		struct undo_group *last =
			&editor.undo.groups[editor.undo.current - 1];
		if (last->entry_count > 0) {
			struct undo_entry *prev =
				&last->entries[last->entry_count - 1];
			if (prev->line_index != entry.line_index)
				force_new_group = 1;
		}
	}
	struct undo_group *group =
		undo_stack_current_group(&editor.undo, force_new_group);
	undo_group_push_entry(group, entry);
}

/* Reverses a single undo entry. */
void undo_entry_apply_reverse(struct undo_entry *entry)
{
	switch (entry->type) {
	case UNDO_INSERT_CHAR:
		if (entry->line_index < editor.line_count) {
			struct line *ln = &editor.lines[entry->line_index];
			line_ensure_warm(ln);
			if ((uint32_t)entry->cell_index < ln->cell_count)
				line_delete_cell(ln, (uint32_t)entry->cell_index);
		}
		break;
	case UNDO_DELETE_CHAR:
		if (entry->line_index < editor.line_count) {
			struct line *ln = &editor.lines[entry->line_index];
			line_insert_cell(ln, (uint32_t)entry->cell_index,
					 entry->saved_cell);
		}
		break;
	case UNDO_SPLIT_LINE:
		if (entry->line_index + 1 < editor.line_count) {
			struct line *upper = &editor.lines[entry->line_index];
			struct line *lower =
				&editor.lines[entry->line_index + 1];
			line_ensure_warm(upper);
			line_ensure_warm(lower);
			upper->cell_count = (uint32_t)entry->cell_index;
			upper->temperature = LINE_HOT;
			line_append_cells(upper, lower, 0);
			editor_line_delete(entry->line_index + 1);
		}
		break;
	case UNDO_JOIN_LINE:
		if (entry->line_index < editor.line_count) {
			struct line *ln = &editor.lines[entry->line_index];
			line_ensure_warm(ln);
			uint32_t tail_start = (uint32_t)entry->cell_index;
			uint32_t tail_count = ln->cell_count - tail_start;
			editor_line_insert(entry->line_index + 1, "", 0);
			struct line *new_line =
				&editor.lines[entry->line_index + 1];
			line_ensure_capacity(new_line, tail_count);
			if (tail_count > 0) {
				ln = &editor.lines[entry->line_index];
				memcpy(new_line->cells, &ln->cells[tail_start],
				       sizeof(struct cell) * tail_count);
				new_line->cell_count = tail_count;
			}
			ln = &editor.lines[entry->line_index];
			ln->cell_count = tail_start;
			ln->temperature = LINE_HOT;
		}
		break;
	case UNDO_INSERT_LINE:
		if (entry->line_index < editor.line_count)
			editor_line_delete(entry->line_index);
		break;
	case UNDO_DELETE_LINE:
		editor_line_insert(entry->line_index, "", 0);
		if (entry->saved_cells && entry->saved_cell_count > 0) {
			struct line *ln = &editor.lines[entry->line_index];
			line_ensure_capacity(ln, entry->saved_cell_count);
			memcpy(ln->cells, entry->saved_cells,
			       sizeof(struct cell) * entry->saved_cell_count);
			ln->cell_count = entry->saved_cell_count;
			ln->temperature = LINE_HOT;
		}
		break;
	}
}

/* Replays a single undo entry forward (for redo). */
void undo_entry_apply_forward(struct undo_entry *entry)
{
	switch (entry->type) {
	case UNDO_INSERT_CHAR:
		if (entry->line_index < editor.line_count) {
			struct line *ln = &editor.lines[entry->line_index];
			line_insert_cell(ln, (uint32_t)entry->cell_index,
					 entry->saved_cell);
		}
		break;
	case UNDO_DELETE_CHAR:
		if (entry->line_index < editor.line_count) {
			struct line *ln = &editor.lines[entry->line_index];
			line_ensure_warm(ln);
			if ((uint32_t)entry->cell_index < ln->cell_count)
				line_delete_cell(ln, (uint32_t)entry->cell_index);
		}
		break;
	case UNDO_SPLIT_LINE:
		if (entry->line_index < editor.line_count) {
			struct line *ln = &editor.lines[entry->line_index];
			line_ensure_warm(ln);
			uint32_t split = (uint32_t)entry->cell_index;
			uint32_t tail_count = ln->cell_count - split;
			editor_line_insert(entry->line_index + 1, "", 0);
			struct line *new_line =
				&editor.lines[entry->line_index + 1];
			line_ensure_capacity(new_line, tail_count);
			if (tail_count > 0) {
				ln = &editor.lines[entry->line_index];
				memcpy(new_line->cells, &ln->cells[split],
				       sizeof(struct cell) * tail_count);
				new_line->cell_count = tail_count;
			}
			ln = &editor.lines[entry->line_index];
			ln->cell_count = split;
			ln->temperature = LINE_HOT;
		}
		break;
	case UNDO_JOIN_LINE:
		if (entry->line_index + 1 < editor.line_count) {
			struct line *upper = &editor.lines[entry->line_index];
			struct line *lower =
				&editor.lines[entry->line_index + 1];
			line_ensure_warm(upper);
			line_ensure_warm(lower);
			line_append_cells(upper, lower, 0);
			editor_line_delete(entry->line_index + 1);
		}
		break;
	case UNDO_INSERT_LINE:
		editor_line_insert(entry->line_index, "", 0);
		if (entry->saved_cells && entry->saved_cell_count > 0) {
			struct line *ln = &editor.lines[entry->line_index];
			line_ensure_capacity(ln, entry->saved_cell_count);
			memcpy(ln->cells, entry->saved_cells,
			       sizeof(struct cell) * entry->saved_cell_count);
			ln->cell_count = entry->saved_cell_count;
			ln->temperature = LINE_HOT;
		}
		break;
	case UNDO_DELETE_LINE:
		if (entry->line_index < editor.line_count)
			editor_line_delete(entry->line_index);
		break;
	}
}

/* Undoes the most recent group. */
void editor_undo(void)
{
	editor.force_full_redraw = 1;
	if (editor.undo.current == 0) {
		editor_set_status_message("Nothing to undo");
		return;
	}

	editor.undo.current--;
	struct undo_group *group = &editor.undo.groups[editor.undo.current];

	for (int i = group->entry_count - 1; i >= 0; i--)
		undo_entry_apply_reverse(&group->entries[i]);

	if (group->entry_count > 0) {
		editor.cursor_x = group->entries[0].cursor_x_before;
		editor.cursor_y = group->entries[0].cursor_y_before;
	}

	editor.dirty++;
	editor_set_status_message("Undo");
}

/* Redoes the most recently undone group. */
void editor_redo(void)
{
	editor.force_full_redraw = 1;
	if (editor.undo.current >= editor.undo.group_count) {
		editor_set_status_message("Nothing to redo");
		return;
	}

	struct undo_group *group = &editor.undo.groups[editor.undo.current];

	for (int i = 0; i < group->entry_count; i++)
		undo_entry_apply_forward(&group->entries[i]);

	if (group->entry_count > 0) {
		struct undo_entry *last =
			&group->entries[group->entry_count - 1];
		switch (last->type) {
		case UNDO_INSERT_CHAR:
			editor.cursor_x = last->cell_index + 1;
			editor.cursor_y = last->line_index;
			break;
		case UNDO_DELETE_CHAR:
			editor.cursor_x = last->cell_index;
			editor.cursor_y = last->line_index;
			break;
		case UNDO_SPLIT_LINE:
			editor.cursor_x = 0;
			editor.cursor_y = last->line_index + 1;
			break;
		case UNDO_JOIN_LINE:
			editor.cursor_x = last->cell_index;
			editor.cursor_y = last->line_index;
			break;
		case UNDO_INSERT_LINE:
			editor.cursor_x = 0;
			editor.cursor_y = last->line_index;
			break;
		case UNDO_DELETE_LINE:
			editor.cursor_x = 0;
			editor.cursor_y = last->line_index;
			break;
		}
	}

	editor.undo.current++;
	editor.dirty++;
	editor_set_status_message("Redo");
}

/*** Selection ***/

/* Clears the active selection. */
void selection_clear(void)
{
	if (!editor.selection.active)
		return;
	editor.force_full_redraw = 1;
	int start_y, start_x, end_y, end_x;
	if (editor.selection.anchor_y < editor.cursor_y ||
	    (editor.selection.anchor_y == editor.cursor_y &&
	     editor.selection.anchor_x <= editor.cursor_x)) {
		start_y = editor.selection.anchor_y;
		start_x = editor.selection.anchor_x;
		end_y = editor.cursor_y;
		end_x = editor.cursor_x;
	} else {
		start_y = editor.cursor_y;
		start_x = editor.cursor_x;
		end_y = editor.selection.anchor_y;
		end_x = editor.selection.anchor_x;
	}
	for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		if (ln->temperature == LINE_COLD)
			continue;
		int from = (y == start_y) ? start_x : 0;
		int to = (y == end_y) ? end_x : (int)ln->cell_count;
		if (to > (int)ln->cell_count)
			to = (int)ln->cell_count;
		for (int i = from; i < to; i++)
			ln->cells[i].flags &= ~CELL_FLAG_SELECTED;
	}
	editor.selection.active = 0;
}

/* Begins a new selection at the current cursor position. */
void selection_start(void)
{
	if (editor.selection.active)
		return;
	editor.selection.active = 1;
	editor.selection.anchor_y = editor.cursor_y;
	editor.selection.anchor_x = editor.cursor_x;
}

/* Returns the ordered start/end of the current selection. */
int selection_get_range(int *start_y, int *start_x, int *end_y, int *end_x)
{
	if (!editor.selection.active)
		return 0;
	if (editor.selection.anchor_y < editor.cursor_y ||
	    (editor.selection.anchor_y == editor.cursor_y &&
	     editor.selection.anchor_x <= editor.cursor_x)) {
		*start_y = editor.selection.anchor_y;
		*start_x = editor.selection.anchor_x;
		*end_y = editor.cursor_y;
		*end_x = editor.cursor_x;
	} else {
		*start_y = editor.cursor_y;
		*start_x = editor.cursor_x;
		*end_y = editor.selection.anchor_y;
		*end_x = editor.selection.anchor_x;
	}
	return 1;
}

/* Updates CELL_FLAG_SELECTED on cells in the selection range. */
void selection_update(void)
{
	if (!editor.selection.active)
		return;
	editor.force_full_redraw = 1;
	int start_y, start_x, end_y, end_x;
	selection_get_range(&start_y, &start_x, &end_y, &end_x);

	for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		line_ensure_warm(ln);
		int from = (y == start_y) ? start_x : 0;
		int to = (y == end_y) ? end_x : (int)ln->cell_count;
		if (to > (int)ln->cell_count)
			to = (int)ln->cell_count;
		for (uint32_t i = 0; i < ln->cell_count; i++) {
			if ((int)i >= from && (int)i < to)
				ln->cells[i].flags |= CELL_FLAG_SELECTED;
			else
				ln->cells[i].flags &= ~CELL_FLAG_SELECTED;
		}
	}
}

/* Extracts the selected text as a UTF-8 string. Caller frees. */
char *selection_to_string(size_t *out_length)
{
	int start_y, start_x, end_y, end_x;
	if (!selection_get_range(&start_y, &start_x, &end_y, &end_x))
		return NULL;

	size_t capacity = 256;
	size_t length = 0;
	char *buffer = malloc(capacity);
	if (!buffer)
		return NULL;

	for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		line_ensure_warm(ln);
		int from = (y == start_y) ? start_x : 0;
		int to = (y == end_y) ? end_x : (int)ln->cell_count;
		if (to > (int)ln->cell_count)
			to = (int)ln->cell_count;
		for (int i = from; i < to; i++) {
			char utf8_buf[UTF8_MAX_BYTES];
			int bytes = utf8_encode(ln->cells[i].codepoint, utf8_buf);
			if (length + bytes + 2 > capacity) {
				capacity *= 2;
				buffer = realloc(buffer, capacity);
				if (!buffer)
					return NULL;
			}
			memcpy(buffer + length, utf8_buf, bytes);
			length += bytes;
		}
		if (y < end_y) {
			if (length + 2 > capacity) {
				capacity *= 2;
				buffer = realloc(buffer, capacity);
				if (!buffer)
					return NULL;
			}
			buffer[length++] = '\n';
		}
	}
	buffer[length] = '\0';
	*out_length = length;
	return buffer;
}

/* Deletes all text in the current selection. */
void selection_delete(void)
{
	editor.force_full_redraw = 1;
	int start_y, start_x, end_y, end_x;
	if (!selection_get_range(&start_y, &start_x, &end_y, &end_x))
		return;

	if (start_y == end_y) {
		struct line *ln = &editor.lines[start_y];
		line_ensure_warm(ln);
		for (int i = end_x - 1; i >= start_x; i--)
			line_delete_cell(ln, (uint32_t)i);
	} else {
		/* Truncate first line at start_x */
		struct line *first = &editor.lines[start_y];
		line_ensure_warm(first);
		first->cell_count = (uint32_t)start_x;
		first->temperature = LINE_HOT;

		/* Append remaining cells from last line */
		struct line *last = &editor.lines[end_y];
		line_ensure_warm(last);
		if (end_x < (int)last->cell_count)
			line_append_cells(first, last, (uint32_t)end_x);

		/* Delete middle and last lines (in reverse) */
		for (int y = end_y; y > start_y; y--)
			editor_line_delete(y);
	}

	editor.cursor_y = start_y;
	editor.cursor_x = start_x;
	selection_clear();
	editor.selection.active = 0;
	syntax_propagate(start_y);
	editor.dirty++;
}

/*** Clipboard ***/

void clipboard_clear(void)
{
	for (int i = 0; i < editor.clipboard.line_count; i++)
		free(editor.clipboard.lines[i]);
	free(editor.clipboard.lines);
	free(editor.clipboard.line_lengths);
	editor.clipboard = (struct clipboard){0};
}

void clipboard_store(const char *text, size_t length, int is_line_mode)
{
	clipboard_clear();

	/* Count lines */
	int count = 1;
	for (size_t i = 0; i < length; i++)
		if (text[i] == '\n')
			count++;

	editor.clipboard.lines = malloc(sizeof(char *) * count);
	editor.clipboard.line_lengths = malloc(sizeof(size_t) * count);
	editor.clipboard.line_count = 0;
	editor.clipboard.is_line_mode = is_line_mode;

	size_t start = 0;
	for (size_t i = 0; i <= length; i++) {
		if (i == length || text[i] == '\n') {
			size_t line_len = i - start;
			char *line = malloc(line_len + 1);
			memcpy(line, text + start, line_len);
			line[line_len] = '\0';
			editor.clipboard.lines[editor.clipboard.line_count] = line;
			editor.clipboard.line_lengths[editor.clipboard.line_count] = line_len;
			editor.clipboard.line_count++;
			start = i + 1;
		}
	}
}

/* Sends text to the system clipboard via OSC 52. */
void clipboard_set_system(const char *text, size_t length)
{
	if (length > OSC52_MAX_PAYLOAD_BYTES)
		return;
	/* Base64 encode */
	static const char b64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t encoded_len = 4 * ((length + 2) / 3);
	char *encoded = malloc(encoded_len + 1);
	if (!encoded)
		return;
	size_t ei = 0;
	for (size_t i = 0; i < length; i += 3) {
		uint32_t n = (uint32_t)((unsigned char)text[i]) << 16;
		if (i + 1 < length) n |= (uint32_t)((unsigned char)text[i + 1]) << 8;
		if (i + 2 < length) n |= (uint32_t)((unsigned char)text[i + 2]);
		encoded[ei++] = b64[(n >> 18) & 0x3F];
		encoded[ei++] = b64[(n >> 12) & 0x3F];
		encoded[ei++] = (i + 1 < length) ? b64[(n >> 6) & 0x3F] : '=';
		encoded[ei++] = (i + 2 < length) ? b64[n & 0x3F] : '=';
	}
	encoded[ei] = '\0';

	write(STDOUT_FILENO, "\x1b]52;c;", 7);
	write(STDOUT_FILENO, encoded, ei);
	write(STDOUT_FILENO, "\x1b\\", 2);
	free(encoded);
}

void editor_copy(void)
{
	if (!editor.selection.active) {
		editor_set_status_message("Nothing selected");
		return;
	}
	size_t length;
	char *text = selection_to_string(&length);
	if (!text)
		return;
	clipboard_store(text, length, 0);
	clipboard_set_system(text, length);
	editor_set_status_message("Copied %zu bytes", length);
	free(text);
}

void editor_cut(void)
{
	if (!editor.selection.active) {
		editor_set_status_message("Nothing selected");
		return;
	}
	editor_copy();
	selection_delete();
}

void editor_paste(void)
{
	if (editor.clipboard.line_count == 0) {
		editor_set_status_message("Nothing to paste");
		return;
	}

	if (editor.selection.active)
		selection_delete();

	if (editor.clipboard.is_line_mode) {
		/* Line-mode paste: insert whole lines below cursor */
		for (int i = 0; i < editor.clipboard.line_count; i++) {
			editor_line_insert(editor.cursor_y + 1 + i,
					   editor.clipboard.lines[i],
					   editor.clipboard.line_lengths[i]);
		}
		editor.cursor_y++;
		editor.cursor_x = 0;
	} else if (editor.clipboard.line_count == 1) {
		/* Single-line paste: insert characters at cursor */
		const char *text = editor.clipboard.lines[0];
		size_t len = editor.clipboard.line_lengths[0];
		size_t pos = 0;
		while (pos < len) {
			uint32_t cp;
			int consumed = utf8_decode(&text[pos], (int)(len - pos), &cp);
			if (consumed <= 0) { consumed = 1; cp = '?'; }
			editor_insert_char((int)cp);
			pos += consumed;
		}
	} else {
		/* Multi-line paste */
		for (int i = 0; i < editor.clipboard.line_count; i++) {
			if (i > 0)
				editor_insert_newline();
			const char *text = editor.clipboard.lines[i];
			size_t len = editor.clipboard.line_lengths[i];
			size_t pos = 0;
			while (pos < len) {
				uint32_t cp;
				int consumed = utf8_decode(&text[pos],
							   (int)(len - pos), &cp);
				if (consumed <= 0) { consumed = 1; cp = '?'; }
				editor_insert_char((int)cp);
				pos += consumed;
			}
		}
	}
	editor_set_status_message("Pasted");
}

void editor_cut_line(void)
{
	editor.force_full_redraw = 1;
	if (editor.cursor_y >= editor.line_count)
		return;
	size_t length;
	char *bytes = line_to_bytes(&editor.lines[editor.cursor_y], &length);
	if (!bytes)
		return;
	clipboard_store(bytes, length, 1);
	clipboard_set_system(bytes, length);
	free(bytes);
	editor_line_delete(editor.cursor_y);
	if (editor.cursor_y >= editor.line_count && editor.line_count > 0)
		editor.cursor_y = editor.line_count - 1;
	int row_length = (editor.cursor_y < editor.line_count)
				 ? (int)editor.lines[editor.cursor_y].cell_count
				 : 0;
	if (editor.cursor_x > row_length)
		editor.cursor_x = row_length;
	editor_set_status_message("Line cut");
}

void editor_duplicate_line(void)
{
	editor.force_full_redraw = 1;
	if (editor.cursor_y >= editor.line_count)
		return;
	size_t length;
	char *bytes = line_to_bytes(&editor.lines[editor.cursor_y], &length);
	if (!bytes)
		return;
	editor_line_insert(editor.cursor_y + 1, bytes, length);
	free(bytes);
	editor.cursor_y++;
	editor_set_status_message("Line duplicated");
}

/*** Word Movement ***/

/* Classifies a codepoint for word movement: 0 = whitespace,
 * 1 = punctuation/separator, 2 = word character. This gives
 * three classes so punctuation like = and ; act as their own
 * "words" rather than being skipped with whitespace. */
int word_char_class(uint32_t codepoint)
{
	if (codepoint == ' ' || codepoint == '\t' || codepoint == '\0')
		return 0;
	if (codepoint < 128 && syntax_is_separator((int)codepoint))
		return 1;
	return 2;
}

/* Returns the cell index of the start of the previous word. */
int cursor_prev_word(struct line *line, int cell_index)
{
	if (cell_index <= 0)
		return 0;
	line_ensure_warm(line);
	int pos = cell_index - 1;
	/* Skip whitespace */
	while (pos > 0 && word_char_class(line->cells[pos].codepoint) == 0)
		pos--;
	/* Skip run of same class */
	if (pos >= 0) {
		int cls = word_char_class(line->cells[pos].codepoint);
		while (pos > 0 && word_char_class(line->cells[pos - 1].codepoint) == cls)
			pos--;
	}
	return pos;
}

/* Returns the cell index of the start of the next word. */
int cursor_next_word(struct line *line, int cell_index)
{
	line_ensure_warm(line);
	int count = (int)line->cell_count;
	if (cell_index >= count)
		return count;
	int pos = cell_index;
	/* Skip run of current class */
	int cls = word_char_class(line->cells[pos].codepoint);
	while (pos < count && word_char_class(line->cells[pos].codepoint) == cls)
		pos++;
	/* Skip whitespace */
	while (pos < count && word_char_class(line->cells[pos].codepoint) == 0)
		pos++;
	return pos;
}

/*** Editor operations ***/

/* Inserts a character at the current cursor position. If the cursor is
 * past the last line, a new empty line is created first. Advances the
 * cursor one position to the right. */
void editor_insert_char(int character)
{
	editor.force_full_redraw = 1;
	editor.preferred_column = -1;
	if (editor.cursor_y == editor.line_count) {
		editor_line_insert(editor.line_count, "", 0);
	}
	struct line *ln = &editor.lines[editor.cursor_y];
	struct cell c = {
		.codepoint = (uint32_t)character,
		.syntax = HL_NORMAL,
		.neighbor = 0,
		.flags = 0,
		.context = 0,
	};

	undo_push((struct undo_entry){
		.type = UNDO_INSERT_CHAR,
		.line_index = editor.cursor_y,
		.cell_index = editor.cursor_x,
		.saved_cell = c,
		.cursor_x_before = editor.cursor_x,
		.cursor_y_before = editor.cursor_y,
	}, 0);

	line_insert_cell(ln, (uint32_t)editor.cursor_x, c);
	syntax_propagate(editor.cursor_y);
	editor.dirty++;
	editor.cursor_x++;

	/* Bounds check */
	if (editor.cursor_x > (int)ln->cell_count)
		editor.cursor_x = (int)ln->cell_count;
}

/* Handles the Enter key by splitting the current line at the cursor position.
 * If the cursor is at the start, an empty line is inserted above. Otherwise
 * the text after the cursor moves to a new line below. */
void editor_insert_newline(void)
{
	editor.force_full_redraw = 1;
	editor.preferred_column = -1;
	undo_push((struct undo_entry){
		.type = UNDO_SPLIT_LINE,
		.line_index = editor.cursor_y,
		.cell_index = editor.cursor_x,
		.cursor_x_before = editor.cursor_x,
		.cursor_y_before = editor.cursor_y,
	}, 1);

	/* Capture leading whitespace for auto-indent before any realloc
	 * that might invalidate the line pointer. */
	struct cell indent_cells[128];
	int indent_count = 0;
	if (editor.cursor_y < editor.line_count && editor.cursor_x > 0) {
		struct line *ln = &editor.lines[editor.cursor_y];
		line_ensure_warm(ln);
		for (uint32_t i = 0; i < ln->cell_count && indent_count < 128; i++) {
			uint32_t cp = ln->cells[i].codepoint;
			if (cp == ' ' || cp == '\t')
				indent_cells[indent_count++] = ln->cells[i];
			else
				break;
		}
	}

	if (editor.cursor_x == 0) {
		editor_line_insert(editor.cursor_y, "", 0);
	} else {
		struct line *ln = &editor.lines[editor.cursor_y];
		/* Extract bytes after cursor for the new line.
		 * Convert cell index to byte offset since line_to_bytes
		 * produces a UTF-8 string where multi-byte characters make
		 * cell indices diverge from byte positions. */
		size_t tail_len;
		char *tail = line_to_bytes(ln, &tail_len);
		size_t byte_offset = 0;
		int cells = 0;
		while (cells < editor.cursor_x && byte_offset < tail_len) {
			uint32_t cp;
			int consumed = utf8_decode(&tail[byte_offset],
						   (int)(tail_len - byte_offset), &cp);
			if (consumed <= 0) consumed = 1;
			byte_offset += consumed;
			cells++;
		}
		size_t new_len = tail_len - byte_offset;
		editor_line_insert(editor.cursor_y + 1, &tail[byte_offset], new_len);
		free(tail);
		/* Truncate the current line at the cursor position */
		ln = &editor.lines[editor.cursor_y];
		ln->cell_count = (uint32_t)editor.cursor_x;
		syntax_propagate(editor.cursor_y);
		editor.dirty++;
	}
	editor.cursor_y++;

	/* Auto-indent: prepend captured whitespace to the new line */
	if (indent_count > 0) {
		struct line *new_ln = &editor.lines[editor.cursor_y];
		line_ensure_warm(new_ln);
		for (int i = indent_count - 1; i >= 0; i--) {
			indent_cells[i].syntax = HL_NORMAL;
			indent_cells[i].flags = 0;
			line_insert_cell(new_ln, 0, indent_cells[i]);
		}
	}
	editor.cursor_x = indent_count;
}

/* Deletes the grapheme cluster to the left of the cursor (backspace behavior).
 * If the cursor is at the start of a line, merges the line with the one above
 * by appending its content and deleting the current line. */
void editor_delete_char(void)
{
	editor.force_full_redraw = 1;
	editor.preferred_column = -1;
	if (editor.cursor_y == editor.line_count)
		return;
	if (editor.cursor_x == 0 && editor.cursor_y == 0)
		return;
	struct line *ln = &editor.lines[editor.cursor_y];
	if (editor.cursor_x > 0) {
		/* Find the start of the grapheme cluster before the cursor */
		int prev = cursor_prev_grapheme(ln, editor.cursor_x);
		/* Record each deleted cell for undo (reverse order so undo
		 * replays them in the correct insertion order). */
		for (int i = prev; i < editor.cursor_x; i++) {
			undo_push((struct undo_entry){
				.type = UNDO_DELETE_CHAR,
				.line_index = editor.cursor_y,
				.cell_index = prev,
				.saved_cell = ln->cells[i],
				.cursor_x_before = editor.cursor_x,
				.cursor_y_before = editor.cursor_y,
			}, 0);
		}
		/* Delete all cells in the cluster (from prev to cursor_x) */
		for (int i = editor.cursor_x - 1; i >= prev; i--) {
			line_delete_cell(ln, (uint32_t)i);
		}
		syntax_propagate(editor.cursor_y);
		editor.dirty++;
		editor.cursor_x = prev;
	} else {
		/* Line join: record the join point for undo */
		line_ensure_warm(&editor.lines[editor.cursor_y - 1]);
		int join_point =
			(int)editor.lines[editor.cursor_y - 1].cell_count;

		undo_push((struct undo_entry){
			.type = UNDO_JOIN_LINE,
			.line_index = editor.cursor_y - 1,
			.cell_index = join_point,
			.cursor_x_before = editor.cursor_x,
			.cursor_y_before = editor.cursor_y,
		}, 1);

		editor.cursor_x = join_point;
		line_append_cells(&editor.lines[editor.cursor_y - 1], ln, 0);
		editor_line_delete(editor.cursor_y);
		editor.cursor_y--;
		syntax_propagate(editor.cursor_y);
		editor.dirty++;
	}
}

/*** File i/o ***/

/* Concatenates all editor lines into a single newline-separated string for
 * writing to disk. Stores the total byte count in buffer_length. The caller
 * is responsible for freeing the returned buffer. */
char *editor_rows_to_string(size_t *buffer_length)
{
	size_t total_length = 0;
	size_t capacity = 0;
	char *buffer = NULL;

	for (int j = 0; j < editor.line_count; j++) {
		struct line *line = &editor.lines[j];
		size_t byte_len;
		const char *bytes;
		char *allocated_bytes = NULL;

		/* COLD lines still have their original mmap content, so copy
		 * directly from the mmap region without warming the line. This
		 * avoids a massive memory spike when saving large files where
		 * only a few lines were edited. */
		if (line->temperature == LINE_COLD && editor.mmap_base) {
			bytes = editor.mmap_base + line->mmap_offset;
			byte_len = line->mmap_length;
		} else {
			allocated_bytes = line_to_bytes(line, &byte_len);
			bytes = allocated_bytes;
		}

		size_t needed = total_length + byte_len + 1;
		if (needed > capacity) {
			capacity = needed * 2;
			buffer = realloc(buffer, capacity);
			if (buffer == NULL) terminal_die("realloc");
		}
		memcpy(buffer + total_length, bytes, byte_len);
		total_length += byte_len;
		free(allocated_bytes);
		buffer[total_length] = '\n';
		total_length++;
	}
	*buffer_length = total_length;
	return buffer;
}
/* Opens a file using mmap for lazy loading. Scans the mapped region for
 * newlines to build a line index with byte offsets and lengths. All lines
 * start COLD -- cells are allocated on demand when lines are accessed.
 * Returns 0 on success, negative errno on failure. */
int editor_open_mmap(char *filename)
{
	int ret = 0;
	int fd = -1;

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		ret = -errno;
		goto out;
	}

	off_t file_size = lseek(fd, 0, SEEK_END);
	if (file_size == -1) {
		ret = -errno;
		goto out;
	}

	if (file_size == 0)
		goto out;

	char *base = mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (base == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	/* Success: transfer fd ownership to editor state */
	editor.file_descriptor = fd;
	editor.mmap_base = base;
	editor.mmap_size = (size_t)file_size;
	fd = -1;

	/* Hint the kernel to read ahead sequentially during the line scan. */
	madvise(base, (size_t)file_size, MADV_SEQUENTIAL);

	/* Scan for newlines using memchr for SIMD-accelerated searching. */
	const char *pos = base;
	const char *end = base + file_size;
	const char *line_start = base;
	const char *newline;
	while ((newline = memchr(pos, '\n', (size_t)(end - pos))) != NULL) {
		size_t line_len = (size_t)(newline - line_start);
		/* Strip trailing \r */
		if (line_len > 0 && line_start[line_len - 1] == '\r')
			line_len--;

		editor_lines_ensure_capacity(editor.line_count + 1);
		struct line *ln = &editor.lines[editor.line_count];
		ln->cells = NULL;
		ln->cell_count = 0;
		ln->cell_capacity = 0;
		ln->line_index = editor.line_count;
		ln->open_comment = 0;
		ln->mmap_offset = (size_t)(line_start - base);
		ln->mmap_length = (uint32_t)line_len;
		ln->temperature = LINE_COLD;
		ln->syntax_stale = 1;
		editor.line_count++;

		pos = newline + 1;
		line_start = pos;
	}

	/* Handle last line without trailing newline */
	if (line_start < end) {
		size_t line_len = (size_t)(end - line_start);
		if (line_len > 0 && line_start[line_len - 1] == '\r')
			line_len--;

		editor_lines_ensure_capacity(editor.line_count + 1);
		struct line *ln = &editor.lines[editor.line_count];
		ln->cells = NULL;
		ln->cell_count = 0;
		ln->cell_capacity = 0;
		ln->line_index = editor.line_count;
		ln->open_comment = 0;
		ln->mmap_offset = (size_t)(line_start - base);
		ln->mmap_length = (uint32_t)line_len;
		ln->temperature = LINE_COLD;
		ln->syntax_stale = 1;
		editor.line_count++;
	}

	/* Switch to random-access hint now that line scanning is complete. */
	madvise(editor.mmap_base, editor.mmap_size, MADV_RANDOM);

out:
	if (fd != -1)
		close(fd);
	return ret;
}


/* Opens a file by reading its contents line by line into editor rows.
 * Strips trailing newline and carriage return characters from each line.
 * Sets the editor filename and triggers syntax highlighting selection.
 * Returns 0 on success, negative errno on failure. */
int editor_open(char *filename)
{
	editor.force_full_redraw = 1;
	free(editor.filename);
	editor.filename = strdup(filename);
	if (editor.filename == NULL) {
		editor_set_status_message("Out of memory");
		return -ENOMEM;
	}

	int ret = editor_open_mmap(filename);
	if (ret < 0) {
		const char *error_msg;
		switch (-ret) {
		case ENOENT:
			error_msg = "File not found";
			break;
		case EACCES:
			error_msg = "Permission denied";
			break;
		case ENOMEM:
			error_msg = "Out of memory";
			break;
		default:
			error_msg = strerror(-ret);
			break;
		}
		editor_set_status_message("Can't open file: %s", error_msg);
		return ret;
	}
	editor_update_gutter_width();

	syntax_select_highlight();

	editor.dirty = 0;
	return 0;
}

/* Writes the current file content to disk using an atomic write-to-temp
 * + rename pattern. This ensures the original file is never corrupted
 * if a write fails partway through (disk full, I/O error, etc).
 * Returns 0 on success, negative errno on failure. */
int editor_save_write(void)
{
	int ret = 0;
	int fd = -1;
	char *tmp_path = NULL;
	size_t file_length;
	char *file_content = editor_rows_to_string(&file_length);

	/* Release mmap before writing — all lines are now warm. */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
		editor.mmap_size = 0;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}

	/* Build temporary file path in the same directory as the target
	 * so rename() is atomic (same filesystem). */
	size_t filename_len = strlen(editor.filename);
	tmp_path = malloc(filename_len + 8);
	if (tmp_path == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	snprintf(tmp_path, filename_len + 8, "%s.XXXXXX", editor.filename);

	fd = mkstemp(tmp_path);
	if (fd == -1) {
		ret = -errno;
		goto out;
	}

	/* Preserve original file permissions if the file exists */
	struct stat original_stat;
	if (stat(editor.filename, &original_stat) == 0) {
		fchmod(fd, original_stat.st_mode);
	} else {
		fchmod(fd, FILE_PERMISSION_DEFAULT);
	}

	/* Write all content, retrying on short writes. */
	size_t bytes_written = 0;
	while (bytes_written < file_length) {
		ssize_t result = write(fd, file_content + bytes_written,
				       file_length - bytes_written);
		if (result == -1) {
			if (errno == EINTR)
				continue;
			ret = -errno;
			goto out;
		}
		bytes_written += (size_t)result;
	}

	/* Flush to stable storage before rename */
	if (fsync(fd) == -1) {
		ret = -errno;
		goto out;
	}
	close(fd);
	fd = -1;

	/* Atomic rename over the original file */
	if (rename(tmp_path, editor.filename) == -1) {
		ret = -errno;
		goto out;
	}
	/* tmp_path is now consumed by rename — don't unlink it */
	free(tmp_path);
	tmp_path = NULL;

	editor.dirty = 0;
	editor_set_status_message("%zu bytes written to disk", file_length);

out:
	if (fd != -1)
		close(fd);
	if (tmp_path != NULL)
		unlink(tmp_path);
	free(tmp_path);
	free(file_content);
	if (ret < 0) {
		const char *error_msg;
		switch (-ret) {
		case EACCES:
			error_msg = "Permission denied";
			break;
		case ENOSPC:
			error_msg = "No space left on device";
			break;
		case EROFS:
			error_msg = "Read-only file system";
			break;
		case ENOMEM:
			error_msg = "Out of memory";
			break;
		default:
			error_msg = strerror(-ret);
			break;
		}
		editor_set_status_message("Can't save! %s", error_msg);
	}

	if (ret == 0 && editor.quit_after_save) {
		write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
		write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));
		exit(0);
	}
	return ret;
}

/* Called when the user accepts a filename in the save prompt. Sets the
 * filename, selects syntax highlighting, and writes the file. */
void editor_save_accept(char *filename)
{
	free(editor.filename);
	editor.filename = filename;
	syntax_select_highlight();
	editor_save_write();
}

/* Called when the user cancels the save prompt. */
void editor_save_cancel(void)
{
	editor_set_status_message("Save aborted");
	editor.quit_after_save = 0;
}

/* Starts the save flow. If a filename exists, writes immediately.
 * Otherwise opens a prompt to ask for one. */
void editor_save_start(void)
{
	if (editor.filename != NULL) {
		editor_save_write();
	} else {
		prompt_open("Save as: %s (ESC to cancel)", NULL,
			    editor_save_accept, editor_save_cancel);
	}
}

/* Called when the user accepts a filename in the save-as prompt. Replaces
 * the current filename, re-runs syntax highlighting, and writes. */
void editor_save_as_accept(char *filename)
{
	free(editor.filename);
	editor.filename = filename;
	syntax_select_highlight();
	editor_save_write();
}

/* Called when the user cancels the save-as prompt. */
void editor_save_as_cancel(void)
{
	editor_set_status_message("Save as aborted");
}

/* Opens a save-as prompt, always asking for a filename. */
void editor_save_as_start(void)
{
	prompt_open("Save as: %s (ESC to cancel)", NULL,
		    editor_save_as_accept, editor_save_as_cancel);
}

/* Handles the quit confirmation response. 'y' saves then exits, 'n'
 * exits without saving, anything else returns to normal editing. */
void editor_quit_confirm(int key)
{
	if (key == 'y' || key == 'Y') {
		editor.quit_after_save = 1;
		editor_save_start();
	} else if (key == 'n' || key == 'N') {
		write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
		write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));
		exit(0);
	} else {
		editor_set_status_message("");
	}
}

/* Cleans up all allocated resources before exiting. Frees every line's
 * data, clears the screen, and frees the filename. Registered with
 * atexit() so it runs automatically on exit. */
void editor_quit(void)
{
	/* Free clipboard and undo history */
	clipboard_clear();
	undo_stack_destroy(&editor.undo);

	/* Free all line data */
	for (int i = 0; i < editor.line_count; i++) {
		line_free(&editor.lines[i]);
	}
	free(editor.lines);

	/* Release mmap resources */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}

	/* Disable mouse reporting and reset the terminal */
	terminal_disable_mouse_reporting();
	write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
	write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));

	/* Free the filename */
	free(editor.filename);

	/* Free syntax highlighting scratch buffers */
	free(editor.scratch_buffer);
	free(editor.scratch_offsets);
}

/*** Help ***/

/* Help text displayed when the user presses F1 or Alt+?. Loaded into
 * the editor buffer as a temporary read-only view. */
static const char *help_text =
	"edit -- Terminal Text Editor (v" EDIT_VERSION ")\n"
	"\n"
	"FILE\n"
	"  Alt+S / Ctrl+S       Save\n"
	"  Alt+Shift+S          Save as\n"
	"  Alt+Q / Ctrl+Q       Quit\n"
	"  Ctrl+Z               Suspend (return to shell)\n"
	"\n"
	"NAVIGATION\n"
	"  Arrows / Alt+HJKL    Move cursor\n"
	"  Home / End           Start / end of line\n"
	"  Ctrl+A / Ctrl+E      Start / end of line\n"
	"  Ctrl+Left/Right      Jump by word\n"
	"  PgUp / PgDn          Scroll by screen\n"
	"  Alt+G / Ctrl+G       Go to line number\n"
	"  Mouse click          Position cursor\n"
	"  Mouse scroll         Scroll (accelerated)\n"
	"\n"
	"SEARCH\n"
	"  Alt+F / Ctrl+F       Find (incremental)\n"
	"                         Arrow keys navigate matches\n"
	"                         Enter to accept, ESC to cancel\n"
	"\n"
	"SELECTION\n"
	"  Shift+Arrow          Select by character/line\n"
	"  Shift+Ctrl+Left/Right  Select by word\n"
	"  Alt+A / Alt+E        Select to line start / end\n"
	"  Mouse drag           Select with mouse\n"
	"  ESC                  Clear selection\n"
	"\n"
	"CLIPBOARD\n"
	"  Alt+C                Copy\n"
	"  Alt+X                Cut\n"
	"  Alt+V                Paste\n"
	"  Alt+Shift+K          Cut entire line\n"
	"  Alt+D                Duplicate line\n"
	"\n"
	"EDITING\n"
	"  Ctrl+U               Undo\n"
	"  Ctrl+R               Redo\n"
	"\n"
	"DISPLAY\n"
	"  Alt+T                Cycle color theme\n"
	"  Alt+N                Toggle line numbers\n"
	"  F1 / Alt+?           This help screen\n"
	"\n"
	"Press ESC or Alt+Q to return to your file.\n";

/* Saves the current buffer state into the snapshot so it can be
 * restored later. Zeroes the buffer fields in editor so the
 * new content won't interfere with the saved data. */
void editor_snapshot_save(void)
{
	editor.snapshot.lines = editor.lines;
	editor.snapshot.line_count = editor.line_count;
	editor.snapshot.line_capacity = editor.line_capacity;
	editor.snapshot.cursor_x = editor.cursor_x;
	editor.snapshot.cursor_y = editor.cursor_y;
	editor.snapshot.row_offset = editor.row_offset;
	editor.snapshot.column_offset = editor.column_offset;
	editor.snapshot.dirty = editor.dirty;
	editor.snapshot.filename = editor.filename;
	editor.snapshot.syntax = editor.syntax;
	editor.snapshot.file_descriptor = editor.file_descriptor;
	editor.snapshot.mmap_base = editor.mmap_base;
	editor.snapshot.mmap_size = editor.mmap_size;

	/* Detach buffer from editor so new content is independent */
	editor.lines = NULL;
	editor.line_count = 0;
	editor.line_capacity = 0;
	editor.filename = NULL;
	editor.syntax = NULL;
	editor.file_descriptor = -1;
	editor.mmap_base = NULL;
	editor.mmap_size = 0;
}

/* Frees the current (temporary) buffer and restores the snapshot. */
void editor_snapshot_restore(void)
{
	/* Free the temporary buffer lines */
	for (int i = 0; i < editor.line_count; i++)
		line_free(&editor.lines[i]);
	free(editor.lines);

	/* Restore from snapshot */
	editor.lines = editor.snapshot.lines;
	editor.line_count = editor.snapshot.line_count;
	editor.line_capacity = editor.snapshot.line_capacity;
	editor.cursor_x = editor.snapshot.cursor_x;
	editor.cursor_y = editor.snapshot.cursor_y;
	editor.row_offset = editor.snapshot.row_offset;
	editor.column_offset = editor.snapshot.column_offset;
	editor.dirty = editor.snapshot.dirty;
	editor.filename = editor.snapshot.filename;
	editor.syntax = editor.snapshot.syntax;
	editor.file_descriptor = editor.snapshot.file_descriptor;
	editor.mmap_base = editor.snapshot.mmap_base;
	editor.mmap_size = editor.snapshot.mmap_size;

	/* Clear snapshot */
	editor.snapshot = (struct editor_snapshot){0};

	editor_update_gutter_width();
}

/* Opens the help screen by saving current state and loading help text. */
void editor_help_open(void)
{
	editor.force_full_redraw = 1;
	if (editor.viewing_help)
		return;

	editor_snapshot_save();

	/* Load help text line by line */
	const char *pos = help_text;
	while (*pos) {
		const char *eol = strchr(pos, '\n');
		if (eol == NULL)
			eol = pos + strlen(pos);
		editor_line_insert(editor.line_count, pos, (size_t)(eol - pos));
		pos = (*eol == '\n') ? eol + 1 : eol;
	}

	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.row_offset = 0;
	editor.column_offset = 0;
	editor.dirty = 0;
	editor.viewing_help = 1;
	editor_update_gutter_width();
	editor_set_status_message("HELP -- Press ESC or Alt+Q to return");
}

/* Closes the help screen and restores the previous buffer. */
void editor_help_close(void)
{
	editor.force_full_redraw = 1;
	if (!editor.viewing_help)
		return;

	editor.viewing_help = 0;
	editor_snapshot_restore();
	editor_set_status_message(
		"Alt: S=save Q=quit F=find G=goto N=lines T=theme F11=help");
}

/*** Find ***/

/* Searches for needle in a byte region of the given length, optionally
 * ignoring ASCII letter case. When case_insensitive is false, delegates
 * to memmem() directly. When true, performs a brute-force byte-by-byte
 * comparison with tolower(). Returns a pointer to the first match, or
 * NULL if not found. */
char *editor_memmem(const char *haystack, size_t haystack_length, const char *needle, size_t needle_length, int case_insensitive)
{
	if (!case_insensitive)
		return memmem(haystack, haystack_length, needle, needle_length);
	if (needle_length == 0)
		return (char *)haystack;
	if (needle_length > haystack_length)
		return NULL;
	size_t limit = haystack_length - needle_length;
	for (size_t i = 0; i <= limit; i++) {
		int match = 1;
		for (size_t j = 0; j < needle_length; j++) {
			if (tolower((unsigned char)haystack[i + j]) !=
			    tolower((unsigned char)needle[j])) {
				match = 0;
				break;
			}
		}
		if (match)
			return (char *)&haystack[i];
	}
	return NULL;
}

/* Callback invoked on each keypress during incremental search. Uses
 * editor.search_* fields to track state instead of static locals, enabling
 * proper interaction with the non-blocking mode system. */
void editor_find_callback(char *query, int key)
{
	editor.force_full_redraw = 1;
	if (editor.search_saved_syntax) {
		struct line *restore_ln = &editor.lines[editor.search_saved_highlight_line];
		line_ensure_warm(restore_ln);
		for (uint32_t k = 0; k < editor.search_saved_syntax_count && k < restore_ln->cell_count; k++)
			restore_ln->cells[k].syntax = editor.search_saved_syntax[k];
		free(editor.search_saved_syntax);
		editor.search_saved_syntax = NULL;
		editor.search_saved_syntax_count = 0;
	}

	if (key == '\r' || key == ESC_KEY) {
		editor.search_last_match = -1;
		editor.search_last_match_offset = -1;
		editor.search_direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		editor.search_direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		editor.search_direction = -1;
	} else {
		editor.search_last_match = -1;
		editor.search_last_match_offset = -1;
		editor.search_direction = 1;
	}

	int current;
	int search_offset;
	if (editor.search_last_match == -1) {
		editor.search_direction = 1;
		current = editor.cursor_y;
		search_offset = -1;
	} else {
		current = editor.search_last_match;
		search_offset = editor.search_last_match_offset;
	}
	int screen_middle = editor.screen_rows / 2;
	size_t query_len = strlen(query);

	for (int i = 0; i < editor.line_count; i++) {
		if (i == 0 && current != -1) {
			/* First iteration: try to find another match on the
			 * same line before moving to the next one. */
		} else {
			current += editor.search_direction;
			search_offset = -1;
		}
		if (current == -1)
			current = editor.line_count - 1;
		else if (current == editor.line_count)
			current = 0;

		struct line *ln = &editor.lines[current];

		/* For COLD lines with a valid mmap, search directly in the
		 * mapped bytes to avoid allocating and freeing a buffer per
		 * line. This makes search on large unedited files essentially
		 * zero-allocation. Uses memmem() instead of strstr() because
		 * the mmap region is not null-terminated per line. */
		size_t byte_len;
		const char *render;
		int allocated = 0;
		if (ln->temperature == LINE_COLD && editor.mmap_base) {
			render = editor.mmap_base + ln->mmap_offset;
			byte_len = ln->mmap_length;
		} else {
			line_ensure_warm(ln);
			render = line_to_bytes(ln, &byte_len);
			allocated = 1;
		}
		char *match = NULL;
		int case_flag = editor.search_case_insensitive;

		if (editor.search_direction == 1) {
			/* Forward: search after the previous match offset */
			int start = (search_offset >= 0) ? search_offset + 1 : 0;
			if (start < (int)byte_len)
				match = editor_memmem(render + start, byte_len - start, query, query_len, case_flag);
		} else {
			/* Backward: find the last match before the offset */
			int limit = (search_offset >= 0) ? search_offset : (int)byte_len;
			const char *candidate = render;
			while (candidate < render + limit) {
				char *found = editor_memmem(candidate, (size_t)(render + limit - candidate), query, query_len, case_flag);
				if (!found || found >= render + limit)
					break;
				match = found;
				candidate = found + 1;
			}
		}

		if (match) {
			int match_byte_offset = (int)(match - render);
			editor.search_last_match = current;
			editor.search_last_match_offset = match_byte_offset;
			editor.cursor_y = current;

			/* Warm the line now that we need cell-level access
			 * for cursor positioning and highlight painting. */
			if (allocated)
				free((char *)render);
			allocated = 0;
			line_ensure_warm(ln);
			char *warm_render = line_to_bytes(ln, &byte_len);

			/* Convert byte offset to cell index by counting
			 * codepoints in the bytes before the match. */
			int cell_index = 0;
			size_t byte_pos = 0;
			while ((int)byte_pos < match_byte_offset && (uint32_t)cell_index < ln->cell_count) {
				uint32_t cp;
				int consumed = utf8_decode(&warm_render[byte_pos], (int)(byte_len - byte_pos), &cp);
				if (consumed <= 0) consumed = 1;
				byte_pos += consumed;
				cell_index++;
			}
			editor.cursor_x = cell_index;

			/* Count codepoints in query for highlight range */
			int query_cell_count = 0;
			size_t qpos = 0;
			while (qpos < query_len) {
				uint32_t cp;
				int consumed = utf8_decode(&query[qpos], (int)(query_len - qpos), &cp);
				if (consumed <= 0) consumed = 1;
				qpos += consumed;
				query_cell_count++;
			}

			/* Center the match on the screen */
			editor.row_offset = editor.cursor_y - screen_middle;
			if (editor.row_offset < 0)
				editor.row_offset = 0;
			int max_offset = editor.line_count - editor.screen_rows;
			if (max_offset < 0)
				max_offset = 0;
			if (editor.row_offset > max_offset)
				editor.row_offset = max_offset;

			/* Save current syntax and highlight match */
			editor.search_saved_highlight_line = current;
			editor.search_saved_syntax_count = ln->cell_count;
			editor.search_saved_syntax = malloc(sizeof(uint16_t) * ln->cell_count);
			if (editor.search_saved_syntax == NULL) {
				free(warm_render);
				break;
			}
			for (uint32_t k = 0; k < ln->cell_count; k++)
				editor.search_saved_syntax[k] = ln->cells[k].syntax;
			for (int k = 0; k < query_cell_count && cell_index + k < (int)ln->cell_count; k++)
				ln->cells[cell_index + k].syntax = HL_MATCH;

			free(warm_render);
			break;
		}
		if (allocated)
			free((char *)render);
	}
}

/* Called when the user accepts a search result (Enter). Frees the query
 * string and leaves the cursor at the match position. */
void editor_find_accept(char *query)
{
	free(query);
}

/* Called when the user cancels a search (ESC). Restores the cursor and
 * viewport to the position saved before the search started. */
void editor_find_cancel(void)
{
	editor.force_full_redraw = 1;
	editor.cursor_x = editor.saved_cursor_x;
	editor.cursor_y = editor.saved_cursor_y;
	editor.column_offset = editor.saved_column_offset;
	editor.row_offset = editor.saved_row_offset;
}

/* Starts an incremental search. Saves cursor/viewport state and opens
 * the search prompt. */
void editor_find_start(void)
{
	editor.saved_cursor_x = editor.cursor_x;
	editor.saved_cursor_y = editor.cursor_y;
	editor.saved_column_offset = editor.column_offset;
	editor.saved_row_offset = editor.row_offset;
	editor.search_last_match = -1;
	editor.search_last_match_offset = -1;
	editor.search_direction = 1;
	prompt_open("Search: %s (Use ESC/Arrows/Enter)",
		    editor_find_callback, editor_find_accept, editor_find_cancel);
}

/* Called when the user accepts a jump-to-line input. Parses the line
 * number, validates it, and moves the cursor. */
void editor_jump_to_line_accept(char *input)
{
	int line = atoi(input);
	free(input);

	if (line > 0 && line <= editor.line_count) {
		editor.cursor_y = line - 1;
		editor.cursor_x = 0;

		/* Calculate the new row offset to center the jumped-to line */
		int new_row_offset = editor.cursor_y - (editor.screen_rows / 2);
		if (new_row_offset < 0)
			new_row_offset = 0;
		int max_offset = editor.line_count - editor.screen_rows;
		if (max_offset < 0)
			max_offset = 0;
		if (new_row_offset > max_offset)
			new_row_offset = max_offset;
		editor.row_offset = new_row_offset;

		/* Ensure the cursor is visible horizontally */
		editor.column_offset = 0;
	} else {
		editor_set_status_message("Invalid line number");
	}
}

/* Opens the jump-to-line prompt. */
void editor_jump_to_line_start(void)
{
	prompt_open("Jump to line: %s", NULL, editor_jump_to_line_accept, NULL);
}

/*** Append buffer ***/

/* Dynamically growing string buffer used to build screen output
 * before writing it to the terminal in a single write() call. */
struct append_buffer {
	/* Heap-allocated character data. */
	char *buffer;
	/* Current number of bytes stored in buffer. */
	int length;
	/* Allocated capacity of the buffer. */
	int capacity;
};

/* Initializer for an empty append_buffer. */
#define APPEND_BUFFER_INIT {NULL, 0, 0}

/* Appends a string of the given length to the append buffer. Uses a
 * capacity-doubling strategy to avoid realloc on every call. */
void append_buffer_write(struct append_buffer *append_buffer, const char *string, int length)
{
	int needed = append_buffer->length + length;
	if (needed > append_buffer->capacity) {
		int new_capacity = append_buffer->capacity ? append_buffer->capacity : 1024;
		while (new_capacity < needed)
			new_capacity *= 2;
		char *new_buffer = realloc(append_buffer->buffer, new_capacity);
		if (new_buffer == NULL)
			terminal_die("realloc");
		append_buffer->buffer = new_buffer;
		append_buffer->capacity = new_capacity;
	}
	memcpy(&append_buffer->buffer[append_buffer->length], string, length);
	append_buffer->length += length;
}

/* Frees the memory used by the append buffer's character data. */
void append_buffer_free(struct append_buffer *append_buffer)
{
	free(append_buffer->buffer);
}

/* Pre-parsed RGB color value. Avoids repeated sscanf() during rendering. */
struct rgb {
	uint8_t r, g, b;
};

/* Parses a hex color string like "FF00FF" into an rgb struct. */
struct rgb rgb_parse(const char *hex)
{
	unsigned int red, green, blue;
	sscanf(hex, "%02x%02x%02x", &red, &green, &blue);
	return (struct rgb){ .r = (uint8_t)red, .g = (uint8_t)green, .b = (uint8_t)blue };
}

/* Writes a 24-bit foreground color escape sequence to the append buffer.
 * Caches the last parsed hex color to avoid redundant sscanf() calls
 * when the same color pointer is used consecutively. */
void append_buffer_write_color(struct append_buffer *append_buffer, const char *hex_color)
{
	static const char *last_hex = NULL;
	static struct rgb last_rgb;
	if (hex_color != last_hex) {
		last_rgb = rgb_parse(hex_color);
		last_hex = hex_color;
	}
	char color_sequence[COLOR_SEQUENCE_SIZE];
	snprintf(color_sequence, sizeof(color_sequence), COLOR_FG_FORMAT,
		 last_rgb.r, last_rgb.g, last_rgb.b);
	append_buffer_write(append_buffer, color_sequence, strlen(color_sequence));
}

/* Writes a 24-bit background color escape sequence to the append buffer.
 * Caches the last parsed hex color to skip sscanf() on repeated calls
 * with the same color pointer. */
void append_buffer_write_background(struct append_buffer *append_buffer, const char *hex_color)
{
	static const char *last_hex = NULL;
	static struct rgb last_rgb;
	if (hex_color != last_hex) {
		last_rgb = rgb_parse(hex_color);
		last_hex = hex_color;
	}
	char color_sequence[COLOR_SEQUENCE_SIZE];
	snprintf(color_sequence, sizeof(color_sequence), COLOR_BG_FORMAT,
		 last_rgb.r, last_rgb.g, last_rgb.b);
	append_buffer_write(append_buffer, color_sequence, strlen(color_sequence));
}

/*** Output ***/

/* Adjusts the viewport scroll offsets so the cursor stays visible on screen.
 * Updates render_x from the cursor position, then shifts row_offset and
 * column_offset as needed to keep the cursor within the visible area. */
void editor_scroll(void)
{
	editor.render_x = 0;
	if (editor.cursor_y < editor.line_count) {
		editor.render_x = line_cell_to_render_column(
				&editor.lines[editor.cursor_y], editor.cursor_x);
	}
	int margin = editor.suppress_scroll_margin ? 0 : SCROLL_MARGIN;
	editor.suppress_scroll_margin = 0;
	if (margin > editor.screen_rows / 2)
		margin = editor.screen_rows / 2;

	if (editor.cursor_y < editor.row_offset + margin) {
		editor.row_offset = editor.cursor_y - margin;
		if (editor.row_offset < 0)
			editor.row_offset = 0;
	}
	if (editor.cursor_y >= editor.row_offset + editor.screen_rows - margin) {
		editor.row_offset = editor.cursor_y - editor.screen_rows + margin + 1;
	}
	if (editor.render_x < editor.column_offset) {
		editor.column_offset = editor.render_x;
	}
	int text_columns = editor.screen_columns - editor.line_number_width;
	if (editor.render_x >= editor.column_offset + text_columns) {
		editor.column_offset = editor.render_x - text_columns + 1;
	}
}

/* Scrolls the editor view by the given number of rows in the specified
 * direction (ARROW_UP or ARROW_DOWN). Moves the viewport and clamps
 * the cursor to remain visible. Used by mouse scroll wheel with
 * acceleration. */
void editor_scroll_rows(int scroll_direction, int scroll_amount)
{
	if (scroll_direction == ARROW_UP)
		editor.row_offset -= scroll_amount;
	else
		editor.row_offset += scroll_amount;

	/* Clamp viewport to valid range */
	int max_offset = editor.line_count - editor.screen_rows;
	if (max_offset < 0)
		max_offset = 0;
	if (editor.row_offset < 0)
		editor.row_offset = 0;
	if (editor.row_offset > max_offset)
		editor.row_offset = max_offset;

	/* Clamp cursor to visible area, honoring the scroll margin so the
	 * cursor lands at the margin boundary rather than the absolute edge. */
	int margin = SCROLL_MARGIN;
	if (margin > editor.screen_rows / 2)
		margin = editor.screen_rows / 2;
	int top_bound = editor.row_offset + margin;
	int bottom_bound = editor.row_offset + editor.screen_rows - 1 - margin;
	if (editor.cursor_y < top_bound && editor.cursor_y < editor.row_offset)
		editor.cursor_y = (top_bound < editor.line_count)
			? top_bound : editor.row_offset;
	if (editor.cursor_y > bottom_bound
	    && editor.cursor_y >= editor.row_offset + editor.screen_rows)
		editor.cursor_y = bottom_bound;

	/* Clamp cursor to file bounds */
	if (editor.cursor_y >= editor.line_count)
		editor.cursor_y = editor.line_count > 0
			? editor.line_count - 1 : 0;

	/* Clamp cursor_x to the new line's length */
	if (editor.cursor_y < editor.line_count) {
		line_ensure_warm(&editor.lines[editor.cursor_y]);
		int row_length = (int)editor.lines[editor.cursor_y].cell_count;
		if (editor.cursor_x > row_length)
			editor.cursor_x = row_length;
	}
}

/* Updates the scroll speed based on the time between consecutive scroll
 * events. Quick successive scrolls increase speed up to SCROLL_SPEED_MAX;
 * a pause resets it back to 1. */
void editor_update_scroll_speed(void)
{
	struct timeval current_time;
	gettimeofday(&current_time, NULL);
	long time_diff =
			(current_time.tv_sec - editor.last_scroll_time.tv_sec) * MICROSECONDS_PER_SECOND +
			(current_time.tv_usec - editor.last_scroll_time.tv_usec);
	if (time_diff < SCROLL_ACCELERATION_FAST_US) {
		editor.scroll_speed = editor.scroll_speed < SCROLL_SPEED_MAX
															? editor.scroll_speed + 1
															: SCROLL_SPEED_MAX;
	} else if (time_diff > SCROLL_DECELERATION_SLOW_US) {
		editor.scroll_speed = 1;
	}
	editor.last_scroll_time = current_time;
}

/* Renders all visible rows to the append buffer, including line numbers,
 * syntax-highlighted text, and the welcome message for empty files. Applies
 * background colors, bold formatting for keywords, and handles control
 * characters with inverted display. */
void editor_draw_rows(struct append_buffer *append_buffer)
{
	int screen_row;
	for (screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
		int file_row = screen_row + editor.row_offset;

		/* Set background color for the entire line */
		append_buffer_write_background(
				append_buffer,
				editor.theme.background);

		if (file_row >= editor.line_count) {
			if (editor.line_number_width > 0) {
				int tilde_padding = editor.line_number_width - 2;
				while (tilde_padding-- > 0)
					append_buffer_write(append_buffer, " ", 1);
			}
			append_buffer_write(append_buffer, "~", 1);
		} else {
			if (file_row == editor.cursor_y) {
				append_buffer_write_background(
						append_buffer,
						editor.theme
								.highlight_background);
			}
			/* Line numbers */
			if (editor.line_number_width > 0) {
				append_buffer_write_color(
						append_buffer,
						editor.theme.line_number);
				char line_number_string[LINE_NUMBER_BUFFER_SIZE];
				snprintf(line_number_string, sizeof(line_number_string),
							 "%*d ", editor.line_number_width - 1, file_row + 1);
				append_buffer_write(append_buffer, line_number_string, strlen(line_number_string));
				append_buffer_write_color(
						append_buffer, editor.theme.foreground);
			}

			struct line *ln = &editor.lines[file_row];
			line_ensure_warm(ln);
			int render_width = line_render_width(ln);
			int visible_length = render_width - editor.column_offset;
			if (visible_length < 0)
				visible_length = 0;
			int text_columns = editor.screen_columns - editor.line_number_width;
			if (visible_length > text_columns)
				visible_length = text_columns;
			/* Render cells with tab expansion and UTF-8 encoding.
			 * Iterates by grapheme cluster to correctly handle
			 * multi-codepoint sequences (flags, ZWJ emoji, etc). */
			char *current_color = NULL;
			int col = 0;
			int output_col = 0;
			uint32_t ci = 0;
			while (ci < ln->cell_count && output_col < visible_length) {
				uint32_t cp = ln->cells[ci].codepoint;
				uint16_t hl = ln->cells[ci].syntax;
				if (cp == '\t') {
					int cw = cell_display_width(&ln->cells[ci], col);
					/* Expand tab to spaces */
					for (int t = 0; t < cw && output_col < visible_length; t++) {
						if (col >= editor.column_offset) {
							if (hl == HL_NORMAL) {
								if (current_color != NULL) {
									append_buffer_write_color(append_buffer, editor.theme.foreground);
									current_color = NULL;
								}
							} else {
								char *color = syntax_to_color(hl);
								if (color != current_color) {
									current_color = color;
									append_buffer_write_color(append_buffer, color);
								}
							}
							append_buffer_write(append_buffer, " ", 1);
							output_col++;
						}
						col++;
					}
					ci++;
					continue;
				}
				/* Find the end of this grapheme cluster */
				int grapheme_end = cursor_next_grapheme(ln, (int)ci);
				int cw = grapheme_display_width(ln, (int)ci, grapheme_end);
				if (col >= editor.column_offset) {
					if (cp < 0x20) {
						/* Control character: render as inverted symbol */
						char symbol = (cp <= CONTROL_CHAR_MAX) ? '@' + (char)cp : '?';
						append_buffer_write(append_buffer, INVERT_COLOR, strlen(INVERT_COLOR));
						append_buffer_write(append_buffer, &symbol, 1);
						append_buffer_write(append_buffer, RESET_ALL_ATTRIBUTES, strlen(RESET_ALL_ATTRIBUTES));
						if (current_color != NULL) {
							append_buffer_write_color(append_buffer, current_color);
						}
					} else {
						/* Selection: use terminal reverse video */
						int selected = ln->cells[ci].flags & CELL_FLAG_SELECTED;
						if (selected) {
							append_buffer_write(append_buffer,
								INVERT_COLOR, strlen(INVERT_COLOR));
						} else if (hl == HL_NORMAL) {
							if (current_color != NULL) {
								append_buffer_write_color(
										append_buffer,
										editor.theme.foreground);
								current_color = NULL;
							}
						} else {
							char *color = syntax_to_color(hl);
							if (color != current_color) {
								current_color = color;
								append_buffer_write_color(append_buffer, color);
							}
						}
						/* Output all codepoints in the cluster */
						for (int gi = (int)ci; gi < grapheme_end; gi++) {
							char utf8_buf[UTF8_MAX_BYTES];
							int utf8_len = utf8_encode(
									ln->cells[gi].codepoint, utf8_buf);
							append_buffer_write(append_buffer, utf8_buf, utf8_len);
						}
						/* End reverse video after selected cells */
						if (selected) {
							append_buffer_write(append_buffer,
								RESET_ALL_ATTRIBUTES, strlen(RESET_ALL_ATTRIBUTES));
							/* Re-establish line background and reset color tracking */
							if (file_row == editor.cursor_y)
								append_buffer_write_background(append_buffer,
									editor.theme.highlight_background);
							else
								append_buffer_write_background(append_buffer,
									editor.theme.background);
							current_color = NULL;
						}
					}
					output_col += cw;
				}
				col += cw;
				ci = (uint32_t)grapheme_end;
			}
			append_buffer_write_color(
					append_buffer, editor.theme.foreground);
		}
		append_buffer_write(append_buffer, CLEAR_LINE, strlen(CLEAR_LINE));
		append_buffer_write(append_buffer, CRLF, strlen(CRLF));
	}
}

/* Renders the status bar showing the filename, line count, modification
 * status, cursor position, and file type. Uses the theme's status bar
 * colors and right-aligns the secondary information. */
void editor_draw_status_bar(struct append_buffer *append_buffer)
{
	append_buffer_write_background(
			append_buffer,
			editor.theme.status_bar);
	append_buffer_write_color(
			append_buffer,
			editor.theme.status_bar_text);
	char status[STATUS_BUFFER_SIZE], right_status[STATUS_BUFFER_SIZE];
	int status_length = snprintf(status, sizeof(status), "%.20s%s",
												editor.filename ? editor.filename : "[No Name]",
												editor.dirty ? " [+]" : "");
	int right_status_length = snprintf(
			right_status, sizeof(right_status),
			"%d:%d/%d",
			editor.cursor_y + 1, editor.cursor_x + 1,
			editor.line_count);
	if (status_length > editor.screen_columns)
		status_length = editor.screen_columns;
	append_buffer_write(append_buffer, status, status_length);
	while (status_length < editor.screen_columns) {
		if (editor.screen_columns - status_length == right_status_length) {
			append_buffer_write_color(append_buffer, editor.theme.status_bar_text);
			append_buffer_write(append_buffer, right_status, right_status_length);
			break;
		} else {
			append_buffer_write(append_buffer, " ", 1);
			status_length++;
		}
	}
	append_buffer_write_color(append_buffer, editor.theme.foreground);
	append_buffer_write_background(append_buffer, editor.theme.background);
	append_buffer_write(append_buffer, CRLF, strlen(CRLF));
}

/* Renders the message bar below the status bar. Shows the current status
 * message if it was set within the last STATUS_MESSAGE_TIMEOUT_SECONDS. */
void editor_draw_message_bar(struct append_buffer *append_buffer)
{
	append_buffer_write(append_buffer, CLEAR_LINE, strlen(CLEAR_LINE));
	int message_length = strlen(editor.status_message);
	if (message_length > editor.screen_columns)
		message_length = editor.screen_columns;
	if (message_length && time(NULL) - editor.status_message_time <
										STATUS_MESSAGE_TIMEOUT_SECONDS) {
		append_buffer_write_color(append_buffer, editor.theme.message_bar);
		append_buffer_write(append_buffer, editor.status_message, message_length);
		append_buffer_write_color(append_buffer, editor.theme.foreground);
	}
}

/* Redraws the screen. When only the cursor moved since the last frame,
 * emits just a cursor reposition escape (~12 bytes) instead of redrawing
 * every visible line (~20KB). Falls through to a full redraw when content,
 * viewport, status bar, or selection state changed. */
void editor_refresh_screen(void)
{
	editor_scroll();

	/* Detect whether a full redraw is needed by comparing the current
	 * frame state against the previous frame's snapshot. Pure cursor
	 * movement (the most common operation) skips the expensive draw. */
	int message_visible = (editor.status_message[0] != '\0'
		&& time(NULL) - editor.status_message_time < STATUS_MESSAGE_TIMEOUT_SECONDS);
	int prev_message_visible = (editor.prev_status_message[0] != '\0');
	int needs_full_redraw = editor.force_full_redraw
		|| editor.cursor_y != editor.prev_cursor_y
		|| editor.row_offset != editor.prev_row_offset
		|| editor.column_offset != editor.prev_column_offset
		|| editor.line_count != editor.prev_line_count
		|| editor.dirty != editor.prev_dirty
		|| editor.selection.active
		|| strcmp(editor.status_message, editor.prev_status_message) != 0
		|| message_visible != prev_message_visible;

	if (!needs_full_redraw) {
		/* Cursor-only fast path: skip the expensive row drawing but
		 * still update the status bar (it shows cursor position)
		 * and reposition the cursor. */
		struct append_buffer output_buffer = APPEND_BUFFER_INIT;
		append_buffer_write(&output_buffer, HIDE_CURSOR, strlen(HIDE_CURSOR));

		/* Move to the status bar row and redraw it */
		char move_buffer[CURSOR_BUFFER_SIZE];
		snprintf(move_buffer, sizeof(move_buffer), CURSOR_MOVE,
			 editor.screen_rows + 1, 1);
		append_buffer_write(&output_buffer, move_buffer, strlen(move_buffer));
		editor_draw_status_bar(&output_buffer);
		editor_draw_message_bar(&output_buffer);

		/* Position the cursor at the correct editing location */
		char cursor_buffer[CURSOR_BUFFER_SIZE];
		snprintf(cursor_buffer, sizeof(cursor_buffer), CURSOR_MOVE,
			 (editor.cursor_y - editor.row_offset) + 1,
			 (editor.render_x - editor.column_offset) + editor.line_number_width + 1);
		append_buffer_write(&output_buffer, cursor_buffer, strlen(cursor_buffer));
		append_buffer_write(&output_buffer, SHOW_CURSOR, strlen(SHOW_CURSOR));

		write(STDOUT_FILENO, output_buffer.buffer, output_buffer.length);
		append_buffer_free(&output_buffer);

		/* Update previous-frame cursor state */
		editor.prev_cursor_x = editor.cursor_x;
		editor.prev_cursor_y = editor.cursor_y;
		editor.prev_render_x = editor.render_x;
		return;
	}

	/* Full redraw path */
	struct append_buffer output_buffer = APPEND_BUFFER_INIT;

	append_buffer_write(&output_buffer, HIDE_CURSOR, strlen(HIDE_CURSOR));
	append_buffer_write(&output_buffer, CURSOR_HOME, strlen(CURSOR_HOME));

	editor_draw_rows(&output_buffer);
	editor_draw_status_bar(&output_buffer);
	editor_draw_message_bar(&output_buffer);

	char cursor_buffer[CURSOR_BUFFER_SIZE];
	snprintf(cursor_buffer, sizeof(cursor_buffer), CURSOR_MOVE,
					 (editor.cursor_y - editor.row_offset) + 1,
					 (editor.render_x - editor.column_offset) + editor.line_number_width + 1);
	append_buffer_write(&output_buffer, cursor_buffer, strlen(cursor_buffer));

	append_buffer_write(&output_buffer, SHOW_CURSOR, strlen(SHOW_CURSOR));

	write(STDOUT_FILENO, output_buffer.buffer, output_buffer.length);
	append_buffer_free(&output_buffer);

	/* Snapshot the current frame state for next-frame comparison.
	 * For the status message, store what was actually drawn: if the
	 * message timed out, record an empty string so the next frame
	 * comparison reflects the visible state, not the stale text. */
	editor.prev_cursor_x = editor.cursor_x;
	editor.prev_cursor_y = editor.cursor_y;
	editor.prev_render_x = editor.render_x;
	editor.prev_row_offset = editor.row_offset;
	editor.prev_column_offset = editor.column_offset;
	editor.prev_line_count = editor.line_count;
	editor.prev_dirty = editor.dirty;
	if (message_visible) {
		strncpy(editor.prev_status_message, editor.status_message,
			STATUS_MESSAGE_SIZE);
		editor.prev_status_message[STATUS_MESSAGE_SIZE - 1] = '\0';
	} else {
		editor.prev_status_message[0] = '\0';
	}
	editor.force_full_redraw = 0;
}

/* Sets the status bar message using printf-style formatting. Records the
 * current time so the message can be auto-cleared after a timeout. */
void editor_set_status_message(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(editor.status_message, sizeof(editor.status_message), format, args);
	va_end(args);
	editor.status_message_time = time(NULL);
}

/*** Input ***/

/* Opens a prompt in the status bar. Sets mode to MODE_PROMPT and stores
 * the callbacks for per-key updates, accept, and cancel actions. */
void prompt_open(char *format, void (*per_key_callback)(char *, int),
		 void (*on_accept)(char *), void (*on_cancel)(void))
{
	editor.mode = MODE_PROMPT;
	editor.prompt.format = format;
	editor.prompt.buffer_capacity = PROMPT_INITIAL_SIZE;
	editor.prompt.buffer = malloc(editor.prompt.buffer_capacity);
	if (editor.prompt.buffer == NULL)
		terminal_die("malloc");
	editor.prompt.buffer[0] = '\0';
	editor.prompt.buffer_length = 0;
	editor.prompt.per_key_callback = per_key_callback;
	editor.prompt.on_accept = on_accept;
	editor.prompt.on_cancel = on_cancel;
	editor_set_status_message(format, "");
}

/* Handles a keypress while in MODE_PROMPT. Processes backspace, ESC,
 * Enter, and printable characters. Calls per_key_callback after each
 * keypress, and on_accept/on_cancel on Enter/ESC. */
void prompt_handle_key(struct input_event event)
{
	int key = event.key;
	/* Alt+C during search toggles case sensitivity. Only active when
	 * the per-key callback is the incremental search handler. */
	if (key == ALT_KEY('c') && editor.prompt.per_key_callback == editor_find_callback) {
		editor.search_case_insensitive = !editor.search_case_insensitive;
		editor_set_status_message("Search: %s [case %s]",
					 editor.prompt.buffer,
					 editor.search_case_insensitive ? "off" : "on");
		/* Re-run the search with the new case setting */
		if (editor.prompt.per_key_callback)
			editor.prompt.per_key_callback(editor.prompt.buffer, key);
		return;
	}
	if (key == DEL_KEY || key == CTRL_KEY('h') || key == BACKSPACE) {
		if (editor.prompt.buffer_length != 0)
			editor.prompt.buffer[--editor.prompt.buffer_length] = '\0';
	} else if (key == ESC_KEY) {
		editor_set_status_message("");
		if (editor.prompt.per_key_callback)
			editor.prompt.per_key_callback(editor.prompt.buffer, key);
		void (*on_cancel)(void) = editor.prompt.on_cancel;
		free(editor.prompt.buffer);
		editor.prompt.buffer = NULL;
		editor.mode = MODE_NORMAL;
		if (on_cancel)
			on_cancel();
		return;
	} else if (key == '\r') {
		if (editor.prompt.buffer_length != 0) {
			editor_set_status_message("");
			if (editor.prompt.per_key_callback)
				editor.prompt.per_key_callback(editor.prompt.buffer, key);
			/* on_accept takes ownership of the buffer */
			char *buffer = editor.prompt.buffer;
			void (*on_accept)(char *) = editor.prompt.on_accept;
			editor.prompt.buffer = NULL;
			editor.mode = MODE_NORMAL;
			if (on_accept)
				on_accept(buffer);
			return;
		}
	} else if (key > 0 && key < ARROW_LEFT && (key >= 128 || !iscntrl(key))) {
		char utf8_buf[UTF8_MAX_BYTES];
		int byte_count;
		if (key < ASCII_MAX) {
			utf8_buf[0] = (char)key;
			byte_count = 1;
		} else {
			byte_count = utf8_encode((uint32_t)key, utf8_buf);
		}
		while (editor.prompt.buffer_length + byte_count >= editor.prompt.buffer_capacity - 1) {
			editor.prompt.buffer_capacity *= 2;
			editor.prompt.buffer = realloc(editor.prompt.buffer,
						       editor.prompt.buffer_capacity);
			if (editor.prompt.buffer == NULL)
				terminal_die("realloc");
		}
		memcpy(editor.prompt.buffer + editor.prompt.buffer_length,
		       utf8_buf, byte_count);
		editor.prompt.buffer_length += byte_count;
		editor.prompt.buffer[editor.prompt.buffer_length] = '\0';
	}

	if (editor.prompt.per_key_callback)
		editor.prompt.per_key_callback(editor.prompt.buffer, key);
	editor_set_status_message(editor.prompt.format, editor.prompt.buffer);
}

/* Closes the current prompt and returns to MODE_NORMAL. */
void prompt_close(void)
{
	editor.mode = MODE_NORMAL;
	editor_set_status_message("");
}

/* Opens a single-key confirmation dialog in the status bar. */
void confirm_open(const char *message, void (*callback)(int key))
{
	editor.mode = MODE_CONFIRM;
	editor.confirm_callback = callback;
	editor_set_status_message("%s", message);
}

/* Handles a keypress while in MODE_CONFIRM. Calls the stored callback
 * and returns to MODE_NORMAL. */
void editor_handle_confirm(struct input_event event)
{
	editor.mode = MODE_NORMAL;
	if (editor.confirm_callback)
		editor.confirm_callback(event.key);
}

/* Moves the cursor in the direction indicated by the event parameter. Handles
 * arrow keys, mouse clicks, and line wrapping (moving past the end of a line
 * wraps to the next, and past the start wraps to the previous). Clamps the
 * cursor to the current line length after vertical movement. */
void editor_move_cursor(struct input_event event)
{
	struct line *current_line = (editor.cursor_y >= editor.line_count)
												? NULL
												: &editor.lines[editor.cursor_y];

	/* Reset preferred column on any horizontal movement */
	int is_vertical = (event.key == ARROW_UP || event.key == ARROW_DOWN
			   || event.key == ALT_KEY('j')
			   || event.key == ALT_KEY('k')
			   || event.key == PAGE_UP || event.key == PAGE_DOWN
			   || event.key == MOUSE_SCROLL_UP
			   || event.key == MOUSE_SCROLL_DOWN);
	if (!is_vertical)
		editor.preferred_column = -1;

	switch (event.key) {

	case MOUSE_LEFT_BUTTON_PRESSED:
		if (event.mouse_y >= 0 && event.mouse_y < editor.screen_rows) {
			int file_row = event.mouse_y + editor.row_offset;
			if (file_row < editor.line_count) {
				editor.cursor_y = file_row;
				struct line *click_line = &editor.lines[editor.cursor_y];
				int render_col = event.mouse_x + editor.column_offset;
				editor.cursor_x = line_render_column_to_cell(click_line, render_col);
			}
		}
		break;

	case ALT_KEY('h'):
	case ARROW_LEFT:
		if (current_line == NULL)
			break;
		if (editor.cursor_x != 0) {
			editor.cursor_x = cursor_prev_grapheme(current_line, editor.cursor_x);
		} else if (editor.cursor_y > 0) {
			editor.cursor_y--;
			editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
		}
		break;

	case ALT_KEY('l'):
	case ARROW_RIGHT:
		if (current_line && editor.cursor_x < (int)current_line->cell_count) {
			editor.cursor_x = cursor_next_grapheme(current_line, editor.cursor_x);
		} else if (current_line && editor.cursor_x == (int)current_line->cell_count) {
			if (editor.cursor_y < editor.line_count - 1) {
				editor.cursor_y++;
				editor.cursor_x = 0;
			}
		}
		break;

	case ALT_KEY('k'):
	case ARROW_UP:
		if (editor.cursor_y != 0) {
			if (editor.preferred_column == -1 && current_line)
				editor.preferred_column =
					line_cell_to_render_column(
						current_line, editor.cursor_x);
			editor.cursor_y--;
		}
		break;

	case ALT_KEY('j'):
	case ARROW_DOWN:
		if (editor.cursor_y < editor.line_count) {
			if (editor.preferred_column == -1 && current_line)
				editor.preferred_column =
					line_cell_to_render_column(
						current_line, editor.cursor_x);
			editor.cursor_y++;
		}
		break;
	}

	current_line = (editor.cursor_y >= editor.line_count) ? NULL
																: &editor.lines[editor.cursor_y];
	int row_length = current_line ? (int)current_line->cell_count : 0;

	if (editor.preferred_column >= 0 && current_line) {
		editor.cursor_x = line_render_column_to_cell(
			current_line, editor.preferred_column);
		if (editor.cursor_x > row_length)
			editor.cursor_x = row_length;
	} else if (editor.cursor_x > row_length) {
		editor.cursor_x = row_length;
	}

	/* Snap cursor to grapheme cluster boundary after vertical movement.
	 * If cursor_x lands in the middle of a multi-codepoint grapheme
	 * (e.g., a flag emoji spanning cells 2-3), snap to the cluster start. */
	if (current_line && editor.cursor_x > 0
			&& editor.cursor_x < row_length) {
		int prev = cursor_prev_grapheme(current_line, editor.cursor_x);
		int next = cursor_next_grapheme(current_line, prev);
		if (next > editor.cursor_x) {
			editor.cursor_x = prev;
		}
	}
}

/* Main input handler for MODE_NORMAL. Dispatches the input event to the
 * appropriate action: text insertion, cursor movement, file operations,
 * search, or quit. Event is passed in from the main loop. */
void editor_process_keypress(struct input_event event)
{
	int key = event.key;

	/* When viewing help, only allow navigation and exit keys.
	 * Everything else is blocked to keep the buffer read-only. */
	if (editor.viewing_help) {
		int allowed = 0;
		switch (key) {
		/* Navigation */
		case ARROW_UP: case ARROW_DOWN: case ARROW_LEFT: case ARROW_RIGHT:
		case ALT_KEY('h'): case ALT_KEY('j'): case ALT_KEY('k'): case ALT_KEY('l'):
		case PAGE_UP: case PAGE_DOWN: case HOME_KEY: case END_KEY:
		case CTRL_ARROW_LEFT: case CTRL_ARROW_RIGHT:
		case MOUSE_LEFT_BUTTON_PRESSED:
		case MOUSE_SCROLL_UP: case MOUSE_SCROLL_DOWN:
		/* Exit help */
		case ESC_KEY: case ALT_KEY('q'): case CTRL_KEY('q'):
		/* Search (read-only browsing) */
		case ALT_KEY('f'): case CTRL_KEY('f'):
		/* Display toggles */
		case ALT_KEY('t'): case ALT_KEY('n'):
		/* Select to start/end of line */
		case ALT_KEY('a'): case ALT_KEY('e'):
		/* Goto line */
		case ALT_KEY('g'): case CTRL_KEY('g'):
			allowed = 1;
			break;
		}
		if (!allowed) {
			editor_set_status_message(
				"Help is read-only -- ESC or Alt+Q to return");
			return;
		}
	}

	switch (key) {
	case '\r':
		if (editor.selection.active)
			selection_delete();
		editor_insert_newline();
		break;

	case ALT_KEY('t'):
		editor_switch_theme();
		break;
	case ALT_KEY('n'):
		editor_toggle_line_numbers();
		break;

	case ALT_KEY('g'):
	case CTRL_KEY('g'):
		editor_jump_to_line_start();
		break;

	case ALT_KEY('q'):
	case CTRL_KEY('q'):
		if (editor.viewing_help) {
			editor_help_close();
			break;
		}
		if (editor.dirty) {
			confirm_open("Unsaved changes. Save before quitting? (y/n/ESC)",
				     editor_quit_confirm);
		} else {
			write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
			write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));
			exit(0);
		}
		break;

	case ALT_KEY('s'):
	case CTRL_KEY('s'):
		editor_save_start();
		break;
	case ALT_KEY('S'):
		editor_save_as_start();
		break;

	case CTRL_KEY('z'):
		/* Suspend: restore terminal, stop the process, re-enable
		 * raw mode when resumed. */
		terminal_disable_mouse_reporting();
		terminal_disable_raw_mode();
		kill(getpid(), SIGTSTP);
		/* Execution resumes here after SIGCONT */
		terminal_enable_raw_mode();
		terminal_enable_mouse_reporting();
		editor.force_full_redraw = 1;
		break;

	case HOME_KEY:
	case CTRL_KEY('a'):
		editor.cursor_x = 0;
		break;

	case END_KEY:
	case CTRL_KEY('e'):
		if (editor.cursor_y < editor.line_count)
			editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
		break;

	case ALT_KEY('f'):
	case CTRL_KEY('f'):
		editor_find_start();
		break;

	case ALT_KEY('?'):
	case F11_KEY:
		editor_help_open();
		break;

	case CTRL_KEY('u'):
		editor_undo();
		break;
	case CTRL_KEY('r'):
		editor_redo();
		break;

	/* Clipboard operations */
	case ALT_KEY('c'):
		editor_copy();
		break;
	case ALT_KEY('x'):
		editor_cut();
		break;
	case ALT_KEY('v'):
		editor_paste();
		break;
	case ALT_KEY('K'):
		editor_cut_line();
		break;
	case ALT_KEY('d'):
		editor_duplicate_line();
		break;

	/* Word movement */
	case CTRL_ARROW_LEFT: {
		if (editor.selection.active)
			selection_clear();
		struct line *wl = (editor.cursor_y < editor.line_count)
					  ? &editor.lines[editor.cursor_y]
					  : NULL;
		if (wl && editor.cursor_x > 0) {
			editor.cursor_x = cursor_prev_word(wl, editor.cursor_x);
		} else if (editor.cursor_y > 0) {
			editor.cursor_y--;
			editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
		}
		editor.preferred_column = -1;
		break;
	}
	case CTRL_ARROW_RIGHT: {
		if (editor.selection.active)
			selection_clear();
		struct line *wl = (editor.cursor_y < editor.line_count)
					  ? &editor.lines[editor.cursor_y]
					  : NULL;
		if (wl && editor.cursor_x < (int)wl->cell_count) {
			editor.cursor_x = cursor_next_word(wl, editor.cursor_x);
		} else if (editor.cursor_y < editor.line_count - 1) {
			editor.cursor_y++;
			editor.cursor_x = 0;
		}
		editor.preferred_column = -1;
		break;
	}

	/* Shift+Arrow selection */
	case SHIFT_ARROW_LEFT:
	case SHIFT_ARROW_RIGHT:
	case SHIFT_ARROW_UP:
	case SHIFT_ARROW_DOWN:
	case SHIFT_HOME_KEY:
	case SHIFT_END_KEY: {
		if (!editor.selection.active)
			selection_start();
		switch (key) {
		case SHIFT_ARROW_LEFT:
			editor_move_cursor((struct input_event){.key = ARROW_LEFT});
			break;
		case SHIFT_ARROW_RIGHT:
			editor_move_cursor((struct input_event){.key = ARROW_RIGHT});
			break;
		case SHIFT_ARROW_UP:
			editor_move_cursor((struct input_event){.key = ARROW_UP});
			break;
		case SHIFT_ARROW_DOWN:
			editor_move_cursor((struct input_event){.key = ARROW_DOWN});
			break;
		case SHIFT_HOME_KEY:
			editor.cursor_x = 0;
			break;
		case SHIFT_END_KEY:
			if (editor.cursor_y < editor.line_count)
				editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
			break;
		}
		selection_update();
		break;
	}

	/* Alt+A / Alt+E: select to start/end of line */
	case ALT_KEY('a'):
		if (!editor.selection.active)
			selection_start();
		editor.cursor_x = 0;
		selection_update();
		break;
	case ALT_KEY('e'):
		if (!editor.selection.active)
			selection_start();
		if (editor.cursor_y < editor.line_count)
			editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
		selection_update();
		break;

	/* Shift+Ctrl+Arrow word selection */
	case SHIFT_CTRL_ARROW_LEFT:
	case SHIFT_CTRL_ARROW_RIGHT: {
		if (!editor.selection.active)
			selection_start();
		struct line *wl = (editor.cursor_y < editor.line_count)
					  ? &editor.lines[editor.cursor_y]
					  : NULL;
		if (key == SHIFT_CTRL_ARROW_LEFT) {
			if (wl && editor.cursor_x > 0)
				editor.cursor_x = cursor_prev_word(wl, editor.cursor_x);
			else if (editor.cursor_y > 0) {
				editor.cursor_y--;
				editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
			}
		} else {
			if (wl && editor.cursor_x < (int)wl->cell_count)
				editor.cursor_x = cursor_next_word(wl, editor.cursor_x);
			else if (editor.cursor_y < editor.line_count - 1) {
				editor.cursor_y++;
				editor.cursor_x = 0;
			}
		}
		editor.preferred_column = -1;
		selection_update();
		break;
	}

	/* Mouse drag selection */
	case MOUSE_LEFT_BUTTON_DRAG: {
		int file_row = event.mouse_y + editor.row_offset;
		if (file_row < 0) file_row = 0;
		if (file_row >= editor.line_count)
			file_row = editor.line_count > 0 ? editor.line_count - 1 : 0;
		editor.cursor_y = file_row;
		if (editor.cursor_y < editor.line_count) {
			struct line *dl = &editor.lines[editor.cursor_y];
			int render_col = event.mouse_x + editor.column_offset;
			editor.cursor_x = line_render_column_to_cell(dl, render_col);
		}
		selection_update();
		break;
	}
	case MOUSE_BUTTON_RELEASED:
		break;

	case BACKSPACE:
	case CTRL_KEY('h'):
		if (editor.selection.active) {
			selection_delete();
			break;
		}
		editor_delete_char();
		break;
	case DEL_KEY: {
		if (editor.selection.active) {
			selection_delete();
			break;
		}
		int saved_y = editor.cursor_y;
		int saved_x = editor.cursor_x;
		editor_move_cursor((struct input_event){.key = ARROW_RIGHT});
		if (editor.cursor_y == saved_y && editor.cursor_x == saved_x)
			break;
		editor_delete_char();
		break;
	}

	case PAGE_UP:
	case PAGE_DOWN: {
		if (key == PAGE_UP) {
			editor.cursor_y = editor.row_offset;
		} else if (key == PAGE_DOWN) {
			editor.cursor_y = editor.row_offset + editor.screen_rows - 1;
			if (editor.cursor_y > editor.line_count)
				editor.cursor_y = editor.line_count;
		}

		int times = editor.screen_rows;
		while (times--)
			editor_move_cursor((struct input_event){.key = key == PAGE_UP ? ARROW_UP : ARROW_DOWN});
		editor.suppress_scroll_margin = 1;
	} break;

	case ALT_KEY('h'):
	case ALT_KEY('j'):
	case ALT_KEY('k'):
	case ALT_KEY('l'):
	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		if (editor.selection.active)
			selection_clear();
		editor_move_cursor(event);
		break;

	case MOUSE_LEFT_BUTTON_PRESSED:
		if (editor.selection.active)
			selection_clear();
		selection_start();
		editor_move_cursor(event);
		editor.selection.anchor_y = editor.cursor_y;
		editor.selection.anchor_x = editor.cursor_x;
		break;

	case MOUSE_SCROLL_UP:
		editor_update_scroll_speed();
		editor_scroll_rows(ARROW_UP, editor.scroll_speed);
		break;

	case MOUSE_SCROLL_DOWN:
		editor_update_scroll_speed();
		editor_scroll_rows(ARROW_DOWN, editor.scroll_speed);
		break;

	case ESC_KEY:
		if (editor.viewing_help) {
			editor_help_close();
			break;
		}
		if (editor.selection.active)
			selection_clear();
		break;

	default:
		/* Only insert printable characters: Unicode codepoints above
		 * the control range, or tab. Filter out negative values,
		 * control characters, and unrecognized escape sequences. */
		if (key == '\t' || (key >= 32 && key != BACKSPACE)) {
			if (editor.selection.active)
				selection_delete();
			editor_insert_char(key);
		}
		break;
	}

}


/*** Init ***/

/* Initializes all editor state to default values: cursor at origin, no file
 * loaded, light theme active. Queries the terminal size and reserves two
 * rows for the status and message bars. */
void editor_init(void)
{
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.render_x = 0;
	editor.show_line_numbers = 1;
	editor.row_offset = 0;
	editor.column_offset = 0;
	editor.line_count = 0;
	editor.line_capacity = 0;
	editor_update_gutter_width();
	editor.lines = NULL;
	editor.dirty = 0;
	editor.filename = NULL;
	editor.status_message[0] = '\0';
	editor.status_message_time = 0;
	editor.syntax = NULL;
	editor_set_theme(current_theme_index);

	/* Install signal handlers using sigaction for portable, reliable behavior.
	 * SA_RESTART ensures system calls are not interrupted by signals. */
	struct sigaction sa_resize = {.sa_handler = terminal_handle_resize,
				     .sa_flags = SA_RESTART};
	sigemptyset(&sa_resize.sa_mask);
	sigaction(SIGWINCH, &sa_resize, NULL);

	struct sigaction sa_terminate = {.sa_handler = terminal_handle_terminate,
					.sa_flags = 0};
	sigemptyset(&sa_terminate.sa_mask);
	sigaction(SIGTERM, &sa_terminate, NULL);
	sigaction(SIGHUP, &sa_terminate, NULL);

	/* Ignore SIGPIPE so write() to a broken pipe returns EPIPE instead
	 * of killing the process. */
	signal(SIGPIPE, SIG_IGN);
	if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns) ==
			-1) {
		/* terminal_get_window_size already set defaults, just warn */
		editor_set_status_message("Warning: Using default terminal size (80x24)");
	}
	editor.screen_rows -= 2;

	gettimeofday(&editor.last_scroll_time, NULL);
	editor.scroll_speed = 1;
	editor.file_descriptor = -1;
	editor.mmap_base = NULL;
	editor.mmap_size = 0;

	editor.mode = MODE_NORMAL;
	editor.prompt = (struct prompt_state){0};
	editor.quit_after_save = 0;
	editor.confirm_callback = NULL;
	editor.saved_cursor_x = 0;
	editor.saved_cursor_y = 0;
	editor.saved_column_offset = 0;
	editor.saved_row_offset = 0;
	editor.search_last_match = -1;
	editor.search_last_match_offset = -1;
	editor.search_direction = 1;
	editor.search_saved_highlight_line = 0;
	editor.search_saved_syntax = NULL;
	editor.search_saved_syntax_count = 0;

	editor.preferred_column = -1;
	editor.suppress_scroll_margin = 0;
	editor.selection = (struct selection_state){0};
	editor.clipboard = (struct clipboard){0};
	undo_stack_init(&editor.undo);
	editor.viewing_help = 0;
	editor.snapshot = (struct editor_snapshot){0};
	editor.scratch_buffer = NULL;
	editor.scratch_capacity = 0;
	editor.scratch_offsets = NULL;
	editor.scratch_offsets_capacity = 0;
	editor.prev_cursor_x = -1;
	editor.prev_cursor_y = -1;
	editor.prev_render_x = -1;
	editor.prev_row_offset = -1;
	editor.prev_column_offset = -1;
	editor.prev_line_count = -1;
	editor.prev_status_message[0] = '\0';
	editor.prev_dirty = 0;
	editor.force_full_redraw = 1;
}

/* Entry point. Sets up the terminal (mouse reporting, raw mode), initializes
 * the editor, optionally opens a file from the command line argument, and
 * enters a poll()-based main loop. Input is buffered and decoded without
 * blocking, so paste and resize are handled efficiently. */
int main(int argc, char *argv[])
{
	atexit(editor_quit);

	/* Enable mouse reporting (non-fatal if it fails) */
	if (terminal_enable_mouse_reporting() == -1) {
		/* Mouse support unavailable, continue without it */
	}

	/* Enable raw mode */
	terminal_enable_raw_mode();

	editor_init();
	if (argc >= 2) {
		editor_open(argv[1]);
	}

	editor_set_status_message(
			"Alt: S=save Q=quit F=find G=goto N=lines T=theme F11=help");

	/* Switch to fully non-blocking reads now that startup terminal
	 * queries (which need VTIME=1) are complete. */
	terminal_set_nonblocking();

	while (1) {
		if (resize_pending)
			terminal_process_resize();

		editor_refresh_screen();

		struct pollfd stdin_poll = {STDIN_FILENO, POLLIN, 0};
		if (poll(&stdin_poll, 1, -1) == -1) {
			if (errno == EINTR)
				continue;
			terminal_die("poll");
		}

		input_buffer_fill();

		struct input_event event;
		while ((event = terminal_decode_key()).key != -1) {
			switch (editor.mode) {
			case MODE_NORMAL:
				editor_process_keypress(event);
				break;
			case MODE_PROMPT:
				prompt_handle_key(event);
				break;
			case MODE_CONFIRM:
				editor_handle_confirm(event);
				break;
			}
			if (input_buffer_available() == 0)
				input_buffer_fill();
		}
	}
}
