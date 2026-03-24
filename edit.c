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
#include <sys/file.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <regex.h>
#include <unistd.h>
#include <limits.h>
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
/* Git gutter marker states for tracking changes since last save. */
enum gutter_marker {
	GUTTER_NONE = 0,
	GUTTER_ADDED = 1,
	GUTTER_MODIFIED = 2
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
	/* Git-style gutter marker: none, added, or modified since last save. */
	int gutter_marker;
	/* Cached total display width of the line, or -1 when invalidated.
	 * Avoids recomputing line_render_width() multiple times per frame. */
	int cached_render_width;
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


/* Maximum lines to scan when searching for a matching bracket. */
#define BRACKET_SCAN_MAX_LINES 5000

/* Number of cycling colors used for bracket pair colorization. */
#define BRACKET_COLOR_COUNT 4

/* Maximum number of search queries retained in the history ring. */
#define SEARCH_HISTORY_MAX 50

/* Highest ASCII value for Ctrl+letter key combinations (A through Z). */
#define CONTROL_CHAR_MAX 26
/* Upper bound of the standard ASCII character range. */
#define ASCII_MAX 128

/* Number of microseconds in one second, for timeval arithmetic. */
#define MICROSECONDS_PER_SECOND 1000000

/* Carriage return + line feed sequence for terminal line endings. */
#define CRLF "\r\n"

/* Number of bytes to scan at the start of a file for NUL bytes when
 * detecting binary content. */
#define BINARY_DETECTION_SCAN_SIZE 8192

/* Maximum number of lines to cool per frame during the gradual
 * cooling scan of distant off-screen lines. */
#define COOL_SCAN_LINES_PER_FRAME 100

/* Seconds between automatic swap file writes. */
#define SWAP_WRITE_INTERVAL_SECONDS 30

/* File suffix appended to the original filename for swap files. */
#define SWAP_FILE_SUFFIX ".edit.swp"

/* Magic bytes at the start of a swap file for format identification. */
#define SWAP_MAGIC "EDSWAP01"

/* Size of the swap file magic header in bytes. */
#define SWAP_MAGIC_SIZE 8

/* Size of the PID field in the swap file header (little-endian uint32). */
#define SWAP_PID_SIZE 4

/* Total size of the swap file header (magic + PID). */
#define SWAP_HEADER_SIZE (SWAP_MAGIC_SIZE + SWAP_PID_SIZE)

/* Maximum number of cursor position entries to remember across files. */
#define CURSOR_HISTORY_MAX_ENTRIES 200

/* Maximum path length for cursor history and data directory paths. */
#define DATA_PATH_MAX 1024

/* Filename for the cursor position history storage. */
#define CURSOR_HISTORY_FILENAME "cursor_history"

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
	/* Background color for search match highlighting. */
	char *match_background;
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
	/* Current color theme. */
	struct editor_theme theme;
	/* Pending scroll events accumulated during the event loop.
	 * Processed once at the start of editor_refresh_screen(). */
	int scroll_pending_up;
	int scroll_pending_down;
	/* Timestamp of the last render frame that processed scroll events. */
	struct timeval scroll_last_frame_time;
	/* Number of consecutive frames with scroll events. Drives the
	 * acceleration multiplier (0 = first frame, ramps up). */
	int scroll_consecutive_frames;
	/* Whether line numbers are displayed in the gutter. */
	int show_line_numbers;
	/* Number of columns reserved for the line number gutter (digits + space). */
	int line_number_width;
	/* When set, long lines wrap at the screen edge instead of scrolling. */
	int word_wrap;
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
	/* Copy of the active search query, used by editor_draw_rows() to
	 * highlight all visible matches. Set during search, cleared on
	 * accept or cancel. */
	char *search_highlight_query;
	/* Total number of matches for the current query across all lines. */
	int search_total_matches;
	/* One-based index of the current match among all matches. */
	int search_current_match_index;
	/* When set, regex search mode is active. Toggled with Alt+X
	 * during a search prompt. */
	int search_regex_enabled;
	/* Compiled POSIX extended regular expression for regex search. */
	regex_t search_regex_compiled;
	/* True when search_regex_compiled holds a valid compiled regex. */
	int search_regex_valid;
	/* Previously searched queries, newest at the end. */
	char *search_history[SEARCH_HISTORY_MAX];
	/* Number of entries currently stored in search_history. */
	int search_history_count;
	/* Current browse position in the history, or -1 when not browsing. */
	int search_history_browse_index;
	/* Typed text saved when the user starts browsing history, so
	 * pressing down past the end restores the original input. */
	char *search_history_stash;
	/* Search string for find-and-replace, or NULL when inactive. */
	char *replace_query;
	/* Replacement string for find-and-replace, or NULL when inactive. */
	char *replace_with;
	/* Number of replacements performed in the current replace-all pass. */
	int replace_count;
	/* Desired render column preserved across vertical movement. */
	int preferred_column;
	/* Current text selection state. */
	struct selection_state selection;
	/* Internal clipboard for cut/copy/paste. */
	struct clipboard clipboard;
	/* Undo/redo history for the current file. */
	struct undo_stack undo;
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
	/* Running bracket nesting depth at the top of the viewport, used for
	 * bracket pair colorization. Starts at 0 for simplicity (no scan). */
	int bracket_depth_at_viewport;
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
	/* Line index of the matching bracket, or -1 when no match. */
	int bracket_match_line;
	/* Cell index of the matching bracket, or -1 when no match. */
	int bracket_match_cell;
	/* Tab display width in columns (default EDIT_TAB_STOP). Configurable
	 * via config file or --tabstop=N command line flag. */
	int tab_stop;
	/* Whether the terminal supports 24-bit true color. Detected at
	 * startup from COLORTERM and TERM environment variables. */
	int true_color;
	/* Column position for the vertical ruler line (0 = disabled).
	 * Configurable via config file or --ruler=N command line flag. */
	int ruler_column;
	/* Set when the opened file contains NUL bytes in its first 8 KB,
	 * indicating it may be binary data. Used to show a one-time warning. */
	int binary_file_warning;
	/* Previous frame's row_offset, used to detect scroll direction for
	 * predictive line warming (prefetching lines ahead of the viewport). */
	int prev_row_offset_for_prefetch;
	/* Current position in the file for the gradual line cooling scan.
	 * Cycles through all lines over multiple frames to avoid O(N) work
	 * per frame. */
	int cool_scan_position;
	/* Path to the swap file for crash recovery, or NULL. */
	char *swap_file_path;
	/* Timestamp of the last swap file write, for throttling. */
	time_t last_swap_write_time;
	/* Modification time of the file when it was opened or last saved.
	 * Used to detect external changes before overwriting. */
	struct timespec file_mtime;
	/* Device ID of the file when opened, for change detection. */
	dev_t file_device;
	/* Inode number of the file when opened, for change detection. */
	ino_t file_inode;
	/* Set when the file is not writable by the current user. Checked
	 * via access(W_OK) after opening. Does not block saves since the
	 * user may change permissions externally. */
	int read_only;
	/* File descriptor holding a LOCK_EX flock on the opened file, or
	 * -1 when no lock is held. Prevents concurrent editing. */
	int lock_file_descriptor;
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
void editor_cool_distant_lines(void);
void editor_prefetch_lines(void);
void editor_lines_ensure_capacity(int needed);
void swap_file_write(void);
void swap_file_remove(void);
void swap_file_check_existing(void);
char *swap_file_generate_path(void);
void file_record_stat(void);
int file_check_changed(void);
void cursor_history_restore(void);
void cursor_history_record(void);
void editor_check_read_only(void);
void file_acquire_lock(void);
void file_release_lock(void);
static int config_mkdir_parents(const char *path);

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
/* Replaces the prompt buffer contents with a new string. */
void prompt_set_buffer(const char *text);
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
/* Counts total search matches and determines the current match index. */
void editor_count_search_matches(const char *query, int match_line, int match_byte_offset);
/* Searches for a match on a single line using regex or literal search. */
char *editor_find_on_line(const char *render, size_t byte_length, const char *query, size_t query_length, int direction, int start_or_limit_byte, int case_flag, size_t *match_length_out);
/* Highlights all occurrences of the search query in a line's cells. */
int editor_highlight_search_matches(struct line *line, uint16_t *saved_syntax);
/* Adds a query to the search history ring. */
void editor_search_history_add(const char *query);
/* Starts the find-and-replace flow. */
void editor_replace_start(void);
/* Reads all data from stdin when piped, returning a malloc'd buffer.
 * Stores the total bytes read in *out_length. */
char *editor_read_stdin_pipe(size_t *out_length);
/* Saves the current theme name to the config file for persistence. */
void config_save_theme(const char *theme_name);
/* Reads the config file and applies settings to editor state. */
void config_load(void);
/* Detects whether the terminal supports 24-bit true color. */
void terminal_detect_true_color(void);

/*** Editor Theme ***/

/* Array of all available editor themes. */
struct editor_theme editor_themes[] = {

	/* Cyberpunk - Dark neon */
	{.name = "Cyberpunk", .background = "0A0A0C", .foreground = "D0D8E0",
	 .line_number = "404048", .status_bar = "101014", .status_bar_text = "00FFFF",
	 .message_bar = "FF00FF", .highlight_background = "151518",
	 .highlight_foreground = "FFFFFF", .comment = "505060", .keyword1 = "FF00FF",
	 .keyword2 = "00FFFF", .string = "00FF80", .number = "FFFF00", .match = "FF0080",
	 .match_background = "3D0020"},

	/* Nightwatch - Monochrome dark */
	{.name = "Nightwatch", .background = "0A0A0A", .foreground = "D0D0D0",
	 .line_number = "505050", .status_bar = "1A1A1A", .status_bar_text = "A0A0A0",
	 .message_bar = "808080", .highlight_background = "1A1A1A",
	 .highlight_foreground = "E0E0E0", .comment = "606060", .keyword1 = "FFFFFF",
	 .keyword2 = "B0B0B0", .string = "909090", .number = "C0C0C0", .match = "404040",
	 .match_background = "303030"},

	/* Daywatch - Monochrome light */
	{.name = "Daywatch", .background = "F5F5F5", .foreground = "303030",
	 .line_number = "A0A0A0", .status_bar = "E5E5E5", .status_bar_text = "505050",
	 .message_bar = "707070", .highlight_background = "E0E0E0",
	 .highlight_foreground = "202020", .comment = "808080", .keyword1 = "000000",
	 .keyword2 = "404040", .string = "505050", .number = "303030", .match = "C0C0C0",
	 .match_background = "D0D0B0"},

	/* Tokyo Night */
	{.name = "Tokyo Night", .background = "1A1B26", .foreground = "C0CAF5",
	 .line_number = "3B4261", .status_bar = "16161E", .status_bar_text = "7AA2F7",
	 .message_bar = "BB9AF7", .highlight_background = "292E42",
	 .highlight_foreground = "C0CAF5", .comment = "565F89", .keyword1 = "BB9AF7",
	 .keyword2 = "7DCFFF", .string = "9ECE6A", .number = "FF9E64", .match = "E0AF68",
	 .match_background = "3A2810"},

	/* Akira - Neo-Tokyo red/cyan */
	{.name = "Akira", .background = "0C0608", .foreground = "F0E4E8",
	 .line_number = "584048", .status_bar = "1C1018", .status_bar_text = "E0CCD4",
	 .message_bar = "D4C0C8", .highlight_background = "1C1014",
	 .highlight_foreground = "F0E4E8", .comment = "685060", .keyword1 = "FF3050",
	 .keyword2 = "40D0E8", .string = "F88080", .number = "E06878", .match = "103840",
	 .match_background = "002028"},
	/* Tokyo Night Cyberpunk - Neon accents on Tokyo Night's deep indigo base */
	{.name = "Tokyo Cyberpunk", .background = "13141F", .foreground = "D5DEFF",
	 .line_number = "2E3456", .status_bar = "0E0F18", .status_bar_text = "00FFFF",
	 .message_bar = "FF44CC", .highlight_background = "1E2036",
	 .highlight_foreground = "FFFFFF", .comment = "4A5380", .keyword1 = "FF44CC",
	 .keyword2 = "00FFFF", .string = "7AFF8E", .number = "FFB86C", .match = "E0AF68",
	 .match_background = "3A2810"},

	/* Clarity - Colorblind-accessible blue/orange/yellow palette that works
	 * across all color vision deficiencies (protanopia, deuteranopia, tritanopia). */
	{.name = "Clarity", .background = "1B1D2A", .foreground = "D4D7E4",
	 .line_number = "4A4D5E", .status_bar = "12131D", .status_bar_text = "5DA8E6",
	 .message_bar = "E89B4D", .highlight_background = "252838",
	 .highlight_foreground = "FFFFFF", .comment = "6B7089", .keyword1 = "5DA8E6",
	 .keyword2 = "E89B4D", .string = "E8C94D", .number = "7AB8E8", .match = "D48E2A",
	 .match_background = "2A1A08"},
};

/* Index of the currently active theme in editor_themes[]. */
int current_theme_index = 0;

/* Applies the theme at the given index in editor_themes[]. */
void editor_set_theme(int index)
{
	editor.theme = editor_themes[index];
}

/* Cycles to the next theme in editor_themes[] and displays its name.
 * Persists the choice to the config file so it survives restarts. */
void editor_switch_theme(void)
{
	editor.force_full_redraw = 1;
	current_theme_index = (current_theme_index + 1) %
		(int)(sizeof(editor_themes) / sizeof(editor_themes[0]));
	editor_set_theme(current_theme_index);
	config_save_theme(editor.theme.name);
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

/* Toggles soft word wrap. When enabled, long lines wrap at the screen
 * edge instead of scrolling horizontally. */
void editor_toggle_word_wrap(void)
{
	editor.force_full_redraw = 1;
	editor.word_wrap = !editor.word_wrap;
	if (editor.word_wrap)
		editor.column_offset = 0;
	editor_set_status_message(
			"Word wrap: %s", editor.word_wrap ? "on" : "off");
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
#ifdef EDIT_TESTING
int terminal_get_window_size(int *rows, int *columns)
{
	*columns = 80;
	*rows = 24;
	return 0;
}
#else
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
#endif

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
	line->gutter_marker = GUTTER_NONE;
	line->cached_render_width = -1;
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
	line->cached_render_width = -1;
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
		line->cached_render_width = -1;
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
		return NULL;
	size_t pos = 0;
	for (uint32_t i = 0; i < line->cell_count; i++) {
		int written = utf8_encode(line->cells[i].codepoint, &bytes[pos]);
		pos += written;
	}
	bytes[pos] = '\0';
	*out_length = pos;
	return bytes;
}
/* Inserts a cell at the given position, shifting cells to the right.
 * Marks the line as modified for the git gutter. */
void line_insert_cell(struct line *line, uint32_t pos, struct cell c)
{
	line_ensure_warm(line);
	line->temperature = LINE_HOT;
	line->cached_render_width = -1;
	if (line->gutter_marker == GUTTER_NONE)
		line->gutter_marker = GUTTER_MODIFIED;
	if (pos > line->cell_count)
		pos = line->cell_count;
	line_ensure_capacity(line, line->cell_count + 1);
	memmove(&line->cells[pos + 1], &line->cells[pos],
			sizeof(struct cell) * (line->cell_count - pos));
	line->cells[pos] = c;
	line->cell_count++;
}
/* Deletes the cell at the given position, shifting cells to the left.
 * Marks the line as modified for the git gutter. */
void line_delete_cell(struct line *line, uint32_t pos)
{
	line_ensure_warm(line);
	line->temperature = LINE_HOT;
	line->cached_render_width = -1;
	if (line->gutter_marker == GUTTER_NONE)
		line->gutter_marker = GUTTER_MODIFIED;
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
	dest->cached_render_width = -1;
	if (from >= src->cell_count)
		return;
	uint32_t count = src->cell_count - from;
	line_ensure_capacity(dest, dest->cell_count + count);
	memcpy(&dest->cells[dest->cell_count], &src->cells[from],
		   sizeof(struct cell) * count);
	dest->cell_count += count;
}
/* Returns the display width of a single cell at the given column position.
 * Tabs expand to the next tab stop using the configurable editor.tab_stop
 * width; wide/CJK characters take 2 columns; control and zero-width
 * characters take 1 column (rendered as symbols). */
int cell_display_width(struct cell *c, int current_column)
{
	if (c->codepoint == '\t')
		return editor.tab_stop - (current_column % editor.tab_stop);
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

/* Returns the total display width of a line (tab and width aware).
 * Uses a cached value when available to avoid redundant traversals
 * of the cell array, which matters for very long lines. */
int line_render_width(struct line *line)
{
	if (line->cached_render_width >= 0)
		return line->cached_render_width;
	line_ensure_warm(line);
	int width = line_cell_to_render_column(line, (int)line->cell_count);
	line->cached_render_width = width;
	return width;
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
	editor.lines[position].gutter_marker = GUTTER_ADDED;

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
			upper->cached_render_width = -1;
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
				new_line->cached_render_width = -1;
			}
			ln = &editor.lines[entry->line_index];
			ln->cell_count = tail_start;
			ln->temperature = LINE_HOT;
			ln->cached_render_width = -1;
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
			ln->cached_render_width = -1;
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
				new_line->cached_render_width = -1;
			}
			ln = &editor.lines[entry->line_index];
			ln->cell_count = split;
			ln->temperature = LINE_HOT;
			ln->cached_render_width = -1;
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
			ln->cached_render_width = -1;
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

	/* Compute the full range that could have flags set. Use both
	 * anchor and cursor to cover the widest possible range — the
	 * cursor may have moved since the last selection_update(). */
	int min_y = editor.selection.anchor_y;
	int max_y = editor.cursor_y;
	if (min_y > max_y) {
		int tmp = min_y;
		min_y = max_y;
		max_y = tmp;
	}

	for (int y = min_y; y <= max_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		if (ln->temperature == LINE_COLD)
			continue;
		for (uint32_t i = 0; i < ln->cell_count; i++)
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

/* Clears CELL_FLAG_SELECTED from all cells on lines between
 * prev_start_y and prev_end_y (the previous selection range).
 * Called before setting new flags so shrinking selections don't
 * leave stale highlights on lines that fell out of range. */
void selection_clear_range(int prev_start_y, int prev_end_y)
{
	for (int y = prev_start_y; y <= prev_end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		if (ln->temperature == LINE_COLD)
			continue;
		for (uint32_t i = 0; i < ln->cell_count; i++)
			ln->cells[i].flags &= ~CELL_FLAG_SELECTED;
	}
}

/* Updates CELL_FLAG_SELECTED on cells in the selection range. Tracks
 * the previous range so shrinking selections correctly clear stale
 * flags on lines that are no longer in range. */
void selection_update(void)
{
	if (!editor.selection.active)
		return;
	editor.force_full_redraw = 1;

	/* Clear flags from the previous selection range first. This
	 * handles the case where the selection shrank — lines that
	 * were previously selected but are no longer in range would
	 * otherwise keep stale CELL_FLAG_SELECTED bits. */
	static int prev_start_y = -1, prev_end_y = -1;
	if (prev_start_y >= 0)
		selection_clear_range(prev_start_y, prev_end_y);

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

	prev_start_y = start_y;
	prev_end_y = end_y;
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
		first->cached_render_width = -1;

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
	if (!editor.clipboard.lines || !editor.clipboard.line_lengths) {
		free(editor.clipboard.lines);
		free(editor.clipboard.line_lengths);
		editor.clipboard = (struct clipboard){0};
		return;
	}
	editor.clipboard.line_count = 0;
	editor.clipboard.is_line_mode = is_line_mode;

	size_t start = 0;
	for (size_t i = 0; i <= length; i++) {
		if (i == length || text[i] == '\n') {
			size_t line_len = i - start;
			char *line = malloc(line_len + 1);
			if (!line)
				return;
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
		if (tail == NULL)
			return;
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
		ln->cached_render_width = -1;
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

/* Checks whether the current file is writable by the user. Uses access()
 * with W_OK to test write permission. New files (no filename) are never
 * marked read-only. Called after opening a file. */
void editor_check_read_only(void)
{
	if (editor.filename == NULL) {
		editor.read_only = 0;
		return;
	}
	editor.read_only = (access(editor.filename, W_OK) == -1) ? 1 : 0;
}

/* Acquires an exclusive advisory lock on the current file using flock().
 * If the file is already locked by another process, the editor continues
 * in read-only mode with a warning. Non-fatal on any other flock failure
 * (e.g., NFS or unsupported filesystem) -- locking is silently skipped. */
void file_acquire_lock(void)
{
	if (editor.filename == NULL)
		return;

	int fd = open(editor.filename, O_RDONLY);
	if (fd == -1)
		return;

	if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
		if (errno == EWOULDBLOCK) {
			editor_set_status_message(
				"File is locked by another process");
			editor.read_only = 1;
		}
		close(fd);
		return;
	}

	editor.lock_file_descriptor = fd;
}

/* Releases the advisory file lock and closes the lock file descriptor.
 * Safe to call when no lock is held (lock_file_descriptor == -1). */
void file_release_lock(void)
{
	if (editor.lock_file_descriptor == -1)
		return;

	flock(editor.lock_file_descriptor, LOCK_UN);
	close(editor.lock_file_descriptor);
	editor.lock_file_descriptor = -1;
}

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
			if (allocated_bytes == NULL) {
				free(buffer);
				return NULL;
			}
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

	/* Scan the first few KB for NUL bytes to detect binary files. */
	size_t scan_limit = (size_t)file_size;
	if (scan_limit > BINARY_DETECTION_SCAN_SIZE)
		scan_limit = BINARY_DETECTION_SCAN_SIZE;
	if (memchr(base, '\0', scan_limit) != NULL)
		editor.binary_file_warning = 1;

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
		ln->gutter_marker = GUTTER_NONE;
		ln->cached_render_width = -1;
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
		ln->gutter_marker = GUTTER_NONE;
		ln->cached_render_width = -1;
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

	/* Check for an existing swap file before loading. If recovery
	 * is accepted, the callback replaces the buffer content. */
	swap_file_check_existing();

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

	/* Record the file's stat for external change detection. */
	file_record_stat();

	editor_update_gutter_width();

	syntax_select_highlight();

	if (editor.binary_file_warning)
		editor_set_status_message("Warning: file appears to contain binary data");

	editor.dirty = 0;
	return 0;
}

/* Writes a buffer to a file descriptor, retrying on short writes and
 * EINTR. Returns 0 on success, negative errno on failure. */
static int write_full(int fd, const char *data, size_t length)
{
	size_t written = 0;
	while (written < length) {
		ssize_t result = write(fd, data + written, length - written);
		if (result == -1) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		written += (size_t)result;
	}
	return 0;
}

/* Writes the current file content to disk using an atomic write-to-temp
 * + rename pattern. Streams each line directly to the file descriptor
 * instead of building a monolithic buffer, so peak memory is O(max_line)
 * instead of O(file_size). COLD lines are written straight from the mmap
 * region; WARM/HOT lines are converted to bytes one at a time.
 * Returns 0 on success, negative errno on failure. */
/* Callback for the file-changed-on-disk confirmation. Proceeds with
 * the save on 'y', cancels on anything else. */
static void file_changed_overwrite_callback(int key)
{
	if (key == 'y' || key == 'Y') {
		editor_save_write();
	} else {
		editor_set_status_message("Save cancelled");
		editor.quit_after_save = 0;
	}
}

int editor_save_write(void)
{
	/* Check whether the file was modified externally since we opened
	 * or last saved it. If so, prompt for confirmation before
	 * overwriting. The check is skipped when we are already inside
	 * the confirmation callback (file_mtime will be zeroed). */
	if (file_check_changed()) {
		/* Reset the stored stat so the callback doesn't re-prompt. */
		file_record_stat();
		confirm_open("File changed on disk! Overwrite? (y/n)",
			     file_changed_overwrite_callback);
		return 0;
	}

	int ret = 0;
	int fd = -1;
	char *tmp_path = NULL;
	size_t total_bytes = 0;
	char newline = '\n';

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

	/* Stream each line to the file descriptor. COLD lines are written
	 * directly from mmap; WARM/HOT lines are serialized one at a time
	 * to keep peak memory usage proportional to the longest line. */
	for (int j = 0; j < editor.line_count; j++) {
		struct line *line = &editor.lines[j];

		if (line->temperature == LINE_COLD && editor.mmap_base) {
			ret = write_full(fd, editor.mmap_base + line->mmap_offset,
					 line->mmap_length);
			if (ret < 0)
				goto out;
			total_bytes += line->mmap_length;
		} else {
			size_t byte_length;
			char *bytes = line_to_bytes(line, &byte_length);
			if (bytes == NULL) {
				ret = -ENOMEM;
				goto out;
			}
			ret = write_full(fd, bytes, byte_length);
			free(bytes);
			if (ret < 0)
				goto out;
			total_bytes += byte_length;
		}

		ret = write_full(fd, &newline, 1);
		if (ret < 0)
			goto out;
		total_bytes++;
	}

	/* Flush to stable storage before rename */
	if (fsync(fd) == -1) {
		ret = -errno;
		goto out;
	}
	close(fd);
	fd = -1;

	/* Release mmap after writing — COLD lines may have been read from
	 * it during the streaming write above. */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
		editor.mmap_size = 0;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}

	/* Atomic rename over the original file */
	if (rename(tmp_path, editor.filename) == -1) {
		ret = -errno;
		goto out;
	}
	/* tmp_path is now consumed by rename — don't unlink it */
	free(tmp_path);
	tmp_path = NULL;

	editor.dirty = 0;

	/* Update the stored stat so subsequent saves detect new changes. */
	file_record_stat();

	/* Remove the swap file since the buffer is now safely on disk. */
	swap_file_remove();

	/* Reset all gutter markers since the file is now saved. */
	for (int i = 0; i < editor.line_count; i++)
		editor.lines[i].gutter_marker = GUTTER_NONE;

	editor_set_status_message("%zu bytes written to disk", total_bytes);

out:
	if (fd != -1)
		close(fd);
	if (tmp_path != NULL)
		unlink(tmp_path);
	free(tmp_path);
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
 * Otherwise opens a prompt to ask for one. Shows a warning when the
 * file is read-only, but does not block the save since the user may
 * have changed permissions externally. */
void editor_save_start(void)
{
	if (editor.read_only) {
		editor_set_status_message(
			"Warning: file is read-only (saving anyway)");
	}
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
	/* Record cursor position before cleanup destroys the state. */
	cursor_history_record();

	/* Release the advisory file lock before cleanup. */
	file_release_lock();

	/* Remove the swap file if the buffer is clean. */
	if (!editor.dirty)
		swap_file_remove();

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

	/* Free any in-progress replace state */
	free(editor.replace_query);
	free(editor.replace_with);

	/* Free search highlight query */
	free(editor.search_highlight_query);

	/* Free compiled regex if valid */
	if (editor.search_regex_valid) {
		regfree(&editor.search_regex_compiled);
		editor.search_regex_valid = 0;
	}

	/* Free search history entries */
	for (int i = 0; i < editor.search_history_count; i++)
		free(editor.search_history[i]);
	free(editor.search_history_stash);

	/* Free swap file path */
	free(editor.swap_file_path);
}

/*** Swap file ***/

/* Generates the swap file path for the current buffer. For named files
 * the swap file lives alongside the original with ".edit.swp" appended
 * (e.g., "foo.c.edit.swp"). For unnamed buffers it uses a PID-based
 * temporary path. Caller must free the returned string. */
char *swap_file_generate_path(void)
{
	if (editor.filename) {
		size_t filename_length = strlen(editor.filename);
		size_t suffix_length = strlen(SWAP_FILE_SUFFIX);
		char *path = malloc(filename_length + suffix_length + 1);
		if (!path)
			return NULL;
		memcpy(path, editor.filename, filename_length);
		memcpy(path + filename_length, SWAP_FILE_SUFFIX, suffix_length);
		path[filename_length + suffix_length] = '\0';
		return path;
	}

	/* Unnamed buffer: use /tmp with a PID-based name. */
	char path[DATA_PATH_MAX];
	snprintf(path, sizeof(path), "/tmp/.edit_unnamed_%d.swp",
		 (int)getpid());
	return strdup(path);
}

/* Writes the current buffer to the swap file for crash recovery. Only
 * writes when the buffer is dirty and enough time has elapsed since the
 * last write. Failure is non-fatal -- a warning is shown in the status
 * bar but editing continues normally. */
void swap_file_write(void)
{
	if (!editor.dirty || editor.line_count == 0)
		return;

	time_t now = time(NULL);
	if (now - editor.last_swap_write_time < SWAP_WRITE_INTERVAL_SECONDS)
		return;

	/* Generate the swap path if we haven't already. */
	if (!editor.swap_file_path) {
		editor.swap_file_path = swap_file_generate_path();
		if (!editor.swap_file_path)
			return;
	}

	int fd = open(editor.swap_file_path,
		      O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMISSION_DEFAULT);
	if (fd == -1) {
		editor_set_status_message("Swap file warning: %s",
					 strerror(errno));
		editor.last_swap_write_time = now;
		return;
	}

	/* Write the header: 8-byte magic + 4-byte little-endian PID. */
	unsigned char header[SWAP_HEADER_SIZE];
	memcpy(header, SWAP_MAGIC, SWAP_MAGIC_SIZE);
	uint32_t pid = (uint32_t)getpid();
	header[SWAP_MAGIC_SIZE + 0] = (unsigned char)(pid & 0xFF);
	header[SWAP_MAGIC_SIZE + 1] = (unsigned char)((pid >> 8) & 0xFF);
	header[SWAP_MAGIC_SIZE + 2] = (unsigned char)((pid >> 16) & 0xFF);
	header[SWAP_MAGIC_SIZE + 3] = (unsigned char)((pid >> 24) & 0xFF);
	if (write_full(fd, (const char *)header, SWAP_HEADER_SIZE) < 0)
		goto fail;

	/* Stream each line to the swap file, using the same COLD/WARM/HOT
	 * strategy as editor_save_write(). */
	char newline = '\n';
	for (int j = 0; j < editor.line_count; j++) {
		struct line *line = &editor.lines[j];

		if (line->temperature == LINE_COLD && editor.mmap_base) {
			if (write_full(fd, editor.mmap_base + line->mmap_offset,
				       line->mmap_length) < 0)
				goto fail;
		} else {
			size_t byte_length;
			char *bytes = line_to_bytes(line, &byte_length);
			if (bytes == NULL)
				goto fail;
			int result = write_full(fd, bytes, byte_length);
			free(bytes);
			if (result < 0)
				goto fail;
		}

		if (write_full(fd, &newline, 1) < 0)
			goto fail;
	}

	close(fd);
	editor.last_swap_write_time = now;
	return;

fail:
	close(fd);
	editor.last_swap_write_time = now;
	editor_set_status_message("Swap file warning: write failed");
}

/* Removes the swap file from disk. Called after a successful save and
 * on clean exit when there are no unsaved changes. */
void swap_file_remove(void)
{
	if (editor.swap_file_path) {
		unlink(editor.swap_file_path);
		free(editor.swap_file_path);
		editor.swap_file_path = NULL;
	}
}

/* Callback for the swap file recovery confirmation prompt. On 'y',
 * loads the swap file content into the buffer. On 'n', deletes the
 * stale swap file and proceeds with the normal file open. */
static void swap_file_recover_callback(int key)
{
	if (key != 'y' && key != 'Y') {
		/* User declined recovery -- remove the stale swap file. */
		if (editor.swap_file_path) {
			unlink(editor.swap_file_path);
			free(editor.swap_file_path);
			editor.swap_file_path = NULL;
		}
		editor_set_status_message("Recovery skipped");
		return;
	}

	/* Read the swap file and load its content into the buffer. */
	if (!editor.swap_file_path)
		return;

	int fd = open(editor.swap_file_path, O_RDONLY);
	if (fd == -1) {
		editor_set_status_message("Cannot open swap file: %s",
					 strerror(errno));
		return;
	}

	off_t file_size = lseek(fd, 0, SEEK_END);
	if (file_size < SWAP_HEADER_SIZE) {
		close(fd);
		editor_set_status_message("Swap file is corrupt");
		return;
	}
	lseek(fd, 0, SEEK_SET);

	char *data = malloc((size_t)file_size);
	if (!data) {
		close(fd);
		editor_set_status_message("Out of memory reading swap file");
		return;
	}

	ssize_t bytes_read = read(fd, data, (size_t)file_size);
	close(fd);
	if (bytes_read < SWAP_HEADER_SIZE) {
		free(data);
		editor_set_status_message("Swap file read error");
		return;
	}

	/* Verify the magic header. */
	if (memcmp(data, SWAP_MAGIC, SWAP_MAGIC_SIZE) != 0) {
		free(data);
		editor_set_status_message("Swap file has invalid magic");
		return;
	}

	/* Clear the existing buffer content. */
	for (int i = 0; i < editor.line_count; i++)
		line_free(&editor.lines[i]);
	editor.line_count = 0;

	/* Release existing mmap since we are replacing the content. */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
		editor.mmap_size = 0;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}

	/* Parse the content after the header into editor lines. */
	const char *pos = data + SWAP_HEADER_SIZE;
	const char *end = data + bytes_read;
	while (pos < end) {
		const char *newline = memchr(pos, '\n', (size_t)(end - pos));
		if (!newline)
			newline = end;
		size_t line_length = (size_t)(newline - pos);
		if (line_length > 0 && pos[line_length - 1] == '\r')
			line_length--;
		editor_line_insert(editor.line_count, pos, line_length);
		pos = (newline < end) ? newline + 1 : end;
	}

	free(data);
	editor.dirty = 1;
	editor_update_gutter_width();
	editor_set_status_message("Recovered from swap file");
}

/* Checks whether a swap file exists for the current filename. If the
 * creator PID is still alive, shows a warning. If the creator is dead,
 * prompts the user to recover. Called before loading the file. */
void swap_file_check_existing(void)
{
	char *swap_path = swap_file_generate_path();
	if (!swap_path)
		return;

	struct stat swap_stat;
	if (stat(swap_path, &swap_stat) == -1) {
		/* No swap file exists -- nothing to do. */
		free(swap_path);
		return;
	}

	/* Read the header to extract the creator PID. */
	int fd = open(swap_path, O_RDONLY);
	if (fd == -1) {
		free(swap_path);
		return;
	}

	unsigned char header[SWAP_HEADER_SIZE];
	ssize_t bytes_read = read(fd, header, SWAP_HEADER_SIZE);
	close(fd);
	if (bytes_read < SWAP_HEADER_SIZE
	    || memcmp(header, SWAP_MAGIC, SWAP_MAGIC_SIZE) != 0) {
		free(swap_path);
		return;
	}

	uint32_t creator_pid = (uint32_t)header[SWAP_MAGIC_SIZE]
		| ((uint32_t)header[SWAP_MAGIC_SIZE + 1] << 8)
		| ((uint32_t)header[SWAP_MAGIC_SIZE + 2] << 16)
		| ((uint32_t)header[SWAP_MAGIC_SIZE + 3] << 24);

	/* Save the path so the callback can access it. */
	free(editor.swap_file_path);
	editor.swap_file_path = swap_path;

	/* Check if the creator process is still running. */
	if (kill((pid_t)creator_pid, 0) == 0) {
		/* Process is alive -- just warn, don't offer recovery. */
		editor_set_status_message(
			"Swap file exists (PID %u is running)",
			(unsigned)creator_pid);
		return;
	}

	/* Creator is dead -- offer recovery. */
	confirm_open("Recovery file found. Recover? (y/n)",
		     swap_file_recover_callback);
}

/*** File change detection ***/

/* Records the file's mtime, device, and inode from stat so we can
 * detect external modifications later. Called after a successful
 * editor_open_mmap() or editor_save_write(). */
void file_record_stat(void)
{
	if (!editor.filename)
		return;

	struct stat file_stat;
	if (stat(editor.filename, &file_stat) == 0) {
		editor.file_mtime = file_stat.st_mtim;
		editor.file_device = file_stat.st_dev;
		editor.file_inode = file_stat.st_ino;
	}
}

/* Compares the file's current stat against the stored values to detect
 * external changes. Returns 1 if the file has been modified or replaced
 * since we last recorded its stat, 0 otherwise. */
int file_check_changed(void)
{
	if (!editor.filename)
		return 0;

	struct stat file_stat;
	if (stat(editor.filename, &file_stat) != 0)
		return 0;

	/* Different device or inode means the file was replaced. */
	if (file_stat.st_dev != editor.file_device
	    || file_stat.st_ino != editor.file_inode)
		return 1;

	/* Different modification time means the file was edited. */
	if (file_stat.st_mtim.tv_sec != editor.file_mtime.tv_sec
	    || file_stat.st_mtim.tv_nsec != editor.file_mtime.tv_nsec)
		return 1;

	return 0;
}

/*** Cursor history ***/

/* Builds the cursor history directory path into the provided buffer.
 * Uses $XDG_DATA_HOME/edit or falls back to $HOME/.local/share/edit.
 * Returns 0 on success, -1 if no home directory is set. */
static int cursor_history_build_directory(char *dir_out, size_t dir_size)
{
	const char *xdg = getenv("XDG_DATA_HOME");
	if (xdg && xdg[0] != '\0') {
		snprintf(dir_out, dir_size, "%s/edit", xdg);
		return 0;
	}
	const char *home = getenv("HOME");
	if (home && home[0] != '\0') {
		snprintf(dir_out, dir_size, "%s/.local/share/edit", home);
		return 0;
	}
	return -1;
}

/* Builds the full path to the cursor history file. Returns 0 on
 * success, -1 if the home directory cannot be determined. */
static int cursor_history_build_path(char *path_out, size_t path_size)
{
	char dir[DATA_PATH_MAX];
	if (cursor_history_build_directory(dir, sizeof(dir)) == -1)
		return -1;
	snprintf(path_out, path_size, "%s/%s", dir, CURSOR_HISTORY_FILENAME);
	return 0;
}

/* Restores the cursor position and viewport offset for the current file
 * from the on-disk history. Resolves the filename to an absolute path
 * and looks it up in the history file. Clamps the restored values to
 * the actual file bounds. Called after editor_open() loads the file. */
void cursor_history_restore(void)
{
	if (!editor.filename)
		return;

	char *absolute_path = realpath(editor.filename, NULL);
	if (!absolute_path)
		return;

	char history_path[DATA_PATH_MAX];
	if (cursor_history_build_path(history_path,
				      sizeof(history_path)) == -1) {
		free(absolute_path);
		return;
	}

	FILE *history_file = fopen(history_path, "r");
	if (!history_file) {
		free(absolute_path);
		return;
	}

	char line_buffer[DATA_PATH_MAX];
	int found_cursor_y = -1;
	int found_cursor_x = -1;
	int found_row_offset = -1;

	while (fgets(line_buffer, sizeof(line_buffer), history_file)) {
		/* Strip trailing newline. */
		char *newline = strchr(line_buffer, '\n');
		if (newline)
			*newline = '\0';

		/* Parse "path:cursor_y:cursor_x:row_offset". Find the last
		 * three colons to handle paths that contain colons. */
		char *last_colon = strrchr(line_buffer, ':');
		if (!last_colon || last_colon == line_buffer)
			continue;
		*last_colon = '\0';
		int row_offset_value = atoi(last_colon + 1);

		char *second_colon = strrchr(line_buffer, ':');
		if (!second_colon || second_colon == line_buffer)
			continue;
		*second_colon = '\0';
		int cursor_x_value = atoi(second_colon + 1);

		char *first_colon = strrchr(line_buffer, ':');
		if (!first_colon || first_colon == line_buffer)
			continue;
		*first_colon = '\0';
		int cursor_y_value = atoi(first_colon + 1);

		if (strcmp(line_buffer, absolute_path) == 0) {
			found_cursor_y = cursor_y_value;
			found_cursor_x = cursor_x_value;
			found_row_offset = row_offset_value;
			/* Keep scanning -- last entry for this path wins
			 * (it is the most recent). */
		}
	}

	fclose(history_file);

	if (found_cursor_y >= 0) {
		/* Clamp cursor_y to file bounds. */
		if (found_cursor_y >= editor.line_count)
			found_cursor_y = editor.line_count > 0
				? editor.line_count - 1 : 0;

		editor.cursor_y = found_cursor_y;

		/* Clamp cursor_x to line bounds. */
		if (found_cursor_y < editor.line_count) {
			struct line *target_line =
				&editor.lines[found_cursor_y];
			line_ensure_warm(target_line);
			if (found_cursor_x > (int)target_line->cell_count)
				found_cursor_x = (int)target_line->cell_count;
		}
		editor.cursor_x = found_cursor_x < 0 ? 0 : found_cursor_x;

		/* Clamp and restore row_offset. */
		if (found_row_offset < 0)
			found_row_offset = 0;
		if (found_row_offset >= editor.line_count)
			found_row_offset = editor.line_count > 0
				? editor.line_count - 1 : 0;
		editor.row_offset = found_row_offset;
	}

	free(absolute_path);
}

/* Records the current cursor position and viewport offset for the file
 * into the on-disk history. Uses LRU eviction to keep the history file
 * at most CURSOR_HISTORY_MAX_ENTRIES entries. Called before editor_quit()
 * cleans up resources. */
void cursor_history_record(void)
{
	if (!editor.filename)
		return;

	char *absolute_path = realpath(editor.filename, NULL);
	if (!absolute_path)
		return;

	char data_directory[DATA_PATH_MAX];
	if (cursor_history_build_directory(data_directory,
					   sizeof(data_directory)) == -1) {
		free(absolute_path);
		return;
	}

	/* Ensure the data directory exists. */
	config_mkdir_parents(data_directory);

	char history_path[DATA_PATH_MAX];
	if (cursor_history_build_path(history_path,
				      sizeof(history_path)) == -1) {
		free(absolute_path);
		return;
	}

	/* Read existing entries, filtering out the current file. */
	char **entries = NULL;
	int entry_count = 0;
	int entry_capacity = 0;

	FILE *history_file = fopen(history_path, "r");
	if (history_file) {
		char line_buffer[DATA_PATH_MAX];
		while (fgets(line_buffer, sizeof(line_buffer), history_file)) {
			/* Strip trailing newline. */
			char *newline = strchr(line_buffer, '\n');
			if (newline)
				*newline = '\0';
			if (line_buffer[0] == '\0')
				continue;

			/* Check if this entry is for the same file by
			 * parsing the path portion (everything before the
			 * last three colon-separated fields). */
			char test_buffer[DATA_PATH_MAX];
			snprintf(test_buffer, sizeof(test_buffer),
				 "%s", line_buffer);
			char *last = strrchr(test_buffer, ':');
			if (last) *last = '\0';
			char *second = strrchr(test_buffer, ':');
			if (second) *second = '\0';
			char *first = strrchr(test_buffer, ':');
			if (first) *first = '\0';

			/* Skip entries for the current file (will be
			 * re-added at the end). */
			if (strcmp(test_buffer, absolute_path) == 0)
				continue;

			/* Grow the entries array if needed. */
			if (entry_count >= entry_capacity) {
				entry_capacity = entry_capacity
					? entry_capacity * 2 : 32;
				entries = realloc(entries,
						  sizeof(char *) * entry_capacity);
				if (!entries) {
					free(absolute_path);
					fclose(history_file);
					return;
				}
			}
			entries[entry_count++] = strdup(line_buffer);
		}
		fclose(history_file);
	}

	/* Evict oldest entries if we are at the limit. The new entry
	 * will be appended, so make room for it. */
	while (entry_count >= CURSOR_HISTORY_MAX_ENTRIES - 1
	       && entry_count > 0) {
		free(entries[0]);
		memmove(&entries[0], &entries[1],
			sizeof(char *) * (entry_count - 1));
		entry_count--;
	}

	/* Write all entries plus the new one. */
	FILE *output = fopen(history_path, "w");
	if (!output) {
		for (int i = 0; i < entry_count; i++)
			free(entries[i]);
		free(entries);
		free(absolute_path);
		return;
	}

	for (int i = 0; i < entry_count; i++) {
		fprintf(output, "%s\n", entries[i]);
		free(entries[i]);
	}
	free(entries);

	fprintf(output, "%s:%d:%d:%d\n", absolute_path,
		editor.cursor_y, editor.cursor_x, editor.row_offset);

	fclose(output);
	free(absolute_path);
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
	"                         Left/Right navigate matches\n"
	"                         Up/Down browse search history\n"
	"                         Alt+C toggles case sensitivity\n"
	"                         Alt+X toggles regex mode\n"
	"                         Enter to accept, ESC to cancel\n"
	"  Alt+R                Find and replace\n"
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
	"  Tab (with selection)  Indent block\n"
	"  Shift+Tab (w/ sel)   Dedent block\n"
	"  Alt+/                Toggle line comment\n"
	"  Alt+]                Jump to matching bracket\n"
	"\n"
	"DISPLAY\n"
	"  Alt+T                Cycle color theme\n"
	"  Alt+N                Toggle line numbers\n"
	"  Alt+W                Toggle word wrap\n"
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

/* Counts all occurrences of the search query across the entire file.
 * When regex mode is enabled, uses regexec() on each line's byte string.
 * Also tracks which occurrence the current match is (by line and byte
 * offset) and stores results in editor.search_total_matches and
 * editor.search_current_match_index. */
void editor_count_search_matches(const char *query, int match_line, int match_byte_offset)
{
	size_t query_length = strlen(query);
	int total = 0;
	int current_index = 0;
	int case_flag = editor.search_case_insensitive;

	for (int i = 0; i < editor.line_count; i++) {
		struct line *ln = &editor.lines[i];
		size_t byte_length;
		const char *bytes;
		int allocated = 0;

		if (ln->temperature == LINE_COLD && editor.mmap_base) {
			bytes = editor.mmap_base + ln->mmap_offset;
			byte_length = ln->mmap_length;
		} else {
			line_ensure_warm(ln);
			bytes = line_to_bytes(ln, &byte_length);
			if (bytes == NULL)
				continue;
			allocated = 1;
		}

		if (editor.search_regex_enabled && editor.search_regex_valid) {
			/* Regex counting needs a null-terminated string */
			char *null_terminated = NULL;
			if (!allocated) {
				null_terminated = malloc(byte_length + 1);
				if (null_terminated) {
					memcpy(null_terminated, bytes, byte_length);
					null_terminated[byte_length] = '\0';
					bytes = null_terminated;
				}
			}
			regmatch_t match_info;
			size_t offset = 0;
			while (offset < byte_length) {
				int result = regexec(&editor.search_regex_compiled,
						     bytes + offset, 1, &match_info, 0);
				if (result != 0 || match_info.rm_so < 0)
					break;
				size_t abs_offset = offset + (size_t)match_info.rm_so;
				if ((size_t)match_info.rm_eo == (size_t)match_info.rm_so) {
					offset = abs_offset + 1;
					continue;
				}
				if (i < match_line || (i == match_line && (int)abs_offset < match_byte_offset))
					current_index++;
				else if (i == match_line && (int)abs_offset == match_byte_offset)
					current_index = total;
				total++;
				offset += (size_t)match_info.rm_eo;
			}
			free(null_terminated);
		} else {
			const char *search_pos = bytes;
			size_t remaining = byte_length;
			while (remaining >= query_length) {
				char *found = editor_memmem(search_pos, remaining,
							    query, query_length, case_flag);
				if (!found)
					break;
				int found_offset = (int)(found - bytes);
				if (i < match_line || (i == match_line && found_offset < match_byte_offset))
					current_index++;
				else if (i == match_line && found_offset == match_byte_offset)
					current_index = total;
				total++;
				search_pos = found + 1;
				remaining = byte_length - (size_t)(search_pos - bytes);
			}
		}

		if (allocated)
			free((char *)bytes);
	}

	editor.search_total_matches = total;
	editor.search_current_match_index = current_index + 1;
}

/* Searches for a match on a single line using either regex or literal
 * search depending on the current mode. For the forward direction,
 * finds the first match after start_byte. For backward, finds the
 * last match before limit_byte. Returns a pointer into render on
 * success, NULL on failure. For regex matches, stores the match
 * length in *match_length_out (literal matches use query_length). */
char *editor_find_on_line(const char *render, size_t byte_length, const char *query, size_t query_length, int direction, int start_or_limit_byte, int case_flag, size_t *match_length_out)
{
	*match_length_out = query_length;

	if (editor.search_regex_enabled && editor.search_regex_valid) {
		/* Regex needs a null-terminated string. If the render
		 * buffer isn't null-terminated (COLD mmap lines), copy it. */
		char *null_terminated = NULL;
		const char *search_buf = render;
		if (byte_length > 0 && render[byte_length] != '\0') {
			null_terminated = malloc(byte_length + 1);
			if (!null_terminated)
				return NULL;
			memcpy(null_terminated, render, byte_length);
			null_terminated[byte_length] = '\0';
			search_buf = null_terminated;
		}

		regmatch_t match_info;
		char *result = NULL;

		if (direction == 1) {
			int start = (start_or_limit_byte >= 0) ? start_or_limit_byte + 1 : 0;
			if (start < (int)byte_length) {
				int rc = regexec(&editor.search_regex_compiled,
						 search_buf + start, 1, &match_info, 0);
				if (rc == 0 && match_info.rm_so >= 0) {
					int match_len = match_info.rm_eo - match_info.rm_so;
					if (match_len > 0) {
						result = (char *)(render + start + match_info.rm_so);
						*match_length_out = (size_t)match_len;
					}
				}
			}
		} else {
			int limit = (start_or_limit_byte >= 0) ? start_or_limit_byte : (int)byte_length;
			size_t offset = 0;
			while (offset < (size_t)limit) {
				int rc = regexec(&editor.search_regex_compiled,
						 search_buf + offset, 1, &match_info, 0);
				if (rc != 0 || match_info.rm_so < 0)
					break;
				size_t abs_pos = offset + (size_t)match_info.rm_so;
				if (abs_pos >= (size_t)limit)
					break;
				int match_len = match_info.rm_eo - match_info.rm_so;
				if (match_len <= 0) {
					offset = abs_pos + 1;
					continue;
				}
				result = (char *)(render + abs_pos);
				*match_length_out = (size_t)match_len;
				offset += (size_t)match_info.rm_eo;
			}
		}

		free(null_terminated);
		return result;
	}

	/* Literal search path */
	if (direction == 1) {
		int start = (start_or_limit_byte >= 0) ? start_or_limit_byte + 1 : 0;
		if (start < (int)byte_length)
			return editor_memmem(render + start, byte_length - start, query, query_length, case_flag);
		return NULL;
	}

	/* Backward literal search: find the last match before the limit */
	int limit = (start_or_limit_byte >= 0) ? start_or_limit_byte : (int)byte_length;
	const char *candidate = render;
	char *result = NULL;
	while (candidate < render + limit) {
		char *found = editor_memmem(candidate, (size_t)(render + limit - candidate), query, query_length, case_flag);
		if (!found || found >= render + limit)
			break;
		result = found;
		candidate = found + 1;
	}
	return result;
}

/* Callback invoked on each keypress during incremental search. Updates
 * the search highlight query, compiles regex when needed, searches for
 * the next/previous match, counts total matches, and shows wrap status. */
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
	} else if (key == ARROW_RIGHT) {
		editor.search_direction = 1;
	} else if (key == ARROW_LEFT) {
		editor.search_direction = -1;
	} else {
		editor.search_last_match = -1;
		editor.search_last_match_offset = -1;
		editor.search_direction = 1;
	}

	/* Update the all-match highlight query so editor_draw_rows()
	 * can mark every visible occurrence during rendering. */
	free(editor.search_highlight_query);
	editor.search_highlight_query = (query[0] != '\0') ? strdup(query) : NULL;

	/* Compile the regex when regex mode is enabled. Recompile on
	 * every callback because the query changes incrementally. */
	if (editor.search_regex_enabled) {
		if (editor.search_regex_valid) {
			regfree(&editor.search_regex_compiled);
			editor.search_regex_valid = 0;
		}
		if (query[0] != '\0') {
			int regex_flags = REG_EXTENDED;
			if (editor.search_case_insensitive)
				regex_flags |= REG_ICASE;
			int rc = regcomp(&editor.search_regex_compiled, query, regex_flags);
			if (rc != 0) {
				char regex_error[STATUS_MESSAGE_SIZE];
				regerror(rc, &editor.search_regex_compiled, regex_error, sizeof(regex_error));
				editor_set_status_message("[regex error] %s", regex_error);
				return;
			}
			editor.search_regex_valid = 1;
		}
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

	/* Track whether the search wrapped past the end or start of
	 * the file so we can show a [Wrapped] indicator. */
	int wrapped = 0;
	int start_line = current;

	for (int i = 0; i < editor.line_count; i++) {
		if (i == 0 && current != -1) {
			/* First iteration: try to find another match on the
			 * same line before moving to the next one. */
		} else {
			current += editor.search_direction;
			search_offset = -1;
		}
		if (current == -1) {
			current = editor.line_count - 1;
			wrapped = 1;
		} else if (current == editor.line_count) {
			current = 0;
			wrapped = 1;
		}

		/* Detect wrap when we cross the starting line */
		if (i > 0 && !wrapped) {
			if (editor.search_direction == 1 && current < start_line)
				wrapped = 1;
			else if (editor.search_direction == -1 && current > start_line)
				wrapped = 1;
		}

		struct line *ln = &editor.lines[current];

		/* For COLD lines with a valid mmap, search directly in the
		 * mapped bytes to avoid allocating and freeing a buffer per
		 * line. This makes search on large unedited files essentially
		 * zero-allocation. Uses memmem() instead of strstr() because
		 * the mmap region is not null-terminated per line. */
		size_t byte_len;
		const char *render;
		int allocated = 0;

		/* Regex search always needs a null-terminated string, so
		 * skip the mmap fast path when regex is enabled. */
		if (ln->temperature == LINE_COLD && editor.mmap_base
		    && !editor.search_regex_enabled) {
			render = editor.mmap_base + ln->mmap_offset;
			byte_len = ln->mmap_length;
		} else {
			line_ensure_warm(ln);
			render = line_to_bytes(ln, &byte_len);
			if (render == NULL)
				continue;
			allocated = 1;
		}
		int case_flag = editor.search_case_insensitive;
		size_t match_length = query_len;
		char *match = editor_find_on_line(render, byte_len, query, query_len,
						  editor.search_direction, search_offset,
						  case_flag, &match_length);

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
			if (warm_render == NULL)
				break;

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

			/* Count codepoints in match for highlight range */
			int match_cell_count = 0;
			size_t qpos = 0;
			while (qpos < match_length) {
				uint32_t cp;
				int consumed = utf8_decode(&match[qpos], (int)(match_length - qpos), &cp);
				if (consumed <= 0) consumed = 1;
				qpos += consumed;
				match_cell_count++;
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
			for (int k = 0; k < match_cell_count && cell_index + k < (int)ln->cell_count; k++)
				ln->cells[cell_index + k].syntax = HL_MATCH;

			/* Count total matches and determine current index */
			editor_count_search_matches(query, current, match_byte_offset);

			/* Build the status message with match count and
			 * optional wrap indicator. */
			char match_status[STATUS_MESSAGE_SIZE];
			if (wrapped)
				snprintf(match_status, sizeof(match_status),
					 "[Wrapped] Match %d of %d",
					 editor.search_current_match_index,
					 editor.search_total_matches);
			else
				snprintf(match_status, sizeof(match_status),
					 "Match %d of %d",
					 editor.search_current_match_index,
					 editor.search_total_matches);
			if (editor.search_regex_enabled)
				editor_set_status_message("[regex] %s", match_status);
			else
				editor_set_status_message("%s", match_status);

			free(warm_render);
			break;
		}
		if (allocated)
			free((char *)render);
	}
}

/* Adds a query to the search history ring, skipping if it duplicates
 * the most recent entry. Shifts older entries out when the ring is full. */
void editor_search_history_add(const char *query)
{
	if (!query || query[0] == '\0')
		return;

	/* Skip duplicates of the most recent entry */
	if (editor.search_history_count > 0) {
		char *last = editor.search_history[editor.search_history_count - 1];
		if (last && strcmp(last, query) == 0)
			return;
	}

	if (editor.search_history_count >= SEARCH_HISTORY_MAX) {
		free(editor.search_history[0]);
		memmove(&editor.search_history[0], &editor.search_history[1],
			sizeof(char *) * (SEARCH_HISTORY_MAX - 1));
		editor.search_history_count--;
	}

	editor.search_history[editor.search_history_count] = strdup(query);
	if (editor.search_history[editor.search_history_count] == NULL)
		return;
	editor.search_history_count++;
}

/* Called when the user accepts a search result (Enter). Saves the query
 * to the history, clears the highlight query, frees regex state, and
 * leaves the cursor at the match position. */
void editor_find_accept(char *query)
{
	editor_search_history_add(query);
	free(editor.search_highlight_query);
	editor.search_highlight_query = NULL;
	if (editor.search_regex_valid) {
		regfree(&editor.search_regex_compiled);
		editor.search_regex_valid = 0;
	}
	free(editor.search_history_stash);
	editor.search_history_stash = NULL;
	free(query);
}

/* Called when the user cancels a search (ESC). Restores the cursor and
 * viewport to the position saved before the search started. Clears
 * the highlight query and frees regex state. */
void editor_find_cancel(void)
{
	editor.force_full_redraw = 1;
	editor.cursor_x = editor.saved_cursor_x;
	editor.cursor_y = editor.saved_cursor_y;
	editor.column_offset = editor.saved_column_offset;
	editor.row_offset = editor.saved_row_offset;
	free(editor.search_highlight_query);
	editor.search_highlight_query = NULL;
	if (editor.search_regex_valid) {
		regfree(&editor.search_regex_compiled);
		editor.search_regex_valid = 0;
	}
	free(editor.search_history_stash);
	editor.search_history_stash = NULL;
}

/* Starts an incremental search. Saves cursor/viewport state and opens
 * the search prompt. Resets the history browse index so up/down arrows
 * cycle through previous queries. */
void editor_find_start(void)
{
	editor.saved_cursor_x = editor.cursor_x;
	editor.saved_cursor_y = editor.cursor_y;
	editor.saved_column_offset = editor.column_offset;
	editor.saved_row_offset = editor.row_offset;
	editor.search_last_match = -1;
	editor.search_last_match_offset = -1;
	editor.search_direction = 1;
	editor.search_history_browse_index = -1;
	free(editor.search_history_stash);
	editor.search_history_stash = NULL;
	prompt_open("Search: %s (ESC/Enter, Alt+C=case, Alt+X=regex)",
		    editor_find_callback, editor_find_accept, editor_find_cancel);
}

/* Finds the next match for the current replace query starting from the
 * cursor position. Positions the cursor at the match and returns 1 if
 * found, 0 if no more matches exist. */
int editor_replace_find_next(void)
{
	if (!editor.replace_query)
		return 0;
	size_t query_length = strlen(editor.replace_query);
	if (query_length == 0)
		return 0;
	int case_flag = editor.search_case_insensitive;

	/* Search from the current cursor position forward, wrapping once. */
	int start_line = editor.cursor_y;
	int start_col = editor.cursor_x;
	for (int i = 0; i < editor.line_count; i++) {
		int line_index = (start_line + i) % editor.line_count;
		struct line *ln = &editor.lines[line_index];
		size_t byte_length;
		char *bytes = line_to_bytes(ln, &byte_length);
		if (bytes == NULL)
			continue;

		/* On the starting line, skip bytes before the cursor so we
		 * don't re-find a match we have already processed. */
		int byte_start = 0;
		if (i == 0) {
			int cells_counted = 0;
			size_t byte_position = 0;
			while (cells_counted < start_col && byte_position < byte_length) {
				uint32_t codepoint;
				int consumed = utf8_decode(&bytes[byte_position], (int)(byte_length - byte_position), &codepoint);
				if (consumed <= 0) consumed = 1;
				byte_position += consumed;
				cells_counted++;
			}
			byte_start = (int)byte_position;
		}

		char *match = editor_memmem(bytes + byte_start, byte_length - byte_start, editor.replace_query, query_length, case_flag);
		if (match) {
			int match_byte_offset = (int)(match - bytes);
			/* Convert byte offset to cell index */
			int cell_index = 0;
			size_t byte_position = 0;
			while ((int)byte_position < match_byte_offset && (uint32_t)cell_index < ln->cell_count) {
				uint32_t codepoint;
				int consumed = utf8_decode(&bytes[byte_position], (int)(byte_length - byte_position), &codepoint);
				if (consumed <= 0) consumed = 1;
				byte_position += consumed;
				cell_index++;
			}
			editor.cursor_y = line_index;
			editor.cursor_x = cell_index;

			/* Center the match on screen */
			int screen_middle = editor.screen_rows / 2;
			editor.row_offset = editor.cursor_y - screen_middle;
			if (editor.row_offset < 0)
				editor.row_offset = 0;
			int max_offset = editor.line_count - editor.screen_rows;
			if (max_offset < 0)
				max_offset = 0;
			if (editor.row_offset > max_offset)
				editor.row_offset = max_offset;

			/* Highlight the match cells */
			int query_cell_count = 0;
			size_t qpos = 0;
			while (qpos < query_length) {
				uint32_t codepoint;
				int consumed = utf8_decode(&editor.replace_query[qpos], (int)(query_length - qpos), &codepoint);
				if (consumed <= 0) consumed = 1;
				qpos += consumed;
				query_cell_count++;
			}
			line_ensure_warm(ln);
			for (int k = 0; k < query_cell_count && cell_index + k < (int)ln->cell_count; k++)
				ln->cells[cell_index + k].syntax = HL_MATCH;

			free(bytes);
			editor.force_full_redraw = 1;
			return 1;
		}
		free(bytes);
	}
	return 0;
}

/* Performs one replacement at the current cursor position: deletes
 * the matched cells and inserts the replacement text character by
 * character using editor_insert_char(). */
void editor_replace_perform_one(void)
{
	if (!editor.replace_query || !editor.replace_with)
		return;
	/* Count cells in the matched query */
	size_t query_length = strlen(editor.replace_query);
	int query_cell_count = 0;
	size_t qpos = 0;
	while (qpos < query_length) {
		uint32_t codepoint;
		int consumed = utf8_decode(&editor.replace_query[qpos], (int)(query_length - qpos), &codepoint);
		if (consumed <= 0) consumed = 1;
		qpos += consumed;
		query_cell_count++;
	}

	/* Delete the matched cells at cursor position */
	if (editor.cursor_y < editor.line_count) {
		struct line *ln = &editor.lines[editor.cursor_y];
		line_ensure_warm(ln);
		for (int k = 0; k < query_cell_count && editor.cursor_x < (int)ln->cell_count; k++)
			line_delete_cell(ln, (uint32_t)editor.cursor_x);
	}

	/* Insert replacement text character by character */
	size_t replace_length = strlen(editor.replace_with);
	size_t rpos = 0;
	while (rpos < replace_length) {
		uint32_t codepoint;
		int consumed = utf8_decode(&editor.replace_with[rpos], (int)(replace_length - rpos), &codepoint);
		if (consumed <= 0) { consumed = 1; codepoint = '?'; }
		editor_insert_char((int)codepoint);
		rpos += consumed;
	}
	editor.replace_count++;
	syntax_propagate(editor.cursor_y);
}

/* Handles the y/n/a/ESC confirmation during find-and-replace.
 * Called from MODE_CONFIRM for each keypress. */
void editor_replace_confirm(int key)
{
	if (key == 'y' || key == 'Y') {
		editor_replace_perform_one();
		if (editor_replace_find_next()) {
			confirm_open("Replace? (y)es (n)o (a)ll (ESC)cancel",
				     editor_replace_confirm);
		} else {
			editor_set_status_message("Replaced %d occurrence(s)",
						 editor.replace_count);
			free(editor.replace_query);
			editor.replace_query = NULL;
			free(editor.replace_with);
			editor.replace_with = NULL;
		}
	} else if (key == 'n' || key == 'N') {
		/* Skip: advance past the current match and find the next */
		size_t query_length = strlen(editor.replace_query);
		int query_cell_count = 0;
		size_t qpos = 0;
		while (qpos < query_length) {
			uint32_t codepoint;
			int consumed = utf8_decode(&editor.replace_query[qpos], (int)(query_length - qpos), &codepoint);
			if (consumed <= 0) consumed = 1;
			qpos += consumed;
			query_cell_count++;
		}
		editor.cursor_x += query_cell_count;
		if (editor_replace_find_next()) {
			confirm_open("Replace? (y)es (n)o (a)ll (ESC)cancel",
				     editor_replace_confirm);
		} else {
			editor_set_status_message("Replaced %d occurrence(s)",
						 editor.replace_count);
			free(editor.replace_query);
			editor.replace_query = NULL;
			free(editor.replace_with);
			editor.replace_with = NULL;
		}
	} else if (key == 'a' || key == 'A') {
		/* Replace all remaining matches */
		editor_replace_perform_one();
		while (editor_replace_find_next())
			editor_replace_perform_one();
		editor_set_status_message("Replaced %d occurrence(s)",
					 editor.replace_count);
		free(editor.replace_query);
		editor.replace_query = NULL;
		free(editor.replace_with);
		editor.replace_with = NULL;
	} else {
		/* ESC or any other key cancels */
		editor_set_status_message("Replace cancelled (%d replaced)",
					 editor.replace_count);
		free(editor.replace_query);
		editor.replace_query = NULL;
		free(editor.replace_with);
		editor.replace_with = NULL;
	}
}

/* Called when the user accepts the replacement string. Stores it and
 * begins the interactive find-and-replace confirmation loop. */
void editor_replace_with_accept(char *replacement)
{
	editor.replace_with = replacement;
	editor.replace_count = 0;
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	if (editor_replace_find_next()) {
		confirm_open("Replace? (y)es (n)o (a)ll (ESC)cancel",
			     editor_replace_confirm);
	} else {
		editor_set_status_message("No matches found");
		free(editor.replace_query);
		editor.replace_query = NULL;
		free(editor.replace_with);
		editor.replace_with = NULL;
	}
}

/* Called when the user cancels the replacement string prompt. */
void editor_replace_with_cancel(void)
{
	editor_set_status_message("Replace cancelled");
	free(editor.replace_query);
	editor.replace_query = NULL;
}

/* Called when the user accepts the search string for replace. Stores
 * it and opens a second prompt for the replacement text. */
void editor_replace_search_accept(char *query)
{
	editor.replace_query = query;
	prompt_open("Replace with: %s (ESC to cancel)", NULL,
		    editor_replace_with_accept, editor_replace_with_cancel);
}

/* Called when the user cancels the search prompt for replace. */
void editor_replace_search_cancel(void)
{
	editor_set_status_message("Replace cancelled");
}

/* Starts the find-and-replace flow by opening a search prompt. */
void editor_replace_start(void)
{
	prompt_open("Replace: %s (ESC to cancel)", NULL,
		    editor_replace_search_accept, editor_replace_search_cancel);
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

/*** Bracket matching ***/

/* Returns the matching bracket character for the given opener or closer.
 * Returns 0 if the character is not a bracket. */
uint32_t bracket_get_match(uint32_t codepoint)
{
	switch (codepoint) {
	case '(': return ')';
	case ')': return '(';
	case '[': return ']';
	case ']': return '[';
	case '{': return '}';
	case '}': return '{';
	default: return 0;
	}
}

/* Returns 1 if the codepoint is an opening bracket. */
int bracket_is_opener(uint32_t codepoint)
{
	return codepoint == '(' || codepoint == '[' || codepoint == '{';
}

/* Scans for the bracket matching the one at the cursor position. Skips
 * brackets inside strings and comments by checking cell.syntax. Uses a
 * depth counter and scans forward for openers, backward for closers.
 * Caps the scan at BRACKET_SCAN_MAX_LINES to avoid lag. Stores the
 * result in editor.bracket_match_line and editor.bracket_match_cell. */
void bracket_update_cursor_match(void)
{
	editor.bracket_match_line = -1;
	editor.bracket_match_cell = -1;

	if (editor.cursor_y >= editor.line_count)
		return;

	struct line *cursor_line = &editor.lines[editor.cursor_y];
	line_ensure_warm(cursor_line);

	if (editor.cursor_x >= (int)cursor_line->cell_count)
		return;

	struct cell *cursor_cell = &cursor_line->cells[editor.cursor_x];
	uint32_t bracket = cursor_cell->codepoint;
	uint32_t target = bracket_get_match(bracket);
	if (target == 0)
		return;

	/* Skip brackets that are inside strings or comments. */
	if (cursor_cell->syntax == HL_STRING
	    || cursor_cell->syntax == HL_COMMENT
	    || cursor_cell->syntax == HL_MLCOMMENT)
		return;

	int forward = bracket_is_opener(bracket);
	int depth = 1;
	int scan_line = editor.cursor_y;
	int scan_cell = editor.cursor_x;

	int lines_scanned = 0;

	while (lines_scanned < BRACKET_SCAN_MAX_LINES) {
		if (forward) {
			scan_cell++;
		} else {
			scan_cell--;
		}

		/* Move to next/previous line when off the edge. */
		while (scan_line >= 0 && scan_line < editor.line_count) {
			struct line *ln = &editor.lines[scan_line];
			line_ensure_warm(ln);
			if (scan_cell >= 0 && scan_cell < (int)ln->cell_count)
				break;

			if (forward) {
				scan_line++;
				scan_cell = 0;
			} else {
				scan_line--;
				if (scan_line >= 0) {
					line_ensure_warm(&editor.lines[scan_line]);
					scan_cell = (int)editor.lines[scan_line].cell_count - 1;
				}
			}
			lines_scanned++;
			if (lines_scanned >= BRACKET_SCAN_MAX_LINES)
				return;
		}

		if (scan_line < 0 || scan_line >= editor.line_count)
			return;

		struct line *ln = &editor.lines[scan_line];
		struct cell *c = &ln->cells[scan_cell];

		/* Skip brackets inside strings and comments. */
		if (c->syntax == HL_STRING || c->syntax == HL_COMMENT
		    || c->syntax == HL_MLCOMMENT) {
			continue;
		}

		if (c->codepoint == bracket) {
			depth++;
		} else if (c->codepoint == target) {
			depth--;
			if (depth == 0) {
				editor.bracket_match_line = scan_line;
				editor.bracket_match_cell = scan_cell;
				return;
			}
		}
	}
}

/* Jumps the cursor to the matching bracket position. Shows a message
 * if the cursor is not on a bracket or no match was found. */
void editor_jump_to_matching_bracket(void)
{
	bracket_update_cursor_match();
	if (editor.bracket_match_line < 0) {
		editor_set_status_message("No matching bracket");
		return;
	}
	editor.cursor_y = editor.bracket_match_line;
	editor.cursor_x = editor.bracket_match_cell;
	editor.preferred_column = -1;
}

/*** Block indent/dedent ***/

/* Indents all lines in the current selection by inserting one tab at the
 * start of each non-empty line. Adjusts cursor and selection bounds. */
void editor_indent_selection(void)
{
	editor.force_full_redraw = 1;
	int start_y, start_x, end_y, end_x;
	if (!selection_get_range(&start_y, &start_x, &end_y, &end_x))
		return;

	struct cell tab_cell = {
		.codepoint = '\t',
		.syntax = HL_NORMAL,
		.neighbor = 0,
		.flags = 0,
		.context = 0,
	};

	for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		line_ensure_warm(ln);

		/* Skip empty lines. */
		if (ln->cell_count == 0)
			continue;

		line_insert_cell(ln, 0, tab_cell);
		syntax_propagate(y);
	}

	/* Adjust cursor and selection bounds for the inserted tab. */
	if (editor.cursor_x > 0 || editor.cursor_y > start_y
	    || editor.cursor_y == start_y)
		editor.cursor_x++;
	if (editor.selection.anchor_x > 0
	    || editor.selection.anchor_y > start_y
	    || editor.selection.anchor_y == start_y)
		editor.selection.anchor_x++;

	selection_update();
	editor.dirty++;
}

/* Removes one level of indentation from all lines in the current selection.
 * Removes a leading tab, or up to EDIT_TAB_STOP leading spaces. */
void editor_dedent_selection(void)
{
	editor.force_full_redraw = 1;
	int start_y, start_x, end_y, end_x;
	if (!selection_get_range(&start_y, &start_x, &end_y, &end_x))
		return;

	for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		line_ensure_warm(ln);

		if (ln->cell_count == 0)
			continue;

		int removed = 0;
		if (ln->cells[0].codepoint == '\t') {
			line_delete_cell(ln, 0);
			removed = 1;
		} else {
			/* Remove up to EDIT_TAB_STOP leading spaces. */
			int spaces = 0;
			while (spaces < EDIT_TAB_STOP
			       && spaces < (int)ln->cell_count
			       && ln->cells[spaces].codepoint == ' ')
				spaces++;
			for (int i = 0; i < spaces; i++)
				line_delete_cell(ln, 0);
			removed = spaces;
		}

		if (removed > 0) {
			syntax_propagate(y);

			/* Adjust cursor position on this line. */
			if (y == editor.cursor_y) {
				editor.cursor_x -= removed;
				if (editor.cursor_x < 0)
					editor.cursor_x = 0;
			}
			/* Adjust anchor position on this line. */
			if (y == editor.selection.anchor_y) {
				editor.selection.anchor_x -= removed;
				if (editor.selection.anchor_x < 0)
					editor.selection.anchor_x = 0;
			}
		}
	}

	selection_update();
	editor.dirty++;
}

/*** Comment toggle ***/

/* Toggles line comments on the current line or all selected lines.
 * Uses the filetype's singleline_comment_start prefix to add or remove
 * comments. If all affected lines are commented, removes the prefix;
 * otherwise adds it. */
void editor_toggle_comment(void)
{
	editor.force_full_redraw = 1;

	if (!editor.syntax || !editor.syntax->singleline_comment_start) {
		editor_set_status_message("No comment style for this file type");
		return;
	}

	char *prefix = editor.syntax->singleline_comment_start;
	int prefix_length = (int)strlen(prefix);

	/* Determine affected line range. */
	int start_y = editor.cursor_y;
	int end_y = editor.cursor_y;
	if (editor.selection.active) {
		int sx, sy, ex, ey;
		selection_get_range(&sy, &sx, &ey, &ex);
		start_y = sy;
		end_y = ey;
	}

	/* Check if ALL affected non-empty lines start with the prefix. */
	int all_commented = 1;
	int has_nonempty = 0;
	for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
		struct line *ln = &editor.lines[y];
		line_ensure_warm(ln);

		if (ln->cell_count == 0)
			continue;
		has_nonempty = 1;

		/* Find the first non-whitespace cell. */
		int first_nonws = 0;
		while (first_nonws < (int)ln->cell_count
		       && (ln->cells[first_nonws].codepoint == ' '
		           || ln->cells[first_nonws].codepoint == '\t'))
			first_nonws++;

		/* Check if the prefix matches starting at first_nonws. */
		int match = 1;
		for (int i = 0; i < prefix_length; i++) {
			if (first_nonws + i >= (int)ln->cell_count
			    || ln->cells[first_nonws + i].codepoint != (uint32_t)prefix[i]) {
				match = 0;
				break;
			}
		}
		if (!match) {
			all_commented = 0;
			break;
		}
	}

	if (!has_nonempty)
		return;

	if (all_commented) {
		/* Remove the prefix (and one trailing space) from each line. */
		for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
			struct line *ln = &editor.lines[y];
			line_ensure_warm(ln);

			if (ln->cell_count == 0)
				continue;

			int first_nonws = 0;
			while (first_nonws < (int)ln->cell_count
			       && (ln->cells[first_nonws].codepoint == ' '
			           || ln->cells[first_nonws].codepoint == '\t'))
				first_nonws++;

			/* Remove the prefix characters. */
			int to_remove = prefix_length;
			/* Also remove one trailing space if present. */
			if (first_nonws + prefix_length < (int)ln->cell_count
			    && ln->cells[first_nonws + prefix_length].codepoint == ' ')
				to_remove++;

			for (int i = 0; i < to_remove; i++)
				line_delete_cell(ln, (uint32_t)first_nonws);

			/* Adjust cursor if on this line. */
			if (y == editor.cursor_y) {
				editor.cursor_x -= to_remove;
				if (editor.cursor_x < first_nonws)
					editor.cursor_x = first_nonws;
			}

			syntax_propagate(y);
		}
	} else {
		/* Insert prefix + space after leading whitespace on each line. */
		for (int y = start_y; y <= end_y && y < editor.line_count; y++) {
			struct line *ln = &editor.lines[y];
			line_ensure_warm(ln);

			if (ln->cell_count == 0)
				continue;

			int first_nonws = 0;
			while (first_nonws < (int)ln->cell_count
			       && (ln->cells[first_nonws].codepoint == ' '
			           || ln->cells[first_nonws].codepoint == '\t'))
				first_nonws++;

			/* Insert a space after the prefix. */
			struct cell space_cell = {
				.codepoint = ' ',
				.syntax = HL_NORMAL,
				.neighbor = 0,
				.flags = 0,
				.context = 0,
			};
			line_insert_cell(ln, (uint32_t)first_nonws, space_cell);

			/* Insert the prefix characters in reverse order at
			 * first_nonws so they end up in the correct order. */
			for (int i = prefix_length - 1; i >= 0; i--) {
				struct cell prefix_cell = {
					.codepoint = (uint32_t)prefix[i],
					.syntax = HL_NORMAL,
					.neighbor = 0,
					.flags = 0,
					.context = 0,
				};
				line_insert_cell(ln, (uint32_t)first_nonws, prefix_cell);
			}

			int inserted = prefix_length + 1;

			/* Adjust cursor if on this line. */
			if (y == editor.cursor_y && editor.cursor_x >= first_nonws)
				editor.cursor_x += inserted;

			syntax_propagate(y);
		}
	}

	editor.dirty++;
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
	if (editor.cursor_y < editor.row_offset) {
		editor.row_offset = editor.cursor_y;
	}
	if (editor.cursor_y >= editor.row_offset + editor.screen_rows) {
		editor.row_offset = editor.cursor_y - editor.screen_rows + 1;
	}
	int text_columns = editor.screen_columns - editor.line_number_width;

	if (editor.word_wrap) {
		/* With word wrap, horizontal scrolling is disabled. Instead,
		 * ensure the cursor line is visible by counting how many
		 * visual rows the lines between row_offset and cursor_y
		 * occupy. If they exceed the screen, bump row_offset. */
		editor.column_offset = 0;
		while (editor.row_offset < editor.cursor_y) {
			int visual_rows = 0;
			for (int i = editor.row_offset; i <= editor.cursor_y && i < editor.line_count; i++) {
				int width = line_render_width(&editor.lines[i]);
				int rows = (text_columns > 0 && width > text_columns)
					   ? (width + text_columns - 1) / text_columns
					   : 1;
				visual_rows += rows;
			}
			if (visual_rows <= editor.screen_rows)
				break;
			editor.row_offset++;
		}
	} else {
		if (editor.render_x < editor.column_offset) {
			editor.column_offset = editor.render_x;
		}
		if (editor.render_x >= editor.column_offset + text_columns) {
			editor.column_offset = editor.render_x - text_columns + 1;
		}
	}
}

/* Scrolls the editor view by the given number of rows in the specified
 * direction (ARROW_UP or ARROW_DOWN). Keeps the cursor centered on screen
 * when possible, and handles boundary conditions at the top and bottom
 * of the file. Used by mouse scroll wheel with acceleration. */
void editor_scroll_rows(int scroll_direction, int scroll_amount)
{
	int screen_middle = editor.screen_rows / 2;

	if (scroll_direction == ARROW_UP) {

		/* Clamp the scroll amount so the cursor stays within file bounds. */
		if (editor.cursor_y - scroll_amount < 0) {
			scroll_amount = editor.cursor_y;
		}

		/* Move the cursor up if it is below the screen center. */
		if (editor.cursor_y > editor.row_offset + screen_middle) {
			editor.cursor_y -= scroll_amount;
		}

		/* If scrolling up would move the cursor past (above) center,
		 * stop moving the cursor and start scrolling the viewport down
		 * instead. This gradually pulls the cursor's screen position
		 * toward center, mirroring the scroll-down behavior. */
		if (editor.cursor_y - scroll_amount < editor.row_offset + screen_middle) {

			/* Don't adjust the viewport if we're already at the top of
			 * the file — there's nowhere to scroll down to. */
			if (editor.row_offset != 0) {

				/* Cursor is already above center (clicked in top half).
				 * Keep the cursor on its file line and slide the viewport
				 * down toward it. Clamp the viewport shift to the
				 * distance between center and cursor so we don't
				 * overshoot past the cursor. */
				if (editor.cursor_y < editor.row_offset + screen_middle) {
					int middle_offset = (editor.row_offset + screen_middle) - editor.cursor_y;
					if (middle_offset < scroll_amount) {
						scroll_amount = middle_offset;
					}
					editor.row_offset -= scroll_amount;

				/* Cursor is at or below center but would cross it.
				 * Snap the cursor to center so the next scroll tick
				 * enters the pinned-at-center state below. */
				} else {
					editor.cursor_y = editor.row_offset + screen_middle;
				}
			}
		}

		/* When the cursor is exactly at center, scroll both the viewport and
		 * cursor together to keep the cursor pinned to center. */
		if (editor.cursor_y == editor.row_offset + screen_middle) {
			if (editor.row_offset - scroll_amount >= 0) {
				editor.cursor_y = editor.row_offset + screen_middle - scroll_amount;
				editor.row_offset -= scroll_amount;
			}
		}

		/* At top of file, let the cursor move past center toward the first row. */
		if (editor.row_offset == 0) {
			editor.cursor_y -= scroll_amount;
		}

	} else if (scroll_direction == ARROW_DOWN) {

		/* Clamp the scroll amount so the cursor stays within file bounds. */
		if (editor.cursor_y + scroll_amount > editor.line_count) {
			scroll_amount = editor.line_count - editor.cursor_y;
		}

		/* Move the cursor down if it is above the screen center. */
		if (editor.cursor_y < editor.row_offset + screen_middle) {
			editor.cursor_y += scroll_amount;
		}

		/* When the cursor would scroll past the center, lock it to center
		 * and scroll the viewport instead. Clamp scroll_amount to the
		 * distance between the cursor and center to prevent jumping. */
		if (editor.cursor_y + scroll_amount > editor.row_offset + screen_middle) {

			/* Only scroll the viewport if we haven't reached the end of file. */
			if (editor.row_offset + editor.screen_rows != editor.line_count + 1) {

				if (editor.cursor_y > editor.row_offset + screen_middle) {
					int middle_offset = editor.cursor_y - (editor.row_offset + screen_middle);
					if (middle_offset < scroll_amount) {
						scroll_amount = middle_offset;
					}
					editor.row_offset += scroll_amount;
				} else {
					editor.cursor_y = editor.row_offset + screen_middle;
				}
			}
		}

		/* When the cursor is exactly at center, scroll both the viewport and
		 * cursor together to keep the cursor pinned to center. */
		if (editor.cursor_y == editor.row_offset + screen_middle) {
			if (editor.row_offset + editor.screen_rows + scroll_amount <=
					editor.line_count + 1) {
				editor.cursor_y = editor.row_offset + screen_middle + scroll_amount;
				editor.row_offset += scroll_amount;
			}
		}

		/* At end of file, let the cursor move past center toward the last row. */
		if (editor.row_offset + editor.screen_rows == editor.line_count + 1) {
			editor.cursor_y += scroll_amount;
		}
	}
	/* Final bounds clamp */
	if (editor.cursor_y < 0)
		editor.cursor_y = 0;
	if (editor.cursor_y > editor.line_count)
		editor.cursor_y = editor.line_count;
}

/* Processes pending scroll events accumulated during the event loop.
 * Called once per frame at the start of editor_refresh_screen().
 * Scrolls exactly 1 line per frame at base speed regardless of how
 * many events the terminal batched. Acceleration ramps up when
 * consecutive frames contain scroll events. */
void editor_process_pending_scroll(void)
{
	int pending_up = editor.scroll_pending_up;
	int pending_down = editor.scroll_pending_down;
	editor.scroll_pending_up = 0;
	editor.scroll_pending_down = 0;

	if (pending_up == 0 && pending_down == 0)
		return;

	/* Determine scroll direction. If both directions present
	 * (unlikely), use whichever had more events. */
	int direction, pending;
	if (pending_up >= pending_down) {
		direction = ARROW_UP;
		pending = pending_up;
	} else {
		direction = ARROW_DOWN;
		pending = pending_down;
	}
	(void)pending;

	/* Measure time since last frame with scrolling to detect
	 * whether the user is scrolling continuously. */
	struct timeval now;
	gettimeofday(&now, NULL);
	long elapsed =
		(now.tv_sec - editor.scroll_last_frame_time.tv_sec) *
			MICROSECONDS_PER_SECOND +
		(now.tv_usec - editor.scroll_last_frame_time.tv_usec);
	editor.scroll_last_frame_time = now;

	if (elapsed < SCROLL_ACCELERATION_FAST_US) {
		if (editor.scroll_consecutive_frames < SCROLL_SPEED_MAX)
			editor.scroll_consecutive_frames++;
	} else if (elapsed > SCROLL_DECELERATION_SLOW_US) {
		editor.scroll_consecutive_frames = 0;
	}

	/* Base speed is always 1 line. Acceleration kicks in
	 * immediately and ramps aggressively. */
	int multiplier;
	if (editor.scroll_consecutive_frames <= 1)
		multiplier = 1;
	else
		multiplier = (editor.scroll_consecutive_frames - 1) * 4;
	if (multiplier > SCROLL_SPEED_MAX)
		multiplier = SCROLL_SPEED_MAX;

	editor_scroll_rows(direction, multiplier);
}

/* Highlights all occurrences of the search query in a line by setting
 * matching cells to HL_MATCH. When regex mode is active, uses regexec()
 * on the byte string; otherwise scans with editor_memmem(). Stores
 * original syntax values into saved_syntax (must be pre-allocated with
 * at least line->cell_count entries). Returns the number of cells
 * whose syntax was modified so the caller can restore them later. */
int editor_highlight_search_matches(struct line *line, uint16_t *saved_syntax)
{
	if (!editor.search_highlight_query || editor.search_highlight_query[0] == '\0')
		return 0;

	line_ensure_warm(line);
	int modified = 0;

	/* Save original syntax values */
	for (uint32_t i = 0; i < line->cell_count; i++)
		saved_syntax[i] = line->cells[i].syntax;

	size_t byte_length;
	char *bytes = line_to_bytes(line, &byte_length);
	if (bytes == NULL)
		return 0;

	/* Build byte-to-cell mapping */
	int *byte_to_cell = malloc(sizeof(int) * (byte_length + 1));
	if (byte_to_cell == NULL) {
		free(bytes);
		return 0;
	}
	size_t byte_position = 0;
	int cell_index = 0;
	while (byte_position < byte_length && (uint32_t)cell_index < line->cell_count) {
		byte_to_cell[byte_position] = cell_index;
		uint32_t codepoint;
		int consumed = utf8_decode(&bytes[byte_position], (int)(byte_length - byte_position), &codepoint);
		if (consumed <= 0)
			consumed = 1;
		for (int k = 1; k < consumed && byte_position + k < byte_length; k++)
			byte_to_cell[byte_position + k] = cell_index;
		byte_position += consumed;
		cell_index++;
	}
	byte_to_cell[byte_length] = cell_index;

	size_t query_length = strlen(editor.search_highlight_query);
	int case_flag = editor.search_case_insensitive;

	if (editor.search_regex_enabled && editor.search_regex_valid) {
		/* Regex search path */
		regmatch_t match_info;
		size_t offset = 0;
		while (offset < byte_length) {
			int result = regexec(&editor.search_regex_compiled,
					     bytes + offset, 1, &match_info, 0);
			if (result != 0 || match_info.rm_so < 0)
				break;
			size_t match_start = offset + (size_t)match_info.rm_so;
			size_t match_end = offset + (size_t)match_info.rm_eo;
			if (match_start >= byte_length)
				break;
			/* Zero-length match: advance by one byte to avoid infinite loop */
			if (match_end == match_start) {
				offset = match_start + 1;
				continue;
			}
			int start_cell = (match_start < byte_length) ? byte_to_cell[match_start] : cell_index;
			int end_cell = (match_end <= byte_length) ? byte_to_cell[match_end] : cell_index;
			for (int k = start_cell; k < end_cell && (uint32_t)k < line->cell_count; k++) {
				line->cells[k].syntax = HL_MATCH;
				modified = 1;
			}
			offset = match_end;
		}
	} else {
		/* Literal search path */
		const char *search_position = bytes;
		size_t remaining = byte_length;
		while (remaining >= query_length) {
			char *found = editor_memmem(search_position, remaining,
						    editor.search_highlight_query,
						    query_length, case_flag);
			if (!found)
				break;
			size_t match_byte_offset = (size_t)(found - bytes);
			size_t match_end_offset = match_byte_offset + query_length;
			int start_cell = byte_to_cell[match_byte_offset];
			int end_cell = (match_end_offset <= byte_length)
				? byte_to_cell[match_end_offset] : cell_index;
			for (int k = start_cell; k < end_cell && (uint32_t)k < line->cell_count; k++) {
				line->cells[k].syntax = HL_MATCH;
				modified = 1;
			}
			search_position = found + 1;
			remaining = byte_length - (size_t)(search_position - bytes);
		}
	}

	free(byte_to_cell);
	free(bytes);
	return modified;
}

/* Returns true if the codepoint is an opening or closing bracket character
 * that participates in bracket pair colorization. */
static int cell_is_bracket(uint32_t codepoint)
{
	return codepoint == '(' || codepoint == ')' ||
	       codepoint == '[' || codepoint == ']' ||
	       codepoint == '{' || codepoint == '}';
}

/* Returns true if the codepoint is an opening bracket. */
static int cell_is_opening_bracket(uint32_t codepoint)
{
	return codepoint == '(' || codepoint == '[' || codepoint == '{';
}

/* Returns the bracket color from the theme for the given nesting depth.
 * Cycles through keyword1, keyword2, string, and number colors. */
static char *bracket_color_for_depth(int depth)
{
	switch (depth % BRACKET_COLOR_COUNT) {
	case 0:  return editor.theme.keyword1;
	case 1:  return editor.theme.keyword2;
	case 2:  return editor.theme.string;
	default: return editor.theme.number;
	}
}

/* Renders all visible rows to the append buffer, including line numbers,
 * syntax-highlighted text, and the welcome message for empty files. Applies
 * trailing whitespace visualization, horizontal scroll indicators, and
 * bracket pair colorization based on nesting depth. */
void editor_draw_rows(struct append_buffer *append_buffer)
{
	/* Bracket depth starts at 0 at the top of the viewport.
	 * This is intentionally simple — colors may be wrong if there
	 * are unmatched brackets above the viewport, but most code files
	 * have balanced brackets per function. */
	int bracket_depth = editor.bracket_depth_at_viewport;

	/* When word wrap is on, a logical line may span multiple screen
	 * rows. Track the current file line and cell offset separately
	 * so continuation rows resume where the previous row stopped. */
	int file_row_index = editor.row_offset;
	int wrap_cell_offset = 0;

	int screen_row;
	for (screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
		int file_row = file_row_index;
		int has_right_indicator = 0;

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
			/* Line numbers and git gutter marker.
			 * Continuation rows from word wrap get a blank gutter. */
			int is_continuation = (editor.word_wrap && wrap_cell_offset > 0);
			if (editor.line_number_width > 0) {
				if (is_continuation) {
					for (int pad = 0; pad < editor.line_number_width; pad++)
						append_buffer_write(append_buffer, " ", 1);
				} else {
					append_buffer_write_color(
							append_buffer,
							editor.theme.line_number);
					char line_number_string[LINE_NUMBER_BUFFER_SIZE];
					snprintf(line_number_string, sizeof(line_number_string),
								 "%*d", editor.line_number_width - 1, file_row + 1);
					append_buffer_write(append_buffer, line_number_string, strlen(line_number_string));

					/* Render gutter marker in place of the separator space. */
					int marker = editor.lines[file_row].gutter_marker;
					if (marker == GUTTER_ADDED) {
						append_buffer_write_color(append_buffer, editor.theme.keyword2);
						append_buffer_write(append_buffer, "+", 1);
					} else if (marker == GUTTER_MODIFIED) {
						append_buffer_write_color(append_buffer, editor.theme.number);
						append_buffer_write(append_buffer, "~", 1);
					} else {
						append_buffer_write(append_buffer, " ", 1);
					}
				}

				append_buffer_write_color(
						append_buffer, editor.theme.foreground);
			}

			struct line *ln = &editor.lines[file_row];
			line_ensure_warm(ln);

			/* When a search is active, highlight all matching
			 * cells on this line. Save their original syntax so
			 * we can restore it after rendering. */
			uint16_t *search_saved = NULL;
			int search_modified = 0;
			if (editor.search_highlight_query) {
				search_saved = malloc(sizeof(uint16_t) * (ln->cell_count + 1));
				if (search_saved)
					search_modified = editor_highlight_search_matches(ln, search_saved);
			}

			int render_width = line_render_width(ln);
			int text_columns = editor.screen_columns - editor.line_number_width;
			int visible_length;
			if (editor.word_wrap) {
				visible_length = text_columns;
			} else {
				visible_length = render_width - editor.column_offset;
				if (visible_length < 0)
					visible_length = 0;
				if (visible_length > text_columns)
					visible_length = text_columns;
			}

			/* Compute trailing whitespace start index for this line.
			 * Scan backward from the end to find the first non-whitespace
			 * cell. Only applies to lines with actual content. */
			int trailing_start = (int)ln->cell_count;
			if (ln->cell_count > 0) {
				int scan = (int)ln->cell_count - 1;
				while (scan >= 0) {
					uint32_t scan_cp = ln->cells[scan].codepoint;
					if (scan_cp != ' ' && scan_cp != '\t')
						break;
					scan--;
				}
				/* Only mark trailing whitespace if the line has
				 * non-whitespace content (not a blank line). */
				if (scan >= 0)
					trailing_start = scan + 1;
			}

			/* Determine whether horizontal scroll indicators are needed.
			 * A left indicator shows when content is scrolled off-screen
			 * to the left; a right indicator when content extends past
			 * the visible area to the right. */
			int has_left_indicator = (!editor.word_wrap
						  && editor.column_offset > 0
						  && ln->cell_count > 0);
			has_right_indicator = (!editor.word_wrap
					      && render_width >
					      editor.column_offset + text_columns);

			/* Render cells with tab expansion and UTF-8 encoding.
			 * Iterates by grapheme cluster to correctly handle
			 * multi-codepoint sequences (flags, ZWJ emoji, etc). */
			char *current_color = NULL;
			uint32_t ci = (uint32_t)wrap_cell_offset;
			int col = (wrap_cell_offset > 0)
				   ? line_cell_to_render_column(ln, wrap_cell_offset)
				   : 0;
			int output_col = 0;
			while (ci < ln->cell_count && output_col < visible_length) {
				uint32_t cp = ln->cells[ci].codepoint;
				uint16_t hl = ln->cells[ci].syntax;

				/* Highlight the matching bracket with HL_MATCH. */
				if (file_row == editor.bracket_match_line
				    && (int)ci == editor.bracket_match_cell)
					hl = HL_MATCH;

				/* Check if this cell is trailing whitespace */
				int in_trailing = ((int)ci >= trailing_start
						   && (cp == ' ' || cp == '\t'));

				/* Determine bracket color override for this cell.
				 * Only colorize brackets outside strings, comments,
				 * and search matches. */
				char *bracket_override = NULL;
				if (cell_is_bracket(cp)
				    && hl != HL_STRING && hl != HL_COMMENT
				    && hl != HL_MLCOMMENT && hl != HL_MATCH) {
					if (cell_is_opening_bracket(cp)) {
						bracket_override = bracket_color_for_depth(bracket_depth);
						bracket_depth++;
					} else {
						if (bracket_depth > 0)
							bracket_depth--;
						bracket_override = bracket_color_for_depth(bracket_depth);
					}
				}

				if (cp == '\t') {
					int cw = cell_display_width(&ln->cells[ci], col);
					/* Expand tab to spaces */
					for (int t = 0; t < cw && output_col < visible_length; t++) {
						if (col >= editor.column_offset) {
							/* Apply trailing whitespace background tint */
							if (in_trailing) {
								append_buffer_write_background(
									append_buffer,
									editor.theme.line_number);
							} else if (editor.ruler_column > 0
								   && col == editor.ruler_column) {
								/* Ruler column background tint */
								append_buffer_write_background(
									append_buffer,
									editor.theme.line_number);
							}
							/* Apply match background for search hits */
							if (hl == HL_MATCH) {
								append_buffer_write_background(
									append_buffer,
									editor.theme.match_background);
							}
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
							/* Left scroll indicator: override first visible
							 * character position with '<' */
							if (has_left_indicator && output_col == 0) {
								append_buffer_write_color(
									append_buffer,
									editor.theme.line_number);
								append_buffer_write(append_buffer, "<", 1);
								current_color = NULL;
							/* Right scroll indicator: override last visible
							 * character position with '>' */
							} else if (has_right_indicator
								   && output_col == text_columns - 1) {
								append_buffer_write_color(
									append_buffer,
									editor.theme.line_number);
								append_buffer_write(append_buffer, ">", 1);
								current_color = NULL;
							} else {
								append_buffer_write(append_buffer, " ", 1);
							}
							output_col++;
							/* Restore background after match highlight */
							if (hl == HL_MATCH && !in_trailing) {
								if (file_row == editor.cursor_y)
									append_buffer_write_background(
										append_buffer,
										editor.theme.highlight_background);
								else
									append_buffer_write_background(
										append_buffer,
										editor.theme.background);
							}
							/* Restore normal background after trailing ws */
							if (in_trailing) {
								if (file_row == editor.cursor_y)
									append_buffer_write_background(
										append_buffer,
										editor.theme.highlight_background);
								else
									append_buffer_write_background(
										append_buffer,
										editor.theme.background);
							} else if (editor.ruler_column > 0
								   && col == editor.ruler_column) {
								/* Restore background after ruler tint */
								if (file_row == editor.cursor_y)
									append_buffer_write_background(
										append_buffer,
										editor.theme.highlight_background);
								else
									append_buffer_write_background(
										append_buffer,
										editor.theme.background);
							}
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
						/* Apply trailing whitespace background tint */
						if (in_trailing) {
							append_buffer_write_background(
								append_buffer,
								editor.theme.line_number);
						}
						/* Apply match background for search hits */
						if (hl == HL_MATCH && !in_trailing) {
							append_buffer_write_background(
								append_buffer,
								editor.theme.match_background);
						}
						/* Ruler column background tint for
						 * non-trailing, non-match cells. */
						if (editor.ruler_column > 0
						    && col == editor.ruler_column
						    && !in_trailing
						    && hl != HL_MATCH) {
							append_buffer_write_background(
								append_buffer,
								editor.theme.line_number);
						}
						/* Selection: use terminal reverse video */
						int selected = ln->cells[ci].flags & CELL_FLAG_SELECTED;
						if (selected) {
							append_buffer_write(append_buffer,
								INVERT_COLOR, strlen(INVERT_COLOR));
						} else if (bracket_override != NULL) {
							/* Bracket pair colorization overrides normal syntax */
							if (bracket_override != current_color) {
								current_color = bracket_override;
								append_buffer_write_color(
									append_buffer,
									bracket_override);
							}
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
						/* Left scroll indicator: show '<' at first visible column */
						if (has_left_indicator && output_col == 0) {
							append_buffer_write_color(
								append_buffer,
								editor.theme.line_number);
							append_buffer_write(append_buffer, "<", 1);
							current_color = NULL;
						/* Right scroll indicator: show '>' at last visible column */
						} else if (has_right_indicator
							   && output_col >= text_columns - 1) {
							append_buffer_write_color(
								append_buffer,
								editor.theme.line_number);
							append_buffer_write(append_buffer, ">", 1);
							current_color = NULL;
						} else {
							/* Output all codepoints in the cluster */
							for (int gi = (int)ci; gi < grapheme_end; gi++) {
								char utf8_buf[UTF8_MAX_BYTES];
								int utf8_len = utf8_encode(
										ln->cells[gi].codepoint, utf8_buf);
								append_buffer_write(append_buffer, utf8_buf, utf8_len);
							}
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
						/* Restore background after match highlight */
						if (hl == HL_MATCH && !in_trailing && !selected) {
							if (file_row == editor.cursor_y)
								append_buffer_write_background(
									append_buffer,
									editor.theme.highlight_background);
							else
								append_buffer_write_background(
									append_buffer,
									editor.theme.background);
						}
						/* Restore background after trailing whitespace */
						if (in_trailing && !selected) {
							if (file_row == editor.cursor_y)
								append_buffer_write_background(
									append_buffer,
									editor.theme.highlight_background);
							else
								append_buffer_write_background(
									append_buffer,
									editor.theme.background);
						}
						/* Restore background after ruler tint */
						if (editor.ruler_column > 0
						    && col == editor.ruler_column
						    && !in_trailing && !selected
						    && hl != HL_MATCH) {
							if (file_row == editor.cursor_y)
								append_buffer_write_background(
									append_buffer,
									editor.theme.highlight_background);
							else
								append_buffer_write_background(
									append_buffer,
									editor.theme.background);
						}
					}
					output_col += cw;
				}
				col += cw;
				ci = (uint32_t)grapheme_end;
			}

			/* Restore original syntax values after rendering if
			 * search highlighting was applied to this line. */
			if (search_modified && search_saved) {
				for (uint32_t k = 0; k < ln->cell_count; k++)
					ln->cells[k].syntax = search_saved[k];
			}
			free(search_saved);

			append_buffer_write_color(
					append_buffer, editor.theme.foreground);

			/* Draw ruler character in empty space past end of text.
			 * If the ruler column falls within the visible area
			 * but beyond the rendered content, pad with spaces to
			 * that position and draw a faint vertical bar. */
			if (editor.ruler_column > 0) {
				int ruler_screen_col = editor.ruler_column
						       - editor.column_offset;
				if (ruler_screen_col >= output_col
				    && ruler_screen_col < text_columns) {
					/* Pad to the ruler position. */
					int pad = ruler_screen_col - output_col;
					for (int p = 0; p < pad; p++)
						append_buffer_write(
							append_buffer, " ", 1);
					/* Draw faint vertical bar. */
					append_buffer_write_color(
						append_buffer,
						editor.theme.line_number);
					/* U+2502 BOX DRAWINGS LIGHT VERTICAL
					 * encoded as 3 UTF-8 bytes. */
					append_buffer_write(
						append_buffer,
						"\xe2\x94\x82", 3);
					append_buffer_write_color(
						append_buffer,
						editor.theme.foreground);
				}
			}

			/* Advance to next line or continue wrapping */
			if (editor.word_wrap && ci < ln->cell_count) {
				/* More cells remain -- continue this line
				 * on the next screen row. */
				wrap_cell_offset = (int)ci;
			} else {
				file_row_index++;
				wrap_cell_offset = 0;
			}
		}
		append_buffer_write(append_buffer, CLEAR_LINE, strlen(CLEAR_LINE));

		/* Right scroll indicator: draw '>' at the last screen column
		 * AFTER CLEAR_LINE so it can't be erased. Uses a cursor move
		 * to position precisely at the rightmost column. */
		/* Right scroll indicator: draw '>' at the last screen
		 * column after CLEAR_LINE. Uses a cursor move to position
		 * at the second-to-last column for compatibility with
		 * terminals that don't render at the final column. */
		if (has_right_indicator) {
			char move_buf[CURSOR_BUFFER_SIZE];
			snprintf(move_buf, sizeof(move_buf),
				 "\x1b[%d;%dH", screen_row + 1,
				 editor.screen_columns - 1);
			append_buffer_write(append_buffer, move_buf,
					    strlen(move_buf));
			append_buffer_write_color(append_buffer,
						  editor.theme.line_number);
			append_buffer_write(append_buffer, ">", 1);
		}

		append_buffer_write(append_buffer, CRLF, strlen(CRLF));
	}
}

/* Renders the status bar showing the filename, dirty indicator, filetype,
 * cursor position, and file percentage. Uses proportional filename
 * truncation and highlights the dirty indicator in the match color. */
void editor_draw_status_bar(struct append_buffer *append_buffer)
{
	append_buffer_write_background(
			append_buffer,
			editor.theme.status_bar);
	append_buffer_write_color(
			append_buffer,
			editor.theme.status_bar_text);

	/* Build the right side first so we know how much space is available
	 * for the left side. Format: "line:col | N%" */
	char right_status[STATUS_BUFFER_SIZE];
	char position_indicator[STATUS_BUFFER_SIZE];
	if (editor.line_count == 0 || editor.cursor_y == 0) {
		snprintf(position_indicator, sizeof(position_indicator), "Top");
	} else if (editor.cursor_y >= editor.line_count - 1) {
		snprintf(position_indicator, sizeof(position_indicator), "Bot");
	} else {
		int percent = ((editor.cursor_y + 1) * 100) / editor.line_count;
		snprintf(position_indicator, sizeof(position_indicator), "%d%%", percent);
	}
	int right_status_length = snprintf(
			right_status, sizeof(right_status),
			"%d:%d | %s",
			editor.cursor_y + 1, editor.cursor_x + 1,
			position_indicator);

	/* Determine the filetype label for display. */
	const char *filetype_label = editor.syntax
		? editor.syntax->filetype : "text";

	/* Compute the read-only, dirty indicator, and filetype suffix. */
	const char *read_only_text = editor.read_only ? " [RO]" : "";
	int read_only_length = (int)strlen(read_only_text);
	const char *dirty_text = editor.dirty ? " [+]" : "";
	int dirty_length = (int)strlen(dirty_text);
	char filetype_suffix[STATUS_BUFFER_SIZE];
	int filetype_suffix_length = snprintf(filetype_suffix,
		sizeof(filetype_suffix), " %s", filetype_label);

	/* Calculate available width for the filename. Reserve space for
	 * the read-only indicator, dirty indicator, filetype, a gap,
	 * and the right side. */
	int reserved = read_only_length + dirty_length + filetype_suffix_length + 1 + right_status_length;
	int filename_max = editor.screen_columns - reserved;
	if (filename_max < 0)
		filename_max = 0;

	/* Render the filename with proportional truncation. When the name
	 * is too long, show "...tail" keeping the end of the path. */
	const char *filename = editor.filename ? editor.filename : "[No Name]";
	int filename_length = (int)strlen(filename);
	int output_col = 0;

	if (filename_length <= filename_max) {
		append_buffer_write(append_buffer, filename, filename_length);
		output_col += filename_length;
	} else if (filename_max > 3) {
		/* Show "..." followed by as much of the tail as fits. */
		append_buffer_write(append_buffer, "...", 3);
		int tail_length = filename_max - 3;
		append_buffer_write(append_buffer,
			filename + filename_length - tail_length, tail_length);
		output_col += filename_max;
	} else {
		/* Extremely narrow: just show what fits. */
		append_buffer_write(append_buffer, filename,
			filename_max > filename_length ? filename_length : filename_max);
		output_col += (filename_max > filename_length
			? filename_length : filename_max);
	}

	/* Render the read-only indicator between filename and dirty flag. */
	if (editor.read_only && output_col + read_only_length <= editor.screen_columns) {
		append_buffer_write(append_buffer, read_only_text, read_only_length);
		output_col += read_only_length;
	}

	/* Render the dirty indicator in the match/number color to stand out. */
	if (editor.dirty && output_col + dirty_length <= editor.screen_columns) {
		append_buffer_write_color(append_buffer, editor.theme.number);
		append_buffer_write(append_buffer, dirty_text, dirty_length);
		append_buffer_write_color(append_buffer, editor.theme.status_bar_text);
		output_col += dirty_length;
	} else if (!editor.dirty) {
		/* No dirty indicator needed */
	}

	/* Render the filetype suffix. */
	if (output_col + filetype_suffix_length <= editor.screen_columns) {
		append_buffer_write(append_buffer, filetype_suffix,
			filetype_suffix_length);
		output_col += filetype_suffix_length;
	}

	/* Fill the gap and render the right-aligned status. */
	while (output_col < editor.screen_columns) {
		if (editor.screen_columns - output_col == right_status_length) {
			append_buffer_write_color(
				append_buffer, editor.theme.status_bar_text);
			append_buffer_write(append_buffer,
				right_status, right_status_length);
			break;
		} else {
			append_buffer_write(append_buffer, " ", 1);
			output_col++;
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

/* Computes the cursor's screen row when word wrap is active. Counts the
 * visual rows consumed by lines from row_offset up to (but not including)
 * cursor_y, plus any wrap rows within the cursor line itself. */
int editor_cursor_screen_row(void)
{
	int text_columns = editor.screen_columns - editor.line_number_width;
	if (text_columns <= 0)
		text_columns = 1;
	int visual_row = 0;
	for (int i = editor.row_offset; i < editor.cursor_y && i < editor.line_count; i++) {
		int width = line_render_width(&editor.lines[i]);
		int rows = (width > text_columns)
			   ? (width + text_columns - 1) / text_columns
			   : 1;
		visual_row += rows;
	}
	/* Add the wrap row offset within the cursor line itself */
	int cursor_render_col = editor.render_x;
	visual_row += cursor_render_col / text_columns;
	return visual_row;
}

/* Redraws the screen. When only the cursor moved since the last frame,
 * emits just a cursor reposition escape (~12 bytes) instead of redrawing
 * every visible line (~20KB). Falls through to a full redraw when content,
 * viewport, status bar, or selection state changed. */
void editor_refresh_screen(void)
{
	/* Process batched scroll events before adjusting the viewport. */
	editor_process_pending_scroll();

	editor_scroll();
	bracket_update_cursor_match();

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
		|| message_visible != prev_message_visible
		|| (editor.word_wrap && editor.render_x != editor.prev_render_x);

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
		int cursor_screen_row, cursor_screen_col;
		if (editor.word_wrap) {
			int text_cols = editor.screen_columns - editor.line_number_width;
			if (text_cols <= 0)
				text_cols = 1;
			cursor_screen_row = editor_cursor_screen_row() + 1;
			cursor_screen_col = (editor.render_x % text_cols) + editor.line_number_width + 1;
		} else {
			cursor_screen_row = (editor.cursor_y - editor.row_offset) + 1;
			cursor_screen_col = (editor.render_x - editor.column_offset) + editor.line_number_width + 1;
		}
		char cursor_buffer[CURSOR_BUFFER_SIZE];
		snprintf(cursor_buffer, sizeof(cursor_buffer), CURSOR_MOVE,
			 cursor_screen_row, cursor_screen_col);
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

	int cursor_screen_row, cursor_screen_col;
	if (editor.word_wrap) {
		int text_cols = editor.screen_columns - editor.line_number_width;
		if (text_cols <= 0)
			text_cols = 1;
		cursor_screen_row = editor_cursor_screen_row() + 1;
		cursor_screen_col = (editor.render_x % text_cols) + editor.line_number_width + 1;
	} else {
		cursor_screen_row = (editor.cursor_y - editor.row_offset) + 1;
		cursor_screen_col = (editor.render_x - editor.column_offset) + editor.line_number_width + 1;
	}
	char cursor_buffer[CURSOR_BUFFER_SIZE];
	snprintf(cursor_buffer, sizeof(cursor_buffer), CURSOR_MOVE,
					 cursor_screen_row, cursor_screen_col);
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

/* Frees cells for WARM lines that have scrolled far off-screen, returning
 * them to COLD state. This keeps memory proportional to the viewport
 * rather than growing with the total file size. Only WARM lines are
 * cooled -- HOT lines contain user edits that would be lost. Lines
 * inside an active selection are also skipped. Scans at most
 * COOL_SCAN_LINES_PER_FRAME lines per call, cycling through the file
 * gradually to avoid O(line_count) work every frame. */
void editor_cool_distant_lines(void)
{
	if (editor.line_count == 0 || editor.mmap_base == NULL)
		return;

	int distance_threshold = editor.screen_rows * 3;
	int viewport_start = editor.row_offset;
	int viewport_end = editor.row_offset + editor.screen_rows;

	/* Determine the selection range so we can skip those lines. */
	int selection_start = -1;
	int selection_end = -1;
	if (editor.selection.active) {
		int sel_sy, sel_sx, sel_ey, sel_ex;
		if (selection_get_range(&sel_sy, &sel_sx, &sel_ey, &sel_ex)) {
			selection_start = sel_sy;
			selection_end = sel_ey;
		}
	}

	int scan_start = editor.cool_scan_position;
	if (scan_start >= editor.line_count)
		scan_start = 0;

	int scanned = 0;
	int position = scan_start;
	while (scanned < COOL_SCAN_LINES_PER_FRAME && scanned < editor.line_count) {
		struct line *ln = &editor.lines[position];

		/* Only cool WARM lines that are far from the viewport. */
		if (ln->temperature == LINE_WARM) {
			int distance_from_viewport;
			if (position < viewport_start)
				distance_from_viewport = viewport_start - position;
			else if (position >= viewport_end)
				distance_from_viewport = position - viewport_end;
			else
				distance_from_viewport = 0;

			int in_selection = (selection_start >= 0
					    && position >= selection_start
					    && position <= selection_end);

			if (distance_from_viewport > distance_threshold && !in_selection) {
				free(ln->cells);
				ln->cells = NULL;
				ln->cell_count = 0;
				ln->cell_capacity = 0;
				ln->temperature = LINE_COLD;
				ln->cached_render_width = -1;
				ln->syntax_stale = 1;
			}
		}

		position++;
		if (position >= editor.line_count)
			position = 0;
		scanned++;
	}

	editor.cool_scan_position = position;
}

/* Pre-warms lines just beyond the viewport in the scroll direction so
 * they are ready to render when they come into view. Compares the
 * current row_offset against the previous frame's value to determine
 * scroll direction, then warms up to half a screen of lines ahead.
 * Only warms COLD lines; WARM and HOT lines already have cells. */
void editor_prefetch_lines(void)
{
	if (editor.line_count == 0)
		return;

	int direction = editor.row_offset - editor.prev_row_offset_for_prefetch;
	editor.prev_row_offset_for_prefetch = editor.row_offset;

	if (direction == 0)
		return;

	int prefetch_count = editor.screen_rows / 2;
	if (prefetch_count < 1)
		prefetch_count = 1;

	int start, end;
	if (direction > 0) {
		/* Scrolling down: warm lines beyond the bottom of the viewport */
		start = editor.row_offset + editor.screen_rows;
		end = start + prefetch_count;
	} else {
		/* Scrolling up: warm lines above the top of the viewport */
		end = editor.row_offset;
		start = end - prefetch_count;
	}

	if (start < 0)
		start = 0;
	if (end > editor.line_count)
		end = editor.line_count;

	for (int i = start; i < end; i++) {
		if (editor.lines[i].temperature == LINE_COLD)
			line_ensure_warm(&editor.lines[i]);
	}
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
/* Replaces the prompt buffer contents with the given string, updating
 * the buffer length and capacity as needed. Used by search history
 * browsing to swap in a previous query. */
void prompt_set_buffer(const char *text)
{
	size_t length = strlen(text);
	while (length >= editor.prompt.buffer_capacity - 1) {
		editor.prompt.buffer_capacity *= 2;
		editor.prompt.buffer = realloc(editor.prompt.buffer,
					       editor.prompt.buffer_capacity);
		if (editor.prompt.buffer == NULL)
			terminal_die("realloc");
	}
	memcpy(editor.prompt.buffer, text, length);
	editor.prompt.buffer[length] = '\0';
	editor.prompt.buffer_length = length;
}

void prompt_handle_key(struct input_event event)
{
	int key = event.key;
	int is_search = (editor.prompt.per_key_callback == editor_find_callback);

	/* Alt+C during search toggles case sensitivity. Only active when
	 * the per-key callback is the incremental search handler. */
	if (key == ALT_KEY('c') && is_search) {
		editor.search_case_insensitive = !editor.search_case_insensitive;
		editor_set_status_message("Search: %s [case %s]",
					 editor.prompt.buffer,
					 editor.search_case_insensitive ? "off" : "on");
		/* Re-run the search with the new case setting */
		if (editor.prompt.per_key_callback)
			editor.prompt.per_key_callback(editor.prompt.buffer, key);
		return;
	}
	/* Alt+X during search toggles regex mode. */
	if (key == ALT_KEY('x') && is_search) {
		editor.search_regex_enabled = !editor.search_regex_enabled;
		editor_set_status_message("Search: %s [regex %s]",
					 editor.prompt.buffer,
					 editor.search_regex_enabled ? "on" : "off");
		/* Re-run the search with the new regex setting */
		if (editor.prompt.per_key_callback)
			editor.prompt.per_key_callback(editor.prompt.buffer, key);
		return;
	}
	/* Up/Down arrows during search browse through search history.
	 * The current typed text is stashed so the user can return to
	 * it by pressing down past the end of the history. */
	if ((key == ARROW_UP || key == ARROW_DOWN) && is_search) {
		if (editor.search_history_count > 0) {
			if (key == ARROW_UP) {
				if (editor.search_history_browse_index == -1) {
					/* Stash the current typed text before
					 * entering history browsing. */
					free(editor.search_history_stash);
					editor.search_history_stash = strdup(editor.prompt.buffer);
					editor.search_history_browse_index = editor.search_history_count - 1;
				} else if (editor.search_history_browse_index > 0) {
					editor.search_history_browse_index--;
				}
				prompt_set_buffer(editor.search_history[editor.search_history_browse_index]);
			} else {
				if (editor.search_history_browse_index >= 0 &&
				    editor.search_history_browse_index < editor.search_history_count - 1) {
					editor.search_history_browse_index++;
					prompt_set_buffer(editor.search_history[editor.search_history_browse_index]);
				} else if (editor.search_history_browse_index == editor.search_history_count - 1) {
					/* Past the end: restore the stashed text */
					editor.search_history_browse_index = -1;
					if (editor.search_history_stash)
						prompt_set_buffer(editor.search_history_stash);
					else
						prompt_set_buffer("");
				}
			}
		}
		/* Re-run the search with the new buffer contents */
		editor.search_last_match = -1;
		editor.search_last_match_offset = -1;
		if (editor.prompt.per_key_callback)
			editor.prompt.per_key_callback(editor.prompt.buffer, key);
		editor_set_status_message(editor.prompt.format, editor.prompt.buffer);
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
		if (editor.preferred_column == -1 && current_line)
			editor.preferred_column =
				line_cell_to_render_column(
					current_line, editor.cursor_x);

		if (editor.word_wrap && current_line) {
			int text_cols = editor.screen_columns -
					editor.line_number_width;
			if (text_cols < 1) text_cols = 1;
			int render_col = line_cell_to_render_column(
				current_line, editor.cursor_x);
			int visual_row = render_col / text_cols;
			if (visual_row > 0) {
				/* Move up within the same wrapped line.
				 * Set preferred_column to the target render
				 * column so the post-switch restore puts
				 * the cursor in the right place. */
				int col_in_row = editor.preferred_column >= 0
					? editor.preferred_column % text_cols
					: render_col % text_cols;
				editor.preferred_column =
					(visual_row - 1) * text_cols +
					col_in_row;
				break;
			}
		}
		if (editor.cursor_y != 0)
			editor.cursor_y--;
		break;

	case ALT_KEY('j'):
	case ARROW_DOWN:
		if (editor.preferred_column == -1 && current_line)
			editor.preferred_column =
				line_cell_to_render_column(
					current_line, editor.cursor_x);

		if (editor.word_wrap && current_line) {
			int text_cols = editor.screen_columns -
					editor.line_number_width;
			if (text_cols < 1) text_cols = 1;
			int render_col = line_cell_to_render_column(
				current_line, editor.cursor_x);
			int render_width_val = line_render_width(current_line);
			int visual_row = render_col / text_cols;
			int total_rows = (render_width_val + text_cols - 1) /
					 text_cols;
			if (total_rows < 1) total_rows = 1;
			if (visual_row < total_rows - 1) {
				/* Move down within the same wrapped line.
				 * Set preferred_column to the target render
				 * column so the post-switch restore puts
				 * the cursor in the right place. */
				int col_in_row = editor.preferred_column >= 0
					? editor.preferred_column % text_cols
					: render_col % text_cols;
				int target = (visual_row + 1) * text_cols +
					     col_in_row;
				if (target > render_width_val)
					target = render_width_val;
				editor.preferred_column = target;
				break;
			}
		}
		if (editor.cursor_y < editor.line_count)
			editor.cursor_y++;
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
		case ALT_KEY('t'): case ALT_KEY('n'): case ALT_KEY('w'):
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
	case ALT_KEY('w'):
		editor_toggle_word_wrap();
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

	case ALT_KEY('r'):
		editor_replace_start();
		break;

	case ALT_KEY(']'):
		editor_jump_to_matching_bracket();
		break;

	case ALT_KEY('/'):
		editor_toggle_comment();
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
		editor.scroll_pending_up++;
		break;

	case MOUSE_SCROLL_DOWN:
		editor.scroll_pending_down++;
		break;

	/* Tab indents the selection when text is selected; otherwise
	 * falls through to the default handler to insert a tab. */
	case '\t':
		if (editor.selection.active) {
			editor_indent_selection();
			break;
		}
		editor_insert_char(key);
		break;

	/* Shift+Tab dedents the selection when text is selected. */
	case SHIFT_TAB:
		if (editor.selection.active)
			editor_dedent_selection();
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
		if (key >= 32 && key != BACKSPACE) {
			if (editor.selection.active)
				selection_delete();
			editor_insert_char(key);
		}
		break;
	}

}



/*** Configuration ***/

/* Maximum path length for config file paths. */
#define CONFIG_PATH_MAX 1024

/* Maximum length of a single line in the config file. */
#define CONFIG_LINE_MAX 256

/* Minimum allowed tab stop value. */
#define TAB_STOP_MIN 1

/* Maximum allowed tab stop value. */
#define TAB_STOP_MAX 32

/* Maximum allowed ruler column value. */
#define RULER_COLUMN_MAX 999

/* Temporary file suffix for atomic config writes. */
#define CONFIG_TEMP_SUFFIX ".tmp"

/* Builds the config file path into the provided buffer. Checks
 * $XDG_CONFIG_HOME/edit/config first, then falls back to
 * $HOME/.config/edit/config. Returns 0 on success, -1 if neither
 * environment variable is set. */
static int config_build_path(char *path_out, size_t path_size)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0] != '\0') {
		snprintf(path_out, path_size, "%s/edit/config", xdg);
		return 0;
	}
	const char *home = getenv("HOME");
	if (home && home[0] != '\0') {
		snprintf(path_out, path_size, "%s/.config/edit/config", home);
		return 0;
	}
	return -1;
}

/* Builds the config directory path into the provided buffer. Returns 0
 * on success, -1 if neither XDG_CONFIG_HOME nor HOME is set. */
static int config_build_directory(char *dir_out, size_t dir_size)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0] != '\0') {
		snprintf(dir_out, dir_size, "%s/edit", xdg);
		return 0;
	}
	const char *home = getenv("HOME");
	if (home && home[0] != '\0') {
		snprintf(dir_out, dir_size, "%s/.config/edit", home);
		return 0;
	}
	return -1;
}

/* Creates a directory and all missing parent components, similar to
 * mkdir -p. Returns 0 on success, -1 on failure. */
static int config_mkdir_parents(const char *path)
{
	char working_path[CONFIG_PATH_MAX];
	snprintf(working_path, sizeof(working_path), "%s", path);
	for (char *separator = working_path + 1; *separator; separator++) {
		if (*separator == '/') {
			*separator = '\0';
			if (mkdir(working_path, 0755) == -1 && errno != EEXIST)
				return -1;
			*separator = '/';
		}
	}
	if (mkdir(working_path, 0755) == -1 && errno != EEXIST)
		return -1;
	return 0;
}

/* Reads the config file and applies key=value settings to the editor state.
 * Skips blank lines and comments starting with '#'. Unknown keys are
 * silently ignored. Shows the first parse error in the status bar. */
void config_load(void)
{
	char config_path[CONFIG_PATH_MAX];
	if (config_build_path(config_path, sizeof(config_path)) == -1)
		return;

	FILE *config_file = fopen(config_path, "r");
	if (!config_file)
		return;

	char line_buffer[CONFIG_LINE_MAX];
	int line_number = 0;
	int first_error_line = 0;
	char first_error_message[STATUS_MESSAGE_SIZE];
	first_error_message[0] = '\0';

	while (fgets(line_buffer, sizeof(line_buffer), config_file)) {
		line_number++;

		/* Strip trailing newline and carriage return. */
		char *newline = strchr(line_buffer, '\n');
		if (newline)
			*newline = '\0';
		newline = strchr(line_buffer, '\r');
		if (newline)
			*newline = '\0';

		/* Skip blank lines and comments. */
		char *cursor = line_buffer;
		while (*cursor == ' ' || *cursor == '\t')
			cursor++;
		if (*cursor == '\0' || *cursor == '#')
			continue;

		/* Find the '=' separator. */
		char *equals = strchr(cursor, '=');
		if (!equals) {
			if (first_error_line == 0) {
				first_error_line = line_number;
				snprintf(first_error_message,
					 sizeof(first_error_message),
					 "Config error line %d: missing '='",
					 line_number);
			}
			continue;
		}

		/* Extract and trim the key. */
		char *key_end = equals - 1;
		while (key_end > cursor
		       && (*key_end == ' ' || *key_end == '\t'))
			key_end--;
		int key_length = (int)(key_end - cursor) + 1;
		char key[CONFIG_LINE_MAX];
		memcpy(key, cursor, (size_t)key_length);
		key[key_length] = '\0';

		/* Extract and trim the value. */
		char *value_start = equals + 1;
		while (*value_start == ' ' || *value_start == '\t')
			value_start++;
		char *value_end = value_start + strlen(value_start) - 1;
		while (value_end > value_start
		       && (*value_end == ' ' || *value_end == '\t'))
			value_end--;
		int value_length = (int)(value_end - value_start) + 1;
		if (value_start > value_end)
			value_length = 0;
		char value[CONFIG_LINE_MAX];
		if (value_length > 0)
			memcpy(value, value_start, (size_t)value_length);
		value[value_length] = '\0';

		/* Apply known configuration keys. */
		if (strcmp(key, "tabstop") == 0) {
			int tab_value = atoi(value);
			if (tab_value >= TAB_STOP_MIN
			    && tab_value <= TAB_STOP_MAX) {
				editor.tab_stop = tab_value;
			} else if (first_error_line == 0) {
				first_error_line = line_number;
				snprintf(first_error_message,
					 sizeof(first_error_message),
					 "Config line %d: tabstop %d-%d",
					 line_number, TAB_STOP_MIN,
					 TAB_STOP_MAX);
			}
		} else if (strcmp(key, "theme") == 0) {
			int theme_count = (int)(sizeof(editor_themes)
					/ sizeof(editor_themes[0]));
			int found = 0;
			for (int i = 0; i < theme_count; i++) {
				if (strcmp(editor_themes[i].name,
					  value) == 0) {
					current_theme_index = i;
					editor_set_theme(i);
					found = 1;
					break;
				}
			}
			if (!found && first_error_line == 0) {
				first_error_line = line_number;
				snprintf(first_error_message,
					 sizeof(first_error_message),
					 "Config line %d: unknown theme",
					 line_number);
			}
		} else if (strcmp(key, "line_numbers") == 0) {
			if (strcmp(value, "true") == 0) {
				editor.show_line_numbers = 1;
			} else if (strcmp(value, "false") == 0) {
				editor.show_line_numbers = 0;
			} else if (first_error_line == 0) {
				first_error_line = line_number;
				snprintf(first_error_message,
					 sizeof(first_error_message),
					 "Config line %d: "
					 "line_numbers must be true/false",
					 line_number);
			}
		} else if (strcmp(key, "ruler") == 0) {
			int ruler_value = atoi(value);
			if (ruler_value >= 0
			    && ruler_value <= RULER_COLUMN_MAX) {
				editor.ruler_column = ruler_value;
			} else if (first_error_line == 0) {
				first_error_line = line_number;
				snprintf(first_error_message,
					 sizeof(first_error_message),
					 "Config line %d: ruler 0-%d",
					 line_number, RULER_COLUMN_MAX);
			}
		}
		/* Unknown keys are silently ignored. */
	}

	fclose(config_file);

	/* Update gutter width in case show_line_numbers changed. */
	editor_update_gutter_width();

	/* Show the first parse error as a status message at startup. */
	if (first_error_line > 0)
		editor_set_status_message("%s", first_error_message);
}

/* Saves the current theme name to the config file. Creates the config
 * directory if it does not exist. If the config file already contains a
 * theme line, it is replaced in-place. Otherwise the theme setting is
 * appended. Uses atomic write (temp file + rename) to avoid corruption. */
void config_save_theme(const char *theme_name)
{
	char config_directory[CONFIG_PATH_MAX];
	if (config_build_directory(config_directory,
				   sizeof(config_directory)) == -1)
		return;

	/* Ensure the config directory exists. */
	if (config_mkdir_parents(config_directory) == -1)
		return;

	char config_path[CONFIG_PATH_MAX];
	if (config_build_path(config_path, sizeof(config_path)) == -1)
		return;

	/* Extra room for the ".tmp" suffix beyond the config path. */
	char temp_path[CONFIG_PATH_MAX + sizeof(CONFIG_TEMP_SUFFIX)];
	snprintf(temp_path, sizeof(temp_path), "%s%s",
		 config_path, CONFIG_TEMP_SUFFIX);

	/* Read the existing config file if present. */
	FILE *existing = fopen(config_path, "r");
	FILE *output = fopen(temp_path, "w");
	if (!output) {
		if (existing)
			fclose(existing);
		return;
	}

	int theme_written = 0;
	if (existing) {
		char line_buffer[CONFIG_LINE_MAX];
		while (fgets(line_buffer, sizeof(line_buffer), existing)) {
			/* Check if this line sets the theme. */
			char *cursor = line_buffer;
			while (*cursor == ' ' || *cursor == '\t')
				cursor++;
			if (strncmp(cursor, "theme", 5) == 0) {
				char *after_key = cursor + 5;
				while (*after_key == ' '
				       || *after_key == '\t')
					after_key++;
				if (*after_key == '=') {
					fprintf(output, "theme = %s\n",
						theme_name);
					theme_written = 1;
					continue;
				}
			}
			fputs(line_buffer, output);
		}
		fclose(existing);
	}

	/* Append theme line if none existed in the original file. */
	if (!theme_written)
		fprintf(output, "theme = %s\n", theme_name);

	fclose(output);

	/* Atomic rename to replace the config file safely. */
	rename(temp_path, config_path);
}

/*** True Color Detection ***/

/* Checks environment variables to determine if the terminal supports
 * 24-bit true color rendering. Looks at COLORTERM for "truecolor" or
 * "24bit", then falls back to checking TERM for terminal types known
 * to support true color (256color, kitty, alacritty). */
void terminal_detect_true_color(void)
{
	const char *colorterm = getenv("COLORTERM");
	if (colorterm) {
		if (strcmp(colorterm, "truecolor") == 0
		    || strcmp(colorterm, "24bit") == 0) {
			editor.true_color = 1;
			return;
		}
	}

	const char *term = getenv("TERM");
	if (term) {
		if (strstr(term, "256color") != NULL
		    || strstr(term, "kitty") != NULL
		    || strstr(term, "alacritty") != NULL) {
			editor.true_color = 1;
			return;
		}
	}

	editor.true_color = 0;
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
	editor.word_wrap = 0;
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
	editor.tab_stop = EDIT_TAB_STOP;
	editor.true_color = 0;
	editor.ruler_column = 0;
	editor_set_theme(current_theme_index);

	/* Load config file after defaults are set so it can override them. */
	config_load();

	/* Detect true color terminal support from environment variables. */
	terminal_detect_true_color();

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

	editor.scroll_pending_up = 0;
	editor.scroll_pending_down = 0;
	gettimeofday(&editor.scroll_last_frame_time, NULL);
	editor.scroll_consecutive_frames = 0;
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
	editor.search_case_insensitive = 0;
	editor.search_highlight_query = NULL;
	editor.search_total_matches = 0;
	editor.search_current_match_index = 0;
	editor.search_regex_enabled = 0;
	editor.search_regex_valid = 0;
	editor.search_history_count = 0;
	editor.search_history_browse_index = -1;
	editor.search_history_stash = NULL;
	for (int i = 0; i < SEARCH_HISTORY_MAX; i++)
		editor.search_history[i] = NULL;

	editor.replace_query = NULL;
	editor.replace_with = NULL;
	editor.replace_count = 0;

	editor.preferred_column = -1;
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
	editor.bracket_match_line = -1;
	editor.bracket_match_cell = -1;
	editor.binary_file_warning = 0;
	editor.prev_row_offset_for_prefetch = 0;
	editor.cool_scan_position = 0;
	editor.swap_file_path = NULL;
	editor.last_swap_write_time = 0;
	editor.file_mtime = (struct timespec){0};
	editor.file_device = 0;
	editor.file_inode = 0;
	editor.read_only = 0;
	editor.lock_file_descriptor = -1;
}

/* Reads all data from stdin into a malloc'd buffer. Used when stdin is
 * a pipe or when "-" is passed as the filename. Stores the total number
 * of bytes read in *out_length. Returns the buffer (caller frees), or
 * NULL on failure. */
char *editor_read_stdin_pipe(size_t *out_length)
{
	size_t capacity = 4096;
	size_t total = 0;
	char *buffer = malloc(capacity);
	if (!buffer)
		return NULL;
	while (1) {
		if (total + 1024 > capacity) {
			capacity *= 2;
			buffer = realloc(buffer, capacity);
			if (!buffer)
				return NULL;
		}
		ssize_t bytes_read = read(STDIN_FILENO, buffer + total, capacity - total);
		if (bytes_read <= 0)
			break;
		total += (size_t)bytes_read;
	}
	*out_length = total;
	return buffer;
}

/* Entry point. Initializes the editor, checks for piped stdin, sets up
 * the terminal (mouse reporting, raw mode), optionally opens a file from
 * the command line, and enters a poll()-based main loop. Input is buffered
 * and decoded without blocking, so paste and resize are handled efficiently.
 *
 * Startup order matters: editor_init() must run before raw mode because
 * it queries the terminal size. When stdin is a pipe, we must read all
 * piped data and reopen /dev/tty BEFORE entering raw mode, since
 * tcgetattr() fails on a pipe file descriptor. */
#ifndef EDIT_TESTING
int main(int argc, char *argv[])
{
	atexit(editor_quit);

	/* Initialize editor state first (needs terminal queries via ioctl,
	 * which work on stdout even when stdin is a pipe). */
	editor_init();

	/* Parse command line flags before opening any file. Flags use
	 * --key=value syntax. Non-flag arguments are treated as filenames. */
	char *filename_arg = NULL;
	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--tabstop=", 10) == 0) {
			int tab_value = atoi(argv[i] + 10);
			if (tab_value >= TAB_STOP_MIN
			    && tab_value <= TAB_STOP_MAX)
				editor.tab_stop = tab_value;
		} else if (strncmp(argv[i], "--ruler=", 8) == 0) {
			int ruler_value = atoi(argv[i] + 8);
			if (ruler_value >= 0
			    && ruler_value <= RULER_COLUMN_MAX)
				editor.ruler_column = ruler_value;
		} else {
			/* First non-flag argument is the filename. */
			filename_arg = argv[i];
		}
	}

	/* Detect piped stdin: either stdin is not a terminal, or the user
	 * passed "-" as the filename argument. Read everything from the
	 * pipe into the editor buffer, then reconnect stdin to /dev/tty
	 * so raw mode and keyboard input work normally. */
	int stdin_is_pipe = !isatty(STDIN_FILENO);
	int arg_is_dash = (filename_arg && strcmp(filename_arg, "-") == 0);
	if (stdin_is_pipe || arg_is_dash) {
		if (stdin_is_pipe) {
			size_t pipe_length;
			char *pipe_data = editor_read_stdin_pipe(&pipe_length);
			if (pipe_data && pipe_length > 0) {
				/* Parse piped data into editor lines */
				const char *pos = pipe_data;
				const char *end = pipe_data + pipe_length;
				while (pos < end) {
					const char *newline = memchr(pos, '\n', (size_t)(end - pos));
					if (!newline)
						newline = end;
					size_t line_length = (size_t)(newline - pos);
					/* Strip trailing carriage return */
					if (line_length > 0 && pos[line_length - 1] == '\r')
						line_length--;
					editor_line_insert(editor.line_count, pos, line_length);
					pos = (newline < end) ? newline + 1 : end;
				}
				free(pipe_data);
				editor.dirty = 1;
				editor_update_gutter_width();
			}
		}

		/* Reopen /dev/tty as stdin so raw mode and keyboard input work */
		int tty_fd = open("/dev/tty", O_RDONLY);
		if (tty_fd == -1) {
			write(STDOUT_FILENO, "Cannot open /dev/tty\r\n", 22);
			_exit(1);
		}
		dup2(tty_fd, STDIN_FILENO);
		close(tty_fd);
	}

	/* Enable mouse reporting (non-fatal if it fails) */
	if (terminal_enable_mouse_reporting() == -1) {
		/* Mouse support unavailable, continue without it */
	}

	/* Enable raw mode */
	terminal_enable_raw_mode();

	/* Open a file from the command line, unless we already loaded
	 * piped data or the argument was "-". */
	if (filename_arg && !arg_is_dash && !stdin_is_pipe) {
		editor_open(filename_arg);
		editor_check_read_only();
		file_acquire_lock();
		cursor_history_restore();
	} else if (filename_arg && arg_is_dash && !stdin_is_pipe) {
		/* "-" was passed but stdin was a terminal -- just start empty */
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

		/* After rendering, do background maintenance: cool distant
		 * lines to reclaim memory, and prefetch lines in the scroll
		 * direction so they are warm before coming into view. */
		editor_cool_distant_lines();
		editor_prefetch_lines();

		/* Periodically write a swap file for crash recovery. */
		swap_file_write();

		/* Use a bounded timeout so the swap file writer gets a
		 * chance to run even when the user is idle. The interval
		 * matches the swap write period (in milliseconds). */
		struct pollfd stdin_poll = {STDIN_FILENO, POLLIN, 0};
		int poll_timeout = SWAP_WRITE_INTERVAL_SECONDS * 1000;
		if (poll(&stdin_poll, 1, poll_timeout) == -1) {
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
#endif /* EDIT_TESTING */
