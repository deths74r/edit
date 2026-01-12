/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * command.c - Leader key command mode
 *
 * Implements a quasi-modal command system triggered by Ctrl+Space.
 * Users press Ctrl+Space to enter command mode, then a single key
 * to execute a command.
 */

#include "command.h"
#include "types.h"
#include <stdbool.h>
#include <ctype.h>

/* External function from edit.c to execute actions */
extern bool execute_action(enum editor_action action);
extern void editor_set_status_message(const char *format, ...);

/*****************************************************************************
 * Module State
 *****************************************************************************/

static enum command_mode_state current_state = COMMAND_MODE_NONE;

/*****************************************************************************
 * Command Mappings
 *
 * Top-level commands are single keys that execute immediately.
 * Category keys enter a submenu for more commands.
 *****************************************************************************/

/* Map a key to an action for top-level command mode */
static enum editor_action
command_top_level_action(int key)
{
	switch (tolower(key)) {
	/* Common editing operations */
	case 'c': return ACTION_COPY;
	case 'x': return ACTION_CUT;
	case 'v': return ACTION_PASTE;
	case 'z': return ACTION_UNDO;
	case 'y': return ACTION_REDO;
	case 'd': return ACTION_DUPLICATE_LINE;
	case 'k': return ACTION_DELETE_LINE;
	case 's': return ACTION_SAVE;
	case 'w': return ACTION_SELECT_WORD;
	case 'a': return ACTION_SELECT_ALL;
	case 'g': return ACTION_GO_TO_LINE;
	case '/': return ACTION_TOGGLE_COMMENT;
	case ']': return ACTION_JUMP_TO_MATCH;
	case 'n': return ACTION_ADD_CURSOR_NEXT;  /* Add cursor at next occurrence */
	default:  return ACTION_NONE;
	}
}

/* Map a key to an action for file submenu */
static enum editor_action
command_file_action(int key)
{
	switch (tolower(key)) {
	case 's': return ACTION_SAVE;
	case 'a': return ACTION_SAVE_AS;
	case 'o': return ACTION_OPEN;
	case 'n': return ACTION_NEW;
	case 'q': return ACTION_QUIT;
	case 't': return ACTION_OPEN_TAB;
	default:  return ACTION_NONE;
	}
}

/* Map a key to an action for view submenu */
static enum editor_action
command_view_action(int key)
{
	switch (tolower(key)) {
	case 'l': return ACTION_TOGGLE_LINE_NUMBERS;
	case 'w': return ACTION_CYCLE_WRAP_MODE;
	case 'c': return ACTION_CYCLE_COLOR_COLUMN;
	case 'm': return ACTION_TOGGLE_HYBRID_MODE;
	case 't': return ACTION_THEME_PICKER;
	case 'h': return ACTION_TOGGLE_WHITESPACE;
	case 'i': return ACTION_CYCLE_WRAP_INDICATOR;
	default:  return ACTION_NONE;
	}
}

/* Map a key to an action for search submenu */
static enum editor_action
command_search_action(int key)
{
	switch (tolower(key)) {
	case 'f': return ACTION_FIND;
	case 'r': return ACTION_FIND_REPLACE;
	case 'n': return ACTION_FIND_NEXT;
	case 'p': return ACTION_FIND_PREV;
	default:  return ACTION_NONE;
	}
}

/*****************************************************************************
 * State Management
 *****************************************************************************/

void
command_mode_enter(void)
{
	current_state = COMMAND_MODE_TOP;
	editor_set_status_message("[Command]");
}

void
command_mode_exit(void)
{
	current_state = COMMAND_MODE_NONE;
	editor_set_status_message("");
}

bool
command_mode_active(void)
{
	return current_state != COMMAND_MODE_NONE;
}

enum command_mode_state
command_mode_get_state(void)
{
	return current_state;
}

const char *
command_mode_status_message(void)
{
	switch (current_state) {
	case COMMAND_MODE_NONE:   return NULL;
	case COMMAND_MODE_TOP:    return "[Command]";
	case COMMAND_MODE_FILE:   return "[Command: File]";
	case COMMAND_MODE_VIEW:   return "[Command: View]";
	case COMMAND_MODE_SEARCH: return "[Command: Search]";
	}
	return NULL;
}

/*****************************************************************************
 * Key Handling
 *****************************************************************************/

/*
 * Handle a key while in top-level command mode.
 * Returns true if key was consumed.
 */
static bool
handle_top_level_key(int key)
{
	/* Escape exits command mode */
	if (key == '\x1b') {
		command_mode_exit();
		return true;
	}

	/* Check for category keys that enter submenus */
	switch (tolower(key)) {
	case 'f':
		current_state = COMMAND_MODE_FILE;
		editor_set_status_message("[Command: File] s=save a=save-as o=open n=new q=quit");
		return true;
	case 'e':  /* 'e' for view since 'v' is paste */
		current_state = COMMAND_MODE_VIEW;
		editor_set_status_message("[Command: View] l=lines w=wrap c=column t=theme h=whitespace");
		return true;
	case 'r':  /* 'r' for search/replace */
		current_state = COMMAND_MODE_SEARCH;
		editor_set_status_message("[Command: Search] f=find r=replace n=next p=prev");
		return true;
	case '?':
		/* Help - show available commands */
		editor_set_status_message("[Command] c=copy x=cut v=paste z=undo y=redo s=save f=file e=view r=search");
		return true;
	}

	/* Check for direct action keys */
	enum editor_action action = command_top_level_action(key);
	if (action != ACTION_NONE) {
		command_mode_exit();
		execute_action(action);
		return true;
	}

	/* Invalid key - show help hint */
	editor_set_status_message("[Command] Unknown key '%c' - press ? for help, Esc to cancel",
	                          isprint(key) ? key : '?');
	return true;
}

/*
 * Handle a key while in a submenu.
 * Returns true if key was consumed.
 */
static bool
handle_submenu_key(int key, enum editor_action (*action_func)(int))
{
	/* Escape goes back to top level */
	if (key == '\x1b') {
		current_state = COMMAND_MODE_TOP;
		editor_set_status_message("[Command]");
		return true;
	}

	enum editor_action action = action_func(key);
	if (action != ACTION_NONE) {
		command_mode_exit();
		execute_action(action);
		return true;
	}

	/* Invalid key in submenu */
	editor_set_status_message("%s Unknown key '%c' - press Esc to go back",
	                          command_mode_status_message(),
	                          isprint(key) ? key : '?');
	return true;
}

bool
command_mode_handle_key(int key)
{
	if (current_state == COMMAND_MODE_NONE)
		return false;

	switch (current_state) {
	case COMMAND_MODE_NONE:
		return false;

	case COMMAND_MODE_TOP:
		return handle_top_level_key(key);

	case COMMAND_MODE_FILE:
		return handle_submenu_key(key, command_file_action);

	case COMMAND_MODE_VIEW:
		return handle_submenu_key(key, command_view_action);

	case COMMAND_MODE_SEARCH:
		return handle_submenu_key(key, command_search_action);
	}

	return false;
}
