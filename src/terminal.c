/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * terminal.c - Terminal handling implementation
 */

#include "edit.h"
#include "terminal.h"

/*****************************************************************************
 * Static State
 *****************************************************************************/

/* Original terminal settings for restoration */
static struct termios original_terminal_settings;

/* Track whether we have valid settings to restore */
static bool settings_saved = false;

/* Track whether raw mode is currently enabled */
static bool raw_mode_enabled = false;

/* Flag set by SIGWINCH handler, checked and cleared by main loop */
static volatile sig_atomic_t terminal_resized = 0;

/*****************************************************************************
 * Raw Mode
 *****************************************************************************/

/*
 * Puts the terminal into raw mode for character-by-character input.
 * Disables echo, canonical mode, and signal processing. Registers
 * terminal_disable_raw_mode() to run at exit.
 *
 * Returns 0 on success, negative error code on failure:
 *   -EEDIT_NOTTY   if stdin is not a terminal
 *   -EEDIT_TERMRAW if terminal configuration fails
 */
int terminal_enable_raw_mode(void)
{
	if (raw_mode_enabled)
		return 0;

	/* Verify stdin is a terminal */
	if (!isatty(STDIN_FILENO))
		return -EEDIT_NOTTY;

	/* Save original settings for restoration at exit */
	if (tcgetattr(STDIN_FILENO, &original_terminal_settings) < 0)
		return -EEDIT_TERMRAW;

	settings_saved = true;

	/* Register cleanup only after we have valid settings to restore */
	atexit(terminal_disable_raw_mode);

	struct termios raw = original_terminal_settings;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
		return -EEDIT_TERMRAW;

	raw_mode_enabled = true;
	return 0;
}

/*
 * Restores the terminal to its original settings. Called automatically
 * at exit via atexit() to ensure the terminal is usable after the editor.
 * Also called by fatal signal handler and BUG macros.
 */
void terminal_disable_raw_mode(void)
{
	terminal_disable_mouse();
	if (settings_saved)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminal_settings);
	raw_mode_enabled = false;
}

/*
 * Check if terminal is currently in raw mode.
 */
bool terminal_is_raw_mode(void)
{
	return raw_mode_enabled;
}

/*****************************************************************************
 * Window Size
 *****************************************************************************/

/*
 * Queries the terminal for its current size in rows and columns.
 *
 * Returns 0 on success, -EEDIT_TERMSIZE on failure (ioctl failed or
 * dimensions too small). Minimum usable size is 10x10.
 */
int terminal_get_window_size(uint32_t *rows, uint32_t *columns)
{
	struct winsize window_size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == -1)
		return -EEDIT_TERMSIZE;

	/*
	 * Sanity check: reject unreasonably small dimensions.
	 * When stdout is a pipe (not a TTY), ioctl may succeed but
	 * return garbage values. Minimum usable size is 10x10.
	 */
	if (window_size.ws_col < MINIMUM_WINDOW_SIZE || window_size.ws_row < MINIMUM_WINDOW_SIZE)
		return -EEDIT_TERMSIZE;

	*columns = window_size.ws_col;
	*rows = window_size.ws_row;
	return 0;
}

/*
 * Get the current cursor position by querying the terminal.
 * Sends DSR (Device Status Report) escape sequence and parses response.
 * Returns 0 on success, -EEDIT_TERMSIZE on failure.
 */
int terminal_get_cursor_position(int *row, int *col)
{
	char buffer[32];
	unsigned int i = 0;

	/* Send cursor position query */
	if (write(STDOUT_FILENO, ESCAPE_CURSOR_POSITION_QUERY, ESCAPE_CURSOR_POSITION_QUERY_LENGTH) != ESCAPE_CURSOR_POSITION_QUERY_LENGTH)
		return -EEDIT_TERMSIZE;

	/* Read response: ESC [ rows ; cols R */
	while (i < sizeof(buffer) - 1) {
		if (read(STDIN_FILENO, &buffer[i], 1) != 1)
			break;
		if (buffer[i] == 'R')
			break;
		i++;
	}
	buffer[i] = '\0';

	/* Parse response */
	if (buffer[0] != '\x1b' || buffer[1] != '[')
		return -EEDIT_TERMSIZE;
	if (sscanf(&buffer[2], "%d;%d", row, col) != 2)
		return -EEDIT_TERMSIZE;

	return 0;
}

/*****************************************************************************
 * Screen Control
 *****************************************************************************/

/*
 * Clears the entire screen and moves the cursor to the home position.
 */
void terminal_clear_screen(void)
{
	write(STDOUT_FILENO, ESCAPE_CLEAR_SCREEN, ESCAPE_CLEAR_SCREEN_LENGTH);
	write(STDOUT_FILENO, ESCAPE_CURSOR_HOME, ESCAPE_CURSOR_HOME_LENGTH);
}

/*****************************************************************************
 * Mouse Tracking
 *****************************************************************************/

/*
 * Enables mouse tracking using SGR extended mode. This allows us to receive
 * click, drag, and scroll events with coordinates that work beyond column 223.
 */
void terminal_enable_mouse(void)
{
	/* Enable button events */
	write(STDOUT_FILENO, ESCAPE_MOUSE_BUTTON_ENABLE, ESCAPE_MOUSE_SEQUENCE_LENGTH);
	/* Enable button + drag events */
	write(STDOUT_FILENO, ESCAPE_MOUSE_DRAG_ENABLE, ESCAPE_MOUSE_SEQUENCE_LENGTH);
	/* Enable SGR extended mode */
	write(STDOUT_FILENO, ESCAPE_MOUSE_SGR_ENABLE, ESCAPE_MOUSE_SEQUENCE_LENGTH);
}

/*
 * Disables mouse tracking. Called at cleanup to restore terminal state.
 */
void terminal_disable_mouse(void)
{
	/* Disable in reverse order */
	write(STDOUT_FILENO, ESCAPE_MOUSE_SGR_DISABLE, ESCAPE_MOUSE_SEQUENCE_LENGTH);
	write(STDOUT_FILENO, ESCAPE_MOUSE_DRAG_DISABLE, ESCAPE_MOUSE_SEQUENCE_LENGTH);
	write(STDOUT_FILENO, ESCAPE_MOUSE_BUTTON_DISABLE, ESCAPE_MOUSE_SEQUENCE_LENGTH);
}

/*****************************************************************************
 * Resize Handling
 *****************************************************************************/

/*
 * Signal handler for SIGWINCH (terminal resize). Sets a flag that the
 * main loop checks to update screen dimensions.
 */
void terminal_handle_resize(int signal)
{
	(void)signal;
	terminal_resized = 1;
}

/*
 * Check if terminal was resized since last check.
 * Returns true if resize occurred, and clears the flag.
 */
bool terminal_check_resize(void)
{
	if (terminal_resized) {
		terminal_resized = 0;
		return true;
	}
	return false;
}
