/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * dialog.c - Dialog panels for edit
 *
 * Provides modal dialog infrastructure, file browser,
 * and theme picker.
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "../third_party/utflite/single_include/utflite.h"
#include "edit.h"

/*****************************************************************************
 * External Dependencies
 *****************************************************************************/

/* Editor state from edit.c */
extern struct editor_state editor;

/* Functions from edit.c */
extern void editor_set_status_message(const char *format, ...);
extern void editor_request_background_warming(void);
extern void multicursor_exit(void);
extern int file_open(struct buffer *buffer, const char *filename);
extern const char *edit_strerror(int error_code);

/* Functions from input.c */
extern void input_set_dialog_mouse_mode(bool enabled);
extern int input_read_key(void);
extern struct mouse_input input_get_last_mouse(void);

/* Functions from theme.c */
extern void theme_apply_by_index(int index);
extern void config_save(void);
extern int style_to_escape(const struct style *style, char *buffer, size_t buffer_size);

/*****************************************************************************
 * Theme Indicator Helpers
 *****************************************************************************/

/*
 * Get the UTF-8 string for a theme indicator style.
 */
static const char *theme_indicator_char(enum theme_indicator ind)
{
	switch (ind) {
		case THEME_INDICATOR_ASTERISK: return "*";
		case THEME_INDICATOR_BULLET:   return "\xe2\x97\x8f";  /* U+25CF ● */
		case THEME_INDICATOR_DIAMOND:  return "\xe2\x97\x86";  /* U+25C6 ◆ */
		case THEME_INDICATOR_TRIANGLE: return "\xe2\x96\xb6";  /* U+25B6 ▶ */
		case THEME_INDICATOR_CHECK:    return "\xe2\x9c\x93";  /* U+2713 ✓ */
		case THEME_INDICATOR_ARROW:    return "\xe2\x86\x92";  /* U+2192 → */
		case THEME_INDICATOR_DOT:      return "\xe2\x80\xa2";  /* U+2022 • */
		case THEME_INDICATOR_CHEVRON:  return "\xe2\x9d\xaf";  /* U+276F ❯ */
		default: return "*";
	}
}

/*
 * Cycle to the next theme indicator style.
 */
static void editor_cycle_theme_indicator(void)
{
	switch (editor.theme_indicator) {
		case THEME_INDICATOR_ASTERISK:
			editor.theme_indicator = THEME_INDICATOR_BULLET;
			break;
		case THEME_INDICATOR_BULLET:
			editor.theme_indicator = THEME_INDICATOR_DIAMOND;
			break;
		case THEME_INDICATOR_DIAMOND:
			editor.theme_indicator = THEME_INDICATOR_TRIANGLE;
			break;
		case THEME_INDICATOR_TRIANGLE:
			editor.theme_indicator = THEME_INDICATOR_CHECK;
			break;
		case THEME_INDICATOR_CHECK:
			editor.theme_indicator = THEME_INDICATOR_ARROW;
			break;
		case THEME_INDICATOR_ARROW:
			editor.theme_indicator = THEME_INDICATOR_DOT;
			break;
		case THEME_INDICATOR_DOT:
			editor.theme_indicator = THEME_INDICATOR_CHEVRON;
			break;
		case THEME_INDICATOR_CHEVRON:
			editor.theme_indicator = THEME_INDICATOR_ASTERISK;
			break;
	}
}

/*****************************************************************************
 * Global Dialog State
 *****************************************************************************/

static struct open_file_state open_file = {0};
static struct theme_picker_state theme_picker = {0};

/*****************************************************************************
 * Dialog Rendering Helpers
 *****************************************************************************/

/*
 * Move cursor to dialog row and column (1-based terminal coordinates).
 */
void dialog_goto(struct output_buffer *output, int row, int column)
{
	char escape[32];
	snprintf(escape, sizeof(escape), "\x1b[%d;%dH", row, column);
	output_buffer_append_string(output, escape);
}

/*
 * Set foreground color for dialog output.
 */
void dialog_set_fg(struct output_buffer *output, struct syntax_color color)
{
	char escape[32];
	snprintf(escape, sizeof(escape), "\x1b[38;2;%d;%d;%dm",
		color.red, color.green, color.blue);
	output_buffer_append_string(output, escape);
}

/*
 * Set background color for dialog output.
 */
void dialog_set_bg(struct output_buffer *output, struct syntax_color color)
{
	char escape[32];
	snprintf(escape, sizeof(escape), "\x1b[48;2;%d;%d;%dm",
		color.red, color.green, color.blue);
	output_buffer_append_string(output, escape);
}

/*
 * Set full style (fg, bg, attributes) for dialog output.
 */
void dialog_set_style(struct output_buffer *output, const struct style *style)
{
	char escape[128];
	int length = style_to_escape(style, escape, sizeof(escape));
	output_buffer_append(output, escape, length);
}

/*
 * Draw dialog header with title centered.
 */
void dialog_draw_header(struct output_buffer *output,
                        struct dialog_state *dialog,
                        const char *title)
{
	dialog_goto(output, dialog->panel_top + 1, dialog->panel_left + 1);
	dialog_set_style(output, &active_theme.dialog_header);

	/* Calculate title position for centering */
	int title_length = strlen(title);
	int padding_left = (dialog->panel_width - title_length) / 2;
	if (padding_left < 1) {
		padding_left = 1;
	}

	/* Draw header with centered title */
	for (int i = 0; i < dialog->panel_width; i++) {
		if (i >= padding_left && i < padding_left + title_length) {
			output_buffer_append_char(output, title[i - padding_left]);
		} else {
			output_buffer_append_char(output, ' ');
		}
	}
}

/*
 * Draw dialog footer with hint text.
 */
void dialog_draw_footer(struct output_buffer *output,
                        struct dialog_state *dialog,
                        const char *hint)
{
	int footer_row = dialog->panel_top + dialog->panel_height;
	dialog_goto(output, footer_row, dialog->panel_left + 1);
	dialog_set_style(output, &active_theme.dialog_footer);

	/* Draw hint left-aligned with padding */
	int hint_length = strlen(hint);
	int chars_written = 0;

	output_buffer_append_char(output, ' ');
	chars_written++;

	for (int i = 0; i < hint_length && chars_written < dialog->panel_width - 1; i++) {
		output_buffer_append_char(output, hint[i]);
		chars_written++;
	}

	/* Fill rest with spaces */
	while (chars_written < dialog->panel_width) {
		output_buffer_append_char(output, ' ');
		chars_written++;
	}
}

/*
 * Draw an empty row in the dialog content area.
 */
void dialog_draw_empty_row(struct output_buffer *output,
                           struct dialog_state *dialog,
                           int row_index)
{
	int screen_row = dialog->panel_top + 2 + row_index;
	dialog_goto(output, screen_row, dialog->panel_left + 1);
	dialog_set_bg(output, active_theme.dialog.bg);

	for (int i = 0; i < dialog->panel_width; i++) {
		output_buffer_append_char(output, ' ');
	}
}

/*
 * Draw a single list item in the dialog.
 */
void dialog_draw_list_item(struct output_buffer *output,
                           struct dialog_state *dialog,
                           int row_index,
                           const char *text,
                           bool is_selected,
                           bool is_directory)
{
	int screen_row = dialog->panel_top + 2 + row_index;
	dialog_goto(output, screen_row, dialog->panel_left + 1);

	if (is_selected) {
		dialog_set_style(output, &active_theme.dialog_highlight);
	} else if (is_directory) {
		dialog_set_style(output, &active_theme.dialog_directory);
	} else {
		dialog_set_style(output, &active_theme.dialog);
	}

	/* Draw text with padding */
	int text_length = strlen(text);
	int chars_written = 0;

	output_buffer_append_char(output, ' ');
	chars_written++;

	/* Draw folder icon or matching indent for alignment */
	if (editor.show_file_icons) {
		if (is_directory) {
			output_buffer_append_string(output, "🗁  ");
		} else {
			output_buffer_append_string(output, "   ");
		}
		chars_written += 3;  /* Icon/indent is 3 cells */
	}

	for (int i = 0; i < text_length && chars_written < dialog->panel_width - 1; i++) {
		output_buffer_append_char(output, text[i]);
		chars_written++;
	}

	/* Fill rest with spaces */
	while (chars_written < dialog->panel_width) {
		output_buffer_append_char(output, ' ');
		chars_written++;
	}
}

/*****************************************************************************
 * Dialog State Management
 *****************************************************************************/

/*
 * Calculate dialog panel dimensions based on screen size.
 * Panel is centered, 50% height, 70% width, with minimum sizes.
 */
void dialog_calculate_dimensions(struct dialog_state *dialog)
{
	const int minimum_width = DIALOG_MIN_WIDTH;
	const int minimum_height = DIALOG_MIN_HEIGHT;

	/* 70% of screen width, at least minimum */
	dialog->panel_width = (editor.screen_columns * DIALOG_WIDTH_PERCENT) / 100;
	if (dialog->panel_width < minimum_width) {
		dialog->panel_width = minimum_width;
	}
	if (dialog->panel_width > (int)editor.screen_columns - DIALOG_SCREEN_MARGIN) {
		dialog->panel_width = (int)editor.screen_columns - DIALOG_SCREEN_MARGIN;
	}

	/* 50% of screen height, at least minimum */
	dialog->panel_height = (editor.screen_rows * DIALOG_HEIGHT_PERCENT) / 100;
	if (dialog->panel_height < minimum_height) {
		dialog->panel_height = minimum_height;
	}
	if (dialog->panel_height > (int)editor.screen_rows - DIALOG_SCREEN_MARGIN) {
		dialog->panel_height = (int)editor.screen_rows - DIALOG_SCREEN_MARGIN;
	}

	/* Center on screen */
	dialog->panel_left = (editor.screen_columns - dialog->panel_width) / 2;
	dialog->panel_top = (editor.screen_rows - dialog->panel_height) / 2;

	/* Content area: subtract 2 for header and footer */
	dialog->visible_rows = dialog->panel_height - 2;
	if (dialog->visible_rows < 1) {
		dialog->visible_rows = 1;
	}
}

/*
 * Ensure the selected item is visible by adjusting scroll offset.
 */
void dialog_ensure_visible(struct dialog_state *dialog)
{
	if (dialog->selected_index < dialog->scroll_offset) {
		dialog->scroll_offset = dialog->selected_index;
	}
	if (dialog->selected_index >= dialog->scroll_offset + dialog->visible_rows) {
		dialog->scroll_offset = dialog->selected_index - dialog->visible_rows + 1;
	}
}

/*
 * Clamp selection index to valid range and ensure visibility.
 */
void dialog_clamp_selection(struct dialog_state *dialog)
{
	if (dialog->selected_index < 0) {
		dialog->selected_index = 0;
	}
	if (dialog->selected_index >= dialog->item_count) {
		dialog->selected_index = dialog->item_count - 1;
	}
	if (dialog->selected_index < 0) {
		dialog->selected_index = 0;
	}
	dialog_ensure_visible(dialog);
}

/*
 * Close the dialog and restore normal editor state.
 */
void dialog_close(struct dialog_state *dialog)
{
	dialog->active = false;
	input_set_dialog_mouse_mode(false);

	/* Show cursor again now that dialog is closed */
	write(STDOUT_FILENO, ESCAPE_CURSOR_SHOW, ESCAPE_CURSOR_SHOW_LENGTH);
}

/*****************************************************************************
 * Dialog Input Handling
 *****************************************************************************/

/*
 * Check if a click qualifies as a double-click based on timing and position.
 */
static bool dialog_is_double_click(struct dialog_state *dialog, int item_index)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Check if same item was clicked */
	if (item_index != dialog->last_click_index) {
		dialog->last_click = now;
		dialog->last_click_index = item_index;
		return false;
	}

	/* Calculate elapsed milliseconds */
	long elapsed_ms = (now.tv_sec - dialog->last_click.tv_sec) * 1000 +
			  (now.tv_nsec - dialog->last_click.tv_nsec) / 1000000;

	dialog->last_click = now;
	dialog->last_click_index = item_index;

	return elapsed_ms <= DIALOG_DOUBLE_CLICK_MS;
}

/*
 * Handle keyboard input for dialog navigation.
 * Returns the action to take.
 */
enum dialog_result dialog_handle_key(struct dialog_state *dialog, int key)
{
	switch (key) {
		case KEY_ARROW_UP:
			dialog->selected_index--;
			dialog_clamp_selection(dialog);
			return DIALOG_CONTINUE;

		case KEY_ARROW_DOWN:
			dialog->selected_index++;
			dialog_clamp_selection(dialog);
			return DIALOG_CONTINUE;

		case KEY_PAGE_UP:
			dialog->selected_index -= dialog->visible_rows;
			dialog_clamp_selection(dialog);
			return DIALOG_CONTINUE;

		case KEY_PAGE_DOWN:
			dialog->selected_index += dialog->visible_rows;
			dialog_clamp_selection(dialog);
			return DIALOG_CONTINUE;

		case KEY_HOME:
			dialog->selected_index = 0;
			dialog_clamp_selection(dialog);
			return DIALOG_CONTINUE;

		case KEY_END:
			dialog->selected_index = dialog->item_count - 1;
			dialog_clamp_selection(dialog);
			return DIALOG_CONTINUE;

		case '\r':
		case '\n':
			return DIALOG_CONFIRM;

		case '\x1b':
			return DIALOG_CANCEL;

		default:
			return DIALOG_CONTINUE;
	}
}

/*
 * Handle mouse input for dialog interaction.
 * Returns the action to take.
 */
enum dialog_result dialog_handle_mouse(struct dialog_state *dialog,
                                       struct mouse_input *mouse)
{
	/*
	 * Mouse coordinates are 0-based (parsed from 1-based terminal coords).
	 * Dialog panel_top is 0-based. Drawing uses panel_top + 1 for ANSI
	 * escape sequences (which are 1-based), so first content row is at
	 * ANSI row (panel_top + 2), which equals 0-based row (panel_top + 1).
	 */
	int content_top = dialog->panel_top + 1;
	int content_bottom = dialog->panel_top + dialog->panel_height - 1;
	int content_left = dialog->panel_left + 1;
	int content_right = dialog->panel_left + dialog->panel_width;

	/* Handle scroll wheel */
	if (mouse->event == MOUSE_SCROLL_UP) {
		dialog->scroll_offset -= 3;
		if (dialog->scroll_offset < 0) {
			dialog->scroll_offset = 0;
		}
		return DIALOG_CONTINUE;
	}

	if (mouse->event == MOUSE_SCROLL_DOWN) {
		int max_scroll = dialog->item_count - dialog->visible_rows;
		if (max_scroll < 0) {
			max_scroll = 0;
		}
		dialog->scroll_offset += 3;
		if (dialog->scroll_offset > max_scroll) {
			dialog->scroll_offset = max_scroll;
		}
		return DIALOG_CONTINUE;
	}

	/* Check if within content area */
	if ((int)mouse->column < content_left || (int)mouse->column >= content_right) {
		return DIALOG_CONTINUE;
	}
	if ((int)mouse->row < content_top || (int)mouse->row >= content_bottom) {
		return DIALOG_CONTINUE;
	}

	/* Calculate which item was clicked */
	int row_offset = mouse->row - content_top;
	int item_index = dialog->scroll_offset + row_offset;

	if (item_index < 0 || item_index >= dialog->item_count) {
		return DIALOG_CONTINUE;
	}

	/* Handle click */
	if (mouse->event == MOUSE_LEFT_PRESS) {
		dialog->mouse_down = true;

		/* Check for double-click */
		if (dialog_is_double_click(dialog, item_index)) {
			dialog->selected_index = item_index;
			return DIALOG_CONFIRM;
		}

		dialog->selected_index = item_index;
		return DIALOG_CONTINUE;
	}

	if (mouse->event == MOUSE_LEFT_RELEASE) {
		dialog->mouse_down = false;
	}

	return DIALOG_CONTINUE;
}

/*****************************************************************************
 * File List Utilities
 *****************************************************************************/

/*
 * Free a single file list item and its allocated strings.
 */
void file_list_item_free(struct file_list_item *item)
{
	if (item == NULL) {
		return;
	}
	free(item->display_name);
	free(item->actual_name);
	item->display_name = NULL;
	item->actual_name = NULL;
}

/*
 * Free an array of file list items.
 */
void file_list_free(struct file_list_item *items, int count)
{
	if (items == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		file_list_item_free(&items[i]);
	}
	free(items);
}

/*
 * Comparison function for sorting file list items.
 * Directories come first, then alphabetical by display name.
 */
static int file_list_compare(const void *first, const void *second)
{
	const struct file_list_item *item_first = first;
	const struct file_list_item *item_second = second;

	/* Directories come before files */
	if (item_first->is_directory && !item_second->is_directory) {
		return -1;
	}
	if (!item_first->is_directory && item_second->is_directory) {
		return 1;
	}

	/* Alphabetical comparison within same type */
	return strcmp(item_first->display_name, item_second->display_name);
}

/*
 * Read directory contents and return sorted file list.
 * Caller must free the returned array with file_list_free().
 * Returns NULL on error and sets count to 0.
 */
struct file_list_item *file_list_read_directory(const char *path, int *count)
{
	*count = 0;

	DIR *directory = opendir(path);
	if (directory == NULL) {
		return NULL;
	}

	/* First pass: count entries */
	int capacity = 64;
	int entry_count = 0;
	struct file_list_item *items = malloc(capacity * sizeof(struct file_list_item));
	if (items == NULL) {
		closedir(directory);
		return NULL;
	}

	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		/* Skip . and .. entries */
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		/* Expand array if needed */
		if (entry_count >= capacity) {
			capacity *= 2;
			struct file_list_item *new_items = realloc(items, capacity * sizeof(struct file_list_item));
			if (new_items == NULL) {
				file_list_free(items, entry_count);
				closedir(directory);
				return NULL;
			}
			items = new_items;
		}

		/* Build full path to check if directory */
		size_t path_length = strlen(path);
		size_t name_length = strlen(entry->d_name);
		char *full_path = malloc(path_length + name_length + 2);
		if (full_path == NULL) {
			file_list_free(items, entry_count);
			closedir(directory);
			return NULL;
		}
		memcpy(full_path, path, path_length);
		if (path_length > 0 && path[path_length - 1] != '/') {
			full_path[path_length] = '/';
			path_length++;
		}
		memcpy(full_path + path_length, entry->d_name, name_length + 1);

		struct stat file_stat;
		bool is_directory = false;
		if (stat(full_path, &file_stat) == 0) {
			is_directory = S_ISDIR(file_stat.st_mode);
		}
		free(full_path);

		/* Allocate display name (with trailing / for directories) */
		char *display_name;
		if (is_directory) {
			display_name = malloc(name_length + 2);
			if (display_name == NULL) {
				file_list_free(items, entry_count);
				closedir(directory);
				return NULL;
			}
			memcpy(display_name, entry->d_name, name_length);
			display_name[name_length] = '/';
			display_name[name_length + 1] = '\0';
		} else {
			display_name = strdup(entry->d_name);
			if (display_name == NULL) {
				file_list_free(items, entry_count);
				closedir(directory);
				return NULL;
			}
		}

		/* Allocate actual name */
		char *actual_name = strdup(entry->d_name);
		if (actual_name == NULL) {
			free(display_name);
			file_list_free(items, entry_count);
			closedir(directory);
			return NULL;
		}

		items[entry_count].display_name = display_name;
		items[entry_count].actual_name = actual_name;
		items[entry_count].is_directory = is_directory;
		entry_count++;
	}

	closedir(directory);

	/* Sort the list: directories first, then alphabetically */
	qsort(items, entry_count, sizeof(struct file_list_item), file_list_compare);

	*count = entry_count;
	return items;
}

/*****************************************************************************
 * Path Utilities
 *****************************************************************************/

/*
 * Get the parent directory of a path.
 * Returns newly allocated string, caller must free.
 * Returns "/" for root path or paths without parent.
 */
char *path_get_parent(const char *path)
{
	if (path == NULL || path[0] == '\0') {
		return strdup("/");
	}

	size_t length = strlen(path);

	/* Skip trailing slashes */
	while (length > 1 && path[length - 1] == '/') {
		length--;
	}

	/* Find last slash */
	while (length > 0 && path[length - 1] != '/') {
		length--;
	}

	/* Skip the slash itself unless it's root */
	if (length > 1) {
		length--;
	}

	if (length == 0) {
		/* No directory component - return "." for current directory */
		return strdup(".");
	}

	char *parent = malloc(length + 1);
	if (parent == NULL) {
		return strdup("/");
	}

	memcpy(parent, path, length);
	parent[length] = '\0';

	return parent;
}

/*
 * Join a directory path and filename.
 * Returns newly allocated string, caller must free.
 */
char *path_join(const char *directory, const char *filename)
{
	if (directory == NULL || directory[0] == '\0') {
		return strdup(filename);
	}
	if (filename == NULL || filename[0] == '\0') {
		return strdup(directory);
	}

	size_t directory_length = strlen(directory);
	size_t filename_length = strlen(filename);

	/* Check if directory ends with slash */
	bool has_slash = (directory[directory_length - 1] == '/');

	size_t total_length = directory_length + filename_length + (has_slash ? 1 : 2);
	char *result = malloc(total_length);
	if (result == NULL) {
		return NULL;
	}

	memcpy(result, directory, directory_length);
	if (!has_slash) {
		result[directory_length] = '/';
		directory_length++;
	}
	memcpy(result + directory_length, filename, filename_length + 1);

	return result;
}

/*****************************************************************************
 * Open File Dialog
 *****************************************************************************/

/*
 * Load directory contents into the open file dialog.
 * Returns true on success.
 */
static bool open_file_load_directory(const char *path)
{
	/* Free existing items */
	if (open_file.items) {
		file_list_free(open_file.items, open_file.item_count);
		open_file.items = NULL;
		open_file.item_count = 0;
	}

	/* Resolve to absolute path */
	char resolved[PATH_MAX];
	if (realpath(path, resolved) == NULL) {
		return false;
	}

	/* Load directory contents */
	int count;
	struct file_list_item *items = file_list_read_directory(resolved, &count);
	if (items == NULL) {
		return false;
	}

	/* Update state */
	strncpy(open_file.current_path, resolved, PATH_MAX - 1);
	open_file.current_path[PATH_MAX - 1] = '\0';
	open_file.items = items;
	open_file.item_count = count;

	/* Update dialog state */
	open_file.dialog.item_count = count;
	open_file.dialog.selected_index = 0;
	open_file.dialog.scroll_offset = 0;

	return true;
}

/*
 * Navigate to parent directory.
 */
static void open_file_go_parent(void)
{
	char *parent = path_get_parent(open_file.current_path);
	if (parent) {
		if (!open_file_load_directory(parent)) {
			editor_set_status_message("Cannot open parent directory");
		}
		free(parent);
	}
}

/*
 * Navigate into selected directory or return selected file path.
 * Returns:
 *   - Allocated string with file path if file was selected (caller must free)
 *   - NULL if navigated into directory (dialog continues)
 *   - NULL with dialog.active = false on error
 */
static char *open_file_select_item(void)
{
	if (open_file.dialog.selected_index < 0 ||
	    open_file.dialog.selected_index >= open_file.item_count) {
		return NULL;
	}

	struct file_list_item *item = &open_file.items[open_file.dialog.selected_index];

	if (item->is_directory) {
		/* Navigate into directory */
		char *new_path = path_join(open_file.current_path, item->actual_name);
		if (new_path) {
			if (!open_file_load_directory(new_path)) {
				editor_set_status_message("Cannot open directory: %s", new_path);
			}
			free(new_path);
		}
		return NULL;
	} else {
		/* Return file path */
		char *file_path = path_join(open_file.current_path, item->actual_name);
		return file_path;
	}
}

/*
 * Draw the file browser panel.
 */
static void open_file_draw(void)
{
	struct output_buffer output = {0};

	/* Hide cursor during drawing */
	output_buffer_append_string(&output, ESCAPE_CURSOR_HIDE);

	/* Calculate dimensions */
	dialog_calculate_dimensions(&open_file.dialog);

	/* Build header title with current path */
	char header[PATH_MAX + 16];
	snprintf(header, sizeof(header), "Open: %s", open_file.current_path);

	/* Truncate path from left if too long */
	int max_header = open_file.dialog.panel_width - 2;
	int header_len = strlen(header);
	if (header_len > max_header) {
		int skip = header_len - max_header + 4;
		memmove(header + 7, header + 6 + skip, strlen(header + 6 + skip) + 1);
		memcpy(header + 4, "...", 3);
	}

	dialog_draw_header(&output, &open_file.dialog, header);

	/* Draw file list */
	for (int row = 0; row < open_file.dialog.visible_rows; row++) {
		int item_index = open_file.dialog.scroll_offset + row;

		if (item_index < open_file.item_count) {
			struct file_list_item *item = &open_file.items[item_index];
			bool is_selected = (item_index == open_file.dialog.selected_index);

			dialog_draw_list_item(&output, &open_file.dialog, row,
			                      item->display_name, is_selected,
			                      item->is_directory);
		} else {
			dialog_draw_empty_row(&output, &open_file.dialog, row);
		}
	}

	/* Draw footer */
	dialog_draw_footer(&output, &open_file.dialog,
	                   "Enter:Open  Left:Parent  Tab:Icons  Esc:Cancel");

	/* Reset attributes, keep cursor hidden while dialog is active */
	output_buffer_append_string(&output, ESCAPE_RESET);

	output_buffer_flush(&output);
	output_buffer_free(&output);
}

/*
 * Show the Open File dialog.
 * Returns allocated file path if user selected a file, NULL if cancelled.
 * Caller must free the returned path.
 */
char *open_file_dialog(void)
{
	/* Initialize state */
	memset(&open_file, 0, sizeof(open_file));
	open_file.dialog.active = true;

	/* Start in directory of current file, or current working directory */
	char start_path[PATH_MAX];
	start_path[0] = '\0';
	if (editor.buffer.filename) {
		char *parent = path_get_parent(editor.buffer.filename);
		if (parent) {
			strncpy(start_path, parent, PATH_MAX - 1);
			start_path[PATH_MAX - 1] = '\0';
			free(parent);
		}
	}
	if (start_path[0] == '\0') {
		if (getcwd(start_path, PATH_MAX) == NULL) {
			start_path[0] = '\0';
		}
	}

	if (!open_file_load_directory(start_path)) {
		/* Fall back to home directory */
		const char *home = getenv("HOME");
		if (!home || !open_file_load_directory(home)) {
			/* Fall back to root */
			if (!open_file_load_directory("/")) {
				editor_set_status_message("Cannot open any directory");
				return NULL;
			}
		}
	}

	/* Enable dialog mouse mode and flush any pending input */
	input_set_dialog_mouse_mode(true);
	tcflush(STDIN_FILENO, TCIFLUSH);

	char *result = NULL;

	while (open_file.dialog.active) {
		open_file_draw();

		/* Read input */
		int key = input_read_key();

		if (key == -1) {
			continue;
		}

		/* Handle resize */
		if (key == KEY_RESIZE) {
			if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns)) {
				editor.screen_rows = 24;
				editor.screen_columns = 80;
			}
			int __unused = render_refresh_screen(); (void)__unused;
			continue;
		}

		/* Check for mouse event */
		if (key == KEY_MOUSE_EVENT) {
			struct mouse_input last_mouse = input_get_last_mouse();
			enum dialog_result dr = dialog_handle_mouse(&open_file.dialog, &last_mouse);

			if (dr == DIALOG_CONFIRM) {
				result = open_file_select_item();
				if (result) {
					open_file.dialog.active = false;
				}
			} else if (dr == DIALOG_CANCEL) {
				open_file.dialog.active = false;
			}
			continue;
		}

		/* Handle special keys for file browser */
		if (key == KEY_ARROW_LEFT) {
			open_file_go_parent();
			continue;
		}

		if (key == KEY_ARROW_RIGHT) {
			/* Enter directory if selected item is a directory */
			if (open_file.dialog.selected_index >= 0 &&
			    open_file.dialog.selected_index < open_file.item_count &&
			    open_file.items[open_file.dialog.selected_index].is_directory) {
				open_file_select_item();
			}
			continue;
		}

		/* Tab toggles file/folder icons */
		if (key == '\t') {
			editor.show_file_icons = !editor.show_file_icons;
			continue;
		}

		/* Handle generic dialog keys */
		enum dialog_result dr = dialog_handle_key(&open_file.dialog, key);

		if (dr == DIALOG_CONFIRM) {
			result = open_file_select_item();
			if (result) {
				open_file.dialog.active = false;
			}
		} else if (dr == DIALOG_CANCEL) {
			open_file.dialog.active = false;
		}
	}

	/* Clean up */
	if (open_file.items) {
		file_list_free(open_file.items, open_file.item_count);
		open_file.items = NULL;
	}

	dialog_close(&open_file.dialog);

	return result;
}

/*****************************************************************************
 * Theme Picker Dialog
 *****************************************************************************/

/*
 * Draw the theme picker panel.
 */
static void theme_picker_draw(void)
{
	struct output_buffer output = {0};

	/* Hide cursor during drawing */
	output_buffer_append_string(&output, ESCAPE_CURSOR_HIDE);

	/* Calculate dimensions (narrower than file browser) */
	dialog_calculate_dimensions(&theme_picker.dialog);

	/* Override width for narrower panel */
	int desired_width = 50;
	if (desired_width > (int)editor.screen_columns - 4) {
		desired_width = (int)editor.screen_columns - 4;
	}
	theme_picker.dialog.panel_width = desired_width;
	theme_picker.dialog.panel_left = (editor.screen_columns - desired_width) / 2;

	dialog_draw_header(&output, &theme_picker.dialog, "Select Theme");

	/* Draw theme list */
	for (int row = 0; row < theme_picker.dialog.visible_rows; row++) {
		int item_index = theme_picker.dialog.scroll_offset + row;

		if (item_index < theme_count) {
			struct theme *t = &loaded_themes[item_index];
			bool is_selected = (item_index == theme_picker.dialog.selected_index);

			/* Get marker for current theme */
			const char *marker = (item_index == current_theme_index)
			                     ? theme_indicator_char(editor.theme_indicator)
			                     : " ";

			/* Position cursor */
			int screen_row = theme_picker.dialog.panel_top + 2 + row;
			dialog_goto(&output, screen_row, theme_picker.dialog.panel_left + 1);

			/* Set colors based on selection */
			if (is_selected) {
				dialog_set_style(&output, &active_theme.dialog_highlight);
			} else {
				dialog_set_style(&output, &active_theme.dialog);
			}

			/* Write marker and name - use display width, not byte count */
			char name_buf[64];
			int bytes = snprintf(name_buf, sizeof(name_buf), " %s %s",
			                     marker, t->name ? t->name : "Unknown");
			int name_len = utflite_string_width(name_buf, bytes);

			int max_name = theme_picker.dialog.panel_width - 12;
			if (name_len > max_name) {
				int trunc_byte = utflite_truncate(name_buf, bytes, max_name);
				name_buf[trunc_byte] = '\0';
				name_len = max_name;
			}
			output_buffer_append_string(&output, name_buf);

			/* Draw color preview strip (4 colored squares) */
			if (t) {
				output_buffer_append_string(&output, " ");
				name_len++;

				/* Background color square */
				dialog_set_fg(&output, t->background);
				output_buffer_append_string(&output, "\xe2\x96\xa0");
				name_len++;

				/* Keyword color square */
				dialog_set_fg(&output, t->syntax[SYNTAX_KEYWORD].fg);
				output_buffer_append_string(&output, "\xe2\x96\xa0");
				name_len++;

				/* String color square */
				dialog_set_fg(&output, t->syntax[SYNTAX_STRING].fg);
				output_buffer_append_string(&output, "\xe2\x96\xa0");
				name_len++;

				/* Comment color square */
				dialog_set_fg(&output, t->syntax[SYNTAX_COMMENT].fg);
				output_buffer_append_string(&output, "\xe2\x96\xa0");
				name_len++;

				/* Reset foreground for padding */
				if (is_selected) {
					dialog_set_fg(&output, active_theme.dialog_highlight.fg);
				} else {
					dialog_set_fg(&output, active_theme.dialog.fg);
				}
			}

			/* Pad with spaces */
			while (name_len < theme_picker.dialog.panel_width) {
				output_buffer_append_char(&output, ' ');
				name_len++;
			}
		} else {
			dialog_draw_empty_row(&output, &theme_picker.dialog, row);
		}
	}

	dialog_draw_footer(&output, &theme_picker.dialog,
	                   "Enter:Select  Tab:Marker  Esc:Cancel");

	/* Reset attributes, keep cursor hidden while dialog is active */
	output_buffer_append_string(&output, ESCAPE_RESET);

	output_buffer_flush(&output);
	output_buffer_free(&output);
}

/*
 * Show the Theme Picker dialog with live preview.
 * Returns selected theme index, or -1 if cancelled.
 */
int theme_picker_dialog(void)
{
	/* Initialize state */
	memset(&theme_picker, 0, sizeof(theme_picker));
	theme_picker.dialog.active = true;
	theme_picker.dialog.item_count = theme_count;
	theme_picker.dialog.selected_index = current_theme_index;
	theme_picker.restore_index = current_theme_index;

	/* Calculate initial dimensions and scroll to show current theme */
	dialog_calculate_dimensions(&theme_picker.dialog);
	dialog_ensure_visible(&theme_picker.dialog);

	/* Enable dialog mouse mode and flush pending input */
	input_set_dialog_mouse_mode(true);
	tcflush(STDIN_FILENO, TCIFLUSH);

	int result = -1;
	int last_preview_index = -1;

	while (theme_picker.dialog.active) {
		/* Apply live preview when selection changes */
		if (theme_picker.dialog.selected_index != last_preview_index) {
			theme_apply_by_index(theme_picker.dialog.selected_index);
			last_preview_index = theme_picker.dialog.selected_index;

			/* Redraw entire screen with new theme, then overlay dialog */
			int __unused = render_refresh_screen(); (void)__unused;
		}

		theme_picker_draw();

		/* Read input */
		int key = input_read_key();

		if (key == -1) {
			continue;
		}

		/* Handle resize */
		if (key == KEY_RESIZE) {
			if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns)) {
				editor.screen_rows = 24;
				editor.screen_columns = 80;
			}
			int __unused = render_refresh_screen(); (void)__unused;
			continue;
		}

		/* Check for mouse event */
		if (key == KEY_MOUSE_EVENT) {
			struct mouse_input last_mouse = input_get_last_mouse();
			enum dialog_result dr = dialog_handle_mouse(&theme_picker.dialog, &last_mouse);

			if (dr == DIALOG_CONFIRM) {
				result = theme_picker.dialog.selected_index;
				theme_picker.dialog.active = false;
			} else if (dr == DIALOG_CANCEL) {
				theme_picker.dialog.active = false;
			}
			continue;
		}

		/* Tab cycles theme indicator style */
		if (key == '\t') {
			editor_cycle_theme_indicator();
			continue;
		}

		/* Handle generic dialog keys */
		enum dialog_result dr = dialog_handle_key(&theme_picker.dialog, key);

		if (dr == DIALOG_CONFIRM) {
			result = theme_picker.dialog.selected_index;
			theme_picker.dialog.active = false;
		} else if (dr == DIALOG_CANCEL) {
			theme_picker.dialog.active = false;
		}
	}

	/* If cancelled, restore original theme */
	if (result == -1) {
		theme_apply_by_index(theme_picker.restore_index);
	} else {
		/* Save selected theme to config */
		config_save();
	}

	dialog_close(&theme_picker.dialog);

	return result;
}

/*****************************************************************************
 * Help Dialog
 *****************************************************************************/

/* Help item: can be a header or a keybinding entry */
struct help_item {
	const char *key;         /* Key combination (NULL for headers) */
	const char *description; /* Description or header text */
};

/* All keyboard shortcuts organized by category */
static const struct help_item help_items[] = {
	{NULL, "FILE OPERATIONS"},
	{"Ctrl+N", "New file"},
	{"Ctrl+S", "Save"},
	{"Alt+Shift+S", "Save As"},
	{"Ctrl+O", "Open file"},
	{"Ctrl+Q", "Quit"},
	{"Ctrl+T", "Theme picker"},
	{"F1", "Help"},
	{NULL, ""},
	{NULL, "NAVIGATION"},
	{"Arrow keys", "Move cursor"},
	{"Ctrl+Left/Right", "Move by word"},
	{"Home / End", "Line start / end"},
	{"Page Up/Down", "Page navigation"},
	{"Ctrl+Home/End", "File start / end"},
	{"Ctrl+G", "Go to line"},
	{"Alt+]", "Jump to matching bracket"},
	{NULL, ""},
	{NULL, "SELECTION"},
	{"Shift+Arrows", "Extend selection"},
	{"Shift+Home/End", "Select to line start / end"},
	{"Shift+Page Up/Down", "Select by page"},
	{"Ctrl+Shift+Left/Right", "Select by word"},
	{"Ctrl+A", "Select all"},
	{"Ctrl+D", "Add cursor at next occurrence"},
	{NULL, ""},
	{NULL, "EDITING"},
	{"Ctrl+C / X / V", "Copy / Cut / Paste"},
	{"Ctrl+Z / Y", "Undo / Redo"},
	{"Backspace / Delete", "Delete character"},
	{"Alt+K", "Delete line"},
	{"Alt+D", "Duplicate line"},
	{"Alt+Up/Down", "Move line up / down"},
	{"Alt+/", "Toggle comment"},
	{NULL, ""},
	{NULL, "SEARCH"},
	{"Ctrl+F", "Find"},
	{"Ctrl+H", "Find & Replace"},
	{"F3 / Alt+N", "Find next"},
	{"Shift+F3 / Alt+P", "Find previous"},
	{"Alt+A", "Find all (multi-cursor)"},
	{"Alt+C", "Toggle case sensitivity"},
	{"Alt+W", "Toggle whole word"},
	{"Alt+R", "Toggle regex"},
	{NULL, ""},
	{NULL, "VIEW"},
	{"Alt+L", "Toggle line numbers"},
	{"Alt+Shift+W", "Toggle whitespace"},
	{"Alt+Shift+C", "Cycle color column"},
	{"Alt+Z", "Cycle wrap mode"},
};

static const int help_item_count = sizeof(help_items) / sizeof(help_items[0]);

/* Help dialog state */
static struct {
	struct dialog_state dialog;
} help_state;

/*
 * Draw the help dialog panel.
 */
static void help_draw(void)
{
	struct output_buffer output;
	output_buffer_init(&output);

	/* Recalculate dimensions in case of resize */
	dialog_calculate_dimensions(&help_state.dialog);

	/* Draw header */
	dialog_draw_header(&output, &help_state.dialog, "Help - Keyboard Shortcuts");

	/* Draw content rows */
	for (int row = 0; row < help_state.dialog.visible_rows; row++) {
		int item_index = help_state.dialog.scroll_offset + row;
		int screen_row = help_state.dialog.panel_top + 1 + row;

		dialog_goto(&output, screen_row, help_state.dialog.panel_left);

		if (item_index < help_state.dialog.item_count) {
			const struct help_item *item = &help_items[item_index];
			bool is_selected = (item_index == help_state.dialog.selected_index);

			/* Set background for selected item */
			if (is_selected) {
				dialog_set_style(&output, &active_theme.dialog_highlight);
			} else {
				dialog_set_style(&output, &active_theme.dialog);
			}

			/* Build the line content */
			char line[256];
			if (item->key == NULL) {
				/* Header or blank line */
				if (item->description[0] == '\0') {
					/* Blank line */
					snprintf(line, sizeof(line), "%*s",
					         help_state.dialog.panel_width, "");
				} else {
					/* Category header - centered */
					int desc_len = (int)strlen(item->description);
					int padding = (help_state.dialog.panel_width - desc_len) / 2;
					if (padding < 1) padding = 1;
					snprintf(line, sizeof(line), "%*s%s%*s",
					         padding, "", item->description,
					         help_state.dialog.panel_width - padding - desc_len, "");
				}
			} else {
				/* Keybinding entry: "  Key              Description" */
				snprintf(line, sizeof(line), "  %-18s %s",
				         item->key, item->description);
				/* Pad to panel width */
				int line_len = (int)strlen(line);
				if (line_len < help_state.dialog.panel_width) {
					int pad = help_state.dialog.panel_width - line_len;
					memset(line + line_len, ' ', pad);
					line[help_state.dialog.panel_width] = '\0';
				}
			}

			output_buffer_append_string(&output, line);
		} else {
			/* Empty row past end of list */
			dialog_draw_empty_row(&output, &help_state.dialog, row);
		}
	}

	/* Draw footer */
	dialog_draw_footer(&output, &help_state.dialog, "Arrow keys to scroll, Escape to close");

	output_buffer_flush(&output);
	output_buffer_free(&output);
}

/*
 * Show the Help dialog with all keyboard shortcuts.
 */
void help_dialog(void)
{
	/* Initialize state */
	memset(&help_state, 0, sizeof(help_state));
	help_state.dialog.active = true;
	help_state.dialog.item_count = help_item_count;
	help_state.dialog.selected_index = 0;

	/* Calculate initial dimensions */
	dialog_calculate_dimensions(&help_state.dialog);
	dialog_ensure_visible(&help_state.dialog);

	/* Enable dialog mouse mode and flush pending input */
	input_set_dialog_mouse_mode(true);
	tcflush(STDIN_FILENO, TCIFLUSH);

	while (help_state.dialog.active) {
		help_draw();

		/* Read input */
		int key = input_read_key();

		if (key == -1) {
			continue;
		}

		/* Handle resize */
		if (key == KEY_RESIZE) {
			if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns)) {
				editor.screen_rows = 24;
				editor.screen_columns = 80;
			}
			int __unused = render_refresh_screen(); (void)__unused;
			continue;
		}

		/* Check for mouse event */
		if (key == KEY_MOUSE_EVENT) {
			struct mouse_input last_mouse = input_get_last_mouse();
			enum dialog_result dr = dialog_handle_mouse(&help_state.dialog, &last_mouse);

			if (dr == DIALOG_CANCEL) {
				help_state.dialog.active = false;
			}
			/* No confirm action for help dialog - just scroll/navigate */
			continue;
		}

		/* Handle generic dialog keys */
		enum dialog_result dr = dialog_handle_key(&help_state.dialog, key);

		if (dr == DIALOG_CONFIRM || dr == DIALOG_CANCEL) {
			help_state.dialog.active = false;
		}
	}

	dialog_close(&help_state.dialog);
}
