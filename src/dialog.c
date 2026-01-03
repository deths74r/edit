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

#include "../lib/utflite-1.5.2/single_include/utflite.h"
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
	 * Dialog panel_top is 0-based. content_offset indicates how many rows
	 * from panel_top the list content starts (default 1 for header only,
	 * 2 for header + extra row like query input).
	 */
	int offset = dialog->content_offset > 0 ? dialog->content_offset : 1;
	int content_top = dialog->panel_top + offset;
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
 * Fuzzy Matching
 *****************************************************************************/

/*
 * Calculate fuzzy match score between pattern and text.
 * Returns score (higher = better), or -1 if no match.
 *
 * Scoring:
 *   - Each matched character: +1
 *   - Consecutive matches: +5 bonus
 *   - Match at start of word (after /, _, -, .): +10 bonus
 *   - Match at start of string: +15 bonus
 */
static int fuzzy_score(const char *pattern, const char *text, bool case_sensitive)
{
	if (pattern == NULL || pattern[0] == '\0') {
		return 0;  /* Empty pattern matches everything with score 0 */
	}
	if (text == NULL || text[0] == '\0') {
		return -1;
	}

	const char *pattern_ptr = pattern;
	const char *text_ptr = text;
	int score = 0;
	bool previous_matched = false;
	const char *prev_char = NULL;

	while (*pattern_ptr != '\0') {
		/* Find next occurrence of pattern character in text */
		bool found = false;

		while (*text_ptr != '\0') {
			char pattern_char = *pattern_ptr;
			char text_char = *text_ptr;

			/* Case insensitive comparison if requested */
			if (!case_sensitive) {
				if (pattern_char >= 'A' && pattern_char <= 'Z') {
					pattern_char = pattern_char + ('a' - 'A');
				}
				if (text_char >= 'A' && text_char <= 'Z') {
					text_char = text_char + ('a' - 'A');
				}
			}

			if (pattern_char == text_char) {
				/* Match found */
				score += 1;

				/* Consecutive match bonus */
				if (previous_matched) {
					score += 5;
				}

				/* Start of word bonus */
				if (prev_char == NULL) {
					score += 15;  /* Start of string */
				} else if (*prev_char == '/' || *prev_char == '_' ||
				           *prev_char == '-' || *prev_char == '.') {
					score += 10;  /* Start of word */
				}

				previous_matched = true;
				prev_char = text_ptr;
				text_ptr++;
				found = true;
				break;
			}

			previous_matched = false;
			prev_char = text_ptr;
			text_ptr++;
		}

		if (!found) {
			return -1;  /* Pattern character not found */
		}

		pattern_ptr++;
	}

	return score;
}

/*
 * State for recursive directory scanning.
 */
struct recursive_scan_state {
	struct file_list_item *items;
	int count;
	int capacity;
	int max_files;
	int max_depth;
	char *base_path;
	int base_path_length;
};

/*
 * Recursively scan a directory and add files to the scan state.
 * Returns 0 on success, -1 on error or if max_files reached.
 */
static int file_list_scan_recursive(struct recursive_scan_state *state,
                                    const char *path, int depth)
{
	if (depth > state->max_depth) {
		return 0;
	}

	if (state->count >= state->max_files) {
		return -1;  /* Reached file limit */
	}

	DIR *directory = opendir(path);
	if (directory == NULL) {
		return 0;  /* Skip inaccessible directories */
	}

	struct dirent *entry;
	while ((entry = readdir(directory)) != NULL) {
		/* Skip . and .. */
		if (entry->d_name[0] == '.') {
			if (entry->d_name[1] == '\0' ||
			    (entry->d_name[1] == '.' && entry->d_name[2] == '\0')) {
				continue;
			}
			/* Skip hidden files/dirs if not showing them */
			if (!editor.show_hidden_files) {
				continue;
			}
		}

		/* Build full path */
		char *full_path = path_join(path, entry->d_name);
		if (full_path == NULL) {
			continue;
		}

		/* Check if it's a directory */
		struct stat statistic;
		bool is_directory = false;
		if (stat(full_path, &statistic) == 0) {
			is_directory = S_ISDIR(statistic.st_mode);
		}

		/* Expand array if needed */
		if (state->count >= state->capacity) {
			int new_capacity = state->capacity * 2;
			struct file_list_item *new_items = realloc(
				state->items,
				new_capacity * sizeof(struct file_list_item));
			if (new_items == NULL) {
				free(full_path);
				closedir(directory);
				return -1;
			}
			state->items = new_items;
			state->capacity = new_capacity;
		}

		/* Create relative path from base */
		const char *relative_path = full_path + state->base_path_length;
		if (*relative_path == '/') {
			relative_path++;
		}

		/* Add item */
		struct file_list_item *item = &state->items[state->count];

		if (is_directory) {
			/* For directories, add trailing slash to display name */
			size_t name_length = strlen(relative_path);
			item->display_name = malloc(name_length + 2);
			if (item->display_name) {
				memcpy(item->display_name, relative_path, name_length);
				item->display_name[name_length] = '/';
				item->display_name[name_length + 1] = '\0';
			}
		} else {
			item->display_name = strdup(relative_path);
		}

		item->actual_name = strdup(full_path);
		item->is_directory = is_directory;

		if (item->display_name == NULL || item->actual_name == NULL) {
			free(item->display_name);
			free(item->actual_name);
			free(full_path);
			continue;
		}

		state->count++;

		/* Recurse into directories */
		if (is_directory && state->count < state->max_files) {
			file_list_scan_recursive(state, full_path, depth + 1);
		}

		free(full_path);

		if (state->count >= state->max_files) {
			break;
		}
	}

	closedir(directory);
	return 0;
}

/*
 * Read directory contents recursively.
 * Caller must free the returned array with file_list_free().
 * Returns NULL on error and sets count to 0.
 */
static struct file_list_item *file_list_read_recursive(const char *path,
                                                       int max_depth,
                                                       int max_files,
                                                       int *count)
{
	*count = 0;

	/* Resolve to absolute path */
	char resolved[PATH_MAX];
	if (realpath(path, resolved) == NULL) {
		return NULL;
	}

	struct recursive_scan_state state = {0};
	state.capacity = 256;
	state.max_files = max_files;
	state.max_depth = max_depth;
	state.base_path = resolved;
	state.base_path_length = strlen(resolved);

	state.items = malloc(state.capacity * sizeof(struct file_list_item));
	if (state.items == NULL) {
		return NULL;
	}

	file_list_scan_recursive(&state, resolved, 0);

	/* Sort: directories first, then alphabetically by display name */
	if (state.count > 0) {
		qsort(state.items, state.count, sizeof(struct file_list_item),
		      file_list_compare);
	}

	*count = state.count;
	return state.items;
}

/*****************************************************************************
 * Open File Dialog
 *****************************************************************************/

/*
 * Comparison function for sorting filtered items by score (descending).
 */
static int *fuzzy_sort_scores = NULL;  /* Used by comparison function */

static int fuzzy_score_compare(const void *first, const void *second)
{
	int index_first = *(const int *)first;
	int index_second = *(const int *)second;
	/* Higher score comes first */
	return fuzzy_sort_scores[index_second] - fuzzy_sort_scores[index_first];
}

/*
 * Apply fuzzy filter to items based on current query.
 * Updates filtered_indices, filtered_scores, and filtered_count.
 */
static void open_file_apply_filter(void)
{
	/* Free existing filter arrays */
	free(open_file.filtered_indices);
	free(open_file.filtered_scores);
	open_file.filtered_indices = NULL;
	open_file.filtered_scores = NULL;
	open_file.filtered_count = 0;

	if (open_file.item_count == 0) {
		open_file.dialog.item_count = 0;
		return;
	}

	/* Allocate filter arrays */
	open_file.filtered_indices = malloc(open_file.item_count * sizeof(int));
	open_file.filtered_scores = malloc(open_file.item_count * sizeof(int));
	if (open_file.filtered_indices == NULL || open_file.filtered_scores == NULL) {
		free(open_file.filtered_indices);
		free(open_file.filtered_scores);
		open_file.filtered_indices = NULL;
		open_file.filtered_scores = NULL;
		return;
	}

	/* If no query, show all items */
	if (open_file.query_length == 0) {
		for (int i = 0; i < open_file.item_count; i++) {
			open_file.filtered_indices[i] = i;
			open_file.filtered_scores[i] = 0;
		}
		open_file.filtered_count = open_file.item_count;
	} else {
		/* Filter items by fuzzy matching */
		int count = 0;
		for (int i = 0; i < open_file.item_count; i++) {
			int score = fuzzy_score(open_file.query,
			                        open_file.items[i].display_name,
			                        editor.fuzzy_case_sensitive);
			if (score >= 0) {
				open_file.filtered_indices[count] = i;
				open_file.filtered_scores[count] = score;
				count++;
			}
		}
		open_file.filtered_count = count;

		/* Sort by score (descending) */
		if (count > 1) {
			fuzzy_sort_scores = open_file.filtered_scores;
			qsort(open_file.filtered_indices, count, sizeof(int),
			      fuzzy_score_compare);
			fuzzy_sort_scores = NULL;
		}
	}

	/* Update dialog state */
	open_file.dialog.item_count = open_file.filtered_count;
	open_file.dialog.selected_index = 0;
	open_file.dialog.scroll_offset = 0;
}

/*
 * Load directory contents recursively into the open file dialog.
 * Returns true on success.
 */
static bool open_file_load_directory(const char *path)
{
	/* Free existing items and filter arrays */
	if (open_file.items) {
		file_list_free(open_file.items, open_file.item_count);
		open_file.items = NULL;
		open_file.item_count = 0;
	}
	free(open_file.filtered_indices);
	free(open_file.filtered_scores);
	open_file.filtered_indices = NULL;
	open_file.filtered_scores = NULL;
	open_file.filtered_count = 0;

	/* Clear query */
	open_file.query[0] = '\0';
	open_file.query_length = 0;

	/* Resolve to absolute path */
	char resolved[PATH_MAX];
	if (realpath(path, resolved) == NULL) {
		return false;
	}

	/* Load directory contents recursively */
	int count;
	struct file_list_item *items = file_list_read_recursive(
		resolved,
		editor.fuzzy_max_depth,
		editor.fuzzy_max_files,
		&count);
	if (items == NULL) {
		return false;
	}

	/* Update state */
	strncpy(open_file.current_path, resolved, PATH_MAX - 1);
	open_file.current_path[PATH_MAX - 1] = '\0';
	open_file.items = items;
	open_file.item_count = count;

	/* Apply initial filter (shows all items) */
	open_file_apply_filter();

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
 * Get the item at the current selection (through filtered indices).
 * Returns NULL if no valid selection.
 */
static struct file_list_item *open_file_get_selected_item(void)
{
	if (open_file.dialog.selected_index < 0 ||
	    open_file.dialog.selected_index >= open_file.filtered_count ||
	    open_file.filtered_indices == NULL) {
		return NULL;
	}

	int item_index = open_file.filtered_indices[open_file.dialog.selected_index];
	if (item_index < 0 || item_index >= open_file.item_count) {
		return NULL;
	}

	return &open_file.items[item_index];
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
	struct file_list_item *item = open_file_get_selected_item();
	if (item == NULL) {
		return NULL;
	}

	if (item->is_directory) {
		/* Navigate into directory - actual_name is full path for recursive */
		if (!open_file_load_directory(item->actual_name)) {
			editor_set_status_message("Cannot open directory: %s",
			                          item->display_name);
		}
		return NULL;
	} else {
		/* Return file path - actual_name is full path */
		return strdup(item->actual_name);
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

	/* Draw query input line */
	int query_row = open_file.dialog.panel_top + 2;
	dialog_goto(&output, query_row, open_file.dialog.panel_left + 1);
	dialog_set_style(&output, &active_theme.dialog);

	/* Draw prompt and query with leading padding */
	int chars_written = 0;
	output_buffer_append_string(&output, " \xE2\x9D\xAF ");  /* space + ❯ + space */
	chars_written += 3;  /* chevron is 1 display column */

	/* Draw query text */
	for (int i = 0; i < open_file.query_length &&
	     chars_written < open_file.dialog.panel_width - 1; i++) {
		output_buffer_append_char(&output, open_file.query[i]);
		chars_written++;
	}

	/* Remember cursor position for later */
	int cursor_col = open_file.dialog.panel_left + 1 + chars_written;

	/* Pad to full width */
	while (chars_written < open_file.dialog.panel_width) {
		output_buffer_append_char(&output, ' ');
		chars_written++;
	}

	/* Draw file list (offset by 1 for query line) */
	int visible_rows = open_file.dialog.visible_rows - 1;  /* Account for query line */
	if (visible_rows < 0) visible_rows = 0;

	for (int row = 0; row < visible_rows; row++) {
		int filtered_index = open_file.dialog.scroll_offset + row;

		/* Position at row + 1 to account for query input line */
		int screen_row = open_file.dialog.panel_top + 3 + row;
		dialog_goto(&output, screen_row, open_file.dialog.panel_left + 1);

		int row_chars = 0;

		if (filtered_index < open_file.filtered_count &&
		    open_file.filtered_indices != NULL) {
			int item_index = open_file.filtered_indices[filtered_index];
			struct file_list_item *item = &open_file.items[item_index];
			bool is_selected = (filtered_index == open_file.dialog.selected_index);

			/* Set style based on selection */
			if (is_selected) {
				dialog_set_style(&output, &active_theme.dialog_highlight);
			} else {
				dialog_set_style(&output, &active_theme.dialog);
			}

			/* Leading space for padding */
			output_buffer_append_char(&output, ' ');
			row_chars++;

			/* Draw icon if enabled */
			if (editor.show_file_icons) {
				if (item->is_directory) {
					output_buffer_append_string(&output, "\xF0\x9F\x97\x81  ");
					row_chars += 3;
				} else {
					output_buffer_append_string(&output, "   ");
					row_chars += 3;
				}
			}

			/* Draw display name, truncating if necessary */
			int name_len = strlen(item->display_name);
			for (int i = 0; i < name_len &&
			     row_chars < open_file.dialog.panel_width - 1; i++) {
				output_buffer_append_char(&output, item->display_name[i]);
				row_chars++;
			}

			/* Pad to full width */
			while (row_chars < open_file.dialog.panel_width) {
				output_buffer_append_char(&output, ' ');
				row_chars++;
			}
		} else {
			/* Empty row */
			dialog_set_style(&output, &active_theme.dialog);
			while (row_chars < open_file.dialog.panel_width) {
				output_buffer_append_char(&output, ' ');
				row_chars++;
			}
		}
	}

	/* Draw footer */
	dialog_draw_footer(&output, &open_file.dialog,
	                   "Tab:Hidden  Shift+Tab:Icons  Enter:Open  Esc:Cancel");

	/* Position cursor in query area and show it */
	output_buffer_append_string(&output, ESCAPE_RESET);
	dialog_goto(&output, query_row, cursor_col);
	output_buffer_append_string(&output, ESCAPE_CURSOR_SHOW);

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
	open_file.dialog.content_offset = 2;  /* Query row shifts file list down */

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

		/* Handle backspace - delete from query */
		if (key == KEY_BACKSPACE || key == 8) {  /* 8 = Ctrl+H */
			if (open_file.query_length > 0) {
				open_file.query_length--;
				open_file.query[open_file.query_length] = '\0';
				open_file_apply_filter();
			}
			continue;
		}

		/* Handle Escape - clear query first, then cancel */
		if (key == 27) {  /* Escape */
			if (open_file.query_length > 0) {
				/* Clear query */
				open_file.query[0] = '\0';
				open_file.query_length = 0;
				open_file_apply_filter();
			} else {
				/* Cancel dialog */
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
			struct file_list_item *item = open_file_get_selected_item();
			if (item != NULL && item->is_directory) {
				open_file_select_item();
			}
			continue;
		}

		/* Tab toggles hidden files */
		if (key == '\t') {
			editor.show_hidden_files = !editor.show_hidden_files;
			config_save();
			/* Rescan directory with new setting */
			char current[PATH_MAX];
			strncpy(current, open_file.current_path, PATH_MAX - 1);
			current[PATH_MAX - 1] = '\0';
			open_file_load_directory(current);
			continue;
		}

		/* Shift+Tab toggles file icons */
		if (key == KEY_SHIFT_TAB) {
			editor.show_file_icons = !editor.show_file_icons;
			config_save();
			continue;
		}

		/* Handle Enter - select item */
		if (key == '\r' || key == '\n') {
			result = open_file_select_item();
			if (result) {
				open_file.dialog.active = false;
			}
			continue;
		}

		/* Handle printable characters - add to query */
		if (key >= 32 && key < 127) {
			if (open_file.query_length < (int)sizeof(open_file.query) - 1) {
				open_file.query[open_file.query_length] = (char)key;
				open_file.query_length++;
				open_file.query[open_file.query_length] = '\0';
				open_file_apply_filter();
			}
			continue;
		}

		/* Handle arrow keys for navigation */
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
	free(open_file.filtered_indices);
	free(open_file.filtered_scores);
	open_file.filtered_indices = NULL;
	open_file.filtered_scores = NULL;

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
	{NULL, "File Operations"},
	{"Ctrl+N", "New file"},
	{"Ctrl+S", "Save"},
	{"Alt+Shift+S", "Save As"},
	{"Ctrl+O", "Open file"},
	{"Ctrl+Q", "Quit"},
	{"Ctrl+T", "Theme picker"},
	{"F1", "Help"},
	{NULL, ""},
	{NULL, "Navigation"},
	{"Arrow keys", "Move cursor"},
	{"Ctrl+Left/Right", "Move by word"},
	{"Home / End", "Line start / end"},
	{"Page Up/Down", "Page navigation"},
	{"Ctrl+Home/End", "File start / end"},
	{"Ctrl+G", "Go to line"},
	{"Alt+]", "Jump to matching bracket"},
	{NULL, ""},
	{NULL, "Selection"},
	{"Shift+Arrows", "Extend selection"},
	{"Shift+Home/End", "Select to line start / end"},
	{"Shift+Page Up/Down", "Select by page"},
	{"Ctrl+Shift+Left/Right", "Select by word"},
	{"Ctrl+A", "Select all"},
	{"Ctrl+D", "Add cursor at next occurrence"},
	{NULL, ""},
	{NULL, "Editing"},
	{"Ctrl+C / X / V", "Copy / Cut / Paste"},
	{"Ctrl+Z / Y", "Undo / Redo"},
	{"Backspace / Delete", "Delete character"},
	{"Alt+K", "Delete line"},
	{"Alt+D", "Duplicate line"},
	{"Alt+Up/Down", "Move line up / down"},
	{"Alt+/", "Toggle comment"},
	{NULL, ""},
	{NULL, "Search"},
	{"Ctrl+F", "Find"},
	{"Ctrl+H", "Find & Replace"},
	{"F3 / Alt+N", "Find next"},
	{"Shift+F3 / Alt+P", "Find previous"},
	{"Alt+A", "Find all (multi-cursor)"},
	{"Alt+C", "Toggle case sensitivity"},
	{"Alt+W", "Toggle whole word"},
	{"Alt+R", "Toggle regex"},
	{NULL, ""},
	{NULL, "View"},
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

	/* Hide cursor */
	output_buffer_append_string(&output, "\x1b[?25l");

	/* Calculate dimensions to fit all content */
	int content_rows = help_item_count;
	int panel_height = content_rows + 4;  /* +1 top, +1 blank before footer, +1 footer, +1 bottom */
	int key_column_width = 24;  /* Fits "Ctrl+Shift+Left/Right" */
	int panel_width = 60;  /* Wide enough for key + description */

	/* Center the panel */
	int panel_top = ((int)editor.screen_rows - panel_height) / 2;
	int panel_left = ((int)editor.screen_columns - panel_width) / 2;
	if (panel_top < 1) panel_top = 1;
	if (panel_left < 1) panel_left = 1;

	/* Store dimensions for drawing helpers */
	help_state.dialog.panel_top = panel_top;
	help_state.dialog.panel_left = panel_left;
	help_state.dialog.panel_width = panel_width;
	help_state.dialog.panel_height = panel_height;
	help_state.dialog.visible_rows = content_rows;

	/* Draw top padding row (no title) */
	dialog_goto(&output, panel_top + 1, panel_left + 1);
	dialog_set_style(&output, &active_theme.dialog);
	for (int i = 0; i < panel_width; i++)
		output_buffer_append_char(&output, ' ');

	/* Draw content rows (start at panel_top + 2, after top padding) */
	for (int row = 0; row < content_rows; row++) {
		int screen_row = panel_top + 2 + row;
		const struct help_item *item = &help_items[row];

		dialog_goto(&output, screen_row, panel_left + 1);
		dialog_set_style(&output, &active_theme.dialog);

		/* Build the line content */
		char line[256];
		if (item->key == NULL) {
			/* Header or blank line */
			if (item->description[0] == '\0') {
				/* Blank line */
				snprintf(line, sizeof(line), "%*s", panel_width, "");
			} else {
				/* Category header - left justified, bold */
				output_buffer_append_string(&output, "\x1b[1m");  /* Bold */
				int desc_len = (int)strlen(item->description);
				snprintf(line, sizeof(line), "  %s%*s",
				         item->description,
				         panel_width - 2 - desc_len, "");
				output_buffer_append_string(&output, line);
				output_buffer_append_string(&output, "\x1b[22m");  /* Normal weight */
				continue;  /* Skip the normal append below */
			}
		} else {
			/* Keybinding entry: "  Key                    Description" */
			snprintf(line, sizeof(line), "  %-*s %s",
			         key_column_width, item->key, item->description);
			/* Pad to panel width */
			int line_len = (int)strlen(line);
			if (line_len < panel_width) {
				int pad = panel_width - line_len;
				memset(line + line_len, ' ', pad);
				line[panel_width] = '\0';
			}
		}

		output_buffer_append_string(&output, line);
	}

	/* Draw blank line before footer */
	dialog_goto(&output, panel_top + content_rows + 2, panel_left + 1);
	dialog_set_style(&output, &active_theme.dialog);
	for (int i = 0; i < panel_width; i++)
		output_buffer_append_char(&output, ' ');

	/* Draw footer with left-aligned text */
	const char *hint = "Press any key to close";
	int hint_len = (int)strlen(hint);
	int footer_row = panel_top + content_rows + 3;
	dialog_goto(&output, footer_row, panel_left + 1);
	dialog_set_style(&output, &active_theme.dialog_footer);

	output_buffer_append_string(&output, "  ");  /* Match content indent */
	output_buffer_append_string(&output, hint);
	for (int i = 2 + hint_len; i < panel_width; i++)
		output_buffer_append_char(&output, ' ');

	/* Draw bottom padding row */
	dialog_goto(&output, panel_top + panel_height, panel_left + 1);
	dialog_set_style(&output, &active_theme.dialog);
	for (int i = 0; i < panel_width; i++)
		output_buffer_append_char(&output, ' ');

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

	/* Flush pending input */
	tcflush(STDIN_FILENO, TCIFLUSH);

	while (help_state.dialog.active) {
		help_draw();

		/* Read input - any key closes the dialog */
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

		/* Ignore mouse events */
		if (key == KEY_MOUSE_EVENT) {
			continue;
		}

		/* Any other key closes the dialog */
		help_state.dialog.active = false;
	}

	/* Show cursor again */
	printf("\x1b[?25h");
	fflush(stdout);

	dialog_close(&help_state.dialog);
}
