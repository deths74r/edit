/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * input.c - Input handling for edit
 *
 * Handles keyboard and mouse input parsing, including:
 * - Reading key codes from stdin
 * - Parsing escape sequences (arrows, function keys, etc.)
 * - Parsing SGR mouse events
 * - UTF-8 multi-byte sequence decoding
 */

#include "input.h"
#include "terminal.h"
#include "../third_party/utflite-1.5.2/single_include/utflite.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/*****************************************************************************
 * Static State
 *****************************************************************************/

/* Dialog mouse mode flag - when true, mouse events go to dialog handler */
static bool dialog_mouse_mode = false;
static struct mouse_input dialog_last_mouse = {0};

/* Registered mouse handler for normal mode */
static mouse_handler_func mouse_handler = NULL;

/*****************************************************************************
 * Dialog Mouse Mode
 *****************************************************************************/

void input_set_dialog_mouse_mode(bool enabled)
{
	dialog_mouse_mode = enabled;
}

bool input_get_dialog_mouse_mode(void)
{
	return dialog_mouse_mode;
}

struct mouse_input input_get_last_mouse(void)
{
	return dialog_last_mouse;
}

void input_clear_last_mouse(void)
{
	dialog_last_mouse.event = MOUSE_NONE;
}

/*****************************************************************************
 * Mouse Handler Registration
 *****************************************************************************/

void input_set_mouse_handler(mouse_handler_func handler)
{
	mouse_handler = handler;
}

/*****************************************************************************
 * Helper Functions
 *****************************************************************************/

bool input_is_mouse_event(int key)
{
	return key == KEY_MOUSE_EVENT;
}

bool input_available(void)
{
	struct timeval timeout = {0, 0};
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0;
}

/*****************************************************************************
 * SGR Mouse Parsing
 *****************************************************************************/

struct mouse_input input_parse_sgr_mouse(void)
{
	struct mouse_input mouse = {.event = MOUSE_NONE, .row = 0, .column = 0};
	char buffer[32];
	int length = 0;

	/* Read until 'M' (press) or 'm' (release) */
	while (length < 31) {
		if (read(STDIN_FILENO, &buffer[length], 1) != 1) {
			return mouse;
		}
		if (buffer[length] == 'M' || buffer[length] == 'm') {
			break;
		}
		length++;
	}

	char final = buffer[length];
	buffer[length] = '\0';

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

/*****************************************************************************
 * Key Reading
 *****************************************************************************/

/*
 * Read a key from stdin.
 *
 * Handles:
 * - Single ASCII characters
 * - Escape sequences for special keys (arrows, F-keys, etc.)
 * - Alt+key combinations (Meta sends ESC followed by letter)
 * - SGR mouse events
 * - UTF-8 multi-byte sequences
 *
 * Returns:
 * - Positive value: character or key code
 * - KEY_MOUSE_EVENT (-3): mouse event occurred, retrieve via input_get_last_mouse()
 * - KEY_RESIZE (-2): terminal was resized
 * - -1: read error
 */
int input_read_key(void)
{
	int read_count;
	unsigned char character;

	while ((read_count = read(STDIN_FILENO, &character, 1)) != 1) {
		if (read_count == -1 && errno != EAGAIN) {
			return -1;
		}
		if (terminal_check_resize()) {
			return KEY_RESIZE;
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
				case 'S': return KEY_ALT_SHIFT_S;
				case 'k': case 'K': return KEY_ALT_K;
				case 'd': case 'D': return KEY_ALT_D;
				case '/': return KEY_ALT_SLASH;
				case 'a': case 'A': return KEY_ALT_A;
				case ']': return KEY_ALT_BRACKET;
				case 'c': return KEY_ALT_C;
				case 'C': return KEY_ALT_SHIFT_C;
				case 'w': return KEY_ALT_W;
				case 'W': return KEY_ALT_SHIFT_W;
				case 'r': return KEY_ALT_R;
				case 'u': return KEY_ALT_U;
				case 'l': case 'L': return KEY_ALT_L;
				case 't': case 'T': return KEY_ALT_T;
				case 'm': case 'M': return KEY_ALT_M;
				case 'o': case 'O': return KEY_ALT_O;
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
				} else if (sequence[2] >= '0' && sequence[2] <= '9') {
					/* Two-digit sequences like \x1b[13~ for F3, \x1b[25~ for Shift+F3 */
					/* Also handles \x1b[13;2~ format (code;modifier~) */
					char digit3;
					if (read(STDIN_FILENO, &digit3, 1) != 1) {
						return '\x1b';
					}
					int code = (sequence[1] - '0') * 10 + (sequence[2] - '0');
					if (digit3 == '~') {
						if (code == 11) return KEY_F1;       /* F1 */
						if (code == 13) return KEY_F3;       /* F3 */
						if (code == 25) return KEY_SHIFT_F3; /* Shift+F3 */
					} else if (digit3 == ';') {
						/* Modified two-digit key: \x1b[13;2~ = Shift+F3 */
						char modifier, final;
						if (read(STDIN_FILENO, &modifier, 1) != 1) {
							return '\x1b';
						}
						if (read(STDIN_FILENO, &final, 1) != 1) {
							return '\x1b';
						}
						if (final == '~' && modifier == '2') {  /* Shift modifier */
							if (code == 13) return KEY_SHIFT_F3;
						}
						/* CSI u mode: \x1b[codepoint;modifieru */
						if (final == 'u' && modifier == '6') {  /* Ctrl+Shift */
							if (code == 78 || code == 110) return KEY_CTRL_SHIFT_N;
							if (code == 79 || code == 111) return KEY_CTRL_SHIFT_O;
						}
					} else if (digit3 >= '0' && digit3 <= '9') {
						/* Three-digit code: \x1b[110;6u for CSI u mode */
						char next;
						if (read(STDIN_FILENO, &next, 1) != 1) {
							return '\x1b';
						}
						int code3 = code * 10 + (digit3 - '0');
						if (next == ';') {
							char modifier, final;
							if (read(STDIN_FILENO, &modifier, 1) != 1) {
								return '\x1b';
							}
							if (read(STDIN_FILENO, &final, 1) != 1) {
								return '\x1b';
							}
							/* CSI u mode: \x1b[codepoint;modifieru */
							if (final == 'u' && modifier == '6') {  /* Ctrl+Shift */
								if (code3 == 110) return KEY_CTRL_SHIFT_N;  /* 'n' */
								if (code3 == 111) return KEY_CTRL_SHIFT_O;  /* 'o' */
							}
						}
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
								case 'R': return KEY_SHIFT_F3;
							}
						} else if (modifier == '5') {  /* Ctrl */
							switch (final) {
								case 'C': return KEY_CTRL_ARROW_RIGHT;
								case 'D': return KEY_CTRL_ARROW_LEFT;
								case 'H': return KEY_CTRL_HOME;
								case 'F': return KEY_CTRL_END;
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
								case 'C': return KEY_ALT_ARROW_RIGHT;
								case 'D': return KEY_ALT_ARROW_LEFT;
							}
						}
					} else if ((sequence[1] == '5' || sequence[1] == '6') &&
					           modifier == '2' && final == '~') {
						/* \x1b[5;2~ or \x1b[6;2~ - Shift+PageUp/Down */
						if (sequence[1] == '5') return KEY_SHIFT_PAGE_UP;
						if (sequence[1] == '6') return KEY_SHIFT_PAGE_DOWN;
					}
				}
			} else if (sequence[1] == '<') {
				/* SGR mouse event: \x1b[<button;column;row{M|m} */
				struct mouse_input mouse = input_parse_sgr_mouse();
				if (mouse.event != MOUSE_NONE) {
					if (dialog_mouse_mode) {
						dialog_last_mouse = mouse;
					} else if (mouse_handler != NULL) {
						mouse_handler(&mouse);
					}
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
				case 'P': return KEY_F1;  /* F1 in xterm */
				case 'R': return KEY_F3;  /* F3 in xterm */
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

	/* Handle Ctrl+key combinations for file operations */
	if (character == CONTROL_KEY('o')) {
		return KEY_CTRL_O;
	}
	if (character == CONTROL_KEY('t')) {
		return KEY_CTRL_T;
	}
	if (character == CONTROL_KEY('n')) {
		return KEY_CTRL_N;
	}
	if (character == CONTROL_KEY('w')) {
		return KEY_CTRL_W;
	}

	return character;
}
