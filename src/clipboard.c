/*
 * clipboard.c - System clipboard integration for edit
 *
 * Provides copy, cut, and paste with system clipboard.
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
extern void editor_delete_selection(void);
extern bool selection_is_empty(void);
extern void selection_clear(void);
extern char *selection_get_text(size_t *out_length);

/* Functions from buffer.c */
extern int buffer_insert_newline_checked(struct buffer *buffer, uint32_t row, uint32_t column);
extern int buffer_insert_cell_at_column_checked(struct buffer *buffer, uint32_t row,
                                                 uint32_t column, uint32_t codepoint);

/* Functions from undo.c */
extern void undo_begin_group(struct buffer *buffer, uint32_t row, uint32_t column);
extern void undo_end_group(struct buffer *buffer, uint32_t row, uint32_t column);
extern void undo_record_insert_char(struct buffer *buffer, uint32_t row, uint32_t column,
                                    uint32_t codepoint);
extern void undo_record_insert_newline(struct buffer *buffer, uint32_t row, uint32_t column);

/* Functions from error.c */
extern const char *edit_strerror(int error_code);

/*****************************************************************************
 * Internal State
 *****************************************************************************/

/* Internal clipboard buffer (fallback when system clipboard unavailable). */
static char *internal_clipboard = NULL;
static size_t internal_clipboard_length = 0;

/* Clipboard tool detection (cached on first use). */
static enum clipboard_tool detected_clipboard_tool = CLIPBOARD_UNKNOWN;

/*****************************************************************************
 * Clipboard Tool Detection
 *****************************************************************************/

/*
 * Detect which clipboard tool is available on the system.
 * Checks for wl-copy (Wayland), xclip, and xsel in order of preference.
 * Result is cached after first call.
 */
static enum clipboard_tool clipboard_detect_tool(void)
{
	if (detected_clipboard_tool != CLIPBOARD_UNKNOWN) {
		return detected_clipboard_tool;
	}

	/* Check for Wayland first if WAYLAND_DISPLAY is set */
	if (getenv("WAYLAND_DISPLAY") != NULL) {
		if (system("command -v wl-copy >/dev/null 2>&1") == 0) {
			detected_clipboard_tool = CLIPBOARD_WL;
			return detected_clipboard_tool;
		}
	}

	/* Check for X11 tools */
	if (system("command -v xclip >/dev/null 2>&1") == 0) {
		detected_clipboard_tool = CLIPBOARD_XCLIP;
		return detected_clipboard_tool;
	}

	if (system("command -v xsel >/dev/null 2>&1") == 0) {
		detected_clipboard_tool = CLIPBOARD_XSEL;
		return detected_clipboard_tool;
	}

	detected_clipboard_tool = CLIPBOARD_INTERNAL;
	return detected_clipboard_tool;
}

/*****************************************************************************
 * Clipboard Operations
 *****************************************************************************/

/*
 * Copy the given text to the system clipboard. Falls back to internal
 * buffer if no clipboard tool is available. Returns true on success.
 */
bool clipboard_copy(const char *text, size_t length)
{
	if (text == NULL || length == 0) {
		return false;
	}

	enum clipboard_tool tool = clipboard_detect_tool();

	if (tool == CLIPBOARD_INTERNAL) {
		/* Use internal buffer */
		free(internal_clipboard);
		internal_clipboard = malloc(length + 1);
		if (internal_clipboard == NULL) {
			return false;
		}
		memcpy(internal_clipboard, text, length);
		internal_clipboard[length] = '\0';
		internal_clipboard_length = length;
		return true;
	}

	/* Use system clipboard */
	const char *command = NULL;
	switch (tool) {
		case CLIPBOARD_XCLIP:
			command = "xclip -selection clipboard";
			break;
		case CLIPBOARD_XSEL:
			command = "xsel --clipboard --input";
			break;
		case CLIPBOARD_WL:
			command = "wl-copy";
			break;
		default:
			return false;
	}

	FILE *pipe = popen(command, "w");
	if (pipe == NULL) {
		return false;
	}

	size_t written = fwrite(text, 1, length, pipe);
	int status = pclose(pipe);

	return written == length && status == 0;
}

/*
 * Paste from the system clipboard. Returns a newly allocated string
 * that the caller must free, or NULL on failure. Sets *out_length
 * to the length of the returned string (excluding null terminator).
 */
char *clipboard_paste(size_t *out_length)
{
	enum clipboard_tool tool = clipboard_detect_tool();

	if (tool == CLIPBOARD_INTERNAL) {
		/* Use internal buffer */
		if (internal_clipboard == NULL || internal_clipboard_length == 0) {
			*out_length = 0;
			return NULL;
		}
		char *copy = malloc(internal_clipboard_length + 1);
		if (copy == NULL) {
			*out_length = 0;
			return NULL;
		}
		memcpy(copy, internal_clipboard, internal_clipboard_length);
		copy[internal_clipboard_length] = '\0';
		*out_length = internal_clipboard_length;
		return copy;
	}

	/* Use system clipboard */
	const char *command = NULL;
	switch (tool) {
		case CLIPBOARD_XCLIP:
			command = "xclip -selection clipboard -o";
			break;
		case CLIPBOARD_XSEL:
			command = "xsel --clipboard --output";
			break;
		case CLIPBOARD_WL:
			command = "wl-paste -n";  /* -n: no trailing newline */
			break;
		default:
			*out_length = 0;
			return NULL;
	}

	FILE *pipe = popen(command, "r");
	if (pipe == NULL) {
		*out_length = 0;
		return NULL;
	}

	/* Read clipboard contents dynamically */
	size_t capacity = CLIPBOARD_INITIAL_CAPACITY;
	size_t length = 0;
	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		pclose(pipe);
		*out_length = 0;
		return NULL;
	}

	while (!feof(pipe)) {
		if (length + CLIPBOARD_READ_CHUNK_SIZE > capacity) {
			capacity *= 2;
			char *new_buffer = realloc(buffer, capacity);
			if (new_buffer == NULL) {
				free(buffer);
				pclose(pipe);
				*out_length = 0;
				return NULL;
			}
			buffer = new_buffer;
		}

		size_t read_count = fread(buffer + length, 1, CLIPBOARD_READ_CHUNK_SIZE, pipe);
		length += read_count;

		if (read_count == 0) {
			break;
		}
	}

	pclose(pipe);

	buffer[length] = '\0';
	*out_length = length;
	return buffer;
}

/*****************************************************************************
 * Internal Clipboard (fallback)
 *****************************************************************************/

/*
 * Free internal clipboard resources.
 * Call during program shutdown.
 */
void clipboard_cleanup(void)
{
	free(internal_clipboard);
	internal_clipboard = NULL;
	internal_clipboard_length = 0;
}

/*****************************************************************************
 * Editor Clipboard Operations
 *****************************************************************************/

/*
 * Copy the current selection to the clipboard without deleting it.
 */
void editor_copy(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		editor_set_status_message("Nothing to copy");
		return;
	}

	size_t length;
	char *text = selection_get_text(&length);
	if (text == NULL) {
		editor_set_status_message("Copy failed");
		return;
	}

	if (clipboard_copy(text, length)) {
		editor_set_status_message("Copied %zu bytes", length);
	} else {
		editor_set_status_message("Copy to clipboard failed");
	}

	free(text);
}

/*
 * Cut the current selection: copy to clipboard and delete.
 */
void editor_cut(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		editor_set_status_message("Nothing to cut");
		return;
	}

	size_t length;
	char *text = selection_get_text(&length);
	if (text == NULL) {
		editor_set_status_message("Cut failed");
		return;
	}

	if (clipboard_copy(text, length)) {
		editor_delete_selection();
		editor_set_status_message("Cut %zu bytes", length);
	} else {
		editor_set_status_message("Cut to clipboard failed");
	}

	free(text);
}

/*
 * Paste from clipboard at current cursor position.
 * If there's a selection, replaces it with pasted content.
 */
void editor_paste(void)
{
	size_t length;
	char *text = clipboard_paste(&length);

	if (text == NULL || length == 0) {
		editor_set_status_message("Clipboard empty");
		free(text);
		return;
	}

	undo_begin_group(&editor.buffer, editor.cursor_row, editor.cursor_column);

	/* Delete selection if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	/* Track starting row for re-highlighting */
	uint32_t start_row = editor.cursor_row;

	/* Insert text character by character, handling newlines */
	size_t offset = 0;
	uint32_t chars_inserted = 0;
	int ret = 0;

	while (offset < length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, length - offset, &codepoint);

		if (bytes <= 0) {
			/* Invalid UTF-8, skip one byte */
			offset++;
			continue;
		}

		if (codepoint == '\n') {
			undo_record_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
			ret = buffer_insert_newline_checked(&editor.buffer, editor.cursor_row,
			                                     editor.cursor_column);
			if (ret)
				break;
			editor.cursor_row++;
			editor.cursor_column = 0;
		} else if (codepoint == '\r') {
			/* Skip carriage returns (Windows line endings) */
		} else {
			undo_record_insert_char(&editor.buffer, editor.cursor_row,
			                        editor.cursor_column, codepoint);
			ret = buffer_insert_cell_at_column_checked(&editor.buffer, editor.cursor_row,
			                                            editor.cursor_column, codepoint);
			if (ret)
				break;
			editor.cursor_column++;
		}

		chars_inserted++;
		offset += bytes;
	}

	undo_end_group(&editor.buffer, editor.cursor_row, editor.cursor_column);

	if (ret) {
		editor_set_status_message("Paste failed after %u chars: %s",
		                          chars_inserted, edit_strerror(ret));
	}

	/* Recompute pairs and re-highlight affected lines */
	buffer_compute_pairs(&editor.buffer);
	for (uint32_t row = start_row; row <= editor.cursor_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		if (line_get_temperature(line) != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(line, &editor.buffer, row);
		}
	}

	free(text);
	editor.buffer.is_modified = true;
	if (!ret)
		editor_set_status_message("Pasted %u characters", chars_inserted);
}
