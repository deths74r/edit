/*****************************************************************************
 * edit - A minimal terminal text editor
 * Phase 25A: Foundation Headers
 *****************************************************************************/

/*****************************************************************************
 * Includes and Configuration
 *****************************************************************************/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#define UTFLITE_IMPLEMENTATION
#include "../third_party/utflite/single_include/utflite.h"

#include "edit.h"

/* Dialog mouse mode flag - when true, mouse events go to dialog handler */
static bool dialog_mouse_mode = false;
static struct mouse_input dialog_last_mouse = {0};

static struct open_file_state open_file = {0};
static struct theme_picker_state theme_picker = {0};

/*****************************************************************************
 * Soft Wrap
 *****************************************************************************/


/*
 * Find the best column to break a line for wrapping.
 *
 * Searches backward from max_width to find an appropriate break point.
 * Uses the neighbor layer to prefer breaking at word boundaries.
 *
 * Parameters:
 *   line        - The line to find a wrap point in (must be warm)
 *   start_col   - Column where this segment starts
 *   max_width   - Maximum visual width for this segment
 *   mode        - WRAP_WORD (prefer word boundaries) or WRAP_CHAR (any column)
 *
 * Returns:
 *   Column index where the segment should END (exclusive).
 *   Next segment starts at this column.
 *   Returns line->cell_count if no wrap needed.
 */
static uint32_t line_find_wrap_point(struct line *line, uint32_t start_col,
                                      uint32_t max_width, enum wrap_mode mode);

/* Cycle through wrap modes: NONE -> WORD -> CHAR -> NONE */
static void editor_cycle_wrap_mode(void);

/* Cycle through wrap indicators. */
static void editor_cycle_wrap_indicator(void);

/* Get the UTF-8 string for a wrap indicator. */
static const char *wrap_indicator_string(enum wrap_indicator ind);


/* Compute wrap points for a line. Populates the wrap cache fields. */
static void line_compute_wrap_points(struct line *line, struct buffer *buffer,
                                     uint16_t text_width, enum wrap_mode mode);


/*****************************************************************************
 * Global State
 *****************************************************************************/

/* The global editor state instance. */
static struct editor_state editor;

/* Mouse click tracking for double/triple-click detection. */
static time_t last_click_time = 0;
static uint32_t last_click_row = 0;
static uint32_t last_click_col = 0;
static int click_count = 0;

/* Scroll velocity tracking for adaptive scroll speed. */
static struct timespec last_scroll_time = {0, 0};
static double scroll_velocity = 0.0;
static int last_scroll_direction = 0;  /* -1 = up, 1 = down, 0 = none */

/* Internal clipboard buffer (fallback when system clipboard unavailable). */
static char *internal_clipboard = NULL;
static size_t internal_clipboard_length = 0;

/* Clipboard tool detection (cached on first use). */
static enum clipboard_tool detected_clipboard_tool = CLIPBOARD_UNKNOWN;

/* Incremental search state. */
static struct search_state search = {0};

/*****************************************************************************
 * Background Search State
 *****************************************************************************/

static struct async_search_state async_search = {
	.current_match_index = -1
};

/*****************************************************************************
 * Background Replace All State
 *****************************************************************************/

static struct async_replace_state async_replace = {0};

/*****************************************************************************
 * Auto-Save and Crash Recovery
 *****************************************************************************/

static struct autosave_state autosave = {
	.enabled = true
};

/* Global snapshot pointer for worker thread access */
static struct buffer_snapshot *pending_snapshot = NULL;
static pthread_mutex_t snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Go-to-line mode state. */
static struct goto_state goto_line = {0};

/* Save As mode state. */
static struct save_as_state save_as = {0};

/* Quit prompt state - shown when quitting with unsaved changes. */
static struct quit_prompt_state quit_prompt = {0};

/* Forward declarations for functions used in input_read_key. */
static struct mouse_input input_parse_sgr_mouse(void);
static void editor_handle_mouse(struct mouse_input *mouse);

/* Forward declarations for search functions used in mouse handling. */
static bool search_find_next(bool wrap);
static bool search_find_previous(bool wrap);

/* Forward declaration for worker thread result processing. */
static void editor_set_status_message(const char *format, ...);

/*****************************************************************************
 * Worker Thread Infrastructure
 *****************************************************************************/

static struct worker_state worker = {0};

/*
 * Generate a unique task ID.
 */
static uint64_t task_generate_id(void)
{
	return atomic_fetch_add(&worker.next_task_id, 1);
}

/*
 * Submit a task to the worker queue.
 * Returns 0 on success, negative error code on failure.
 * The task is copied into the queue.
 */
static int __must_check task_queue_push(struct task *task)
{
	if (unlikely(!worker.initialized)) {
		log_warn("Worker not initialized, dropping task");
		return -EEDIT_THREAD;
	}

	pthread_mutex_lock(&worker.task_mutex);

	if (unlikely(worker.task_count >= TASK_QUEUE_SIZE)) {
		pthread_mutex_unlock(&worker.task_mutex);
		log_warn("Task queue full, dropping task type=%d", task->type);
		return -EEDIT_QUEUEFULL;
	}

	/* Assign task ID if not set */
	if (task->task_id == 0) {
		task->task_id = task_generate_id();
	}

	/* Initialize cancellation flag */
	atomic_store(&task->cancelled, false);

	/* Copy task into queue */
	worker.task_queue[worker.task_tail] = *task;
	worker.task_tail = (worker.task_tail + 1) % TASK_QUEUE_SIZE;
	worker.task_count++;

	log_debug("Task queued: type=%d id=%lu count=%u",
	          task->type, task->task_id, worker.task_count);

	/* Wake worker thread */
	pthread_cond_signal(&worker.task_cond);
	pthread_mutex_unlock(&worker.task_mutex);

	return 0;
}

/*
 * Pop a task from the queue (called by worker thread).
 * Blocks until a task is available or timeout.
 * Returns 0 on success with task copied to *out.
 * Returns -ETIMEDOUT if no task available within timeout.
 */
static int task_queue_pop(struct task *out, int timeout_ms)
{
	pthread_mutex_lock(&worker.task_mutex);

	/* Wait for task or shutdown */
	while (worker.task_count == 0 && !atomic_load(&worker.shutdown)) {
		if (timeout_ms > 0) {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += timeout_ms / 1000;
			ts.tv_nsec += (timeout_ms % 1000) * 1000000;
			if (ts.tv_nsec >= 1000000000) {
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000;
			}

			int err = pthread_cond_timedwait(&worker.task_cond, &worker.task_mutex, &ts);
			if (err == ETIMEDOUT) {
				pthread_mutex_unlock(&worker.task_mutex);
				return -ETIMEDOUT;
			}
		} else {
			pthread_cond_wait(&worker.task_cond, &worker.task_mutex);
		}
	}

	/* Check shutdown */
	if (atomic_load(&worker.shutdown) && worker.task_count == 0) {
		pthread_mutex_unlock(&worker.task_mutex);
		return -EEDIT_CANCELLED;
	}

	/* Pop task */
	*out = worker.task_queue[worker.task_head];
	worker.task_head = (worker.task_head + 1) % TASK_QUEUE_SIZE;
	worker.task_count--;

	pthread_mutex_unlock(&worker.task_mutex);

	log_debug("Task dequeued: type=%d id=%lu", out->type, out->task_id);
	return 0;
}

/*
 * Cancel a pending or running task by ID.
 * Returns true if task was found and cancelled.
 */
static bool task_cancel(uint64_t task_id)
{
	bool found = false;

	pthread_mutex_lock(&worker.task_mutex);

	/* Check queued tasks */
	for (uint32_t i = 0; i < worker.task_count; i++) {
		uint32_t idx = (worker.task_head + i) % TASK_QUEUE_SIZE;
		if (worker.task_queue[idx].task_id == task_id) {
			atomic_store(&worker.task_queue[idx].cancelled, true);
			found = true;
			log_debug("Cancelled queued task %lu", task_id);
			break;
		}
	}

	/* Check currently running task */
	if (!found && worker.current_task != NULL &&
	    worker.current_task->task_id == task_id) {
		atomic_store(&worker.current_task->cancelled, true);
		found = true;
		log_debug("Cancelled running task %lu", task_id);
	}

	pthread_mutex_unlock(&worker.task_mutex);

	return found;
}

/*
 * Cancel all pending tasks of a specific type.
 */
static void task_cancel_all_of_type(enum task_type type)
{
	pthread_mutex_lock(&worker.task_mutex);

	for (uint32_t i = 0; i < worker.task_count; i++) {
		uint32_t idx = (worker.task_head + i) % TASK_QUEUE_SIZE;
		if (worker.task_queue[idx].type == type) {
			atomic_store(&worker.task_queue[idx].cancelled, true);
		}
	}

	if (worker.current_task != NULL && worker.current_task->type == type) {
		atomic_store(&worker.current_task->cancelled, true);
	}

	pthread_mutex_unlock(&worker.task_mutex);

	log_debug("Cancelled all tasks of type %d", type);
}

/*
 * Push a result to the result queue (called by worker thread).
 * If queue is full, oldest result is dropped.
 */
static void result_queue_push(struct task_result *result)
{
	pthread_mutex_lock(&worker.result_mutex);

	if (worker.result_count >= RESULT_QUEUE_SIZE) {
		/* Drop oldest result */
		worker.result_head = (worker.result_head + 1) % RESULT_QUEUE_SIZE;
		worker.result_count--;
		log_warn("Result queue full, dropped oldest result");
	}

	worker.result_queue[worker.result_tail] = *result;
	worker.result_tail = (worker.result_tail + 1) % RESULT_QUEUE_SIZE;
	worker.result_count++;

	pthread_mutex_unlock(&worker.result_mutex);

	log_debug("Result posted: type=%d id=%lu error=%d",
	          result->type, result->task_id, result->error);
}

/*
 * Pop a result from the queue (called by main thread).
 * Non-blocking: returns false immediately if queue is empty.
 */
static bool result_queue_pop(struct task_result *out)
{
	pthread_mutex_lock(&worker.result_mutex);

	if (worker.result_count == 0) {
		pthread_mutex_unlock(&worker.result_mutex);
		return false;
	}

	*out = worker.result_queue[worker.result_head];
	worker.result_head = (worker.result_head + 1) % RESULT_QUEUE_SIZE;
	worker.result_count--;

	pthread_mutex_unlock(&worker.result_mutex);

	return true;
}

/*
 * Check if there are pending results without removing them.
 */
static bool result_queue_has_results(void)
{
	pthread_mutex_lock(&worker.result_mutex);
	bool has_results = (worker.result_count > 0);
	pthread_mutex_unlock(&worker.result_mutex);
	return has_results;
}

/*
 * Check if a task has been cancelled.
 * Call periodically during long-running operations.
 */
static inline bool task_is_cancelled(struct task *task)
{
	return atomic_load_explicit(&task->cancelled, memory_order_relaxed);
}

/* Forward declaration - defined later in Neighbor Layer section */
void neighbor_compute_line(struct line *line);

/*
 * Warm a line from the worker thread.
 * This is a thread-safe version that can be called concurrently with rendering.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int __must_check line_warm_from_worker(struct line *line, struct buffer *buffer)
{
	/* Verify preconditions */
	if (unlikely(line_get_temperature(line) != LINE_TEMPERATURE_COLD)) {
		return 0;  /* Already warm, nothing to do */
	}

	if (unlikely(buffer->mmap_base == NULL)) {
		return -EEDIT_CORRUPT;
	}

	/* Bounds check */
	if (unlikely(line->mmap_offset + line->mmap_length > buffer->mmap_size)) {
		log_err("Line mmap bounds error: offset=%zu len=%u size=%zu",
		        line->mmap_offset, line->mmap_length, buffer->mmap_size);
		return -EEDIT_BOUNDS;
	}

	/* Decode UTF-8 from mmap */
	const char *text = buffer->mmap_base + line->mmap_offset;
	size_t length = line->mmap_length;

	/* Allocate cells */
	uint32_t capacity = length > 0 ? length : 1;
	struct cell *cells = calloc(capacity, sizeof(struct cell));
	if (unlikely(cells == NULL)) {
		log_err("Failed to allocate %u cells", capacity);
		return -ENOMEM;
	}

	/* Decode UTF-8 to cells */
	uint32_t cell_count = 0;
	size_t byte_index = 0;

	while (byte_index < length) {
		/* Ensure capacity */
		if (cell_count >= capacity) {
			capacity *= 2;
			struct cell *new_cells = realloc(cells, capacity * sizeof(struct cell));
			if (unlikely(new_cells == NULL)) {
				free(cells);
				return -ENOMEM;
			}
			cells = new_cells;
		}

		/* Decode one codepoint */
		uint32_t codepoint;
		int bytes = utflite_decode(text + byte_index, length - byte_index, &codepoint);

		if (bytes <= 0 || byte_index + bytes > length) {
			/* Invalid UTF-8 - use replacement character */
			codepoint = 0xFFFD;
			bytes = 1;
		}

		cells[cell_count].codepoint = codepoint;
		cells[cell_count].syntax = SYNTAX_NORMAL;
		cells[cell_count].neighbor = 0;
		cells[cell_count].flags = 0;
		cells[cell_count].context = 0;
		cell_count++;
		byte_index += bytes;
	}

	/*
	 * Atomic publish: write cells pointer and count, then set temperature.
	 * The release semantics ensure all writes are visible before temperature change.
	 */
	line->cells = cells;
	line->cell_count = cell_count;
	line->cell_capacity = capacity;

	/* Initialize wrap cache as invalid */
	line->wrap_columns = NULL;
	line->wrap_segment_count = 0;
	line->wrap_cache_width = 0;

	/* Compute neighbor layer (character classes, token positions) */
	neighbor_compute_line(line);

	/* Publish as WARM - syntax highlighting still needed */
	line_set_temperature(line, LINE_TEMPERATURE_WARM);

	return 0;
}

/*
 * Worker task: warm a range of lines.
 * Decodes UTF-8 and computes neighbor layer for each cold line.
 */
static int worker_process_warm_lines(struct task *task, struct task_result *result)
{
	uint32_t start_row = task->warm.start_row;
	uint32_t end_row = task->warm.end_row;

	/* Validate range */
	if (end_row > editor.buffer.line_count) {
		end_row = editor.buffer.line_count;
	}
	if (start_row >= end_row) {
		result->warm.lines_warmed = 0;
		result->warm.lines_skipped = 0;
		return 0;
	}

	uint32_t warmed = 0;
	uint32_t skipped = 0;

	for (uint32_t row = start_row; row < end_row; row++) {
		/* Check cancellation every 100 lines for responsiveness */
		if ((row - start_row) % 100 == 0) {
			if (task_is_cancelled(task)) {
				log_debug("Warm task cancelled at row %u", row);
				result->warm.lines_warmed = warmed;
				result->warm.lines_skipped = skipped;
				return -EEDIT_CANCELLED;
			}
		}

		struct line *line = &editor.buffer.lines[row];

		/* Skip if not cold */
		if (line_get_temperature(line) != LINE_TEMPERATURE_COLD) {
			skipped++;
			continue;
		}

		/* Try to claim this line */
		if (!line_try_claim_warming(line)) {
			/* Another thread is warming this line */
			skipped++;
			continue;
		}

		/* Double-check temperature after claiming */
		if (line_get_temperature(line) != LINE_TEMPERATURE_COLD) {
			line_release_warming(line);
			skipped++;
			continue;
		}

		/* Warm the line */
		int err = line_warm_from_worker(line, &editor.buffer);

		line_release_warming(line);

		if (unlikely(err)) {
			if (err != -EEDIT_CANCELLED) {
				log_warn("Failed to warm line %u: %s", row, edit_strerror(err));
			}
			/* Continue with other lines */
		} else {
			warmed++;
		}
	}

	result->warm.lines_warmed = warmed;
	result->warm.lines_skipped = skipped;

	log_debug("Warmed %u lines, skipped %u (rows %u-%u)",
	          warmed, skipped, start_row, end_row);

	return 0;
}

/* Forward declarations for async search functions used by worker */
static int search_results_add_match(uint32_t row, uint32_t start_col, uint32_t end_col);
static void search_results_update_progress(uint32_t rows_searched, uint32_t total_rows);
static void search_results_mark_complete(void);

/*
 * Search a single line for pattern matches.
 * Warms the line if needed.
 * Returns number of matches found, negative on error.
 */
static int search_line_for_matches(struct line *line, struct buffer *buffer,
                                   uint32_t row, const char *pattern,
                                   bool use_regex, bool case_sensitive,
                                   bool whole_word, regex_t *compiled_regex)
{
	/* Ensure line is warm */
	int temp = line_get_temperature(line);
	if (temp == LINE_TEMPERATURE_COLD) {
		if (line_try_claim_warming(line)) {
			if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
				int err = line_warm_from_worker(line, buffer);
				if (err) {
					line_release_warming(line);
					return err;
				}
			}
			line_release_warming(line);
		} else {
			/* Wait briefly for other thread */
			for (int i = 0; i < 100 && line_get_temperature(line) == LINE_TEMPERATURE_COLD; i++) {
				/* Spin */
			}
			if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
				return 0;  /* Skip this line */
			}
		}
	}

	/* Convert line to string for searching */
	if (line->cells == NULL || line->cell_count == 0) {
		return 0;
	}

	/* Build UTF-8 string from cells */
	char *line_str = malloc(line->cell_count * 4 + 1);
	if (line_str == NULL) {
		return -ENOMEM;
	}

	size_t str_len = 0;
	uint32_t *byte_to_cell = malloc((line->cell_count * 4 + 1) * sizeof(uint32_t));
	if (byte_to_cell == NULL) {
		free(line_str);
		return -ENOMEM;
	}

	for (uint32_t col = 0; col < line->cell_count; col++) {
		char utf8[4];
		int bytes = utflite_encode(line->cells[col].codepoint, utf8);
		if (bytes > 0) {
			for (int b = 0; b < bytes; b++) {
				byte_to_cell[str_len + b] = col;
			}
			memcpy(line_str + str_len, utf8, bytes);
			str_len += bytes;
		}
	}
	line_str[str_len] = '\0';
	byte_to_cell[str_len] = line->cell_count;

	int matches_found = 0;

	if (use_regex && compiled_regex != NULL) {
		/* Regex search */
		regmatch_t match;
		size_t offset = 0;

		while (offset < str_len) {
			int regex_result = regexec(compiled_regex, line_str + offset, 1, &match,
			                     offset > 0 ? REG_NOTBOL : 0);
			if (regex_result != 0) {
				break;
			}

			size_t match_start = offset + match.rm_so;
			size_t match_end = offset + match.rm_eo;

			uint32_t start_col = byte_to_cell[match_start];
			uint32_t end_col = match_end < str_len ? byte_to_cell[match_end] : line->cell_count;

			/* Whole word check for regex */
			bool is_whole_word = true;
			if (whole_word) {
				if (match_start > 0 && isalnum((unsigned char)line_str[match_start - 1])) {
					is_whole_word = false;
				}
				if (match_end < str_len && isalnum((unsigned char)line_str[match_end])) {
					is_whole_word = false;
				}
			}

			if (is_whole_word) {
				if (search_results_add_match(row, start_col, end_col) < 0) {
					/* At capacity */
					break;
				}
				matches_found++;
			}

			/* Move past this match */
			offset = match_end;
			if (match.rm_so == match.rm_eo) {
				offset++;  /* Prevent infinite loop on empty match */
			}
		}
	} else {
		/* Plain text search */
		size_t pattern_len = strlen(pattern);
		if (pattern_len == 0) {
			free(line_str);
			free(byte_to_cell);
			return 0;
		}

		char *search_str = line_str;
		char *search_pattern = (char *)pattern;
		char *lower_str = NULL;
		char *lower_pattern = NULL;

		if (!case_sensitive) {
			/* Convert to lowercase for case-insensitive search */
			lower_str = malloc(str_len + 1);
			lower_pattern = malloc(pattern_len + 1);
			if (lower_str && lower_pattern) {
				for (size_t i = 0; i <= str_len; i++) {
					lower_str[i] = tolower((unsigned char)line_str[i]);
				}
				for (size_t i = 0; i <= pattern_len; i++) {
					lower_pattern[i] = tolower((unsigned char)pattern[i]);
				}
				search_str = lower_str;
				search_pattern = lower_pattern;
			}
		}

		char *pos = search_str;
		while ((pos = strstr(pos, search_pattern)) != NULL) {
			size_t byte_offset = pos - search_str;
			size_t match_end_byte = byte_offset + pattern_len;

			/* Whole word check */
			bool is_whole_word = true;
			if (whole_word) {
				if (byte_offset > 0 && isalnum((unsigned char)search_str[byte_offset - 1])) {
					is_whole_word = false;
				}
				if (match_end_byte < str_len && isalnum((unsigned char)search_str[match_end_byte])) {
					is_whole_word = false;
				}
			}

			if (is_whole_word) {
				uint32_t start_col = byte_to_cell[byte_offset];
				uint32_t end_col = match_end_byte < str_len ?
				                   byte_to_cell[match_end_byte] : line->cell_count;

				if (search_results_add_match(row, start_col, end_col) < 0) {
					break;  /* At capacity */
				}
				matches_found++;
			}

			pos++;
		}

		free(lower_str);
		free(lower_pattern);
	}

	free(line_str);
	free(byte_to_cell);

	return matches_found;
}

/*
 * Worker task: search buffer for pattern.
 */
static int worker_process_search(struct task *task, struct task_result *result)
{
	const char *pattern = task->search.pattern;
	uint32_t start_row = task->search.start_row;
	uint32_t end_row = task->search.end_row;
	bool use_regex = task->search.use_regex;
	bool case_sensitive = task->search.case_sensitive;
	bool whole_word = task->search.whole_word;

	/* Validate range */
	if (end_row == 0 || end_row > editor.buffer.line_count) {
		end_row = editor.buffer.line_count;
	}
	if (start_row >= end_row) {
		result->search.match_count = 0;
		result->search.rows_searched = 0;
		result->search.complete = true;
		return 0;
	}

	/* Store pattern in results for validation */
	pthread_mutex_lock(&async_search.results_mutex);
	strncpy(async_search.results.pattern, pattern, sizeof(async_search.results.pattern) - 1);
	async_search.results.pattern[sizeof(async_search.results.pattern) - 1] = '\0';
	async_search.results.use_regex = use_regex;
	async_search.results.case_sensitive = case_sensitive;
	async_search.results.whole_word = whole_word;
	async_search.results.total_rows = end_row - start_row;
	pthread_mutex_unlock(&async_search.results_mutex);

	/* Compile regex if needed */
	regex_t compiled_regex;
	bool regex_valid = false;

	if (use_regex) {
		int flags = REG_EXTENDED;
		if (!case_sensitive) {
			flags |= REG_ICASE;
		}
		if (regcomp(&compiled_regex, pattern, flags) == 0) {
			regex_valid = true;
		} else {
			log_warn("Invalid regex pattern: %s", pattern);
			result->search.match_count = 0;
			result->search.complete = true;
			return -EINVAL;
		}
	}

	uint32_t total_matches = 0;
	uint32_t rows_searched = 0;

	for (uint32_t row = start_row; row < end_row; row++) {
		/* Check cancellation every 100 rows */
		if (rows_searched % 100 == 0) {
			if (task_is_cancelled(task)) {
				log_debug("Search cancelled at row %u", row);
				result->search.match_count = total_matches;
				result->search.rows_searched = rows_searched;
				result->search.complete = false;
				if (regex_valid) {
					regfree(&compiled_regex);
				}
				return -EEDIT_CANCELLED;
			}

			/* Update progress */
			search_results_update_progress(rows_searched, end_row - start_row);
		}

		struct line *line = &editor.buffer.lines[row];

		int matches = search_line_for_matches(
			line, &editor.buffer, row, pattern,
			use_regex, case_sensitive, whole_word,
			regex_valid ? &compiled_regex : NULL
		);

		if (matches > 0) {
			total_matches += matches;
		}

		rows_searched++;
	}

	if (regex_valid) {
		regfree(&compiled_regex);
	}

	/* Mark complete */
	search_results_mark_complete();

	result->search.match_count = total_matches;
	result->search.rows_searched = rows_searched;
	result->search.complete = true;

	log_debug("Search complete: %u matches in %u rows", total_matches, rows_searched);

	return 0;
}

/* Forward declarations for async replace functions used by worker */
static int replace_results_add(uint32_t row, uint32_t start_col, uint32_t end_col,
                               char *replacement_text, uint32_t replacement_len);
static void replace_results_update_progress(uint32_t rows_searched, uint32_t total_rows);
static void replace_results_mark_search_complete(void);

/*
 * Expand replacement text with backreferences.
 * Returns newly allocated string, caller must free.
 */
static char *expand_replacement_text(const char *replacement_str, const char *line_str,
                                     regmatch_t *matches, int nmatch)
{
	/* Calculate expanded length */
	size_t expanded_len = 0;
	const char *p = replacement_str;

	while (*p) {
		if (*p == '\\' && p[1] >= '0' && p[1] <= '9') {
			int group = p[1] - '0';
			if (group < nmatch && matches[group].rm_so >= 0) {
				expanded_len += matches[group].rm_eo - matches[group].rm_so;
			}
			p += 2;
		} else if (*p == '\\' && p[1] == '\\') {
			expanded_len++;
			p += 2;
		} else {
			expanded_len++;
			p++;
		}
	}

	/* Allocate and build expanded string */
	char *result = malloc(expanded_len + 1);
	if (result == NULL) {
		return NULL;
	}

	char *out = result;
	p = replacement_str;

	while (*p) {
		if (*p == '\\' && p[1] >= '0' && p[1] <= '9') {
			int group = p[1] - '0';
			if (group < nmatch && matches[group].rm_so >= 0) {
				size_t len = matches[group].rm_eo - matches[group].rm_so;
				memcpy(out, line_str + matches[group].rm_so, len);
				out += len;
			}
			p += 2;
		} else if (*p == '\\' && p[1] == '\\') {
			*out++ = '\\';
			p += 2;
		} else {
			*out++ = *p++;
		}
	}
	*out = '\0';

	return result;
}

/*
 * Worker task: find all replacements.
 * Does NOT modify the buffer - just finds matches and expands replacements.
 */
static int worker_process_replace_all(struct task *task, struct task_result *result)
{
	const char *pattern = task->replace.pattern;
	const char *replacement_str = task->replace.replacement;
	bool use_regex = task->replace.use_regex;
	bool case_sensitive = task->replace.case_sensitive;
	bool whole_word = task->replace.whole_word;

	uint32_t total_rows = editor.buffer.line_count;

	/* Compile regex if needed */
	regex_t compiled_regex;
	bool regex_valid = false;

	if (use_regex) {
		int flags = REG_EXTENDED;
		if (!case_sensitive) {
			flags |= REG_ICASE;
		}
		if (regcomp(&compiled_regex, pattern, flags) == 0) {
			regex_valid = true;
		} else {
			result->replace.replacements = 0;
			result->replace.complete = true;
			return -EINVAL;
		}
	}

	uint32_t total_replacements = 0;
	uint32_t rows_searched = 0;

	for (uint32_t row = 0; row < total_rows; row++) {
		/* Check cancellation */
		if (rows_searched % 100 == 0) {
			if (task_is_cancelled(task)) {
				log_debug("Replace search cancelled at row %u", row);
				result->replace.replacements = total_replacements;
				result->replace.complete = false;
				if (regex_valid) {
					regfree(&compiled_regex);
				}
				return -EEDIT_CANCELLED;
			}
			replace_results_update_progress(rows_searched, total_rows);
		}

		struct line *line = &editor.buffer.lines[row];

		/* Ensure line is warm */
		int temp = line_get_temperature(line);
		if (temp == LINE_TEMPERATURE_COLD) {
			if (line_try_claim_warming(line)) {
				if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
					int __attribute__((unused)) err = line_warm_from_worker(line, &editor.buffer);
				}
				line_release_warming(line);
			}
		}

		if (line->cells == NULL || line->cell_count == 0) {
			rows_searched++;
			continue;
		}

		/* Build UTF-8 string from cells */
		char *line_str = malloc(line->cell_count * 4 + 1);
		if (line_str == NULL) {
			rows_searched++;
			continue;
		}

		size_t str_len = 0;
		uint32_t *byte_to_cell = malloc((line->cell_count * 4 + 1) * sizeof(uint32_t));
		if (byte_to_cell == NULL) {
			free(line_str);
			rows_searched++;
			continue;
		}

		for (uint32_t col = 0; col < line->cell_count; col++) {
			char utf8[4];
			int bytes = utflite_encode(line->cells[col].codepoint, utf8);
			if (bytes > 0) {
				for (int b = 0; b < bytes; b++) {
					byte_to_cell[str_len + b] = col;
				}
				memcpy(line_str + str_len, utf8, bytes);
				str_len += bytes;
			}
		}
		line_str[str_len] = '\0';
		byte_to_cell[str_len] = line->cell_count;

		/* Find matches in this line */
		if (use_regex && regex_valid) {
			regmatch_t matches[10];
			size_t offset = 0;

			while (offset < str_len) {
				int res = regexec(&compiled_regex, line_str + offset, 10, matches,
				                  offset > 0 ? REG_NOTBOL : 0);
				if (res != 0) {
					break;
				}

				/* Adjust match positions for offset */
				for (int mi = 0; mi < 10 && matches[mi].rm_so >= 0; mi++) {
					matches[mi].rm_so += offset;
					matches[mi].rm_eo += offset;
				}

				size_t match_start = matches[0].rm_so;
				size_t match_end = matches[0].rm_eo;

				uint32_t start_col = byte_to_cell[match_start];
				uint32_t end_col = match_end < str_len ?
				                   byte_to_cell[match_end] : line->cell_count;

				/* Expand replacement with backreferences */
				char *expanded = expand_replacement_text(replacement_str, line_str, matches, 10);
				if (expanded) {
					replace_results_add(row, start_col, end_col,
					                    expanded, strlen(expanded));
					total_replacements++;
				}

				offset = match_end;
				if (matches[0].rm_so == matches[0].rm_eo) {
					offset++;
				}
			}
		} else {
			/* Plain text search */
			size_t pattern_len = strlen(pattern);
			if (pattern_len > 0) {
				char *search_str = line_str;
				char *lower_str = NULL;
				char *lower_pattern = NULL;
				const char *search_pattern = pattern;

				if (!case_sensitive) {
					lower_str = malloc(str_len + 1);
					lower_pattern = malloc(pattern_len + 1);
					if (lower_str && lower_pattern) {
						for (size_t si = 0; si <= str_len; si++) {
							lower_str[si] = tolower((unsigned char)line_str[si]);
						}
						for (size_t si = 0; si <= pattern_len; si++) {
							lower_pattern[si] = tolower((unsigned char)pattern[si]);
						}
						search_str = lower_str;
						search_pattern = lower_pattern;
					}
				}

				char *pos = search_str;
				while ((pos = strstr(pos, search_pattern)) != NULL) {
					size_t byte_offset = pos - search_str;
					size_t match_end_byte = byte_offset + pattern_len;

					bool is_whole_word = true;
					if (whole_word) {
						if (byte_offset > 0 &&
						    isalnum((unsigned char)search_str[byte_offset - 1])) {
							is_whole_word = false;
						}
						if (match_end_byte < str_len &&
						    isalnum((unsigned char)search_str[match_end_byte])) {
							is_whole_word = false;
						}
					}

					if (is_whole_word) {
						uint32_t start_col = byte_to_cell[byte_offset];
						uint32_t end_col = match_end_byte < str_len ?
						                   byte_to_cell[match_end_byte] : line->cell_count;

						/* Plain text replacement (no backrefs) */
						char *repl_copy = strdup(replacement_str);
						if (repl_copy) {
							replace_results_add(row, start_col, end_col,
							                    repl_copy, strlen(repl_copy));
							total_replacements++;
						}
					}

					pos++;
				}

				free(lower_str);
				free(lower_pattern);
			}
		}

		free(line_str);
		free(byte_to_cell);
		rows_searched++;
	}

	if (regex_valid) {
		regfree(&compiled_regex);
	}

	replace_results_mark_search_complete();

	result->replace.replacements = total_replacements;
	result->replace.complete = true;

	log_debug("Replace search complete: %u replacements found", total_replacements);

	return 0;
}

/* Forward declaration for autosave task handler */
static int worker_process_autosave(struct task *task, struct task_result *result);

/*
 * Worker thread main function.
 */
static void *worker_thread_main(void *arg)
{
	(void)arg;
	log_info("Worker thread started");

	while (1) {
		struct task task;
		int err = task_queue_pop(&task, 100);  /* 100ms timeout */

		if (err == -ETIMEDOUT) {
			/* No task, check shutdown and loop */
			if (atomic_load(&worker.shutdown)) {
				break;
			}
			continue;
		}

		if (err == -EEDIT_CANCELLED || atomic_load(&worker.shutdown)) {
			log_info("Worker received shutdown signal");
			break;
		}

		if (unlikely(err)) {
			log_err("task_queue_pop error: %s", edit_strerror(err));
			continue;
		}

		/* Handle shutdown task */
		if (task.type == TASK_SHUTDOWN) {
			log_info("Worker received TASK_SHUTDOWN");
			break;
		}

		/* Skip if already cancelled */
		if (task_is_cancelled(&task)) {
			log_debug("Skipping cancelled task %lu", task.task_id);

			struct task_result result = {
				.task_id = task.task_id,
				.type = task.type,
				.error = -EEDIT_CANCELLED
			};
			result_queue_push(&result);
			continue;
		}

		/* Track current task for cancellation */
		pthread_mutex_lock(&worker.task_mutex);
		worker.current_task = &task;
		pthread_mutex_unlock(&worker.task_mutex);

		/* Process the task */
		struct task_result result = {
			.task_id = task.task_id,
			.type = task.type,
			.error = 0
		};

		switch (task.type) {
		case TASK_WARM_LINES:
			result.error = worker_process_warm_lines(&task, &result);
			break;

		case TASK_SEARCH:
			result.error = worker_process_search(&task, &result);
			break;

		case TASK_REPLACE_ALL:
			result.error = worker_process_replace_all(&task, &result);
			break;

		case TASK_AUTOSAVE:
			result.error = worker_process_autosave(&task, &result);
			break;

		default:
			WARN(1, "Unknown task type: %d", task.type);
			result.error = -EINVAL;
			break;
		}

		/* Clear current task */
		pthread_mutex_lock(&worker.task_mutex);
		worker.current_task = NULL;
		pthread_mutex_unlock(&worker.task_mutex);

		/* Post result */
		result_queue_push(&result);
	}

	log_info("Worker thread exiting");
	return NULL;
}

/*
 * Initialize the worker thread and queues.
 * Returns 0 on success, negative error code on failure.
 */
static int __must_check worker_init(void)
{
	if (worker.initialized) {
		log_warn("Worker already initialized");
		return 0;
	}

	log_info("Initializing worker thread");

	/* Allocate task queue */
	worker.task_queue = calloc(TASK_QUEUE_SIZE, sizeof(struct task));
	if (unlikely(worker.task_queue == NULL)) {
		log_err("Failed to allocate task queue");
		return -ENOMEM;
	}

	/* Allocate result queue */
	worker.result_queue = calloc(RESULT_QUEUE_SIZE, sizeof(struct task_result));
	if (unlikely(worker.result_queue == NULL)) {
		log_err("Failed to allocate result queue");
		free(worker.task_queue);
		worker.task_queue = NULL;
		return -ENOMEM;
	}

	/* Initialize mutexes */
	if (unlikely(pthread_mutex_init(&worker.task_mutex, NULL) != 0)) {
		log_err("Failed to initialize task mutex");
		free(worker.task_queue);
		free(worker.result_queue);
		return -EEDIT_MUTEX;
	}

	if (unlikely(pthread_mutex_init(&worker.result_mutex, NULL) != 0)) {
		log_err("Failed to initialize result mutex");
		pthread_mutex_destroy(&worker.task_mutex);
		free(worker.task_queue);
		free(worker.result_queue);
		return -EEDIT_MUTEX;
	}

	/* Initialize condition variable */
	if (unlikely(pthread_cond_init(&worker.task_cond, NULL) != 0)) {
		log_err("Failed to initialize condition variable");
		pthread_mutex_destroy(&worker.task_mutex);
		pthread_mutex_destroy(&worker.result_mutex);
		free(worker.task_queue);
		free(worker.result_queue);
		return -EEDIT_MUTEX;
	}

	/* Initialize state */
	worker.task_head = 0;
	worker.task_tail = 0;
	worker.task_count = 0;
	worker.result_head = 0;
	worker.result_tail = 0;
	worker.result_count = 0;
	worker.current_task = NULL;
	atomic_store(&worker.shutdown, false);
	atomic_store(&worker.next_task_id, 1);

	/* Create worker thread */
	if (unlikely(pthread_create(&worker.thread, NULL, worker_thread_main, NULL) != 0)) {
		log_err("Failed to create worker thread");
		pthread_cond_destroy(&worker.task_cond);
		pthread_mutex_destroy(&worker.task_mutex);
		pthread_mutex_destroy(&worker.result_mutex);
		free(worker.task_queue);
		free(worker.result_queue);
		return -EEDIT_THREAD;
	}

	worker.initialized = true;
	log_info("Worker thread initialized successfully");
	return 0;
}

/*
 * Shutdown the worker thread and free resources.
 */
static void worker_shutdown(void)
{
	if (!worker.initialized) {
		return;
	}

	log_info("Shutting down worker thread");

	/* Signal shutdown */
	atomic_store(&worker.shutdown, true);

	/* Send shutdown task to wake worker if waiting */
	struct task shutdown_task = {.type = TASK_SHUTDOWN};

	pthread_mutex_lock(&worker.task_mutex);
	if (worker.task_count < TASK_QUEUE_SIZE) {
		worker.task_queue[worker.task_tail] = shutdown_task;
		worker.task_tail = (worker.task_tail + 1) % TASK_QUEUE_SIZE;
		worker.task_count++;
	}
	pthread_cond_signal(&worker.task_cond);
	pthread_mutex_unlock(&worker.task_mutex);

	/* Wait for thread to exit */
	int err = pthread_join(worker.thread, NULL);
	if (unlikely(err != 0)) {
		log_err("pthread_join failed: %d", err);
	}

	/* Cleanup */
	pthread_cond_destroy(&worker.task_cond);
	pthread_mutex_destroy(&worker.task_mutex);
	pthread_mutex_destroy(&worker.result_mutex);
	free(worker.task_queue);
	free(worker.result_queue);

	worker.task_queue = NULL;
	worker.result_queue = NULL;
	worker.initialized = false;

	log_info("Worker shutdown complete");
}

/*
 * State for tracking background warming tasks.
 */
struct warming_state {
	uint64_t current_task_id;       /* ID of active warming task */
	uint32_t last_viewport_start;   /* Last viewport start row */
	uint32_t last_viewport_end;     /* Last viewport end row */
	bool task_pending;              /* Is there an active warming task? */
};

static struct warming_state warming = {0};

/* Forward declarations */
static void editor_request_background_warming(void);
static uint32_t async_search_get_progress(bool *complete, uint32_t *rows_searched, uint32_t *total_rows);
static bool async_search_next_match(void);
static void async_replace_apply_pending(void);

/*
 * Process any pending results from the worker thread.
 * Called periodically from the main loop (e.g., after input_read_key).
 */
static void worker_process_results(void)
{
	struct task_result result;
	int processed = 0;

	while (result_queue_pop(&result)) {
		processed++;

		if (result.error && result.error != -EEDIT_CANCELLED) {
			log_warn("Task %lu (type %d) failed: %s",
			         result.task_id, result.type, edit_strerror(result.error));
		}

		switch (result.type) {
		case TASK_WARM_LINES:
			if (result.task_id == warming.current_task_id) {
				warming.task_pending = false;
			}

			if (result.error == -EEDIT_CANCELLED) {
				log_debug("Warming cancelled: %u lines warmed before cancel",
				          result.warm.lines_warmed);
			} else if (result.error) {
				log_warn("Warming failed: %s", edit_strerror(result.error));
			} else {
				log_debug("Warming complete: %u warmed, %u skipped",
				          result.warm.lines_warmed, result.warm.lines_skipped);
			}

			/* Request more warming if needed (new cold lines may be in view) */
			if (result.warm.lines_warmed > 0) {
				editor_request_background_warming();
			}
			break;

		case TASK_SEARCH:
			if (result.task_id == async_search.task_id) {
				async_search.active = false;
			}

			if (result.error == -EEDIT_CANCELLED) {
				/* Don't update status on cancel - likely starting new search */
			} else if (result.error) {
				editor_set_status_message("Search error: %s",
				                          edit_strerror(result.error));
			} else {
				bool complete;
				uint32_t count = async_search_get_progress(&complete, NULL, NULL);
				if (complete) {
					if (count == 0) {
						editor_set_status_message("No matches found");
						search.has_match = false;
					} else {
						editor_set_status_message("Found %u match%s",
						                          count, count == 1 ? "" : "es");
						search.has_match = true;
						/* Auto-navigate to first match if not already at one */
						if (async_search.current_match_index < 0) {
							async_search_next_match();
						}
					}
				}
			}
			break;

		case TASK_REPLACE_ALL:
			if (result.task_id == async_replace.task_id) {
				if (result.error == -EEDIT_CANCELLED) {
					async_replace.active = false;
					editor_set_status_message("Replace cancelled");
				} else if (result.error) {
					async_replace.active = false;
					editor_set_status_message("Replace error: %s",
					                          edit_strerror(result.error));
				} else {
					/* Search phase complete - apply replacements */
					editor_set_status_message("Found %u matches, applying...",
					                          result.replace.replacements);
					async_replace_apply_pending();
				}
			}
			break;

		case TASK_AUTOSAVE:
			if (result.task_id == autosave.task_id) {
				autosave.save_pending = false;

				if (result.error == -EEDIT_CANCELLED) {
					log_debug("Autosave cancelled");
				} else if (result.error) {
					log_warn("Autosave failed: %s", edit_strerror(result.error));
				} else {
					autosave.swap_exists = true;
					log_debug("Autosave successful: %zu bytes",
					          result.autosave.bytes_written);
				}
			}
			break;

		default:
			WARN_ON_ONCE(1);
			break;
		}

		/* Limit processing per call to keep UI responsive */
		if (processed >= 10) {
			break;
		}
	}
}

/*
 * Request background warming of lines around the viewport.
 * Call this after scroll or when viewport changes.
 */
static void editor_request_background_warming(void)
{
	if (!worker.initialized) {
		return;
	}

	/* Calculate current viewport */
	uint32_t viewport_start = editor.row_offset;
	uint32_t viewport_end = editor.row_offset + editor.screen_rows;
	if (viewport_end > editor.buffer.line_count) {
		viewport_end = editor.buffer.line_count;
	}

	/* Check if viewport changed significantly */
	bool viewport_changed = (viewport_start != warming.last_viewport_start ||
	                         viewport_end != warming.last_viewport_end);

	if (!viewport_changed && warming.task_pending) {
		return;  /* Already have a task running for this viewport */
	}

	/* Cancel previous warming task if viewport changed */
	if (viewport_changed && warming.task_pending) {
		task_cancel(warming.current_task_id);
		warming.task_pending = false;
	}

	/* Calculate warming range: viewport + lookahead */
	uint32_t lookahead = editor.screen_rows * 2;  /* 2 screens ahead/behind */

	uint32_t warm_start = (viewport_start > lookahead) ?
	                      (viewport_start - lookahead) : 0;
	uint32_t warm_end = viewport_end + lookahead;
	if (warm_end > editor.buffer.line_count) {
		warm_end = editor.buffer.line_count;
	}

	/* Check if there are cold lines in range */
	bool has_cold_lines = false;
	for (uint32_t row = warm_start; row < warm_end && !has_cold_lines; row++) {
		if (line_get_temperature(&editor.buffer.lines[row]) == LINE_TEMPERATURE_COLD) {
			has_cold_lines = true;
		}
	}

	if (!has_cold_lines) {
		return;  /* Nothing to warm */
	}

	/* Submit warming task */
	struct task task = {
		.type = TASK_WARM_LINES,
		.task_id = task_generate_id(),
		.warm = {
			.start_row = warm_start,
			.end_row = warm_end,
			.priority = 0
		}
	};

	int err = task_queue_push(&task);
	if (err == 0) {
		warming.current_task_id = task.task_id;
		warming.task_pending = true;
		warming.last_viewport_start = viewport_start;
		warming.last_viewport_end = viewport_end;

		log_debug("Queued warming task %lu for rows %u-%u",
		          task.task_id, warm_start, warm_end);
	}
}

/*****************************************************************************
 * Background Search Management
 *****************************************************************************/

/*
 * Initialize the async search system.
 * Call once at startup after worker_init().
 */
static int __must_check async_search_init(void)
{
	if (pthread_mutex_init(&async_search.results_mutex, NULL) != 0) {
		log_err("Failed to initialize search results mutex");
		return -EEDIT_MUTEX;
	}

	async_search.mutex_initialized = true;
	async_search.results.matches = NULL;
	async_search.results.match_count = 0;
	async_search.results.match_capacity = 0;
	async_search.results.complete = false;
	async_search.active = false;
	async_search.current_match_index = -1;

	return 0;
}

/*
 * Cleanup async search resources.
 * Call before worker_shutdown().
 */
static void async_search_cleanup(void)
{
	if (!async_search.mutex_initialized) {
		return;
	}

	pthread_mutex_lock(&async_search.results_mutex);
	free(async_search.results.matches);
	async_search.results.matches = NULL;
	async_search.results.match_count = 0;
	pthread_mutex_unlock(&async_search.results_mutex);

	pthread_mutex_destroy(&async_search.results_mutex);
	async_search.mutex_initialized = false;
}

/*
 * Clear search results (call with mutex held).
 */
static void search_results_clear_locked(void)
{
	async_search.results.match_count = 0;
	async_search.results.rows_searched = 0;
	async_search.results.complete = false;
	async_search.current_match_index = -1;
}

/*
 * Add a match to results (call from worker thread).
 * Returns 0 on success, -ENOMEM if at capacity.
 */
static int search_results_add_match(uint32_t row, uint32_t start_col, uint32_t end_col)
{
	pthread_mutex_lock(&async_search.results_mutex);

	/* Check capacity */
	if (async_search.results.match_count >= MAX_SEARCH_MATCHES) {
		pthread_mutex_unlock(&async_search.results_mutex);
		return -ENOMEM;  /* At limit, stop adding */
	}

	/* Grow array if needed */
	if (async_search.results.match_count >= async_search.results.match_capacity) {
		uint32_t new_capacity = async_search.results.match_capacity == 0 ?
		                        256 : async_search.results.match_capacity * 2;
		if (new_capacity > MAX_SEARCH_MATCHES) {
			new_capacity = MAX_SEARCH_MATCHES;
		}

		struct search_match *new_matches = realloc(
			async_search.results.matches,
			new_capacity * sizeof(struct search_match)
		);
		if (new_matches == NULL) {
			pthread_mutex_unlock(&async_search.results_mutex);
			return -ENOMEM;
		}

		async_search.results.matches = new_matches;
		async_search.results.match_capacity = new_capacity;
	}

	/* Add match */
	struct search_match *match = &async_search.results.matches[async_search.results.match_count++];
	match->row = row;
	match->start_col = start_col;
	match->end_col = end_col;

	pthread_mutex_unlock(&async_search.results_mutex);
	return 0;
}

/*
 * Update search progress (call from worker thread).
 */
static void search_results_update_progress(uint32_t rows_searched, uint32_t total_rows)
{
	pthread_mutex_lock(&async_search.results_mutex);
	async_search.results.rows_searched = rows_searched;
	async_search.results.total_rows = total_rows;
	pthread_mutex_unlock(&async_search.results_mutex);
}

/*
 * Mark search as complete (call from worker thread).
 */
static void search_results_mark_complete(void)
{
	pthread_mutex_lock(&async_search.results_mutex);
	async_search.results.complete = true;
	pthread_mutex_unlock(&async_search.results_mutex);
}

/*
 * Check if async search should be used for current buffer.
 */
static bool should_use_async_search(void)
{
	return worker.initialized &&
	       async_search.mutex_initialized &&
	       editor.buffer.line_count > ASYNC_SEARCH_THRESHOLD;
}

/*
 * Check if a cell is within a search match.
 * Returns: 0 = no match, 1 = match, 2 = current match
 */
static int async_search_get_match_state(uint32_t row, uint32_t col)
{
	if (!async_search.mutex_initialized) {
		return 0;
	}

	if (!async_search.active && async_search.results.match_count == 0) {
		return 0;
	}

	pthread_mutex_lock(&async_search.results_mutex);

	int result = 0;
	for (uint32_t i = 0; i < async_search.results.match_count; i++) {
		struct search_match *match = &async_search.results.matches[i];
		if (match->row == row && col >= match->start_col && col < match->end_col) {
			result = (i == (uint32_t)async_search.current_match_index) ? 2 : 1;
			break;
		}
	}

	pthread_mutex_unlock(&async_search.results_mutex);
	return result;
}

/*
 * Get current search progress for status display.
 * Returns match count and sets *complete to indicate if search finished.
 */
static uint32_t async_search_get_progress(bool *complete, uint32_t *rows_searched,
                                          uint32_t *total_rows)
{
	if (!async_search.mutex_initialized) {
		if (complete) *complete = true;
		if (rows_searched) *rows_searched = 0;
		if (total_rows) *total_rows = 0;
		return 0;
	}

	pthread_mutex_lock(&async_search.results_mutex);
	uint32_t count = async_search.results.match_count;
	if (complete) *complete = async_search.results.complete;
	if (rows_searched) *rows_searched = async_search.results.rows_searched;
	if (total_rows) *total_rows = async_search.results.total_rows;
	pthread_mutex_unlock(&async_search.results_mutex);
	return count;
}

/* Forward declarations for navigation */
static void editor_scroll(void);

/*
 * Navigate to the next search match.
 * Returns true if moved to a match.
 */
static bool async_search_next_match(void)
{
	if (!async_search.mutex_initialized) {
		return false;
	}

	pthread_mutex_lock(&async_search.results_mutex);

	if (async_search.results.match_count == 0) {
		pthread_mutex_unlock(&async_search.results_mutex);
		return false;
	}

	/* Find next match after current cursor position */
	uint32_t cursor_row = editor.cursor_row;
	uint32_t cursor_col = editor.cursor_column;
	int32_t next_index = -1;

	/* First, try to find a match after current position */
	for (uint32_t i = 0; i < async_search.results.match_count; i++) {
		struct search_match *match = &async_search.results.matches[i];
		if (match->row > cursor_row ||
		    (match->row == cursor_row && match->start_col > cursor_col)) {
			next_index = i;
			break;
		}
	}

	/* Wrap around to first match if none found after cursor */
	if (next_index < 0) {
		next_index = 0;
	}

	/* Navigate to match */
	struct search_match *match = &async_search.results.matches[next_index];
	async_search.current_match_index = next_index;

	uint32_t row = match->row;
	uint32_t col = match->start_col;

	pthread_mutex_unlock(&async_search.results_mutex);

	/* Update cursor position */
	editor.cursor_row = row;
	editor.cursor_column = col;
	editor_scroll();

	return true;
}

/*
 * Navigate to the previous search match.
 * Returns true if moved to a match.
 */
static bool async_search_prev_match(void)
{
	if (!async_search.mutex_initialized) {
		return false;
	}

	pthread_mutex_lock(&async_search.results_mutex);

	if (async_search.results.match_count == 0) {
		pthread_mutex_unlock(&async_search.results_mutex);
		return false;
	}

	/* Find previous match before current cursor position */
	uint32_t cursor_row = editor.cursor_row;
	uint32_t cursor_col = editor.cursor_column;
	int32_t prev_index = -1;

	/* Search backwards */
	for (int32_t i = async_search.results.match_count - 1; i >= 0; i--) {
		struct search_match *match = &async_search.results.matches[i];
		if (match->row < cursor_row ||
		    (match->row == cursor_row && match->start_col < cursor_col)) {
			prev_index = i;
			break;
		}
	}

	/* Wrap around to last match if none found before cursor */
	if (prev_index < 0) {
		prev_index = async_search.results.match_count - 1;
	}

	/* Navigate to match */
	struct search_match *match = &async_search.results.matches[prev_index];
	async_search.current_match_index = prev_index;

	uint32_t row = match->row;
	uint32_t col = match->start_col;

	pthread_mutex_unlock(&async_search.results_mutex);

	/* Update cursor position */
	editor.cursor_row = row;
	editor.cursor_column = col;
	editor_scroll();

	return true;
}

/*
 * Cancel current async search.
 */
static void async_search_cancel(void)
{
	if (async_search.active) {
		task_cancel(async_search.task_id);
		async_search.active = false;
		log_debug("Cancelled async search");
	}
}

/*
 * Start an async search.
 * Cancels any existing search first.
 */
static void async_search_start(const char *pattern, bool use_regex,
                               bool case_sensitive, bool whole_word)
{
	if (!worker.initialized || !async_search.mutex_initialized) {
		return;  /* Fall back to sync search */
	}

	/* Cancel existing search */
	if (async_search.active) {
		task_cancel(async_search.task_id);
		async_search.active = false;
	}

	/* Clear previous results */
	pthread_mutex_lock(&async_search.results_mutex);
	search_results_clear_locked();
	pthread_mutex_unlock(&async_search.results_mutex);

	/* Submit new search task */
	struct task task = {
		.type = TASK_SEARCH,
		.task_id = task_generate_id(),
		.search = {
			.start_row = 0,
			.end_row = 0,  /* 0 = entire buffer */
			.use_regex = use_regex,
			.case_sensitive = case_sensitive,
			.whole_word = whole_word
		}
	};
	strncpy(task.search.pattern, pattern, sizeof(task.search.pattern) - 1);
	task.search.pattern[sizeof(task.search.pattern) - 1] = '\0';

	int err = task_queue_push(&task);
	if (err == 0) {
		async_search.task_id = task.task_id;
		async_search.active = true;
		log_debug("Started async search for '%s' (task %lu)", pattern, task.task_id);
	} else {
		log_warn("Failed to start async search: %s", edit_strerror(err));
	}
}

/*****************************************************************************
 * Background Replace All Management
 *****************************************************************************/

/*
 * Initialize async replace system.
 */
static int __must_check async_replace_init(void)
{
	if (pthread_mutex_init(&async_replace.results_mutex, NULL) != 0) {
		log_err("Failed to initialize replace results mutex");
		return -EEDIT_MUTEX;
	}

	async_replace.mutex_initialized = true;
	async_replace.results.replacements = NULL;
	async_replace.results.count = 0;
	async_replace.results.capacity = 0;
	async_replace.active = false;

	return 0;
}

/*
 * Cleanup async replace resources.
 */
static void async_replace_cleanup(void)
{
	if (!async_replace.mutex_initialized) {
		return;
	}

	pthread_mutex_lock(&async_replace.results_mutex);

	/* Free all replacement texts */
	for (uint32_t i = 0; i < async_replace.results.count; i++) {
		free(async_replace.results.replacements[i].replacement_text);
	}
	free(async_replace.results.replacements);
	async_replace.results.replacements = NULL;
	async_replace.results.count = 0;

	pthread_mutex_unlock(&async_replace.results_mutex);
	pthread_mutex_destroy(&async_replace.results_mutex);
	async_replace.mutex_initialized = false;
}

/*
 * Clear replace results (call with mutex held or when single-threaded).
 */
static void replace_results_clear_locked(void)
{
	for (uint32_t i = 0; i < async_replace.results.count; i++) {
		free(async_replace.results.replacements[i].replacement_text);
	}
	async_replace.results.count = 0;
	async_replace.results.rows_searched = 0;
	async_replace.results.search_complete = false;
	async_replace.results.applied_count = 0;
	async_replace.results.apply_complete = false;
}

/*
 * Add a replacement to results (call from worker thread).
 * Takes ownership of replacement_text.
 */
static int replace_results_add(uint32_t row, uint32_t start_col, uint32_t end_col,
                               char *replacement_text, uint32_t replacement_len)
{
	pthread_mutex_lock(&async_replace.results_mutex);

	/* Grow array if needed */
	if (async_replace.results.count >= async_replace.results.capacity) {
		uint32_t new_capacity = async_replace.results.capacity == 0 ?
		                        256 : async_replace.results.capacity * 2;

		struct replacement *new_array = realloc(
			async_replace.results.replacements,
			new_capacity * sizeof(struct replacement)
		);
		if (new_array == NULL) {
			pthread_mutex_unlock(&async_replace.results_mutex);
			free(replacement_text);
			return -ENOMEM;
		}

		async_replace.results.replacements = new_array;
		async_replace.results.capacity = new_capacity;
	}

	/* Add replacement */
	struct replacement *r = &async_replace.results.replacements[async_replace.results.count++];
	r->row = row;
	r->start_col = start_col;
	r->end_col = end_col;
	r->replacement_text = replacement_text;
	r->replacement_len = replacement_len;

	pthread_mutex_unlock(&async_replace.results_mutex);
	return 0;
}

/*
 * Update replace search progress.
 */
static void replace_results_update_progress(uint32_t rows_searched, uint32_t total_rows)
{
	pthread_mutex_lock(&async_replace.results_mutex);
	async_replace.results.rows_searched = rows_searched;
	async_replace.results.total_rows = total_rows;
	pthread_mutex_unlock(&async_replace.results_mutex);
}

/*
 * Mark search phase as complete.
 */
static void replace_results_mark_search_complete(void)
{
	pthread_mutex_lock(&async_replace.results_mutex);
	async_replace.results.search_complete = true;
	pthread_mutex_unlock(&async_replace.results_mutex);
}

/*
 * Check if async replace should be used.
 */
static bool should_use_async_replace(void)
{
	return worker.initialized &&
	       async_replace.mutex_initialized &&
	       editor.buffer.line_count > ASYNC_SEARCH_THRESHOLD;
}

/*
 * Get replace operation progress.
 */
static uint32_t async_replace_get_progress(bool *search_complete, bool *apply_complete,
                                           uint32_t *rows_searched, uint32_t *total_rows)
{
	if (!async_replace.mutex_initialized) {
		if (search_complete) *search_complete = true;
		if (apply_complete) *apply_complete = true;
		if (rows_searched) *rows_searched = 0;
		if (total_rows) *total_rows = 0;
		return 0;
	}

	pthread_mutex_lock(&async_replace.results_mutex);
	uint32_t count = async_replace.results.count;
	if (search_complete) *search_complete = async_replace.results.search_complete;
	if (apply_complete) *apply_complete = async_replace.results.apply_complete;
	if (rows_searched) *rows_searched = async_replace.results.rows_searched;
	if (total_rows) *total_rows = async_replace.results.total_rows;
	pthread_mutex_unlock(&async_replace.results_mutex);
	return count;
}

/*
 * Cancel async replace.
 */
static void async_replace_cancel(void)
{
	if (async_replace.active) {
		task_cancel(async_replace.task_id);
		async_replace.active = false;
		log_debug("Cancelled async replace");
	}
}

/* Forward declarations for apply function */
static void buffer_delete_range_no_record(struct buffer *buffer,
                                          uint32_t start_row, uint32_t start_col,
                                          uint32_t end_row, uint32_t end_col);
static void undo_begin_group(struct buffer *buffer);
static void undo_end_group(struct buffer *buffer);

/*
 * Apply pending replacements from background search.
 * Called from main thread after search completes.
 * Processes replacements in reverse order to preserve positions.
 */
static void async_replace_apply_pending(void)
{
	pthread_mutex_lock(&async_replace.results_mutex);

	if (!async_replace.results.search_complete ||
	    async_replace.results.apply_complete ||
	    async_replace.results.count == 0) {
		pthread_mutex_unlock(&async_replace.results_mutex);
		return;
	}

	uint32_t count = async_replace.results.count;

	/* Copy replacements to local array so we can release mutex */
	struct replacement *local_replacements = malloc(count * sizeof(struct replacement));
	if (local_replacements == NULL) {
		pthread_mutex_unlock(&async_replace.results_mutex);
		editor_set_status_message("Out of memory applying replacements");
		return;
	}
	memcpy(local_replacements, async_replace.results.replacements,
	       count * sizeof(struct replacement));

	/* Clear the results (we've taken ownership of replacement_text pointers) */
	async_replace.results.count = 0;
	async_replace.results.apply_complete = true;

	pthread_mutex_unlock(&async_replace.results_mutex);

	/* Start undo group for all replacements */
	undo_begin_group(&editor.buffer);

	/* Apply in reverse order to preserve positions */
	uint32_t applied = 0;
	for (int32_t i = count - 1; i >= 0; i--) {
		struct replacement *r = &local_replacements[i];

		/* Validate row is still in range */
		if (r->row >= editor.buffer.line_count) {
			free(r->replacement_text);
			continue;
		}

		struct line *line = &editor.buffer.lines[r->row];

		/* Ensure line is warm */
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			line_warm(line, &editor.buffer);
		}

		/* Validate columns are still in range */
		if (r->start_col > line->cell_count || r->end_col > line->cell_count) {
			free(r->replacement_text);
			continue;
		}

		/* Delete the matched text */
		if (r->end_col > r->start_col) {
			buffer_delete_range_no_record(&editor.buffer, r->row,
			                              r->start_col, r->row, r->end_col);
		}

		/* Insert replacement text */
		if (r->replacement_len > 0) {
			/* Convert replacement text to codepoints and insert */
			size_t byte_idx = 0;
			uint32_t col = r->start_col;
			while (byte_idx < r->replacement_len) {
				uint32_t codepoint;
				int bytes = utflite_decode(r->replacement_text + byte_idx,
				                           r->replacement_len - byte_idx,
				                           &codepoint);
				if (bytes <= 0) {
					codepoint = 0xFFFD;
					bytes = 1;
				}
				buffer_insert_cell_at_column(&editor.buffer, r->row, col, codepoint);
				col++;
				byte_idx += bytes;
			}
		}

		applied++;
		free(r->replacement_text);

		/* Update status periodically */
		if (applied % 100 == 0) {
			editor_set_status_message("Applying... %u/%u", applied, count);
		}
	}

	/* End undo group */
	undo_end_group(&editor.buffer);

	free(local_replacements);

	editor_set_status_message("Replaced %u occurrence%s", applied, applied == 1 ? "" : "s");

	/* Mark buffer as modified */
	if (applied > 0) {
		editor.buffer.is_modified = true;
	}

	async_replace.active = false;
}

/*
 * Start async replace-all operation.
 */
static void async_replace_start(const char *pattern, const char *replacement_text,
                                bool use_regex, bool case_sensitive, bool whole_word)
{
	if (!worker.initialized || !async_replace.mutex_initialized) {
		return;
	}

	/* Cancel any existing replace operation */
	if (async_replace.active) {
		task_cancel(async_replace.task_id);
	}

	/* Clear previous results */
	pthread_mutex_lock(&async_replace.results_mutex);
	replace_results_clear_locked();
	pthread_mutex_unlock(&async_replace.results_mutex);

	/* Store parameters */
	strncpy(async_replace.pattern, pattern, sizeof(async_replace.pattern) - 1);
	async_replace.pattern[sizeof(async_replace.pattern) - 1] = '\0';
	strncpy(async_replace.replacement, replacement_text, sizeof(async_replace.replacement) - 1);
	async_replace.replacement[sizeof(async_replace.replacement) - 1] = '\0';
	async_replace.use_regex = use_regex;
	async_replace.case_sensitive = case_sensitive;
	async_replace.whole_word = whole_word;

	/* Submit task */
	struct task task = {
		.type = TASK_REPLACE_ALL,
		.task_id = task_generate_id(),
		.replace = {
			.use_regex = use_regex,
			.case_sensitive = case_sensitive,
			.whole_word = whole_word
		}
	};
	strncpy(task.replace.pattern, pattern, sizeof(task.replace.pattern) - 1);
	task.replace.pattern[sizeof(task.replace.pattern) - 1] = '\0';
	strncpy(task.replace.replacement, replacement_text, sizeof(task.replace.replacement) - 1);
	task.replace.replacement[sizeof(task.replace.replacement) - 1] = '\0';

	int err = task_queue_push(&task);
	if (err == 0) {
		async_replace.task_id = task.task_id;
		async_replace.active = true;
		editor_set_status_message("Replacing all...");
		log_debug("Started async replace for '%s' -> '%s' (task %lu)",
		          pattern, replacement_text, task.task_id);
	} else {
		log_warn("Failed to start async replace: %s", edit_strerror(err));
		editor_set_status_message("Failed to start replace: %s", edit_strerror(err));
	}
}

/*****************************************************************************
 * Auto-Save Functions
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
static void autosave_update_path(void)
{
	autosave_generate_swap_path(editor.buffer.filename,
	                            autosave.swap_path,
	                            sizeof(autosave.swap_path));
}


/*
 * Create a snapshot of the current buffer for background saving.
 * Returns NULL on allocation failure.
 * Caller must free with buffer_snapshot_free().
 */
static struct buffer_snapshot *buffer_snapshot_create(void)
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
static void buffer_snapshot_free(struct buffer_snapshot *snapshot)
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

/*
 * Worker task: write buffer snapshot to swap file.
 */
static int worker_process_autosave(struct task *task, struct task_result *result)
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
 * Mark buffer as modified (for auto-save tracking).
 * Call this whenever buffer content changes.
 */
static void autosave_mark_modified(void)
{
	autosave.last_modify_time = time(NULL);
}

/*
 * Check if auto-save should run and trigger if needed.
 * Call this periodically from the main loop.
 */
static void autosave_check(void)
{
	if (!autosave.enabled || !worker.initialized) {
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
static void autosave_remove_swap(void)
{
	if (autosave.swap_path[0] != '\0') {
		if (unlink(autosave.swap_path) == 0) {
			log_debug("Removed swap file: %s", autosave.swap_path);
		}
		autosave.swap_exists = false;
	}
}

/*
 * Check if a swap file exists for the given filename.
 * Returns the swap file path if found, NULL otherwise.
 */
static const char *autosave_check_recovery(const char *filename)
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

/* Forward declarations for recovery prompt */
static int input_read_key(void);
static void output_buffer_append_string(struct output_buffer *output, const char *text);
static void output_buffer_append_char(struct output_buffer *output, char character);
static void output_buffer_flush(struct output_buffer *output);
static void output_buffer_free(struct output_buffer *output);
static void dialog_goto(struct output_buffer *output, int row, int column);
static void dialog_set_style(struct output_buffer *output, const struct style *style);
static void dialog_draw_header(struct output_buffer *output,
			       struct dialog_state *dialog,
			       const char *title);
static void dialog_draw_footer(struct output_buffer *output,
			       struct dialog_state *dialog,
			       const char *hint);
/* Note: terminal_disable_raw_mode is declared extern in error.h */

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
static bool autosave_prompt_recovery(const char *filename, const char *swap_path)
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
	output_buffer_append_string(&output, "\x1b[2J\x1b[H");  /* Clear screen, home cursor */
	output_buffer_append_string(&output, "\x1b[?25l");     /* Hide cursor */

	swap_recovery_draw(&output, &dialog, filename, swap_path, time_str, swap_size);

	output_buffer_append_string(&output, "\x1b[0m");       /* Reset attributes */
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

/*****************************************************************************
 * Neighbor Layer and Pair Entanglement
 *****************************************************************************/

/* Extract character class from neighbor field. */
static inline enum character_class neighbor_get_class(uint8_t neighbor)
{
	return (neighbor & NEIGHBOR_CLASS_MASK) >> NEIGHBOR_CLASS_SHIFT;
}

/* Extract token position from neighbor field. */
static inline enum token_position neighbor_get_position(uint8_t neighbor)
{
	return (neighbor & NEIGHBOR_POSITION_MASK) >> NEIGHBOR_POSITION_SHIFT;
}

/* Encode character class and token position into neighbor field. */
static inline uint8_t neighbor_encode(enum character_class class, enum token_position position)
{
	return (class << NEIGHBOR_CLASS_SHIFT) | (position << NEIGHBOR_POSITION_SHIFT);
}

/* Extract pair ID from context field. */
static inline uint32_t context_get_pair_id(uint32_t context)
{
	return context & CONTEXT_PAIR_ID_MASK;
}

/* Extract pair type from context field. */
static inline enum pair_type context_get_pair_type(uint32_t context)
{
	return (context & CONTEXT_PAIR_TYPE_MASK) >> CONTEXT_PAIR_TYPE_SHIFT;
}

/* Extract pair role from context field. */
static inline enum pair_role context_get_pair_role(uint32_t context)
{
	return (context & CONTEXT_PAIR_ROLE_MASK) >> CONTEXT_PAIR_ROLE_SHIFT;
}

/* Encode pair ID, type, and role into context field. */
static inline uint32_t context_encode(uint32_t pair_id, enum pair_type type, enum pair_role role)
{
	return (pair_id & CONTEXT_PAIR_ID_MASK) |
	       ((uint32_t)type << CONTEXT_PAIR_TYPE_SHIFT) |
	       ((uint32_t)role << CONTEXT_PAIR_ROLE_SHIFT);
}

/* Classify a codepoint into a character class. */
static enum character_class classify_codepoint(uint32_t cp)
{
	if (cp == ' ' || cp == '\t') {
		return CHAR_CLASS_WHITESPACE;
	}
	if (cp == '_') {
		return CHAR_CLASS_UNDERSCORE;
	}
	if (cp >= 'a' && cp <= 'z') {
		return CHAR_CLASS_LETTER;
	}
	if (cp >= 'A' && cp <= 'Z') {
		return CHAR_CLASS_LETTER;
	}
	if (cp >= '0' && cp <= '9') {
		return CHAR_CLASS_DIGIT;
	}
	if (cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
	    cp == '{' || cp == '}') {
		return CHAR_CLASS_BRACKET;
	}
	if (cp == '"' || cp == '\'' || cp == '`') {
		return CHAR_CLASS_QUOTE;
	}
	/* ASCII punctuation */
	if ((cp >= '!' && cp <= '/') || (cp >= ':' && cp <= '@') ||
	    (cp >= '[' && cp <= '`') || (cp >= '{' && cp <= '~')) {
		return CHAR_CLASS_PUNCTUATION;
	}
	/* Unicode letters (simplified - covers common ranges) */
	if (cp >= 0x00C0 && cp <= 0x024F) {
		return CHAR_CLASS_LETTER;  /* Latin Extended */
	}
	if (cp >= 0x0400 && cp <= 0x04FF) {
		return CHAR_CLASS_LETTER;  /* Cyrillic */
	}
	if (cp >= 0x4E00 && cp <= 0x9FFF) {
		return CHAR_CLASS_LETTER;  /* CJK */
	}

	return CHAR_CLASS_OTHER;
}

/* Check if two character classes form a word together. */
static bool classes_form_word(enum character_class a, enum character_class b)
{
	/* Letters, digits, and underscores form words together */
	bool a_is_word_char = (a == CHAR_CLASS_LETTER || a == CHAR_CLASS_DIGIT ||
	                       a == CHAR_CLASS_UNDERSCORE);
	bool b_is_word_char = (b == CHAR_CLASS_LETTER || b == CHAR_CLASS_DIGIT ||
	                       b == CHAR_CLASS_UNDERSCORE);
	return a_is_word_char && b_is_word_char;
}

/* Compute neighbor data (character class and token position) for a line. */
void neighbor_compute_line(struct line *line)
{
	if (line->cell_count == 0) {
		return;
	}

	/* First pass: assign character classes */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		enum character_class class = classify_codepoint(line->cells[i].codepoint);
		line->cells[i].neighbor = neighbor_encode(class, TOKEN_POSITION_SOLO);
	}

	/* Second pass: compute token positions */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		enum character_class my_class = neighbor_get_class(line->cells[i].neighbor);

		bool has_prev = (i > 0);
		bool has_next = (i < line->cell_count - 1);

		enum character_class prev_class = has_prev ?
			neighbor_get_class(line->cells[i - 1].neighbor) : CHAR_CLASS_WHITESPACE;
		enum character_class next_class = has_next ?
			neighbor_get_class(line->cells[i + 1].neighbor) : CHAR_CLASS_WHITESPACE;

		bool joins_prev = has_prev && classes_form_word(prev_class, my_class);
		bool joins_next = has_next && classes_form_word(my_class, next_class);

		enum token_position position;
		if (!joins_prev && !joins_next) {
			position = TOKEN_POSITION_SOLO;
		} else if (!joins_prev && joins_next) {
			position = TOKEN_POSITION_START;
		} else if (joins_prev && joins_next) {
			position = TOKEN_POSITION_MIDDLE;
		} else {
			position = TOKEN_POSITION_END;
		}

		line->cells[i].neighbor = neighbor_encode(my_class, position);
	}
}

/* Is this cell at the start of a word? */
static bool cell_is_word_start(struct cell *cell)
{
	enum token_position pos = neighbor_get_position(cell->neighbor);
	return pos == TOKEN_POSITION_START || pos == TOKEN_POSITION_SOLO;
}

/* Is this cell at the end of a word? (Reserved for double-click selection) */
__attribute__((unused))
static bool cell_is_word_end(struct cell *cell)
{
	enum token_position pos = neighbor_get_position(cell->neighbor);
	return pos == TOKEN_POSITION_END || pos == TOKEN_POSITION_SOLO;
}

/*
 * Check if a cell is trailing whitespace (whitespace with no non-whitespace
 * content after it on the line). Used for visual highlighting.
 */
static bool is_trailing_whitespace(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return false;
	}

	/* First check if current cell is whitespace. */
	enum character_class current_class =
		neighbor_get_class(line->cells[column].neighbor);
	if (current_class != CHAR_CLASS_WHITESPACE) {
		return false;
	}

	/* Check if there's any non-whitespace after this position. */
	for (uint32_t i = column + 1; i < line->cell_count; i++) {
		enum character_class cell_class =
			neighbor_get_class(line->cells[i].neighbor);
		if (cell_class != CHAR_CLASS_WHITESPACE) {
			return false;
		}
	}

	return true;
}

/* Find start of previous word. */
static uint32_t find_prev_word_start(struct line *line, uint32_t column)
{
	if (column == 0 || line->cell_count == 0) {
		return 0;
	}

	column--;

	/* Skip whitespace */
	while (column > 0 &&
	       neighbor_get_class(line->cells[column].neighbor) == CHAR_CLASS_WHITESPACE) {
		column--;
	}

	/* Find start of this word */
	while (column > 0 && !cell_is_word_start(&line->cells[column])) {
		column--;
	}

	return column;
}

/* Find start of next word. */
static uint32_t find_next_word_start(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return line->cell_count;
	}

	/* Move past current position */
	column++;

	/* Skip until we find a non-whitespace word start */
	while (column < line->cell_count) {
		if (neighbor_get_class(line->cells[column].neighbor) != CHAR_CLASS_WHITESPACE &&
		    cell_is_word_start(&line->cells[column])) {
			break;
		}
		column++;
	}

	return column;
}

/* Allocate a unique pair ID. */
static uint32_t buffer_allocate_pair_id(struct buffer *buffer)
{
	return ++buffer->next_pair_id;
}


/*
 * Scan the entire buffer to match pairs. This handles block comments,
 * and brackets: (), [], {}. Call after loading a file or after edits
 * that might affect pairs.
 */
static void buffer_compute_pairs(struct buffer *buffer)
{
	/* Reset all pair IDs and warm all lines first */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}
		for (uint32_t col = 0; col < line->cell_count; col++) {
			line->cells[col].context = 0;
		}
	}

	buffer->next_pair_id = 0;

	/* Stack for bracket matching */
	struct {
		uint32_t row;
		uint32_t col;
		uint32_t pair_id;
		enum pair_type type;
	} stack[256];
	int stack_top = 0;

	/* Are we inside a block comment? */
	bool in_block_comment = false;
	uint32_t comment_pair_id = 0;

	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		for (uint32_t col = 0; col < line->cell_count; col++) {
			uint32_t cp = line->cells[col].codepoint;

			/* Check for block comment start */
			if (!in_block_comment && cp == '/' && col + 1 < line->cell_count &&
			    line->cells[col + 1].codepoint == '*') {
				in_block_comment = true;
				comment_pair_id = buffer_allocate_pair_id(buffer);

				/* Mark the '/' and '*' as opener */
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_OPENER);
				col++;
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_OPENER);
				continue;
			}

			/* Check for block comment end */
			if (in_block_comment && cp == '*' && col + 1 < line->cell_count &&
			    line->cells[col + 1].codepoint == '/') {
				/* Mark the '*' and '/' as closer */
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_CLOSER);
				col++;
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_CLOSER);
				in_block_comment = false;
				continue;
			}

			/* Skip other processing if inside block comment */
			if (in_block_comment) {
				continue;
			}

			/* Opening brackets */
			if (cp == '(' || cp == '[' || cp == '{') {
				enum pair_type type = (cp == '(') ? PAIR_TYPE_PAREN :
				                      (cp == '[') ? PAIR_TYPE_BRACKET :
				                      PAIR_TYPE_BRACE;
				uint32_t pair_id = buffer_allocate_pair_id(buffer);

				line->cells[col].context = context_encode(pair_id, type,
					PAIR_ROLE_OPENER);

				if (stack_top < 256) {
					stack[stack_top].row = row;
					stack[stack_top].col = col;
					stack[stack_top].pair_id = pair_id;
					stack[stack_top].type = type;
					stack_top++;
				}
				continue;
			}

			/* Closing brackets */
			if (cp == ')' || cp == ']' || cp == '}') {
				enum pair_type type = (cp == ')') ? PAIR_TYPE_PAREN :
				                      (cp == ']') ? PAIR_TYPE_BRACKET :
				                      PAIR_TYPE_BRACE;

				/* Find matching opener on stack */
				int match = -1;
				for (int i = stack_top - 1; i >= 0; i--) {
					if (stack[i].type == type) {
						match = i;
						break;
					}
				}

				if (match >= 0) {
					uint32_t pair_id = stack[match].pair_id;
					line->cells[col].context = context_encode(pair_id, type,
						PAIR_ROLE_CLOSER);

					/* Remove from stack (and any unmatched openers above it) */
					stack_top = match;
				} else {
					/* Unmatched closer */
					line->cells[col].context = 0;
				}
				continue;
			}
		}
	}
}

/*
 * Given a cell with a pair context, find its matching partner.
 * Returns true if found, with partner position in out_row/out_col.
 */
static bool buffer_find_pair_partner(struct buffer *buffer,
                                     uint32_t row, uint32_t col,
                                     uint32_t *out_row, uint32_t *out_col)
{
	if (row >= buffer->line_count) {
		return false;
	}

	struct line *line = &buffer->lines[row];
	if (col >= line->cell_count) {
		return false;
	}

	uint32_t context = line->cells[col].context;
	uint32_t pair_id = context_get_pair_id(context);
	enum pair_role role = context_get_pair_role(context);

	if (pair_id == 0 || role == PAIR_ROLE_NONE) {
		return false;
	}

	/* Search direction depends on role */
	bool search_forward = (role == PAIR_ROLE_OPENER);

	if (search_forward) {
		/* Search forward for closer */
		for (uint32_t r = row; r < buffer->line_count; r++) {
			struct line *search_line = &buffer->lines[r];
			if (line_get_temperature(search_line) == LINE_TEMPERATURE_COLD) {
				line_warm(search_line, buffer);
			}

			uint32_t start_col = (r == row) ? col + 1 : 0;
			for (uint32_t c = start_col; c < search_line->cell_count; c++) {
				if (context_get_pair_id(search_line->cells[c].context) == pair_id &&
				    context_get_pair_role(search_line->cells[c].context) == PAIR_ROLE_CLOSER) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	} else {
		/* Search backward for opener */
		for (int r = row; r >= 0; r--) {
			struct line *search_line = &buffer->lines[r];
			if (line_get_temperature(search_line) == LINE_TEMPERATURE_COLD) {
				line_warm(search_line, buffer);
			}

			int start_col = (r == (int)row) ? (int)col - 1 :
				(int)search_line->cell_count - 1;
			for (int c = start_col; c >= 0; c--) {
				if (context_get_pair_id(search_line->cells[c].context) == pair_id &&
				    context_get_pair_role(search_line->cells[c].context) == PAIR_ROLE_OPENER) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	}

	return false;
}

/*
 * Check if a position is inside a block comment by examining pair context.
 * Scans backward for an unclosed comment opener.
 */
static bool syntax_is_in_block_comment(struct buffer *buffer, uint32_t row, uint32_t col)
{
	/* Scan backwards for an unclosed block comment opener */
	for (int r = row; r >= 0; r--) {
		struct line *line = &buffer->lines[r];
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}

		int end_col = (r == (int)row) ? (int)col - 1 : (int)line->cell_count - 1;

		for (int c = end_col; c >= 0; c--) {
			uint32_t context = line->cells[c].context;
			enum pair_type type = context_get_pair_type(context);
			enum pair_role role = context_get_pair_role(context);

			if (type == PAIR_TYPE_COMMENT) {
				if (role == PAIR_ROLE_CLOSER) {
					/* Found a closer before us, so we're not in that comment */
					return false;
				}
				if (role == PAIR_ROLE_OPENER) {
					/* Found an opener - check if it closes after our position */
					uint32_t partner_row, partner_col;
					if (buffer_find_pair_partner(buffer, r, c,
					                             &partner_row, &partner_col)) {
						/* Has a closer - are we before it? */
						if (partner_row > row ||
						    (partner_row == row && partner_col >= col)) {
							return true;  /* We're inside this comment */
						}
						/* Closer is before us, keep searching */
					} else {
						/* Unclosed comment - we're inside it */
						return true;
					}
				}
			}
		}
	}

	return false;
}

/*****************************************************************************
 * Syntax Highlighting
 *****************************************************************************/

/* C language keywords - control flow and declarations. */
static const char *C_KEYWORDS[] = {
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "goto", "sizeof", "typedef",
	"struct", "union", "enum", "static", "const", "volatile",
	"extern", "register", "inline", "restrict", "_Atomic", "_Noreturn",
	NULL
};

/* C language type names and common typedefs. */
static const char *C_TYPES[] = {
	"int", "char", "short", "long", "float", "double", "void",
	"signed", "unsigned", "bool", "true", "false", "NULL",
	"int8_t", "int16_t", "int32_t", "int64_t",
	"uint8_t", "uint16_t", "uint32_t", "uint64_t",
	"size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
	"FILE", "va_list",
	NULL
};

/* Returns true if codepoint is an ASCII letter. */
static bool syntax_is_alpha(uint32_t cp)
{
	return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

/* Returns true if codepoint is an ASCII digit. */
static bool syntax_is_digit(uint32_t cp)
{
	return cp >= '0' && cp <= '9';
}

/* Returns true if codepoint is alphanumeric or underscore. */
static bool syntax_is_alnum(uint32_t cp)
{
	return syntax_is_alpha(cp) || syntax_is_digit(cp) || cp == '_';
}

/* Returns true if codepoint could be part of a number literal. */
static bool syntax_is_number_char(uint32_t cp)
{
	return syntax_is_digit(cp) || cp == '.' || cp == 'x' || cp == 'X' ||
	       (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F') ||
	       cp == 'u' || cp == 'U' || cp == 'l' || cp == 'L';
}

/* Returns true if codepoint is a C operator. */
static bool syntax_is_operator(uint32_t cp)
{
	return cp == '+' || cp == '-' || cp == '*' || cp == '/' ||
	       cp == '=' || cp == '<' || cp == '>' || cp == '!' ||
	       cp == '&' || cp == '|' || cp == '^' || cp == '~' ||
	       cp == '%' || cp == '?' || cp == ':' || cp == ';' ||
	       cp == ',' || cp == '.';
}

/* Returns true if codepoint is a bracket character. */
static bool syntax_is_bracket(uint32_t cp)
{
	return cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
	       cp == '{' || cp == '}';
}

/* Returns true if position is at line start (only whitespace before). */
static bool syntax_is_line_start(struct line *line, uint32_t pos)
{
	for (uint32_t i = 0; i < pos; i++) {
		uint32_t cp = line->cells[i].codepoint;
		if (cp != ' ' && cp != '\t') {
			return false;
		}
	}
	return true;
}

/* Extracts a word from cells into a buffer. Only ASCII characters. */
static void syntax_extract_word(struct line *line, uint32_t start, uint32_t end,
                                char *buffer, size_t buffer_size)
{
	size_t len = 0;
	for (uint32_t i = start; i < end && len < buffer_size - 1; i++) {
		uint32_t cp = line->cells[i].codepoint;
		if (cp < 128) {
			buffer[len++] = (char)cp;
		}
	}
	buffer[len] = '\0';
}

/* Returns true if word is a C keyword. */
static bool syntax_is_keyword(const char *word)
{
	for (int i = 0; C_KEYWORDS[i] != NULL; i++) {
		if (strcmp(word, C_KEYWORDS[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns true if word is a C type name. */
static bool syntax_is_type(const char *word)
{
	for (int i = 0; C_TYPES[i] != NULL; i++) {
		if (strcmp(word, C_TYPES[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns true if filename has a C/C++ extension. */
static bool syntax_is_c_file(const char *filename)
{
	if (filename == NULL) {
		return false;
	}

	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return false;
	}

	const char *ext = dot + 1;
	return strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0 ||
	       strcmp(ext, "cpp") == 0 || strcmp(ext, "hpp") == 0 ||
	       strcmp(ext, "cc") == 0 || strcmp(ext, "cxx") == 0;
}

/*
 * Apply syntax highlighting to a single line. Tokenizes the line and sets
 * the syntax field of each cell. Uses pair context for multiline comments.
 */
void syntax_highlight_line(struct line *line, struct buffer *buffer,
                                  uint32_t row)
{
	/* Only highlight C files */
	if (!syntax_is_c_file(buffer->filename)) {
		return;
	}

	/* Must be warm/hot to highlight */
	if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
		return;
	}

	/* Reset all cells to normal */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		line->cells[i].syntax = SYNTAX_NORMAL;
	}

	/* Check if line starts inside a block comment */
	bool in_block_comment = syntax_is_in_block_comment(buffer, row, 0);

	uint32_t i = 0;
	while (i < line->cell_count) {
		uint32_t cp = line->cells[i].codepoint;

		/* If inside block comment, everything is comment until we exit */
		if (in_block_comment) {
			line->cells[i].syntax = SYNTAX_COMMENT;

			/* Check for block comment end */
			if (cp == '*' && i + 1 < line->cell_count &&
			    line->cells[i + 1].codepoint == '/') {
				line->cells[i].syntax = SYNTAX_COMMENT;
				i++;
				line->cells[i].syntax = SYNTAX_COMMENT;
				i++;
				in_block_comment = false;
				continue;
			}
			i++;
			continue;
		}

		/* Check for // comment */
		if (cp == '/' && i + 1 < line->cell_count &&
		    line->cells[i + 1].codepoint == '/') {
			while (i < line->cell_count) {
				line->cells[i].syntax = SYNTAX_COMMENT;
				i++;
			}
			break;
		}

		/* Check for block comment start */
		if (cp == '/' && i + 1 < line->cell_count &&
		    line->cells[i + 1].codepoint == '*') {
			in_block_comment = true;
			line->cells[i].syntax = SYNTAX_COMMENT;
			i++;
			line->cells[i].syntax = SYNTAX_COMMENT;
			i++;
			continue;
		}

		/* Check for string literal */
		if (cp == '"' || cp == '\'') {
			uint32_t quote = cp;
			line->cells[i].syntax = SYNTAX_STRING;
			i++;
			while (i < line->cell_count) {
				cp = line->cells[i].codepoint;
				if (cp == '\\' && i + 1 < line->cell_count) {
					line->cells[i].syntax = SYNTAX_ESCAPE;
					i++;
					line->cells[i].syntax = SYNTAX_ESCAPE;
					i++;
					continue;
				}
				line->cells[i].syntax = SYNTAX_STRING;
				if (cp == quote) {
					i++;
					break;
				}
				i++;
			}
			continue;
		}

		/* Check for preprocessor directive */
		if (cp == '#' && syntax_is_line_start(line, i)) {
			while (i < line->cell_count) {
				line->cells[i].syntax = SYNTAX_PREPROCESSOR;
				i++;
			}
			break;
		}

		/* Check for number literal */
		if (syntax_is_digit(cp) ||
		    (cp == '.' && i + 1 < line->cell_count &&
		     syntax_is_digit(line->cells[i + 1].codepoint))) {
			while (i < line->cell_count &&
			       syntax_is_number_char(line->cells[i].codepoint)) {
				line->cells[i].syntax = SYNTAX_NUMBER;
				i++;
			}
			continue;
		}

		/* Check for identifier (keyword, type, or function) */
		if (syntax_is_alpha(cp) || cp == '_') {
			uint32_t start = i;
			while (i < line->cell_count &&
			       syntax_is_alnum(line->cells[i].codepoint)) {
				i++;
			}

			/* Extract word and classify */
			char word[64];
			syntax_extract_word(line, start, i, word, sizeof(word));

			enum syntax_token type = SYNTAX_NORMAL;
			if (syntax_is_keyword(word)) {
				type = SYNTAX_KEYWORD;
			} else if (syntax_is_type(word)) {
				type = SYNTAX_TYPE;
			} else {
				/* Check if followed by '(' - it's a function */
				uint32_t j = i;
				while (j < line->cell_count &&
				       (line->cells[j].codepoint == ' ' ||
				        line->cells[j].codepoint == '\t')) {
					j++;
				}
				if (j < line->cell_count &&
				    line->cells[j].codepoint == '(') {
					type = SYNTAX_FUNCTION;
				}
			}

			for (uint32_t j = start; j < i; j++) {
				line->cells[j].syntax = type;
			}
			continue;
		}

		/* Check for operator */
		if (syntax_is_operator(cp)) {
			line->cells[i].syntax = SYNTAX_OPERATOR;
			i++;
			continue;
		}

		/* Check for bracket */
		if (syntax_is_bracket(cp)) {
			line->cells[i].syntax = SYNTAX_BRACKET;
			i++;
			continue;
		}

		/* Default: skip */
		i++;
	}
}

/*****************************************************************************
 * Signal Handlers
 *****************************************************************************/

/*
 * Fatal signal handler - attempts to save user data before dying.
 *
 * Called on SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL. Restores the
 * terminal to a sane state and attempts emergency save, then re-raises
 * the signal with default handler to get proper exit status.
 */
static void fatal_signal_handler(int sig)
{
	/* Restore terminal first so error output is visible */
	terminal_disable_raw_mode();

	/* Attempt to save user's work */
	emergency_save();

	/* Re-raise with default handler to get proper exit status */
	signal(sig, SIG_DFL);
	raise(sig);
}

/*****************************************************************************
 * Output Buffer
 *****************************************************************************/

/*
 * Initialize an output buffer with starting capacity.
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
static int __must_check output_buffer_init_checked(struct output_buffer *output)
{
	void *data = edit_malloc(INITIAL_OUTPUT_CAPACITY);
	if (IS_ERR(data))
		return (int)PTR_ERR(data);

	output->data = data;
	output->length = 0;
	output->capacity = INITIAL_OUTPUT_CAPACITY;
	return 0;
}

/*
 * Initialize an output buffer with starting capacity.
 * Crashes on allocation failure - use output_buffer_init_checked() for error handling.
 */
__attribute__((unused))
static void output_buffer_init(struct output_buffer *output)
{
	int ret = output_buffer_init_checked(output);
	BUG_ON(ret);
}

/*
 * Append bytes to the output buffer, growing it if necessary.
 * Returns 0 on success, -ENOMEM on allocation failure.
 */
static int __must_check output_buffer_append_checked(struct output_buffer *output,
                                                      const char *text, size_t length)
{
	if (output->length + length > output->capacity) {
		size_t new_capacity = output->capacity ? output->capacity * 2 : 256;
		while (new_capacity < output->length + length)
			new_capacity *= 2;

		void *new_data = edit_realloc(output->data, new_capacity);
		if (IS_ERR(new_data))
			return (int)PTR_ERR(new_data);

		output->data = new_data;
		output->capacity = new_capacity;
	}

	memcpy(output->data + output->length, text, length);
	output->length += length;
	return 0;
}

/*
 * Append bytes to the output buffer, growing it if necessary.
 * Crashes on allocation failure - use output_buffer_append_checked() for error handling.
 */
static void output_buffer_append(struct output_buffer *output, const char *text, size_t length)
{
	int ret = output_buffer_append_checked(output, text, length);
	BUG_ON(ret);
}

/* Appends a null-terminated string to the output buffer. */
static void output_buffer_append_string(struct output_buffer *output, const char *text)
{
	output_buffer_append(output, text, strlen(text));
}

/* Appends a single character to the output buffer. */
static void output_buffer_append_char(struct output_buffer *output, char character)
{
	output_buffer_append(output, &character, 1);
}

/* Writes all buffered data to stdout and resets the buffer length. */
static void output_buffer_flush(struct output_buffer *output)
{
	write(STDOUT_FILENO, output->data, output->length);
	output->length = 0;
}

/* Frees the output buffer's memory and resets all fields. */
static void output_buffer_free(struct output_buffer *output)
{
	free(output->data);
	output->data = NULL;
	output->length = 0;
	output->capacity = 0;
}

/*****************************************************************************
 * Input Handling
 *****************************************************************************/

/* Reads a single keypress from stdin, handling escape sequences and UTF-8.
 * Returns the key code (positive for characters/codepoints, negative for
 * special keys like arrows). Returns -1 on error, -2 on terminal resize. */
static int input_read_key(void)
{
	int read_count;
	unsigned char character;

	while ((read_count = read(STDIN_FILENO, &character, 1)) != 1) {
		if (read_count == -1 && errno != EAGAIN) {
			return -1;
		}
		if (terminal_check_resize()) {
			return -2;
		}
	}

	/* Handle escape sequences */
	if (character == '\x1b') {
		char sequence[4];

		if (read(STDIN_FILENO, &sequence[0], 1) != 1) {
			return '\x1b';
		}

		/* Check for Alt+key (Meta sends ESC followed by letter) */
		if (sequence[0] != '[' && sequence[0] != 'O') {
			switch (sequence[0]) {
				case 'n': case 'N': return KEY_ALT_N;
				case 'p': case 'P': return KEY_ALT_P;
				case 'z': return KEY_ALT_Z;
				case 'Z': return KEY_ALT_SHIFT_Z;
				case 'S': return KEY_ALT_SHIFT_S;
				case 'k': case 'K': return KEY_ALT_K;
				case 'd': case 'D': return KEY_ALT_D;
				case '/': return KEY_ALT_SLASH;
				case 'a': case 'A': return KEY_ALT_A;
				case ']': return KEY_ALT_BRACKET;
				case 'c': case 'C': return KEY_ALT_C;
				case 'w': case 'W': return KEY_ALT_W;
				case 'r': return KEY_ALT_R;
				default: return '\x1b';
			}
		}

		if (read(STDIN_FILENO, &sequence[1], 1) != 1) {
			return '\x1b';
		}

		if (sequence[0] == '[') {
			if (sequence[1] >= '0' && sequence[1] <= '9') {
				if (read(STDIN_FILENO, &sequence[2], 1) != 1) {
					return '\x1b';
				}
				if (sequence[2] == '~') {
					switch (sequence[1]) {
						case '1': return KEY_HOME;
						case '3': return KEY_DELETE;
						case '4': return KEY_END;
						case '5': return KEY_PAGE_UP;
						case '6': return KEY_PAGE_DOWN;
						case '7': return KEY_HOME;
						case '8': return KEY_END;
					}
				} else if (sequence[2] == ';') {
					/* Modified key sequences */
					char modifier, final;
					if (read(STDIN_FILENO, &modifier, 1) != 1) {
						return '\x1b';
					}
					if (read(STDIN_FILENO, &final, 1) != 1) {
						return '\x1b';
					}
					if (sequence[1] == '1') {
						/* \x1b[1;{mod}{key} - modified arrow/Home/End */
						if (modifier == '2') {  /* Shift */
							switch (final) {
								case 'A': return KEY_SHIFT_ARROW_UP;
								case 'B': return KEY_SHIFT_ARROW_DOWN;
								case 'C': return KEY_SHIFT_ARROW_RIGHT;
								case 'D': return KEY_SHIFT_ARROW_LEFT;
								case 'H': return KEY_SHIFT_HOME;
								case 'F': return KEY_SHIFT_END;
								case 'S': return KEY_SHIFT_F4;
							}
						} else if (modifier == '5') {  /* Ctrl */
							switch (final) {
								case 'C': return KEY_CTRL_ARROW_RIGHT;
								case 'D': return KEY_CTRL_ARROW_LEFT;
							}
						} else if (modifier == '6') {  /* Ctrl+Shift */
							switch (final) {
								case 'C': return KEY_CTRL_SHIFT_ARROW_RIGHT;
								case 'D': return KEY_CTRL_SHIFT_ARROW_LEFT;
							}
						} else if (modifier == '3') {  /* Alt */
							switch (final) {
								case 'A': return KEY_ALT_ARROW_UP;
								case 'B': return KEY_ALT_ARROW_DOWN;
							}
						}
					} else if ((sequence[1] == '5' || sequence[1] == '6') &&
					           modifier == '2' && final == '~') {
						/* \x1b[5;2~ or \x1b[6;2~ - Shift+PageUp/Down */
						if (sequence[1] == '5') return KEY_SHIFT_PAGE_UP;
						if (sequence[1] == '6') return KEY_SHIFT_PAGE_DOWN;
					}
				} else if (sequence[2] >= '0' && sequence[2] <= '9') {
					/* Two-digit escape sequence like \x1b[12~ for F2 */
					if (read(STDIN_FILENO, &sequence[3], 1) != 1) {
						return '\x1b';
					}
					if (sequence[3] == '~') {
						if (sequence[1] == '1' && sequence[2] == '2') {
							return KEY_F2;
						} else if (sequence[1] == '1' && sequence[2] == '3') {
							return KEY_F3;
						} else if (sequence[1] == '1' && sequence[2] == '4') {
							return KEY_F4;
						} else if (sequence[1] == '1' && sequence[2] == '5') {
							return KEY_F5;
						} else if (sequence[1] == '2' && sequence[2] == '4') {
							return KEY_F12;
						}
					}
				}
			} else if (sequence[1] == '<') {
				/* SGR mouse event: \x1b[<button;column;row{M|m} */
				struct mouse_input mouse = input_parse_sgr_mouse();
				if (mouse.event != MOUSE_NONE) {
					if (dialog_mouse_mode) {
						dialog_last_mouse = mouse;
					} else {
						editor_handle_mouse(&mouse);
					}
				}
				return KEY_MOUSE_EVENT;
			} else {
				switch (sequence[1]) {
					case 'A': return KEY_ARROW_UP;
					case 'B': return KEY_ARROW_DOWN;
					case 'C': return KEY_ARROW_RIGHT;
					case 'D': return KEY_ARROW_LEFT;
					case 'H': return KEY_HOME;
					case 'F': return KEY_END;
					case 'Z': return KEY_SHIFT_TAB;
				}
			}
		} else if (sequence[0] == 'O') {
			switch (sequence[1]) {
				case 'H': return KEY_HOME;
				case 'F': return KEY_END;
				case 'Q': return KEY_F2;
				case 'R': return KEY_F3;
				case 'S': return KEY_F4;
			}
		}

		return '\x1b';
	}

	/* Handle UTF-8 multi-byte sequences */
	if (character & 0x80) {
		char utf8_buffer[4];
		utf8_buffer[0] = character;
		int bytes_to_read = 0;

		/* Determine number of continuation bytes based on lead byte */
		if ((character & 0xE0) == 0xC0) {
			bytes_to_read = 1;  /* 2-byte sequence */
		} else if ((character & 0xF0) == 0xE0) {
			bytes_to_read = 2;  /* 3-byte sequence */
		} else if ((character & 0xF8) == 0xF0) {
			bytes_to_read = 3;  /* 4-byte sequence */
		} else {
			/* Invalid UTF-8 lead byte, return replacement character */
			return UTFLITE_REPLACEMENT_CHAR;
		}

		/* Read continuation bytes */
		for (int i = 0; i < bytes_to_read; i++) {
			if (read(STDIN_FILENO, &utf8_buffer[1 + i], 1) != 1) {
				return UTFLITE_REPLACEMENT_CHAR;
			}
			/* Verify continuation byte */
			if ((utf8_buffer[1 + i] & 0xC0) != 0x80) {
				return UTFLITE_REPLACEMENT_CHAR;
			}
		}

		/* Decode UTF-8 to codepoint */
		uint32_t codepoint;
		utflite_decode(utf8_buffer, bytes_to_read + 1, &codepoint);
		return (int)codepoint;
	}

	/* Handle Ctrl+O and Ctrl+T for open file and theme picker */
	if (character == CONTROL_KEY('o')) {
		return KEY_CTRL_O;
	}
	if (character == CONTROL_KEY('t')) {
		return KEY_CTRL_T;
	}

	return character;
}


/*****************************************************************************
 * File Operations
 *****************************************************************************/

/*
 * Emergency save - attempt to save buffer contents on crash.
 *
 * This function is called by fatal signal handlers and BUG macros to
 * preserve user data when the editor crashes. It's designed to be as
 * simple and robust as possible:
 *
 * - Guards against recursive calls (crash during emergency save)
 * - Skips if there's nothing to save
 * - Tries original location first, falls back to /tmp
 * - Handles both cold (mmap'd) and warm/hot (cell-based) lines
 * - Best effort: ignores write errors since we're already dying
 */
void emergency_save(void)
{
	static bool in_emergency_save = false;

	/* Guard against recursive calls */
	if (in_emergency_save)
		return;
	in_emergency_save = true;

	struct buffer *buffer = &editor.buffer;

	/* Nothing to save? */
	if (buffer->line_count == 0 || !buffer->is_modified)
		return;

	/* Build emergency filename */
	char emergency_path[PATH_MAX];
	pid_t pid = getpid();

	if (buffer->filename) {
		snprintf(emergency_path, sizeof(emergency_path),
		         "%s.emergency.%d", buffer->filename, pid);
	} else {
		snprintf(emergency_path, sizeof(emergency_path),
		         "/tmp/edit.emergency.%d", pid);
	}

	/* Try to open file for writing */
	FILE *file = fopen(emergency_path, "w");
	if (!file && buffer->filename) {
		/* Fallback to /tmp if original location fails */
		snprintf(emergency_path, sizeof(emergency_path),
		         "/tmp/edit.emergency.%d", pid);
		file = fopen(emergency_path, "w");
	}

	if (!file)
		return;

	/* Write all lines - best effort, ignore errors */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			/* Cold line: write directly from mmap */
			if (buffer->mmap_base && line->mmap_length > 0) {
				fwrite(buffer->mmap_base + line->mmap_offset,
				       1, line->mmap_length, file);
			}
		} else {
			/* Warm/hot line: encode cells to UTF-8 */
			for (uint32_t col = 0; col < line->cell_count; col++) {
				char utf8_buffer[UTFLITE_MAX_BYTES];
				int bytes = utflite_encode(
					line->cells[col].codepoint, utf8_buffer);
				fwrite(utf8_buffer, 1, bytes, file);
			}
		}

		fwrite("\n", 1, 1, file);
	}

	fclose(file);
	fprintf(stderr, "edit: emergency save to %s\n", emergency_path);
}

/*
 * Build the line index by scanning the mmap for newlines. Each line is
 * created as cold with just offset and length - no cells allocated.
 */
static void file_build_line_index(struct buffer *buffer)
{
	if (buffer->mmap_size == 0) {
		/* Empty file - no lines to index */
		return;
	}

	size_t line_start = 0;

	for (size_t i = 0; i <= buffer->mmap_size; i++) {
		bool is_newline = (i < buffer->mmap_size && buffer->mmap_base[i] == '\n');
		bool is_eof = (i == buffer->mmap_size);

		if (is_newline || is_eof) {
			/* Found end of line - strip trailing CR if present */
			size_t line_end = i;
			if (line_end > line_start && buffer->mmap_base[line_end - 1] == '\r') {
				line_end--;
			}

			buffer_ensure_capacity(buffer, buffer->line_count + 1);

			struct line *line = &buffer->lines[buffer->line_count];
			line->cells = NULL;
			line->cell_count = 0;
			line->cell_capacity = 0;
			line->mmap_offset = line_start;
			line->mmap_length = line_end - line_start;
			line_set_temperature(line, LINE_TEMPERATURE_COLD);
			line->wrap_columns = NULL;
			line->wrap_segment_count = 0;
			line->wrap_cache_width = 0;
			line->wrap_cache_mode = WRAP_NONE;

			buffer->line_count++;
			line_start = i + 1;
		}
	}
}

/*
 * Load a file from disk into a buffer using mmap. The file is memory-mapped
 * and lines are created as cold references into the mmap. Only lines that
 * are accessed will have cells allocated.
 *
 * Returns 0 on success, negative error code on failure:
 *   -ENOENT, -EACCES, etc. from open()
 *   -errno from fstat() or mmap()
 *   -ENOMEM from allocation failure
 */
static int __must_check file_open(struct buffer *buffer, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -errno;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		int err = errno;
		close(fd);
		return -err;
	}

	size_t file_size = st.st_size;
	char *mapped = NULL;

	if (file_size > 0) {
		mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mapped == MAP_FAILED) {
			int err = errno;
			close(fd);
			return -err;
		}
		/* Hint: we'll access this randomly as user scrolls */
		madvise(mapped, file_size, MADV_RANDOM);
	}

	buffer->file_descriptor = fd;
	buffer->mmap_base = mapped;
	buffer->mmap_size = file_size;

	/* Build line index by scanning for newlines */
	file_build_line_index(buffer);

	char *name = edit_strdup(filename);
	if (IS_ERR(name)) {
		/* Clean up on allocation failure */
		if (mapped)
			munmap(mapped, file_size);
		close(fd);
		return (int)PTR_ERR(name);
	}
	buffer->filename = name;
	buffer->is_modified = false;

	/* Compute pairs across entire buffer (warms all lines) */
	buffer_compute_pairs(buffer);

	/* Apply syntax highlighting with pair context */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		syntax_highlight_line(&buffer->lines[row], buffer, row);
	}

	return 0;
}

/*
 * Write a buffer's contents to disk. All lines are warmed first since
 * opening the file for writing will invalidate any mmap. The mmap is
 * then unmapped before writing. Updates the status message with bytes
 * written on success.
 *
 * Returns 0 on success, negative error code on failure:
 *   -EINVAL if buffer->filename is NULL
 *   -errno from fopen(), fwrite(), or fclose()
 */
static int __must_check file_save(struct buffer *buffer)
{
	if (buffer->filename == NULL)
		return -EINVAL;

	/* Warm all cold lines before we invalidate the mmap */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		line_warm(&buffer->lines[row], buffer);
	}

	/* Unmap file before overwriting - opening with "w" truncates it */
	if (buffer->mmap_base != NULL) {
		munmap(buffer->mmap_base, buffer->mmap_size);
		buffer->mmap_base = NULL;
		buffer->mmap_size = 0;
	}
	if (buffer->file_descriptor >= 0) {
		close(buffer->file_descriptor);
		buffer->file_descriptor = -1;
	}

	FILE *file = fopen(buffer->filename, "w");
	if (file == NULL)
		return -errno;

	size_t total_bytes = 0;

	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		/* All lines are warm/hot now - write from cells */
		for (uint32_t col = 0; col < line->cell_count; col++) {
			char utf8_buffer[UTFLITE_MAX_BYTES];
			int bytes = utflite_encode(line->cells[col].codepoint, utf8_buffer);
			if (fwrite(utf8_buffer, 1, bytes, file) != (size_t)bytes) {
				int err = errno;
				fclose(file);
				return -err;
			}
			total_bytes += bytes;
		}

		if (fwrite("\n", 1, 1, file) != 1) {
			int err = errno;
			fclose(file);
			return -err;
		}
		total_bytes++;
	}

	if (fclose(file) != 0)
		return -errno;

	buffer->is_modified = false;
	editor_set_status_message("%zu bytes written to disk", total_bytes);

	/* Remove swap file after successful save */
	autosave_remove_swap();
	autosave.last_save_time = time(NULL);

	return 0;
}

/*****************************************************************************
 * Editor Operations
 *****************************************************************************/

/*
 * Initialize the global editor state. Sets up an empty buffer and zeroes
 * all cursor positions, scroll offsets, and screen dimensions. Line numbers
 * are shown by default. The quit confirmation counter starts at its maximum.
 */
static void editor_init(void)
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
	editor.color_column = 0;
	editor.color_column_style = COLOR_COLUMN_SOLID;
	editor.theme_indicator = THEME_INDICATOR_CHECK;
	editor.cursor_count = 0;
	editor.primary_cursor = 0;

	/* Initialize theme system */
	themes_load();
	config_load();
	theme_apply_by_index(current_theme_index);

	/* Initialize worker thread */
	int err = worker_init();
	if (err) {
		log_warn("Worker thread disabled: %s", edit_strerror(err));
		/* Continue without background processing - not fatal */
	}

	/* Initialize async search system */
	err = async_search_init();
	if (err) {
		log_warn("Async search disabled: %s", edit_strerror(err));
	}

	/* Initialize async replace system */
	err = async_replace_init();
	if (err) {
		log_warn("Async replace disabled: %s", edit_strerror(err));
	}
}

/* Start a new selection at the current cursor position. The anchor is set
 * to the cursor position and selection becomes active. */
static void selection_start(void)
{
	editor.selection_anchor_row = editor.cursor_row;
	editor.selection_anchor_column = editor.cursor_column;
	editor.selection_active = true;
}

/* Clear the current selection. After this, no text is selected. */
static void selection_clear(void)
{
	editor.selection_active = false;
}

/*****************************************************************************
 * Multi-Cursor Management
 *****************************************************************************/

/*
 * Compare two cursors by position (for qsort).
 * Sorts by row first, then by column.
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
 * Copies current cursor/selection state to cursors[0].
 */
static void multicursor_enter(void)
{
	if (editor.cursor_count > 0) {
		return;  /* Already in multi-cursor mode */
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
static void multicursor_exit(void)
{
	if (editor.cursor_count == 0) {
		return;
	}

	/* Copy primary cursor back to legacy fields */
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
static void multicursor_normalize(void)
{
	if (editor.cursor_count <= 1) {
		return;
	}

	/* Sort by position */
	qsort(editor.cursors, editor.cursor_count,
	      sizeof(struct cursor), cursor_compare);

	/* Remove duplicates (cursors at same position) */
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

	/* Ensure primary cursor index is valid */
	if (editor.primary_cursor >= editor.cursor_count) {
		editor.primary_cursor = editor.cursor_count - 1;
	}
}

/*
 * Add a new cursor at the specified position with selection.
 * Returns true if added successfully.
 */
static bool multicursor_add(uint32_t row, uint32_t column,
                            uint32_t anchor_row, uint32_t anchor_column,
                            bool has_selection)
{
	if (editor.cursor_count == 0) {
		multicursor_enter();
	}

	if (editor.cursor_count >= MAX_CURSORS) {
		editor_set_status_message("Maximum cursors reached (%d)", MAX_CURSORS);
		return false;
	}

	/* Check for duplicate position */
	for (uint32_t i = 0; i < editor.cursor_count; i++) {
		if (editor.cursors[i].anchor_row == anchor_row &&
		    editor.cursors[i].anchor_column == anchor_column) {
			return false;  /* Already have cursor here */
		}
	}

	struct cursor *new_cursor = &editor.cursors[editor.cursor_count];
	new_cursor->row = row;
	new_cursor->column = column;
	new_cursor->anchor_row = anchor_row;
	new_cursor->anchor_column = anchor_column;
	new_cursor->has_selection = has_selection;

	editor.cursor_count++;

	return true;
}

/*
 * Check if a position is within any cursor's selection.
 */
static bool multicursor_selection_contains(uint32_t row, uint32_t column)
{
	if (editor.cursor_count == 0) {
		return false;
	}

	for (uint32_t i = 0; i < editor.cursor_count; i++) {
		struct cursor *cursor = &editor.cursors[i];
		if (!cursor->has_selection) {
			continue;
		}

		/* Determine selection bounds */
		uint32_t start_row, start_column, end_row, end_column;
		if (cursor->anchor_row < cursor->row ||
		    (cursor->anchor_row == cursor->row &&
		     cursor->anchor_column <= cursor->column)) {
			start_row = cursor->anchor_row;
			start_column = cursor->anchor_column;
			end_row = cursor->row;
			end_column = cursor->column;
		} else {
			start_row = cursor->row;
			start_column = cursor->column;
			end_row = cursor->anchor_row;
			end_column = cursor->anchor_column;
		}

		/* Check if position is within bounds */
		if (row < start_row || row > end_row) {
			continue;
		}
		if (row == start_row && column < start_column) {
			continue;
		}
		if (row == end_row && column >= end_column) {
			continue;
		}

		return true;
	}

	return false;
}

/*****************************************************************************
 * Selection Range Functions
 *****************************************************************************/

/* Get the normalized selection range (start always before end). The selection
 * spans from (start_row, start_col) to (end_row, end_col) where start <= end. */
static void selection_get_range(uint32_t *start_row, uint32_t *start_col,
                                uint32_t *end_row, uint32_t *end_col)
{
	uint32_t anchor_row = editor.selection_anchor_row;
	uint32_t anchor_col = editor.selection_anchor_column;
	uint32_t cursor_row = editor.cursor_row;
	uint32_t cursor_col = editor.cursor_column;

	if (anchor_row < cursor_row ||
	    (anchor_row == cursor_row && anchor_col <= cursor_col)) {
		*start_row = anchor_row;
		*start_col = anchor_col;
		*end_row = cursor_row;
		*end_col = cursor_col;
	} else {
		*start_row = cursor_row;
		*start_col = cursor_col;
		*end_row = anchor_row;
		*end_col = anchor_col;
	}
}

/* Check if a cell position is within the selection. Returns false if no
 * selection is active or if the position is outside the selected range. */
static bool selection_contains(uint32_t row, uint32_t column)
{
	if (!editor.selection_active) {
		return false;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Empty selection */
	if (start_row == end_row && start_col == end_col) {
		return false;
	}

	if (row < start_row || row > end_row) {
		return false;
	}

	if (row == start_row && row == end_row) {
		/* Single line selection */
		return column >= start_col && column < end_col;
	}

	if (row == start_row) {
		return column >= start_col;
	}

	if (row == end_row) {
		return column < end_col;
	}

	/* Row is between start and end */
	return true;
}

/* Check if the current selection is empty (anchor equals cursor). */
static bool selection_is_empty(void)
{
	if (!editor.selection_active) {
		return true;
	}
	return editor.selection_anchor_row == editor.cursor_row &&
	       editor.selection_anchor_column == editor.cursor_column;
}

/*****************************************************************************
 * Undo/Redo History
 *****************************************************************************/

/* Initialize an undo history structure. */
static void undo_history_init(struct undo_history *history)
{
	history->groups = NULL;
	history->group_count = 0;
	history->group_capacity = 0;
	history->current_index = 0;
	history->recording = false;
	history->last_edit_time.tv_sec = 0;
	history->last_edit_time.tv_nsec = 0;
}

/* Free all resources used by an undo history. */
void undo_history_free(struct undo_history *history)
{
	for (uint32_t i = 0; i < history->group_count; i++) {
		struct undo_group *group = &history->groups[i];
		for (uint32_t j = 0; j < group->operation_count; j++) {
			free(group->operations[j].text);
		}
		free(group->operations);
	}
	free(history->groups);
	undo_history_init(history);
}

/* Forward declaration */
static void undo_end_group(struct buffer *buffer);

/*
 * Begin a new undo group. If within timeout of the last edit,
 * continues the existing group. Called before making edits.
 */
static void undo_begin_group(struct buffer *buffer)
{
	struct undo_history *history = &buffer->undo_history;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	double dt = (double)(now.tv_sec - history->last_edit_time.tv_sec) +
	            (double)(now.tv_nsec - history->last_edit_time.tv_nsec) / 1.0e9;

	if (history->recording) {
		/* Already recording - check if we should continue or start new group */
		if (dt < UNDO_GROUP_TIMEOUT) {
			/* Within timeout - continue current group */
			history->last_edit_time = now;
			return;
		}
		/* Timeout passed - end current group and start new one */
		undo_end_group(buffer);
	}

	/* Check if we should continue the previous group (auto-grouping) */
	/* This applies when recording was false but we're within timeout of last edit */
	if (dt < UNDO_GROUP_TIMEOUT && history->current_index > 0 &&
	    history->current_index == history->group_count) {
		/* Continue the previous group */
		history->recording = true;
		history->last_edit_time = now;
		return;
	}

	/* Truncate any redo history (we're making a new edit) */
	for (uint32_t i = history->current_index; i < history->group_count; i++) {
		struct undo_group *group = &history->groups[i];
		for (uint32_t j = 0; j < group->operation_count; j++) {
			free(group->operations[j].text);
		}
		free(group->operations);
	}
	history->group_count = history->current_index;

	/* Grow groups array if needed */
	if (history->group_count >= history->group_capacity) {
		uint32_t new_capacity = history->group_capacity == 0 ?
		                        INITIAL_UNDO_CAPACITY : history->group_capacity * 2;
		struct undo_group *new_groups = realloc(history->groups,
		                                        new_capacity * sizeof(struct undo_group));
		if (new_groups == NULL) {
			WARN_ON_ONCE(1);  /* Undo recording disabled due to OOM */
			return;
		}
		history->groups = new_groups;
		history->group_capacity = new_capacity;
	}

	/* Initialize new group */
	struct undo_group *group = &history->groups[history->group_count];
	group->operations = NULL;
	group->operation_count = 0;
	group->operation_capacity = 0;
	group->cursor_row_before = editor.cursor_row;
	group->cursor_column_before = editor.cursor_column;
	group->cursor_row_after = editor.cursor_row;
	group->cursor_column_after = editor.cursor_column;

	history->group_count++;
	history->current_index = history->group_count;
	history->recording = true;
	history->last_edit_time = now;
}

/*
 * End the current undo group. Records final cursor position.
 */
static void undo_end_group(struct buffer *buffer)
{
	struct undo_history *history = &buffer->undo_history;

	if (!history->recording || history->group_count == 0) {
		return;
	}

	struct undo_group *group = &history->groups[history->group_count - 1];
	group->cursor_row_after = editor.cursor_row;
	group->cursor_column_after = editor.cursor_column;

	/* If group is empty, remove it */
	if (group->operation_count == 0) {
		history->group_count--;
		history->current_index = history->group_count;
	}

	history->recording = false;
}

/*
 * Add an operation to the current undo group.
 */
static void undo_record_operation(struct buffer *buffer, struct edit_operation *op)
{
	struct undo_history *history = &buffer->undo_history;

	if (!history->recording || history->group_count == 0) {
		return;
	}

	struct undo_group *group = &history->groups[history->group_count - 1];

	/* Grow operations array if needed */
	if (group->operation_count >= group->operation_capacity) {
		uint32_t new_capacity = group->operation_capacity == 0 ?
		                        INITIAL_OPERATION_CAPACITY : group->operation_capacity * 2;
		struct edit_operation *new_ops = realloc(group->operations,
		                                         new_capacity * sizeof(struct edit_operation));
		if (new_ops == NULL) {
			WARN_ON_ONCE(1);  /* Undo operation lost due to OOM */
			return;
		}
		group->operations = new_ops;
		group->operation_capacity = new_capacity;
	}

	group->operations[group->operation_count++] = *op;
}

/* Record insertion of a single character. */
static void undo_record_insert_char(struct buffer *buffer, uint32_t row,
                                    uint32_t column, uint32_t codepoint)
{
	struct edit_operation op = {
		.type = EDIT_OP_INSERT_CHAR,
		.row = row,
		.column = column,
		.codepoint = codepoint,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record deletion of a single character. */
static void undo_record_delete_char(struct buffer *buffer, uint32_t row,
                                    uint32_t column, uint32_t codepoint)
{
	struct edit_operation op = {
		.type = EDIT_OP_DELETE_CHAR,
		.row = row,
		.column = column,
		.codepoint = codepoint,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record insertion of a newline. */
static void undo_record_insert_newline(struct buffer *buffer, uint32_t row,
                                       uint32_t column)
{
	struct edit_operation op = {
		.type = EDIT_OP_INSERT_NEWLINE,
		.row = row,
		.column = column,
		.codepoint = 0,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record deletion of a newline (line join). */
static void undo_record_delete_newline(struct buffer *buffer, uint32_t row,
                                       uint32_t column)
{
	struct edit_operation op = {
		.type = EDIT_OP_DELETE_NEWLINE,
		.row = row,
		.column = column,
		.codepoint = 0,
		.text = NULL,
		.text_length = 0,
		.end_row = 0,
		.end_column = 0
	};
	undo_record_operation(buffer, &op);
}

/* Record deletion of multiple characters (selection delete). */
static void undo_record_delete_text(struct buffer *buffer,
                                    uint32_t start_row, uint32_t start_col,
                                    uint32_t end_row, uint32_t end_col,
                                    const char *text, size_t text_length)
{
	struct edit_operation op = {
		.type = EDIT_OP_DELETE_TEXT,
		.row = start_row,
		.column = start_col,
		.codepoint = 0,
		.text = NULL,
		.text_length = text_length,
		.end_row = end_row,
		.end_column = end_col
	};
	if (text_length > 0) {
		op.text = malloc(text_length + 1);
		if (op.text != NULL) {
			memcpy(op.text, text, text_length);
			op.text[text_length] = '\0';
		}
	}
	undo_record_operation(buffer, &op);
}

/*
 * Insert a cell at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_insert_cell_no_record(struct buffer *buffer, uint32_t row,
                                         uint32_t column, uint32_t codepoint)
{
	if (row > buffer->line_count) {
		row = buffer->line_count;
	}

	if (row == buffer->line_count) {
		buffer_insert_empty_line(buffer, buffer->line_count);
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);
	line_insert_cell(line, column, codepoint);
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer->is_modified = true;

	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Delete a cell at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_delete_cell_no_record(struct buffer *buffer, uint32_t row,
                                         uint32_t column)
{
	if (row >= buffer->line_count) {
		return;
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);

	if (column >= line->cell_count) {
		return;
	}

	line_delete_cell(line, column);
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer->is_modified = true;

	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Insert a newline at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_insert_newline_no_record(struct buffer *buffer, uint32_t row,
                                            uint32_t column)
{
	if (row > buffer->line_count) {
		return;
	}

	if (row == buffer->line_count) {
		buffer_insert_empty_line(buffer, buffer->line_count);
		return;
	}

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);

	if (column >= line->cell_count) {
		buffer_insert_empty_line(buffer, row + 1);
	} else {
		buffer_insert_empty_line(buffer, row + 1);
		struct line *new_line = &buffer->lines[row + 1];

		for (uint32_t i = column; i < line->cell_count; i++) {
			line_append_cell(new_line, line->cells[i].codepoint);
		}

		line->cell_count = column;
		line_set_temperature(line, LINE_TEMPERATURE_HOT);

		neighbor_compute_line(line);
		neighbor_compute_line(new_line);
		syntax_highlight_line(line, buffer, row);
		syntax_highlight_line(new_line, buffer, row + 1);
		line_invalidate_wrap_cache(line);
	}

	buffer->is_modified = true;
}

/*
 * Join line with the next line without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_join_lines_no_record(struct buffer *buffer, uint32_t row)
{
	if (row >= buffer->line_count - 1) {
		return;
	}

	struct line *line = &buffer->lines[row];
	struct line *next_line = &buffer->lines[row + 1];

	line_warm(line, buffer);
	line_warm(next_line, buffer);

	/* Append all cells from next line to current line */
	for (uint32_t i = 0; i < next_line->cell_count; i++) {
		line_append_cell(line, next_line->cells[i].codepoint);
	}

	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	buffer_delete_line(buffer, row + 1);
	buffer->is_modified = true;

	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Insert UTF-8 text at the specified position without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_insert_text_no_record(struct buffer *buffer, uint32_t row,
                                         uint32_t column, const char *text,
                                         size_t text_length)
{
	size_t offset = 0;
	uint32_t cur_row = row;
	uint32_t cur_col = column;

	while (offset < text_length) {
		uint32_t codepoint;
		int bytes = utflite_decode(text + offset, text_length - offset, &codepoint);

		if (bytes <= 0) {
			offset++;
			continue;
		}

		if (codepoint == '\n') {
			buffer_insert_newline_no_record(buffer, cur_row, cur_col);
			cur_row++;
			cur_col = 0;
		} else if (codepoint != '\r') {
			buffer_insert_cell_no_record(buffer, cur_row, cur_col, codepoint);
			cur_col++;
		}

		offset += bytes;
	}
}

/*
 * Delete a range of text without recording to undo history.
 * Used during undo/redo operations.
 */
static void buffer_delete_range_no_record(struct buffer *buffer,
                                          uint32_t start_row, uint32_t start_col,
                                          uint32_t end_row, uint32_t end_col)
{
	if (start_row == end_row) {
		/* Single line deletion */
		struct line *line = &buffer->lines[start_row];
		line_warm(line, buffer);

		for (uint32_t i = end_col; i > start_col; i--) {
			line_delete_cell(line, start_col);
		}
		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(line);
		syntax_highlight_line(line, buffer, start_row);
	} else {
		/* Multi-line deletion */
		struct line *start_line = &buffer->lines[start_row];
		struct line *end_line = &buffer->lines[end_row];

		line_warm(start_line, buffer);
		line_warm(end_line, buffer);

		/* Truncate start line at start_col */
		start_line->cell_count = start_col;

		/* Append content after end_col from end line */
		for (uint32_t i = end_col; i < end_line->cell_count; i++) {
			line_append_cell(start_line, end_line->cells[i].codepoint);
		}

		/* Delete lines from start_row+1 to end_row inclusive */
		for (uint32_t i = end_row; i > start_row; i--) {
			buffer_delete_line(buffer, i);
		}

		line_set_temperature(start_line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(start_line);
		buffer_compute_pairs(buffer);

		/* Re-highlight affected lines */
		for (uint32_t r = start_row; r < buffer->line_count; r++) {
			if (buffer->lines[r].temperature != LINE_TEMPERATURE_COLD) {
				syntax_highlight_line(&buffer->lines[r], buffer, r);
			}
		}
	}

	buffer->is_modified = true;
}

/*
 * Reverse an operation (for undo). Does not record to undo history.
 */
static void undo_reverse_operation(struct buffer *buffer, struct edit_operation *op)
{
	switch (op->type) {
		case EDIT_OP_INSERT_CHAR:
			/* Undo insert = delete */
			buffer_delete_cell_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_DELETE_CHAR:
			/* Undo delete = insert */
			buffer_insert_cell_no_record(buffer, op->row, op->column, op->codepoint);
			break;

		case EDIT_OP_INSERT_NEWLINE:
			/* Undo newline = join lines */
			buffer_join_lines_no_record(buffer, op->row);
			break;

		case EDIT_OP_DELETE_NEWLINE:
			/* Undo join = split line */
			buffer_insert_newline_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_DELETE_TEXT:
			/* Undo delete = insert the saved text */
			buffer_insert_text_no_record(buffer, op->row, op->column,
			                             op->text, op->text_length);
			break;
	}
}

/*
 * Apply an operation (for redo). Does not record to undo history.
 */
static void undo_apply_operation(struct buffer *buffer, struct edit_operation *op)
{
	switch (op->type) {
		case EDIT_OP_INSERT_CHAR:
			/* Insert the character */
			buffer_insert_cell_no_record(buffer, op->row, op->column, op->codepoint);
			break;

		case EDIT_OP_DELETE_CHAR:
			/* Delete the character */
			buffer_delete_cell_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_INSERT_NEWLINE:
			/* Split the line */
			buffer_insert_newline_no_record(buffer, op->row, op->column);
			break;

		case EDIT_OP_DELETE_NEWLINE:
			/* Join lines */
			buffer_join_lines_no_record(buffer, op->row);
			break;

		case EDIT_OP_DELETE_TEXT:
			/* Delete text range */
			buffer_delete_range_no_record(buffer, op->row, op->column,
			                              op->end_row, op->end_column);
			break;
	}
}

/*
 * Undo the most recent group of operations.
 */
static void editor_undo(void)
{
	struct undo_history *history = &editor.buffer.undo_history;

	/* End any in-progress group */
	undo_end_group(&editor.buffer);

	if (history->current_index == 0) {
		editor_set_status_message("Nothing to undo");
		return;
	}

	history->current_index--;
	struct undo_group *group = &history->groups[history->current_index];

	/* Reverse operations in reverse order */
	for (int i = (int)group->operation_count - 1; i >= 0; i--) {
		undo_reverse_operation(&editor.buffer, &group->operations[i]);
	}

	/* Restore cursor position */
	editor.cursor_row = group->cursor_row_before;
	editor.cursor_column = group->cursor_column_before;

	/* Clear selection */
	selection_clear();

	/* Recompute syntax highlighting */
	buffer_compute_pairs(&editor.buffer);
	for (uint32_t row = 0; row < editor.buffer.line_count; row++) {
		if (editor.buffer.lines[row].temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(&editor.buffer.lines[row], &editor.buffer, row);
		}
	}

	/* Update modified flag */
	editor.buffer.is_modified = (history->current_index > 0);

	editor_set_status_message("Undo");
}

/*
 * Redo the most recently undone group of operations.
 */
static void editor_redo(void)
{
	struct undo_history *history = &editor.buffer.undo_history;

	/* End any in-progress group */
	undo_end_group(&editor.buffer);

	if (history->current_index >= history->group_count) {
		editor_set_status_message("Nothing to redo");
		return;
	}

	struct undo_group *group = &history->groups[history->current_index];
	history->current_index++;

	/* Apply operations in order */
	for (uint32_t i = 0; i < group->operation_count; i++) {
		undo_apply_operation(&editor.buffer, &group->operations[i]);
	}

	/* Restore cursor position */
	editor.cursor_row = group->cursor_row_after;
	editor.cursor_column = group->cursor_column_after;

	/* Clear selection */
	selection_clear();

	/* Recompute syntax highlighting */
	buffer_compute_pairs(&editor.buffer);
	for (uint32_t row = 0; row < editor.buffer.line_count; row++) {
		if (editor.buffer.lines[row].temperature != LINE_TEMPERATURE_COLD) {
			syntax_highlight_line(&editor.buffer.lines[row], &editor.buffer, row);
		}
	}

	/* Mark as modified */
	editor.buffer.is_modified = true;

	editor_set_status_message("Redo");
}

/*****************************************************************************
 * Clipboard Integration
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

/*
 * Copy the given text to the system clipboard. Falls back to internal
 * buffer if no clipboard tool is available. Returns true on success.
 */
static bool clipboard_copy(const char *text, size_t length)
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
static char *clipboard_paste(size_t *out_length)
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
	size_t capacity = 4096;
	size_t length = 0;
	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		pclose(pipe);
		*out_length = 0;
		return NULL;
	}

	while (!feof(pipe)) {
		if (length + 1024 > capacity) {
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

		size_t read_count = fread(buffer + length, 1, 1024, pipe);
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

/*
 * Extract the currently selected text as a UTF-8 string.
 * Returns a newly allocated string that the caller must free,
 * or NULL if no selection. Sets *out_length to string length.
 */
static char *selection_get_text(size_t *out_length)
{
	if (!editor.selection_active || selection_is_empty()) {
		*out_length = 0;
		return NULL;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Estimate buffer size (4 bytes per codepoint max + newlines) */
	size_t capacity = 0;
	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);
		capacity += line->cell_count * 4 + 1;
	}
	capacity += 1;  /* Null terminator */

	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		*out_length = 0;
		return NULL;
	}

	size_t offset = 0;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		uint32_t col_start = (row == start_row) ? start_col : 0;
		uint32_t col_end = (row == end_row) ? end_col : line->cell_count;

		for (uint32_t col = col_start; col < col_end; col++) {
			uint32_t codepoint = line->cells[col].codepoint;
			char utf8[4];
			int bytes = utflite_encode(codepoint, utf8);
			if (bytes > 0) {
				memcpy(buffer + offset, utf8, bytes);
				offset += bytes;
			}
		}

		/* Add newline between lines (not after last line) */
		if (row < end_row) {
			buffer[offset++] = '\n';
		}
	}

	buffer[offset] = '\0';
	*out_length = offset;
	return buffer;
}

/*
 * Calculate the width of the line number gutter. The gutter expands to
 * fit the number of digits needed for the highest line number, with a
 * minimum of 2 digits plus one space for padding. Set to 0 when line
 * numbers are disabled.
 */
static void editor_update_gutter_width(void)
{
	if (!editor.show_line_numbers) {
		editor.gutter_width = 0;
		return;
	}

	/* Calculate digits needed for line count */
	uint32_t line_count = editor.buffer.line_count;
	if (line_count == 0) {
		line_count = 1;
	}

	uint32_t digits = 0;
	while (line_count > 0) {
		digits++;
		line_count /= 10;
	}

	/* Minimum 2 digits, plus 1 space after */
	if (digits < 2) {
		digits = 2;
	}
	editor.gutter_width = digits + 1;
}

/*
 * Query the terminal size and update editor dimensions. Falls back to
 * 80x24 if the size cannot be determined. Reserves 2 rows at the bottom
 * for the status bar and message bar.
 */
static void editor_update_screen_size(void)
{
	int ret = terminal_get_window_size(&editor.screen_rows, &editor.screen_columns);
	if (ret) {
		/* Fall back to standard 80x24 */
		editor.screen_rows = 24;
		editor.screen_columns = 80;
	}
	/* Reserve space for status bar and message bar */
	editor.screen_rows -= 2;

	/* Terminal width changed, invalidate all wrap caches. */
	buffer_invalidate_all_wrap_caches(&editor.buffer);
}

/*
 * Set a formatted status message to display at the bottom of the screen.
 * Uses printf-style formatting. The message timestamp is recorded so it
 * can be cleared after a timeout.
 */
static void editor_set_status_message(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(editor.status_message, sizeof(editor.status_message), format, arguments);
	va_end(arguments);
	editor.status_message_time = time(NULL);
}

/*****************************************************************************
 * Soft Wrap Implementation
 *****************************************************************************/

static uint32_t line_find_wrap_point(struct line *line, uint32_t start_col,
                                      uint32_t max_width, enum wrap_mode mode)
{
	if (mode == WRAP_NONE) {
		return line->cell_count;  /* No wrapping */
	}

	/* Calculate visual width from start_col */
	uint32_t visual_width = 0;
	uint32_t col = start_col;

	while (col < line->cell_count) {
		uint32_t cp = line->cells[col].codepoint;
		int width;

		if (cp == '\t') {
			width = TAB_STOP_WIDTH - ((visual_width) % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}

		if (visual_width + (uint32_t)width > max_width) {
			/* This character would exceed max_width */
			break;
		}

		visual_width += (uint32_t)width;
		col++;
	}

	/* If we fit the whole line (from start_col), no wrap needed */
	if (col >= line->cell_count) {
		return line->cell_count;
	}

	/* col is now the first character that doesn't fit */
	uint32_t hard_break = col;

	/* For character wrap, just break at the edge */
	if (mode == WRAP_CHAR) {
		/* Don't break at column 0 of segment (infinite loop) */
		return (hard_break > start_col) ? hard_break : start_col + 1;
	}

	/* For word wrap, search backward for a good break point */
	uint32_t best_break = hard_break;
	bool found_break = false;

	for (uint32_t i = hard_break; i > start_col; i--) {
		uint8_t neighbor = line->cells[i - 1].neighbor;
		enum character_class cls = neighbor_get_class(neighbor);
		enum token_position pos = neighbor_get_position(neighbor);

		/* Best: break after whitespace */
		if (cls == CHAR_CLASS_WHITESPACE) {
			best_break = i;
			found_break = true;
			break;
		}

		/* Good: break after punctuation at end of token */
		if (cls == CHAR_CLASS_PUNCTUATION &&
		    (pos == TOKEN_POSITION_END || pos == TOKEN_POSITION_SOLO)) {
			best_break = i;
			found_break = true;
			/* Keep looking for whitespace */
		}

		/* Acceptable: break at word boundary (class transition) */
		if (!found_break && i < hard_break) {
			uint8_t next_neighbor = line->cells[i].neighbor;
			enum character_class next_cls = neighbor_get_class(next_neighbor);
			if (cls != next_cls && cls != CHAR_CLASS_WHITESPACE) {
				best_break = i;
				found_break = true;
				/* Keep looking for better options */
			}
		}
	}

	/* If no good break found, fall back to hard break */
	if (!found_break || best_break <= start_col) {
		best_break = hard_break;
	}

	/* Safety: never return start_col (would cause infinite loop) */
	if (best_break <= start_col) {
		best_break = start_col + 1;
	}

	return best_break;
}

static void editor_cycle_wrap_mode(void)
{
	switch (editor.wrap_mode) {
		case WRAP_NONE:
			editor.wrap_mode = WRAP_WORD;
			editor_set_status_message("Wrap: Word");
			break;
		case WRAP_WORD:
			editor.wrap_mode = WRAP_CHAR;
			editor_set_status_message("Wrap: Character");
			break;
		case WRAP_CHAR:
			editor.wrap_mode = WRAP_NONE;
			editor_set_status_message("Wrap: Off");
			break;
	}
	buffer_invalidate_all_wrap_caches(&editor.buffer);
}

static void editor_cycle_wrap_indicator(void)
{
	switch (editor.wrap_indicator) {
		case WRAP_INDICATOR_NONE:
			editor.wrap_indicator = WRAP_INDICATOR_CORNER;
			editor_set_status_message("Wrap indicator: ⎿");
			break;
		case WRAP_INDICATOR_CORNER:
			editor.wrap_indicator = WRAP_INDICATOR_HOOK;
			editor_set_status_message("Wrap indicator: ↪");
			break;
		case WRAP_INDICATOR_HOOK:
			editor.wrap_indicator = WRAP_INDICATOR_ARROW;
			editor_set_status_message("Wrap indicator: →");
			break;
		case WRAP_INDICATOR_ARROW:
			editor.wrap_indicator = WRAP_INDICATOR_DOT;
			editor_set_status_message("Wrap indicator: ·");
			break;
		case WRAP_INDICATOR_DOT:
			editor.wrap_indicator = WRAP_INDICATOR_FLOOR;
			editor_set_status_message("Wrap indicator: ⌊");
			break;
		case WRAP_INDICATOR_FLOOR:
			editor.wrap_indicator = WRAP_INDICATOR_BOTTOM;
			editor_set_status_message("Wrap indicator: ⌞");
			break;
		case WRAP_INDICATOR_BOTTOM:
			editor.wrap_indicator = WRAP_INDICATOR_RETURN;
			editor_set_status_message("Wrap indicator: ↳");
			break;
		case WRAP_INDICATOR_RETURN:
			editor.wrap_indicator = WRAP_INDICATOR_BOX;
			editor_set_status_message("Wrap indicator: └");
			break;
		case WRAP_INDICATOR_BOX:
			editor.wrap_indicator = WRAP_INDICATOR_NONE;
			editor_set_status_message("Wrap indicator: None");
			break;
	}
}

static const char *wrap_indicator_string(enum wrap_indicator ind)
{
	switch (ind) {
		case WRAP_INDICATOR_CORNER: return "⎿";
		case WRAP_INDICATOR_HOOK:   return "↪";
		case WRAP_INDICATOR_ARROW:  return "→";
		case WRAP_INDICATOR_DOT:    return "·";
		case WRAP_INDICATOR_FLOOR:  return "⌊";
		case WRAP_INDICATOR_BOTTOM: return "⌞";
		case WRAP_INDICATOR_RETURN: return "↳";
		case WRAP_INDICATOR_BOX:    return "└";
		default:                    return " ";
	}
}

/*
 * Get the UTF-8 string for a color column style.
 * Returns NULL for background-only style.
 */
static const char *color_column_char(enum color_column_style style)
{
	switch (style) {
		case COLOR_COLUMN_SOLID:  return "│";  /* U+2502 */
		case COLOR_COLUMN_DASHED: return "┆";  /* U+2506 */
		case COLOR_COLUMN_DOTTED: return "┊";  /* U+250A */
		case COLOR_COLUMN_HEAVY:  return "┃";  /* U+2503 */
		default: return NULL;  /* Background only */
	}
}

/*
 * Get human-readable name for color column style.
 */
static const char *color_column_style_name(enum color_column_style style)
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
static void editor_cycle_color_column_style(void)
{
	if (editor.color_column == 0) {
		editor_set_status_message("Color column is off (F4 to enable)");
		return;
	}

	switch (editor.color_column_style) {
		case COLOR_COLUMN_BACKGROUND:
			editor.color_column_style = COLOR_COLUMN_SOLID;
			break;
		case COLOR_COLUMN_SOLID:
			editor.color_column_style = COLOR_COLUMN_DASHED;
			break;
		case COLOR_COLUMN_DASHED:
			editor.color_column_style = COLOR_COLUMN_DOTTED;
			break;
		case COLOR_COLUMN_DOTTED:
			editor.color_column_style = COLOR_COLUMN_HEAVY;
			break;
		case COLOR_COLUMN_HEAVY:
			editor.color_column_style = COLOR_COLUMN_BACKGROUND;
			break;
	}

	editor_set_status_message("Column %u style: %s",
	                          editor.color_column,
	                          color_column_style_name(editor.color_column_style));
}

/*
 * Get the UTF-8 string for a theme indicator style.
 */
static const char *theme_indicator_char(enum theme_indicator ind)
{
	switch (ind) {
		case THEME_INDICATOR_ASTERISK: return "*";
		case THEME_INDICATOR_BULLET:   return "●";  /* U+25CF */
		case THEME_INDICATOR_DIAMOND:  return "◆";  /* U+25C6 */
		case THEME_INDICATOR_TRIANGLE: return "▶";  /* U+25B6 */
		case THEME_INDICATOR_CHECK:    return "✓";  /* U+2713 */
		case THEME_INDICATOR_ARROW:    return "→";  /* U+2192 */
		case THEME_INDICATOR_DOT:      return "•";  /* U+2022 */
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
			editor.theme_indicator = THEME_INDICATOR_ASTERISK;
			break;
	}
}


/*
 * Compute wrap points for a line and populate the wrap cache.
 *
 * This function calculates where a line should wrap based on the text
 * area width and wrap mode. Results are cached in the line struct to
 * avoid recomputation during scrolling and rendering.
 *
 * Parameters:
 *   line       - The line to compute wrap points for
 *   buffer     - Parent buffer (needed to warm cold lines)
 *   text_width - Available width for text (screen width minus gutter)
 *   mode       - Wrap mode (NONE, WORD, or CHAR)
 */
static void line_compute_wrap_points(struct line *line, struct buffer *buffer,
                                     uint16_t text_width, enum wrap_mode mode)
{
	/*
	 * Check if cache is still valid. A cache hit requires matching
	 * width, mode, and the cache must have been computed at least once.
	 */
	if (line->wrap_cache_width == text_width &&
	    line->wrap_cache_mode == mode &&
	    line->wrap_segment_count > 0) {
		return;
	}

	/* Invalidate any stale cache data before recomputing. */
	line_invalidate_wrap_cache(line);

	/* For no-wrap mode, line is a single segment. */
	if (mode == WRAP_NONE || text_width == 0) {
		line->wrap_columns = malloc(sizeof(uint32_t));
		line->wrap_columns[0] = 0;
		line->wrap_segment_count = 1;
		line->wrap_cache_width = text_width;
		line->wrap_cache_mode = mode;
		return;
	}

	/* Ensure line is warm so we can access cells. */
	line_warm(line, buffer);

	/*
	 * First pass: count how many segments we need.
	 * Start with segment 0 at column 0, then find each wrap point.
	 */
	uint32_t segment_count = 1;
	uint32_t column = 0;

	while (column < line->cell_count) {
		uint32_t wrap_point = line_find_wrap_point(line, column,
		                                           text_width, mode);
		if (wrap_point >= line->cell_count) {
			break;
		}
		segment_count++;
		column = wrap_point;
	}

	/* Allocate array for segment start columns. */
	line->wrap_columns = malloc(segment_count * sizeof(uint32_t));

	/*
	 * Second pass: record the actual wrap columns.
	 * wrap_columns[i] is where segment i starts.
	 */
	line->wrap_columns[0] = 0;
	column = 0;

	for (uint16_t segment = 1; segment < segment_count; segment++) {
		uint32_t wrap_point = line_find_wrap_point(line, column,
		                                           text_width, mode);
		line->wrap_columns[segment] = wrap_point;
		column = wrap_point;
	}

	line->wrap_segment_count = segment_count;
	line->wrap_cache_width = text_width;
	line->wrap_cache_mode = mode;
}

/*
 * Get the text area width (screen width minus gutter).
 * Used for computing wrap points.
 */
static uint16_t editor_get_text_width(void)
{
	return editor.screen_columns > editor.gutter_width
		? editor.screen_columns - editor.gutter_width
		: 1;
}

/*
 * Ensure wrap cache is computed for a line.
 * Uses current editor wrap settings and text width.
 */
static void line_ensure_wrap_cache(struct line *line, struct buffer *buffer)
{
	uint16_t text_width = editor_get_text_width();
	line_compute_wrap_points(line, buffer, text_width, editor.wrap_mode);
}

/*
 * Find which segment of a line contains the given column.
 * Ensures wrap cache is computed first.
 * Returns 0 for first segment, up to segment_count-1 for last.
 */
static uint16_t line_get_segment_for_column(struct line *line,
                                            struct buffer *buffer,
                                            uint32_t column)
{
	line_ensure_wrap_cache(line, buffer);

	if (line->wrap_segment_count <= 1) {
		return 0;
	}

	/*
	 * Binary search for the segment containing this column.
	 * wrap_columns[i] is where segment i starts, so we want
	 * the largest i where wrap_columns[i] <= column.
	 */
	uint16_t low = 0;
	uint16_t high = line->wrap_segment_count - 1;

	while (low < high) {
		uint16_t mid = (low + high + 1) / 2;
		if (line->wrap_columns[mid] <= column) {
			low = mid;
		} else {
			high = mid - 1;
		}
	}

	return low;
}

/*
 * Get the start column for a specific segment of a line.
 * Ensures wrap cache is computed first.
 */
static uint32_t line_get_segment_start(struct line *line,
                                       struct buffer *buffer,
                                       uint16_t segment)
{
	line_ensure_wrap_cache(line, buffer);

	if (segment >= line->wrap_segment_count) {
		segment = line->wrap_segment_count - 1;
	}

	return line->wrap_columns[segment];
}

/*
 * Get the end column (exclusive) for a specific segment of a line.
 * Ensures wrap cache is computed first.
 */
static uint32_t line_get_segment_end(struct line *line,
                                     struct buffer *buffer,
                                     uint16_t segment)
{
	line_ensure_wrap_cache(line, buffer);

	if (segment >= line->wrap_segment_count) {
		return line->cell_count;
	}

	if (segment + 1 < line->wrap_segment_count) {
		return line->wrap_columns[segment + 1];
	}

	return line->cell_count;
}

/*
 * Calculate the visual column within a segment for a given cell column.
 * Returns the rendered width from segment start to the cell.
 */
static uint32_t line_get_visual_column_in_segment(struct line *line,
                                                   struct buffer *buffer,
                                                   uint16_t segment,
                                                   uint32_t cell_column)
{
	line_ensure_wrap_cache(line, buffer);
	line_warm(line, buffer);

	uint32_t segment_start = line_get_segment_start(line, buffer, segment);
	uint32_t segment_end = line_get_segment_end(line, buffer, segment);

	/* Clamp cell_column to segment bounds */
	if (cell_column < segment_start) {
		cell_column = segment_start;
	}
	if (cell_column > segment_end) {
		cell_column = segment_end;
	}

	/* Calculate visual width from segment start to cell_column */
	uint32_t visual_col = 0;
	uint32_t absolute_visual = 0;

	/* First calculate visual column at segment start (for tab alignment) */
	for (uint32_t i = 0; i < segment_start && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		absolute_visual += width;
	}

	/* Now calculate visual width within segment */
	for (uint32_t i = segment_start; i < cell_column && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		visual_col += width;
		absolute_visual += width;
	}

	return visual_col;
}

/*
 * Find the cell column at a given visual column within a segment.
 * Used for maintaining visual position when moving between segments.
 */
static uint32_t line_find_column_at_visual(struct line *line,
                                           struct buffer *buffer,
                                           uint16_t segment,
                                           uint32_t target_visual)
{
	line_ensure_wrap_cache(line, buffer);
	line_warm(line, buffer);

	uint32_t segment_start = line_get_segment_start(line, buffer, segment);
	uint32_t segment_end = line_get_segment_end(line, buffer, segment);

	/* Calculate visual column at segment start (for tab alignment) */
	uint32_t absolute_visual = 0;
	for (uint32_t i = 0; i < segment_start && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		absolute_visual += width;
	}

	/* Find the column at target_visual within the segment */
	uint32_t visual_col = 0;
	uint32_t col = segment_start;

	while (col < segment_end && col < line->cell_count) {
		if (visual_col >= target_visual) {
			break;
		}
		uint32_t cp = line->cells[col].codepoint;
		int width;
		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (absolute_visual % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) width = 1;
		}
		visual_col += width;
		absolute_visual += width;
		col++;
	}

	return col;
}

/*
 * Map a screen row (relative to row_offset) to logical line and segment.
 * Used for converting mouse click positions to buffer positions.
 *
 * Returns true if the screen row maps to valid content, false if it's
 * past the end of the buffer. On success, sets *out_line and *out_segment.
 */
static bool screen_row_to_line_segment(uint32_t screen_row,
                                       uint32_t *out_line,
                                       uint16_t *out_segment)
{
	if (editor.wrap_mode == WRAP_NONE) {
		/* No wrap: direct mapping */
		uint32_t file_row = screen_row + editor.row_offset;
		if (file_row >= editor.buffer.line_count) {
			return false;
		}
		*out_line = file_row;
		*out_segment = 0;
		return true;
	}

	/* With wrap: iterate through lines and segments */
	uint32_t file_row = editor.row_offset;
	uint16_t segment = 0;
	uint32_t screen_pos = 0;

	while (file_row < editor.buffer.line_count && screen_pos <= screen_row) {
		struct line *line = &editor.buffer.lines[file_row];
		line_ensure_wrap_cache(line, &editor.buffer);

		/* Check each segment of this line */
		for (segment = 0; segment < line->wrap_segment_count; segment++) {
			if (screen_pos == screen_row) {
				*out_line = file_row;
				*out_segment = segment;
				return true;
			}
			screen_pos++;
		}
		file_row++;
	}

	return false;
}

/*
 * Calculate the maximum row_offset that ensures all content is reachable.
 * In wrap mode, finds the first logical line such that the total screen
 * rows from that line to the end fill at most screen_rows.
 */
static uint32_t calculate_max_row_offset(void)
{
	if (editor.wrap_mode == WRAP_NONE) {
		/* Simple case: 1 line = 1 screen row */
		if (editor.buffer.line_count > editor.screen_rows) {
			return editor.buffer.line_count - editor.screen_rows;
		}
		return 0;
	}

	/*
	 * With wrap: work backwards from last line, summing screen rows
	 * until we exceed screen_rows. The line after that is max_offset.
	 */
	uint32_t screen_rows_from_end = 0;
	uint32_t candidate = editor.buffer.line_count;

	while (candidate > 0) {
		candidate--;
		struct line *line = &editor.buffer.lines[candidate];
		line_ensure_wrap_cache(line, &editor.buffer);
		screen_rows_from_end += line->wrap_segment_count;

		if (screen_rows_from_end >= editor.screen_rows) {
			/* This line and all after fill the screen */
			return candidate;
		}
	}

	/* All content fits on one screen */
	return 0;
}

/*
 * Return the number of cells in a line. This is the logical length used
 * for cursor positioning, not the rendered width. For cold lines, counts
 * codepoints without allocating cells. Returns 0 for invalid row numbers.
 */
static uint32_t editor_get_line_length(uint32_t row)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}
	return line_get_cell_count(&editor.buffer.lines[row], &editor.buffer);
}

/*
 * Convert a cell column position to a rendered screen column. Accounts
 * for tab expansion and character display widths (CJK characters take 2
 * columns, combining marks take 0). Warms the line to access cells.
 */
static uint32_t editor_get_render_column(uint32_t row, uint32_t column)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);
	uint32_t render_column = 0;

	for (uint32_t i = 0; i < column && i < line->cell_count; i++) {
		uint32_t codepoint = line->cells[i].codepoint;

		if (codepoint == '\t') {
			render_column += TAB_STOP_WIDTH - (render_column % TAB_STOP_WIDTH);
		} else {
			int width = utflite_codepoint_width(codepoint);
			if (width < 0) {
				width = 1;  /* Control characters */
			}
			render_column += width;
		}
	}

	return render_column;
}

/*
 * Adjust scroll offsets to keep the cursor visible on screen. Handles
 * both vertical scrolling (row_offset) and horizontal scrolling
 * (column_offset). In wrap mode, accounts for multi-segment lines.
 */
static void editor_scroll(void)
{
	/* Vertical scrolling (skip if selection active - allow cursor off-screen) */
	if (!editor.selection_active) {
		if (editor.cursor_row < editor.row_offset) {
			editor.row_offset = editor.cursor_row;
		}

		if (editor.wrap_mode == WRAP_NONE) {
			/* No wrap: simple check */
			if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
				editor.row_offset = editor.cursor_row - editor.screen_rows + 1;
			}
		} else {
			/*
			 * Wrap enabled: calculate screen row of cursor and check
			 * if it's past the visible area.
			 */
			uint32_t screen_row = 0;
			for (uint32_t row = editor.row_offset;
			     row <= editor.cursor_row && row < editor.buffer.line_count;
			     row++) {
				struct line *line = &editor.buffer.lines[row];
				line_ensure_wrap_cache(line, &editor.buffer);
				if (row == editor.cursor_row) {
					/* Add cursor's segment within this line */
					uint16_t cursor_segment = line_get_segment_for_column(
						line, &editor.buffer, editor.cursor_column);
					screen_row += cursor_segment + 1;
				} else {
					screen_row += line->wrap_segment_count;
				}
			}

			/* If cursor is past visible area, scroll down */
			while (screen_row > editor.screen_rows &&
			       editor.row_offset < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[editor.row_offset];
				line_ensure_wrap_cache(line, &editor.buffer);
				screen_row -= line->wrap_segment_count;
				editor.row_offset++;
			}
		}
	}

	/* Horizontal scrolling - only applies in WRAP_NONE mode */
	if (editor.wrap_mode == WRAP_NONE) {
		uint32_t render_column = editor_get_render_column(
			editor.cursor_row, editor.cursor_column);
		uint32_t text_area_width = editor.screen_columns - editor.gutter_width;
		if (render_column < editor.column_offset) {
			editor.column_offset = render_column;
		}
		if (render_column >= editor.column_offset + text_area_width) {
			editor.column_offset = render_column - text_area_width + 1;
		}
	} else {
		/* In wrap mode, no horizontal scrolling needed */
		editor.column_offset = 0;
	}

	/* Request background warming for new viewport */
	editor_request_background_warming();
}

/*
 * Handle cursor movement from arrow keys, Home, End, Page Up/Down.
 * Left/Right navigate by grapheme cluster within a line and wrap to
 * adjacent lines at boundaries. Up/Down move vertically. After movement,
 * the cursor is snapped to end of line if it would be past the line length.
 * Shift+key extends selection, plain movement clears selection.
 */
static void editor_move_cursor(int key)
{
	/* Determine if this is a selection-extending key and get base key */
	bool extend_selection = false;
	int base_key = key;

	switch (key) {
		case KEY_SHIFT_ARROW_UP:
			extend_selection = true;
			base_key = KEY_ARROW_UP;
			break;
		case KEY_SHIFT_ARROW_DOWN:
			extend_selection = true;
			base_key = KEY_ARROW_DOWN;
			break;
		case KEY_SHIFT_ARROW_LEFT:
			extend_selection = true;
			base_key = KEY_ARROW_LEFT;
			break;
		case KEY_SHIFT_ARROW_RIGHT:
			extend_selection = true;
			base_key = KEY_ARROW_RIGHT;
			break;
		case KEY_SHIFT_HOME:
			extend_selection = true;
			base_key = KEY_HOME;
			break;
		case KEY_SHIFT_END:
			extend_selection = true;
			base_key = KEY_END;
			break;
		case KEY_SHIFT_PAGE_UP:
			extend_selection = true;
			base_key = KEY_PAGE_UP;
			break;
		case KEY_SHIFT_PAGE_DOWN:
			extend_selection = true;
			base_key = KEY_PAGE_DOWN;
			break;
		case KEY_CTRL_SHIFT_ARROW_LEFT:
			extend_selection = true;
			base_key = KEY_CTRL_ARROW_LEFT;
			break;
		case KEY_CTRL_SHIFT_ARROW_RIGHT:
			extend_selection = true;
			base_key = KEY_CTRL_ARROW_RIGHT;
			break;
	}

	/* Handle selection start/clear */
	if (extend_selection) {
		if (!editor.selection_active) {
			selection_start();
		}
	} else {
		selection_clear();
	}

	uint32_t line_length = editor_get_line_length(editor.cursor_row);
	struct line *current_line = editor.cursor_row < editor.buffer.line_count
	                            ? &editor.buffer.lines[editor.cursor_row] : NULL;

	switch (base_key) {
		case KEY_ARROW_LEFT:
			if (editor.cursor_column > 0 && current_line) {
				editor.cursor_column = cursor_prev_grapheme(current_line, &editor.buffer, editor.cursor_column);
			} else if (editor.cursor_row > 0) {
				editor.cursor_row--;
				editor.cursor_column = editor_get_line_length(editor.cursor_row);
			}
			break;

		case KEY_ARROW_RIGHT:
			if (editor.cursor_column < line_length && current_line) {
				editor.cursor_column = cursor_next_grapheme(current_line, &editor.buffer, editor.cursor_column);
			} else if (editor.cursor_row < editor.buffer.line_count - 1) {
				editor.cursor_row++;
				editor.cursor_column = 0;
			}
			break;

		case KEY_ARROW_UP:
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: move by logical line */
				if (editor.cursor_row > 0) {
					editor.cursor_row--;
				}
			} else if (current_line) {
				/* Wrap enabled: move by screen row (segment) */
				uint16_t cur_segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t visual_col = line_get_visual_column_in_segment(
					current_line, &editor.buffer, cur_segment,
					editor.cursor_column);

				if (cur_segment > 0) {
					/* Move to previous segment of same line */
					uint32_t new_col = line_find_column_at_visual(
						current_line, &editor.buffer,
						cur_segment - 1, visual_col);
					editor.cursor_column = new_col;
				} else if (editor.cursor_row > 0) {
					/* Move to last segment of previous line */
					editor.cursor_row--;
					struct line *prev_line = &editor.buffer.lines[editor.cursor_row];
					line_ensure_wrap_cache(prev_line, &editor.buffer);
					uint16_t last_segment = prev_line->wrap_segment_count - 1;
					uint32_t new_col = line_find_column_at_visual(
						prev_line, &editor.buffer, last_segment, visual_col);
					editor.cursor_column = new_col;
				}
			}
			break;

		case KEY_ARROW_DOWN:
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: move by logical line */
				if (editor.cursor_row < editor.buffer.line_count - 1) {
					editor.cursor_row++;
				}
			} else if (current_line) {
				/* Wrap enabled: move by screen row (segment) */
				line_ensure_wrap_cache(current_line, &editor.buffer);
				uint16_t cur_segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t visual_col = line_get_visual_column_in_segment(
					current_line, &editor.buffer, cur_segment,
					editor.cursor_column);

				if (cur_segment + 1 < current_line->wrap_segment_count) {
					/* Move to next segment of same line */
					uint32_t new_col = line_find_column_at_visual(
						current_line, &editor.buffer,
						cur_segment + 1, visual_col);
					editor.cursor_column = new_col;
				} else if (editor.cursor_row < editor.buffer.line_count - 1) {
					/* Move to first segment of next line */
					editor.cursor_row++;
					struct line *next_line = &editor.buffer.lines[editor.cursor_row];
					uint32_t new_col = line_find_column_at_visual(
						next_line, &editor.buffer, 0, visual_col);
					editor.cursor_column = new_col;
				}
			} else if (editor.buffer.line_count == 0) {
				/* Allow being on line 0 even with empty buffer */
			}
			break;

		case KEY_CTRL_ARROW_LEFT:
			if (current_line) {
				line_warm(current_line, &editor.buffer);
				uint32_t old_col = editor.cursor_column;
				editor.cursor_column = find_prev_word_start(current_line,
					editor.cursor_column);
				/* If stuck at start of line, wrap to previous line */
				if (editor.cursor_column == 0 && old_col == 0 &&
				    editor.cursor_row > 0) {
					editor.cursor_row--;
					struct line *prev = &editor.buffer.lines[editor.cursor_row];
					line_warm(prev, &editor.buffer);
					editor.cursor_column = prev->cell_count;
				}
			}
			break;

		case KEY_CTRL_ARROW_RIGHT:
			if (current_line) {
				line_warm(current_line, &editor.buffer);
				uint32_t old_col = editor.cursor_column;
				uint32_t len = current_line->cell_count;
				editor.cursor_column = find_next_word_start(current_line,
					editor.cursor_column);
				/* If stuck at end of line, wrap to next line */
				if (editor.cursor_column == len && old_col == len &&
				    editor.cursor_row < editor.buffer.line_count - 1) {
					editor.cursor_row++;
					editor.cursor_column = 0;
				}
			}
			break;

		case KEY_HOME:
			if (editor.wrap_mode == WRAP_NONE || !current_line) {
				/* No wrap: go to start of logical line */
				editor.cursor_column = 0;
			} else {
				/* Wrap enabled: go to start of current segment */
				uint16_t segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t segment_start = line_get_segment_start(
					current_line, &editor.buffer, segment);
				if (editor.cursor_column == segment_start && segment > 0) {
					/* Already at segment start, go to previous segment start */
					segment_start = line_get_segment_start(
						current_line, &editor.buffer, segment - 1);
				}
				editor.cursor_column = segment_start;
			}
			break;

		case KEY_END:
			if (editor.wrap_mode == WRAP_NONE || !current_line) {
				/* No wrap: go to end of logical line */
				editor.cursor_column = line_length;
			} else {
				/* Wrap enabled: go to end of current segment */
				line_ensure_wrap_cache(current_line, &editor.buffer);
				uint16_t segment = line_get_segment_for_column(
					current_line, &editor.buffer, editor.cursor_column);
				uint32_t segment_end = line_get_segment_end(
					current_line, &editor.buffer, segment);
				if (editor.cursor_column == segment_end &&
				    segment + 1 < current_line->wrap_segment_count) {
					/* Already at segment end, go to next segment end */
					segment_end = line_get_segment_end(
						current_line, &editor.buffer, segment + 1);
				}
				editor.cursor_column = segment_end;
			}
			break;

		case KEY_PAGE_UP:
			if (editor.cursor_row > editor.screen_rows) {
				editor.cursor_row -= editor.screen_rows;
			} else {
				editor.cursor_row = 0;
			}
			break;

		case KEY_PAGE_DOWN:
			if (editor.cursor_row + editor.screen_rows < editor.buffer.line_count) {
				editor.cursor_row += editor.screen_rows;
			} else if (editor.buffer.line_count > 0) {
				editor.cursor_row = editor.buffer.line_count - 1;
			}
			break;
	}

	/* Snap cursor to end of line if it's past the line length */
	line_length = editor_get_line_length(editor.cursor_row);
	if (editor.cursor_column > line_length) {
		editor.cursor_column = line_length;
	}
}

/*
 * Delete the currently selected text. Handles both single-line and
 * multi-line selections. Moves cursor to the start of the deleted region.
 */
static void editor_delete_selection(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		return;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Save text before deleting for undo */
	size_t text_length;
	char *text = selection_get_text(&text_length);
	if (text != NULL) {
		undo_record_delete_text(&editor.buffer, start_row, start_col,
		                        end_row, end_col, text, text_length);
		free(text);
	}

	if (start_row == end_row) {
		/* Single line selection - delete cells from end to start */
		struct line *line = &editor.buffer.lines[start_row];
		line_warm(line, &editor.buffer);

		for (uint32_t i = end_col; i > start_col; i--) {
			line_delete_cell(line, start_col);
		}
		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, start_row);
	} else {
		/* Multi-line selection */
		/* 1. Truncate start line at start_col */
		struct line *start_line = &editor.buffer.lines[start_row];
		line_warm(start_line, &editor.buffer);
		start_line->cell_count = start_col;

		/* 2. Append content after end_col from end line */
		struct line *end_line = &editor.buffer.lines[end_row];
		line_warm(end_line, &editor.buffer);

		int ret = 0;
		for (uint32_t i = end_col; i < end_line->cell_count; i++) {
			ret = line_append_cell_checked(start_line, end_line->cells[i].codepoint);
			if (ret) {
				editor_set_status_message("Delete failed: %s", edit_strerror(ret));
				break;
			}
		}

		/* 3. Delete lines from start_row+1 to end_row inclusive */
		for (uint32_t i = end_row; i > start_row; i--) {
			buffer_delete_line(&editor.buffer, i);
		}

		line_set_temperature(start_line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(start_line);
		buffer_compute_pairs(&editor.buffer);

		/* Re-highlight affected lines */
		for (uint32_t row = start_row; row < editor.buffer.line_count; row++) {
			if (editor.buffer.lines[row].temperature != LINE_TEMPERATURE_COLD) {
				syntax_highlight_line(&editor.buffer.lines[row], &editor.buffer, row);
			}
		}
	}

	/* Move cursor to start of deleted region */
	editor.cursor_row = start_row;
	editor.cursor_column = start_col;

	editor.buffer.is_modified = true;
	selection_clear();
}

/*
 * Copy the current selection to the clipboard without deleting it.
 */
static void editor_copy(void)
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
static void editor_cut(void)
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
static void editor_paste(void)
{
	size_t length;
	char *text = clipboard_paste(&length);

	if (text == NULL || length == 0) {
		editor_set_status_message("Clipboard empty");
		free(text);
		return;
	}

	undo_begin_group(&editor.buffer);

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

	undo_end_group(&editor.buffer);

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

/*
 * Insert a character at the current cursor position and advance the
 * cursor. If there's an active selection, deletes it first. Resets
 * the quit confirmation counter since the buffer was modified.
 */
static void editor_insert_character(uint32_t codepoint)
{
	undo_begin_group(&editor.buffer);

	/* Delete selection first if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	undo_record_insert_char(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	int ret = buffer_insert_cell_at_column_checked(&editor.buffer, editor.cursor_row,
	                                                editor.cursor_column, codepoint);
	if (ret) {
		editor_set_status_message("Insert failed: %s", edit_strerror(ret));
		undo_end_group(&editor.buffer);
		return;
	}
	editor.cursor_column++;

	undo_end_group(&editor.buffer);
}

/*
 * Handle Enter key by splitting the current line at the cursor position.
 * Moves cursor to the beginning of the newly created line below. If there's
 * an active selection, deletes it first. The new line inherits the leading
 * whitespace (tabs and spaces) from the original line for auto-indentation.
 */
static void editor_insert_newline(void)
{
	undo_begin_group(&editor.buffer);

	/*
	 * Capture leading whitespace before any modifications. We limit the
	 * indent to the cursor column since content before cursor stays on
	 * the current line.
	 */
	uint32_t indent_count = 0;
	uint32_t indent_chars[256];

	if (editor.cursor_row < editor.buffer.line_count) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		line_warm(line, &editor.buffer);

		while (indent_count < line->cell_count && indent_count < 256) {
			uint32_t codepoint = line->cells[indent_count].codepoint;
			if (codepoint != ' ' && codepoint != '\t') {
				break;
			}
			indent_chars[indent_count] = codepoint;
			indent_count++;
		}

		/* Don't copy more indent than cursor position */
		if (indent_count > editor.cursor_column) {
			indent_count = editor.cursor_column;
		}
	}

	/* Delete selection first if active */
	if (editor.selection_active && !selection_is_empty()) {
		editor_delete_selection();
		/* After deletion, re-evaluate indent based on new cursor position */
		indent_count = 0;
		if (editor.cursor_row < editor.buffer.line_count) {
			struct line *line = &editor.buffer.lines[editor.cursor_row];
			line_warm(line, &editor.buffer);

			while (indent_count < line->cell_count && indent_count < 256) {
				uint32_t codepoint = line->cells[indent_count].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				indent_chars[indent_count] = codepoint;
				indent_count++;
			}

			if (indent_count > editor.cursor_column) {
				indent_count = editor.cursor_column;
			}
		}
	}

	undo_record_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	int ret = buffer_insert_newline_checked(&editor.buffer, editor.cursor_row,
	                                         editor.cursor_column);
	if (ret) {
		editor_set_status_message("Cannot insert line: %s", edit_strerror(ret));
		undo_end_group(&editor.buffer);
		return;
	}
	editor.cursor_row++;
	editor.cursor_column = 0;

	/* Insert auto-indent characters on the new line */
	for (uint32_t i = 0; i < indent_count; i++) {
		undo_record_insert_char(&editor.buffer, editor.cursor_row, editor.cursor_column, indent_chars[i]);
		ret = buffer_insert_cell_at_column_checked(&editor.buffer, editor.cursor_row,
		                                            editor.cursor_column, indent_chars[i]);
		if (ret) {
			editor_set_status_message("Auto-indent failed: %s", edit_strerror(ret));
			undo_end_group(&editor.buffer);
			return;
		}
		editor.cursor_column++;
	}


	undo_end_group(&editor.buffer);
}

/*
 * Handle Delete key by removing the grapheme cluster at the cursor
 * position. If there's an active selection, deletes the selection instead.
 * Does nothing if cursor is past the end of the buffer.
 */
static void editor_delete_character(void)
{
	/* Delete selection if active */
	if (editor.selection_active && !selection_is_empty()) {
		undo_begin_group(&editor.buffer);
		editor_delete_selection();
		undo_end_group(&editor.buffer);
		return;
	}

	if (editor.cursor_row >= editor.buffer.line_count) {
		return;
	}

	undo_begin_group(&editor.buffer);

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	if (editor.cursor_column < line->cell_count) {
		/* Deleting a character - record it before deletion */
		uint32_t codepoint = line->cells[editor.cursor_column].codepoint;
		undo_record_delete_char(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
	} else if (editor.cursor_row + 1 < editor.buffer.line_count) {
		/* Joining with next line - record newline deletion */
		undo_record_delete_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	}

	int ret = buffer_delete_grapheme_at_column_checked(&editor.buffer, editor.cursor_row,
	                                                     editor.cursor_column);
	if (ret) {
		editor_set_status_message("Delete failed: %s", edit_strerror(ret));
		undo_end_group(&editor.buffer);
		return;
	}

	undo_end_group(&editor.buffer);
}

/*
 * Handle Backspace key. If there's an active selection, deletes the
 * selection. Otherwise, if within a line, deletes the grapheme cluster
 * before the cursor. If at the start of a line, joins this line with
 * the previous line. Does nothing at the start of the buffer.
 */
static void editor_handle_backspace(void)
{
	/* Delete selection if active */
	if (editor.selection_active && !selection_is_empty()) {
		undo_begin_group(&editor.buffer);
		editor_delete_selection();
		undo_end_group(&editor.buffer);
		return;
	}

	if (editor.cursor_row == 0 && editor.cursor_column == 0) {
		return;
	}

	undo_begin_group(&editor.buffer);

	int ret;
	if (editor.cursor_column > 0) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		line_warm(line, &editor.buffer);
		uint32_t new_column = cursor_prev_grapheme(line, &editor.buffer, editor.cursor_column);
		/* Record the character we're about to delete */
		uint32_t codepoint = line->cells[new_column].codepoint;
		undo_record_delete_char(&editor.buffer, editor.cursor_row, new_column, codepoint);
		editor.cursor_column = new_column;
		ret = buffer_delete_grapheme_at_column_checked(&editor.buffer, editor.cursor_row,
		                                                editor.cursor_column);
		if (ret) {
			editor_set_status_message("Delete failed: %s", edit_strerror(ret));
			undo_end_group(&editor.buffer);
			return;
		}
	} else {
		/* Join with previous line - record newline deletion at end of previous line */
		uint32_t previous_line_length = editor_get_line_length(editor.cursor_row - 1);
		undo_record_delete_newline(&editor.buffer, editor.cursor_row - 1, previous_line_length);
		struct line *previous_line = &editor.buffer.lines[editor.cursor_row - 1];
		struct line *current_line = &editor.buffer.lines[editor.cursor_row];
		line_warm(previous_line, &editor.buffer);
		line_warm(current_line, &editor.buffer);
		ret = line_append_cells_from_line_checked(previous_line, current_line);
		if (ret) {
			editor_set_status_message("Join lines failed: %s", edit_strerror(ret));
			undo_end_group(&editor.buffer);
			return;
		}
		line_set_temperature(previous_line, LINE_TEMPERATURE_HOT);
		buffer_delete_line(&editor.buffer, editor.cursor_row);
		editor.cursor_row--;
		editor.cursor_column = previous_line_length;
	}

	undo_end_group(&editor.buffer);

}

/*****************************************************************************
 * Multi-Cursor Editing
 *****************************************************************************/

/*
 * Insert a character at all cursor positions.
 * Processes cursors in reverse order to preserve positions.
 */
static void multicursor_insert_character(uint32_t codepoint)
{
	if (editor.cursor_count == 0) {
		editor_insert_character(codepoint);
		return;
	}

	undo_begin_group(&editor.buffer);

	/* Process in reverse order (bottom to top) so positions stay valid */
	for (int i = (int)editor.cursor_count - 1; i >= 0; i--) {
		struct cursor *cursor = &editor.cursors[i];

		/* TODO: Delete selection at this cursor if any */

		/* Insert character */
		undo_record_insert_char(&editor.buffer, cursor->row,
		                        cursor->column, codepoint);
		buffer_insert_cell_at_column(&editor.buffer, cursor->row,
		                             cursor->column, codepoint);

		/* Update cursor position */
		cursor->column++;
		cursor->anchor_row = cursor->row;
		cursor->anchor_column = cursor->column;
		cursor->has_selection = false;

		/* Adjust all earlier cursors on the same line */
		for (int j = i - 1; j >= 0; j--) {
			if (editor.cursors[j].row == cursor->row) {
				editor.cursors[j].column++;
				if (editor.cursors[j].anchor_row == cursor->row) {
					editor.cursors[j].anchor_column++;
				}
			}
		}
	}

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	/* Update primary cursor in legacy fields for rendering */
	struct cursor *primary = &editor.cursors[editor.primary_cursor];
	editor.cursor_row = primary->row;
	editor.cursor_column = primary->column;
}

/*
 * Delete character before all cursor positions (backspace).
 * Processes cursors in reverse order to preserve positions.
 */
static void multicursor_backspace(void)
{
	if (editor.cursor_count == 0) {
		editor_handle_backspace();
		return;
	}

	undo_begin_group(&editor.buffer);

	/* Process in reverse order */
	for (int i = (int)editor.cursor_count - 1; i >= 0; i--) {
		struct cursor *cursor = &editor.cursors[i];

		/* TODO: Delete selection at this cursor if any */

		if (cursor->column == 0 && cursor->row == 0) {
			continue;  /* Nothing to delete at buffer start */
		}

		if (cursor->column > 0) {
			/* Delete character before cursor on same line */
			struct line *line = &editor.buffer.lines[cursor->row];
			line_warm(line, &editor.buffer);

			uint32_t delete_column = cursor->column - 1;
			uint32_t deleted_codepoint = line->cells[delete_column].codepoint;

			undo_record_delete_char(&editor.buffer, cursor->row,
			                        delete_column, deleted_codepoint);
			line_delete_cell(line, delete_column);

			cursor->column--;
			cursor->anchor_column = cursor->column;

			/* Update line metadata */
			neighbor_compute_line(line);
			syntax_highlight_line(line, &editor.buffer, cursor->row);
			line_invalidate_wrap_cache(line);

			/* Adjust earlier cursors on same line */
			for (int j = i - 1; j >= 0; j--) {
				if (editor.cursors[j].row == cursor->row &&
				    editor.cursors[j].column > delete_column) {
					editor.cursors[j].column--;
				}
				if (editor.cursors[j].anchor_row == cursor->row &&
				    editor.cursors[j].anchor_column > delete_column) {
					editor.cursors[j].anchor_column--;
				}
			}
		} else {
			/* At start of line - skip line joining in multi-cursor mode */
			/* This is a limitation we accept for Phase 20 */
		}
	}

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	multicursor_normalize();

	/* Update primary cursor in legacy fields */
	if (editor.cursor_count > 0) {
		struct cursor *primary = &editor.cursors[editor.primary_cursor];
		editor.cursor_row = primary->row;
		editor.cursor_column = primary->column;
	}
}

/*****************************************************************************
 * File Operations
 *****************************************************************************/

/*
 * Save the buffer to disk using Ctrl-S. Displays an error in the status
 * bar if no filename is set or if the save fails. Resets the quit
 * confirmation counter on success.
 */
static void editor_save(void)
{
	if (editor.buffer.filename == NULL) {
		editor_set_status_message("No filename specified");
		return;
	}

	int ret = file_save(&editor.buffer);
	if (ret) {
		editor_set_status_message("Save failed: %s", edit_strerror(ret));
	} else {
		/* Success message is set by file_save */
	}
}

/*
 * Convert a visual screen column to a cell index within a line. Accounts
 * for tab expansion and character widths. Used for mouse click positioning.
 */
static uint32_t screen_column_to_cell(uint32_t row, uint32_t target_visual_column)
{
	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	uint32_t visual_column = 0;
	uint32_t cell_index = 0;

	while (cell_index < line->cell_count && visual_column < target_visual_column) {
		uint32_t cp = line->cells[cell_index].codepoint;
		int width;

		if (cp == '\t') {
			width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
		} else {
			width = utflite_codepoint_width(cp);
			if (width < 0) {
				width = 1;
			}
		}

		/* If clicking in the middle of a wide character, round to nearest */
		if (visual_column + width > target_visual_column) {
			if (target_visual_column - visual_column > (uint32_t)width / 2) {
				cell_index++;
			}
			break;
		}

		visual_column += width;
		cell_index++;
	}

	return cell_index;
}

/*
 * Select the word at the given position using neighbor layer data.
 * Does nothing if position is whitespace.
 */
static bool editor_select_word(uint32_t row, uint32_t column)
{
	if (row >= editor.buffer.line_count) {
		return false;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	if (line->cell_count == 0) {
		return false;
	}

	if (column >= line->cell_count) {
		column = line->cell_count - 1;
	}

	/* Use neighbor layer to find word boundaries */
	enum character_class click_class = neighbor_get_class(line->cells[column].neighbor);

	/* Don't select whitespace as a "word" */
	if (click_class == CHAR_CLASS_WHITESPACE) {
		editor.cursor_column = column;
		selection_clear();
		return false;
	}

	/* Find word start */
	uint32_t word_start = column;
	while (word_start > 0) {
		enum character_class prev_class = neighbor_get_class(line->cells[word_start - 1].neighbor);
		if (!classes_form_word(prev_class, click_class) && prev_class != click_class) {
			break;
		}
		word_start--;
	}

	/* Find word end */
	uint32_t word_end = column;
	while (word_end < line->cell_count - 1) {
		enum character_class next_class = neighbor_get_class(line->cells[word_end + 1].neighbor);
		if (!classes_form_word(click_class, next_class) && next_class != click_class) {
			break;
		}
		word_end++;
	}
	word_end++;  /* End is exclusive */

	/* Set selection */
	editor.selection_anchor_row = row;
	editor.selection_anchor_column = word_start;
	editor.cursor_row = row;
	editor.cursor_column = word_end;
	editor.selection_active = true;
	return true;
}

/*
 * Select an entire line including trailing newline conceptually.
 */
static void editor_select_line(uint32_t row)
{
	if (row >= editor.buffer.line_count) {
		return;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	editor.selection_anchor_row = row;
	editor.selection_anchor_column = 0;
	editor.cursor_row = row;
	editor.cursor_column = line->cell_count;
	editor.selection_active = true;
}

/* Forward declarations for search functions used by select next occurrence */
static bool search_matches_at(struct line *line, struct buffer *buffer,
                              uint32_t column, const char *query, uint32_t query_len);
static uint32_t search_query_cell_count(const char *query, uint32_t query_len);

/*
 * Select the word at the current cursor position.
 * If cursor is on whitespace, tries to find the next word to the right.
 * Returns true if a word was selected.
 */
static bool editor_select_word_at_cursor(void)
{
	if (editor.cursor_row >= editor.buffer.line_count) {
		return false;
	}

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	if (line->cell_count == 0) {
		return false;
	}

	uint32_t column = editor.cursor_column;

	/* Clamp to line length */
	if (column >= line->cell_count) {
		column = line->cell_count > 0 ? line->cell_count - 1 : 0;
	}

	/* Check if cursor is on whitespace */
	enum character_class current_class = neighbor_get_class(line->cells[column].neighbor);

	if (current_class == CHAR_CLASS_WHITESPACE) {
		/* On whitespace - try to find word to the right */
		while (column < line->cell_count) {
			current_class = neighbor_get_class(line->cells[column].neighbor);
			if (current_class != CHAR_CLASS_WHITESPACE) {
				break;
			}
			column++;
		}
		if (column >= line->cell_count) {
			return false;  /* No word found */
		}
	}

	return editor_select_word(editor.cursor_row, column);
}

/*
 * Find the next occurrence of the given text starting from a position.
 * Uses case-insensitive matching (same as search).
 * Returns true if found, with position in out_row/out_column.
 */
static bool find_next_occurrence(const char *text, size_t text_length,
                                 uint32_t start_row, uint32_t start_column,
                                 bool wrap,
                                 uint32_t *out_row, uint32_t *out_column)
{
	if (text == NULL || text_length == 0) {
		return false;
	}

	uint32_t row = start_row;
	uint32_t column = start_column;
	bool wrapped = false;

	while (true) {
		if (row >= editor.buffer.line_count) {
			if (!wrap || wrapped) {
				return false;
			}
			/* Wrap to beginning */
			row = 0;
			column = 0;
			wrapped = true;
		}

		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		/* Search this line from column onwards */
		while (column < line->cell_count) {
			if (search_matches_at(line, &editor.buffer, column, text, text_length)) {
				*out_row = row;
				*out_column = column;
				return true;
			}
			column++;
		}

		/* Move to next line */
		row++;
		column = 0;

		/* Check if we've wrapped back past start */
		if (wrapped && row > start_row) {
			return false;
		}
	}
}

/*
 * Select the word under cursor, or find next occurrence of selection.
 * Mimics VS Code / Sublime Text Ctrl+D behavior.
 */
static void editor_select_next_occurrence(void)
{
	if (!editor.selection_active || selection_is_empty()) {
		/*
		 * No selection - select word under cursor.
		 */
		if (editor_select_word_at_cursor()) {
			size_t length;
			char *text = selection_get_text(&length);
			if (text) {
				if (length > 20) {
					editor_set_status_message("Selected: %.17s...", text);
				} else {
					editor_set_status_message("Selected: %s", text);
				}
				free(text);
			}
		} else {
			editor_set_status_message("No word at cursor");
		}
		return;
	}

	/*
	 * Selection exists - find next occurrence and ADD a cursor.
	 */
	size_t text_length;
	char *text = selection_get_text(&text_length);

	if (text == NULL || text_length == 0) {
		editor_set_status_message("Empty selection");
		return;
	}

	/* Get current selection range */
	uint32_t selection_start_row, selection_start_column;
	uint32_t selection_end_row, selection_end_column;
	selection_get_range(&selection_start_row, &selection_start_column,
	                    &selection_end_row, &selection_end_column);

	/* Count cells in selection for positioning new cursor */
	uint32_t selection_cells = selection_end_column - selection_start_column;
	if (selection_end_row != selection_start_row) {
		/* Multi-line selection - use text length as approximation */
		selection_cells = search_query_cell_count(text, text_length);
	}

	/* Determine search start position */
	uint32_t search_row, search_column;
	if (editor.cursor_count > 0) {
		/* Multi-cursor mode: search after the last cursor */
		struct cursor *last = &editor.cursors[editor.cursor_count - 1];
		search_row = last->row;
		search_column = last->column;
	} else {
		search_row = selection_end_row;
		search_column = selection_end_column;
	}

	uint32_t found_row, found_column;
	if (find_next_occurrence(text, text_length,
	                         search_row, search_column,
	                         true,  /* wrap */
	                         &found_row, &found_column)) {

		/* Check if this is the original selection (wrapped around) */
		bool is_original = (found_row == selection_start_row &&
		                    found_column == selection_start_column);

		/* Check if we already have a cursor here */
		bool already_exists = false;
		if (editor.cursor_count > 0) {
			for (uint32_t i = 0; i < editor.cursor_count; i++) {
				if (editor.cursors[i].anchor_row == found_row &&
				    editor.cursors[i].anchor_column == found_column) {
					already_exists = true;
					break;
				}
			}
		}

		if (is_original || already_exists) {
			uint32_t count = editor.cursor_count > 0 ? editor.cursor_count : 1;
			editor_set_status_message("%u cursor%s (all occurrences)",
			                          count, count > 1 ? "s" : "");
		} else {
			/* Calculate new cursor end position */
			uint32_t new_cursor_column = found_column + selection_cells;

			/* Clamp to line length */
			if (found_row < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[found_row];
				line_warm(line, &editor.buffer);
				if (new_cursor_column > line->cell_count) {
					new_cursor_column = line->cell_count;
				}
			}

			/* Add new cursor at found position */
			if (multicursor_add(found_row, new_cursor_column,
			                    found_row, found_column, true)) {
				multicursor_normalize();

				editor_set_status_message("%u cursors",
				                          editor.cursor_count);

				/* Scroll to show new cursor if needed */
				if (found_row < editor.row_offset) {
					editor.row_offset = found_row;
				} else if (found_row >= editor.row_offset + editor.screen_rows) {
					editor.row_offset = found_row - editor.screen_rows + 1;
				}
			}
		}
	} else {
		editor_set_status_message("No more occurrences");
	}

	free(text);
}

/*
 * Parse SGR mouse event after reading \x1b[<. Returns the parsed mouse
 * input or sets event to MOUSE_NONE on parse failure.
 */
static struct mouse_input input_parse_sgr_mouse(void)
{
	struct mouse_input mouse = {.event = MOUSE_NONE, .row = 0, .column = 0};
	char buffer[32];
	int len = 0;

	/* Read until 'M' (press) or 'm' (release) */
	while (len < 31) {
		if (read(STDIN_FILENO, &buffer[len], 1) != 1) {
			return mouse;
		}
		if (buffer[len] == 'M' || buffer[len] == 'm') {
			break;
		}
		len++;
	}

	char final = buffer[len];
	buffer[len] = '\0';

	/* Parse button;column;row */
	int button, col, row;
	if (sscanf(buffer, "%d;%d;%d", &button, &col, &row) != 3) {
		return mouse;
	}

	/* Convert to 0-based coordinates */
	mouse.column = (col > 0) ? (uint32_t)(col - 1) : 0;
	mouse.row = (row > 0) ? (uint32_t)(row - 1) : 0;

	/* Decode button field */
	int button_number = button & 0x03;
	bool is_drag = (button & 0x20) != 0;
	bool is_scroll = (button & 0x40) != 0;

	if (is_scroll) {
		mouse.event = (button_number == 0) ? MOUSE_SCROLL_UP : MOUSE_SCROLL_DOWN;
	} else if (button_number == 0) {
		if (is_drag) {
			mouse.event = MOUSE_LEFT_DRAG;
		} else if (final == 'M') {
			mouse.event = MOUSE_LEFT_PRESS;
		} else {
			mouse.event = MOUSE_LEFT_RELEASE;
		}
	}

	return mouse;
}

/*
 * Calculate adaptive scroll amount based on scroll velocity.
 * Tracks time between scroll events and uses exponential smoothing
 * to determine if user is scrolling slowly (precision) or quickly
 * (navigation). Returns number of lines to scroll.
 *
 * The algorithm:
 * 1. Measure time since last scroll event
 * 2. Calculate instantaneous velocity (events per second)
 * 3. Apply exponential moving average for smoothing
 * 4. Map velocity to scroll amount via linear interpolation
 * 5. Reset on direction change or timeout
 */
static uint32_t calculate_adaptive_scroll(int direction)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Calculate time delta in seconds */
	double dt = (double)(now.tv_sec - last_scroll_time.tv_sec) +
	            (double)(now.tv_nsec - last_scroll_time.tv_nsec) / 1.0e9;

	/* Check if this is the first scroll (timestamp was never set) */
	bool first_scroll = (last_scroll_time.tv_sec == 0 && last_scroll_time.tv_nsec == 0);

	/* Update timestamp */
	last_scroll_time = now;

	/* Reset velocity on direction change, first scroll, or timeout */
	if (direction != last_scroll_direction ||
	    dt > SCROLL_VELOCITY_TIMEOUT ||
	    dt <= 0 ||
	    first_scroll) {
		scroll_velocity = SCROLL_VELOCITY_SLOW;
		last_scroll_direction = direction;
		return SCROLL_MIN_LINES;
	}

	last_scroll_direction = direction;

	/* Calculate instantaneous velocity (events per second) */
	double instant_velocity = 1.0 / dt;

	/* Clamp instant velocity to reasonable bounds to avoid spikes */
	if (instant_velocity > 100.0) {
		instant_velocity = 100.0;
	}

	/* Exponential moving average for smoothing */
	scroll_velocity = SCROLL_VELOCITY_DECAY * scroll_velocity +
	                  (1.0 - SCROLL_VELOCITY_DECAY) * instant_velocity;

	/* Map velocity to scroll amount */
	if (scroll_velocity <= SCROLL_VELOCITY_SLOW) {
		return SCROLL_MIN_LINES;
	}

	if (scroll_velocity >= SCROLL_VELOCITY_FAST) {
		return SCROLL_MAX_LINES;
	}

	/* Smoothstep interpolation between min and max (eases in and out) */
	double t = (scroll_velocity - SCROLL_VELOCITY_SLOW) /
	           (SCROLL_VELOCITY_FAST - SCROLL_VELOCITY_SLOW);
	t = t * t * (3.0 - 2.0 * t);  /* smoothstep: zero derivative at endpoints */

	return SCROLL_MIN_LINES + (uint32_t)(t * (SCROLL_MAX_LINES - SCROLL_MIN_LINES));
}

/*
 * Handle a mouse event by updating cursor position and selection state.
 * Handles click, drag, scroll, and multi-click for word/line selection.
 */
static void editor_handle_mouse(struct mouse_input *mouse)
{
	switch (mouse->event) {
		case MOUSE_LEFT_PRESS: {
			/* Convert screen position to buffer position */
			uint32_t file_row;
			uint16_t segment;
			uint32_t cell_col;

			/* Account for gutter in screen column */
			uint32_t screen_col = mouse->column;
			if (screen_col < editor.gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= editor.gutter_width;
			}

			/* Map screen row to logical line and segment */
			if (screen_row_to_line_segment(mouse->row, &file_row, &segment)) {
				/*
				 * Convert visual position to cell column.
				 * In wrap mode, visual position is just screen_col.
				 * In no-wrap mode, add column_offset.
				 */
				struct line *line = &editor.buffer.lines[file_row];
				if (editor.wrap_mode != WRAP_NONE) {
					cell_col = line_find_column_at_visual(
						line, &editor.buffer, segment, screen_col);
				} else {
					cell_col = screen_column_to_cell(
						file_row, screen_col + editor.column_offset);
				}
			} else {
				/* Click below content: go to last line */
				if (editor.buffer.line_count > 0) {
					file_row = editor.buffer.line_count - 1;
				} else {
					file_row = 0;
				}
				segment = 0;
				cell_col = 0;
			}

			/* Detect double/triple click */
			time_t now = time(NULL);
			if (now - last_click_time <= 1 &&
			    last_click_row == file_row &&
			    last_click_col == cell_col) {
				click_count++;
			} else {
				click_count = 1;
			}
			last_click_time = now;
			last_click_row = file_row;
			last_click_col = cell_col;

			if (click_count == 2) {
				/* Double-click: select word using neighbor layer */
				editor.cursor_row = file_row;
				editor_select_word(file_row, cell_col);
			} else if (click_count >= 3) {
				/* Triple-click: select entire line */
				editor_select_line(file_row);
				click_count = 0;  /* Reset */
			} else {
				/* Single click: position cursor and start selection */
				editor.cursor_row = file_row;
				editor.cursor_column = cell_col;
				selection_start();
			}
			break;
		}

		case MOUSE_LEFT_DRAG: {
			/* Update cursor position; anchor stays fixed */
			uint32_t file_row;
			uint16_t segment;
			uint32_t cell_col;

			uint32_t screen_col = mouse->column;
			if (screen_col < editor.gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= editor.gutter_width;
			}

			/* Map screen row to logical line and segment */
			if (screen_row_to_line_segment(mouse->row, &file_row, &segment)) {
				struct line *line = &editor.buffer.lines[file_row];
				if (editor.wrap_mode != WRAP_NONE) {
					cell_col = line_find_column_at_visual(
						line, &editor.buffer, segment, screen_col);
				} else {
					cell_col = screen_column_to_cell(
						file_row, screen_col + editor.column_offset);
				}
			} else {
				/* Drag below content: clamp to last line */
				if (editor.buffer.line_count > 0) {
					file_row = editor.buffer.line_count - 1;
				} else {
					file_row = 0;
				}
				cell_col = 0;
			}

			editor.cursor_row = file_row;
			editor.cursor_column = cell_col;

			/* Ensure selection is active during drag */
			if (!editor.selection_active) {
				selection_start();
			}
			break;
		}

		case MOUSE_LEFT_RELEASE:
			/* Selection complete; leave it active */
			break;

		case MOUSE_SCROLL_UP: {
			/* In search mode, find previous match */
			if (search.active) {
				search.direction = -1;
				if (!search_find_previous(true)) {
					editor_set_status_message("No more matches");
				}
				break;
			}

			uint32_t scroll_amount = calculate_adaptive_scroll(-1);

			if (editor.row_offset >= scroll_amount) {
				editor.row_offset -= scroll_amount;
			} else {
				editor.row_offset = 0;
			}

			/* Keep cursor on screen (only if no selection) */
			if (!editor.selection_active) {
				if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
					editor.cursor_row = editor.row_offset + editor.screen_rows - 1;
					if (editor.cursor_row >= editor.buffer.line_count && editor.buffer.line_count > 0) {
						editor.cursor_row = editor.buffer.line_count - 1;
					}
				}
			}
			break;
		}

		case MOUSE_SCROLL_DOWN: {
			/* In search mode, find next match */
			if (search.active) {
				search.direction = 1;
				if (!search_find_next(true)) {
					editor_set_status_message("No more matches");
				}
				break;
			}

			uint32_t scroll_amount = calculate_adaptive_scroll(1);

			/* Calculate maximum valid offset (wrap-aware) */
			uint32_t max_offset = calculate_max_row_offset();

			if (editor.row_offset + scroll_amount <= max_offset) {
				editor.row_offset += scroll_amount;
			} else {
				editor.row_offset = max_offset;
			}

			/* Keep cursor on screen (only if no selection) */
			if (!editor.selection_active) {
				if (editor.cursor_row < editor.row_offset) {
					editor.cursor_row = editor.row_offset;
				}
			}
			break;
		}

		default:
			break;
	}
}

/*****************************************************************************
 * Incremental Search
 *****************************************************************************/

/*
 * Enter search mode. Saves current cursor position and initializes search state.
 */
static void search_enter(void)
{
	search.active = true;
	search.replace_mode = false;
	search.query[0] = '\0';
	search.query_length = 0;
	search.replace_text[0] = '\0';
	search.replace_length = 0;
	search.editing_replace = false;
	search.saved_cursor_row = editor.cursor_row;
	search.saved_cursor_column = editor.cursor_column;
	search.saved_row_offset = editor.row_offset;
	search.saved_column_offset = editor.column_offset;
	search.has_match = false;
	search.direction = 1;
}

/*
 * Enter find & replace mode. Similar to search_enter but enables replace UI.
 */
static void replace_enter(void)
{
	search.active = true;
	search.replace_mode = true;
	search.query[0] = '\0';
	search.query_length = 0;
	search.replace_text[0] = '\0';
	search.replace_length = 0;
	search.editing_replace = false;
	search.saved_cursor_row = editor.cursor_row;
	search.saved_cursor_column = editor.cursor_column;
	search.saved_row_offset = editor.row_offset;
	search.saved_column_offset = editor.column_offset;
	search.has_match = false;
	search.direction = 1;
}

/*
 * Exit search mode, optionally restoring cursor position.
 */
static void search_exit(bool restore_position)
{
	if (restore_position) {
		editor.cursor_row = search.saved_cursor_row;
		editor.cursor_column = search.saved_cursor_column;
		editor.row_offset = search.saved_row_offset;
		editor.column_offset = search.saved_column_offset;
	}

	/* Free compiled regex */
	if (search.regex_compiled) {
		regfree(&search.compiled_regex);
		search.regex_compiled = false;
	}

	/* Cancel async search and clear results */
	async_search_cancel();
	if (async_search.mutex_initialized) {
		pthread_mutex_lock(&async_search.results_mutex);
		search_results_clear_locked();
		pthread_mutex_unlock(&async_search.results_mutex);
	}

	search.active = false;
	search.replace_mode = false;
	search.has_match = false;
}

/*
 * Compile the current search query as a regex.
 * Updates search.regex_compiled and search.regex_error.
 */
static void search_compile_regex(void)
{
	/* Free previous compiled regex */
	if (search.regex_compiled) {
		regfree(&search.compiled_regex);
		search.regex_compiled = false;
	}
	search.regex_error[0] = '\0';

	if (search.query_length == 0) {
		return;
	}

	/* Build flags */
	int flags = REG_EXTENDED;
	if (!search.case_sensitive) {
		flags |= REG_ICASE;
	}

	/* Compile the pattern */
	int result = regcomp(&search.compiled_regex, search.query, flags);

	if (result == 0) {
		search.regex_compiled = true;
	} else {
		/* Get error message */
		regerror(result, &search.compiled_regex, search.regex_error,
		         sizeof(search.regex_error));
	}
}

/*
 * Check if a position is at a word boundary.
 * A word boundary exists between a word character and a non-word character.
 */
static bool is_word_boundary(struct line *line, uint32_t column)
{
	if (line->cell_count == 0) {
		return true;
	}

	/* Start of line is a boundary */
	if (column == 0) {
		return true;
	}

	/* End of line is a boundary */
	if (column >= line->cell_count) {
		return true;
	}

	/* Check if character class changes */
	uint32_t previous_codepoint = line->cells[column - 1].codepoint;
	uint32_t current_codepoint = line->cells[column].codepoint;

	bool previous_is_word = isalnum(previous_codepoint) || previous_codepoint == '_';
	bool current_is_word = isalnum(current_codepoint) || current_codepoint == '_';

	return previous_is_word != current_is_word;
}

/*
 * Check if a match at the given position satisfies whole-word constraint.
 */
static bool is_whole_word_match(struct line *line, uint32_t start_column,
                                uint32_t end_column)
{
	/* Check boundary at start */
	if (!is_word_boundary(line, start_column)) {
		return false;
	}

	/* Check boundary at end */
	if (!is_word_boundary(line, end_column)) {
		return false;
	}

	return true;
}

/*
 * Convert a line (or portion) to a UTF-8 string for regex matching.
 * Returns a newly allocated string. Caller must free.
 * Also builds a mapping from byte offset to cell index in *byte_to_cell.
 */
static char *line_to_string(struct line *line, uint32_t start_column,
                            uint32_t **byte_to_cell, size_t *out_length)
{
	if (line->cell_count == 0 || start_column >= line->cell_count) {
		*out_length = 0;
		*byte_to_cell = NULL;
		return strdup("");
	}

	/* Calculate required size */
	size_t capacity = (line->cell_count - start_column) * 4 + 1;
	char *result = malloc(capacity);
	uint32_t *mapping = malloc(capacity * sizeof(uint32_t));

	if (!result || !mapping) {
		free(result);
		free(mapping);
		*out_length = 0;
		*byte_to_cell = NULL;
		return NULL;
	}

	size_t byte_position = 0;
	for (uint32_t column = start_column; column < line->cell_count; column++) {
		uint32_t codepoint = line->cells[column].codepoint;

		char utf8[4];
		int bytes = utflite_encode(codepoint, utf8);

		if (bytes > 0) {
			for (int i = 0; i < bytes; i++) {
				result[byte_position] = utf8[i];
				mapping[byte_position] = column;
				byte_position++;
			}
		}
	}

	result[byte_position] = '\0';
	*out_length = byte_position;
	*byte_to_cell = mapping;

	return result;
}

/*
 * Check if the search query matches at the given position.
 * Returns the length of the match in cells (0 if no match).
 * For literal search, returns query_cell_count on match.
 * For regex, returns the actual match length.
 */
static uint32_t search_match_length_at(struct line *line, uint32_t column)
{
	if (search.query_length == 0) {
		return 0;
	}

	if (column >= line->cell_count) {
		return 0;
	}

	if (search.use_regex) {
		/* Regex matching */
		if (!search.regex_compiled) {
			return 0;
		}

		/* Convert line to string starting at column */
		size_t string_length;
		uint32_t *byte_to_cell;
		char *line_string = line_to_string(line, column, &byte_to_cell, &string_length);

		if (!line_string) {
			return 0;
		}

		regmatch_t match;
		int result = regexec(&search.compiled_regex, line_string, 1, &match, 0);

		uint32_t match_cells = 0;

		if (result == 0 && match.rm_so == 0) {
			/* Match at start of string (which is our column) */
			/* Convert byte end position to cell count */
			if (match.rm_eo > 0 && (size_t)match.rm_eo <= string_length) {
				uint32_t start_cell = column;
				uint32_t end_cell = byte_to_cell[match.rm_eo - 1] + 1;
				match_cells = end_cell - start_cell;

				/* Check whole word constraint */
				if (search.whole_word) {
					if (!is_whole_word_match(line, column, column + match_cells)) {
						match_cells = 0;
					}
				}
			}
		}

		free(line_string);
		free(byte_to_cell);

		return match_cells;

	} else {
		/* Literal matching */
		const char *query = search.query;
		uint32_t query_position = 0;
		uint32_t cell_index = column;
		uint32_t match_start = column;

		while (query[query_position] != '\0' && cell_index < line->cell_count) {
			uint32_t query_codepoint;
			int bytes = utflite_decode(query + query_position,
			                           search.query_length - query_position,
			                           &query_codepoint);
			if (bytes <= 0) break;

			uint32_t cell_codepoint = line->cells[cell_index].codepoint;

			/* Compare (with optional case folding) */
			bool matches;
			if (search.case_sensitive) {
				matches = (cell_codepoint == query_codepoint);
			} else {
				uint32_t lower_cell = cell_codepoint;
				uint32_t lower_query = query_codepoint;
				if (lower_cell >= 'A' && lower_cell <= 'Z') lower_cell += 32;
				if (lower_query >= 'A' && lower_query <= 'Z') lower_query += 32;
				matches = (lower_cell == lower_query);
			}

			if (!matches) {
				return 0;
			}

			query_position += bytes;
			cell_index++;
		}

		/* Check if we matched the entire query */
		if (query[query_position] != '\0') {
			return 0;  /* Query not fully matched */
		}

		uint32_t match_cells = cell_index - match_start;

		/* Check whole word constraint */
		if (search.whole_word) {
			if (!is_whole_word_match(line, match_start, cell_index)) {
				return 0;
			}
		}

		return match_cells;
	}
}

/*
 * Check if the query matches at a specific position in a line.
 * Returns true if query matches starting at the given column.
 * Uses search_match_length_at() for matching with all search options.
 */
static bool search_matches_at(struct line *line, struct buffer *buffer,
                              uint32_t column, const char *query, uint32_t query_len)
{
	(void)query;      /* We use search.query directly now */
	(void)query_len;

	if (search.query_length == 0) {
		return false;
	}

	line_warm(line, buffer);

	return search_match_length_at(line, column) > 0;
}

/*
 * Count the number of cells the query occupies (for highlighting width).
 */
static uint32_t search_query_cell_count(const char *query, uint32_t query_len)
{
	uint32_t count = 0;
	uint32_t offset = 0;
	while (offset < query_len) {
		uint32_t cp;
		int bytes = utflite_decode(query + offset, query_len - offset, &cp);
		if (bytes <= 0) break;
		count++;
		offset += bytes;
	}
	return count;
}

/*
 * Center the viewport vertically on the current match.
 * Called after finding a match to snap it to the middle of the screen.
 */
static void search_center_on_match(void)
{
	if (!search.has_match) {
		return;
	}

	/* Calculate the row offset that would center the match */
	uint32_t target_row = search.match_row;
	uint32_t half_screen = editor.screen_rows / 2;

	if (target_row >= half_screen) {
		editor.row_offset = target_row - half_screen;
	} else {
		editor.row_offset = 0;
	}

	/* Clamp to valid range (wrap-aware) */
	uint32_t max_offset = calculate_max_row_offset();
	if (editor.row_offset > max_offset) {
		editor.row_offset = max_offset;
	}
}

/*
 * Find the next match starting from current position.
 * If wrap is true, wraps around to beginning of file.
 * Returns true if a match was found.
 */
static bool search_find_next(bool wrap)
{
	if (search.query_length == 0) {
		return false;
	}

	uint32_t start_row = editor.cursor_row;
	uint32_t start_col = editor.cursor_column + 1;

	/* Search from current position to end of file */
	for (uint32_t row = start_row; row < editor.buffer.line_count; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		uint32_t col_start = (row == start_row) ? start_col : 0;

		for (uint32_t col = col_start; col < line->cell_count; col++) {
			if (search_matches_at(line, &editor.buffer, col, search.query, search.query_length)) {
				editor.cursor_row = row;
				editor.cursor_column = col;
				search.match_row = row;
				search.match_column = col;
				search.has_match = true;
				search_center_on_match();
				return true;
			}
		}
	}

	/* Wrap around to beginning */
	if (wrap) {
		for (uint32_t row = 0; row <= start_row; row++) {
			struct line *line = &editor.buffer.lines[row];
			line_warm(line, &editor.buffer);

			uint32_t col_end = (row == start_row) ? start_col : line->cell_count;

			for (uint32_t col = 0; col < col_end; col++) {
				if (search_matches_at(line, &editor.buffer, col, search.query, search.query_length)) {
					editor.cursor_row = row;
					editor.cursor_column = col;
					search.match_row = row;
					search.match_column = col;
					search.has_match = true;
					search_center_on_match();
					return true;
				}
			}
		}
	}

	search.has_match = false;
	return false;
}

/*
 * Find the previous match starting from current position.
 * If wrap is true, wraps around to end of file.
 */
static bool search_find_previous(bool wrap)
{
	if (search.query_length == 0) {
		return false;
	}

	int32_t start_row = (int32_t)editor.cursor_row;
	int32_t start_col = (int32_t)editor.cursor_column - 1;

	/* Search from current position to beginning of file */
	for (int32_t row = start_row; row >= 0; row--) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		int32_t col_start = (row == start_row) ? start_col : (int32_t)line->cell_count - 1;
		if (col_start < 0) continue;

		for (int32_t col = col_start; col >= 0; col--) {
			if (search_matches_at(line, &editor.buffer, (uint32_t)col, search.query, search.query_length)) {
				editor.cursor_row = (uint32_t)row;
				editor.cursor_column = (uint32_t)col;
				search.match_row = (uint32_t)row;
				search.match_column = (uint32_t)col;
				search.has_match = true;
				search_center_on_match();
				return true;
			}
		}
	}

	/* Wrap around to end */
	if (wrap && editor.buffer.line_count > 0) {
		for (int32_t row = (int32_t)editor.buffer.line_count - 1; row >= start_row; row--) {
			struct line *line = &editor.buffer.lines[row];
			line_warm(line, &editor.buffer);

			int32_t col_start = (row == start_row) ? start_col : (int32_t)line->cell_count - 1;
			if (col_start < 0) continue;

			for (int32_t col = col_start; col >= 0; col--) {
				if (search_matches_at(line, &editor.buffer, (uint32_t)col, search.query, search.query_length)) {
					editor.cursor_row = (uint32_t)row;
					editor.cursor_column = (uint32_t)col;
					search.match_row = (uint32_t)row;
					search.match_column = (uint32_t)col;
					search.has_match = true;
					search_center_on_match();
					return true;
				}
			}
		}
	}

	search.has_match = false;
	return false;
}

/*
 * Update search results when query changes. Finds first match from saved position.
 */
static void search_update(void)
{
	/* Compile regex if in regex mode */
	if (search.use_regex) {
		search_compile_regex();
	}

	if (search.query_length == 0) {
		/* No query - restore to saved position and cancel async search */
		editor.cursor_row = search.saved_cursor_row;
		editor.cursor_column = search.saved_cursor_column;
		search.has_match = false;
		async_search_cancel();
		pthread_mutex_lock(&async_search.results_mutex);
		search_results_clear_locked();
		pthread_mutex_unlock(&async_search.results_mutex);
		return;
	}

	/* Use async search for large files */
	if (should_use_async_search()) {
		async_search_start(search.query, search.use_regex,
		                   search.case_sensitive, search.whole_word);
		/* Results will come via worker_process_results() */
		return;
	}

	/* Synchronous search for small files */

	/* Start search from saved position */
	editor.cursor_row = search.saved_cursor_row;
	editor.cursor_column = search.saved_cursor_column;

	/* First check if there's a match at current position */
	if (editor.cursor_row < editor.buffer.line_count) {
		struct line *line = &editor.buffer.lines[editor.cursor_row];
		if (search_matches_at(line, &editor.buffer, editor.cursor_column,
		                      search.query, search.query_length)) {
			search.match_row = editor.cursor_row;
			search.match_column = editor.cursor_column;
			search.has_match = true;
			search_center_on_match();
			return;
		}
	}

	/* Otherwise find next match (with wrap) */
	/* Note: search_find_next already calls search_center_on_match */
	search_find_next(true);
}

/*
 * Count the number of cells (grapheme clusters) in a UTF-8 string.
 */
static uint32_t replace_count_cells(const char *text, uint32_t length)
{
	uint32_t count = 0;
	const char *p = text;
	const char *end = text + length;

	while (p < end) {
		uint32_t codepoint;
		int bytes = utflite_decode(p, end - p, &codepoint);
		if (bytes <= 0) {
			break;
		}
		p += bytes;
		count++;
	}

	return count;
}

/*
 * Expand backreferences in replacement string.
 * Supports \0 (or \&) for entire match, \1-\9 for capture groups.
 * Returns newly allocated string. Caller must free.
 */
static char *expand_replacement(const char *replace_text,
                                const char *source_text,
                                regmatch_t *matches,
                                size_t match_count)
{
	/* Calculate required size (conservative estimate) */
	size_t source_length = strlen(source_text);
	size_t replace_length = strlen(replace_text);
	size_t capacity = replace_length + source_length * match_count + 1;

	char *result = malloc(capacity);
	if (!result) {
		return NULL;
	}

	size_t result_position = 0;
	const char *p = replace_text;

	while (*p) {
		if (*p == '\\' && p[1] != '\0') {
			char next = p[1];
			int group = -1;

			if (next == '&' || next == '0') {
				group = 0;  /* Entire match */
			} else if (next >= '1' && next <= '9') {
				group = next - '0';
			} else if (next == '\\') {
				/* Escaped backslash */
				result[result_position++] = '\\';
				p += 2;
				continue;
			} else {
				/* Unknown escape - copy literally */
				result[result_position++] = *p++;
				continue;
			}

			/* Insert captured group */
			if (group >= 0 && (size_t)group < match_count) {
				regmatch_t *m = &matches[group];
				if (m->rm_so >= 0 && m->rm_eo >= m->rm_so) {
					size_t length = m->rm_eo - m->rm_so;

					/* Ensure capacity */
					if (result_position + length >= capacity) {
						capacity = capacity * 2 + length;
						char *new_result = realloc(result, capacity);
						if (!new_result) {
							free(result);
							return NULL;
						}
						result = new_result;
					}

					memcpy(result + result_position, source_text + m->rm_so, length);
					result_position += length;
				}
			}

			p += 2;  /* Skip \N */

		} else {
			result[result_position++] = *p++;
		}

		/* Ensure capacity */
		if (result_position >= capacity - 4) {
			capacity *= 2;
			char *new_result = realloc(result, capacity);
			if (!new_result) {
				free(result);
				return NULL;
			}
			result = new_result;
		}
	}

	result[result_position] = '\0';
	return result;
}

/*
 * Replace the current match with the replacement text.
 * Returns true if a replacement was made.
 * Supports regex backreferences (\0-\9) when in regex mode.
 */
static bool search_replace_current(void)
{
	if (!search.has_match || search.query_length == 0) {
		return false;
	}

	if (editor.cursor_row >= editor.buffer.line_count) {
		return false;
	}

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	/* Get match length - use search_match_length_at for regex support */
	uint32_t match_cells = search_match_length_at(line, editor.cursor_column);
	if (match_cells == 0) {
		return false;
	}

	/* Determine final replacement text (with backreference expansion) */
	char *final_replacement = NULL;

	if (search.use_regex && search.regex_compiled) {
		/* Build line string for backreference extraction */
		size_t string_length;
		uint32_t *byte_to_cell;
		char *line_string = line_to_string(line, editor.cursor_column,
		                                   &byte_to_cell, &string_length);

		if (line_string) {
			/* Get all capture groups */
			regmatch_t matches[10];
			if (regexec(&search.compiled_regex, line_string, 10, matches, 0) == 0) {
				final_replacement = expand_replacement(search.replace_text,
				                                       line_string, matches, 10);
			}
			free(line_string);
			free(byte_to_cell);
		}

		if (!final_replacement) {
			final_replacement = strdup(search.replace_text);
		}
	} else {
		final_replacement = strdup(search.replace_text);
	}

	if (!final_replacement) {
		return false;
	}

	undo_begin_group(&editor.buffer);

	/* Delete the match characters (from end to start for correct undo) */
	for (uint32_t i = 0; i < match_cells; i++) {
		uint32_t delete_position = editor.cursor_column + match_cells - 1 - i;
		if (delete_position < line->cell_count) {
			uint32_t codepoint = line->cells[delete_position].codepoint;
			undo_record_delete_char(&editor.buffer, editor.cursor_row,
			                        delete_position, codepoint);

			/* Shift cells left */
			if (delete_position < line->cell_count - 1) {
				memmove(&line->cells[delete_position],
					&line->cells[delete_position + 1],
					(line->cell_count - delete_position - 1) * sizeof(struct cell));
			}
			line->cell_count--;
		}
	}

	/* Insert replacement text */
	const char *r = final_replacement;
	uint32_t insert_position = editor.cursor_column;

	while (*r) {
		uint32_t codepoint;
		int bytes = utflite_decode(r, strlen(r), &codepoint);
		if (bytes <= 0) {
			break;
		}
		r += bytes;

		undo_record_insert_char(&editor.buffer, editor.cursor_row,
		                        insert_position, codepoint);

		/* Make room and insert */
		int ret = line_ensure_capacity_checked(line, line->cell_count + 1);
		if (ret) {
			editor_set_status_message("Replace failed: %s", edit_strerror(ret));
			free(final_replacement);
			undo_end_group(&editor.buffer);
			return false;
		}

		if (insert_position < line->cell_count) {
			memmove(&line->cells[insert_position + 1],
				&line->cells[insert_position],
				(line->cell_count - insert_position) * sizeof(struct cell));
		}

		line->cells[insert_position].codepoint = codepoint;
		line->cells[insert_position].syntax = SYNTAX_NORMAL;
		line->cells[insert_position].context = 0;
		line->cells[insert_position].neighbor = 0;
		line->cell_count++;
		insert_position++;
	}

	free(final_replacement);

	/* Recompute line metadata */
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	neighbor_compute_line(line);
	syntax_highlight_line(line, &editor.buffer, editor.cursor_row);
	line_invalidate_wrap_cache(line);

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	return true;
}

/*
 * Replace current match and find next.
 */
static void search_replace_and_next(void)
{
	if (search_replace_current()) {
		/* Move cursor past the replacement */
		uint32_t replace_cells = replace_count_cells(search.replace_text,
		                                              search.replace_length);
		editor.cursor_column += replace_cells;

		/* Find next match */
		if (!search_find_next(true)) {
			editor_set_status_message("Replaced. No more matches.");
		} else {
			editor_set_status_message("Replaced.");
		}
	}
}

/*
 * Replace all matches in the buffer.
 */
static void search_replace_all(void)
{
	if (search.query_length == 0) {
		return;
	}

	undo_begin_group(&editor.buffer);

	uint32_t count = 0;

	/* Save current position */
	uint32_t saved_row = editor.cursor_row;
	uint32_t saved_column = editor.cursor_column;

	/* Start from beginning of file */
	editor.cursor_row = 0;
	editor.cursor_column = 0;

	/* Find and replace all matches without wrapping */
	while (search_find_next(false)) {
		if (search_replace_current()) {
			count++;

			/* Move past replacement to avoid infinite loop */
			uint32_t replace_cells = replace_count_cells(search.replace_text,
			                                              search.replace_length);
			editor.cursor_column += replace_cells;
		} else {
			/* No replacement made, move forward to avoid infinite loop */
			editor.cursor_column++;
			if (editor.cursor_row < editor.buffer.line_count) {
				struct line *line = &editor.buffer.lines[editor.cursor_row];
				line_warm(line, &editor.buffer);
				if (editor.cursor_column >= line->cell_count) {
					editor.cursor_row++;
					editor.cursor_column = 0;
				}
			}
		}
	}

	undo_end_group(&editor.buffer);

	/* Restore cursor to beginning if no replacements, or keep at last position */
	if (count == 0) {
		editor.cursor_row = saved_row;
		editor.cursor_column = saved_column;
	}

	search.has_match = false;
	editor_set_status_message("Replaced %u occurrence%s", count, count != 1 ? "s" : "");
}

/*
 * Check if a cell is part of a search match.
 * Returns: 0 = not a match, 1 = other match, 2 = current match
 */
static int search_match_type(uint32_t row, uint32_t column)
{
	if (!search.active || search.query_length == 0) {
		return 0;
	}

	if (row >= editor.buffer.line_count) {
		return 0;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	/*
	 * For regex, matches can have variable length, so we need to check
	 * each potential starting position. For literal search, we can use
	 * the cached query length as an optimization.
	 */
	uint32_t max_match_len;
	if (search.use_regex) {
		/* Regex matches can be any length - use conservative estimate */
		max_match_len = line->cell_count;
	} else {
		/* Literal search - match length is fixed */
		max_match_len = search_query_cell_count(search.query, search.query_length);
	}

	/* Check if this cell is part of the current match */
	if (search.has_match && row == search.match_row) {
		uint32_t current_match_len = search_match_length_at(line, search.match_column);
		if (current_match_len > 0 &&
		    column >= search.match_column &&
		    column < search.match_column + current_match_len) {
			return 2;  /* Current match */
		}
	}

	/* Look backward to see if a match starts before this column */
	uint32_t check_start = (column >= max_match_len) ? column - max_match_len + 1 : 0;

	for (uint32_t col = check_start; col <= column; col++) {
		uint32_t this_match_len = search_match_length_at(line, col);
		if (this_match_len > 0 && column < col + this_match_len) {
			/* Skip if this is the current match (already handled above) */
			if (search.has_match && row == search.match_row && col == search.match_column) {
				continue;
			}
			return 1;  /* Other match */
		}
	}

	return 0;
}

/*****************************************************************************
 * Rendering
 *****************************************************************************/

/*
 * Output an ANSI escape sequence for the given syntax token type.
 * Includes foreground color, background color, and text attributes.
 */
static void render_set_syntax_style(struct output_buffer *output, enum syntax_token type)
{
	char escape[128];
	int len = style_to_escape(&active_theme.syntax[type], escape, sizeof(escape));
	output_buffer_append(output, escape, len);
}

/*
 * Render a segment of a line's content to the output buffer.
 *
 * Two rendering modes:
 * 1. Segment mode (end_cell < UINT32_MAX): Render cells from start_cell to end_cell.
 *    Used for wrapped line segments.
 * 2. Scroll mode (end_cell == UINT32_MAX): Horizontal scroll - start_cell is a
 *    visual column offset, skip to that position then render. Used for WRAP_NONE.
 *
 * Parameters:
 *   output        - Output buffer to append rendered content to
 *   line          - The line to render (will be warmed if cold)
 *   buffer        - Parent buffer
 *   file_row      - Logical line number in file (for search/selection)
 *   start_cell    - First cell index, OR visual column offset if end_cell==UINT32_MAX
 *   end_cell      - Last cell index (exclusive), or UINT32_MAX for scroll mode
 *   max_width     - Maximum visual width to render
 *   is_cursor_line - True if cursor is on this line (for background highlight)
 */
static void render_line_content(struct output_buffer *output, struct line *line,
                                struct buffer *buffer, uint32_t file_row,
                                uint32_t start_cell, uint32_t end_cell,
                                int max_width, bool is_cursor_line)
{
	line_warm(line, buffer);

	int visual_column = 0;
	uint32_t cell_index = 0;

	/*
	 * Handle the two modes differently:
	 * - Scroll mode (UINT32_MAX): skip cells until we reach the visual column
	 * - Segment mode: jump directly to start_cell, computing visual column
	 */
	if (end_cell == UINT32_MAX) {
		/* Scroll mode: start_cell is actually a visual column offset */
		uint32_t column_offset = start_cell;
		while (cell_index < line->cell_count && visual_column < (int)column_offset) {
			uint32_t codepoint = line->cells[cell_index].codepoint;
			int width;
			if (codepoint == '\t') {
				width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			} else {
				width = utflite_codepoint_width(codepoint);
				if (width < 0) width = 1;
			}
			visual_column += width;
			cell_index++;
		}
		end_cell = line->cell_count;
	} else {
		/* Segment mode: compute visual column at start of segment */
		for (uint32_t i = 0; i < start_cell && i < line->cell_count; i++) {
			uint32_t codepoint = line->cells[i].codepoint;
			int width;
			if (codepoint == '\t') {
				width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			} else {
				width = utflite_codepoint_width(codepoint);
				if (width < 0) width = 1;
			}
			visual_column += width;
		}
		cell_index = start_cell;
		if (end_cell > line->cell_count) {
			end_cell = line->cell_count;
		}
	}

	/* Track current state to minimize escape sequences */
	enum syntax_token current_syntax = SYNTAX_NORMAL;
	int current_highlight = 0;  /* 0=normal, 1=selected, 2=search_other, 3=search_current */

	/* Set initial style with cursor line background if applicable */
	{
		char escape[128];
		struct style *style = &active_theme.syntax[current_syntax];
		struct syntax_color bg = is_cursor_line ? active_theme.cursor_line : style->bg;
		int len = snprintf(escape, sizeof(escape),
		         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
		         bg.red, bg.green, bg.blue,
		         style->fg.red, style->fg.green, style->fg.blue);
		len += attr_to_escape(style->attr, escape + len, sizeof(escape) - len);
		output_buffer_append(output, escape, len);
	}

	/* Render visible content up to end_cell or max_width */
	int rendered_width = 0;
	while (cell_index < end_cell && rendered_width < max_width) {
		uint32_t codepoint = line->cells[cell_index].codepoint;
		enum syntax_token syntax = line->cells[cell_index].syntax;

		/*
		 * Determine highlight type with priority:
		 * search current > search other > selection > trailing ws > cursor line
		 */
		int highlight = 0;
		int match_type = search_match_type(file_row, cell_index);
		if (match_type == 2) {
			highlight = 3;  /* Current search match */
		} else if (match_type == 1) {
			highlight = 2;  /* Other search match */
		} else if (selection_contains(file_row, cell_index) ||
		           multicursor_selection_contains(file_row, cell_index)) {
			highlight = 1;  /* Selection */
		} else if (is_trailing_whitespace(line, cell_index)) {
			highlight = 4;  /* Trailing whitespace */
		}

		/* Change colors if syntax or highlight changed */
		if (syntax != current_syntax || highlight != current_highlight) {
			char escape[128];
			struct style *style = &active_theme.syntax[syntax];
			struct syntax_color bg;

			switch (highlight) {
				case 4:  /* Trailing whitespace - warning red */
					bg = active_theme.trailing_ws;
					break;
				case 3:  /* Current search match - gold */
					bg = active_theme.search_current;
					break;
				case 2:  /* Other search match - blue */
					bg = active_theme.search_match;
					break;
				case 1:  /* Selection */
					bg = active_theme.selection;
					break;
				default: /* Token background, cursor line, or normal */
					if (is_cursor_line) {
						bg = active_theme.cursor_line;
					} else {
						bg = style->bg;
					}
					break;
			}

			/* Ensure foreground has sufficient contrast with background */
			struct syntax_color adjusted_fg = color_ensure_contrast(style->fg, bg);

			int len = snprintf(escape, sizeof(escape),
			         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
			         bg.red, bg.green, bg.blue,
			         adjusted_fg.red, adjusted_fg.green, adjusted_fg.blue);

			/* Add text attributes */
			len += attr_to_escape(style->attr, escape + len, sizeof(escape) - len);

			output_buffer_append(output, escape, len);
			current_syntax = syntax;
			current_highlight = highlight;
		}

		int width;
		if (codepoint == '\t') {
			width = TAB_STOP_WIDTH - (visual_column % TAB_STOP_WIDTH);
			/* Render tab with optional visible indicator */
			if (editor.show_whitespace) {
				/* Show → with tab colors, using cursor_line bg if applicable */
				char ws_escape[128];
				struct syntax_color ws_bg = is_cursor_line
					? active_theme.cursor_line
					: active_theme.whitespace_tab.bg;
				int ws_len = style_to_escape_with_bg(&active_theme.whitespace_tab,
				                                     ws_bg, ws_escape, sizeof(ws_escape));
				output_buffer_append(output, ws_escape, ws_len);
				output_buffer_append_string(output, "→");
				rendered_width++;
				for (int i = 1; i < width && rendered_width < max_width; i++) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
				/* Restore syntax style with cursor_line bg if applicable */
				struct style *restore_style = &active_theme.syntax[current_syntax];
				struct syntax_color restore_bg = is_cursor_line
					? active_theme.cursor_line : restore_style->bg;
				char restore_escape[128];
				int restore_len = snprintf(restore_escape, sizeof(restore_escape),
				         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
				         restore_bg.red, restore_bg.green, restore_bg.blue,
				         restore_style->fg.red, restore_style->fg.green, restore_style->fg.blue);
				restore_len += attr_to_escape(restore_style->attr, restore_escape + restore_len,
				                              sizeof(restore_escape) - restore_len);
				output_buffer_append(output, restore_escape, restore_len);
			} else {
				/* Render spaces for tab */
				for (int i = 0; i < width && rendered_width < max_width; i++) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
			}
		} else if (codepoint == ' ' && editor.show_whitespace) {
			/* Show space as middle dot, using cursor_line bg if applicable */
			char ws_escape[128];
			struct syntax_color ws_bg = is_cursor_line
				? active_theme.cursor_line
				: active_theme.whitespace_space.bg;
			int ws_len = style_to_escape_with_bg(&active_theme.whitespace_space,
			                                     ws_bg, ws_escape, sizeof(ws_escape));
			output_buffer_append(output, ws_escape, ws_len);
			output_buffer_append_string(output, "·");
			/* Restore syntax style with cursor_line bg if applicable */
			struct style *restore_style = &active_theme.syntax[current_syntax];
			struct syntax_color restore_bg = is_cursor_line
				? active_theme.cursor_line : restore_style->bg;
			char restore_escape[128];
			int restore_len = snprintf(restore_escape, sizeof(restore_escape),
			         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
			         restore_bg.red, restore_bg.green, restore_bg.blue,
			         restore_style->fg.red, restore_style->fg.green, restore_style->fg.blue);
			restore_len += attr_to_escape(restore_style->attr, restore_escape + restore_len,
			                              sizeof(restore_escape) - restore_len);
			output_buffer_append(output, restore_escape, restore_len);
			rendered_width++;
			width = 1;
		} else {
			width = utflite_codepoint_width(codepoint);
			if (width < 0) {
				width = 1;  /* Control characters */
			}

			/* Only render if we have room for the full character width */
			if (rendered_width + width <= max_width) {
				char utf8_buffer[UTFLITE_MAX_BYTES];
				int bytes = utflite_encode(codepoint, utf8_buffer);
				output_buffer_append(output, utf8_buffer, bytes);
				rendered_width += width;
			} else {
				/* Not enough room for wide character, fill with spaces */
				while (rendered_width < max_width) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
			}
		}

		visual_column += width;
		cell_index++;
	}

	/*
	 * Fill remaining space with appropriate background color.
	 * Use cursor_line background if on cursor line for highlight effect.
	 */
	struct syntax_color fill_bg = is_cursor_line ? active_theme.cursor_line : active_theme.background;
	char bg_escape[48];
	snprintf(bg_escape, sizeof(bg_escape), "\x1b[48;2;%d;%d;%dm",
	         fill_bg.red, fill_bg.green, fill_bg.blue);
	output_buffer_append_string(output, bg_escape);

	/* Fill to max_width with spaces */
	while (rendered_width < max_width) {
		output_buffer_append_string(output, " ");
		rendered_width++;
	}
}

/*
 * Ensure a line is at least WARM before rendering.
 * Called from main thread only. If the line is cold, warms it synchronously.
 * Then applies syntax highlighting if needed.
 */
static void ensure_line_warm_for_render(struct line *line, struct buffer *buffer, uint32_t row)
{
	int temp = line_get_temperature(line);

	if (temp == LINE_TEMPERATURE_COLD) {
		/* Must warm synchronously for render */
		if (line_try_claim_warming(line)) {
			/* We claimed it - warm now */
			if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
				int __attribute__((unused)) err = line_warm_from_worker(line, buffer);
			}
			line_release_warming(line);
		} else {
			/* Worker is warming it - spin briefly then fall back */
			for (int i = 0; i < 1000 && line_get_temperature(line) == LINE_TEMPERATURE_COLD; i++) {
				/* Brief spin */
			}

			/* If still cold, force warm (worker may have failed) */
			if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
				/* Wait for worker to release, then warm ourselves */
				while (!line_try_claim_warming(line)) {
					/* Spin */
				}
				if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
					int __attribute__((unused)) err = line_warm_from_worker(line, buffer);
				}
				line_release_warming(line);
			}
		}
	}

	/* At this point line is WARM or HOT */

	/* Compute syntax highlighting if needed (main thread only) */
	temp = line_get_temperature(line);
	if (temp == LINE_TEMPERATURE_WARM) {
		syntax_highlight_line(line, buffer, row);
		/* syntax_highlight_line sets temperature to HOT */
	}
}

/*
 * Render all visible rows of the editor. For each screen row, draws
 * the line number gutter (if enabled) and line content. Handles soft
 * wrap by mapping screen rows to (logical_line, segment) pairs. Empty
 * rows past the end of the file are blank. Shows a centered welcome
 * message for empty buffers.
 */
static void render_draw_rows(struct output_buffer *output)
{
	uint32_t welcome_row = editor.screen_rows / 2;
	int text_area_width = editor.screen_columns - editor.gutter_width;

	/*
	 * Track current position in the buffer.
	 * file_row = logical line index
	 * segment = which segment of that line (0 = first/only)
	 */
	uint32_t file_row = editor.row_offset;
	uint16_t segment = 0;

	/*
	 * Cursor segment is needed to determine if we should highlight
	 * the current screen row as "cursor line".
	 */
	uint16_t cursor_segment = 0;
	if (editor.cursor_row < editor.buffer.line_count) {
		struct line *cursor_line = &editor.buffer.lines[editor.cursor_row];
		cursor_segment = line_get_segment_for_column(cursor_line,
		                                             &editor.buffer,
		                                             editor.cursor_column);
	}

	for (uint32_t screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
		output_buffer_append_string(output, "\x1b[2K");

		bool is_empty_buffer_first_line = (editor.buffer.line_count == 0 && file_row == 0);

		if (file_row >= editor.buffer.line_count && !is_empty_buffer_first_line) {
			/* Empty line past end of file */
			if (editor.buffer.line_count == 0 && screen_row == welcome_row) {
				/* Welcome message */
				char color_escape[128];
				int escape_len = style_to_escape(&active_theme.welcome,
				                                 color_escape, sizeof(color_escape));
				output_buffer_append(output, color_escape, escape_len);

				char welcome[64];
				int welcome_length = snprintf(welcome, sizeof(welcome),
				                              "edit v%s", EDIT_VERSION);
				int padding = (text_area_width - welcome_length) / 2;
				if (padding < 0) padding = 0;

				for (uint32_t i = 0; i < editor.gutter_width; i++) {
					output_buffer_append_string(output, " ");
				}
				for (int i = 0; i < padding; i++) {
					output_buffer_append_string(output, " ");
				}
				output_buffer_append(output, welcome, welcome_length);
			} else if (editor.color_column > 0) {
				/* Draw color column marker on empty lines */
				uint32_t col_pos = editor.color_column - 1;
				if (col_pos < (uint32_t)text_area_width) {
					for (uint32_t i = 0; i < editor.gutter_width + col_pos; i++) {
						output_buffer_append_string(output, " ");
					}
					const char *col_char = color_column_char(editor.color_column_style);
					char col_escape[96];
					if (col_char != NULL) {
						/* Draw line character */
						snprintf(col_escape, sizeof(col_escape),
						         "\x1b[38;2;%d;%d;%dm%s\x1b[48;2;%d;%d;%dm",
						         active_theme.color_column_line.red,
						         active_theme.color_column_line.green,
						         active_theme.color_column_line.blue,
						         col_char,
						         active_theme.background.red, active_theme.background.green,
						         active_theme.background.blue);
					} else {
						/* Background tint only */
						snprintf(col_escape, sizeof(col_escape),
						         "\x1b[48;2;%d;%d;%dm \x1b[48;2;%d;%d;%dm",
						         active_theme.color_column.red, active_theme.color_column.green,
						         active_theme.color_column.blue,
						         active_theme.background.red, active_theme.background.green,
						         active_theme.background.blue);
					}
					output_buffer_append_string(output, col_escape);
				}
			}
		} else if (file_row < editor.buffer.line_count) {
			struct line *line = &editor.buffer.lines[file_row];

			/* Ensure line is warmed before rendering */
			ensure_line_warm_for_render(line, &editor.buffer, file_row);

			line_ensure_wrap_cache(line, &editor.buffer);

			/*
			 * Determine if this screen row should have cursor line highlight.
			 * Only highlight the segment containing the cursor.
			 */
			bool is_cursor_line_segment =
				(file_row == editor.cursor_row && segment == cursor_segment);

			/* Draw gutter: line number for segment 0, indicator for continuations */
			if (editor.show_line_numbers && editor.gutter_width > 0) {
				if (segment == 0) {
					/* First segment: show line number */
					struct style *ln_style = is_cursor_line_segment
						? &active_theme.line_number_active : &active_theme.line_number;
					struct syntax_color ln_bg = is_cursor_line_segment
						? active_theme.gutter_active.bg : active_theme.gutter.bg;

					char color_escape[128];
					int escape_len = style_to_escape_with_bg(ln_style, ln_bg,
					                                         color_escape, sizeof(color_escape));
					output_buffer_append(output, color_escape, escape_len);

					char line_number_buffer[16];
					snprintf(line_number_buffer, sizeof(line_number_buffer),
					         "%*u ", editor.gutter_width - 1, file_row + 1);
					output_buffer_append(output, line_number_buffer,
					                     editor.gutter_width);
				} else {
					/* Continuation: show wrap indicator */
					struct syntax_color wrap_bg = is_cursor_line_segment
						? active_theme.gutter_active.bg : active_theme.wrap_indicator.bg;

					char color_escape[128];
					int escape_len = style_to_escape_with_bg(&active_theme.wrap_indicator,
					                                         wrap_bg, color_escape,
					                                         sizeof(color_escape));
					output_buffer_append(output, color_escape, escape_len);

					const char *indicator = wrap_indicator_string(editor.wrap_indicator);
					/* Pad to align indicator same as line numbers (with trailing space) */
					for (uint32_t i = 0; i < editor.gutter_width - 2; i++) {
						output_buffer_append_string(output, " ");
					}
					output_buffer_append_string(output, indicator);
					output_buffer_append_string(output, " ");
				}
			}

			/* Calculate segment bounds */
			uint32_t start_cell = line_get_segment_start(line, &editor.buffer, segment);
			uint32_t end_cell = line_get_segment_end(line, &editor.buffer, segment);

			/*
			 * For WRAP_NONE mode, use horizontal scrolling.
			 * For wrap modes, render the segment directly.
			 */
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: use column_offset for horizontal scrolling */
				render_line_content(output, line, &editor.buffer, file_row,
				                    editor.column_offset, UINT32_MAX,
				                    text_area_width, is_cursor_line_segment);
			} else {
				/* Wrap enabled: render this segment */
				render_line_content(output, line, &editor.buffer, file_row,
				                    start_cell, end_cell,
				                    text_area_width, is_cursor_line_segment);
			}

			/* Fill rest of line with appropriate background */
			if (is_cursor_line_segment) {
				if (editor.color_column > 0) {
					/*
					 * Cursor line with color column: calculate position
					 * and draw the column marker with cursor line background.
					 */
					uint32_t line_visual_width = 0;
					for (uint32_t i = 0; i < line->cell_count; i++) {
						uint32_t cp = line->cells[i].codepoint;
						if (cp == '\t') {
							line_visual_width += TAB_STOP_WIDTH -
								(line_visual_width % TAB_STOP_WIDTH);
						} else {
							int w = utflite_codepoint_width(cp);
							line_visual_width += (w > 0) ? w : 1;
						}
					}
					uint32_t col_pos = editor.color_column - 1;
					if (col_pos >= line_visual_width &&
					    col_pos < line_visual_width + (uint32_t)text_area_width) {
						/* Set cursor line bg and fill to column */
						char cursor_bg[48];
						snprintf(cursor_bg, sizeof(cursor_bg),
						         "\x1b[48;2;%d;%d;%dm",
						         active_theme.cursor_line.red,
						         active_theme.cursor_line.green,
						         active_theme.cursor_line.blue);
						output_buffer_append_string(output, cursor_bg);
						uint32_t spaces_before = col_pos - line_visual_width;
						for (uint32_t i = 0; i < spaces_before; i++) {
							output_buffer_append_string(output, " ");
						}
						/* Draw color column marker */
						const char *col_char = color_column_char(editor.color_column_style);
						char col_escape[96];
						if (col_char != NULL) {
							/* Draw line character */
							snprintf(col_escape, sizeof(col_escape),
							         "\x1b[38;2;%d;%d;%dm%s\x1b[48;2;%d;%d;%dm\x1b[K",
							         active_theme.color_column_line.red,
							         active_theme.color_column_line.green,
							         active_theme.color_column_line.blue,
							         col_char,
							         active_theme.cursor_line.red,
							         active_theme.cursor_line.green,
							         active_theme.cursor_line.blue);
						} else {
							/* Background tint only */
							snprintf(col_escape, sizeof(col_escape),
							         "\x1b[48;2;%d;%d;%dm \x1b[48;2;%d;%d;%dm\x1b[K",
							         active_theme.color_column.red,
							         active_theme.color_column.green,
							         active_theme.color_column.blue,
							         active_theme.cursor_line.red,
							         active_theme.cursor_line.green,
							         active_theme.cursor_line.blue);
						}
						output_buffer_append_string(output, col_escape);
					} else {
						/* Column not in visible area, just fill */
						char fill_escape[64];
						snprintf(fill_escape, sizeof(fill_escape),
						         "\x1b[48;2;%d;%d;%dm\x1b[K",
						         active_theme.cursor_line.red,
						         active_theme.cursor_line.green,
						         active_theme.cursor_line.blue);
						output_buffer_append_string(output, fill_escape);
					}
				} else {
					char fill_escape[64];
					snprintf(fill_escape, sizeof(fill_escape),
					         "\x1b[48;2;%d;%d;%dm\x1b[K",
					         active_theme.cursor_line.red,
					         active_theme.cursor_line.green,
					         active_theme.cursor_line.blue);
					output_buffer_append_string(output, fill_escape);
				}
				/* Reset to normal background */
				char reset_bg[48];
				snprintf(reset_bg, sizeof(reset_bg),
				         "\x1b[48;2;%d;%d;%dm",
				         active_theme.background.red, active_theme.background.green,
				         active_theme.background.blue);
				output_buffer_append_string(output, reset_bg);
			} else if (editor.color_column > 0) {
				/*
				 * Draw color column marker in empty area if applicable.
				 * Calculate where we are and if the color column is visible.
				 */
				uint32_t line_visual_width = 0;
				line_warm(line, &editor.buffer);
				for (uint32_t i = 0; i < line->cell_count; i++) {
					uint32_t cp = line->cells[i].codepoint;
					if (cp == '\t') {
						line_visual_width += TAB_STOP_WIDTH -
							(line_visual_width % TAB_STOP_WIDTH);
					} else {
						int w = utflite_codepoint_width(cp);
						line_visual_width += (w > 0) ? w : 1;
					}
				}
				uint32_t col_pos = editor.color_column - 1;
				if (col_pos >= line_visual_width &&
				    col_pos < line_visual_width + (uint32_t)text_area_width) {
					uint32_t spaces_before = col_pos - line_visual_width;
					for (uint32_t i = 0; i < spaces_before; i++) {
						output_buffer_append_string(output, " ");
					}
					const char *col_char = color_column_char(editor.color_column_style);
					char col_escape[96];
					if (col_char != NULL) {
						/* Draw line character */
						snprintf(col_escape, sizeof(col_escape),
						         "\x1b[38;2;%d;%d;%dm%s\x1b[48;2;%d;%d;%dm\x1b[K",
						         active_theme.color_column_line.red,
						         active_theme.color_column_line.green,
						         active_theme.color_column_line.blue,
						         col_char,
						         active_theme.background.red, active_theme.background.green,
						         active_theme.background.blue);
					} else {
						/* Background tint only */
						snprintf(col_escape, sizeof(col_escape),
						         "\x1b[48;2;%d;%d;%dm \x1b[48;2;%d;%d;%dm\x1b[K",
						         active_theme.color_column.red, active_theme.color_column.green,
						         active_theme.color_column.blue,
						         active_theme.background.red, active_theme.background.green,
						         active_theme.background.blue);
					}
					output_buffer_append_string(output, col_escape);
				}
			}

			/* Advance to next segment or line */
			segment++;
			if (segment >= line->wrap_segment_count) {
				segment = 0;
				file_row++;
			}
		}

		output_buffer_append_string(output, "\r\n");
	}
}

/*
 * Draw the status bar using theme colors. Shows the filename (or
 * "[No Name]") on the left with a [+] indicator if modified, and the
 * cursor position (current line / total lines) on the right.
 */
static void render_draw_status_bar(struct output_buffer *output)
{
	char color_escape[128];
	int escape_len;

	/* Status bar base background */
	snprintf(color_escape, sizeof(color_escape),
	         "\x1b[0m\x1b[48;2;%u;%u;%um",
	         active_theme.status.bg.red, active_theme.status.bg.green,
	         active_theme.status.bg.blue);
	output_buffer_append_string(output, color_escape);

	/* Filename with its own style */
	const char *filename = editor.buffer.filename ? editor.buffer.filename : "[No Name]";
	char filename_buf[104];
	int filename_len = snprintf(filename_buf, sizeof(filename_buf), " %.100s", filename);

	escape_len = style_to_escape(&active_theme.status_filename, color_escape,
	                             sizeof(color_escape));
	output_buffer_append(output, color_escape, escape_len);
	output_buffer_append(output, filename_buf, filename_len);

	int current_pos = filename_len;

	/* Modified indicator with its own style */
	if (editor.buffer.is_modified) {
		escape_len = style_to_escape(&active_theme.status_modified, color_escape,
		                             sizeof(color_escape));
		output_buffer_append(output, color_escape, escape_len);
		output_buffer_append_string(output, " [+]");
		current_pos += 4;
	}

	/* Right-aligned position with its own style */
	char right_status[64];
	int right_length = snprintf(right_status, sizeof(right_status), "%u/%u ",
	                            editor.cursor_row + 1, editor.buffer.line_count);

	/* Fill with base status bar style */
	escape_len = style_to_escape(&active_theme.status, color_escape, sizeof(color_escape));
	output_buffer_append(output, color_escape, escape_len);

	while (current_pos < (int)editor.screen_columns) {
		if ((int)editor.screen_columns - current_pos == right_length) {
			/* Position indicator with its own style */
			escape_len = style_to_escape(&active_theme.status_position, color_escape,
			                             sizeof(color_escape));
			output_buffer_append(output, color_escape, escape_len);
			output_buffer_append(output, right_status, right_length);
			break;
		} else {
			output_buffer_append_string(output, " ");
			current_pos++;
		}
	}

	/* Reset attributes */
	output_buffer_append_string(output, "\x1b[m");
	output_buffer_append_string(output, "\r\n");
}

/*
 * Helper to set message bar style.
 */
static void message_bar_set_style(struct output_buffer *output,
                                  const struct style *style)
{
	char escape[128];
	int len = style_to_escape(style, escape, sizeof(escape));
	output_buffer_append(output, escape, len);
}

/*
 * Draw the message bar at the bottom of the screen. Shows the current
 * status message if one was set within the last 5 seconds. Uses theme
 * colors for the message bar background and text.
 */
static void render_draw_message_bar(struct output_buffer *output)
{
	/* Set message bar style from theme */
	message_bar_set_style(output, &active_theme.message);

	/* Clear line with message bar background color */
	output_buffer_append_string(output, "\x1b[K");

	if (save_as.active) {
		if (save_as.confirm_overwrite) {
			/* Warning prompt */
			message_bar_set_style(output, &active_theme.prompt_warning);
			output_buffer_append_string(output, "File exists. Overwrite? (y/n)");
		} else {
			/* Label */
			message_bar_set_style(output, &active_theme.prompt_label);
			output_buffer_append_string(output, "Save as: ");

			/* Input */
			message_bar_set_style(output, &active_theme.prompt_input);
			char path_buf[PATH_MAX];
			int path_len = strlen(save_as.path);
			int max_len = (int)editor.screen_columns - 9;
			if (path_len > max_len && max_len > 4) {
				/* Truncate from the left */
				snprintf(path_buf, sizeof(path_buf), "...%s",
				         save_as.path + path_len - max_len + 3);
				output_buffer_append_string(output, path_buf);
			} else {
				output_buffer_append(output, save_as.path, path_len);
			}
		}
		return;
	}

	if (search.active) {
		bool has_regex_error = search.use_regex && !search.regex_compiled && search.query_length > 0;

		/* Label: "Search" or "Find" */
		message_bar_set_style(output, &active_theme.prompt_label);
		output_buffer_append_string(output, search.replace_mode ? "Find" : "Search");

		/* Options [CWR!] */
		if (search.case_sensitive || search.whole_word || search.use_regex) {
			output_buffer_append_string(output, " ");
			message_bar_set_style(output, &active_theme.search_options);
			output_buffer_append_string(output, "[");
			if (search.case_sensitive)
				output_buffer_append_string(output, "C");
			if (search.whole_word)
				output_buffer_append_string(output, "W");
			if (search.use_regex) {
				output_buffer_append_string(output, "R");
				if (has_regex_error) {
					message_bar_set_style(output, &active_theme.search_error);
					output_buffer_append_string(output, "!");
					message_bar_set_style(output, &active_theme.search_options);
				}
			}
			output_buffer_append_string(output, "]");
		}

		/* Colon separator */
		message_bar_set_style(output, &active_theme.prompt_label);
		output_buffer_append_string(output, ": ");

		if (search.replace_mode) {
			/* Replace mode: Find: [query] | Replace: [replace] */
			if (!search.editing_replace) {
				message_bar_set_style(output, &active_theme.prompt_bracket);
				output_buffer_append_string(output, "[");
			}
			message_bar_set_style(output, &active_theme.prompt_input);
			output_buffer_append_string(output, search.query);
			if (!search.editing_replace) {
				message_bar_set_style(output, &active_theme.prompt_bracket);
				output_buffer_append_string(output, "]");
			}

			/* No match indicator after search query */
			if (search.query_length > 0 && !search.has_match) {
				message_bar_set_style(output, &active_theme.search_nomatch);
				output_buffer_append_string(output, " (no match)");
			}

			message_bar_set_style(output, &active_theme.prompt_label);
			output_buffer_append_string(output, " | Replace: ");

			if (search.editing_replace) {
				message_bar_set_style(output, &active_theme.prompt_bracket);
				output_buffer_append_string(output, "[");
			}
			message_bar_set_style(output, &active_theme.prompt_input);
			output_buffer_append_string(output, search.replace_text);
			if (search.editing_replace) {
				message_bar_set_style(output, &active_theme.prompt_bracket);
				output_buffer_append_string(output, "]");
			}
		} else {
			/* Search-only mode */
			message_bar_set_style(output, &active_theme.prompt_input);
			output_buffer_append_string(output, search.query);

			if (search.query_length > 0 && !search.has_match) {
				message_bar_set_style(output, &active_theme.search_nomatch);
				output_buffer_append_string(output, " (no match)");
			}
		}
		return;
	}

	if (goto_line.active) {
		/* Label */
		message_bar_set_style(output, &active_theme.prompt_label);
		output_buffer_append_string(output, "Go to line: ");

		/* Input */
		message_bar_set_style(output, &active_theme.prompt_input);
		output_buffer_append_string(output, goto_line.input);
		return;
	}

	int message_length = strlen(editor.status_message);

	if (message_length > (int)editor.screen_columns) {
		message_length = editor.screen_columns;
	}

	if (message_length > 0 && time(NULL) - editor.status_message_time < STATUS_MESSAGE_TIMEOUT) {
		output_buffer_append(output, editor.status_message, message_length);
	}
}

/*
 * Refresh the entire screen. Updates the gutter width and scroll offsets,
 * then redraws all rows, the status bar, and message bar. Positions the
 * cursor using rendered column to account for tabs and wide characters.
 * The cursor is hidden during drawing to avoid flicker.
 */
static int __must_check render_refresh_screen(void)
{
	editor_update_gutter_width();
	editor_scroll();

	struct output_buffer output;
	int ret = output_buffer_init_checked(&output);
	if (ret)
		return ret;

	/* Hide cursor */
	output_buffer_append_string(&output, "\x1b[?25l");
	/* Move cursor to home */
	output_buffer_append_string(&output, "\x1b[H");

	/* Set background color for the entire screen */
	char bg_escape[32];
	snprintf(bg_escape, sizeof(bg_escape), "\x1b[48;2;%d;%d;%dm",
	         active_theme.background.red, active_theme.background.green, active_theme.background.blue);
	output_buffer_append_string(&output, bg_escape);

	render_draw_rows(&output);
	render_draw_status_bar(&output);
	render_draw_message_bar(&output);

	/* Position cursor - account for wrapped segments in wrap mode */
	char cursor_position[32];
	uint32_t cursor_screen_row;
	uint32_t cursor_screen_col;

	if (editor.wrap_mode == WRAP_NONE) {
		/* No wrap: simple calculation */
		cursor_screen_row = (editor.cursor_row - editor.row_offset) + 1;
		uint32_t render_column = editor_get_render_column(
			editor.cursor_row, editor.cursor_column);
		cursor_screen_col = (render_column - editor.column_offset) +
		                    editor.gutter_width + 1;
	} else {
		/*
		 * Wrap enabled: sum screen rows from row_offset to cursor_row,
		 * then add the cursor's segment within its line.
		 */
		cursor_screen_row = 1;  /* 1-based terminal rows */
		for (uint32_t row = editor.row_offset; row < editor.cursor_row &&
		     row < editor.buffer.line_count; row++) {
			struct line *line = &editor.buffer.lines[row];
			line_ensure_wrap_cache(line, &editor.buffer);
			cursor_screen_row += line->wrap_segment_count;
		}

		/* Add the segment offset within cursor's line */
		if (editor.cursor_row < editor.buffer.line_count) {
			struct line *cursor_line = &editor.buffer.lines[editor.cursor_row];
			line_ensure_wrap_cache(cursor_line, &editor.buffer);
			uint16_t cursor_segment = line_get_segment_for_column(
				cursor_line, &editor.buffer, editor.cursor_column);
			cursor_screen_row += cursor_segment;

			/* Column is visual position within segment */
			uint32_t visual_col = line_get_visual_column_in_segment(
				cursor_line, &editor.buffer, cursor_segment,
				editor.cursor_column);
			cursor_screen_col = visual_col + editor.gutter_width + 1;
		} else {
			cursor_screen_col = editor.gutter_width + 1;
		}
	}

	snprintf(cursor_position, sizeof(cursor_position), "\x1b[%u;%uH",
	         cursor_screen_row, cursor_screen_col);
	output_buffer_append_string(&output, cursor_position);

	/* Show cursor */
	output_buffer_append_string(&output, "\x1b[?25h");

	output_buffer_flush(&output);
	output_buffer_free(&output);
	return 0;
}

/*****************************************************************************
 * Dialog Panel System
 *****************************************************************************/

/*
 * Free a single file list item and its allocated strings.
 */
static void file_list_item_free(struct file_list_item *item)
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
static void file_list_free(struct file_list_item *items, int count)
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
static struct file_list_item *file_list_read_directory(const char *path, int *count)
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

/*
 * Calculate dialog panel dimensions based on screen size.
 * Panel is centered, 50% height, 70% width, with minimum sizes.
 */
static void dialog_calculate_dimensions(struct dialog_state *dialog)
{
	const int minimum_width = 40;
	const int minimum_height = 10;

	/* 70% of screen width, at least minimum */
	dialog->panel_width = (editor.screen_columns * 70) / 100;
	if (dialog->panel_width < minimum_width) {
		dialog->panel_width = minimum_width;
	}
	if (dialog->panel_width > (int)editor.screen_columns - 2) {
		dialog->panel_width = (int)editor.screen_columns - 2;
	}

	/* 50% of screen height, at least minimum */
	dialog->panel_height = (editor.screen_rows * 50) / 100;
	if (dialog->panel_height < minimum_height) {
		dialog->panel_height = minimum_height;
	}
	if (dialog->panel_height > (int)editor.screen_rows - 2) {
		dialog->panel_height = (int)editor.screen_rows - 2;
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
static void dialog_ensure_visible(struct dialog_state *dialog)
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
static void dialog_clamp_selection(struct dialog_state *dialog)
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
 * Set foreground color for dialog output.
 */
static void dialog_set_fg(struct output_buffer *output, struct syntax_color color)
{
	char escape[32];
	snprintf(escape, sizeof(escape), "\x1b[38;2;%d;%d;%dm",
		color.red, color.green, color.blue);
	output_buffer_append_string(output, escape);
}

/*
 * Set background color for dialog output.
 */
static void dialog_set_bg(struct output_buffer *output, struct syntax_color color)
{
	char escape[32];
	snprintf(escape, sizeof(escape), "\x1b[48;2;%d;%d;%dm",
		color.red, color.green, color.blue);
	output_buffer_append_string(output, escape);
}

/*
 * Set full style (fg, bg, attributes) for dialog output.
 */
static void dialog_set_style(struct output_buffer *output, const struct style *style)
{
	char escape[128];
	int len = style_to_escape(style, escape, sizeof(escape));
	output_buffer_append(output, escape, len);
}

/*
 * Move cursor to dialog row and column (1-based terminal coordinates).
 */
static void dialog_goto(struct output_buffer *output, int row, int column)
{
	char escape[32];
	snprintf(escape, sizeof(escape), "\x1b[%d;%dH", row, column);
	output_buffer_append_string(output, escape);
}

/*
 * Draw the dialog header with title centered.
 */
static void dialog_draw_header(struct output_buffer *output,
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
 * Draw the dialog footer with hint text.
 */
static void dialog_draw_footer(struct output_buffer *output,
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
static void dialog_draw_empty_row(struct output_buffer *output,
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
static void dialog_draw_list_item(struct output_buffer *output,
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
static enum dialog_result dialog_handle_key(struct dialog_state *dialog, int key)
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
static enum dialog_result dialog_handle_mouse(struct dialog_state *dialog,
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

/*
 * Get the parent directory of a path.
 * Returns newly allocated string, caller must free.
 * Returns "/" for root path or paths without parent.
 */
static char *path_get_parent(const char *path)
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
static char *path_join(const char *directory, const char *filename)
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

/*
 * Close the dialog and restore normal editor state.
 */
static void dialog_close(struct dialog_state *dialog)
{
	dialog->active = false;
	dialog_mouse_mode = false;

	/* Show cursor again now that dialog is closed */
	write(STDOUT_FILENO, "\x1b[?25h", 6);
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
	output_buffer_append_string(&output, "\x1b[?25l");

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
	                   "Enter:Open  Left:Parent  Esc:Cancel");

	/* Reset attributes, keep cursor hidden while dialog is active */
	output_buffer_append_string(&output, "\x1b[0m");

	output_buffer_flush(&output);
	output_buffer_free(&output);
}

/*
 * Show the Open File dialog.
 * Returns allocated file path if user selected a file, NULL if cancelled.
 * Caller must free the returned path.
 */
static char *open_file_dialog(void)
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
	dialog_mouse_mode = true;
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
		if (key == -2) {
			if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns)) {
				editor.screen_rows = 24;
				editor.screen_columns = 80;
			}
			int __unused = render_refresh_screen(); (void)__unused;
			continue;
		}

		/* Check for mouse event */
		if (key == KEY_MOUSE_EVENT) {
			enum dialog_result dr = dialog_handle_mouse(&open_file.dialog, &dialog_last_mouse);

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

/*
 * Open a file, replacing current buffer.
 * Returns true if file was opened successfully.
 */
static bool editor_open_file(const char *path)
{
	/* Clear existing buffer */
	buffer_free(&editor.buffer);

	/* Reset editor state */
	editor.cursor_row = 0;
	editor.cursor_column = 0;
	editor.row_offset = 0;
	editor.column_offset = 0;
	editor.selection_active = false;

	/* Exit multi-cursor mode if active */
	if (editor.cursor_count > 0) {
		multicursor_exit();
	}

	/* Initialize new buffer */
	buffer_init(&editor.buffer);

	/* Load the file */
	int ret = file_open(&editor.buffer, path);
	if (ret) {
		/* Failed to load - show error with reason */
		editor_set_status_message("Cannot open file: %s", edit_strerror(ret));
		return false;
	}

	editor_set_status_message("Opened: %s (%u lines)", path, editor.buffer.line_count);

	/* Start background warming for the new file */
	editor_request_background_warming();

	return true;
}

/*
 * Handle the Ctrl+O command to open a file.
 */
static void editor_command_open_file(void)
{
	/* Warn about unsaved changes */
	static bool warned = false;
	if (editor.buffer.is_modified && !warned) {
		editor_set_status_message("Unsaved changes! Press Ctrl+O again to open anyway");
		warned = true;
		return;
	}
	warned = false;

	/* Show open file dialog */
	char *path = open_file_dialog();

	/* Redraw screen after dialog closes */
	int __unused = render_refresh_screen(); (void)__unused;

	if (path) {
		editor_open_file(path);
		free(path);
	} else {
		editor_set_status_message("Open cancelled");
	}
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
	output_buffer_append_string(&output, "\x1b[?25l");

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
				output_buffer_append_string(&output, "■");
				name_len++;

				/* Keyword color square */
				dialog_set_fg(&output, t->syntax[SYNTAX_KEYWORD].fg);
				output_buffer_append_string(&output, "■");
				name_len++;

				/* String color square */
				dialog_set_fg(&output, t->syntax[SYNTAX_STRING].fg);
				output_buffer_append_string(&output, "■");
				name_len++;

				/* Comment color square */
				dialog_set_fg(&output, t->syntax[SYNTAX_COMMENT].fg);
				output_buffer_append_string(&output, "■");
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
	output_buffer_append_string(&output, "\x1b[0m");

	output_buffer_flush(&output);
	output_buffer_free(&output);
}

/*
 * Show the Theme Picker dialog with live preview.
 * Returns selected theme index, or -1 if cancelled.
 */
static int theme_picker_dialog(void)
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
	dialog_mouse_mode = true;
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
		if (key == -2) {
			if (terminal_get_window_size(&editor.screen_rows, &editor.screen_columns)) {
				editor.screen_rows = 24;
				editor.screen_columns = 80;
			}
			int __unused = render_refresh_screen(); (void)__unused;
			continue;
		}

		/* Check for mouse event */
		if (key == KEY_MOUSE_EVENT) {
			enum dialog_result dr = dialog_handle_mouse(&theme_picker.dialog, &dialog_last_mouse);

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

/*
 * Handle the F5/Ctrl+T command to open theme picker.
 */
static void editor_command_theme_picker(void)
{
	int selected = theme_picker_dialog();

	/* Redraw screen after dialog closes */
	int __unused = render_refresh_screen(); (void)__unused;

	if (selected >= 0) {
		editor_set_status_message("Switched to %s theme", active_theme.name);
	} else {
		editor_set_status_message("Theme selection cancelled");
	}
}

/*****************************************************************************
 * Main Loop
 *****************************************************************************/

/*
 * Process a single keypress and dispatch to the appropriate handler.
 * Handles Ctrl-Q (quit with confirmation for unsaved changes), Ctrl-S
 * (save), F2 (toggle line numbers), arrow keys, and character insertion.
 * Terminal resize signals are handled by updating screen dimensions.
 */
/*
 * Handle a keypress in search mode. Returns true if the key was handled.
 * In replace mode, handles Tab to toggle fields, Enter to replace, and
 * Alt+A to replace all.
 */
static bool search_handle_key(int key)
{
	if (!search.active) {
		return false;
	}

	switch (key) {
		case '\x1b':  /* Escape - cancel search */
			search_exit(true);  /* Restore position */
			editor_set_status_message(search.replace_mode ? "Replace cancelled" : "Search cancelled");
			return true;

		case '\r':  /* Enter */
			if (search.replace_mode && search.has_match) {
				/* Replace current match and find next */
				search_replace_and_next();
			} else {
				/* Exit search, keep current position */
				search_exit(false);
				if (search.query_length > 0) {
					editor_set_status_message("Found: %s", search.query);
				}
			}
			return true;

		case '\t':  /* Tab */
			if (search.replace_mode) {
				/* Toggle between search and replace fields */
				search.editing_replace = !search.editing_replace;
			} else if (search.has_match) {
				/* In search-only mode, skip to next match */
				search.direction = 1;
				if (!search_find_next(true)) {
					editor_set_status_message("No more matches");
				}
			}
			return true;

		case KEY_ALT_A:
			/* Replace all (only in replace mode) */
			if (search.replace_mode) {
				if (should_use_async_replace()) {
					async_replace_start(search.query, search.replace_text,
					                    search.use_regex, search.case_sensitive,
					                    search.whole_word);
				} else {
					search_replace_all();
				}
			}
			return true;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			if (search.replace_mode && search.editing_replace) {
				/* Delete from replace field */
				if (search.replace_length > 0) {
					uint32_t i = search.replace_length - 1;
					while (i > 0 && (search.replace_text[i] & 0xC0) == 0x80) {
						i--;
					}
					search.replace_length = i;
					search.replace_text[search.replace_length] = '\0';
				}
			} else {
				/* Delete from search field */
				if (search.query_length > 0) {
					uint32_t i = search.query_length - 1;
					while (i > 0 && (search.query[i] & 0xC0) == 0x80) {
						i--;
					}
					search.query_length = i;
					search.query[search.query_length] = '\0';
					search_update();
				}
			}
			return true;

		case KEY_ALT_N:
		case KEY_ARROW_DOWN:
		case KEY_ARROW_RIGHT:
			/* Find next match */
			search.direction = 1;
			if (should_use_async_search()) {
				if (!async_search_next_match()) {
					editor_set_status_message("No more matches");
				}
			} else {
				if (!search_find_next(true)) {
					editor_set_status_message("No more matches");
				}
			}
			return true;

		case KEY_ALT_P:
		case KEY_ARROW_UP:
		case KEY_ARROW_LEFT:
			/* Find previous match */
			search.direction = -1;
			if (should_use_async_search()) {
				if (!async_search_prev_match()) {
					editor_set_status_message("No more matches");
				}
			} else {
				if (!search_find_previous(true)) {
					editor_set_status_message("No more matches");
				}
			}
			return true;

		case KEY_ALT_C:
			/* Toggle case sensitivity */
			search.case_sensitive = !search.case_sensitive;
			if (search.use_regex) {
				search_compile_regex();
			}
			search_update();
			editor_set_status_message("Case %s",
			                          search.case_sensitive ? "sensitive" : "insensitive");
			return true;

		case KEY_ALT_W:
			/* Toggle whole word matching */
			search.whole_word = !search.whole_word;
			search_update();
			editor_set_status_message("Whole word %s",
			                          search.whole_word ? "ON" : "OFF");
			return true;

		case KEY_ALT_R:
			/* Toggle regex mode */
			search.use_regex = !search.use_regex;
			if (search.use_regex) {
				search_compile_regex();
				if (!search.regex_compiled && search.query_length > 0) {
					editor_set_status_message("Regex error: %s", search.regex_error);
					return true;
				}
			} else {
				if (search.regex_compiled) {
					regfree(&search.compiled_regex);
					search.regex_compiled = false;
				}
			}
			search_update();
			editor_set_status_message("Regex %s", search.use_regex ? "ON" : "OFF");
			return true;

		default:
			/* Insert printable character */
			if ((key >= 32 && key < 127) || key >= 128) {
				char utf8[4];
				int bytes = utflite_encode((uint32_t)key, utf8);

				if (search.replace_mode && search.editing_replace) {
					/* Insert into replace field */
					if (bytes > 0 && search.replace_length + (uint32_t)bytes < sizeof(search.replace_text) - 1) {
						memcpy(search.replace_text + search.replace_length, utf8, bytes);
						search.replace_length += bytes;
						search.replace_text[search.replace_length] = '\0';
					}
				} else {
					/* Insert into search field */
					if (bytes > 0 && search.query_length + (uint32_t)bytes < sizeof(search.query) - 1) {
						memcpy(search.query + search.query_length, utf8, bytes);
						search.query_length += bytes;
						search.query[search.query_length] = '\0';
						search_update();
					}
				}
				return true;
			}
			break;
	}

	return false;
}

/*****************************************************************************
 * Go to Line
 *****************************************************************************/

/*
 * Enter go-to-line mode. Saves cursor position for cancel.
 */
static void goto_enter(void)
{
	goto_line.active = true;
	goto_line.input[0] = '\0';
	goto_line.input_length = 0;
	goto_line.saved_cursor_row = editor.cursor_row;
	goto_line.saved_cursor_column = editor.cursor_column;
	goto_line.saved_row_offset = editor.row_offset;
	editor_set_status_message("");
}

/*
 * Exit go-to-line mode, optionally restoring cursor position.
 */
static void goto_exit(bool restore)
{
	if (restore) {
		editor.cursor_row = goto_line.saved_cursor_row;
		editor.cursor_column = goto_line.saved_cursor_column;
		editor.row_offset = goto_line.saved_row_offset;
	}
	goto_line.active = false;
}

/*
 * Execute the go-to-line command. Jumps to the line number in input.
 */
static void goto_execute(void)
{
	if (goto_line.input_length == 0) {
		goto_exit(true);
		return;
	}

	/* Parse line number */
	long line_num = strtol(goto_line.input, NULL, 10);

	if (line_num < 1) {
		line_num = 1;
	}
	if (line_num > (long)editor.buffer.line_count) {
		line_num = editor.buffer.line_count;
	}
	if (editor.buffer.line_count == 0) {
		line_num = 1;
	}

	/* Jump to line (1-indexed input, 0-indexed internal) */
	editor.cursor_row = (uint32_t)(line_num - 1);
	editor.cursor_column = 0;

	/* Center the line on screen */
	uint32_t half_screen = editor.screen_rows / 2;
	if (editor.cursor_row >= half_screen) {
		editor.row_offset = editor.cursor_row - half_screen;
	} else {
		editor.row_offset = 0;
	}

	/* Clamp row_offset (wrap-aware) */
	uint32_t max_offset = calculate_max_row_offset();
	if (editor.row_offset > max_offset) {
		editor.row_offset = max_offset;
	}

	selection_clear();
	goto_exit(false);
	editor_set_status_message("Line %ld", line_num);
}

/*
 * Handle keypress in go-to-line mode. Returns true if handled.
 */
static bool goto_handle_key(int key)
{
	if (!goto_line.active) {
		return false;
	}

	switch (key) {
		case '\x1b':  /* Escape - cancel */
			goto_exit(true);
			editor_set_status_message("Cancelled");
			return true;

		case '\r':  /* Enter - execute */
			goto_execute();
			return true;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			if (goto_line.input_length > 0) {
				goto_line.input_length--;
				goto_line.input[goto_line.input_length] = '\0';
			}
			return true;

		default:
			/* Accept digits only */
			if (key >= '0' && key <= '9') {
				if (goto_line.input_length < sizeof(goto_line.input) - 1) {
					goto_line.input[goto_line.input_length++] = (char)key;
					goto_line.input[goto_line.input_length] = '\0';

					/* Live preview: jump as user types */
					long line_num = strtol(goto_line.input, NULL, 10);
					if (line_num >= 1 && line_num <= (long)editor.buffer.line_count) {
						editor.cursor_row = (uint32_t)(line_num - 1);
						editor.cursor_column = 0;

						/* Center on screen */
						uint32_t half_screen = editor.screen_rows / 2;
						if (editor.cursor_row >= half_screen) {
							editor.row_offset = editor.cursor_row - half_screen;
						} else {
							editor.row_offset = 0;
						}
					}
				}
				return true;
			}
			break;
	}

	return true;  /* Consume all keys in goto mode */
}

/*****************************************************************************
 * Quit Prompt
 *****************************************************************************/

/*
 * Perform the actual exit - cleanup and terminate.
 * Called when quitting normally or after user confirms quit.
 */
static void editor_perform_exit(void)
{
	terminal_clear_screen();
	/* Remove swap file on clean exit (no unsaved changes) */
	if (!editor.buffer.is_modified) {
		autosave_remove_swap();
	}
	async_replace_cleanup();
	async_search_cleanup();
	worker_shutdown();
	free(internal_clipboard);
	buffer_free(&editor.buffer);
	themes_free();
	free(active_theme.name);
	exit(0);
}

/*
 * Enter quit prompt mode - ask user what to do with unsaved changes.
 */
static void quit_prompt_enter(void)
{
	quit_prompt.active = true;
	editor_set_status_message("Unsaved changes! Save before quitting? [y]es [n]o [c]ancel: ");
}

/*
 * Handle a keypress in quit prompt mode.
 * Returns true if the key was consumed.
 */
static bool quit_prompt_handle_key(int key)
{
	if (!quit_prompt.active) {
		return false;
	}

	switch (key) {
		case 'y':
		case 'Y':
			/* Save and quit */
			quit_prompt.active = false;
			if (editor.buffer.filename == NULL) {
				/* No filename - need to do Save As first */
				editor_set_status_message("No filename. Use Ctrl-Shift-S to Save As, then quit.");
				return true;
			}
			editor_save();
			if (!editor.buffer.is_modified) {
				/* Save succeeded */
				editor_perform_exit();
			}
			/* Save failed - message already set by editor_save */
			return true;

		case 'n':
		case 'N':
			/* Quit without saving */
			quit_prompt.active = false;
			editor_perform_exit();
			return true;

		case 'c':
		case 'C':
		case '\x1b':  /* Escape */
		case CONTROL_KEY('q'):  /* Ctrl+Q again cancels */
			/* Cancel - return to editing */
			quit_prompt.active = false;
			editor_set_status_message("Quit cancelled");
			return true;

		default:
			/* Invalid key - remind user of options */
			editor_set_status_message("Unsaved changes! Save before quitting? [y]es [n]o [c]ancel: ");
			return true;
	}
}

/*****************************************************************************
 * Save As
 *****************************************************************************/

/*
 * Check if a file exists at the given path.
 */
static bool file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

/*
 * Enter Save As mode. Pre-fills with current filename if available.
 */
static void save_as_enter(void)
{
	save_as.active = true;
	save_as.confirm_overwrite = false;

	if (editor.buffer.filename != NULL) {
		/* Start with current filename */
		size_t length = strlen(editor.buffer.filename);
		if (length >= sizeof(save_as.path)) {
			length = sizeof(save_as.path) - 1;
		}
		memcpy(save_as.path, editor.buffer.filename, length);
		save_as.path[length] = '\0';
		save_as.path_length = length;
		save_as.cursor_position = length;
	} else {
		/* No current filename - start with current directory */
		if (getcwd(save_as.path, sizeof(save_as.path)) != NULL) {
			size_t length = strlen(save_as.path);
			if (length + 1 < sizeof(save_as.path)) {
				save_as.path[length] = '/';
				save_as.path[length + 1] = '\0';
				save_as.path_length = length + 1;
				save_as.cursor_position = length + 1;
			}
		} else {
			save_as.path[0] = '\0';
			save_as.path_length = 0;
			save_as.cursor_position = 0;
		}
	}

	editor_set_status_message("");
}

/*
 * Exit Save As mode.
 */
static void save_as_exit(void)
{
	save_as.active = false;
	save_as.confirm_overwrite = false;
}

/*
 * Execute the Save As operation.
 * Returns true on success.
 */
static bool save_as_execute(void)
{
	if (save_as.path_length == 0) {
		editor_set_status_message("No filename provided");
		return false;
	}

	/* Check if file exists and we haven't confirmed overwrite */
	if (!save_as.confirm_overwrite && file_exists(save_as.path)) {
		save_as.confirm_overwrite = true;
		editor_set_status_message("File exists. Overwrite? (y/n)");
		return false;
	}

	/* Update the filename */
	char *old_filename = editor.buffer.filename;
	editor.buffer.filename = strdup(save_as.path);

	if (editor.buffer.filename == NULL) {
		editor.buffer.filename = old_filename;
		editor_set_status_message("Memory allocation failed");
		return false;
	}

	free(old_filename);

	/* Save the file - editor_save handles error display */
	int ret = file_save(&editor.buffer);
	if (ret) {
		editor_set_status_message("Save failed: %s", edit_strerror(ret));
		return false;
	}

	/* Exit save as mode */
	save_as_exit();

	return true;
}

/*
 * Handle key input in Save As mode.
 * Returns true if the key was handled.
 */
static bool save_as_handle_key(int key)
{
	if (!save_as.active) {
		return false;
	}

	/* Handle overwrite confirmation */
	if (save_as.confirm_overwrite) {
		switch (key) {
			case 'y':
			case 'Y':
				/* Keep confirm_overwrite true so execute() skips the check */
				save_as_execute();
				return true;

			case 'n':
			case 'N':
			case '\x1b':
				save_as.confirm_overwrite = false;
				editor_set_status_message("Save cancelled");
				return true;

			default:
				editor_set_status_message("File exists. Overwrite? (y/n)");
				return true;
		}
	}

	switch (key) {
		case '\x1b':
			save_as_exit();
			editor_set_status_message("Save As cancelled");
			return true;

		case '\r':
			save_as_execute();
			return true;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			if (save_as.cursor_position > 0) {
				/* Delete character before cursor (handle UTF-8) */
				uint32_t delete_position = save_as.cursor_position - 1;
				while (delete_position > 0 &&
				       (save_as.path[delete_position] & 0xC0) == 0x80) {
					delete_position--;
				}

				uint32_t delete_length = save_as.cursor_position - delete_position;
				memmove(save_as.path + delete_position,
				        save_as.path + save_as.cursor_position,
				        save_as.path_length - save_as.cursor_position + 1);

				save_as.path_length -= delete_length;
				save_as.cursor_position = delete_position;
			}
			return true;

		case KEY_DELETE:
			if (save_as.cursor_position < save_as.path_length) {
				/* Delete character at cursor (handle UTF-8) */
				uint32_t delete_end = save_as.cursor_position + 1;
				while (delete_end < save_as.path_length &&
				       (save_as.path[delete_end] & 0xC0) == 0x80) {
					delete_end++;
				}

				uint32_t delete_length = delete_end - save_as.cursor_position;
				memmove(save_as.path + save_as.cursor_position,
				        save_as.path + delete_end,
				        save_as.path_length - delete_end + 1);

				save_as.path_length -= delete_length;
			}
			return true;

		case KEY_ARROW_LEFT:
			if (save_as.cursor_position > 0) {
				save_as.cursor_position--;
				while (save_as.cursor_position > 0 &&
				       (save_as.path[save_as.cursor_position] & 0xC0) == 0x80) {
					save_as.cursor_position--;
				}
			}
			return true;

		case KEY_ARROW_RIGHT:
			if (save_as.cursor_position < save_as.path_length) {
				save_as.cursor_position++;
				while (save_as.cursor_position < save_as.path_length &&
				       (save_as.path[save_as.cursor_position] & 0xC0) == 0x80) {
					save_as.cursor_position++;
				}
			}
			return true;

		case KEY_HOME:
		case CONTROL_KEY('a'):
			save_as.cursor_position = 0;
			return true;

		case KEY_END:
		case CONTROL_KEY('e'):
			save_as.cursor_position = save_as.path_length;
			return true;

		case CONTROL_KEY('u'):
			/* Clear entire path */
			save_as.path[0] = '\0';
			save_as.path_length = 0;
			save_as.cursor_position = 0;
			return true;

		case CONTROL_KEY('w'):
			/* Delete word before cursor */
			if (save_as.cursor_position > 0) {
				uint32_t word_start = save_as.cursor_position;

				/* Skip trailing slashes/spaces */
				while (word_start > 0 &&
				       (save_as.path[word_start - 1] == '/' ||
				        save_as.path[word_start - 1] == ' ')) {
					word_start--;
				}

				/* Find start of word */
				while (word_start > 0 &&
				       save_as.path[word_start - 1] != '/' &&
				       save_as.path[word_start - 1] != ' ') {
					word_start--;
				}

				uint32_t delete_length = save_as.cursor_position - word_start;
				memmove(save_as.path + word_start,
				        save_as.path + save_as.cursor_position,
				        save_as.path_length - save_as.cursor_position + 1);

				save_as.path_length -= delete_length;
				save_as.cursor_position = word_start;
			}
			return true;

		case '\t':
			/* Tab completion for file paths */
			{
				char *last_slash = strrchr(save_as.path, '/');
				char directory_path[PATH_MAX];
				char prefix[256] = "";

				if (last_slash) {
					size_t directory_length = last_slash - save_as.path + 1;
					memcpy(directory_path, save_as.path, directory_length);
					directory_path[directory_length] = '\0';
					snprintf(prefix, sizeof(prefix), "%s", last_slash + 1);
				} else {
					strcpy(directory_path, ".");
					snprintf(prefix, sizeof(prefix), "%s", save_as.path);
				}

				DIR *directory = opendir(directory_path);
				if (directory) {
					struct dirent *entry;
					char *match = NULL;
					int match_count = 0;
					size_t prefix_length = strlen(prefix);

					while ((entry = readdir(directory)) != NULL) {
						if (entry->d_name[0] == '.' && prefix[0] != '.') {
							continue;
						}
						if (strncmp(entry->d_name, prefix, prefix_length) == 0) {
							match_count++;
							if (match == NULL) {
								match = strdup(entry->d_name);
							}
						}
					}
					closedir(directory);

					if (match_count == 1 && match) {
						snprintf(save_as.path, sizeof(save_as.path),
						         "%s%s", directory_path, match);
						save_as.path_length = strlen(save_as.path);

						/* Add trailing slash if directory */
						struct stat st;
						if (stat(save_as.path, &st) == 0 && S_ISDIR(st.st_mode)) {
							if (save_as.path_length + 1 < sizeof(save_as.path)) {
								save_as.path[save_as.path_length++] = '/';
								save_as.path[save_as.path_length] = '\0';
							}
						}

						save_as.cursor_position = save_as.path_length;
					} else if (match_count > 1) {
						editor_set_status_message("%d matches", match_count);
					}

					free(match);
				}
			}
			return true;

		default:
			/* Insert printable character */
			if (key >= 32 && key < 127) {
				if (save_as.path_length + 1 < sizeof(save_as.path)) {
					memmove(save_as.path + save_as.cursor_position + 1,
					        save_as.path + save_as.cursor_position,
					        save_as.path_length - save_as.cursor_position + 1);

					save_as.path[save_as.cursor_position] = (char)key;
					save_as.path_length++;
					save_as.cursor_position++;
				}
				return true;
			} else if (key >= 128) {
				/* UTF-8 multi-byte */
				char utf8[4];
				int bytes = utflite_encode((uint32_t)key, utf8);

				if (bytes > 0 && save_as.path_length + (uint32_t)bytes < sizeof(save_as.path)) {
					memmove(save_as.path + save_as.cursor_position + bytes,
					        save_as.path + save_as.cursor_position,
					        save_as.path_length - save_as.cursor_position + 1);

					memcpy(save_as.path + save_as.cursor_position, utf8, bytes);
					save_as.path_length += bytes;
					save_as.cursor_position += bytes;
				}
				return true;
			}
			break;
	}

	return false;
}

/*****************************************************************************
 * Line Operations - Delete and Duplicate
 *****************************************************************************/

/*
 * Select all text in the buffer.
 */
static void editor_select_all(void)
{
	if (editor.buffer.line_count == 0) {
		return;
	}

	/* Anchor at start */
	editor.selection_anchor_row = 0;
	editor.selection_anchor_column = 0;

	/* Cursor at end */
	editor.cursor_row = editor.buffer.line_count - 1;
	struct line *last_line = &editor.buffer.lines[editor.cursor_row];
	line_warm(last_line, &editor.buffer);
	editor.cursor_column = last_line->cell_count;

	editor.selection_active = true;
	editor_set_status_message("Selected all");
}

/*
 * Delete the current line. Records undo operation for the deleted content.
 */
static void editor_delete_line(void)
{
	if (editor.buffer.line_count == 0) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row >= editor.buffer.line_count) {
		row = editor.buffer.line_count - 1;
	}

	struct line *line = &editor.buffer.lines[row];
	line_warm(line, &editor.buffer);

	undo_begin_group(&editor.buffer);

	/* Build text to save for undo: line content + newline (if not last line) */
	size_t text_capacity = line->cell_count * 4 + 2;
	char *text = malloc(text_capacity);
	size_t text_len = 0;

	if (text != NULL) {
		for (uint32_t i = 0; i < line->cell_count; i++) {
			char utf8[4];
			int bytes = utflite_encode(line->cells[i].codepoint, utf8);
			if (bytes > 0) {
				memcpy(text + text_len, utf8, bytes);
				text_len += bytes;
			}
		}
		if (row < editor.buffer.line_count - 1) {
			text[text_len++] = '\n';
		}
		text[text_len] = '\0';

		/* Record the deletion - from start of this line to start of next line */
		uint32_t end_row = (row < editor.buffer.line_count - 1) ? row + 1 : row;
		uint32_t end_col = (row < editor.buffer.line_count - 1) ? 0 : line->cell_count;
		undo_record_delete_text(&editor.buffer, row, 0, end_row, end_col, text, text_len);
		free(text);
	}

	/* Delete the line */
	buffer_delete_line(&editor.buffer, row);

	/* Handle empty buffer */
	if (editor.buffer.line_count == 0) {
		buffer_ensure_capacity(&editor.buffer, 1);
		line_init(&editor.buffer.lines[0]);
		editor.buffer.line_count = 1;
	}

	/* Adjust cursor */
	if (editor.cursor_row >= editor.buffer.line_count) {
		editor.cursor_row = editor.buffer.line_count - 1;
	}
	editor.cursor_column = 0;

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);
	selection_clear();

	editor_set_status_message("Line deleted");
}

/*
 * Duplicate the current line. Inserts a copy below the current line.
 */
static void editor_duplicate_line(void)
{
	if (editor.buffer.line_count == 0) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row >= editor.buffer.line_count) {
		row = editor.buffer.line_count - 1;
	}

	struct line *source = &editor.buffer.lines[row];
	line_warm(source, &editor.buffer);

	undo_begin_group(&editor.buffer);

	/* Save cursor at end of line, insert newline (creates a new line) */
	uint32_t saved_col = editor.cursor_column;

	editor.cursor_row = row;
	editor.cursor_column = source->cell_count;

	/* Record and insert newline */
	undo_record_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);
	buffer_insert_newline(&editor.buffer, editor.cursor_row, editor.cursor_column);

	/* Move to the new line */
	editor.cursor_row = row + 1;
	editor.cursor_column = 0;

	/* Re-get source pointer (may have moved due to realloc) */
	source = &editor.buffer.lines[row];

	/* Copy each character from source into the new line */
	for (uint32_t i = 0; i < source->cell_count; i++) {
		uint32_t codepoint = source->cells[i].codepoint;
		undo_record_insert_char(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
		buffer_insert_cell_at_column(&editor.buffer, editor.cursor_row, editor.cursor_column, codepoint);
		editor.cursor_column++;
	}

	/* Restore cursor column on the duplicated line */
	struct line *dest = &editor.buffer.lines[row + 1];
	editor.cursor_column = saved_col;
	if (editor.cursor_column > dest->cell_count) {
		editor.cursor_column = dest->cell_count;
	}

	undo_end_group(&editor.buffer);

	editor_set_status_message("Line duplicated");
}

/*
 * Check if a line starts with a comment (// with optional leading whitespace).
 * Returns the column where the comment starts, or UINT32_MAX if not commented.
 */
static uint32_t line_comment_start(struct line *line)
{
	uint32_t column = 0;

	/* Skip leading whitespace */
	while (column < line->cell_count) {
		uint32_t codepoint = line->cells[column].codepoint;
		if (codepoint != ' ' && codepoint != '\t') {
			break;
		}
		column++;
	}

	/* Check for // */
	if (column + 1 < line->cell_count &&
	    line->cells[column].codepoint == '/' &&
	    line->cells[column + 1].codepoint == '/') {
		return column;
	}

	return UINT32_MAX;
}

/*
 * Check if a codepoint is a bracket character that can be matched.
 * Returns true for parentheses, square brackets, and curly braces.
 */
static bool is_matchable_bracket(uint32_t codepoint)
{
	return codepoint == '(' || codepoint == ')' ||
	       codepoint == '[' || codepoint == ']' ||
	       codepoint == '{' || codepoint == '}';
}

/*
 * Jump the cursor to the matching bracket at the current position.
 * If not on a bracket, scans forward on the current line to find one.
 * Uses the existing pair infrastructure for cross-line matching.
 */
static void editor_jump_to_match(void)
{
	if (editor.cursor_row >= editor.buffer.line_count) {
		editor_set_status_message("No bracket found");
		return;
	}

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	uint32_t search_column = editor.cursor_column;
	uint32_t match_row, match_column;
	bool found = false;

	/*
	 * First try at the cursor position. If not a bracket, scan forward
	 * on the current line to find one.
	 */
	if (search_column < line->cell_count &&
	    is_matchable_bracket(line->cells[search_column].codepoint)) {
		found = buffer_find_pair_partner(&editor.buffer,
		                                 editor.cursor_row, search_column,
		                                 &match_row, &match_column);
	}

	/* If not found at cursor, scan forward on the line */
	if (!found) {
		for (uint32_t column = search_column + 1; column < line->cell_count; column++) {
			if (is_matchable_bracket(line->cells[column].codepoint)) {
				found = buffer_find_pair_partner(&editor.buffer,
				                                 editor.cursor_row, column,
				                                 &match_row, &match_column);
				if (found) {
					break;
				}
			}
		}
	}

	if (found) {
		selection_clear();
		editor.cursor_row = match_row;
		editor.cursor_column = match_column;

		/* Ensure cursor is visible by scrolling if needed */
		if (editor.cursor_row < editor.row_offset) {
			editor.row_offset = editor.cursor_row;
		} else if (editor.cursor_row >= editor.row_offset + editor.screen_rows) {
			editor.row_offset = editor.cursor_row - editor.screen_rows + 1;
		}

		editor_set_status_message("Jumped to match");
	} else {
		editor_set_status_message("No matching bracket");
	}
}

/*
 * Toggle line comments on the current line or selection. Uses C-style //
 * comments. If all affected lines are commented, removes comments. Otherwise,
 * adds comments to all lines at the minimum indent level for alignment.
 */
static void editor_toggle_comment(void)
{
	uint32_t start_row, end_row;

	if (editor.selection_active && !selection_is_empty()) {
		uint32_t start_column, end_column;
		selection_get_range(&start_row, &start_column, &end_row, &end_column);
	} else {
		start_row = editor.cursor_row;
		end_row = editor.cursor_row;
	}

	if (start_row >= editor.buffer.line_count) {
		return;
	}
	if (end_row >= editor.buffer.line_count) {
		end_row = editor.buffer.line_count - 1;
	}

	/*
	 * Determine action: if ALL non-empty lines are commented, uncomment.
	 * Otherwise, comment all lines.
	 */
	bool all_commented = true;
	bool has_content = false;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		/* Skip empty lines for this check */
		uint32_t first_non_whitespace = 0;
		while (first_non_whitespace < line->cell_count) {
			uint32_t codepoint = line->cells[first_non_whitespace].codepoint;
			if (codepoint != ' ' && codepoint != '\t') {
				break;
			}
			first_non_whitespace++;
		}

		if (first_non_whitespace < line->cell_count) {
			has_content = true;
			if (line_comment_start(line) == UINT32_MAX) {
				all_commented = false;
				break;
			}
		}
	}

	/* If no content, nothing to do */
	if (!has_content) {
		return;
	}

	bool should_comment = !all_commented;

	undo_begin_group(&editor.buffer);

	/*
	 * Find the minimum indent across all lines with content. We insert
	 * comments at this position for visual alignment.
	 */
	uint32_t min_indent = UINT32_MAX;
	if (should_comment) {
		for (uint32_t row = start_row; row <= end_row; row++) {
			struct line *line = &editor.buffer.lines[row];

			/* Skip empty lines */
			if (line->cell_count == 0) {
				continue;
			}

			uint32_t indent = 0;
			while (indent < line->cell_count) {
				uint32_t codepoint = line->cells[indent].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				indent++;
			}

			/* Only count lines with content after whitespace */
			if (indent < line->cell_count && indent < min_indent) {
				min_indent = indent;
			}
		}
		if (min_indent == UINT32_MAX) {
			min_indent = 0;
		}
	}

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		if (should_comment) {
			/* Skip empty lines */
			if (line->cell_count == 0) {
				continue;
			}

			/* Check if line has content after whitespace */
			uint32_t first_content = 0;
			while (first_content < line->cell_count) {
				uint32_t codepoint = line->cells[first_content].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				first_content++;
			}
			if (first_content >= line->cell_count) {
				continue;
			}

			/* Insert "// " at min_indent position */
			uint32_t insert_position = min_indent;

			/* Make room and insert the three characters */
			line_ensure_capacity(line, line->cell_count + 3);

			/* Shift existing cells right by 3 */
			memmove(&line->cells[insert_position + 3],
				&line->cells[insert_position],
				(line->cell_count - insert_position) * sizeof(struct cell));

			/* Insert // and space */
			line->cells[insert_position].codepoint = '/';
			line->cells[insert_position].syntax = SYNTAX_COMMENT;
			line->cells[insert_position].context = 0;
			line->cells[insert_position].neighbor = 0;

			line->cells[insert_position + 1].codepoint = '/';
			line->cells[insert_position + 1].syntax = SYNTAX_COMMENT;
			line->cells[insert_position + 1].context = 0;
			line->cells[insert_position + 1].neighbor = 0;

			line->cells[insert_position + 2].codepoint = ' ';
			line->cells[insert_position + 2].syntax = SYNTAX_COMMENT;
			line->cells[insert_position + 2].context = 0;
			line->cells[insert_position + 2].neighbor = 0;

			line->cell_count += 3;
			line_set_temperature(line, LINE_TEMPERATURE_HOT);

			/* Record for undo (in reverse order for correct undo sequence) */
			undo_record_insert_char(&editor.buffer, row, insert_position, '/');
			undo_record_insert_char(&editor.buffer, row, insert_position + 1, '/');
			undo_record_insert_char(&editor.buffer, row, insert_position + 2, ' ');

			/* Adjust cursor if on this line */
			if (row == editor.cursor_row && editor.cursor_column >= insert_position) {
				editor.cursor_column += 3;
			}
		} else {
			/* Remove // (and optional trailing space) */
			uint32_t comment_start = line_comment_start(line);
			if (comment_start == UINT32_MAX) {
				continue;
			}

			/* Check if there's a space after // */
			uint32_t chars_to_remove = 2;
			if (comment_start + 2 < line->cell_count &&
			    line->cells[comment_start + 2].codepoint == ' ') {
				chars_to_remove = 3;
			}

			/* Record deletions for undo (from end to start) */
			for (uint32_t i = chars_to_remove; i > 0; i--) {
				uint32_t delete_position = comment_start + i - 1;
				uint32_t codepoint = line->cells[delete_position].codepoint;
				undo_record_delete_char(&editor.buffer, row, delete_position, codepoint);
			}

			/* Shift cells left to remove the comment */
			memmove(&line->cells[comment_start],
				&line->cells[comment_start + chars_to_remove],
				(line->cell_count - comment_start - chars_to_remove) * sizeof(struct cell));

			line->cell_count -= chars_to_remove;
			line_set_temperature(line, LINE_TEMPERATURE_HOT);

			/* Adjust cursor if on this line */
			if (row == editor.cursor_row && editor.cursor_column > comment_start) {
				if (editor.cursor_column >= comment_start + chars_to_remove) {
					editor.cursor_column -= chars_to_remove;
				} else {
					editor.cursor_column = comment_start;
				}
			}
		}

		/* Recompute line metadata */
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, row);
		line_invalidate_wrap_cache(line);
	}

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	uint32_t count = end_row - start_row + 1;
	editor_set_status_message("%s %u line%s",
				  should_comment ? "Commented" : "Uncommented",
				  count, count > 1 ? "s" : "");
}


/*
 * Move the current line up one position.
 */
static void editor_move_line_up(void)
{
	if (editor.buffer.line_count < 2) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row == 0) {
		return;
	}
	if (row >= editor.buffer.line_count) {
		row = editor.buffer.line_count - 1;
	}

	undo_begin_group(&editor.buffer);

	/* Swap with line above */
	buffer_swap_lines(&editor.buffer, row, row - 1);

	/* Invalidate wrap caches */
	line_invalidate_wrap_cache(&editor.buffer.lines[row]);
	line_invalidate_wrap_cache(&editor.buffer.lines[row - 1]);

	/* Move cursor up */
	editor.cursor_row--;

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	editor_set_status_message("Line moved up");
}

/*
 * Move the current line down one position.
 */
static void editor_move_line_down(void)
{
	if (editor.buffer.line_count < 2) {
		return;
	}

	uint32_t row = editor.cursor_row;
	if (row >= editor.buffer.line_count - 1) {
		return;
	}

	undo_begin_group(&editor.buffer);

	/* Swap with line below */
	buffer_swap_lines(&editor.buffer, row, row + 1);

	/* Invalidate wrap caches */
	line_invalidate_wrap_cache(&editor.buffer.lines[row]);
	line_invalidate_wrap_cache(&editor.buffer.lines[row + 1]);

	/* Move cursor down */
	editor.cursor_row++;

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	editor_set_status_message("Line moved down");
}

/*
 * Indent the current line or all lines in selection.
 * Inserts a tab character at the start of each line.
 */
static void editor_indent_lines(void)
{
	uint32_t start_row, end_row;

	if (editor.selection_active && !selection_is_empty()) {
		uint32_t start_col, end_col;
		selection_get_range(&start_row, &start_col, &end_row, &end_col);
	} else {
		start_row = editor.cursor_row;
		end_row = editor.cursor_row;
	}

	if (start_row >= editor.buffer.line_count) {
		return;
	}
	if (end_row >= editor.buffer.line_count) {
		end_row = editor.buffer.line_count - 1;
	}

	undo_begin_group(&editor.buffer);

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		/* Skip empty lines */
		if (line->cell_count == 0) {
			continue;
		}

		/* Record for undo before inserting */
		undo_record_insert_char(&editor.buffer, row, 0, '\t');

		/* Insert tab at position 0 */
		line_insert_cell(line, 0, '\t');
		line_set_temperature(line, LINE_TEMPERATURE_HOT);

		/* Recompute line metadata */
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, row);
		line_invalidate_wrap_cache(line);
	}

	/* Adjust cursor column to account for inserted tab */
	if (editor.cursor_row >= start_row && editor.cursor_row <= end_row) {
		editor.cursor_column++;
	}

	/* Adjust selection anchor if needed */
	if (editor.selection_active) {
		if (editor.selection_anchor_row >= start_row &&
		    editor.selection_anchor_row <= end_row) {
			editor.selection_anchor_column++;
		}
	}

	editor.buffer.is_modified = true;
	undo_end_group(&editor.buffer);

	uint32_t count = end_row - start_row + 1;
	editor_set_status_message("Indented %u line%s", count, count > 1 ? "s" : "");
}

/*
 * Outdent the current line or all lines in selection.
 * Removes leading tab or spaces from each line.
 */
static void editor_outdent_lines(void)
{
	uint32_t start_row, end_row;

	if (editor.selection_active && !selection_is_empty()) {
		uint32_t start_col, end_col;
		selection_get_range(&start_row, &start_col, &end_row, &end_col);
	} else {
		start_row = editor.cursor_row;
		end_row = editor.cursor_row;
	}

	if (start_row >= editor.buffer.line_count) {
		return;
	}
	if (end_row >= editor.buffer.line_count) {
		end_row = editor.buffer.line_count - 1;
	}

	undo_begin_group(&editor.buffer);

	uint32_t lines_modified = 0;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &editor.buffer.lines[row];
		line_warm(line, &editor.buffer);

		if (line->cell_count == 0) {
			continue;
		}

		uint32_t chars_to_remove = 0;

		/* Check what's at the start of the line */
		if (line->cells[0].codepoint == '\t') {
			chars_to_remove = 1;
		} else if (line->cells[0].codepoint == ' ') {
			/* Remove up to TAB_STOP_WIDTH spaces */
			while (chars_to_remove < (uint32_t)TAB_STOP_WIDTH &&
			       chars_to_remove < line->cell_count &&
			       line->cells[chars_to_remove].codepoint == ' ') {
				chars_to_remove++;
			}
		}

		if (chars_to_remove == 0) {
			continue;
		}

		/* Record and delete each character */
		for (uint32_t i = 0; i < chars_to_remove; i++) {
			undo_record_delete_char(&editor.buffer, row, 0, line->cells[0].codepoint);
			line_delete_cell(line, 0);
		}

		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(line);
		syntax_highlight_line(line, &editor.buffer, row);
		line_invalidate_wrap_cache(line);
		lines_modified++;

		/* Adjust cursor column */
		if (row == editor.cursor_row) {
			if (editor.cursor_column >= chars_to_remove) {
				editor.cursor_column -= chars_to_remove;
			} else {
				editor.cursor_column = 0;
			}
		}

		/* Adjust selection anchor if needed */
		if (editor.selection_active && row == editor.selection_anchor_row) {
			if (editor.selection_anchor_column >= chars_to_remove) {
				editor.selection_anchor_column -= chars_to_remove;
			} else {
				editor.selection_anchor_column = 0;
			}
		}
	}

	if (lines_modified > 0) {
		editor.buffer.is_modified = true;
	}

	undo_end_group(&editor.buffer);

	editor_set_status_message("Outdented %u line%s", lines_modified,
	                          lines_modified != 1 ? "s" : "");
}

static void editor_process_keypress(void)
{
	int key = input_read_key();

	if (key == -1) {
		return;
	}

	if (key == -2) {
		/* Terminal resize */
		editor_update_screen_size();
		return;
	}

	/* Handle Save As mode input first */
	if (save_as_handle_key(key)) {
		return;
	}

	/* Handle search mode input */
	if (search_handle_key(key)) {
		return;
	}

	/* Handle go-to-line mode input */
	if (goto_handle_key(key)) {
		return;
	}

	/* Handle quit prompt input */
	if (quit_prompt_handle_key(key)) {
		return;
	}

	switch (key) {
		case CONTROL_KEY('q'):
			if (editor.buffer.is_modified) {
				/* Unsaved changes - show prompt */
				quit_prompt_enter();
				return;
			}
			/* No unsaved changes - exit immediately */
			editor_perform_exit();
			break;

		case CONTROL_KEY('s'):
			editor_save();
			break;

		case KEY_ALT_SHIFT_S:
		case KEY_F12:
			save_as_enter();
			break;

		case KEY_CTRL_O:
			editor_command_open_file();
			break;

		case KEY_F5:
		case KEY_CTRL_T:
			editor_command_theme_picker();
			break;

		case CONTROL_KEY('c'):
			editor_copy();
			break;

		case CONTROL_KEY('x'):
			editor_cut();
			break;

		case CONTROL_KEY('v'):
			editor_paste();
			break;

		case CONTROL_KEY('z'):
			editor_undo();
			break;

		case CONTROL_KEY('y'):
			editor_redo();
			break;

		case KEY_F2:
			editor.show_line_numbers = !editor.show_line_numbers;
			editor_update_gutter_width();
			editor_set_status_message("Line numbers %s", editor.show_line_numbers ? "on" : "off");
			break;

		case KEY_F3:
			editor.show_whitespace = !editor.show_whitespace;
			editor_set_status_message("Whitespace %s", editor.show_whitespace ? "visible" : "hidden");
			break;

		case KEY_F4:
			/* Cycle color column: 0 -> 80 -> 120 -> 0 */
			if (editor.color_column == 0) {
				editor.color_column = 80;
			} else if (editor.color_column == 80) {
				editor.color_column = 120;
			} else {
				editor.color_column = 0;
			}
			if (editor.color_column > 0) {
				editor_set_status_message("Column %u (%s) - Shift+F4 to change style",
				                          editor.color_column,
				                          color_column_style_name(editor.color_column_style));
			} else {
				editor_set_status_message("Color column off");
			}
			break;

		case KEY_SHIFT_F4:
			editor_cycle_color_column_style();
			break;

		case KEY_ALT_Z:
			editor_cycle_wrap_mode();
			break;

		case KEY_ALT_SHIFT_Z:
			editor_cycle_wrap_indicator();
			break;

		case CONTROL_KEY('f'):
			search_enter();
			break;

		case CONTROL_KEY('r'):
			replace_enter();
			break;

		case CONTROL_KEY('g'):
			goto_enter();
			break;

		case CONTROL_KEY('a'):
			editor_select_all();
			break;

		case CONTROL_KEY('d'):
			editor_select_next_occurrence();
			break;

		case KEY_ALT_K:
			editor_delete_line();
			break;

		case KEY_ALT_D:
			editor_duplicate_line();
			break;

		case KEY_ALT_ARROW_UP:
			editor_move_line_up();
			break;

		case KEY_ALT_ARROW_DOWN:
			editor_move_line_down();
			break;

		case 0x1f:  /* Ctrl+/ in many terminals */
		case KEY_ALT_SLASH:
			editor_toggle_comment();
			break;

		case 0x1d:  /* Ctrl+] */
		case KEY_ALT_BRACKET:
			editor_jump_to_match();
			break;

		case KEY_ARROW_UP:
		case KEY_ARROW_DOWN:
		case KEY_ARROW_LEFT:
		case KEY_ARROW_RIGHT:
		case KEY_CTRL_ARROW_LEFT:
		case KEY_CTRL_ARROW_RIGHT:
		case KEY_HOME:
		case KEY_END:
		case KEY_PAGE_UP:
		case KEY_PAGE_DOWN:
		case KEY_SHIFT_ARROW_UP:
		case KEY_SHIFT_ARROW_DOWN:
		case KEY_SHIFT_ARROW_LEFT:
		case KEY_SHIFT_ARROW_RIGHT:
		case KEY_SHIFT_HOME:
		case KEY_SHIFT_END:
		case KEY_SHIFT_PAGE_UP:
		case KEY_SHIFT_PAGE_DOWN:
		case KEY_CTRL_SHIFT_ARROW_LEFT:
		case KEY_CTRL_SHIFT_ARROW_RIGHT:
			editor_move_cursor(key);
			break;

		case KEY_BACKSPACE:
		case CONTROL_KEY('h'):
			multicursor_backspace();
			break;

		case KEY_DELETE:
			editor_delete_character();
			break;

		case '\r':
			editor_insert_newline();
			break;

		case '\x1b':
			/* Escape exits multi-cursor mode first, then clears selection */
			if (editor.cursor_count > 0) {
				multicursor_exit();
			} else {
				selection_clear();
			}
			break;

		case CONTROL_KEY('l'):
			/* Ctrl-L: ignore (refresh) */
			break;

		case KEY_MOUSE_EVENT:
			/* Mouse events are handled in input_read_key via editor_handle_mouse */
			break;

		case '\t':
			/* Tab indents when selection active, otherwise inserts tab */
			if (editor.selection_active && !selection_is_empty()) {
				editor_indent_lines();
			} else {
				editor_insert_character('\t');
			}
			break;

		case KEY_SHIFT_TAB:
			editor_outdent_lines();
			break;

		default:
			/* Insert printable ASCII (32-126) and Unicode codepoints (>= 128) */
			if ((key >= 32 && key < 127) || key >= 128) {
				multicursor_insert_character((uint32_t)key);
			}
			break;
	}
}

/*
 * Program entry point. Initializes the terminal in raw mode, sets up
 * the window resize signal handler, and opens the file specified on
 * the command line (or starts with an empty buffer). Enters the main
 * loop which alternates between refreshing the screen and processing
 * keypresses.
 */
int main(int argument_count, char *argument_values[])
{
	int ret = terminal_enable_raw_mode();
	if (ret) {
		fprintf(stderr, "edit: %s\n", edit_strerror(ret));
		return 1;
	}

	terminal_enable_mouse();

	/* Set up signal handler for window resize */
	struct sigaction signal_action;
	signal_action.sa_handler = terminal_handle_resize;
	sigemptyset(&signal_action.sa_mask);
	signal_action.sa_flags = SA_RESTART;
	sigaction(SIGWINCH, &signal_action, NULL);

	/* Set up fatal signal handlers for crash recovery */
	signal(SIGSEGV, fatal_signal_handler);
	signal(SIGABRT, fatal_signal_handler);
	signal(SIGBUS, fatal_signal_handler);
	signal(SIGFPE, fatal_signal_handler);
	signal(SIGILL, fatal_signal_handler);

	editor_init();
	editor_update_screen_size();

	if (argument_count >= 2) {
		const char *filename = argument_values[1];

		/* Check for swap file (crash recovery) */
		const char *swap_path = autosave_check_recovery(filename);
		if (swap_path != NULL) {
			bool recover = autosave_prompt_recovery(filename, swap_path);
			if (recover) {
				/* Load from swap file */
				int ret = file_open(&editor.buffer, swap_path);
				if (ret == 0) {
					/* Set original filename */
					free(editor.buffer.filename);
					editor.buffer.filename = strdup(filename);
					editor.buffer.is_modified = true;
					autosave_update_path();
					autosave.swap_exists = true;
					editor_set_status_message("Recovered from swap - save to keep changes");
				} else {
					editor_set_status_message("Recovery failed: %s", edit_strerror(ret));
					/* Fall through to normal open */
					swap_path = NULL;
				}
			} else {
				swap_path = NULL;  /* User chose not to recover */
			}
		}

		if (swap_path == NULL) {
			/* Normal file open */
			int ret = file_open(&editor.buffer, filename);
			if (ret) {
				if (ret == -ENOENT) {
					/* File doesn't exist yet - that's OK, new file */
					editor.buffer.filename = strdup(filename);
					editor.buffer.is_modified = false;
				} else {
					/* Real error - show message but continue with empty buffer */
					editor_set_status_message("Cannot open file: %s",
					                          edit_strerror(ret));
				}
			}
			autosave_update_path();
		}
	} else {
		/* No file specified - also update swap path for unnamed */
		autosave_update_path();
	}

	editor_update_gutter_width();
	editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit | F2 = toggle line numbers");

	/* Track last autosave check time */
	static time_t last_autosave_check = 0;

	while (1) {
		int ret = render_refresh_screen();
		if (ret) {
			/* Minimal recovery on render failure */
			write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
			fprintf(stderr, "Render error: %s\n", edit_strerror(ret));
			usleep(100000);
		}
		editor_process_keypress();
		worker_process_results();

		/* Check auto-save periodically */
		time_t now = time(NULL);
		if (now - last_autosave_check >= 5) {
			autosave_check();
			last_autosave_check = now;
		}
	}

	return 0;
}
