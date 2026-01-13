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
#include "error.h"
#include "terminal.h"
#include "../third_party/utflite-1.5.2/single_include/utflite.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/*****************************************************************************
 * Kitty Keyboard Protocol Constants
 *****************************************************************************/

#define KITTY_KEY_CODEPOINT_LEFT_SHIFT 57441
#define KITTY_KEY_CODEPOINT_RIGHT_SHIFT 57442
#define KITTY_KEY_EVENT_RELEASE 3

/*****************************************************************************
 * Static State
 *****************************************************************************/

/* Dialog mouse mode flag - when true, mouse events go to dialog handler */
static bool dialog_mouse_mode = false;
static struct mouse_input dialog_last_mouse = {0};

/* Registered mouse handler for normal mode */
static mouse_handler_func mouse_handler = NULL;
static bool shift_key_held = false;

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
int input_read_key_timeout(int timeout_ms)
{
	struct timeval timeout;
	fd_set readfds;
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
	int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
	if (result < 0) {
		return -1;  /* Error */
	}
	if (result == 0) {
		return 0;   /* Timeout */
	}
	/* Input available - read it */
	return input_read_key();
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
 * Extended CSI Parsing (Kitty Keyboard Protocol + Legacy)
 *****************************************************************************/

/*
 * Map CSI u codepoint to key (for 'u' terminated sequences).
 * codepoint is the first number, modifier bits already decoded.
 */
static int
map_csi_u_codepoint(int codepoint, bool shift, bool alt, bool ctrl)
{
	(void)alt;  /* Alt+key handled separately via ESC prefix */

	switch (codepoint) {
	case 9:   /* Tab */
		if (shift) return KEY_SHIFT_TAB;
		return '\t';
	case 13:  /* Enter */
		if (ctrl) return KEY_CTRL_ENTER;
		return '\r';
	case 27:  /* Escape */
		return 27;
	case 32:  /* Space */
		if (ctrl) return 0;  /* Ctrl+Space = leader key */
		if (shift) return KEY_SHIFT_SPACE;
		return ' ';
	case 127: /* Backspace */
		return KEY_BACKSPACE;
	}

	/* Regular ASCII characters */
	if (codepoint >= 32 && codepoint < 127) {
		if (ctrl && shift) {
			if (codepoint == 'n' || codepoint == 'N')
				return KEY_CTRL_SHIFT_N;
			if (codepoint == 'o' || codepoint == 'O')
				return KEY_CTRL_SHIFT_O;
		}
		return codepoint;
	}

	/*
	 * Unrecognized codepoints (e.g., modifier keys like Shift/Ctrl/Alt
	 * which use codepoints 57441-57452) should be ignored, not treated
	 * as errors. Return -2 to indicate "recognized but ignored".
	 */
	return -2;
}

/*
 * Map parsed CSI sequence to key code.
 * buffer contains "number" or "number;modifier" or "number;modifier;..."
 * terminator is the final character (u, A, B, C, D, H, F, ~, etc.)
 */
static int
map_extended_csi(const char *buffer, char terminator)
{
	int num1 = 0, modifier = 1;
	int event = 1;

	/* Parse first number */
	const char *p = buffer;
	while (*p >= '0' && *p <= '9') {
		num1 = num1 * 10 + (*p - '0');
		p++;
	}

	/* CSI u format: codepoint ; modifier u */
	if (terminator == 'u') {
		bool log_sequence = false;  /* Enable for debugging Shift+Space issues */
		(void)log_sequence;  /* Suppress unused warning when disabled */

		/* Skip optional alternate keys: codepoint:alternate-keys */
		if (*p == ':') {
			while (*p != '\0' && *p != ';') {
				p++;
			}
		}

		/* Parse modifier and optional event type: modifier[:event] */
		if (*p == ';') {
			bool has_digit = false;
			p++;
			modifier = 0;
			while (*p >= '0' && *p <= '9') {
				modifier = modifier * 10 + (*p - '0');
				p++;
				has_digit = true;
			}
			if (!has_digit) {
				modifier = 1;
			}
			if (*p == ':') {
				int parsed_event = 0;
				bool has_event = false;
				p++;
				while (*p >= '0' && *p <= '9') {
					parsed_event = parsed_event * 10 + (*p - '0');
					p++;
					has_event = true;
				}
				if (has_event) {
					event = parsed_event;
				}
			}
		}

		/* Decode modifier bits: modifier = 1 + shift(1) + alt(2) + ctrl(4) + ... */
		int mod_bits = (modifier > 0) ? modifier - 1 : 0;
		bool shift = (mod_bits & 1) != 0;
		bool alt = (mod_bits & 2) != 0;
		bool ctrl = (mod_bits & 4) != 0;

		if (log_sequence) {
			debug_log("CSI u buffer='%s' codepoint=%d modifier=%d event=%d shift=%d alt=%d ctrl=%d",
				  buffer, num1, modifier, event, shift, alt, ctrl);
		}

		if (num1 == KITTY_KEY_CODEPOINT_LEFT_SHIFT ||
		    num1 == KITTY_KEY_CODEPOINT_RIGHT_SHIFT) {
			shift_key_held = (event != KITTY_KEY_EVENT_RELEASE);
			if (log_sequence) {
				debug_log("CSI u shift_key_held=%d", shift_key_held);
			}
			return -2;
		}

		if (event == KITTY_KEY_EVENT_RELEASE) {
			if (log_sequence) {
				debug_log("CSI u release ignored codepoint=%d", num1);
			}
			return -2;
		}

		int mapped_key = map_csi_u_codepoint(num1, shift, alt, ctrl);
		if (log_sequence) {
			debug_log("CSI u mapped_key=%d", mapped_key);
		}
		return mapped_key;
	}

	/* Parse modifier if present */
	if (*p == ';') {
		p++;
		modifier = 0;
		while (*p >= '0' && *p <= '9') {
			modifier = modifier * 10 + (*p - '0');
			p++;
		}
	}

	/* Decode modifier bits: modifier = 1 + shift(1) + alt(2) + ctrl(4) + ... */
	int mod_bits = (modifier > 0) ? modifier - 1 : 0;
	bool shift = (mod_bits & 1) != 0;
	bool alt = (mod_bits & 2) != 0;
	bool ctrl = (mod_bits & 4) != 0;

	/* Arrow keys: 1 ; modifier A/B/C/D */
	if (terminator == 'A') {  /* Up */
		if (shift) return KEY_SHIFT_ARROW_UP;
		if (alt) return KEY_ALT_ARROW_UP;
		return KEY_ARROW_UP;
	}
	if (terminator == 'B') {  /* Down */
		if (shift) return KEY_SHIFT_ARROW_DOWN;
		if (alt) return KEY_ALT_ARROW_DOWN;
		return KEY_ARROW_DOWN;
	}
	if (terminator == 'C') {  /* Right */
		if (ctrl && shift) return KEY_CTRL_SHIFT_ARROW_RIGHT;
		if (ctrl) return KEY_CTRL_ARROW_RIGHT;
		if (shift) return KEY_SHIFT_ARROW_RIGHT;
		if (alt) return KEY_ALT_ARROW_RIGHT;
		return KEY_ARROW_RIGHT;
	}
	if (terminator == 'D') {  /* Left */
		if (ctrl && shift) return KEY_CTRL_SHIFT_ARROW_LEFT;
		if (ctrl) return KEY_CTRL_ARROW_LEFT;
		if (shift) return KEY_SHIFT_ARROW_LEFT;
		if (alt) return KEY_ALT_ARROW_LEFT;
		return KEY_ARROW_LEFT;
	}

	/* Home/End: 1 ; modifier H/F */
	if (terminator == 'H') {
		if (ctrl) return KEY_CTRL_HOME;
		if (shift) return KEY_SHIFT_HOME;
		return KEY_HOME;
	}
	if (terminator == 'F') {
		if (ctrl) return KEY_CTRL_END;
		if (shift) return KEY_SHIFT_END;
		return KEY_END;
	}

	/* Legacy ~ format: number ; modifier ~ */
	if (terminator == '~') {
		switch (num1) {
		case 3: return KEY_DELETE;
		case 5:
			if (shift) return KEY_SHIFT_PAGE_UP;
			return KEY_PAGE_UP;
		case 6:
			if (shift) return KEY_SHIFT_PAGE_DOWN;
			return KEY_PAGE_DOWN;
		}
	}

	/* F-keys: ESC O P/Q/R/S for F1-F4 handled elsewhere */
	if (terminator == 'P') return KEY_F1;
	if (terminator == 'R') return KEY_F3;

	return -1;
}

/*
 * Parse extended CSI sequence after ESC [ and first digit.
 * Handles both CSI u format and extended arrow/function key format.
 * Returns KEY_* constant or -1 if not recognized.
 */
static int
parse_extended_csi(char first_digit)
{
	char c;
	char buffer[32];
	int buf_idx = 0;

	/* Store first digit */
	buffer[buf_idx++] = first_digit;

	/* Read until we hit a terminator (letter or ~) */
	while (buf_idx < 31 && read(STDIN_FILENO, &c, 1) == 1) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
			/* Found terminator */
			buffer[buf_idx] = '\0';
			return map_extended_csi(buffer, c);
		}
		buffer[buf_idx++] = c;
	}
	return -1;
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
				case 'f': case 'F': return KEY_ALT_F;
				default: return '\x1b';
			}
		}

		if (read(STDIN_FILENO, &sequence[1], 1) != 1) {
			return '\x1b';
		}

		if (sequence[0] == '[') {
			if (sequence[1] >= '0' && sequence[1] <= '9') {
				/*
				 * Extended CSI sequence handling.
				 * Kitty sends: arrows as ESC[1;modA/B/C/D
				 *              space/enter as ESC[cp;modu
				 *              pgup/pgdn as ESC[5;mod~ / ESC[6;mod~
				 */
				int key = parse_extended_csi(sequence[1]);
				if (key == -2) {
					/* Ignored key (e.g., modifier key press/release) */
					return input_read_key();
				}
				if (key != -1)
					return key;
				/* Fall through to return ESC if not recognized */
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

	if (character == ' ' && shift_key_held) {
		debug_log("raw space with shift_key_held=1");
		return KEY_SHIFT_SPACE;
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
