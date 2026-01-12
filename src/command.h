/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * command.h - Leader key command mode
 *
 * Implements a quasi-modal command system triggered by Ctrl+Space.
 * Users press Ctrl+Space to enter command mode, then a single key
 * to execute a command. Supports categorized submenus.
 */

#ifndef EDIT_COMMAND_H
#define EDIT_COMMAND_H

#include "types.h"
#include <stdbool.h>

/*
 * Enter command mode. Sets state to COMMAND_MODE_TOP
 * and updates the status bar.
 */
void command_mode_enter(void);

/*
 * Exit command mode. Resets state to COMMAND_MODE_NONE
 * and clears the status bar message.
 */
void command_mode_exit(void);

/*
 * Handle a key press while in command mode.
 * Returns true if the key was consumed (we're in command mode),
 * false if not in command mode.
 */
bool command_mode_handle_key(int key);

/*
 * Check if command mode is currently active.
 */
bool command_mode_active(void);

/*
 * Get the current command mode state.
 */
enum command_mode_state command_mode_get_state(void);

/*
 * Get the status bar message for the current command mode state.
 * Returns NULL if not in command mode.
 */
const char *command_mode_status_message(void);

#endif /* EDIT_COMMAND_H */
