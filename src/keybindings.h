/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * keybindings.h - Customizable key binding system
 *
 * This module provides a table-based approach to keybindings, allowing
 * keys to be mapped to actions dynamically. Supports loading from
 * ~/.edit/keybindings.ini and built-in presets like "macos".
 */

#ifndef EDIT_KEYBINDINGS_H
#define EDIT_KEYBINDINGS_H

#include "types.h"

/* Maximum number of key-to-action bindings */
#define MAX_KEYBINDINGS 256

/* A single key-to-action mapping */
struct keybinding {
	int key;                    /* Key code (KEY_* or CONTROL_KEY() value) */
	enum editor_action action;  /* Action to execute when key is pressed */
};

/*
 * Look up the action bound to a key.
 * Returns ACTION_NONE if no binding exists for the key.
 */
enum editor_action keybinding_lookup(int key);

/*
 * Add a key binding. If the key is already bound, replaces it.
 * Returns 0 on success, -1 if binding table is full.
 */
int keybinding_add(int key, enum editor_action action);

/*
 * Remove a key binding.
 * Returns 0 if binding was removed, -1 if key was not bound.
 */
int keybinding_remove(int key);

/*
 * Load default PC-style keybindings.
 * This clears any existing bindings and sets the standard defaults.
 */
void keybinding_load_defaults(void);

/*
 * Load a named preset ("default", "macos").
 * Returns 0 on success, -1 if preset name is unknown.
 */
int keybinding_load_preset(const char *name);

/*
 * Load keybindings from a file.
 * File format:
 *   preset = macos          # Optional: load preset as base
 *   [bindings]
 *   save = Ctrl+S
 *   copy = Alt+C
 *
 * Returns 0 on success, -1 on file error.
 */
int keybinding_load_file(const char *path);

/*
 * Load user keybindings from ~/.edit/keybindings.ini if it exists.
 * Called during editor initialization.
 */
void keybinding_init(void);

/*
 * Get the display name for an action (e.g., "save" for ACTION_SAVE).
 * Returns NULL if action is invalid.
 */
const char *keybinding_action_name(enum editor_action action);

/*
 * Get the key string for a bound action (e.g., "Ctrl+S" for ACTION_SAVE).
 * Returns the first key bound to this action.
 * Writes to buffer and returns buffer, or returns NULL if not bound.
 */
const char *keybinding_key_string(enum editor_action action, char *buffer, size_t size);

/*
 * Parse a key string like "Ctrl+S" or "Alt+Shift+Z" into a key code.
 * Returns the key code, or 0 if parsing fails.
 */
int keybinding_parse_key(const char *str);

/*
 * Parse an action name like "save" or "toggle_line_numbers" into an action.
 * Returns the action, or ACTION_NONE if parsing fails.
 */
enum editor_action keybinding_parse_action(const char *str);

#endif /* EDIT_KEYBINDINGS_H */
