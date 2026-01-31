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

/* Number of spaces used to render a tab character. */
#define EDIT_TAB_STOP 8
/* Maps a letter key to its Ctrl+key equivalent by masking the upper bits. */
#define CTRL_KEY(k) ((k) & 0x1f)

/* Encodes an Alt+key combination by adding an offset above special keys. */
#define ALT_KEY(k) ((k) + 2000)

/* Special key codes returned by terminal_read_key(). Values above 127 avoid
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
	F11_KEY
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
};

/* ASCII escape character used to begin terminal escape sequences. */
#define ESC_KEY '\x1b'

/* ANSI escape sequence to clear the entire screen. */
#define CLEAR_SCREEN "\x1b[2J"
/* ANSI escape sequence to move the cursor to the top-left corner. */
#define CURSOR_HOME "\x1b[H"
/* Enables SGR mouse reporting and basic mouse tracking in the terminal. */
#define ENABLE_MOUSE_REPORTING "\x1b[?1006h\x1b[?1000h"
/* Disables SGR mouse reporting and basic mouse tracking. */
#define DISABLE_MOUSE_REPORTING "\x1b[?1006l\x1b[?1000l"
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
	/* Characters that begin a single-line comment (e.g., "//"). */
	char *singleline_comment_start;
	/* Characters that begin a multi-line comment. */
	char *multiline_comment_start;
	/* Characters that end a multi-line comment. */
	char *multiline_comment_end;
	/* Bit flags controlling highlighting behavior (HL_HIGHLIGHT_*). */
	int flags;
};

/* Tracks the current state of mouse input from SGR mouse reporting.
 * Button values: 0=left, 1=middle, 2=right, 35=movement, 64=scroll up,
 * 65=scroll down */
struct mouse_input {
	/* Button identifier from the mouse event. */
	int button;
	/* Mouse column coordinate (1-based from terminal). */
	int column;
	/* Mouse row coordinate (1-based from terminal). */
	int row;
	/* Press state: 'M' for pressed, 'm' for released. */
	char pressed;
} mouse_input;

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
	/* File descriptor for mmap, or -1 if not using mmap. */
	int file_descriptor;
	/* Base address of the mmap region, or NULL. */
	char *mmap_base;
	/* Size of the mmap region in bytes. */
	size_t mmap_size;
};

/* Global editor state. */
struct editor_state editor;

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

/* Registry of all supported file types and their highlighting rules. */
struct editor_syntax syntax_highlight_database[] = {
	{"c", c_highlight_extensions, c_highlight_keywords, "//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
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
/* Displays a prompt and collects user input; returns the typed string. */
char *editor_prompt(char *prompt, void (*callback)(char *, int));
/* Prompts for a line number and moves the cursor there. */
void editor_jump_to_line(void);
/* Recalculates the gutter width based on the current line count. */
void editor_update_gutter_width(void);
/* Recalculates syntax highlighting for a line given previous open_comment state. */
int line_update_syntax(struct line *line, int prev_open_comment);

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
int current_theme_index = 3;

/* Applies the theme at the given index in editor_themes[]. */
void editor_set_theme(int index)
{
	editor.theme = editor_themes[index];
}

/* Cycles to the next theme in editor_themes[] and displays its name. */
void editor_switch_theme(void)
{
	current_theme_index = (current_theme_index + 1) %
		(int)(sizeof(editor_themes) / sizeof(editor_themes[0]));
	editor_set_theme(current_theme_index);
	editor_set_status_message("Theme: %s", editor.theme.name);
}
/* Toggles line number gutter visibility and updates the gutter width. */
void editor_toggle_line_numbers(void)
{
	editor.show_line_numbers = !editor.show_line_numbers;
	editor_update_gutter_width();
	editor_set_status_message(
			"Line numbers: %s", editor.show_line_numbers ? "on" : "off");
}

/*** Terminal ***/

/* Forward declaration so early terminal functions can call terminal_die(). */
void terminal_die(const char *message);

/* Prints an error message and exits. Clears the screen first to leave the
 * terminal in a clean state before displaying the error via perror(). */
void terminal_die(const char *message)
{
	write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
	write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));

	perror(message);
	exit(1);
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
 * This allows the editor to receive click, scroll, and movement events. */
void terminal_enable_mouse_reporting(void)
{
	write(STDOUT_FILENO, ENABLE_MOUSE_REPORTING, strlen(ENABLE_MOUSE_REPORTING));
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

/* Processes a pending terminal resize by re-querying the terminal size,
 * clamping the cursor to stay within bounds, and redrawing the screen.
 * Called from the main loop when resize_pending is set. */
void terminal_process_resize(void)
{
	resize_pending = 0;
	int new_rows, new_columns;
	if (terminal_get_window_size(&new_rows, &new_columns) == -1)
		return;
	editor.screen_rows = new_rows - 2;
	editor.screen_columns = new_columns;
	if (editor.cursor_y >= editor.line_count)
		editor.cursor_y = editor.line_count > 0 ? editor.line_count - 1 : 0;
	if (editor.cursor_y >= editor.screen_rows)
		editor.cursor_y = editor.screen_rows - 1;
	if (editor.cursor_x >= editor.screen_columns)
		editor.cursor_x = editor.screen_columns - 1;
}

/* Reads a single keypress from stdin and translates escape sequences into
 * editor_key enum values. Handles arrow keys, page up/down, home/end,
 * delete, SGR mouse events, and Alt+key combinations (ESC followed by a
 * character). Returns the key code or ESC_KEY on unrecognized sequences.
 * Blocks until a key is available. */
int terminal_read_key(void)
{
	int bytes_read;
	char character;

	while ((bytes_read = read(STDIN_FILENO, &character, 1)) != 1) {
		if (bytes_read == -1 && errno != EAGAIN && errno != EINTR)
			terminal_die("read");
	}

	if (character == ESC_KEY) {
		char sequence[ESCAPE_SEQUENCE_SIZE];

		if (read(STDIN_FILENO, &sequence[0], 1) != 1)
			return ESC_KEY;

		if (sequence[0] == '[') {
			if (read(STDIN_FILENO, &sequence[1], 1) != 1)
				return ESC_KEY;

			if (sequence[1] >= '0' && sequence[1] <= '9') {
				if (read(STDIN_FILENO, &sequence[2], 1) != 1)
					return ESC_KEY;
				if (sequence[2] == '~') {
					switch (sequence[1]) {
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				} else if (sequence[2] >= '0' && sequence[2] <= '9') {
					char terminator;
					if (read(STDIN_FILENO, &terminator, 1) != 1)
						return ESC_KEY;
					if (terminator == '~') {
						int code = (sequence[1] - '0') * 10
							+ (sequence[2] - '0');
						switch (code) {
						case 23:
							return F11_KEY;
						}
					}
				}
			} else if (sequence[1] == '<') {

				char mouse_sequence[MOUSE_SEQUENCE_SIZE];

				/* Read the mouse sequence and null-terminate at the
				 * actual read length to avoid parsing uninitialized bytes. */
				int mouse_bytes = read(STDIN_FILENO, mouse_sequence,
						       sizeof(mouse_sequence) - 1);
				if (mouse_bytes <= 0) {
					return ESC_KEY;
				}
				mouse_sequence[mouse_bytes] = '\0';

				if (sscanf(mouse_sequence, "%d;%d;%d%c", &mouse_input.button, &mouse_input.column,
									 &mouse_input.row, &mouse_input.pressed) == 4) {

					switch (mouse_input.button) {
					case 0:
						if (mouse_input.pressed == 'M') {
							mouse_input.column = mouse_input.column - editor.line_number_width - 1;
							if (mouse_input.column < 0)
								mouse_input.column = 0;
							mouse_input.row = mouse_input.row - 1;
							return MOUSE_LEFT_BUTTON_PRESSED;
						}
						break;
					case 1:
						break;
					case 2:
						break;
					case 35:
						break;
					case 64:
						return MOUSE_SCROLL_UP;
					case 65:
						return MOUSE_SCROLL_DOWN;
					}
				}

				return ESC_KEY;

			} else {
				switch (sequence[1]) {
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
		} else if (sequence[0] == 'O') {
			if (read(STDIN_FILENO, &sequence[1], 1) != 1)
				return ESC_KEY;

			switch (sequence[1]) {
			case 'H':
				return HOME_KEY;
			case 'F':
				return END_KEY;
			}
		} else {
			return ALT_KEY(sequence[0]);
		}
		return ESC_KEY;
	} else if ((unsigned char)character >= 0x80) {
		/* Multi-byte UTF-8 sequence: determine expected length from
		 * the leading byte, read continuation bytes, then decode. */
		unsigned char lead = (unsigned char)character;
		int expected;
		if ((lead & 0xE0) == 0xC0) expected = 2;
		else if ((lead & 0xF0) == 0xE0) expected = 3;
		else if ((lead & 0xF8) == 0xF0) expected = 4;
		else return UTF8_REPLACEMENT_CHAR;

		char utf8_buf[4];
		utf8_buf[0] = character;
		for (int i = 1; i < expected; i++) {
			if (read(STDIN_FILENO, &utf8_buf[i], 1) != 1)
				return UTF8_REPLACEMENT_CHAR;
		}
		uint32_t codepoint;
		int consumed = utf8_decode(utf8_buf, expected, &codepoint);
		if (consumed <= 0)
			return UTF8_REPLACEMENT_CHAR;
		return (int)codepoint;
	} else {
		return character;
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
 * position. Stores results in the provided row and column pointers.
 * Returns 0 on success, -1 on failure. */
int terminal_get_window_size(int *rows, int *columns)
{
	struct winsize window;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == -1 || window.ws_col == 0) {
		if (write(STDOUT_FILENO, CURSOR_BOTTOM_RIGHT, strlen(CURSOR_BOTTOM_RIGHT)) != (ssize_t)strlen(CURSOR_BOTTOM_RIGHT))
			return -1;
		return terminal_get_cursor_position(rows, columns);
	} else {
		*columns = window.ws_col;
		*rows = window.ws_row;
		return 0;
	}
}

/*** Syntax highlighting ***/

/* Returns true if the character is a separator for syntax highlighting
 * purposes: whitespace, null byte, or one of the common punctuation
 * characters that separate tokens in C code. */
int syntax_is_separator(int character)
{
	return isspace(character) || character == '\0' || strchr(",.()+-/*=~%<>[];", character) != NULL;
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

				for (int file_row = 0; file_row < editor.line_count; file_row++) {
					int prev_open = (file_row > 0) ? editor.lines[file_row - 1].open_comment : 0;
					line_update_syntax(&editor.lines[file_row], prev_open);
				}

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

/* Warms a COLD line by decoding its mmap bytes into cells.
 * Must be called before any cell access on a potentially cold line. */
void line_ensure_warm(struct line *line)
{
	if (line->temperature != LINE_COLD)
		return;
	line->cells = malloc(sizeof(struct cell) * LINE_INITIAL_CAPACITY);
	if (line->cells == NULL)
		terminal_die("malloc");
	line->cell_capacity = LINE_INITIAL_CAPACITY;
	line->cell_count = 0;
	line->temperature = LINE_WARM;
	line_populate_from_bytes(line, editor.mmap_base + line->mmap_offset,
							line->mmap_length);
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
	int prev_byte = utf8_prev_grapheme(buf, byte_len);
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

	/* Build a temporary byte string for keyword/comment matching */
	size_t byte_len;
	char *render = line_to_bytes(line, &byte_len);

	/* Build cell-to-byte offset map so cell index i maps to render position.
	 * Needed because multi-byte UTF-8 characters make cell indices diverge
	 * from byte offsets. */
	int *byte_offsets = malloc(sizeof(int) * (line->cell_count + 1));
	if (byte_offsets == NULL) {
		free(render);
		terminal_die("malloc");
	}
	int bpos = 0;
	for (uint32_t k = 0; k < line->cell_count; k++) {
		byte_offsets[k] = bpos;
		char tmp[UTF8_MAX_BYTES];
		bpos += utf8_encode(line->cells[k].codepoint, tmp);
	}
	byte_offsets[line->cell_count] = bpos;

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
				if (current_cp == '"' || current_cp == '\'') {
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
			for (j = 0; keywords[j]; j++) {
				int keyword_length = strlen(keywords[j]);
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

	free(byte_offsets);
	free(render);

	int changed = (line->open_comment != in_comment);
	line->open_comment = in_comment;
	return changed;
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

/* Inserts a new line at the given position in the editor's lines array.
 * Populates the line from the provided byte string, initializes the line
 * fields, and triggers a syntax update. Shifts existing lines down and
 * increments their line_index values. */
void editor_line_insert(int position, const char *string, size_t length)
{
	if (position < 0 || position > editor.line_count)
		return;

	editor.lines =
			realloc(editor.lines, sizeof(struct line) * (editor.line_count + 1));
	if (editor.lines == NULL)
		terminal_die("realloc");
	memmove(&editor.lines[position + 1], &editor.lines[position],
				sizeof(struct line) * (editor.line_count - position));
	for (int j = position + 1; j <= editor.line_count; j++)
		editor.lines[j].line_index++;

	line_init(&editor.lines[position], position);
	line_populate_from_bytes(&editor.lines[position], string, length);

	/* Run syntax highlighting on the new line */
	int prev_open = (position > 0) ? editor.lines[position - 1].open_comment : 0;
	line_update_syntax(&editor.lines[position], prev_open);

	editor.line_count++;
	editor_update_gutter_width();
	editor.dirty++;
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

/*** Editor operations ***/

/* Inserts a character at the current cursor position. If the cursor is
 * past the last line, a new empty line is created first. Advances the
 * cursor one position to the right. */
void editor_insert_char(int character)
{
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
	line_insert_cell(ln, (uint32_t)editor.cursor_x, c);
	int prev_open = (editor.cursor_y > 0) ? editor.lines[editor.cursor_y - 1].open_comment : 0;
	line_update_syntax(ln, prev_open);
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
		int prev_open = (editor.cursor_y > 0) ? editor.lines[editor.cursor_y - 1].open_comment : 0;
		line_update_syntax(ln, prev_open);
		editor.dirty++;
	}
	editor.cursor_y++;
	editor.cursor_x = 0;
}

/* Deletes the grapheme cluster to the left of the cursor (backspace behavior).
 * If the cursor is at the start of a line, merges the line with the one above
 * by appending its content and deleting the current line. */
void editor_delete_char(void)
{
	if (editor.cursor_y == editor.line_count)
		return;
	if (editor.cursor_x == 0 && editor.cursor_y == 0)
		return;
	struct line *ln = &editor.lines[editor.cursor_y];
	if (editor.cursor_x > 0) {
		/* Find the start of the grapheme cluster before the cursor */
		int prev = cursor_prev_grapheme(ln, editor.cursor_x);
		/* Delete all cells in the cluster (from prev to cursor_x) */
		for (int i = editor.cursor_x - 1; i >= prev; i--) {
			line_delete_cell(ln, (uint32_t)i);
		}
		int prev_open = (editor.cursor_y > 0) ? editor.lines[editor.cursor_y - 1].open_comment : 0;
		line_update_syntax(ln, prev_open);
		editor.dirty++;
		editor.cursor_x = prev;
	} else {
		line_ensure_warm(&editor.lines[editor.cursor_y - 1]);
		editor.cursor_x = (int)editor.lines[editor.cursor_y - 1].cell_count;
		line_append_cells(&editor.lines[editor.cursor_y - 1], ln, 0);
		int prev_open = (editor.cursor_y > 1) ? editor.lines[editor.cursor_y - 2].open_comment : 0;
		line_update_syntax(&editor.lines[editor.cursor_y - 1], prev_open);
		editor.dirty++;
		editor_line_delete(editor.cursor_y);
		editor.cursor_y--;
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
		size_t byte_len;
		char *bytes = line_to_bytes(&editor.lines[j], &byte_len);
		size_t needed = total_length + byte_len + 1;
		if (needed > capacity) {
			capacity = needed * 2;
			buffer = realloc(buffer, capacity);
			if (buffer == NULL) terminal_die("realloc");
		}
		memcpy(buffer + total_length, bytes, byte_len);
		total_length += byte_len;
		free(bytes);
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

	/* Scan for newlines to build line index */
	size_t line_start = 0;
	for (size_t i = 0; i < (size_t)file_size; i++) {
		if (base[i] == '\n') {
			size_t line_len = i - line_start;
			/* Strip trailing \r */
			if (line_len > 0 && base[line_start + line_len - 1] == '\r')
				line_len--;

			editor.lines = realloc(editor.lines,
								   sizeof(struct line) * (editor.line_count + 1));
			if (editor.lines == NULL)
				terminal_die("realloc");
			struct line *ln = &editor.lines[editor.line_count];
			ln->cells = NULL;
			ln->cell_count = 0;
			ln->cell_capacity = 0;
			ln->line_index = editor.line_count;
			ln->open_comment = 0;
			ln->mmap_offset = line_start;
			ln->mmap_length = (uint32_t)line_len;
			ln->temperature = LINE_COLD;
			editor.line_count++;

			line_start = i + 1;
		}
	}

	/* Handle last line without trailing newline */
	if (line_start < (size_t)file_size) {
		size_t line_len = (size_t)file_size - line_start;
		if (line_len > 0 && base[line_start + line_len - 1] == '\r')
			line_len--;

		editor.lines = realloc(editor.lines,
							   sizeof(struct line) * (editor.line_count + 1));
		if (editor.lines == NULL)
			terminal_die("realloc");
		struct line *ln = &editor.lines[editor.line_count];
		ln->cells = NULL;
		ln->cell_count = 0;
		ln->cell_capacity = 0;
		ln->line_index = editor.line_count;
		ln->open_comment = 0;
		ln->mmap_offset = line_start;
		ln->mmap_length = (uint32_t)line_len;
		ln->temperature = LINE_COLD;
		editor.line_count++;
	}

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
	free(editor.filename);
	editor.filename = strdup(filename);

	int ret = editor_open_mmap(filename);
	if (ret < 0) {
		editor_set_status_message("Can't open file: %s", strerror(-ret));
		return ret;
	}
	editor_update_gutter_width();

	syntax_select_highlight();

	editor.dirty = 0;
	return 0;
}

/* Saves the current file to disk. Prompts for a filename if one hasn't been
 * set yet. Converts all rows to a string, truncates the file, and writes
 * the content. Displays a status message on success or error.
 * Returns 0 on success, negative errno on failure. */
int editor_save(void)
{
	if (editor.filename == NULL) {
		editor.filename = editor_prompt("Save as: %s (ESC to cancel)", NULL);
		if (editor.filename == NULL) {
			editor_set_status_message("Save aborted");
			return -ECANCELED;
		}
		syntax_select_highlight();
	}

	int ret = 0;
	int fd = -1;
	size_t file_length;
	char *file_content = editor_rows_to_string(&file_length);

	fd = open(editor.filename, O_RDWR | O_CREAT, FILE_PERMISSION_DEFAULT);
	if (fd == -1) {
		ret = -errno;
		goto out;
	}
	if (ftruncate(fd, (off_t)file_length) == -1) {
		ret = -errno;
		goto out;
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

	/* Success */
	editor.dirty = 0;
	/* Release old mmap since file content has changed */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
		editor.mmap_size = 0;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	editor_set_status_message("%zu bytes written to disk", file_length);

out:
	if (fd != -1)
		close(fd);
	free(file_content);
	if (ret < 0)
		editor_set_status_message("Can't save! I/O error: %s", strerror(-ret));
	return ret;
}
/* Prompts for a new filename and saves the file to it. Always prompts
 * regardless of whether a filename is already set, unlike editor_save
 * which only prompts when no filename exists. Re-runs syntax highlighting
 * since the file extension may have changed.
 * Returns 0 on success, negative errno on failure. */
int editor_save_as(void)
{
	char *new_filename = editor_prompt("Save as: %s (ESC to cancel)", NULL);
	if (new_filename == NULL) {
		editor_set_status_message("Save as aborted");
		return -ECANCELED;
	}
	free(editor.filename);
	editor.filename = new_filename;
	syntax_select_highlight();
	return editor_save();
}

/* Cleans up all allocated resources before exiting. Frees every line's
 * data, clears the screen, and frees the filename. Registered with
 * atexit() so it runs automatically on exit. */
void editor_quit(void)
{
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
}

/*** Find ***/

/* Callback invoked on each keypress during incremental search. Tracks the
 * last match position and search direction, highlights the matching text,
 * and scrolls the view to center the match. Restores original highlighting
 * when the search is cancelled or a new match is found. */
void editor_find_callback(char *query, int key)
{
	static int last_match = -1;
	static int last_match_offset = -1;
	static int direction = 1;
	static int saved_highlight_line;
	static uint16_t *saved_syntax = NULL;
	static uint32_t saved_syntax_count = 0;

	if (saved_syntax) {
		struct line *restore_ln = &editor.lines[saved_highlight_line];
		line_ensure_warm(restore_ln);
		for (uint32_t k = 0; k < saved_syntax_count && k < restore_ln->cell_count; k++)
			restore_ln->cells[k].syntax = saved_syntax[k];
		free(saved_syntax);
		saved_syntax = NULL;
		saved_syntax_count = 0;
	}

	if (key == '\r' || key == ESC_KEY) {
		last_match = -1;
		last_match_offset = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		last_match = -1;
		last_match_offset = -1;
		direction = 1;
	}

	int current;
	int search_offset;
	if (last_match == -1) {
		direction = 1;
		current = editor.cursor_y;
		search_offset = -1;
	} else {
		current = last_match;
		search_offset = last_match_offset;
	}
	int screen_middle = editor.screen_rows / 2;
	size_t query_len = strlen(query);

	for (int i = 0; i < editor.line_count; i++) {
		if (i == 0 && current != -1) {
			/* First iteration: try to find another match on the
			 * same line before moving to the next one. */
		} else {
			current += direction;
			search_offset = -1;
		}
		if (current == -1)
			current = editor.line_count - 1;
		else if (current == editor.line_count)
			current = 0;

		struct line *ln = &editor.lines[current];
		line_ensure_warm(ln);
		/* Convert line to bytes for strstr matching */
		size_t byte_len;
		char *render = line_to_bytes(ln, &byte_len);
		char *match = NULL;

		if (direction == 1) {
			/* Forward: search after the previous match offset */
			int start = (search_offset >= 0) ? search_offset + 1 : 0;
			if (start < (int)byte_len)
				match = strstr(render + start, query);
		} else {
			/* Backward: find the last match before the offset */
			int limit = (search_offset >= 0) ? search_offset : (int)byte_len;
			char *candidate = render;
			while (candidate < render + limit) {
				char *found = strstr(candidate, query);
				if (!found || found >= render + limit)
					break;
				match = found;
				candidate = found + 1;
			}
		}

		if (match) {
			int match_byte_offset = (int)(match - render);
			last_match = current;
			last_match_offset = match_byte_offset;
			editor.cursor_y = current;

			/* Convert byte offset to cell index by counting
			 * codepoints in the bytes before the match. */
			int cell_index = 0;
			size_t byte_pos = 0;
			while ((int)byte_pos < match_byte_offset && (uint32_t)cell_index < ln->cell_count) {
				uint32_t cp;
				int consumed = utf8_decode(&render[byte_pos], (int)(byte_len - byte_pos), &cp);
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
			if (editor.row_offset > editor.line_count - editor.screen_rows)
				editor.row_offset = editor.line_count - editor.screen_rows;

			/* Save current syntax and highlight match */
			saved_highlight_line = current;
			saved_syntax_count = ln->cell_count;
			saved_syntax = malloc(sizeof(uint16_t) * ln->cell_count);
			if (saved_syntax == NULL) {
				free(render);
				break;
			}
			for (uint32_t k = 0; k < ln->cell_count; k++)
				saved_syntax[k] = ln->cells[k].syntax;
			for (int k = 0; k < query_cell_count && cell_index + k < (int)ln->cell_count; k++)
				ln->cells[cell_index + k].syntax = HL_MATCH;

			free(render);
			break;
		}
		free(render);
	}
}

/* Opens the search prompt and performs incremental find. Saves the cursor
 * position before searching and restores it if the user cancels with ESC. */
void editor_find(void)
{
	int saved_cursor_x = editor.cursor_x;
	int saved_cursor_y = editor.cursor_y;
	int saved_column_offset = editor.column_offset;
	int saved_row_offset = editor.row_offset;

	char *query =
			editor_prompt("Search: %s (Use ESC/Arrows/Enter)", editor_find_callback);

	if (query) {
		free(query);
	} else {
		editor.cursor_x = saved_cursor_x;
		editor.cursor_y = saved_cursor_y;
		editor.column_offset = saved_column_offset;
		editor.row_offset = saved_row_offset;
	}
}

/* Prompts the user for a line number and moves the cursor to that line.
 * Centers the target line vertically on screen and resets the horizontal
 * scroll offset. Shows an error message for invalid line numbers. */
void editor_jump_to_line(void)
{
	char *line_number = editor_prompt("Jump to line: %s", NULL);
	if (line_number == NULL)
		return;

	int line = atoi(line_number);
	free(line_number);

	if (line > 0 && line <= editor.line_count) {
		editor.cursor_y = line - 1;
		editor.cursor_x = 0;

		/* Calculate the new row offset to center the jumped-to line */
		int new_row_offset = editor.cursor_y - (editor.screen_rows / 2);
		if (new_row_offset < 0)
			new_row_offset = 0;
		if (new_row_offset > editor.line_count - editor.screen_rows)
			new_row_offset = editor.line_count - editor.screen_rows;
		editor.row_offset = new_row_offset;

		/* Ensure the cursor is visible horizontally */
		editor.column_offset = 0;
	} else {
		editor_set_status_message("Invalid line number");
	}
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

/* Writes a 24-bit foreground color escape sequence to the append buffer.
 * Parses the hex color string (e.g., "FF0000") into RGB components and
 * formats the ANSI escape sequence. */
void append_buffer_write_color(struct append_buffer *append_buffer, const char *hex_color)
{
	unsigned int red, green, blue;
	sscanf(hex_color, "%02x%02x%02x", &red, &green, &blue);
	char color_sequence[COLOR_SEQUENCE_SIZE];
	snprintf(color_sequence, sizeof(color_sequence), COLOR_FG_FORMAT, red, green, blue);
	append_buffer_write(append_buffer, color_sequence, strlen(color_sequence));
}

/* Writes a 24-bit background color escape sequence to the append buffer.
 * Works like append_buffer_write_color() but sets the background instead. */
void append_buffer_write_background(struct append_buffer *append_buffer, const char *hex_color)
{
	unsigned int red, green, blue;
	sscanf(hex_color, "%02x%02x%02x", &red, &green, &blue);
	char color_sequence[COLOR_SEQUENCE_SIZE];
	snprintf(color_sequence, sizeof(color_sequence), COLOR_BG_FORMAT, red, green, blue);
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
	if (editor.cursor_y < editor.row_offset) {
		editor.row_offset = editor.cursor_y;
	}
	if (editor.cursor_y >= editor.row_offset + editor.screen_rows) {
		editor.row_offset = editor.cursor_y - editor.screen_rows + 1;
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
						/* Normal or highlighted: set color and encode all
						 * codepoints in the grapheme cluster as UTF-8 */
						if (hl == HL_NORMAL) {
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

/* Redraws the entire screen by building output in an append buffer and
 * writing it to stdout in a single call. Hides the cursor during drawing
 * to avoid flicker, then repositions it at the correct location. */
void editor_refresh_screen(void)
{
	editor_scroll();

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

/* Displays a prompt in the status bar and collects user input character by
 * character. Supports backspace, ESC to cancel (returns NULL), and Enter to
 * confirm. The optional callback is invoked after each keypress for features
 * like incremental search. The caller must free the returned string. */
char *editor_prompt(char *prompt, void (*callback)(char *, int))
{
	size_t buffer_size = PROMPT_INITIAL_SIZE;
	char *buffer = malloc(buffer_size);
	if (buffer == NULL)
		terminal_die("malloc");

	size_t buffer_length = 0;
	buffer[0] = '\0';

	while (1) {
		editor_set_status_message(prompt, buffer);
		editor_refresh_screen();

		int key = terminal_read_key();
		if (key == DEL_KEY || key == CTRL_KEY('h') || key == BACKSPACE) {
			if (buffer_length != 0)
				buffer[--buffer_length] = '\0';
		} else if (key == ESC_KEY) {
			editor_set_status_message("");
			if (callback)
				callback(buffer, key);
			free(buffer);
			return NULL;
		} else if (key == '\r') {
			if (buffer_length != 0) {
				editor_set_status_message("");
				if (callback)
					callback(buffer, key);
				return buffer;
			}
		} else if (!iscntrl(key) && key > 0 && key < ARROW_LEFT) {
			char utf8_buf[UTF8_MAX_BYTES];
			int byte_count;
			if (key < ASCII_MAX) {
				utf8_buf[0] = (char)key;
				byte_count = 1;
			} else {
				byte_count = utf8_encode((uint32_t)key, utf8_buf);
			}
			while (buffer_length + byte_count >= buffer_size - 1) {
				buffer_size *= 2;
				buffer = realloc(buffer, buffer_size);
				if (buffer == NULL)
					terminal_die("realloc");
			}
			memcpy(buffer + buffer_length, utf8_buf, byte_count);
			buffer_length += byte_count;
			buffer[buffer_length] = '\0';
		}

		if (callback)
			callback(buffer, key);
	}
}

/* Moves the cursor in the direction indicated by the key parameter. Handles
 * arrow keys, mouse clicks, and line wrapping (moving past the end of a line
 * wraps to the next, and past the start wraps to the previous). Clamps the
 * cursor to the current line length after vertical movement. */
void editor_move_cursor(int key)
{
	struct line *current_line = (editor.cursor_y >= editor.line_count)
												? NULL
												: &editor.lines[editor.cursor_y];

	switch (key) {

	case MOUSE_LEFT_BUTTON_PRESSED:
		if (mouse_input.row >= 0 && mouse_input.row < editor.screen_rows) {
			int file_row = mouse_input.row + editor.row_offset;
			if (file_row < editor.line_count) {
				editor.cursor_y = file_row;
				struct line *click_line = &editor.lines[editor.cursor_y];
				int render_col = mouse_input.column + editor.column_offset;
				editor.cursor_x = line_render_column_to_cell(click_line, render_col);
			}
		}

		break;

	case ALT_KEY('h'):
	case ARROW_LEFT:
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
			editor.cursor_y--;
		}
		break;

	case ALT_KEY('j'):
	case ARROW_DOWN:
		if (editor.cursor_y < editor.line_count) {
			editor.cursor_y++;
		}
		break;
	}

	current_line = (editor.cursor_y >= editor.line_count) ? NULL
																: &editor.lines[editor.cursor_y];
	int row_length = current_line ? (int)current_line->cell_count : 0;
	if (editor.cursor_x > row_length) {
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

/* Main input handler called once per iteration of the editor loop. Reads a
 * keypress and dispatches it to the appropriate action: text insertion,
 * cursor movement, file operations, search, or quit. Prompts to save
 * unsaved changes before quitting. */
void editor_process_keypress(void)
{
	int key = terminal_read_key();
	switch (key) {
	case '\r':
		editor_insert_newline();
		break;

	case ALT_KEY('t'):
		editor_switch_theme();
		break;
	case ALT_KEY('n'):
		editor_toggle_line_numbers();
		break;

	case ALT_KEY('g'):
		editor_jump_to_line();
		break;

	case ALT_KEY('q'):
		if (editor.dirty) {
			editor_set_status_message(
					"Unsaved changes. Save before quitting? (y/n/ESC)");
			editor_refresh_screen();
			int response = terminal_read_key();
			if (response == 'y' || response == 'Y') {
				editor_save();
			} else if (response != 'n' && response != 'N') {
				editor_set_status_message("");
				return;
			}
		}
		write(STDOUT_FILENO, CLEAR_SCREEN, strlen(CLEAR_SCREEN));
		write(STDOUT_FILENO, CURSOR_HOME, strlen(CURSOR_HOME));
		exit(0);
		break;

	case ALT_KEY('s'):
		editor_save();
		break;
	case ALT_KEY('S'):
		editor_save_as();
		break;

	case HOME_KEY:
		editor.cursor_x = 0;
		break;

	case END_KEY:
		if (editor.cursor_y < editor.line_count)
			editor.cursor_x = (int)editor.lines[editor.cursor_y].cell_count;
		break;

	case ALT_KEY('f'):
		editor_find();
		break;

	case BACKSPACE:
	case CTRL_KEY('h'):
	case DEL_KEY:
		if (key == DEL_KEY)
			editor_move_cursor(ARROW_RIGHT);
		editor_delete_char();
		break;

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
			editor_move_cursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
	} break;

	case ALT_KEY('h'):
	case ALT_KEY('j'):
	case ALT_KEY('k'):
	case ALT_KEY('l'):
	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editor_move_cursor(key);
		break;

	case MOUSE_LEFT_BUTTON_PRESSED:
		editor_move_cursor(MOUSE_LEFT_BUTTON_PRESSED);
		break;

	case MOUSE_SCROLL_UP:
		editor_update_scroll_speed();
		editor_scroll_rows(ARROW_UP, editor.scroll_speed);
		break;

	case MOUSE_SCROLL_DOWN:
		editor_update_scroll_speed();
		editor_scroll_rows(ARROW_DOWN, editor.scroll_speed);
		break;

	case F11_KEY:
		editor_set_status_message("Edit %s", EDIT_VERSION);
		break;

	case ESC_KEY:
		break;

	default:
		editor_insert_char(key);
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
	editor_update_gutter_width();
	editor.lines = NULL;
	editor.dirty = 0;
	editor.filename = NULL;
	editor.status_message[0] = '\0';
	editor.status_message_time = 0;
	editor.syntax = NULL;
	editor_set_theme(3);

	signal(SIGWINCH, terminal_handle_resize);
	if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns) ==
			-1) {
		terminal_die("terminal_get_window_size");
	}
	editor.screen_rows -= 2;

	gettimeofday(&editor.last_scroll_time, NULL);
	editor.scroll_speed = 1;
	editor.file_descriptor = -1;
	editor.mmap_base = NULL;
	editor.mmap_size = 0;
}

/* Entry point. Sets up the terminal (mouse reporting, raw mode), initializes
 * the editor, optionally opens a file from the command line argument, and
 * enters the main loop of refreshing the screen and processing keypresses. */
int main(int argc, char *argv[])
{
	atexit(editor_quit);

	/* Enable mouse reporting */
	terminal_enable_mouse_reporting();

	/* Enable raw mode */
	terminal_enable_raw_mode();

	editor_init();
	if (argc >= 2) {
		editor_open(argv[1]);
	}

	editor_set_status_message(
			"Alt: S=save Q=quit F=find G=goto N=lines T=theme HJKL=move");

	while (1) {
		if (resize_pending)
			terminal_process_resize();
		editor_refresh_screen();
		editor_process_keypress();
	}
}
