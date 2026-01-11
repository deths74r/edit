/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * terminal.h - Terminal handling for edit
 *
 * Provides raw mode terminal I/O, window size detection,
 * mouse tracking, and resize signal handling.
 */

#ifndef EDIT_TERMINAL_H
#define EDIT_TERMINAL_H

#include "types.h"
#include <termios.h>

/*****************************************************************************
 * Raw Mode
 *****************************************************************************/

/*
 * Enable raw mode for terminal input.
 * Disables canonical mode, echo, and signal generation.
 * Registers terminal_disable_raw_mode() to run at exit.
 * Returns 0 on success, negative error code on failure:
 *   -EEDIT_NOTTY   if stdin is not a terminal
 *   -EEDIT_TERMRAW if terminal configuration fails
 */
int terminal_enable_raw_mode(void);

/*
 * Restore terminal to original settings.
 * Safe to call multiple times or if raw mode was never enabled.
 * Also disables mouse tracking.
 */
void terminal_disable_raw_mode(void);

/*
 * Check if terminal is currently in raw mode.
 */
bool terminal_is_raw_mode(void);

/*****************************************************************************
 * Window Size
 *****************************************************************************/

/*
 * Get the current terminal window size.
 * Returns 0 on success, -EEDIT_TERMSIZE on failure.
 * Minimum usable size is 10x10.
 */
int terminal_get_window_size(uint32_t *rows, uint32_t *columns);

/*
 * Get the current cursor position by querying the terminal.
 * Returns 0 on success, -EEDIT_TERMSIZE on failure.
 */
int terminal_get_cursor_position(int *row, int *col);

/*****************************************************************************
 * Screen Control
 *****************************************************************************/

/*
 * Clear the entire screen and move cursor to home position.
 */
void terminal_clear_screen(void);

/*****************************************************************************
 * Mouse Tracking
 *****************************************************************************/

/*
 * Enable mouse tracking using SGR extended mode.
 * Allows receiving click, drag, and scroll events.
 */
void terminal_enable_mouse(void);

/*
 * Disable mouse tracking.
 */
void terminal_disable_mouse(void);

/*****************************************************************************
 * Resize Handling
 *****************************************************************************/

/*
 * Signal handler for SIGWINCH (terminal resize).
 * Sets a flag that can be checked with terminal_check_resize().
 */
void terminal_handle_resize(int signal);

/*
 * Check if terminal was resized since last check.
 * Returns true if resize occurred, and clears the flag.
 */
bool terminal_check_resize(void);

#endif /* EDIT_TERMINAL_H */
