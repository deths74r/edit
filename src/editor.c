/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
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
	/* Initialize context array - start with one context */
	editor.context_count = 1;
	editor.active_context = 0;

	/* Initialize first context (per-buffer state) */
	buffer_init(E_BUF);
	E_CTX->cursor_row = 0;
	E_CTX->cursor_column = 0;
	E_CTX->row_offset = 0;
	E_CTX->column_offset = 0;
	E_CTX->gutter_width = 0;
	E_CTX->selection_anchor_row = 0;
	E_CTX->selection_anchor_column = 0;
	E_CTX->selection_active = false;
	E_CTX->cursor_count = 0;
	E_CTX->primary_cursor = 0;
	E_CTX->hybrid_mode = false;
	E_CTX->link_preview_active = false;
	memset(&E_CTX->search, 0, sizeof(E_CTX->search));

	/* Initialize global state */
	editor.screen_rows = 0;
	editor.screen_columns = 0;
	editor.show_line_numbers = true;
	editor.status_message[0] = '\0';
	editor.status_message_time = 0;
	editor.wrap_mode = WRAP_WORD;
	editor.wrap_indicator = WRAP_INDICATOR_RETURN;
	editor.show_whitespace = false;
	editor.show_file_icons = true;
	editor.show_hidden_files = false;
	editor.tab_width = 4;
	editor.color_column = 0;
	editor.color_column_style = COLOR_COLUMN_SOLID;
	editor.theme_indicator = THEME_INDICATOR_CHECK;
	editor.fuzzy_max_depth = 10;
	editor.fuzzy_max_files = 10000;
	editor.fuzzy_case_sensitive = false;
	editor.help_context_index = -1;
	editor.previous_context_before_help = 0;

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
/*****************************************************************************
 * Context Management
 *****************************************************************************/
/*
 * Initialize a context to default state.
 */
static void context_init(struct editor_context *ctx)
{
	buffer_init(&ctx->buffer);
	ctx->cursor_row = 0;
	ctx->cursor_column = 0;
	ctx->row_offset = 0;
	ctx->column_offset = 0;
	ctx->gutter_width = 0;
	ctx->selection_anchor_row = 0;
	ctx->selection_anchor_column = 0;
	ctx->selection_active = false;
	ctx->cursor_count = 0;
	ctx->primary_cursor = 0;
	ctx->hybrid_mode = false;
	ctx->link_preview_active = false;
	memset(&ctx->search, 0, sizeof(ctx->search));
}
/*
 * Create a new editor context.
 * Returns the index of the new context, or -1 on error.
 */
int editor_context_new(void)
{
	if (editor.context_count >= MAX_CONTEXTS) {
		editor_set_status_message("Maximum buffers reached (%d)", MAX_CONTEXTS);
		return -1;
	}
	uint32_t index = editor.context_count;
	context_init(&editor.contexts[index]);
	editor.context_count++;
	/* Recalculate screen size (tab bar may appear) */
	editor_update_screen_size();
	return (int)index;
}
/*
 * Close a context by index.
 * Returns true if closed, false if cancelled or invalid.
 */
bool editor_context_close(uint32_t index)
{
	if (index >= editor.context_count) {
		return false;
	}
	struct editor_context *ctx = &editor.contexts[index];
	/* Can't close the last context - always keep at least one */
	if (editor.context_count == 1) {
		editor_set_status_message("Cannot close last buffer");
		return false;
	}
	/* Free the buffer */
	buffer_free(&ctx->buffer);
	/* Shift remaining contexts down */
	for (uint32_t i = index; i < editor.context_count - 1; i++) {
		editor.contexts[i] = editor.contexts[i + 1];
	}
	editor.context_count--;
	/* Adjust active context if needed */
	if (editor.active_context >= editor.context_count) {
		editor.active_context = editor.context_count - 1;
	} else if (editor.active_context > index) {
		editor.active_context--;
	}
	/* Adjust help context index if needed */
	if (editor.help_context_index >= 0) {
		if ((uint32_t)editor.help_context_index == index) {
			/* Closing the help context */
			editor.help_context_index = -1;
		} else if ((uint32_t)editor.help_context_index > index) {
			/* Help context shifted down */
			editor.help_context_index--;
		}
	}
	/* Recalculate screen size (tab bar may disappear) */
	editor_update_screen_size();
	/* Signal main loop to skip this iteration */
	editor.context_just_closed = true;
	return true;
}
/*
 * Safely get the active buffer, with bounds checking.
 * Returns NULL if no valid buffer exists (shouldn't happen in normal operation).
 * Use this instead of E_BUF macro in places where context may have just been closed.
 */
struct buffer *editor_get_active_buffer(void)
{
	if (editor.context_count == 0 ||
	    editor.active_context >= editor.context_count) {
		return NULL;
	}
	return &editor.contexts[editor.active_context].buffer;
}
/*
 * Switch to a context by index.
 */
void editor_context_switch(uint32_t index)
{
	if (index >= editor.context_count) {
		return;
	}
	if (index == editor.active_context) {
		return;  /* Already active */
	}
	editor.active_context = index;
	/* Update gutter width for new buffer */
	editor_update_gutter_width();
	/* Update autosave path */
	autosave_update_path();
	const char *filename = E_BUF->filename ? E_BUF->filename : "[No Name]";
	editor_set_status_message("Switched to: %s", filename);
}
/*
 * Switch to previous context.
 */
void editor_context_prev(void)
{
	if (editor.context_count <= 1) {
		return;
	}
	uint32_t new_index;
	if (editor.active_context == 0) {
		new_index = editor.context_count - 1;  /* Wrap to end */
	} else {
		new_index = editor.active_context - 1;
	}
	editor_context_switch(new_index);
}
/*
 * Switch to next context.
 */
void editor_context_next(void)
{
	if (editor.context_count <= 1) {
		return;
	}
	uint32_t new_index = (editor.active_context + 1) % editor.context_count;
	editor_context_switch(new_index);
}
/*****************************************************************************
 * Editor Lifecycle
 *****************************************************************************/

/*
 * Perform clean exit.
 */
void editor_perform_exit(void)
{
	terminal_clear_screen();
	if (!E_BUF->is_modified) {
		autosave_remove_swap();
	}
	search_cleanup();
	worker_shutdown();
	clipboard_cleanup();
	buffer_free(E_BUF);
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
		E_CTX->gutter_width = 0;
		return;
	}

	/* Calculate digits needed for largest line number */
	uint32_t max_line = E_BUF->line_count;
	uint32_t digits = 1;
	while (max_line >= DECIMAL_BASE) {
		max_line /= DECIMAL_BASE;
		digits++;
	}

	/* Apply minimum digits and add trailing space */
	if (digits < MINIMUM_GUTTER_DIGITS) {
		digits = MINIMUM_GUTTER_DIGITS;
	}
	E_CTX->gutter_width = digits + GUTTER_PADDING;
}

/*
 * Update screen dimensions from terminal.
 */
void editor_update_screen_size(void)
{
	uint32_t rows, cols;
	if (terminal_get_window_size(&rows, &cols) == 0) {
		/* Reserve rows for status and message bars */
		uint32_t reserved = STATUS_BAR_ROWS;
		/* Add tab bar row when multiple buffers open */
		if (editor.context_count > 1) {
			reserved += TAB_BAR_ROWS;
		}
		editor.screen_rows = rows - reserved;
		editor.screen_columns = cols;
	}
	editor_update_gutter_width();
	buffer_invalidate_all_wrap_caches(E_BUF);
}

/*
 * Get the text area width.
 */
uint16_t editor_get_text_width(void)
{
	if (editor.screen_columns <= E_CTX->gutter_width) {
		return 1;
	}
	return editor.screen_columns - E_CTX->gutter_width;
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
	E_CTX->selection_anchor_row = E_CTX->cursor_row;
	E_CTX->selection_anchor_column = E_CTX->cursor_column;
	E_CTX->selection_active = true;
}

/*
 * Get the normalized selection range.
 */
void selection_get_range(uint32_t *start_row, uint32_t *start_col,
                         uint32_t *end_row, uint32_t *end_col)
{
	if (E_CTX->selection_anchor_row < E_CTX->cursor_row ||
	    (E_CTX->selection_anchor_row == E_CTX->cursor_row &&
	     E_CTX->selection_anchor_column <= E_CTX->cursor_column)) {
		*start_row = E_CTX->selection_anchor_row;
		*start_col = E_CTX->selection_anchor_column;
		*end_row = E_CTX->cursor_row;
		*end_col = E_CTX->cursor_column;
	} else {
		*start_row = E_CTX->cursor_row;
		*start_col = E_CTX->cursor_column;
		*end_row = E_CTX->selection_anchor_row;
		*end_col = E_CTX->selection_anchor_column;
	}
}

/*
 * Clear the current selection.
 */
void selection_clear(void)
{
	E_CTX->selection_active = false;
}

/*
 * Check if the current selection is empty (cursor at anchor).
 */
bool selection_is_empty(void)
{
	if (!E_CTX->selection_active) {
		return true;
	}
	return (E_CTX->cursor_row == E_CTX->selection_anchor_row &&
	        E_CTX->cursor_column == E_CTX->selection_anchor_column);
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
	if (E_CTX->cursor_count > 0) {
		return;
	}

	E_CTX->cursors[0].row = E_CTX->cursor_row;
	E_CTX->cursors[0].column = E_CTX->cursor_column;
	E_CTX->cursors[0].anchor_row = E_CTX->selection_anchor_row;
	E_CTX->cursors[0].anchor_column = E_CTX->selection_anchor_column;
	E_CTX->cursors[0].has_selection = E_CTX->selection_active;

	E_CTX->cursor_count = 1;
	E_CTX->primary_cursor = 0;
}

/*
 * Exit multi-cursor mode, keeping only the primary cursor.
 */
void editor_multi_cursor_exit(void)
{
	if (E_CTX->cursor_count == 0) {
		return;
	}

	struct cursor *primary = &E_CTX->cursors[E_CTX->primary_cursor];
	E_CTX->cursor_row = primary->row;
	E_CTX->cursor_column = primary->column;
	E_CTX->selection_anchor_row = primary->anchor_row;
	E_CTX->selection_anchor_column = primary->anchor_column;
	E_CTX->selection_active = primary->has_selection;

	E_CTX->cursor_count = 0;

	editor_set_status_message("Exited multi-cursor mode");
}

/*
 * Sort cursors by position and remove duplicates.
 */
void editor_cursors_sort_and_merge(void)
{
	if (E_CTX->cursor_count <= 1) {
		return;
	}

	qsort(E_CTX->cursors, E_CTX->cursor_count,
	      sizeof(struct cursor), cursor_compare);

	uint32_t write_index = 1;
	for (uint32_t read_index = 1; read_index < E_CTX->cursor_count; read_index++) {
		struct cursor *prev = &E_CTX->cursors[write_index - 1];
		struct cursor *curr = &E_CTX->cursors[read_index];

		if (curr->row != prev->row || curr->column != prev->column) {
			if (write_index != read_index) {
				E_CTX->cursors[write_index] = *curr;
			}
			write_index++;
		}
	}
	E_CTX->cursor_count = write_index;

	if (E_CTX->primary_cursor >= E_CTX->cursor_count) {
		E_CTX->primary_cursor = E_CTX->cursor_count - 1;
	}
}

/*
 * Check if we have multiple cursors.
 */
bool editor_has_multi_cursor(void)
{
	return E_CTX->cursor_count > 1;
}

/*
 * Add a cursor at position.
 */
void editor_add_cursor(uint32_t row, uint32_t col)
{
	if (E_CTX->cursor_count == 0) {
		multicursor_enter();
	}

	if (E_CTX->cursor_count >= MAX_CURSORS) {
		editor_set_status_message("Maximum cursors reached (%d)", MAX_CURSORS);
		return;
	}

	struct cursor *c = &E_CTX->cursors[E_CTX->cursor_count];
	c->row = row;
	c->column = col;
	c->anchor_row = row;
	c->anchor_column = col;
	c->has_selection = false;

	E_CTX->cursor_count++;
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
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	uint32_t new_row, new_col;
	if (undo_perform(E_BUF, &new_row, &new_col)) {
		E_CTX->cursor_row = new_row;
		E_CTX->cursor_column = new_col;
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
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	uint32_t new_row, new_col;
	if (redo_perform(E_BUF, &new_row, &new_col)) {
		E_CTX->cursor_row = new_row;
		E_CTX->cursor_column = new_col;
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
			if (line_num > 0 && (uint32_t)line_num <= E_BUF->line_count) {
				E_CTX->cursor_row = line_num - 1;
				E_CTX->cursor_column = 0;
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

	if (E_BUF->filename) {
		strncpy(save_as.path, E_BUF->filename, sizeof(save_as.path) - 1);
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

	free(E_BUF->filename);
	E_BUF->filename = new_filename;

	/* Save the file */
	int ret = file_save(E_BUF);
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
		if (E_BUF->filename == NULL) {
			editor_set_status_message("No filename. Use Ctrl-Shift-S to Save As, then quit.");
			return true;
		}
		editor_save();
		if (!E_BUF->is_modified) {
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
	if (E_BUF->filename == NULL)
		return;

	/* Save filename before freeing buffer */
	char *filename = edit_strdup(E_BUF->filename);
	if (IS_ERR(filename)) {
		editor_set_status_message("Reload failed: out of memory");
		return;
	}

	/* Save cursor position for restoration */
	uint32_t saved_row = E_CTX->cursor_row;
	uint32_t saved_column = E_CTX->cursor_column;

	/* Free current buffer and reload */
	buffer_free(E_BUF);
	buffer_init(E_BUF);

	int ret = file_open(E_BUF, filename);
	if (ret) {
		editor_set_status_message("Reload failed: %s", edit_strerror(ret));
		free(filename);
		return;
	}
	free(filename);

	/* Restore cursor position, clamped to new file bounds */
	if (saved_row >= E_BUF->line_count)
		saved_row = E_BUF->line_count > 0 ? E_BUF->line_count - 1 : 0;
	E_CTX->cursor_row = saved_row;
	E_CTX->cursor_column = saved_column;

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
		if (stat(E_BUF->filename, &st) == 0)
			E_BUF->file_mtime = st.st_mtime;
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
