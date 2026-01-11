/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * search.c - Search and replace for edit
 *
 * Provides async search and replace infrastructure
 * for background search on large files.
 */

#include "../third_party/utflite-1.5.2/single_include/utflite.h"
#include "edit.h"

/*****************************************************************************
 * Global State
 *****************************************************************************/

/* Async search state */
static struct async_search_state async_search = {
	.current_match_index = -1
};

/* Async replace state */
static struct async_replace_state async_replace = {0};

/* Access to editor state (defined in edit.c) */
extern struct editor_state editor;
extern struct search_state search;

/* Forward declaration for status messages */
extern void editor_set_status_message(const char *format, ...);

/* Forward declaration for scrolling */
static void search_scroll_to(uint32_t row, uint32_t col);

/*****************************************************************************
 * Async Search Initialization
 *****************************************************************************/

int search_init(void)
{
	/* Initialize async search mutex */
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

	/* Initialize async replace mutex */
	if (pthread_mutex_init(&async_replace.results_mutex, NULL) != 0) {
		log_err("Failed to initialize replace results mutex");
		pthread_mutex_destroy(&async_search.results_mutex);
		async_search.mutex_initialized = false;
		return -EEDIT_MUTEX;
	}

	async_replace.mutex_initialized = true;
	async_replace.results.replacements = NULL;
	async_replace.results.count = 0;
	async_replace.results.capacity = 0;
	async_replace.active = false;

	return 0;
}

void search_cleanup(void)
{
	/* Cleanup async search */
	if (async_search.mutex_initialized) {
		pthread_mutex_lock(&async_search.results_mutex);
		free(async_search.results.matches);
		async_search.results.matches = NULL;
		async_search.results.match_count = 0;
		pthread_mutex_unlock(&async_search.results_mutex);

		pthread_mutex_destroy(&async_search.results_mutex);
		async_search.mutex_initialized = false;
	}

	/* Cleanup async replace */
	if (async_replace.mutex_initialized) {
		pthread_mutex_lock(&async_replace.results_mutex);

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
}

/*****************************************************************************
 * Async Search Results Management
 *****************************************************************************/

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

int search_results_add_match(uint32_t row, uint32_t start_col, uint32_t end_col)
{
	pthread_mutex_lock(&async_search.results_mutex);

	/* Check capacity */
	if (async_search.results.match_count >= MAX_SEARCH_MATCHES) {
		pthread_mutex_unlock(&async_search.results_mutex);
		return -ENOMEM;
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

void search_results_update_progress(uint32_t rows_searched, uint32_t total_rows)
{
	pthread_mutex_lock(&async_search.results_mutex);
	async_search.results.rows_searched = rows_searched;
	async_search.results.total_rows = total_rows;
	pthread_mutex_unlock(&async_search.results_mutex);
}

void search_results_mark_complete(void)
{
	pthread_mutex_lock(&async_search.results_mutex);
	async_search.results.complete = true;
	pthread_mutex_unlock(&async_search.results_mutex);
}

/*****************************************************************************
 * Async Search Operations
 *****************************************************************************/

bool search_should_use_async(void)
{
	return worker_is_initialized() &&
	       async_search.mutex_initialized &&
	       editor.buffer.line_count > ASYNC_SEARCH_THRESHOLD;
}

int search_async_get_match_state(uint32_t row, uint32_t col)
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

uint32_t search_async_get_progress(bool *complete, uint32_t *rows_searched,
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

/*
 * Helper to scroll editor to a position.
 */
static void search_scroll_to(uint32_t row, uint32_t col)
{
	editor.cursor_row = row;
	editor.cursor_column = col;

	/* Center vertically if out of view */
	if (row < editor.row_offset ||
	    row >= editor.row_offset + editor.screen_rows) {
		if (row > editor.screen_rows / 2) {
			editor.row_offset = row - editor.screen_rows / 2;
		} else {
			editor.row_offset = 0;
		}
	}
}

bool search_async_next_match(void)
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

	search_scroll_to(row, col);
	return true;
}

bool search_async_prev_match(void)
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

	search_scroll_to(row, col);
	return true;
}

void search_async_cancel(void)
{
	if (async_search.active) {
		task_cancel(async_search.task_id);
		async_search.active = false;
		log_debug("Cancelled async search");
	}
}

void search_async_start(const char *pattern, bool use_regex,
                        bool case_sensitive, bool whole_word)
{
	if (!worker_is_initialized() || !async_search.mutex_initialized) {
		return;
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
			.end_row = 0,
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

/*
 * Clear search results and notify async search.
 */
void search_async_clear_results(void)
{
	if (!async_search.mutex_initialized) {
		return;
	}

	pthread_mutex_lock(&async_search.results_mutex);
	search_results_clear_locked();
	pthread_mutex_unlock(&async_search.results_mutex);
}

/*
 * Check if async search is active.
 */
bool search_async_is_active(void)
{
	return async_search.active;
}

/*
 * Get async search task ID for result matching.
 */
uint64_t search_async_get_task_id(void)
{
	return async_search.task_id;
}

/*
 * Mark async search as inactive (called when result received).
 */
void search_async_set_inactive(void)
{
	async_search.active = false;
}

/*
 * Get current match index.
 */
int32_t search_async_get_current_match_index(void)
{
	return async_search.current_match_index;
}

/*****************************************************************************
 * Async Replace Results Management
 *****************************************************************************/

/*
 * Clear replace results (call with mutex held).
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

int replace_results_add(uint32_t row, uint32_t start_col, uint32_t end_col,
                        const char *replacement, uint32_t replacement_len)
{
	/* Make a copy of the replacement text */
	char *replacement_copy = malloc(replacement_len + 1);
	if (replacement_copy == NULL) {
		return -ENOMEM;
	}
	memcpy(replacement_copy, replacement, replacement_len);
	replacement_copy[replacement_len] = '\0';

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
			free(replacement_copy);
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
	r->replacement_text = replacement_copy;
	r->replacement_len = replacement_len;

	pthread_mutex_unlock(&async_replace.results_mutex);
	return 0;
}

void replace_results_update_progress(uint32_t rows_searched, uint32_t total_rows)
{
	pthread_mutex_lock(&async_replace.results_mutex);
	async_replace.results.rows_searched = rows_searched;
	async_replace.results.total_rows = total_rows;
	pthread_mutex_unlock(&async_replace.results_mutex);
}

void replace_results_mark_complete(void)
{
	pthread_mutex_lock(&async_replace.results_mutex);
	async_replace.results.search_complete = true;
	pthread_mutex_unlock(&async_replace.results_mutex);
}

/*****************************************************************************
 * Async Replace Operations
 *****************************************************************************/

bool search_should_use_async_replace(void)
{
	return worker_is_initialized() &&
	       async_replace.mutex_initialized &&
	       editor.buffer.line_count > ASYNC_SEARCH_THRESHOLD;
}

uint32_t search_async_replace_get_progress(bool *search_complete,
                                           bool *apply_complete,
                                           uint32_t *total)
{
	if (!async_replace.mutex_initialized) {
		if (search_complete) *search_complete = true;
		if (apply_complete) *apply_complete = true;
		if (total) *total = 0;
		return 0;
	}

	pthread_mutex_lock(&async_replace.results_mutex);
	uint32_t count = async_replace.results.count;
	if (search_complete) *search_complete = async_replace.results.search_complete;
	if (apply_complete) *apply_complete = async_replace.results.apply_complete;
	if (total) *total = async_replace.results.total_rows;
	pthread_mutex_unlock(&async_replace.results_mutex);
	return count;
}

void search_async_replace_cancel(void)
{
	if (async_replace.active) {
		task_cancel(async_replace.task_id);
		async_replace.active = false;
		log_debug("Cancelled async replace");
	}
}

void search_async_replace_apply(void)
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
	undo_begin_group(&editor.buffer, editor.cursor_row, editor.cursor_column);

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
	undo_end_group(&editor.buffer, editor.cursor_row, editor.cursor_column);

	free(local_replacements);

	editor_set_status_message("Replaced %u occurrence%s", applied, applied == 1 ? "" : "s");

	/* Mark buffer as modified */
	if (applied > 0) {
		editor.buffer.is_modified = true;
	}

	async_replace.active = false;
}

void search_async_replace_start(const char *pattern, const char *replacement,
                                bool use_regex, bool case_sensitive,
                                bool whole_word)
{
	if (!worker_is_initialized() || !async_replace.mutex_initialized) {
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
	strncpy(async_replace.replacement, replacement, sizeof(async_replace.replacement) - 1);
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
	strncpy(task.replace.replacement, replacement, sizeof(task.replace.replacement) - 1);
	task.replace.replacement[sizeof(task.replace.replacement) - 1] = '\0';

	int err = task_queue_push(&task);
	if (err == 0) {
		async_replace.task_id = task.task_id;
		async_replace.active = true;
		editor_set_status_message("Replacing all...");
		log_debug("Started async replace for '%s' -> '%s' (task %lu)",
		          pattern, replacement, task.task_id);
	} else {
		log_warn("Failed to start async replace: %s", edit_strerror(err));
		editor_set_status_message("Failed to start replace: %s", edit_strerror(err));
	}
}

/*
 * Check if async replace is active.
 */
bool search_async_replace_is_active(void)
{
	return async_replace.active;
}

/*
 * Get async replace task ID for result matching.
 */
uint64_t search_async_replace_get_task_id(void)
{
	return async_replace.task_id;
}

/*
 * Mark async replace as inactive (called when result received).
 */
void search_async_replace_set_inactive(void)
{
	async_replace.active = false;
}
