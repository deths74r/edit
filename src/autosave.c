/*
 * autosave.c - Auto-save and crash recovery for edit
 *
 * Provides periodic automatic saves to swap files and
 * recovery from previous crashed sessions.
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "../third_party/utflite/single_include/utflite.h"
#include "edit.h"

/*****************************************************************************
 * Global State
 *****************************************************************************/

/* Autosave state */
static struct autosave_state autosave = {
	.enabled = true
};

/* Global snapshot pointer for worker thread access */
static struct buffer_snapshot *pending_snapshot = NULL;
static pthread_mutex_t snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Access to editor state (defined in edit.c) */
extern struct editor_state editor;

/*****************************************************************************
 * Autosave Configuration
 *****************************************************************************/

void autosave_set_enabled(bool enabled)
{
	autosave.enabled = enabled;
}

bool autosave_is_enabled(void)
{
	return autosave.enabled;
}

void autosave_set_swap_exists(bool exists)
{
	autosave.swap_exists = exists;
}

/*****************************************************************************
 * Swap Path Generation
 *****************************************************************************/

/*
 * Generate swap file path for a given file.
 * Format: .filename.swp (in same directory as file)
 * For unnamed files: ~/.edit/.unnamed.swp
 */
static void autosave_generate_swap_path(const char *filename, char *swap_path, size_t size)
{
	if (filename == NULL || filename[0] == '\0') {
		/* Unnamed file - use home directory */
		const char *home = getenv("HOME");
		if (home) {
			snprintf(swap_path, size, "%s/.edit/.unnamed.swp", home);
		} else {
			snprintf(swap_path, size, "/tmp/.edit-unnamed.swp");
		}
		return;
	}

	/* Find directory and basename */
	const char *slash = strrchr(filename, '/');

	if (slash) {
		/* Has directory component */
		size_t dir_len = slash - filename + 1;
		if (dir_len >= size - 1) {
			dir_len = size - 2;
		}
		memcpy(swap_path, filename, dir_len);
		snprintf(swap_path + dir_len, size - dir_len, ".%s.swp", slash + 1);
	} else {
		/* Just filename, use current directory */
		snprintf(swap_path, size, ".%s.swp", filename);
	}
}

/*
 * Update swap path when filename changes.
 */
void autosave_update_path(void)
{
	autosave_generate_swap_path(editor.buffer.filename,
	                            autosave.swap_path,
	                            sizeof(autosave.swap_path));
}

/*****************************************************************************
 * Buffer Snapshot
 *****************************************************************************/

/*
 * Create a snapshot of the current buffer for background saving.
 * Returns NULL on allocation failure.
 * Caller must free with buffer_snapshot_free().
 */
struct buffer_snapshot *buffer_snapshot_create(void)
{
	struct buffer_snapshot *snapshot = calloc(1, sizeof(struct buffer_snapshot));
	if (snapshot == NULL) {
		return NULL;
	}

	uint32_t line_count = editor.buffer.line_count;
	snapshot->line_count = line_count;
	strncpy(snapshot->swap_path, autosave.swap_path, PATH_MAX - 1);

	if (line_count == 0) {
		snapshot->lines = NULL;
		return snapshot;
	}

	snapshot->lines = calloc(line_count, sizeof(char *));
	if (snapshot->lines == NULL) {
		free(snapshot);
		return NULL;
	}

	/* Convert each line to UTF-8 string */
	for (uint32_t row = 0; row < line_count; row++) {
		struct line *line = &editor.buffer.lines[row];

		/* Ensure line is warm */
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			/* Use mmap content directly for cold lines */
			if (editor.buffer.mmap_base != NULL &&
			    line->mmap_offset + line->mmap_length <= editor.buffer.mmap_size) {
				snapshot->lines[row] = malloc(line->mmap_length + 1);
				if (snapshot->lines[row]) {
					memcpy(snapshot->lines[row],
					       editor.buffer.mmap_base + line->mmap_offset,
					       line->mmap_length);
					snapshot->lines[row][line->mmap_length] = '\0';
				}
			} else {
				snapshot->lines[row] = strdup("");
			}
		} else if (line->cells != NULL) {
			/* Convert cells to UTF-8 */
			size_t max_bytes = line->cell_count * 4 + 1;
			char *str = malloc(max_bytes);
			if (str) {
				size_t offset = 0;
				for (uint32_t col = 0; col < line->cell_count; col++) {
					char utf8[4];
					int bytes = utflite_encode(line->cells[col].codepoint, utf8);
					if (bytes > 0 && offset + bytes < max_bytes) {
						memcpy(str + offset, utf8, bytes);
						offset += bytes;
					}
				}
				str[offset] = '\0';
				snapshot->lines[row] = str;
			}
		} else {
			snapshot->lines[row] = strdup("");
		}

		/* Check for allocation failure */
		if (snapshot->lines[row] == NULL) {
			/* Cleanup and fail */
			for (uint32_t i = 0; i < row; i++) {
				free(snapshot->lines[i]);
			}
			free(snapshot->lines);
			free(snapshot);
			return NULL;
		}
	}

	return snapshot;
}

/*
 * Free a buffer snapshot.
 */
void buffer_snapshot_free(struct buffer_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	if (snapshot->lines) {
		for (uint32_t i = 0; i < snapshot->line_count; i++) {
			free(snapshot->lines[i]);
		}
		free(snapshot->lines);
	}
	free(snapshot);
}

/*****************************************************************************
 * Worker Thread Interface
 *****************************************************************************/

/*
 * Worker task: write buffer snapshot to swap file.
 */
int worker_process_autosave(struct task *task, struct task_result *result)
{
	result->autosave.success = false;
	result->autosave.bytes_written = 0;

	/* Get the snapshot */
	pthread_mutex_lock(&snapshot_mutex);
	struct buffer_snapshot *snapshot = pending_snapshot;
	pending_snapshot = NULL;
	pthread_mutex_unlock(&snapshot_mutex);

	if (snapshot == NULL) {
		log_warn("Autosave task with no snapshot");
		return -EINVAL;
	}

	/* Check cancellation before expensive I/O */
	if (task_is_cancelled(task)) {
		buffer_snapshot_free(snapshot);
		return -EEDIT_CANCELLED;
	}

	/* Ensure directory exists for unnamed files */
	if (strstr(snapshot->swap_path, "/.edit/") != NULL) {
		char dir[PATH_MAX];
		const char *home = getenv("HOME");
		if (home) {
			snprintf(dir, sizeof(dir), "%s/.edit", home);
			mkdir(dir, 0700);  /* Ignore error if exists */
		}
	}

	/* Write to temporary file first, then rename (atomic) */
	char tmp_path[PATH_MAX];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", snapshot->swap_path);

	FILE *fp = fopen(tmp_path, "w");
	if (fp == NULL) {
		log_warn("Cannot create swap file: %s (%s)", tmp_path, strerror(errno));
		buffer_snapshot_free(snapshot);
		return -errno;
	}

	size_t bytes_written = 0;

	for (uint32_t row = 0; row < snapshot->line_count; row++) {
		/* Check cancellation periodically */
		if (row % 1000 == 0 && task_is_cancelled(task)) {
			fclose(fp);
			unlink(tmp_path);
			buffer_snapshot_free(snapshot);
			return -EEDIT_CANCELLED;
		}

		if (snapshot->lines[row]) {
			size_t len = strlen(snapshot->lines[row]);
			if (fwrite(snapshot->lines[row], 1, len, fp) != len) {
				log_warn("Write error in autosave");
				fclose(fp);
				unlink(tmp_path);
				buffer_snapshot_free(snapshot);
				return -EIO;
			}
			bytes_written += len;
		}

		/* Write newline (except for last line) */
		if (row < snapshot->line_count - 1) {
			if (fputc('\n', fp) == EOF) {
				log_warn("Write error in autosave");
				fclose(fp);
				unlink(tmp_path);
				buffer_snapshot_free(snapshot);
				return -EIO;
			}
			bytes_written++;
		}
	}

	/* Flush and close */
	if (fflush(fp) != 0 || fclose(fp) != 0) {
		log_warn("Flush error in autosave");
		unlink(tmp_path);
		buffer_snapshot_free(snapshot);
		return -EIO;
	}

	/* Atomic rename */
	if (rename(tmp_path, snapshot->swap_path) != 0) {
		log_warn("Cannot rename swap file: %s", strerror(errno));
		unlink(tmp_path);
		buffer_snapshot_free(snapshot);
		return -errno;
	}

	buffer_snapshot_free(snapshot);

	result->autosave.success = true;
	result->autosave.bytes_written = bytes_written;

	log_debug("Autosave complete: %zu bytes to %s", bytes_written, task->autosave.swap_path);

	return 0;
}

/*
 * Handle autosave task result.
 */
void autosave_handle_result(struct task_result *result)
{
	if (result->task_id != autosave.task_id) {
		return;
	}

	autosave.save_pending = false;

	if (result->error == -EEDIT_CANCELLED) {
		log_debug("Autosave cancelled");
	} else if (result->error) {
		log_warn("Autosave failed: %s", edit_strerror(result->error));
	} else {
		autosave.swap_exists = true;
		log_debug("Autosave successful: %zu bytes",
		          result->autosave.bytes_written);
	}
}

/*****************************************************************************
 * Autosave Operations
 *****************************************************************************/

/*
 * Mark buffer as modified (for auto-save tracking).
 * Call this whenever buffer content changes.
 */
void autosave_mark_modified(void)
{
	autosave.last_modify_time = time(NULL);
}

/*
 * Check if auto-save should run and trigger if needed.
 * Call this periodically from the main loop.
 */
void autosave_check(void)
{
	if (!autosave.enabled || !worker_is_initialized()) {
		return;
	}

	/* Don't auto-save if save is already pending */
	if (autosave.save_pending) {
		return;
	}

	/* Don't auto-save unmodified buffers */
	if (!editor.buffer.is_modified) {
		/* Reset modify time when buffer becomes unmodified (after save) */
		autosave.last_modify_time = 0;
		return;
	}

	/* Don't auto-save empty buffers */
	if (editor.buffer.line_count == 0) {
		return;
	}

	/* Track when buffer became modified */
	time_t now = time(NULL);
	if (autosave.last_modify_time == 0) {
		autosave.last_modify_time = now;
	}

	/* Check time since last save */
	if (now - autosave.last_save_time < AUTOSAVE_INTERVAL) {
		return;
	}

	/* Check if buffer was modified since last auto-save */
	if (autosave.last_modify_time <= autosave.last_save_time) {
		return;
	}

	/* Estimate buffer size - skip huge files */
	size_t estimated_size = 0;
	uint32_t sample_count = editor.buffer.line_count < 1000 ? editor.buffer.line_count : 1000;
	for (uint32_t row = 0; row < sample_count; row++) {
		struct line *line = &editor.buffer.lines[row];
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			estimated_size += line->mmap_length;
		} else {
			estimated_size += line->cell_count * 2;  /* Rough estimate */
		}
	}
	estimated_size = (estimated_size * editor.buffer.line_count) / sample_count;

	if (estimated_size > AUTOSAVE_MAX_SIZE) {
		log_debug("Skipping autosave: file too large (~%zu bytes)", estimated_size);
		return;
	}

	/* Update swap path if needed */
	autosave_update_path();

	/* Create snapshot */
	struct buffer_snapshot *snapshot = buffer_snapshot_create();
	if (snapshot == NULL) {
		log_warn("Failed to create buffer snapshot for autosave");
		return;
	}

	/* Store snapshot for worker */
	pthread_mutex_lock(&snapshot_mutex);
	if (pending_snapshot != NULL) {
		buffer_snapshot_free(pending_snapshot);
	}
	pending_snapshot = snapshot;
	pthread_mutex_unlock(&snapshot_mutex);

	/* Submit task */
	struct task task = {
		.type = TASK_AUTOSAVE,
		.task_id = task_generate_id()
	};
	strncpy(task.autosave.swap_path, autosave.swap_path, PATH_MAX - 1);

	int err = task_queue_push(&task);
	if (err == 0) {
		autosave.task_id = task.task_id;
		autosave.save_pending = true;
		autosave.last_save_time = now;
		log_debug("Triggered autosave to %s", autosave.swap_path);
	} else {
		/* Failed to queue - free snapshot */
		pthread_mutex_lock(&snapshot_mutex);
		buffer_snapshot_free(pending_snapshot);
		pending_snapshot = NULL;
		pthread_mutex_unlock(&snapshot_mutex);
		log_warn("Failed to queue autosave: %s", edit_strerror(err));
	}
}

/*
 * Remove the swap file (called on clean save or exit).
 */
void autosave_remove_swap(void)
{
	if (autosave.swap_path[0] != '\0') {
		if (unlink(autosave.swap_path) == 0) {
			log_debug("Removed swap file: %s", autosave.swap_path);
		}
		autosave.swap_exists = false;
	}
}

/*
 * Reset autosave state after successful save.
 */
void autosave_on_save(void)
{
	autosave_remove_swap();
	autosave.last_save_time = time(NULL);
}

/*****************************************************************************
 * Crash Recovery
 *****************************************************************************/

/*
 * Check if a swap file exists for the given filename.
 * Returns the swap file path if found, NULL otherwise.
 */
const char *autosave_check_recovery(const char *filename)
{
	static char swap_path[PATH_MAX];

	autosave_generate_swap_path(filename, swap_path, sizeof(swap_path));

	struct stat st;
	if (stat(swap_path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
		return swap_path;
	}

	return NULL;
}

/*
 * Get modification time of swap file.
 */
static time_t autosave_get_swap_mtime(const char *swap_path)
{
	struct stat st;
	if (stat(swap_path, &st) == 0) {
		return st.st_mtime;
	}
	return 0;
}

/*
 * Format a time for display.
 */
static void autosave_format_time(time_t t, char *buf, size_t size)
{
	struct tm *tm = localtime(&t);
	if (tm) {
		strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm);
	} else {
		strncpy(buf, "unknown time", size);
	}
}

/*
 * Draw a single text row in the swap recovery dialog.
 * text can be NULL for an empty row.
 */
static void swap_dialog_draw_row(struct output_buffer *output,
				 struct dialog_state *dialog,
				 int row_index,
				 const char *text)
{
	int screen_row = dialog->panel_top + 2 + row_index;
	dialog_goto(output, screen_row, dialog->panel_left + 1);
	dialog_set_style(output, &active_theme.dialog);

	int chars_written = 0;
	output_buffer_append_char(output, ' ');
	chars_written++;

	if (text != NULL) {
		int text_len = strlen(text);
		for (int i = 0; i < text_len && chars_written < dialog->panel_width - 1; i++) {
			output_buffer_append_char(output, text[i]);
			chars_written++;
		}
	}

	/* Fill rest with spaces */
	while (chars_written < dialog->panel_width) {
		output_buffer_append_char(output, ' ');
		chars_written++;
	}
}

/*
 * Draw the swap file recovery dialog.
 */
static void swap_recovery_draw(struct output_buffer *output,
			       struct dialog_state *dialog,
			       const char *filename,
			       const char *swap_path,
			       const char *time_str,
			       size_t swap_size)
{
	dialog_draw_header(output, dialog, "SWAP FILE FOUND");

	/* Content rows */
	int row = 0;
	swap_dialog_draw_row(output, dialog, row++, "A swap file was found for:");

	/* Filename (truncated if needed) */
	char filename_line[256];
	snprintf(filename_line, sizeof(filename_line), "  %s",
		 filename ? filename : "(unnamed)");
	swap_dialog_draw_row(output, dialog, row++, filename_line);

	swap_dialog_draw_row(output, dialog, row++, NULL);  /* blank */

	/* Swap file details */
	char detail_line[256];
	snprintf(detail_line, sizeof(detail_line), "Swap file: %s", swap_path);
	swap_dialog_draw_row(output, dialog, row++, detail_line);

	snprintf(detail_line, sizeof(detail_line), "Modified:  %s", time_str);
	swap_dialog_draw_row(output, dialog, row++, detail_line);

	snprintf(detail_line, sizeof(detail_line), "Size:      %zu bytes", swap_size);
	swap_dialog_draw_row(output, dialog, row++, detail_line);

	swap_dialog_draw_row(output, dialog, row++, NULL);  /* blank */

	swap_dialog_draw_row(output, dialog, row++,
			     "This may be from a previous session that");
	swap_dialog_draw_row(output, dialog, row++,
			     "crashed or was interrupted.");

	swap_dialog_draw_row(output, dialog, row++, NULL);  /* blank */

	/* Options */
	swap_dialog_draw_row(output, dialog, row++,
			     "[R] Recover - Open the swap file");
	swap_dialog_draw_row(output, dialog, row++,
			     "[D] Delete  - Delete swap file and open original");
	swap_dialog_draw_row(output, dialog, row++,
			     "[Q] Quit    - Exit without opening anything");

	/* Fill remaining rows */
	while (row < dialog->visible_rows) {
		swap_dialog_draw_row(output, dialog, row++, NULL);
	}

	dialog_draw_footer(output, dialog, "Press R, D, or Q");
}

/*
 * Show recovery prompt and handle user response.
 * Returns true if user chose to recover, false to ignore.
 */
bool autosave_prompt_recovery(const char *filename, const char *swap_path)
{
	/* Get swap file modification time */
	time_t swap_mtime = autosave_get_swap_mtime(swap_path);
	char time_str[64];
	autosave_format_time(swap_mtime, time_str, sizeof(time_str));

	/* Get swap file size */
	struct stat st;
	size_t swap_size = 0;
	if (stat(swap_path, &st) == 0) {
		swap_size = st.st_size;
	}

	/* Set up dialog dimensions */
	struct dialog_state dialog = {0};
	const int content_rows = 13;  /* Number of content lines we need */
	const int dialog_width = 60;
	const int dialog_height = content_rows + 2;  /* +2 for header and footer */

	dialog.panel_width = dialog_width;
	if (dialog.panel_width > (int)editor.screen_columns - 4) {
		dialog.panel_width = (int)editor.screen_columns - 4;
	}
	dialog.panel_height = dialog_height;
	if (dialog.panel_height > (int)editor.screen_rows - 2) {
		dialog.panel_height = (int)editor.screen_rows - 2;
	}
	dialog.panel_left = (editor.screen_columns - dialog.panel_width) / 2;
	dialog.panel_top = (editor.screen_rows - dialog.panel_height) / 2;
	dialog.visible_rows = dialog.panel_height - 2;

	/* Clear screen and draw dialog */
	struct output_buffer output = {0};
	/* Clear screen and position cursor at home */
	output_buffer_append_string(&output, ESCAPE_CLEAR_SCREEN_HOME);
	/* Hide cursor during dialog display */
	output_buffer_append_string(&output, ESCAPE_CURSOR_HIDE);

	swap_recovery_draw(&output, &dialog, filename, swap_path, time_str, swap_size);

	/* Reset text attributes */
	output_buffer_append_string(&output, ESCAPE_RESET);
	output_buffer_flush(&output);
	output_buffer_free(&output);

	/* Read response (we're still in raw mode) */
	while (1) {
		int c = input_read_key();

		if (c == 'r' || c == 'R') {
			return true;
		} else if (c == 'd' || c == 'D') {
			/* Delete the swap file */
			if (unlink(swap_path) == 0) {
				log_debug("Deleted swap file: %s", swap_path);
			}
			return false;
		} else if (c == 'q' || c == 'Q' || c == CONTROL_KEY('q')) {
			/* Exit the editor */
			terminal_disable_raw_mode();
			exit(0);
		}
		/* Ignore other keys */
	}
}
