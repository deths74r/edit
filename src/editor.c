/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * editor.c - Core editor state and operations for edit
 *
 * Provides initialization, cursor movement, text editing,
 * selection handling, and high-level editor operations.
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "../third_party/utflite-1.5.2/single_include/utflite.h"
#include "edit.h"
#include "update.h"

/*****************************************************************************
 * External Dependencies
 *****************************************************************************/

/* Editor state from edit.c */
extern struct editor_state editor;

/* Search state from search.c */
extern struct search_state search;

/* Theme globals from theme.c */
extern struct theme active_theme;
extern int current_theme_index;

/* Functions from edit.c that we call */
extern void editor_save(void);
extern int file_save(struct buffer *buffer);

/* Functions from input.c */
extern int input_read_key(void);

/* Functions from render.c */
extern int render_refresh_screen(void);

/*****************************************************************************
 * Internal State
 *****************************************************************************/

/* Go-to-line mode state */
static struct goto_state goto_line = {0};

/* Save As mode state */
static struct save_as_state save_as = {0};

/* Quit prompt state */
static struct quit_prompt_state quit_prompt = {0};

/*****************************************************************************
 * Editor Initialization
 *****************************************************************************/

/*
 * Initialize the editor state.
 */
void editor_init(void)
{
	buffer_init(&editor.buffer);
	editor.cursor_row = 0;
	editor.cursor_column = 0;
	editor.row_offset = 0;
	editor.column_offset = 0;
	editor.screen_rows = 0;
	editor.screen_columns = 0;
	editor.gutter_width = 0;
	editor.show_line_numbers = true;
	editor.status_message[0] = '\0';
	editor.status_message_time = 0;
	editor.selection_anchor_row = 0;
	editor.selection_anchor_column = 0;
	editor.selection_active = false;
	editor.wrap_mode = WRAP_WORD;
	editor.wrap_indicator = WRAP_INDICATOR_RETURN;
	editor.show_whitespace = false;
	editor.show_file_icons = true;
	editor.show_hidden_files = false;
	editor.tab_width = 4;
	editor.color_column = 0;
	editor.color_column_style = COLOR_COLUMN_SOLID;
	editor.theme_indicator = THEME_INDICATOR_CHECK;
	editor.cursor_count = 0;
	editor.primary_cursor = 0;
	editor.fuzzy_max_depth = 10;
	editor.fuzzy_max_files = 10000;
	editor.fuzzy_case_sensitive = false;

	/* Initialize theme system */
	themes_load();
	config_load();
	theme_apply_by_index(current_theme_index);

	/* Initialize worker thread */
	int err = worker_init();
	if (err) {
		log_warn("Worker thread disabled: %s", edit_strerror(err));
	}

	/* Initialize search system */
	err = search_init();
	if (err) {
		log_warn("Async search/replace disabled: %s", edit_strerror(err));
	}
}

/*
 * Perform clean exit.
 */
void editor_perform_exit(void)
{
	terminal_clear_screen();
	if (!editor.buffer.is_modified) {
		autosave_remove_swap();
	}
	search_cleanup();
	worker_shutdown();
	clipboard_cleanup();
	buffer_free(&editor.buffer);
	themes_free();
	free(active_theme.name);
	exit(0);
}

/*****************************************************************************
 * Status Messages
 *****************************************************************************/

/*
 * Set a formatted status message.
 */
void editor_set_status_message(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(editor.status_message, sizeof(editor.status_message), format, arguments);
	va_end(arguments);
	editor.status_message_time = time(NULL);
}

/*****************************************************************************
 * Screen and Viewport Management
 *****************************************************************************/

/*
 * Update gutter width based on line count.
 */
void editor_update_gutter_width(void)
{
	if (!editor.show_line_numbers) {
		editor.gutter_width = 0;
		return;
	}

	/* Calculate digits needed for largest line number */
	uint32_t max_line = editor.buffer.line_count;
	uint32_t digits = 1;
	while (max_line >= DECIMAL_BASE) {
		max_line /= DECIMAL_BASE;
		digits++;
	}

	/* Apply minimum digits and add trailing space */
	if (digits < MINIMUM_GUTTER_DIGITS) {
		digits = MINIMUM_GUTTER_DIGITS;
	}
	editor.gutter_width = digits + GUTTER_PADDING;
}

/*
 * Update screen dimensions from terminal.
 */
void editor_update_screen_size(void)
{
	uint32_t rows, cols;
	if (terminal_get_window_size(&rows, &cols) == 0) {
		/* Reserve rows for status and message bars */
		editor.screen_rows = rows - STATUS_BAR_ROWS;
		editor.screen_columns = cols;
	}
	editor_update_gutter_width();
	buffer_invalidate_all_wrap_caches(&editor.buffer);
}

/*
 * Get the text area width.
 */
uint16_t editor_get_text_width(void)
{
	if (editor.screen_columns <= editor.gutter_width) {
		return 1;
	}
	return editor.screen_columns - editor.gutter_width;
}

/*****************************************************************************
 * Color Column
 *****************************************************************************/

/*
 * Get the UTF-8 string for a color column style.
 */
const char *color_column_char(enum color_column_style style)
{
	switch (style) {
		case COLOR_COLUMN_SOLID:  return "\xe2\x94\x82";  /* U+2502 */
		case COLOR_COLUMN_DASHED: return "\xe2\x94\x86";  /* U+2506 */
		case COLOR_COLUMN_DOTTED: return "\xe2\x94\x8a";  /* U+250A */
		case COLOR_COLUMN_HEAVY:  return "\xe2\x94\x83";  /* U+2503 */
		default: return NULL;
	}
}

/*
 * Get human-readable name for color column style.
 */
const char *color_column_style_name(enum color_column_style style)
{
	switch (style) {
		case COLOR_COLUMN_BACKGROUND: return "background";
		case COLOR_COLUMN_SOLID:      return "solid";
		case COLOR_COLUMN_DASHED:     return "dashed";
		case COLOR_COLUMN_DOTTED:     return "dotted";
		case COLOR_COLUMN_HEAVY:      return "heavy";
		default: return "unknown";
	}
}

/*
 * Cycle to the next color column style.
 */
void editor_cycle_color_column_style(void)
{
	if (editor.color_column == 0) {
		editor_set_status_message("Color column is off (F4 to enable)");
		return;
	}

	editor.color_column_style = (editor.color_column_style + 1) % (COLOR_COLUMN_HEAVY + 1);

	const char *indicator = color_column_char(editor.color_column_style);
	if (indicator) {
		editor_set_status_message("Column %u style: %s",
		                          editor.color_column,
		                          color_column_style_name(editor.color_column_style));
	} else {
		editor_set_status_message("Column %u style: background only",
		                          editor.color_column);
	}
}

/*****************************************************************************
 * Selection
 *****************************************************************************/

/*
 * Start a new selection at the current cursor position.
 */
void selection_start(void)
{
	editor.selection_anchor_row = editor.cursor_row;
	editor.selection_anchor_column = editor.cursor_column;
	editor.selection_active = true;
}

/*
 * Get the normalized selection range.
 */
void selection_get_range(uint32_t *start_row, uint32_t *start_col,
                         uint32_t *end_row, uint32_t *end_col)
{
	if (editor.selection_anchor_row < editor.cursor_row ||
	    (editor.selection_anchor_row == editor.cursor_row &&
	     editor.selection_anchor_column <= editor.cursor_column)) {
		*start_row = editor.selection_anchor_row;
		*start_col = editor.selection_anchor_column;
		*end_row = editor.cursor_row;
		*end_col = editor.cursor_column;
	} else {
		*start_row = editor.cursor_row;
		*start_col = editor.cursor_column;
		*end_row = editor.selection_anchor_row;
		*end_col = editor.selection_anchor_column;
	}
}

/*
 * Clear the current selection.
 */
void selection_clear(void)
{
	editor.selection_active = false;
}

/*
 * Check if the current selection is empty (cursor at anchor).
 */
bool selection_is_empty(void)
{
	if (!editor.selection_active) {
		return true;
	}
	return (editor.cursor_row == editor.selection_anchor_row &&
	        editor.cursor_column == editor.selection_anchor_column);
}

/*****************************************************************************
 * Multi-Cursor Management
 *****************************************************************************/

/*
 * Compare two cursors by position (for qsort).
 */
static int cursor_compare(const void *a, const void *b)
{
	const struct cursor *cursor_a = (const struct cursor *)a;
	const struct cursor *cursor_b = (const struct cursor *)b;

	if (cursor_a->row != cursor_b->row) {
		return (cursor_a->row < cursor_b->row) ? -1 : 1;
	}
	if (cursor_a->column != cursor_b->column) {
		return (cursor_a->column < cursor_b->column) ? -1 : 1;
	}
	return 0;
}

/*
 * Transition from single-cursor to multi-cursor mode.
 */
static void multicursor_enter(void)
{
	if (editor.cursor_count > 0) {
		return;
	}

	editor.cursors[0].row = editor.cursor_row;
	editor.cursors[0].column = editor.cursor_column;
	editor.cursors[0].anchor_row = editor.selection_anchor_row;
	editor.cursors[0].anchor_column = editor.selection_anchor_column;
	editor.cursors[0].has_selection = editor.selection_active;

	editor.cursor_count = 1;
	editor.primary_cursor = 0;
}

/*
 * Exit multi-cursor mode, keeping only the primary cursor.
 */
void editor_multi_cursor_exit(void)
{
	if (editor.cursor_count == 0) {
		return;
	}

	struct cursor *primary = &editor.cursors[editor.primary_cursor];
	editor.cursor_row = primary->row;
	editor.cursor_column = primary->column;
	editor.selection_anchor_row = primary->anchor_row;
	editor.selection_anchor_column = primary->anchor_column;
	editor.selection_active = primary->has_selection;

	editor.cursor_count = 0;

	editor_set_status_message("Exited multi-cursor mode");
}

/*
 * Sort cursors by position and remove duplicates.
 */
void editor_cursors_sort_and_merge(void)
{
	if (editor.cursor_count <= 1) {
		return;
	}

	qsort(editor.cursors, editor.cursor_count,
	      sizeof(struct cursor), cursor_compare);

	uint32_t write_index = 1;
	for (uint32_t read_index = 1; read_index < editor.cursor_count; read_index++) {
		struct cursor *prev = &editor.cursors[write_index - 1];
		struct cursor *curr = &editor.cursors[read_index];

		if (curr->row != prev->row || curr->column != prev->column) {
			if (write_index != read_index) {
				editor.cursors[write_index] = *curr;
			}
			write_index++;
		}
	}
	editor.cursor_count = write_index;

	if (editor.primary_cursor >= editor.cursor_count) {
		editor.primary_cursor = editor.cursor_count - 1;
	}
}

/*
 * Check if we have multiple cursors.
 */
bool editor_has_multi_cursor(void)
{
	return editor.cursor_count > 1;
}

/*
 * Add a cursor at position.
 */
void editor_add_cursor(uint32_t row, uint32_t col)
{
	if (editor.cursor_count == 0) {
		multicursor_enter();
	}

	if (editor.cursor_count >= MAX_CURSORS) {
		editor_set_status_message("Maximum cursors reached (%d)", MAX_CURSORS);
		return;
	}

	struct cursor *c = &editor.cursors[editor.cursor_count];
	c->row = row;
	c->column = col;
	c->anchor_row = row;
	c->anchor_column = col;
	c->has_selection = false;

	editor.cursor_count++;
	editor_cursors_sort_and_merge();
}

/*****************************************************************************
 * Undo/Redo
 *****************************************************************************/

/*
 * Undo the most recent operation.
 */
void editor_undo(void)
{
	undo_end_group(&editor.buffer, editor.cursor_row, editor.cursor_column);

	uint32_t new_row, new_col;
	if (undo_perform(&editor.buffer, &new_row, &new_col)) {
		editor.cursor_row = new_row;
		editor.cursor_column = new_col;
		selection_clear();
		editor_set_status_message("Undo");
	} else {
		editor_set_status_message("Nothing to undo");
	}
}

/*
 * Redo the most recently undone operation.
 */
void editor_redo(void)
{
	undo_end_group(&editor.buffer, editor.cursor_row, editor.cursor_column);

	uint32_t new_row, new_col;
	if (redo_perform(&editor.buffer, &new_row, &new_col)) {
		editor.cursor_row = new_row;
		editor.cursor_column = new_col;
		selection_clear();
		editor_set_status_message("Redo");
	} else {
		editor_set_status_message("Nothing to redo");
	}
}

/*****************************************************************************
 * Go-To-Line Dialog
 *****************************************************************************/

/*
 * Enter go-to-line mode.
 */
void goto_line_enter(void)
{
	goto_line.active = true;
	goto_line.input[0] = '\0';
	goto_line.input_length = 0;
	editor_set_status_message("Go to line: ");
}

/*
 * Handle input in go-to-line mode.
 */
bool goto_handle_key(int key)
{
	if (!goto_line.active) {
		return false;
	}

	if (key == '\x1b' || key == CONTROL_KEY('g')) {
		goto_line.active = false;
		editor_set_status_message("");
		return true;
	}

	if (key == '\r') {
		goto_line.active = false;
		if (goto_line.input_length > 0) {
			long line_num = strtol(goto_line.input, NULL, 10);
			if (line_num > 0 && (uint32_t)line_num <= editor.buffer.line_count) {
				editor.cursor_row = line_num - 1;
				editor.cursor_column = 0;
				selection_clear();
				editor_set_status_message("Line %ld", line_num);
			} else {
				editor_set_status_message("Invalid line number");
			}
		} else {
			editor_set_status_message("");
		}
		return true;
	}

	if (key == KEY_BACKSPACE || key == 127) {
		if (goto_line.input_length > 0) {
			goto_line.input_length--;
			goto_line.input[goto_line.input_length] = '\0';
		}
		editor_set_status_message("Go to line: %s", goto_line.input);
		return true;
	}

	if (key >= '0' && key <= '9' && goto_line.input_length < sizeof(goto_line.input) - 1) {
		goto_line.input[goto_line.input_length++] = key;
		goto_line.input[goto_line.input_length] = '\0';
		editor_set_status_message("Go to line: %s", goto_line.input);
		return true;
	}

	return true;
}

/*
 * Check if go-to-line mode is active.
 */
bool goto_line_is_active(void)
{
	return goto_line.active;
}

/*
 * Get the current go-to-line input string.
 */
const char *goto_line_get_input(void)
{
	return goto_line.input;
}

/*****************************************************************************
 * Save As Dialog
 *****************************************************************************/

/*
 * Enter save-as mode.
 */
void save_as_enter(void)
{
	save_as.active = true;
	save_as.confirm_overwrite = false;

	if (editor.buffer.filename) {
		strncpy(save_as.path, editor.buffer.filename, sizeof(save_as.path) - 1);
		save_as.path[sizeof(save_as.path) - 1] = '\0';
		save_as.path_length = strlen(save_as.path);
	} else {
		save_as.path[0] = '\0';
		save_as.path_length = 0;
	}

	editor_set_status_message("Save as: %s", save_as.path);
}

/*
 * Exit save-as mode.
 */
static void save_as_exit(void)
{
	save_as.active = false;
	save_as.confirm_overwrite = false;
	editor_set_status_message("");
}

/*
 * Execute save-as operation.
 */
static bool save_as_execute(void)
{
	if (save_as.path_length == 0) {
		editor_set_status_message("No filename provided");
		return false;
	}

	/* Check if file exists and we haven't confirmed overwrite */
	if (!save_as.confirm_overwrite && access(save_as.path, F_OK) == 0) {
		editor_set_status_message("File exists. Overwrite? (y/n)");
		save_as.confirm_overwrite = true;
		return false;
	}

	/* Update filename */
	char *new_filename = strdup(save_as.path);
	if (new_filename == NULL) {
		editor_set_status_message("Memory allocation failed");
		return false;
	}

	free(editor.buffer.filename);
	editor.buffer.filename = new_filename;

	/* Save the file */
	int ret = file_save(&editor.buffer);
	if (ret) {
		editor_set_status_message("Save failed: %s", edit_strerror(ret));
		return false;
	}

	return true;
}

/*
 * Handle input in save-as mode.
 */
bool save_as_handle_key(int key)
{
	if (!save_as.active) {
		return false;
	}

	/* Handle overwrite confirmation */
	if (save_as.confirm_overwrite) {
		if (key == 'y' || key == 'Y') {
			save_as.confirm_overwrite = false;
			if (save_as_execute()) {
				save_as_exit();
			}
		} else if (key == 'n' || key == 'N') {
			save_as.confirm_overwrite = false;
			editor_set_status_message("Save as: %s", save_as.path);
		} else if (key == '\x1b') {
			editor_set_status_message("Save cancelled");
			save_as_exit();
		}
		return true;
	}

	if (key == '\x1b') {
		editor_set_status_message("Save As cancelled");
		save_as_exit();
		return true;
	}

	if (key == '\r') {
		if (save_as_execute()) {
			save_as_exit();
		}
		return true;
	}

	if (key == KEY_BACKSPACE || key == 127) {
		if (save_as.path_length > 0) {
			save_as.path_length--;
			save_as.path[save_as.path_length] = '\0';
		}
		editor_set_status_message("Save as: %s", save_as.path);
		return true;
	}

	if (key >= 32 && key < 127 && save_as.path_length < sizeof(save_as.path) - 1) {
		save_as.path[save_as.path_length++] = key;
		save_as.path[save_as.path_length] = '\0';
		editor_set_status_message("Save as: %s", save_as.path);
		return true;
	}

	return true;
}

/*
 * Check if save-as mode is active.
 */
bool save_as_is_active(void)
{
	return save_as.active;
}

/*
 * Check if save-as is prompting for overwrite confirmation.
 */
bool save_as_is_confirm_overwrite(void)
{
	return save_as.confirm_overwrite;
}

/*
 * Get the current path in save-as dialog.
 */
const char *save_as_get_path(void)
{
	return save_as.path;
}

/*****************************************************************************
 * Quit Prompt
 *****************************************************************************/

/*
 * Enter quit prompt mode.
 */
void quit_prompt_enter(void)
{
	quit_prompt.active = true;
	editor_set_status_message("Unsaved changes! Save before quitting? [y]es [n]o [c]ancel: ");
}

/*
 * Handle input in quit prompt mode.
 */
bool quit_prompt_handle_key(int key)
{
	if (!quit_prompt.active) {
		return false;
	}

	if (key == 'y' || key == 'Y') {
		quit_prompt.active = false;
		if (editor.buffer.filename == NULL) {
			editor_set_status_message("No filename. Use Ctrl-Shift-S to Save As, then quit.");
			return true;
		}
		editor_save();
		if (!editor.buffer.is_modified) {
			editor_perform_exit();
		}
		return true;
	}

	if (key == 'n' || key == 'N') {
		quit_prompt.active = false;
		editor_perform_exit();
		return true;
	}

	if (key == 'c' || key == 'C' || key == '\x1b' || key == CONTROL_KEY('q')) {
		quit_prompt.active = false;
		editor_set_status_message("Quit cancelled");
		return true;
	}

	editor_set_status_message("Unsaved changes! Save before quitting? [y]es [n]o [c]ancel: ");
	return true;
}

/*****************************************************************************
 * Reload Prompt
 *****************************************************************************/

static struct reload_prompt_state reload_prompt = {0};

/*
 * Enter reload prompt mode when file changes on disk.
 */
void reload_prompt_enter(void)
{
	reload_prompt.active = true;
	editor_set_status_message("File changed on disk. [R]eload [K]eep: ");
}

/*
 * Check if reload prompt is currently active.
 */
bool reload_prompt_is_active(void)
{
	return reload_prompt.active;
}

/*
 * Reload the current file from disk, preserving cursor position.
 */
void editor_reload_file(void)
{
	if (editor.buffer.filename == NULL)
		return;

	/* Save filename before freeing buffer */
	char *filename = edit_strdup(editor.buffer.filename);
	if (IS_ERR(filename)) {
		editor_set_status_message("Reload failed: out of memory");
		return;
	}

	/* Save cursor position for restoration */
	uint32_t saved_row = editor.cursor_row;
	uint32_t saved_column = editor.cursor_column;

	/* Free current buffer and reload */
	buffer_free(&editor.buffer);
	buffer_init(&editor.buffer);

	int ret = file_open(&editor.buffer, filename);
	if (ret) {
		editor_set_status_message("Reload failed: %s", edit_strerror(ret));
		free(filename);
		return;
	}
	free(filename);

	/* Restore cursor position, clamped to new file bounds */
	if (saved_row >= editor.buffer.line_count)
		saved_row = editor.buffer.line_count > 0 ? editor.buffer.line_count - 1 : 0;
	editor.cursor_row = saved_row;
	editor.cursor_column = saved_column;

	editor_set_status_message("File reloaded");
}

/*
 * Handle input in reload prompt mode.
 */
bool reload_prompt_handle_key(int key)
{
	if (!reload_prompt.active)
		return false;

	if (key == 'r' || key == 'R') {
		reload_prompt.active = false;
		editor_reload_file();
		return true;
	}

	if (key == 'k' || key == 'K' || key == '\x1b') {
		reload_prompt.active = false;
		/* Update stored mtime so we don't prompt again for this change */
		struct stat st;
		if (stat(editor.buffer.filename, &st) == 0)
			editor.buffer.file_mtime = st.st_mtime;
		editor_set_status_message("Keeping local version");
		return true;
	}

	/* Repeat prompt for unrecognized keys */
	editor_set_status_message("File changed on disk. [R]eload [K]eep: ");
	return true;
}

/*****************************************************************************
 * Update Check
 *****************************************************************************/

/*
 * Check for updates and handle the full update flow.
 * Called when user presses Alt+U.
 */
void editor_check_for_updates(void)
{
	/* If we already know an update is available, ask to install */
	if (editor.update_available) {
		editor_set_status_message("Update v%s available. Install? [y/n]: ",
		                          editor.update_version);
		render_refresh_screen();
		int key = input_read_key();
		if (key == 'y' || key == 'Y') {
			if (update_install(editor.update_version)) {
				editor.update_available = false;
			}
		} else if (key == 'n' || key == 'N') {
			editor.update_available = false;
			editor_set_status_message("Update skipped");
		} else {
			editor_set_status_message("Update cancelled");
		}
		return;
	}

	/* Show checking message and refresh screen */
	editor_set_status_message("Checking for updates...");
	render_refresh_screen();

	/* Check for updates */
	update_check();

	/* If an update was found, prompt immediately */
	if (editor.update_available) {
		editor_set_status_message("Update v%s available (current: v%s). Install? [y/n]: ",
		                          editor.update_version, EDIT_VERSION);
		render_refresh_screen();
		int key = input_read_key();
		if (key == 'y' || key == 'Y') {
			editor_set_status_message("Downloading v%s...", editor.update_version);
			render_refresh_screen();
			if (update_install(editor.update_version)) {
				editor.update_available = false;
			}
		} else if (key == 'n' || key == 'N') {
			editor.update_available = false;
			editor_set_status_message("Update skipped");
		} else {
			editor_set_status_message("Update cancelled");
		}
	}
}
