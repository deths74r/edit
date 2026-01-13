/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * keybindings.c - Customizable key binding system
 *
 * Implements a table-based keybinding system that maps key codes to
 * editor actions. Supports loading from config files and presets.
 */

#include "keybindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*****************************************************************************
 * Module State
 *****************************************************************************/

static struct keybinding bindings[MAX_KEYBINDINGS];
static int binding_count = 0;

/*****************************************************************************
 * Action Name Table
 *****************************************************************************/

static const struct {
	enum editor_action action;
	const char *name;
} action_names[] = {
	{ ACTION_QUIT, "quit" },
	{ ACTION_SAVE, "save" },
	{ ACTION_SAVE_AS, "save_as" },
	{ ACTION_OPEN, "open" },
	{ ACTION_NEW, "new" },
	{ ACTION_UNDO, "undo" },
	{ ACTION_REDO, "redo" },
	{ ACTION_CUT, "cut" },
	{ ACTION_COPY, "copy" },
	{ ACTION_PASTE, "paste" },
	{ ACTION_DELETE_LINE, "delete_line" },
	{ ACTION_DUPLICATE_LINE, "duplicate_line" },
	{ ACTION_MOVE_UP, "move_up" },
	{ ACTION_MOVE_DOWN, "move_down" },
	{ ACTION_MOVE_LEFT, "move_left" },
	{ ACTION_MOVE_RIGHT, "move_right" },
	{ ACTION_MOVE_WORD_LEFT, "move_word_left" },
	{ ACTION_MOVE_WORD_RIGHT, "move_word_right" },
	{ ACTION_MOVE_LINE_START, "move_line_start" },
	{ ACTION_MOVE_LINE_END, "move_line_end" },
	{ ACTION_MOVE_PAGE_UP, "move_page_up" },
	{ ACTION_MOVE_PAGE_DOWN, "move_page_down" },
	{ ACTION_MOVE_FILE_START, "move_file_start" },
	{ ACTION_MOVE_FILE_END, "move_file_end" },
	{ ACTION_GO_TO_LINE, "go_to_line" },
	{ ACTION_SELECT_UP, "select_up" },
	{ ACTION_SELECT_DOWN, "select_down" },
	{ ACTION_SELECT_LEFT, "select_left" },
	{ ACTION_SELECT_RIGHT, "select_right" },
	{ ACTION_SELECT_WORD_LEFT, "select_word_left" },
	{ ACTION_SELECT_WORD_RIGHT, "select_word_right" },
	{ ACTION_SELECT_LINE_START, "select_line_start" },
	{ ACTION_SELECT_LINE_END, "select_line_end" },
	{ ACTION_SELECT_PAGE_UP, "select_page_up" },
	{ ACTION_SELECT_PAGE_DOWN, "select_page_down" },
	{ ACTION_SELECT_ALL, "select_all" },
	{ ACTION_SELECT_WORD, "select_word" },
	{ ACTION_ADD_CURSOR_NEXT, "add_cursor_next" },
	{ ACTION_FIND, "find" },
	{ ACTION_FIND_REPLACE, "find_replace" },
	{ ACTION_FIND_NEXT, "find_next" },
	{ ACTION_FIND_PREV, "find_prev" },
	{ ACTION_MOVE_LINE_UP, "move_line_up" },
	{ ACTION_MOVE_LINE_DOWN, "move_line_down" },
	{ ACTION_TOGGLE_COMMENT, "toggle_comment" },
	{ ACTION_JUMP_TO_MATCH, "jump_to_match" },
	{ ACTION_TOGGLE_LINE_NUMBERS, "toggle_line_numbers" },
	{ ACTION_TOGGLE_WHITESPACE, "toggle_whitespace" },
	{ ACTION_CYCLE_WRAP_MODE, "cycle_wrap_mode" },
	{ ACTION_CYCLE_WRAP_INDICATOR, "cycle_wrap_indicator" },
	{ ACTION_CYCLE_COLOR_COLUMN, "cycle_color_column" },
	{ ACTION_TOGGLE_HYBRID_MODE, "toggle_hybrid_mode" },
	{ ACTION_TOGGLE_BAR_POSITION, "toggle_bar_position" },
	{ ACTION_HELP, "help" },
	{ ACTION_THEME_PICKER, "theme_picker" },
	{ ACTION_CHECK_UPDATES, "check_updates" },
	{ ACTION_FORMAT_TABLES, "format_tables" },
	{ ACTION_ESCAPE, "escape" },
	{ ACTION_INSERT_TAB, "insert_tab" },
	{ ACTION_INSERT_BACKTAB, "insert_backtab" },
	{ ACTION_INSERT_NEWLINE, "insert_newline" },
	{ ACTION_BACKSPACE, "backspace" },
	{ ACTION_DELETE, "delete" },
	{ ACTION_CONTEXT_PREV, "context_prev" },
	{ ACTION_CONTEXT_NEXT, "context_next" },
	{ ACTION_CONTEXT_CLOSE, "context_close" },
	{ ACTION_NEW_TAB, "new_tab" },
	{ ACTION_OPEN_TAB, "open_tab" },
	{ ACTION_NONE, NULL }
};

/*****************************************************************************
 * Binding Table Operations
 *****************************************************************************/

enum editor_action
keybinding_lookup(int key)
{
	for (int i = 0; i < binding_count; i++) {
		if (bindings[i].key == key)
			return bindings[i].action;
	}
	return ACTION_NONE;
}

int
keybinding_add(int key, enum editor_action action)
{
	/* Check if key is already bound */
	for (int i = 0; i < binding_count; i++) {
		if (bindings[i].key == key) {
			bindings[i].action = action;
			return 0;
		}
	}

	/* Add new binding */
	if (binding_count >= MAX_KEYBINDINGS)
		return -1;

	bindings[binding_count].key = key;
	bindings[binding_count].action = action;
	binding_count++;
	return 0;
}

int
keybinding_remove(int key)
{
	for (int i = 0; i < binding_count; i++) {
		if (bindings[i].key == key) {
			/* Shift remaining entries down */
			for (int j = i; j < binding_count - 1; j++)
				bindings[j] = bindings[j + 1];
			binding_count--;
			return 0;
		}
	}
	return -1;
}

/*****************************************************************************
 * Default Bindings
 *****************************************************************************/

void
keybinding_load_defaults(void)
{
	binding_count = 0;

	/* File operations */
	keybinding_add(CONTROL_KEY('q'), ACTION_CONTEXT_CLOSE);
	keybinding_add(CONTROL_KEY('s'), ACTION_SAVE);
	keybinding_add(KEY_ALT_SHIFT_S, ACTION_SAVE_AS);
	keybinding_add(KEY_CTRL_O, ACTION_OPEN);
	keybinding_add(KEY_CTRL_N, ACTION_NEW);
	keybinding_add(KEY_ALT_N, ACTION_NEW_TAB);
	keybinding_add(KEY_ALT_O, ACTION_OPEN_TAB);

	/* Edit operations */
	keybinding_add(CONTROL_KEY('z'), ACTION_UNDO);
	keybinding_add(CONTROL_KEY('y'), ACTION_REDO);
	keybinding_add(CONTROL_KEY('x'), ACTION_CUT);
	keybinding_add(CONTROL_KEY('c'), ACTION_COPY);
	keybinding_add(CONTROL_KEY('v'), ACTION_PASTE);
	keybinding_add(KEY_ALT_K, ACTION_DELETE_LINE);
	keybinding_add(KEY_ALT_D, ACTION_DUPLICATE_LINE);

	/* Cursor movement */
	keybinding_add(KEY_ARROW_UP, ACTION_MOVE_UP);
	keybinding_add(KEY_ARROW_DOWN, ACTION_MOVE_DOWN);
	keybinding_add(KEY_ARROW_LEFT, ACTION_MOVE_LEFT);
	keybinding_add(KEY_ARROW_RIGHT, ACTION_MOVE_RIGHT);
	keybinding_add(KEY_CTRL_ARROW_LEFT, ACTION_MOVE_WORD_LEFT);
	keybinding_add(KEY_CTRL_ARROW_RIGHT, ACTION_MOVE_WORD_RIGHT);
	keybinding_add(KEY_HOME, ACTION_MOVE_LINE_START);
	keybinding_add(KEY_END, ACTION_MOVE_LINE_END);
	keybinding_add(KEY_PAGE_UP, ACTION_MOVE_PAGE_UP);
	keybinding_add(KEY_PAGE_DOWN, ACTION_MOVE_PAGE_DOWN);
	keybinding_add(KEY_CTRL_HOME, ACTION_MOVE_FILE_START);
	keybinding_add(KEY_CTRL_END, ACTION_MOVE_FILE_END);
	keybinding_add(CONTROL_KEY('g'), ACTION_GO_TO_LINE);

	/* Selection */
	keybinding_add(KEY_SHIFT_ARROW_UP, ACTION_SELECT_UP);
	keybinding_add(KEY_SHIFT_ARROW_DOWN, ACTION_SELECT_DOWN);
	keybinding_add(KEY_SHIFT_ARROW_LEFT, ACTION_SELECT_LEFT);
	keybinding_add(KEY_SHIFT_ARROW_RIGHT, ACTION_SELECT_RIGHT);
	keybinding_add(KEY_CTRL_SHIFT_ARROW_LEFT, ACTION_SELECT_WORD_LEFT);
	keybinding_add(KEY_CTRL_SHIFT_ARROW_RIGHT, ACTION_SELECT_WORD_RIGHT);
	keybinding_add(KEY_SHIFT_HOME, ACTION_SELECT_LINE_START);
	keybinding_add(KEY_SHIFT_END, ACTION_SELECT_LINE_END);
	keybinding_add(KEY_SHIFT_PAGE_UP, ACTION_SELECT_PAGE_UP);
	keybinding_add(KEY_SHIFT_PAGE_DOWN, ACTION_SELECT_PAGE_DOWN);
	keybinding_add(CONTROL_KEY('a'), ACTION_SELECT_ALL);
	keybinding_add(KEY_ALT_W, ACTION_SELECT_WORD);
	keybinding_add(CONTROL_KEY('d'), ACTION_ADD_CURSOR_NEXT);

	/* Search */
	keybinding_add(CONTROL_KEY('f'), ACTION_FIND);
	keybinding_add(CONTROL_KEY('r'), ACTION_FIND_REPLACE);
	keybinding_add(CONTROL_KEY('h'), ACTION_FIND_REPLACE);  /* Alt binding */
	keybinding_add(KEY_F3, ACTION_FIND_NEXT);
	keybinding_add(KEY_SHIFT_F3, ACTION_FIND_PREV);

	/* Line operations */
	keybinding_add(KEY_ALT_ARROW_UP, ACTION_MOVE_LINE_UP);
	keybinding_add(KEY_ALT_ARROW_DOWN, ACTION_MOVE_LINE_DOWN);
	keybinding_add(KEY_ALT_SLASH, ACTION_TOGGLE_COMMENT);
	keybinding_add(0x1f, ACTION_TOGGLE_COMMENT);  /* Ctrl+/ */
	keybinding_add(KEY_ALT_BRACKET, ACTION_JUMP_TO_MATCH);
	keybinding_add(0x1d, ACTION_JUMP_TO_MATCH);  /* Ctrl+] */

	/* View toggles */
	keybinding_add(KEY_ALT_L, ACTION_TOGGLE_LINE_NUMBERS);
	keybinding_add(KEY_ALT_SHIFT_W, ACTION_TOGGLE_WHITESPACE);
	keybinding_add(KEY_ALT_Z, ACTION_CYCLE_WRAP_MODE);
	keybinding_add(KEY_ALT_SHIFT_Z, ACTION_CYCLE_WRAP_INDICATOR);
	keybinding_add(KEY_ALT_SHIFT_C, ACTION_CYCLE_COLOR_COLUMN);
	keybinding_add(KEY_ALT_M, ACTION_TOGGLE_HYBRID_MODE);

	/* Dialogs */
	keybinding_add(KEY_F1, ACTION_HELP);
	keybinding_add(KEY_CTRL_T, ACTION_THEME_PICKER);
	keybinding_add(KEY_ALT_U, ACTION_CHECK_UPDATES);
	keybinding_add(KEY_ALT_T, ACTION_FORMAT_TABLES);
	/* Buffer switching */
	keybinding_add(KEY_ALT_ARROW_LEFT, ACTION_CONTEXT_PREV);
	keybinding_add(KEY_ALT_ARROW_RIGHT, ACTION_CONTEXT_NEXT);

	/* Special keys */
	keybinding_add(27, ACTION_ESCAPE);  /* ESC */
	keybinding_add('\t', ACTION_INSERT_TAB);
	keybinding_add(KEY_SHIFT_TAB, ACTION_INSERT_BACKTAB);
	keybinding_add('\r', ACTION_INSERT_NEWLINE);
	keybinding_add(KEY_BACKSPACE, ACTION_BACKSPACE);
	keybinding_add(KEY_DELETE, ACTION_DELETE);
}
/*****************************************************************************
 * Leader Mode Preset
 *
 * Minimal keybindings for use with leader key (Ctrl+Space) command mode.
 * Only navigation, selection, and text entry keys are bound directly.
 * All other commands are accessed via Ctrl+Space followed by a key.
 *****************************************************************************/
static void
keybinding_load_leader_mode(void)
{
	binding_count = 0;
	/* Navigation - arrows, Home/End, Page Up/Down */
	keybinding_add(KEY_ARROW_UP, ACTION_MOVE_UP);
	keybinding_add(KEY_ARROW_DOWN, ACTION_MOVE_DOWN);
	keybinding_add(KEY_ARROW_LEFT, ACTION_MOVE_LEFT);
	keybinding_add(KEY_ARROW_RIGHT, ACTION_MOVE_RIGHT);
	keybinding_add(KEY_CTRL_ARROW_LEFT, ACTION_MOVE_WORD_LEFT);
	keybinding_add(KEY_CTRL_ARROW_RIGHT, ACTION_MOVE_WORD_RIGHT);
	keybinding_add(KEY_HOME, ACTION_MOVE_LINE_START);
	keybinding_add(KEY_END, ACTION_MOVE_LINE_END);
	keybinding_add(KEY_PAGE_UP, ACTION_MOVE_PAGE_UP);
	keybinding_add(KEY_PAGE_DOWN, ACTION_MOVE_PAGE_DOWN);
	keybinding_add(KEY_CTRL_HOME, ACTION_MOVE_FILE_START);
	keybinding_add(KEY_CTRL_END, ACTION_MOVE_FILE_END);
	/* Selection - shift+navigation */
	keybinding_add(KEY_SHIFT_ARROW_UP, ACTION_SELECT_UP);
	keybinding_add(KEY_SHIFT_ARROW_DOWN, ACTION_SELECT_DOWN);
	keybinding_add(KEY_SHIFT_ARROW_LEFT, ACTION_SELECT_LEFT);
	keybinding_add(KEY_SHIFT_ARROW_RIGHT, ACTION_SELECT_RIGHT);
	keybinding_add(KEY_CTRL_SHIFT_ARROW_LEFT, ACTION_SELECT_WORD_LEFT);
	keybinding_add(KEY_CTRL_SHIFT_ARROW_RIGHT, ACTION_SELECT_WORD_RIGHT);
	keybinding_add(KEY_SHIFT_HOME, ACTION_SELECT_LINE_START);
	keybinding_add(KEY_SHIFT_END, ACTION_SELECT_LINE_END);
	keybinding_add(KEY_SHIFT_PAGE_UP, ACTION_SELECT_PAGE_UP);
	keybinding_add(KEY_SHIFT_PAGE_DOWN, ACTION_SELECT_PAGE_DOWN);
	/* Line movement with Alt+arrows (useful enough to keep) */
	keybinding_add(KEY_ALT_ARROW_UP, ACTION_MOVE_LINE_UP);
	keybinding_add(KEY_ALT_ARROW_DOWN, ACTION_MOVE_LINE_DOWN);
	/* Buffer switching with Alt+left/right */
	keybinding_add(KEY_ALT_ARROW_LEFT, ACTION_CONTEXT_PREV);
	keybinding_add(KEY_ALT_ARROW_RIGHT, ACTION_CONTEXT_NEXT);
	/* Text entry keys */
	keybinding_add(27, ACTION_ESCAPE);  /* ESC */
	keybinding_add('\t', ACTION_INSERT_TAB);
	keybinding_add(KEY_SHIFT_TAB, ACTION_INSERT_BACKTAB);
	keybinding_add('\r', ACTION_INSERT_NEWLINE);
	keybinding_add(KEY_BACKSPACE, ACTION_BACKSPACE);
	keybinding_add(KEY_DELETE, ACTION_DELETE);
	/* F-keys for search navigation (hands stay on keyboard) */
	keybinding_add(KEY_F1, ACTION_HELP);
	keybinding_add(KEY_F3, ACTION_FIND_NEXT);
	keybinding_add(KEY_SHIFT_F3, ACTION_FIND_PREV);
}

/*****************************************************************************
 * macOS Preset
 *
 * Remaps common operations to Alt+key since Cmd is not available in terminal.
 *****************************************************************************/

static void
keybinding_apply_macos_overrides(void)
{
	/* File operations - use Alt instead of Ctrl */
	keybinding_add(KEY_ALT_A, ACTION_SELECT_ALL);  /* Alt+A for select all */

	/*
	 * Note: We keep Ctrl+Q/S/C/X/V/Z/Y as they are since they work in
	 * most terminals. Users can override individual keys in their config.
	 * The macOS preset mainly adds Alt alternatives and adjusts a few
	 * bindings that conflict with terminal behavior.
	 */
}

int
keybinding_load_preset(const char *name)
{
	if (strcmp(name, "default") == 0) {
		keybinding_load_defaults();
		return 0;
	}

	if (strcmp(name, "macos") == 0) {
		keybinding_load_defaults();
		keybinding_apply_macos_overrides();
		return 0;
	}
	if (strcmp(name, "leader") == 0) {
		keybinding_load_leader_mode();
		return 0;
	}

	return -1;
}

/*****************************************************************************
 * Key String Parser
 *
 * Parses strings like "Ctrl+S", "Alt+Shift+Z", "F1", "Escape"
 *****************************************************************************/

int
keybinding_parse_key(const char *str)
{
	if (!str || !*str)
		return 0;

	bool ctrl = false;
	bool alt = false;
	bool shift = false;

	/* Parse modifiers */
	while (*str) {
		if (strncasecmp(str, "Ctrl+", 5) == 0 ||
		    strncasecmp(str, "Control+", 8) == 0) {
			ctrl = true;
			str = strchr(str, '+') + 1;
		} else if (strncasecmp(str, "Alt+", 4) == 0 ||
		           strncasecmp(str, "Meta+", 5) == 0 ||
		           strncasecmp(str, "Option+", 7) == 0) {
			alt = true;
			str = strchr(str, '+') + 1;
		} else if (strncasecmp(str, "Shift+", 6) == 0) {
			shift = true;
			str = strchr(str, '+') + 1;
		} else {
			break;
		}
	}

	/* Parse base key */
	if (strcasecmp(str, "Escape") == 0 || strcasecmp(str, "Esc") == 0)
		return 27;
	if (strcasecmp(str, "Tab") == 0)
		return shift ? KEY_SHIFT_TAB : '\t';
	if (strcasecmp(str, "Enter") == 0 || strcasecmp(str, "Return") == 0)
		return '\r';
	if (strcasecmp(str, "Backspace") == 0)
		return KEY_BACKSPACE;
	if (strcasecmp(str, "Delete") == 0 || strcasecmp(str, "Del") == 0)
		return KEY_DELETE;
	if (strcasecmp(str, "Home") == 0) {
		if (ctrl) return KEY_CTRL_HOME;
		if (shift) return KEY_SHIFT_HOME;
		return KEY_HOME;
	}
	if (strcasecmp(str, "End") == 0) {
		if (ctrl) return KEY_CTRL_END;
		if (shift) return KEY_SHIFT_END;
		return KEY_END;
	}
	if (strcasecmp(str, "PageUp") == 0 || strcasecmp(str, "PgUp") == 0) {
		if (shift) return KEY_SHIFT_PAGE_UP;
		return KEY_PAGE_UP;
	}
	if (strcasecmp(str, "PageDown") == 0 || strcasecmp(str, "PgDn") == 0) {
		if (shift) return KEY_SHIFT_PAGE_DOWN;
		return KEY_PAGE_DOWN;
	}
	if (strcasecmp(str, "Up") == 0 || strcasecmp(str, "ArrowUp") == 0) {
		if (alt) return KEY_ALT_ARROW_UP;
		if (shift) return KEY_SHIFT_ARROW_UP;
		return KEY_ARROW_UP;
	}
	if (strcasecmp(str, "Down") == 0 || strcasecmp(str, "ArrowDown") == 0) {
		if (alt) return KEY_ALT_ARROW_DOWN;
		if (shift) return KEY_SHIFT_ARROW_DOWN;
		return KEY_ARROW_DOWN;
	}
	if (strcasecmp(str, "Left") == 0 || strcasecmp(str, "ArrowLeft") == 0) {
		if (ctrl && shift) return KEY_CTRL_SHIFT_ARROW_LEFT;
		if (ctrl) return KEY_CTRL_ARROW_LEFT;
		if (alt) return KEY_ALT_ARROW_LEFT;
		if (shift) return KEY_SHIFT_ARROW_LEFT;
		return KEY_ARROW_LEFT;
	}
	if (strcasecmp(str, "Right") == 0 || strcasecmp(str, "ArrowRight") == 0) {
		if (ctrl && shift) return KEY_CTRL_SHIFT_ARROW_RIGHT;
		if (ctrl) return KEY_CTRL_ARROW_RIGHT;
		if (alt) return KEY_ALT_ARROW_RIGHT;
		if (shift) return KEY_SHIFT_ARROW_RIGHT;
		return KEY_ARROW_RIGHT;
	}

	/* Function keys */
	if (str[0] == 'F' || str[0] == 'f') {
		int num = atoi(str + 1);
		if (num == 1) return KEY_F1;
		if (num == 3) return shift ? KEY_SHIFT_F3 : KEY_F3;
		/* Add more F-keys as needed */
	}

	/* Single letter with modifiers */
	if (strlen(str) == 1 && isalpha((unsigned char)str[0])) {
		char c = tolower((unsigned char)str[0]);

		if (ctrl)
			return CONTROL_KEY(c);

		if (alt) {
			/* Map to KEY_ALT_* constants */
			switch (c) {
			case 'a': return KEY_ALT_A;
			case 'c': return KEY_ALT_C;
			case 'd': return KEY_ALT_D;
			case 'k': return KEY_ALT_K;
			case 'l': return KEY_ALT_L;
			case 'm': return KEY_ALT_M;
			case 't': return KEY_ALT_T;
			case 'n': return KEY_ALT_N;
			case 'o': return KEY_ALT_O;
			case 'p': return KEY_ALT_P;
			case 'r': return KEY_ALT_R;
			case 'u': return KEY_ALT_U;
			case 'w': return KEY_ALT_W;
			case 'z': return shift ? KEY_ALT_SHIFT_Z : KEY_ALT_Z;
			case 's': return shift ? KEY_ALT_SHIFT_S : 0;
			/* Add KEY_ALT_S etc as they are added to types.h */
			}
		}
	}

	/* Special symbols */
	if (strcmp(str, "/") == 0 || strcmp(str, "Slash") == 0) {
		if (alt) return KEY_ALT_SLASH;
		if (ctrl) return 0x1f;  /* Ctrl+/ */
	}
	if (strcmp(str, "]") == 0 || strcmp(str, "RightBracket") == 0) {
		if (alt) return KEY_ALT_BRACKET;
		if (ctrl) return 0x1d;  /* Ctrl+] */
	}

	return 0;
}

/*****************************************************************************
 * Action Name Parser
 *****************************************************************************/

enum editor_action
keybinding_parse_action(const char *str)
{
	if (!str)
		return ACTION_NONE;

	for (int i = 0; action_names[i].name; i++) {
		if (strcasecmp(str, action_names[i].name) == 0)
			return action_names[i].action;
	}
	return ACTION_NONE;
}

const char *
keybinding_action_name(enum editor_action action)
{
	for (int i = 0; action_names[i].name; i++) {
		if (action_names[i].action == action)
			return action_names[i].name;
	}
	return NULL;
}

/*****************************************************************************
 * Key String Generation (for help display)
 *****************************************************************************/

const char *
keybinding_key_string(enum editor_action action, char *buffer, size_t size)
{
	/* Find first key bound to this action */
	for (int i = 0; i < binding_count; i++) {
		if (bindings[i].action != action)
			continue;

		int key = bindings[i].key;
		buffer[0] = '\0';

		/* Check for known KEY_* constants */
		switch (key) {
		case KEY_F1: snprintf(buffer, size, "F1"); return buffer;
		case KEY_F3: snprintf(buffer, size, "F3"); return buffer;
		case KEY_SHIFT_F3: snprintf(buffer, size, "Shift+F3"); return buffer;
		case KEY_ARROW_UP: snprintf(buffer, size, "Up"); return buffer;
		case KEY_ARROW_DOWN: snprintf(buffer, size, "Down"); return buffer;
		case KEY_ARROW_LEFT: snprintf(buffer, size, "Left"); return buffer;
		case KEY_ARROW_RIGHT: snprintf(buffer, size, "Right"); return buffer;
		case KEY_HOME: snprintf(buffer, size, "Home"); return buffer;
		case KEY_END: snprintf(buffer, size, "End"); return buffer;
		case KEY_PAGE_UP: snprintf(buffer, size, "PgUp"); return buffer;
		case KEY_PAGE_DOWN: snprintf(buffer, size, "PgDn"); return buffer;
		case KEY_DELETE: snprintf(buffer, size, "Del"); return buffer;
		case KEY_BACKSPACE: snprintf(buffer, size, "Backspace"); return buffer;
		case KEY_SHIFT_TAB: snprintf(buffer, size, "Shift+Tab"); return buffer;
		case KEY_CTRL_HOME: snprintf(buffer, size, "Ctrl+Home"); return buffer;
		case KEY_CTRL_END: snprintf(buffer, size, "Ctrl+End"); return buffer;
		case KEY_CTRL_ARROW_LEFT: snprintf(buffer, size, "Ctrl+Left"); return buffer;
		case KEY_CTRL_ARROW_RIGHT: snprintf(buffer, size, "Ctrl+Right"); return buffer;
		case KEY_SHIFT_ARROW_UP: snprintf(buffer, size, "Shift+Up"); return buffer;
		case KEY_SHIFT_ARROW_DOWN: snprintf(buffer, size, "Shift+Down"); return buffer;
		case KEY_SHIFT_ARROW_LEFT: snprintf(buffer, size, "Shift+Left"); return buffer;
		case KEY_SHIFT_ARROW_RIGHT: snprintf(buffer, size, "Shift+Right"); return buffer;
		case KEY_SHIFT_HOME: snprintf(buffer, size, "Shift+Home"); return buffer;
		case KEY_SHIFT_END: snprintf(buffer, size, "Shift+End"); return buffer;
		case KEY_SHIFT_PAGE_UP: snprintf(buffer, size, "Shift+PgUp"); return buffer;
		case KEY_SHIFT_PAGE_DOWN: snprintf(buffer, size, "Shift+PgDn"); return buffer;
		case KEY_CTRL_SHIFT_ARROW_LEFT: snprintf(buffer, size, "Ctrl+Shift+Left"); return buffer;
		case KEY_CTRL_SHIFT_ARROW_RIGHT: snprintf(buffer, size, "Ctrl+Shift+Right"); return buffer;
		case KEY_ALT_A: snprintf(buffer, size, "Alt+A"); return buffer;
		case KEY_ALT_C: snprintf(buffer, size, "Alt+C"); return buffer;
		case KEY_ALT_D: snprintf(buffer, size, "Alt+D"); return buffer;
		case KEY_ALT_K: snprintf(buffer, size, "Alt+K"); return buffer;
		case KEY_ALT_L: snprintf(buffer, size, "Alt+L"); return buffer;
		case KEY_ALT_M: snprintf(buffer, size, "Alt+M"); return buffer;
		case KEY_ALT_T: snprintf(buffer, size, "Alt+T"); return buffer;
		case KEY_ALT_N: snprintf(buffer, size, "Alt+N"); return buffer;
		case KEY_ALT_O: snprintf(buffer, size, "Alt+O"); return buffer;
		case KEY_ALT_P: snprintf(buffer, size, "Alt+P"); return buffer;
		case KEY_ALT_R: snprintf(buffer, size, "Alt+R"); return buffer;
		case KEY_ALT_U: snprintf(buffer, size, "Alt+U"); return buffer;
		case KEY_ALT_W: snprintf(buffer, size, "Alt+W"); return buffer;
		case KEY_ALT_Z: snprintf(buffer, size, "Alt+Z"); return buffer;
		case KEY_ALT_SHIFT_Z: snprintf(buffer, size, "Alt+Shift+Z"); return buffer;
		case KEY_ALT_SHIFT_S: snprintf(buffer, size, "Alt+Shift+S"); return buffer;
		case KEY_ALT_SHIFT_W: snprintf(buffer, size, "Alt+Shift+W"); return buffer;
		case KEY_ALT_SHIFT_C: snprintf(buffer, size, "Alt+Shift+C"); return buffer;
		case KEY_ALT_ARROW_UP: snprintf(buffer, size, "Alt+Up"); return buffer;
		case KEY_ALT_ARROW_DOWN: snprintf(buffer, size, "Alt+Down"); return buffer;
		case KEY_ALT_ARROW_LEFT: snprintf(buffer, size, "Alt+Left"); return buffer;
		case KEY_ALT_ARROW_RIGHT: snprintf(buffer, size, "Alt+Right"); return buffer;
		case KEY_ALT_SLASH: snprintf(buffer, size, "Alt+/"); return buffer;
		case KEY_ALT_BRACKET: snprintf(buffer, size, "Alt+]"); return buffer;
		case KEY_CTRL_O: snprintf(buffer, size, "Ctrl+O"); return buffer;
		case KEY_CTRL_N: snprintf(buffer, size, "Ctrl+N"); return buffer;
		case KEY_CTRL_T: snprintf(buffer, size, "Ctrl+T"); return buffer;
		case KEY_CTRL_W: snprintf(buffer, size, "Ctrl+W"); return buffer;
		case KEY_CTRL_SHIFT_N: snprintf(buffer, size, "Ctrl+Shift+N"); return buffer;
		case KEY_CTRL_SHIFT_O: snprintf(buffer, size, "Ctrl+Shift+O"); return buffer;
		case KEY_SHIFT_SPACE: snprintf(buffer, size, "Shift+Space"); return buffer;
		case KEY_CTRL_ENTER: snprintf(buffer, size, "Ctrl+Enter"); return buffer;
		case 27: snprintf(buffer, size, "Esc"); return buffer;
		case '\t': snprintf(buffer, size, "Tab"); return buffer;
		case '\r': snprintf(buffer, size, "Enter"); return buffer;
		case 0x1f: snprintf(buffer, size, "Ctrl+/"); return buffer;
		case 0x1d: snprintf(buffer, size, "Ctrl+]"); return buffer;
		}

		/* Check for Ctrl+letter (ASCII 1-26) */
		if (key >= 1 && key <= 26) {
			snprintf(buffer, size, "Ctrl+%c", 'A' + key - 1);
			return buffer;
		}

		/* Unknown key */
		snprintf(buffer, size, "?");
		return buffer;
	}

	return NULL;
}

/*****************************************************************************
 * Config File Loading
 *****************************************************************************/

static char *
safe_get_home(void)
{
	char *home = getenv("HOME");
	if (!home || !*home)
		return NULL;
	return home;
}

static void
trim_whitespace(char *str)
{
	/* Trim leading */
	char *start = str;
	while (isspace((unsigned char)*start))
		start++;

	if (start != str)
		memmove(str, start, strlen(start) + 1);

	/* Trim trailing */
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end))
		*end-- = '\0';
}

int
keybinding_load_file(const char *path)
{
	FILE *file = fopen(path, "r");
	if (!file)
		return -1;

	char line[512];
	bool in_bindings_section = false;

	while (fgets(line, sizeof(line), file)) {
		/* Skip comments and empty lines */
		char *p = line;
		while (isspace((unsigned char)*p))
			p++;

		if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n')
			continue;

		/* Remove trailing newline */
		char *newline = strchr(p, '\n');
		if (newline)
			*newline = '\0';

		/* Check for section header */
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = '\0';
				in_bindings_section =
					(strcasecmp(p + 1, "bindings") == 0);
			}
			continue;
		}

		/* Parse key=value */
		char *equals = strchr(p, '=');
		if (!equals)
			continue;

		*equals = '\0';
		char *key = p;
		char *value = equals + 1;

		trim_whitespace(key);
		trim_whitespace(value);

		/* Handle preset directive (outside sections) */
		if (!in_bindings_section && strcasecmp(key, "preset") == 0) {
			keybinding_load_preset(value);
			continue;
		}

		/* Handle bindings (in [bindings] section or top-level) */
		if (in_bindings_section || !strchr(key, ' ')) {
			enum editor_action action = keybinding_parse_action(key);
			if (action != ACTION_NONE) {
				int keycode = keybinding_parse_key(value);
				if (keycode != 0)
					keybinding_add(keycode, action);
			}
		}
	}

	fclose(file);
	return 0;
}

void
keybinding_init(void)
{
	/* Start with leader mode (Ctrl+Space command mode experiment) */
	keybinding_load_leader_mode();

	/* Try to load user config */
	char *home = safe_get_home();
	if (!home)
		return;

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s%s", home, KEYBINDINGS_FILE);

	/* File may not exist - that's fine */
	keybinding_load_file(path);
}
