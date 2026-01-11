/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * input.h - Input handling for edit
 *
 * Provides keyboard and mouse input parsing.
 */

#ifndef EDIT_INPUT_H
#define EDIT_INPUT_H

#include "types.h"

/*****************************************************************************
 * Key Input
 *****************************************************************************/

/*
 * Read a key from stdin.
 * Blocks until input is available.
 * Returns a key code (may be a special KEY_* value).
 *
 * Note: This function handles mouse events internally by calling
 * the registered mouse handler (editor_handle_mouse) or storing
 * the event for dialog mode retrieval.
 */
int input_read_key(void);

/*
 * Check if input is available without blocking.
 * Returns true if a character is waiting to be read.
 */
bool input_available(void);
/*
 * Read a key with timeout. Used for auto-scroll during drag selection.
 * Returns key code if input available, 0 if timeout, -1 on error.
 * timeout_ms: milliseconds to wait (0 = non-blocking check)
 */
int input_read_key_timeout(int timeout_ms);

/*****************************************************************************
 * Mouse Input
 *****************************************************************************/

/*
 * Get the last mouse input event from dialog mode.
 * Only valid when dialog mouse mode is active.
 */
struct mouse_input input_get_last_mouse(void);

/*
 * Clear the last mouse input event.
 * Called after the dialog has processed the mouse event.
 */
void input_clear_last_mouse(void);

/*
 * Check if a key code is a mouse event.
 * Returns true if key == KEY_MOUSE_EVENT.
 */
bool input_is_mouse_event(int key);

/*
 * Parse SGR mouse escape sequence.
 * Called internally by input_read_key after seeing \x1b[<
 * Returns the parsed mouse event.
 */
struct mouse_input input_parse_sgr_mouse(void);

/*****************************************************************************
 * Dialog Mouse Mode
 *****************************************************************************/

/*
 * Set dialog mouse mode.
 * When enabled, mouse events are stored for dialog retrieval
 * instead of being sent to the main editor handler.
 */
void input_set_dialog_mouse_mode(bool enabled);

/*
 * Check if dialog mouse mode is active.
 */
bool input_get_dialog_mouse_mode(void);

/*****************************************************************************
 * Mouse Handler Registration
 *****************************************************************************/

/*
 * Register the main editor mouse handler.
 * This function is called for mouse events when not in dialog mode.
 */
typedef void (*mouse_handler_func)(struct mouse_input *mouse);
void input_set_mouse_handler(mouse_handler_func handler);

#endif /* EDIT_INPUT_H */
