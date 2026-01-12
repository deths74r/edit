/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

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
#include "../third_party/utflite-1.5.2/single_include/utflite.h"

#include "edit.h"
#include "syntax.h"
#include "keybindings.h"
#include "command.h"
#include <execinfo.h>

/*****************************************************************************
 * Global State
 *****************************************************************************/

/* The global editor state instance (non-static for module access). */
struct editor_state editor;

/* Mouse click tracking for double/triple-click detection. */
static time_t last_click_time = 0;
static uint32_t last_click_row = 0;
static uint32_t last_click_col = 0;
static int click_count = 0;

/* Scroll velocity tracking for adaptive scroll speed. */
static struct timespec last_scroll_time = {0, 0};
static double scroll_velocity = 0.0;
static int last_scroll_direction = 0;  /* -1 = up, 1 = down, 0 = none */
/* Trackpad detection - rolling average of event intervals */
static double avg_scroll_interval = 0.1;  /* Start assuming mouse wheel */
static bool trackpad_mode = false;
#define TRACKPAD_INTERVAL_THRESHOLD 0.05  /* 50ms = trackpad-like */
#define TRACKPAD_MAX_LINES 3              /* Conservative max for trackpads */
#define TRACKPAD_VELOCITY_DECAY 0.95      /* Heavier smoothing */
#define TRACKPAD_VELOCITY_TIMEOUT 0.6     /* Longer timeout for trackpads */

/* Incremental search state (non-static for module access). */
struct search_state search = {0};

/*****************************************************************************
 * Worker Task Processing
 *****************************************************************************/

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
int worker_process_warm_lines(struct task *task, struct task_result *result)
{
	uint32_t start_row = task->warm.start_row;
	uint32_t end_row = task->warm.end_row;

	/* Validate range */
	if (end_row > E_BUF->line_count) {
		end_row = E_BUF->line_count;
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

		struct line *line = &E_BUF->lines[row];

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
		int err = line_warm_from_worker(line, E_BUF);

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
int worker_process_search(struct task *task, struct task_result *result)
{
	const char *pattern = task->search.pattern;
	uint32_t start_row = task->search.start_row;
	uint32_t end_row = task->search.end_row;
	bool use_regex = task->search.use_regex;
	bool case_sensitive = task->search.case_sensitive;
	bool whole_word = task->search.whole_word;

	/* Validate range */
	if (end_row == 0 || end_row > E_BUF->line_count) {
		end_row = E_BUF->line_count;
	}
	if (start_row >= end_row) {
		result->search.match_count = 0;
		result->search.rows_searched = 0;
		result->search.complete = true;
		return 0;
	}

	/* Compile regex if needed */
	regex_t compiled_regex;
	bool regex_valid = false;

	if (use_regex) {
		/* Validate pattern length to mitigate ReDoS attacks */
		if (strlen(pattern) > MAX_REGEX_PATTERN_LENGTH) {
			log_warn("Regex pattern too long (max %d)", MAX_REGEX_PATTERN_LENGTH);
			result->search.match_count = 0;
			result->search.complete = true;
			return -EINVAL;
		}
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

		struct line *line = &E_BUF->lines[row];

		int matches = search_line_for_matches(
			line, E_BUF, row, pattern,
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
int worker_process_replace_all(struct task *task, struct task_result *result)
{
	const char *pattern = task->replace.pattern;
	const char *replacement_str = task->replace.replacement;
	bool use_regex = task->replace.use_regex;
	bool case_sensitive = task->replace.case_sensitive;
	bool whole_word = task->replace.whole_word;

	uint32_t total_rows = E_BUF->line_count;

	/* Compile regex if needed */
	regex_t compiled_regex;
	bool regex_valid = false;

	if (use_regex) {
		/* Validate pattern length to mitigate ReDoS attacks */
		if (strlen(pattern) > MAX_REGEX_PATTERN_LENGTH) {
			result->replace.replacements = 0;
			result->replace.complete = true;
			return -EINVAL;
		}
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

		struct line *line = &E_BUF->lines[row];

		/* Ensure line is warm */
		int temp = line_get_temperature(line);
		if (temp == LINE_TEMPERATURE_COLD) {
			if (line_try_claim_warming(line)) {
				if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
					int __attribute__((unused)) err = line_warm_from_worker(line, E_BUF);
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
					free(expanded);
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
						replace_results_add(row, start_col, end_col,
						                    replacement_str, strlen(replacement_str));
						total_replacements++;
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

	replace_results_mark_complete();

	result->replace.replacements = total_replacements;
	result->replace.complete = true;

	log_debug("Replace search complete: %u replacements found", total_replacements);

	return 0;
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

/*
 * Process any pending results from the worker thread.
 * Called periodically from the main loop (e.g., after input_read_key).
 */
void worker_process_results(void)
{
	struct task_result result;
	int processed = 0;

	while (result_queue_pop(&result)) {
		processed++;

		if (result.error && result.error != -EEDIT_CANCELLED) {
			log_warn("Task %" PRIu64 " (type %d) failed: %s",
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
			if (result.task_id == search_async_get_task_id()) {
				search_async_set_inactive();
			}

			if (result.error == -EEDIT_CANCELLED) {
				/* Don't update status on cancel - likely starting new search */
			} else if (result.error) {
				editor_set_status_message("Search error: %s",
				                          edit_strerror(result.error));
			} else {
				bool complete;
				uint32_t count = search_async_get_progress(&complete, NULL, NULL);
				if (complete) {
					if (count == 0) {
						editor_set_status_message("No matches found");
						search.has_match = false;
					} else {
						editor_set_status_message("Found %u match%s",
						                          count, count == 1 ? "" : "es");
						search.has_match = true;
						/* Auto-navigate to first match if not already at one */
						if (search_async_get_current_match_index() < 0) {
							search_async_next_match();
						}
					}
				}
			}
			break;

		case TASK_REPLACE_ALL:
			if (result.task_id == search_async_replace_get_task_id()) {
				if (result.error == -EEDIT_CANCELLED) {
					search_async_replace_set_inactive();
					editor_set_status_message("Replace cancelled");
				} else if (result.error) {
					search_async_replace_set_inactive();
					editor_set_status_message("Replace error: %s",
					                          edit_strerror(result.error));
				} else {
					/* Search phase complete - apply replacements */
					editor_set_status_message("Found %u matches, applying...",
					                          result.replace.replacements);
					search_async_replace_apply();
				}
			}
			break;

		case TASK_AUTOSAVE:
			autosave_handle_result(&result);
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
	if (!worker_is_initialized()) {
		return;
	}

	/* Calculate current viewport */
	uint32_t viewport_start = E_CTX->row_offset;
	uint32_t viewport_end = E_CTX->row_offset + editor.screen_rows;
	if (viewport_end > E_BUF->line_count) {
		viewport_end = E_BUF->line_count;
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
	if (warm_end > E_BUF->line_count) {
		warm_end = E_BUF->line_count;
	}

	/* Check if there are cold lines in range */
	bool has_cold_lines = false;
	for (uint32_t row = warm_start; row < warm_end && !has_cold_lines; row++) {
		if (line_get_temperature(&E_BUF->lines[row]) == LINE_TEMPERATURE_COLD) {
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
 * Signal Handlers
 *****************************************************************************/

/*
 * Fatal signal handler - attempts to save user data before dying.
 *
 * Called on SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL. Restores the
 * terminal to a sane state, prints diagnostic info and backtrace,
 * attempts emergency save, then re-raises the signal.
 */
void fatal_signal_handler(int sig)
{
	/* Restore terminal first so error output is visible */
	terminal_disable_raw_mode();

	/* Print signal information */
	const char *sig_name = "UNKNOWN";
	switch (sig) {
		case SIGSEGV: sig_name = "SIGSEGV (Segmentation fault)"; break;
		case SIGABRT: sig_name = "SIGABRT (Abort)"; break;
		case SIGBUS:  sig_name = "SIGBUS (Bus error)"; break;
		case SIGFPE:  sig_name = "SIGFPE (Floating point exception)"; break;
		case SIGILL:  sig_name = "SIGILL (Illegal instruction)"; break;
	}
	fprintf(stderr, "\nedit: fatal signal %s\n", sig_name);

	/* Log crash to debug.log */
	debug_log("CRASH: signal=%s", sig_name);
	debug_log("CRASH: context_count=%u, active_context=%u",
	          editor.context_count, editor.active_context);

	/* Print editor state for debugging */
	fprintf(stderr, "edit: state: context_count=%u, active_context=%u\n",
	        editor.context_count, editor.active_context);

	/* Log all context states to debug.log */
	for (uint32_t i = 0; i < editor.context_count && i < MAX_CONTEXTS; i++) {
		struct buffer *buf = &editor.contexts[i].buffer;
		debug_log("CRASH: context[%u]: filename=%s, lines=%u, lines_ptr=%p, modified=%d",
		          i,
		          buf->filename ? buf->filename : "(unnamed)",
		          buf->line_count,
		          (void *)buf->lines,
		          buf->is_modified);
	}

	if (editor.context_count > 0 && editor.active_context < editor.context_count) {
		struct buffer *buf = &editor.contexts[editor.active_context].buffer;
		fprintf(stderr, "edit: buffer: filename=%s, lines=%u, modified=%d\n",
		        buf->filename ? buf->filename : "(none)",
		        buf->line_count, buf->is_modified);
	}

	/* Print backtrace */
	void *bt_buffer[64];
	int bt_size = backtrace(bt_buffer, 64);
	fprintf(stderr, "edit: backtrace (%d frames):\n", bt_size);
	backtrace_symbols_fd(bt_buffer, bt_size, STDERR_FILENO);

	debug_log("CRASH: backtrace logged to stderr");

	/* Attempt to save user's work */
	emergency_save();

	/* Re-raise with default handler to get proper exit status */
	signal(sig, SIG_DFL);
	raise(sig);
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
 * - Validates state before accessing contexts (crash-safe)
 * - Saves ALL modified buffers, not just the active one
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

	/* Validate state before accessing - we may be in corrupted state */
	if (editor.context_count == 0 || editor.context_count > MAX_CONTEXTS) {
		fprintf(stderr, "edit: emergency save skipped (invalid state)\n");
		return;
	}

	pid_t pid = getpid();
	int saved_count = 0;

	/* Save ALL modified buffers */
	for (uint32_t ctx_idx = 0; ctx_idx < editor.context_count; ctx_idx++) {
		struct buffer *buffer = &editor.contexts[ctx_idx].buffer;

		/* Skip unmodified or empty buffers */
		if (buffer->line_count == 0 || !buffer->is_modified)
			continue;

		/* Build emergency filename */
		char emergency_path[PATH_MAX];

		if (buffer->filename) {
			snprintf(emergency_path, sizeof(emergency_path),
			         "%s.emergency.%d", buffer->filename, pid);
		} else {
			snprintf(emergency_path, sizeof(emergency_path),
			         "/tmp/edit.emergency.%u.%d", ctx_idx, pid);
		}

		/* Try to open file for writing */
		FILE *file = fopen(emergency_path, "w");
		if (!file && buffer->filename) {
			/* Fallback to /tmp if original location fails */
			snprintf(emergency_path, sizeof(emergency_path),
			         "/tmp/edit.emergency.%u.%d", ctx_idx, pid);
			file = fopen(emergency_path, "w");
		}

		if (!file)
			continue;

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
		saved_count++;
	}

	if (saved_count == 0) {
		fprintf(stderr, "edit: no modified buffers to save\n");
	}
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
			atomic_store(&line->warming_in_progress, false);
			line->wrap_columns = NULL;
			line->wrap_segment_count = 0;
			line->wrap_cache_width = 0;
			line->wrap_cache_mode = WRAP_NONE;
			line->md_elements = NULL;

			buffer->line_count++;
			line_start = i + 1;
		}
	}
}
/*
 * Read all available data from stdin into a dynamically allocated buffer.
 * Used when stdin is a pipe (not a terminal) to load piped content.
 *
 * Returns allocated buffer on success (caller must free).
 * Returns NULL on error or empty input.
 * Sets out_size to number of bytes read (or bytes attempted on error).
 */
char *stdin_read_all(size_t *out_size)
{
	size_t capacity = STDIN_INITIAL_CAPACITY;
	size_t length = 0;
	char *buffer = malloc(capacity);
	if (buffer == NULL) {
		*out_size = 0;
		return NULL;
	}
	while (1) {
		/* Grow buffer if needed */
		if (length + STDIN_READ_CHUNK_SIZE > capacity) {
			capacity *= 2;
			char *new_buffer = realloc(buffer, capacity);
			if (new_buffer == NULL) {
				free(buffer);
				*out_size = length;
				return NULL;
			}
			buffer = new_buffer;
		}
		ssize_t bytes_read = read(STDIN_FILENO, buffer + length,
		                          STDIN_READ_CHUNK_SIZE);
		if (bytes_read < 0) {
			if (errno == EINTR)
				continue;
			free(buffer);
			*out_size = length;
			return NULL;
		}
		if (bytes_read == 0)
			break;  /* EOF */
		length += bytes_read;
	}
	*out_size = length;
	return buffer;
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
int __must_check file_open(struct buffer *buffer, const char *filename)
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
	buffer->file_mtime = st.st_mtime;

	/* Compute pairs across entire buffer (warms all lines) */
	buffer_compute_pairs(buffer);

	/* Apply syntax highlighting with pair context */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		syntax_highlight_line(&buffer->lines[row], buffer, row);
	}

	return 0;
}

/*
 * Check if the file on disk has been modified externally since we loaded it.
 * Returns true if the file's mtime differs from the stored mtime.
 * Returns false for new files (no filename) or if the file cannot be stat'd.
 */
bool file_check_external_change(struct buffer *buffer)
{
	if (buffer->filename == NULL)
		return false;

	struct stat st;
	if (stat(buffer->filename, &st) < 0)
		return false;

	return st.st_mtime != buffer->file_mtime;
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
int __must_check file_save(struct buffer *buffer)
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

	/* Update stored mtime to prevent false external change detection */
	struct stat st;
	if (stat(buffer->filename, &st) == 0)
		buffer->file_mtime = st.st_mtime;

	editor_set_status_message("%zu bytes written to disk", total_bytes);

	/* Remove swap file after successful save */
	autosave_on_save();

	return 0;
}

/* selection_clear is now in editor.c but kept here for compatibility */

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
	if (E_CTX->cursor_count > 0) {
		return;  /* Already in multi-cursor mode */
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
static void multicursor_exit(void)
{
	if (E_CTX->cursor_count == 0) {
		return;
	}

	/* Copy primary cursor back to legacy fields */
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
static void multicursor_normalize(void)
{
	if (E_CTX->cursor_count <= 1) {
		return;
	}

	/* Sort by position */
	qsort(E_CTX->cursors, E_CTX->cursor_count,
	      sizeof(struct cursor), cursor_compare);

	/* Remove duplicates (cursors at same position) */
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

	/* Ensure primary cursor index is valid */
	if (E_CTX->primary_cursor >= E_CTX->cursor_count) {
		E_CTX->primary_cursor = E_CTX->cursor_count - 1;
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
	if (E_CTX->cursor_count == 0) {
		multicursor_enter();
	}

	if (E_CTX->cursor_count >= MAX_CURSORS) {
		editor_set_status_message("Maximum cursors reached (%d)", MAX_CURSORS);
		return false;
	}

	/* Check for duplicate position */
	for (uint32_t i = 0; i < E_CTX->cursor_count; i++) {
		if (E_CTX->cursors[i].anchor_row == anchor_row &&
		    E_CTX->cursors[i].anchor_column == anchor_column) {
			return false;  /* Already have cursor here */
		}
	}

	struct cursor *new_cursor = &E_CTX->cursors[E_CTX->cursor_count];
	new_cursor->row = row;
	new_cursor->column = column;
	new_cursor->anchor_row = anchor_row;
	new_cursor->anchor_column = anchor_column;
	new_cursor->has_selection = has_selection;

	E_CTX->cursor_count++;

	return true;
}

/*
 * Check if a position is within any cursor's selection.
 */
static bool multicursor_selection_contains(uint32_t row, uint32_t column)
{
	if (E_CTX->cursor_count == 0) {
		return false;
	}

	for (uint32_t i = 0; i < E_CTX->cursor_count; i++) {
		struct cursor *cursor = &E_CTX->cursors[i];
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

/*
 * Check if a position is a cursor position.
 * Returns cursor index (0+) or -1 if not a cursor.
 */
static int multicursor_cursor_at(uint32_t row, uint32_t column)
{
	for (uint32_t i = 0; i < E_CTX->cursor_count; i++) {
		if (E_CTX->cursors[i].row == row &&
		    E_CTX->cursors[i].column == column) {
			return (int)i;
		}
	}
	return -1;
}

/*****************************************************************************
 * Selection Range Functions
 *****************************************************************************/

/* Check if a cell position is within the selection. Returns false if no
 * selection is active or if the position is outside the selected range. */
static bool selection_contains(uint32_t row, uint32_t column)
{
	if (!E_CTX->selection_active) {
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

/*
 * Extract the currently selected text as a UTF-8 string.
 * Returns a newly allocated string that the caller must free,
 * or NULL if no selection. Sets *out_length to string length.
 */
char *selection_get_text(size_t *out_length)
{
	if (!E_CTX->selection_active || selection_is_empty()) {
		*out_length = 0;
		return NULL;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Estimate buffer size (4 bytes per codepoint max + newlines) */
	size_t capacity = 0;
	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);
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
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

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
 * Get the display width of a grapheme cluster.
 * Handles emoji presentation (VS16), keycap sequences, ZWJ sequences, etc.
 */
static int grapheme_display_width(struct line *line, uint32_t start, uint32_t end,
                                  uint32_t absolute_visual)
{
	if (start >= end || start >= line->cell_count) {
		return 0;
	}

	uint32_t first_cp = line->cells[start].codepoint;

	/* Handle tabs specially */
	if (first_cp == '\t') {
		return editor.tab_width - (absolute_visual % editor.tab_width);
	}

	/*
	 * Check for Variation Selector-16 (U+FE0F) which indicates emoji
	 * presentation. Emoji presentation is always width 2. This handles
	 * keycap sequences like 1️⃣, #️⃣, *️⃣ where base char is width 1
	 * but the emoji renders as width 2.
	 */
	for (uint32_t i = start; i < end && i < line->cell_count; i++) {
		if (line->cells[i].codepoint == 0xFE0F) {
			return 2;
		}
	}

	/* For grapheme clusters, use width of first non-zero-width codepoint */
	for (uint32_t i = start; i < end && i < line->cell_count; i++) {
		int w = utflite_codepoint_width(line->cells[i].codepoint);
		if (w > 0) {
			return w;
		}
	}

	/* Fallback: control chars or all zero-width */
	return 1;
}

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
	uint32_t i = 0;
	while (i < segment_start && i < line->cell_count) {
		uint32_t grapheme_end = cursor_next_grapheme(line, buffer, i);
		int width = grapheme_display_width(line, i, grapheme_end, absolute_visual);
		absolute_visual += width;
		i = grapheme_end;
	}

	/* Now calculate visual width within segment, iterating by grapheme */
	i = segment_start;
	while (i < cell_column && i < line->cell_count) {
		uint32_t grapheme_end = cursor_next_grapheme(line, buffer, i);
		/* Don't go past cell_column */
		if (grapheme_end > cell_column) {
			break;
		}
		int width = grapheme_display_width(line, i, grapheme_end, absolute_visual);
		visual_col += width;
		absolute_visual += width;
		i = grapheme_end;
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

	/*
	 * Calculate visual column at segment start (for tab alignment).
	 * Iterate by grapheme to handle multi-codepoint clusters.
	 */
	uint32_t absolute_visual = 0;
	uint32_t i = 0;
	while (i < segment_start && i < line->cell_count) {
		uint32_t grapheme_end = cursor_next_grapheme(line, buffer, i);
		int width = grapheme_display_width(line, i, grapheme_end, absolute_visual);
		absolute_visual += width;
		i = grapheme_end;
	}

	/*
	 * Find the column at target_visual within the segment.
	 * Iterate by grapheme to correctly position within ZWJ sequences etc.
	 */
	uint32_t visual_col = 0;
	uint32_t col = segment_start;

	while (col < segment_end && col < line->cell_count) {
		if (visual_col >= target_visual) {
			break;
		}
		uint32_t grapheme_end = cursor_next_grapheme(line, buffer, col);
		int width = grapheme_display_width(line, col, grapheme_end, absolute_visual);
		visual_col += width;
		absolute_visual += width;
		col = grapheme_end;
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
		uint32_t file_row = screen_row + E_CTX->row_offset;
		if (file_row >= E_BUF->line_count) {
			return false;
		}
		*out_line = file_row;
		*out_segment = 0;
		return true;
	}

	/* With wrap: iterate through lines and segments */
	uint32_t file_row = E_CTX->row_offset;
	uint16_t segment = 0;
	uint32_t screen_pos = 0;

	while (file_row < E_BUF->line_count && screen_pos <= screen_row) {
		struct line *line = &E_BUF->lines[file_row];
		line_ensure_wrap_cache(line, E_BUF);

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
		if (E_BUF->line_count > editor.screen_rows) {
			return E_BUF->line_count - editor.screen_rows;
		}
		return 0;
	}

	/*
	 * With wrap: work backwards from last line, summing screen rows
	 * until we exceed screen_rows. The line after that is max_offset.
	 */
	uint32_t screen_rows_from_end = 0;
	uint32_t candidate = E_BUF->line_count;

	while (candidate > 0) {
		candidate--;
		struct line *line = &E_BUF->lines[candidate];
		line_ensure_wrap_cache(line, E_BUF);
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
	if (row >= E_BUF->line_count) {
		return 0;
	}
	return line_get_cell_count(&E_BUF->lines[row], E_BUF);
}

/*
 * Adjust scroll offsets to keep the cursor visible on screen. Handles
 * both vertical scrolling (row_offset) and horizontal scrolling
 * (column_offset). In wrap mode, accounts for multi-segment lines.
 */
static void editor_scroll(void)
{
	/* Vertical scrolling (skip if selection active - allow cursor off-screen) */
	if (!E_CTX->selection_active) {
		if (E_CTX->cursor_row < E_CTX->row_offset) {
			E_CTX->row_offset = E_CTX->cursor_row;
		}

		if (editor.wrap_mode == WRAP_NONE) {
			/* No wrap: simple check */
			if (E_CTX->cursor_row >= E_CTX->row_offset + editor.screen_rows) {
				E_CTX->row_offset = E_CTX->cursor_row - editor.screen_rows + 1;
			}
		} else {
			/*
			 * Wrap enabled: calculate screen row of cursor and check
			 * if it's past the visible area.
			 */
			uint32_t screen_row = 0;
			for (uint32_t row = E_CTX->row_offset;
			     row <= E_CTX->cursor_row && row < E_BUF->line_count;
			     row++) {
				struct line *line = &E_BUF->lines[row];
				line_ensure_wrap_cache(line, E_BUF);
				if (row == E_CTX->cursor_row) {
					/* Add cursor's segment within this line */
					uint16_t cursor_segment = line_get_segment_for_column(
						line, E_BUF, E_CTX->cursor_column);
					screen_row += cursor_segment + 1;
				} else {
					screen_row += line->wrap_segment_count;
				}
			}

			/* If cursor is past visible area, scroll down */
			while (screen_row > editor.screen_rows &&
			       E_CTX->row_offset < E_BUF->line_count) {
				struct line *line = &E_BUF->lines[E_CTX->row_offset];
				line_ensure_wrap_cache(line, E_BUF);
				screen_row -= line->wrap_segment_count;
				E_CTX->row_offset++;
			}
		}
	}

	/* Horizontal scrolling - only applies in WRAP_NONE mode */
	if (editor.wrap_mode == WRAP_NONE) {
		uint32_t render_column = editor_get_render_column(
			E_CTX->cursor_row, E_CTX->cursor_column);
		uint32_t text_area_width = editor.screen_columns - E_CTX->gutter_width;
		if (render_column < E_CTX->column_offset) {
			E_CTX->column_offset = render_column;
		}
		if (render_column >= E_CTX->column_offset + text_area_width) {
			E_CTX->column_offset = render_column - text_area_width + 1;
		}
	} else {
		/* In wrap mode, no horizontal scrolling needed */
		E_CTX->column_offset = 0;
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
		if (!E_CTX->selection_active) {
			selection_start();
		}
	} else {
		selection_clear();
	}

	uint32_t line_length = editor_get_line_length(E_CTX->cursor_row);
	struct line *current_line = E_CTX->cursor_row < E_BUF->line_count
	                            ? &E_BUF->lines[E_CTX->cursor_row] : NULL;

	switch (base_key) {
		case KEY_ARROW_LEFT:
			if (E_CTX->cursor_column > 0 && current_line) {
				E_CTX->cursor_column = cursor_prev_grapheme(current_line, E_BUF, E_CTX->cursor_column);
			} else if (E_CTX->cursor_row > 0) {
				E_CTX->cursor_row--;
				E_CTX->cursor_column = editor_get_line_length(E_CTX->cursor_row);
			}
			break;

		case KEY_ARROW_RIGHT:
			if (E_CTX->cursor_column < line_length && current_line) {
				E_CTX->cursor_column = cursor_next_grapheme(current_line, E_BUF, E_CTX->cursor_column);
			} else if (E_CTX->cursor_row < E_BUF->line_count - 1) {
				E_CTX->cursor_row++;
				E_CTX->cursor_column = 0;
			}
			break;

		case KEY_ARROW_UP:
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: move by logical line */
				if (E_CTX->cursor_row > 0) {
					E_CTX->cursor_row--;
				}
			} else if (current_line) {
				/* Wrap enabled: move by screen row (segment) */
				uint16_t cur_segment = line_get_segment_for_column(
					current_line, E_BUF, E_CTX->cursor_column);
				uint32_t visual_col = line_get_visual_column_in_segment(
					current_line, E_BUF, cur_segment,
					E_CTX->cursor_column);

				if (cur_segment > 0) {
					/* Move to previous segment of same line */
					uint32_t new_col = line_find_column_at_visual(
						current_line, E_BUF,
						cur_segment - 1, visual_col);
					E_CTX->cursor_column = new_col;
				} else if (E_CTX->cursor_row > 0) {
					/* Move to last segment of previous line */
					E_CTX->cursor_row--;
					struct line *prev_line = &E_BUF->lines[E_CTX->cursor_row];
					line_ensure_wrap_cache(prev_line, E_BUF);
					uint16_t last_segment = prev_line->wrap_segment_count - 1;
					uint32_t new_col = line_find_column_at_visual(
						prev_line, E_BUF, last_segment, visual_col);
					E_CTX->cursor_column = new_col;
				}
			}
			break;

		case KEY_ARROW_DOWN:
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: move by logical line */
				if (E_CTX->cursor_row < E_BUF->line_count - 1) {
					E_CTX->cursor_row++;
				}
			} else if (current_line) {
				/* Wrap enabled: move by screen row (segment) */
				line_ensure_wrap_cache(current_line, E_BUF);
				uint16_t cur_segment = line_get_segment_for_column(
					current_line, E_BUF, E_CTX->cursor_column);
				uint32_t visual_col = line_get_visual_column_in_segment(
					current_line, E_BUF, cur_segment,
					E_CTX->cursor_column);

				if (cur_segment + 1 < current_line->wrap_segment_count) {
					/* Move to next segment of same line */
					uint32_t new_col = line_find_column_at_visual(
						current_line, E_BUF,
						cur_segment + 1, visual_col);
					E_CTX->cursor_column = new_col;
				} else if (E_CTX->cursor_row < E_BUF->line_count - 1) {
					/* Move to first segment of next line */
					E_CTX->cursor_row++;
					struct line *next_line = &E_BUF->lines[E_CTX->cursor_row];
					uint32_t new_col = line_find_column_at_visual(
						next_line, E_BUF, 0, visual_col);
					E_CTX->cursor_column = new_col;
				}
			} else if (E_BUF->line_count == 0) {
				/* Allow being on line 0 even with empty buffer */
			}
			break;

		case KEY_CTRL_ARROW_LEFT:
			if (current_line) {
				line_warm(current_line, E_BUF);
				uint32_t old_col = E_CTX->cursor_column;
				E_CTX->cursor_column = find_prev_word_start(current_line,
					E_CTX->cursor_column);
				/* If stuck at start of line, wrap to previous line */
				if (E_CTX->cursor_column == 0 && old_col == 0 &&
				    E_CTX->cursor_row > 0) {
					E_CTX->cursor_row--;
					struct line *prev = &E_BUF->lines[E_CTX->cursor_row];
					line_warm(prev, E_BUF);
					E_CTX->cursor_column = prev->cell_count;
				}
			}
			break;

		case KEY_CTRL_ARROW_RIGHT:
			if (current_line) {
				line_warm(current_line, E_BUF);
				uint32_t old_col = E_CTX->cursor_column;
				uint32_t len = current_line->cell_count;
				E_CTX->cursor_column = find_next_word_start(current_line,
					E_CTX->cursor_column);
				/* If stuck at end of line, wrap to next line */
				if (E_CTX->cursor_column == len && old_col == len &&
				    E_CTX->cursor_row < E_BUF->line_count - 1) {
					E_CTX->cursor_row++;
					E_CTX->cursor_column = 0;
				}
			}
			break;

		case KEY_HOME:
			if (editor.wrap_mode == WRAP_NONE || !current_line) {
				/* No wrap: go to start of logical line */
				E_CTX->cursor_column = 0;
			} else {
				/* Wrap enabled: go to start of current segment */
				uint16_t segment = line_get_segment_for_column(
					current_line, E_BUF, E_CTX->cursor_column);
				uint32_t segment_start = line_get_segment_start(
					current_line, E_BUF, segment);
				if (E_CTX->cursor_column == segment_start && segment > 0) {
					/* Already at segment start, go to previous segment start */
					segment_start = line_get_segment_start(
						current_line, E_BUF, segment - 1);
				}
				E_CTX->cursor_column = segment_start;
			}
			break;

		case KEY_END:
			if (editor.wrap_mode == WRAP_NONE || !current_line) {
				/* No wrap: go to end of logical line */
				E_CTX->cursor_column = line_length;
			} else {
				/* Wrap enabled: go to end of current segment */
				line_ensure_wrap_cache(current_line, E_BUF);
				uint16_t segment = line_get_segment_for_column(
					current_line, E_BUF, E_CTX->cursor_column);
				uint32_t segment_end = line_get_segment_end(
					current_line, E_BUF, segment);
				if (E_CTX->cursor_column == segment_end &&
				    segment + 1 < current_line->wrap_segment_count) {
					/* Already at segment end, go to next segment end */
					segment_end = line_get_segment_end(
						current_line, E_BUF, segment + 1);
				}
				E_CTX->cursor_column = segment_end;
			}
			break;

		case KEY_PAGE_UP:
			if (E_CTX->cursor_row > editor.screen_rows) {
				E_CTX->cursor_row -= editor.screen_rows;
			} else {
				E_CTX->cursor_row = 0;
			}
			break;

		case KEY_PAGE_DOWN:
			if (E_CTX->cursor_row + editor.screen_rows < E_BUF->line_count) {
				E_CTX->cursor_row += editor.screen_rows;
			} else if (E_BUF->line_count > 0) {
				E_CTX->cursor_row = E_BUF->line_count - 1;
			}
			break;
	}

	/* Snap cursor to end of line if it's past the line length */
	line_length = editor_get_line_length(E_CTX->cursor_row);
	if (E_CTX->cursor_column > line_length) {
		E_CTX->cursor_column = line_length;
	}
	/*
	 * Sync multi-cursor positions with primary cursor movement.
	 * Calculate the delta from the primary cursor's old position and
	 * apply it to all cursors in the array.
	 */
	if (E_CTX->cursor_count > 0) {
		struct cursor *primary = &E_CTX->cursors[E_CTX->primary_cursor];
		int32_t row_delta = (int32_t)E_CTX->cursor_row - (int32_t)primary->row;
		int32_t col_delta = (int32_t)E_CTX->cursor_column - (int32_t)primary->column;
		for (uint32_t i = 0; i < E_CTX->cursor_count; i++) {
			struct cursor *cursor = &E_CTX->cursors[i];
			/* Calculate new position with bounds checking */
			int32_t new_row = (int32_t)cursor->row + row_delta;
			int32_t new_col = (int32_t)cursor->column + col_delta;
			/* Clamp row to valid bounds */
			if (new_row < 0) {
				new_row = 0;
			}
			if (new_row >= (int32_t)E_BUF->line_count) {
				new_row = E_BUF->line_count - 1;
			}
			cursor->row = (uint32_t)new_row;
			/* Clamp column to line length */
			uint32_t cursor_line_len = editor_get_line_length(cursor->row);
			if (new_col < 0) {
				new_col = 0;
			}
			if ((uint32_t)new_col > cursor_line_len) {
				new_col = cursor_line_len;
			}
			cursor->column = (uint32_t)new_col;
		}
		multicursor_normalize();
	}
}

/*
 * Delete the currently selected text. Handles both single-line and
 * multi-line selections. Moves cursor to the start of the deleted region.
 */
void editor_delete_selection(void)
{
	if (!E_CTX->selection_active || selection_is_empty()) {
		return;
	}

	uint32_t start_row, start_col, end_row, end_col;
	selection_get_range(&start_row, &start_col, &end_row, &end_col);

	/* Save text before deleting for undo */
	size_t text_length;
	char *text = selection_get_text(&text_length);
	if (text != NULL) {
		undo_record_delete_text(E_BUF, start_row, start_col,
		                        end_row, end_col, text, text_length);
		free(text);
	}

	if (start_row == end_row) {
		/* Single line selection - delete cells from end to start */
		struct line *line = &E_BUF->lines[start_row];
		line_warm(line, E_BUF);

		for (uint32_t i = end_col; i > start_col; i--) {
			line_delete_cell(line, start_col);
		}
		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(line);
		syntax_highlight_line(line, E_BUF, start_row);
	} else {
		/* Multi-line selection */
		/* 1. Truncate start line at start_col */
		struct line *start_line = &E_BUF->lines[start_row];
		line_warm(start_line, E_BUF);
		start_line->cell_count = start_col;

		/* 2. Append content after end_col from end line */
		struct line *end_line = &E_BUF->lines[end_row];
		line_warm(end_line, E_BUF);

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
			buffer_delete_line(E_BUF, i);
		}

		line_set_temperature(start_line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(start_line);
		buffer_compute_pairs(E_BUF);

		/* Re-highlight affected lines */
		for (uint32_t row = start_row; row < E_BUF->line_count; row++) {
			if (E_BUF->lines[row].temperature != LINE_TEMPERATURE_COLD) {
				syntax_highlight_line(&E_BUF->lines[row], E_BUF, row);
			}
		}
	}

	/* Move cursor to start of deleted region */
	E_CTX->cursor_row = start_row;
	E_CTX->cursor_column = start_col;

	E_BUF->is_modified = true;
	selection_clear();
}


/*
 * Check if cursor is in a table and move to next cell.
 * Returns true if navigation happened.
 */
static bool editor_table_next_cell(void)
{
	if (!syntax_is_markdown_file(E_BUF->filename))
		return false;
	uint32_t start_row, end_row, separator_row;
	if (!table_detect_bounds(E_BUF, E_CTX->cursor_row,
				 &start_row, &end_row, &separator_row))
		return false;
	/* Clear selection before table navigation */
	selection_clear();
	/* Reformat table before navigating */
	table_reformat(E_BUF, E_CTX->cursor_row);
	struct line *line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);
	/* Find first pipe at or after cursor - that ends current cell */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		if (line->cells[i].codepoint == '|' && i >= E_CTX->cursor_column) {
			/* This pipe ends current cell, move to next cell */
			if (i + 1 < line->cell_count) {
				uint32_t next_cell_start = i + 1;
				/* Skip leading space */
				if (next_cell_start < line->cell_count &&
				    line->cells[next_cell_start].codepoint == ' ')
					next_cell_start++;
				E_CTX->cursor_column = next_cell_start;
				return true;
			}
			break;  /* No next cell, try next row */
		}
	}
	/* No next cell on this row - try next content row */
	uint32_t next_row = E_CTX->cursor_row + 1;
	if (next_row == separator_row)
		next_row++;  /* Skip separator */

	if (next_row <= end_row) {
		E_CTX->cursor_row = next_row;
		line = &E_BUF->lines[E_CTX->cursor_row];
		line_warm(line, E_BUF);
		/* Find first cell (after first |) */
		for (uint32_t i = 0; i < line->cell_count; i++) {
			if (line->cells[i].codepoint == '|') {
				E_CTX->cursor_column = i + 1;
				/* Skip leading space */
				if (E_CTX->cursor_column < line->cell_count &&
				    line->cells[E_CTX->cursor_column].codepoint == ' ')
					E_CTX->cursor_column++;
				return true;
			}
		}
	}

	/* At end of table - create a new row */
	/* Count columns from current line */
	line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);
	uint32_t col_count = 0;
	for (uint32_t i = 0; i < line->cell_count; i++) {
		if (line->cells[i].codepoint == '|')
			col_count++;
	}
	if (col_count > 1)
		col_count--;  /* N pipes = N-1 columns */

	if (col_count == 0)
		return true;

	/* Insert new line after current row */
	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Move to end of current line and insert newline */
	E_CTX->cursor_column = line->cell_count;
	undo_record_insert_newline(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
	int ret = buffer_insert_newline_checked(E_BUF, E_CTX->cursor_row,
	                                         E_CTX->cursor_column);
	if (ret) {
		undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		return true;
	}

	/* Move to new line */
	E_CTX->cursor_row++;
	E_CTX->cursor_column = 0;

	/* Build new row: | cell | cell | ... | */
	for (uint32_t col = 0; col < col_count; col++) {
		/* Insert | */
		undo_record_insert_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, '|');
		buffer_insert_cell_at_column_checked(E_BUF, E_CTX->cursor_row,
		                                      E_CTX->cursor_column, '|');
		E_CTX->cursor_column++;
		/* Insert space */
		undo_record_insert_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, ' ');
		buffer_insert_cell_at_column_checked(E_BUF, E_CTX->cursor_row,
		                                      E_CTX->cursor_column, ' ');
		E_CTX->cursor_column++;
	}
	/* Final | */
	undo_record_insert_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, '|');
	buffer_insert_cell_at_column_checked(E_BUF, E_CTX->cursor_row,
	                                      E_CTX->cursor_column, '|');

	/* Reformat to match column widths */
	table_reformat(E_BUF, E_CTX->cursor_row);

	/* Move cursor to first cell */
	line = &E_BUF->lines[E_CTX->cursor_row];
	E_CTX->cursor_column = 2;  /* After | and space */

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
	return true;
}
/*
 * Check if cursor is in a table and move to previous cell.
 * Returns true if navigation happened.
 */
static bool editor_table_prev_cell(void)
{
	if (!syntax_is_markdown_file(E_BUF->filename))
		return false;
	uint32_t start_row, end_row, separator_row;
	if (!table_detect_bounds(E_BUF, E_CTX->cursor_row,
				 &start_row, &end_row, &separator_row))
		return false;
	/* Clear selection before table navigation */
	selection_clear();
	/* Reformat table before navigating */
	table_reformat(E_BUF, E_CTX->cursor_row);
	struct line *line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);
	/* Find pipe before cursor */
	int32_t prev_pipe = -1;
	int32_t prev_prev_pipe = -1;
	for (uint32_t i = 0; i < line->cell_count && i < E_CTX->cursor_column; i++) {
		if (line->cells[i].codepoint == '|') {
			prev_prev_pipe = prev_pipe;
			prev_pipe = i;
		}
	}
	if (prev_prev_pipe >= 0) {
		/* Move to previous cell */
		E_CTX->cursor_column = prev_prev_pipe + 1;
		/* Skip leading space */
		if (E_CTX->cursor_column < line->cell_count &&
		    line->cells[E_CTX->cursor_column].codepoint == ' ')
			E_CTX->cursor_column++;
		return true;
	}
	/* At first cell - try previous content row */
	uint32_t prev_row = E_CTX->cursor_row - 1;
	if (prev_row == separator_row && prev_row > start_row)
		prev_row--;  /* Skip separator */
	if (prev_row >= start_row && prev_row != separator_row) {
		E_CTX->cursor_row = prev_row;
		line = &E_BUF->lines[E_CTX->cursor_row];
		line_warm(line, E_BUF);
		/* Find last cell (before last |) */
		int32_t last_pipe = -1;
		int32_t second_last_pipe = -1;
		for (uint32_t i = 0; i < line->cell_count; i++) {
			if (line->cells[i].codepoint == '|') {
				second_last_pipe = last_pipe;
				last_pipe = i;
			}
		}
		if (second_last_pipe >= 0) {
			E_CTX->cursor_column = second_last_pipe + 1;
			/* Skip leading space */
			if (E_CTX->cursor_column < line->cell_count &&
			    line->cells[E_CTX->cursor_column].codepoint == ' ')
				E_CTX->cursor_column++;
			return true;
		}
	}
	return true;  /* Was in table but at start */
}
/*
 * Insert a character at the current cursor position and advance the
 * cursor. If there's an active selection, deletes it first. Resets
 * the quit confirmation counter since the buffer was modified.
 */
static void editor_insert_character(uint32_t codepoint)
{
	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Delete selection first if active */
	if (E_CTX->selection_active && !selection_is_empty()) {
		editor_delete_selection();
	}

	undo_record_insert_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, codepoint);
	int ret = buffer_insert_cell_at_column_checked(E_BUF, E_CTX->cursor_row,
	                                                E_CTX->cursor_column, codepoint);
	if (ret) {
		editor_set_status_message("Insert failed: %s", edit_strerror(ret));
		undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		return;
	}
	E_CTX->cursor_column++;

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
}

/*
 * Handle Enter key by splitting the current line at the cursor position.
 * Moves cursor to the beginning of the newly created line below. If there's
 * an active selection, deletes it first. The new line inherits the leading
 * whitespace (tabs and spaces) from the original line for auto-indentation.
 */
static void editor_insert_newline(void)
{
	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/*
	 * Capture leading whitespace before any modifications. We limit the
	 * indent to the cursor column since content before cursor stays on
	 * the current line.
	 */
	uint32_t indent_count = 0;
	uint32_t indent_chars[256];

	if (E_CTX->cursor_row < E_BUF->line_count) {
		struct line *line = &E_BUF->lines[E_CTX->cursor_row];
		line_warm(line, E_BUF);

		while (indent_count < line->cell_count && indent_count < 256) {
			uint32_t codepoint = line->cells[indent_count].codepoint;
			if (codepoint != ' ' && codepoint != '\t') {
				break;
			}
			indent_chars[indent_count] = codepoint;
			indent_count++;
		}

		/* Don't copy more indent than cursor position */
		if (indent_count > E_CTX->cursor_column) {
			indent_count = E_CTX->cursor_column;
		}
	}

	/* Delete selection first if active */
	if (E_CTX->selection_active && !selection_is_empty()) {
		editor_delete_selection();
		/* After deletion, re-evaluate indent based on new cursor position */
		indent_count = 0;
		if (E_CTX->cursor_row < E_BUF->line_count) {
			struct line *line = &E_BUF->lines[E_CTX->cursor_row];
			line_warm(line, E_BUF);

			while (indent_count < line->cell_count && indent_count < 256) {
				uint32_t codepoint = line->cells[indent_count].codepoint;
				if (codepoint != ' ' && codepoint != '\t') {
					break;
				}
				indent_chars[indent_count] = codepoint;
				indent_count++;
			}

			if (indent_count > E_CTX->cursor_column) {
				indent_count = E_CTX->cursor_column;
			}
		}
	}

	undo_record_insert_newline(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
	int ret = buffer_insert_newline_checked(E_BUF, E_CTX->cursor_row,
	                                         E_CTX->cursor_column);
	if (ret) {
		editor_set_status_message("Cannot insert line: %s", edit_strerror(ret));
		undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		return;
	}
	E_CTX->cursor_row++;
	E_CTX->cursor_column = 0;

	/* Check if previous line was a table separator - if so, complete the table */
	if (syntax_is_markdown_file(E_BUF->filename) && E_CTX->cursor_row > 0) {
		struct line *prev_line = &E_BUF->lines[E_CTX->cursor_row - 1];
		line_warm(prev_line, E_BUF);

		if (md_is_table_separator(prev_line)) {
			uint32_t start_row, end_row, separator_row;
			if (table_detect_bounds(E_BUF, E_CTX->cursor_row - 1,
						&start_row, &end_row, &separator_row)) {
				/* Valid table detected - reformat it */
				table_reformat(E_BUF, E_CTX->cursor_row - 1);

				/* Count columns from separator row */
				prev_line = &E_BUF->lines[separator_row];
				line_warm(prev_line, E_BUF);
				uint32_t col_count = 0;
				for (uint32_t i = 0; i < prev_line->cell_count; i++) {
					if (prev_line->cells[i].codepoint == '|')
						col_count++;
				}
				if (col_count > 1)
					col_count--;  /* N pipes = N-1 columns */

				if (col_count > 0) {
					/* Build new content row on current line */
					/* Insert | at start */
					undo_record_insert_char(E_BUF, E_CTX->cursor_row, 0, '|');
					buffer_insert_cell_at_column_checked(E_BUF,
									     E_CTX->cursor_row, 0, '|');

					/* For each column, insert space + | */
					uint32_t pos = 1;
					for (uint32_t c = 0; c < col_count; c++) {
						undo_record_insert_char(E_BUF,
									E_CTX->cursor_row, pos, ' ');
						buffer_insert_cell_at_column_checked(E_BUF,
										     E_CTX->cursor_row,
										     pos++, ' ');
						undo_record_insert_char(E_BUF,
									E_CTX->cursor_row, pos, '|');
						buffer_insert_cell_at_column_checked(E_BUF,
										     E_CTX->cursor_row,
										     pos++, '|');
					}

					/* Reformat to get proper column widths */
					table_reformat(E_BUF, E_CTX->cursor_row);

					/* Position cursor in first cell (after | and space) */
					E_CTX->cursor_column = 2;

					undo_end_group(E_BUF, E_CTX->cursor_row,
						       E_CTX->cursor_column);
					return;  /* Skip auto-indent */
				}
			}
		}
	}

	/* Insert auto-indent characters on the new line */
	for (uint32_t i = 0; i < indent_count; i++) {
		undo_record_insert_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, indent_chars[i]);
		ret = buffer_insert_cell_at_column_checked(E_BUF, E_CTX->cursor_row,
		                                            E_CTX->cursor_column, indent_chars[i]);
		if (ret) {
			editor_set_status_message("Auto-indent failed: %s", edit_strerror(ret));
			undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
			return;
		}
		E_CTX->cursor_column++;
	}
	/*
	 * Markdown list continuation: auto-insert list markers on Enter.
	 * Detects ordered (1. 2.) and unordered (- * +) lists, including task lists.
	 * If the previous line was an empty list item (just marker, no content),
	 * we remove the marker instead of adding a new one.
	 */
	if (syntax_is_markdown_file(E_BUF->filename) && E_CTX->cursor_row > 0) {
		struct line *prev_line = &E_BUF->lines[E_CTX->cursor_row - 1];
		line_warm(prev_line, E_BUF);
		/* Scan for list marker in previous line */
		uint32_t marker_start = UINT32_MAX;
		uint32_t marker_end = 0;
		bool found_marker = false;
		/* Find where the marker ends and content begins */
		for (uint32_t i = 0; i < prev_line->cell_count; i++) {
			if (prev_line->cells[i].syntax == SYNTAX_MD_LIST_MARKER) {
				if (!found_marker) {
					marker_start = i;
				}
				marker_end = i + 1;
				found_marker = true;
			} else if (found_marker) {
				break;
			}
		}
		if (found_marker && marker_start != UINT32_MAX) {
			/* Check if there's any content after the marker (excluding trailing whitespace) */
			bool has_content = false;
			for (uint32_t i = marker_end; i < prev_line->cell_count; i++) {
				uint32_t cp = prev_line->cells[i].codepoint;
				if (cp != ' ' && cp != '\t') {
					has_content = true;
					break;
				}
			}
			if (!has_content) {
				/*
				 * Empty list item - remove the marker from previous line.
				 * Delete from marker_start to end of line.
				 */
				uint32_t delete_count = prev_line->cell_count - marker_start;
				for (uint32_t i = 0; i < delete_count; i++) {
					undo_record_delete_char(E_BUF,
						E_CTX->cursor_row - 1, marker_start,
						prev_line->cells[marker_start].codepoint);
					line_delete_cell(prev_line, marker_start);
				}
			} else {
				/*
				 * Has content - insert a new list marker on the new line.
				 * Copy unordered markers directly, increment ordered numbers.
				 */
				uint32_t list_marker[32];
				uint32_t list_marker_count = 0;
				uint32_t first_cp = prev_line->cells[marker_start].codepoint;
				if (first_cp == '-' || first_cp == '*' || first_cp == '+') {
					/* Unordered list: copy marker and space */
					list_marker[list_marker_count++] = first_cp;
					list_marker[list_marker_count++] = ' ';
					/* Check for task marker: - [ ] or - [x] */
					if (marker_end - marker_start >= 5) {
						/* Task list marker includes [ ] */
						bool is_task = false;
						for (uint32_t i = marker_start; i < marker_end; i++) {
							if (prev_line->cells[i].codepoint == '[') {
								is_task = true;
								break;
							}
						}
						if (is_task) {
							/* Add unchecked task marker */
							list_marker[list_marker_count++] = '[';
							list_marker[list_marker_count++] = ' ';
							list_marker[list_marker_count++] = ']';
							list_marker[list_marker_count++] = ' ';
						}
					}
				} else if (first_cp >= '0' && first_cp <= '9') {
					/* Ordered list: parse number, increment, format */
					uint32_t num = 0;
					char delimiter = '.';
					/* Parse the number */
					for (uint32_t i = marker_start; i < marker_end; i++) {
						uint32_t cp = prev_line->cells[i].codepoint;
						if (cp >= '0' && cp <= '9') {
							num = num * 10 + (cp - '0');
						} else if (cp == '.' || cp == ')') {
							delimiter = (char)cp;
							break;
						}
					}
					/* Increment and format */
					num++;
					char num_str[16];
					int num_len = snprintf(num_str, sizeof(num_str), "%u%c ",
							       num, delimiter);
					for (int i = 0; i < num_len && list_marker_count < 32; i++) {
						list_marker[list_marker_count++] = (uint32_t)num_str[i];
					}
				}
				/* Insert the list marker on the new line */
				for (uint32_t i = 0; i < list_marker_count; i++) {
					undo_record_insert_char(E_BUF,
						E_CTX->cursor_row, E_CTX->cursor_column,
						list_marker[i]);
					buffer_insert_cell_at_column_checked(E_BUF,
						E_CTX->cursor_row, E_CTX->cursor_column,
						list_marker[i]);
					E_CTX->cursor_column++;
				}
			}
		}
	}


	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
}

/*
 * Handle Delete key by removing the grapheme cluster at the cursor
 * position. If there's an active selection, deletes the selection instead.
 * Does nothing if cursor is past the end of the buffer.
 */
static void editor_delete_character(void)
{
	/* Delete selection if active */
	if (E_CTX->selection_active && !selection_is_empty()) {
		undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		editor_delete_selection();
		undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		return;
	}

	if (E_CTX->cursor_row >= E_BUF->line_count) {
		return;
	}

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	struct line *line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);

	if (E_CTX->cursor_column < line->cell_count) {
		/* Deleting a character - record it before deletion */
		uint32_t codepoint = line->cells[E_CTX->cursor_column].codepoint;
		undo_record_delete_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, codepoint);
	} else if (E_CTX->cursor_row + 1 < E_BUF->line_count) {
		/* Joining with next line - record newline deletion */
		undo_record_delete_newline(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
	}

	int ret = buffer_delete_grapheme_at_column_checked(E_BUF, E_CTX->cursor_row,
	                                                     E_CTX->cursor_column);
	if (ret) {
		editor_set_status_message("Delete failed: %s", edit_strerror(ret));
		undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		return;
	}

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
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
	if (E_CTX->selection_active && !selection_is_empty()) {
		undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		editor_delete_selection();
		undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
		return;
	}

	if (E_CTX->cursor_row == 0 && E_CTX->cursor_column == 0) {
		return;
	}

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	int ret;
	if (E_CTX->cursor_column > 0) {
		struct line *line = &E_BUF->lines[E_CTX->cursor_row];
		line_warm(line, E_BUF);
		uint32_t new_column = cursor_prev_grapheme(line, E_BUF, E_CTX->cursor_column);
		/* Record the character we're about to delete */
		uint32_t codepoint = line->cells[new_column].codepoint;
		undo_record_delete_char(E_BUF, E_CTX->cursor_row, new_column, codepoint);
		E_CTX->cursor_column = new_column;
		ret = buffer_delete_grapheme_at_column_checked(E_BUF, E_CTX->cursor_row,
		                                                E_CTX->cursor_column);
		if (ret) {
			editor_set_status_message("Delete failed: %s", edit_strerror(ret));
			undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
			return;
		}
	} else {
		/* Join with previous line - record newline deletion at end of previous line */
		uint32_t previous_line_length = editor_get_line_length(E_CTX->cursor_row - 1);
		undo_record_delete_newline(E_BUF, E_CTX->cursor_row - 1, previous_line_length);
		struct line *previous_line = &E_BUF->lines[E_CTX->cursor_row - 1];
		struct line *current_line = &E_BUF->lines[E_CTX->cursor_row];
		line_warm(previous_line, E_BUF);
		line_warm(current_line, E_BUF);
		ret = line_append_cells_from_line_checked(previous_line, current_line);
		if (ret) {
			editor_set_status_message("Join lines failed: %s", edit_strerror(ret));
			undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
			return;
		}
		line_set_temperature(previous_line, LINE_TEMPERATURE_HOT);
		buffer_delete_line(E_BUF, E_CTX->cursor_row);
		E_CTX->cursor_row--;
		E_CTX->cursor_column = previous_line_length;
	}

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

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
	if (E_CTX->cursor_count == 0) {
		editor_insert_character(codepoint);
		return;
	}

	/* Sort cursors so reverse iteration processes bottom-to-top */
	multicursor_normalize();

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Process in reverse order (bottom to top) so positions stay valid */
	for (int i = (int)E_CTX->cursor_count - 1; i >= 0; i--) {
		struct cursor *cursor = &E_CTX->cursors[i];

		/* TODO: Delete selection at this cursor if any */

		/* Insert character */
		undo_record_insert_char(E_BUF, cursor->row,
		                        cursor->column, codepoint);
		buffer_insert_cell_at_column(E_BUF, cursor->row,
		                             cursor->column, codepoint);

		/* Update cursor position */
		cursor->column++;
		cursor->anchor_row = cursor->row;
		cursor->anchor_column = cursor->column;
		cursor->has_selection = false;

		/*
		 * Adjust later cursors on the same line.
		 * We process in reverse (high to low index). After ascending sort,
		 * j > i means higher column positions already processed.
		 * They need to shift right because we just inserted before them.
		 */
		for (int j = i + 1; j < (int)E_CTX->cursor_count; j++) {
			if (E_CTX->cursors[j].row == cursor->row) {
				E_CTX->cursors[j].column++;
				if (E_CTX->cursors[j].anchor_row == cursor->row) {
					E_CTX->cursors[j].anchor_column++;
				}
			}
		}
	}

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Update primary cursor in legacy fields for rendering */
	struct cursor *primary = &E_CTX->cursors[E_CTX->primary_cursor];
	E_CTX->cursor_row = primary->row;
	E_CTX->cursor_column = primary->column;
}

/*
 * Delete character before all cursor positions (backspace).
 * Processes cursors in reverse order to preserve positions.
 */
static void multicursor_backspace(void)
{
	if (E_CTX->cursor_count == 0) {
		editor_handle_backspace();
		return;
	}

	/* Sort cursors so reverse iteration processes bottom-to-top */
	multicursor_normalize();

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Process in reverse order */
	for (int i = (int)E_CTX->cursor_count - 1; i >= 0; i--) {
		struct cursor *cursor = &E_CTX->cursors[i];

		/* TODO: Delete selection at this cursor if any */

		if (cursor->column == 0 && cursor->row == 0) {
			continue;  /* Nothing to delete at buffer start */
		}

		if (cursor->column > 0) {
			/* Delete character before cursor on same line */
			struct line *line = &E_BUF->lines[cursor->row];
			line_warm(line, E_BUF);

			uint32_t delete_column = cursor->column - 1;
			uint32_t deleted_codepoint = line->cells[delete_column].codepoint;

			undo_record_delete_char(E_BUF, cursor->row,
			                        delete_column, deleted_codepoint);
			line_delete_cell(line, delete_column);

			cursor->column--;
			cursor->anchor_column = cursor->column;

			/* Update line metadata */
			neighbor_compute_line(line);
			syntax_highlight_line(line, E_BUF, cursor->row);
			line_invalidate_wrap_cache(line);

			/* Adjust later cursors on same line (already processed) */
			for (int j = i + 1; j < (int)E_CTX->cursor_count; j++) {
				if (E_CTX->cursors[j].row == cursor->row &&
				    E_CTX->cursors[j].column > delete_column) {
					E_CTX->cursors[j].column--;
				}
				if (E_CTX->cursors[j].anchor_row == cursor->row &&
				    E_CTX->cursors[j].anchor_column > delete_column) {
					E_CTX->cursors[j].anchor_column--;
				}
			}
		} else {
			/* At start of line - skip line joining in multi-cursor mode */
			/* This is a limitation we accept for Phase 20 */
		}
	}

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	multicursor_normalize();

	/* Update primary cursor in legacy fields */
	if (E_CTX->cursor_count > 0) {
		struct cursor *primary = &E_CTX->cursors[E_CTX->primary_cursor];
		E_CTX->cursor_row = primary->row;
		E_CTX->cursor_column = primary->column;
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
void editor_save(void)
{
	if (E_BUF->filename == NULL) {
		editor_set_status_message("No filename specified");
		return;
	}
	/* Cannot save to stdin - prompt user to use Save As */
	if (strcmp(E_BUF->filename, "<stdin>") == 0) {
		editor_set_status_message("Use Alt+Shift+S or F12 to Save As");
		return;
	}

	int ret = file_save(E_BUF);
	if (ret) {
		editor_set_status_message("Save failed: %s", edit_strerror(ret));
	} else {
		/* Success message is set by file_save */
	}
}

/*
 * Convert a visual screen column to a cell index within a line. Accounts
 * for tab expansion and character widths. Used for mouse click positioning.
 * Iterates by grapheme cluster to correctly handle multi-codepoint characters.
 */
static uint32_t screen_column_to_cell(uint32_t row, uint32_t target_visual_column)
{
	if (row >= E_BUF->line_count) {
		return 0;
	}

	struct line *line = &E_BUF->lines[row];
	line_warm(line, E_BUF);

	uint32_t visual_column = 0;
	uint32_t cell_index = 0;

	while (cell_index < line->cell_count && visual_column < target_visual_column) {
		uint32_t grapheme_end = cursor_next_grapheme(line, E_BUF, cell_index);
		int width = grapheme_display_width(line, cell_index, grapheme_end, visual_column);

		/* If clicking in the middle of a wide character, round to nearest */
		if (visual_column + width > target_visual_column) {
			if (target_visual_column - visual_column > (uint32_t)width / 2) {
				cell_index = grapheme_end;
			}
			break;
		}

		visual_column += width;
		cell_index = grapheme_end;
	}

	return cell_index;
}

/*
 * Select the word at the given position using neighbor layer data.
 * Does nothing if position is whitespace.
 */
static bool editor_select_word(uint32_t row, uint32_t column)
{
	if (row >= E_BUF->line_count) {
		return false;
	}

	struct line *line = &E_BUF->lines[row];
	line_warm(line, E_BUF);

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
		E_CTX->cursor_column = column;
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
	E_CTX->selection_anchor_row = row;
	E_CTX->selection_anchor_column = word_start;
	E_CTX->cursor_row = row;
	E_CTX->cursor_column = word_end;
	E_CTX->selection_active = true;
	return true;
}

/*
 * Select an entire line including trailing newline conceptually.
 */
static void editor_select_line(uint32_t row)
{
	if (row >= E_BUF->line_count) {
		return;
	}

	struct line *line = &E_BUF->lines[row];
	line_warm(line, E_BUF);

	E_CTX->selection_anchor_row = row;
	E_CTX->selection_anchor_column = 0;
	E_CTX->cursor_row = row;
	E_CTX->cursor_column = line->cell_count;
	E_CTX->selection_active = true;
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
	if (E_CTX->cursor_row >= E_BUF->line_count) {
		return false;
	}

	struct line *line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);

	if (line->cell_count == 0) {
		return false;
	}

	uint32_t column = E_CTX->cursor_column;

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

	return editor_select_word(E_CTX->cursor_row, column);
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
		if (row >= E_BUF->line_count) {
			if (!wrap || wrapped) {
				return false;
			}
			/* Wrap to beginning */
			row = 0;
			column = 0;
			wrapped = true;
		}

		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

		/* Search this line from column onwards */
		while (column < line->cell_count) {
			if (search_matches_at(line, E_BUF, column, text, text_length)) {
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
	if (!E_CTX->selection_active || selection_is_empty()) {
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

	/* Set search query to selected text for matching */
	if (text_length < sizeof(search.query)) {
		memcpy(search.query, text, text_length);
		search.query[text_length] = '\0';
		search.query_length = text_length;
		search.case_sensitive = true;  /* Exact match for Ctrl+D */
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
	if (E_CTX->cursor_count > 0) {
		/* Multi-cursor mode: search after the last cursor */
		struct cursor *last = &E_CTX->cursors[E_CTX->cursor_count - 1];
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
		if (E_CTX->cursor_count > 0) {
			for (uint32_t i = 0; i < E_CTX->cursor_count; i++) {
				if (E_CTX->cursors[i].anchor_row == found_row &&
				    E_CTX->cursors[i].anchor_column == found_column) {
					already_exists = true;
					break;
				}
			}
		}

		if (is_original || already_exists) {
			uint32_t count = E_CTX->cursor_count > 0 ? E_CTX->cursor_count : 1;
			editor_set_status_message("%u cursor%s (all occurrences)",
			                          count, count > 1 ? "s" : "");
		} else {
			/* Calculate new cursor end position */
			uint32_t new_cursor_column = found_column + selection_cells;

			/* Clamp to line length */
			if (found_row < E_BUF->line_count) {
				struct line *line = &E_BUF->lines[found_row];
				line_warm(line, E_BUF);
				if (new_cursor_column > line->cell_count) {
					new_cursor_column = line->cell_count;
				}
			}

			/* Add new cursor at found position */
			if (multicursor_add(found_row, new_cursor_column,
			                    found_row, found_column, true)) {
				multicursor_normalize();

				editor_set_status_message("%u cursors",
				                          E_CTX->cursor_count);

				/* Scroll to show new cursor if needed */
				if (found_row < E_CTX->row_offset) {
					E_CTX->row_offset = found_row;
				} else if (found_row >= E_CTX->row_offset + editor.screen_rows) {
					E_CTX->row_offset = found_row - editor.screen_rows + 1;
				}
			}
		}
	} else {
		editor_set_status_message("No more occurrences");
	}

	free(text);
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

	/* Update rolling average of event intervals for trackpad detection */
	if (!first_scroll && dt > 0 && dt < 1.0) {
		avg_scroll_interval = 0.9 * avg_scroll_interval + 0.1 * dt;
		trackpad_mode = (avg_scroll_interval < TRACKPAD_INTERVAL_THRESHOLD);
	}

	/* Select parameters based on input device mode */
	double decay = trackpad_mode ? TRACKPAD_VELOCITY_DECAY : SCROLL_VELOCITY_DECAY;
	uint32_t max_lines = trackpad_mode ? TRACKPAD_MAX_LINES : SCROLL_MAX_LINES;
	double timeout = trackpad_mode ? TRACKPAD_VELOCITY_TIMEOUT : SCROLL_VELOCITY_TIMEOUT;

	/* Reset velocity on direction change, first scroll, or timeout */
	if (direction != last_scroll_direction ||
	    dt > timeout ||
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

	/* Exponential moving average for smoothing (heavier for trackpads) */
	scroll_velocity = decay * scroll_velocity + (1.0 - decay) * instant_velocity;

	/* Map velocity to scroll amount */
	if (scroll_velocity <= SCROLL_VELOCITY_SLOW) {
		return SCROLL_MIN_LINES;
	}

	if (scroll_velocity >= SCROLL_VELOCITY_FAST) {
		return max_lines;
	}

	/* Smoothstep interpolation between min and max (eases in and out) */
	double t = (scroll_velocity - SCROLL_VELOCITY_SLOW) /
	           (SCROLL_VELOCITY_FAST - SCROLL_VELOCITY_SLOW);
	t = t * t * (3.0 - 2.0 * t);  /* smoothstep: zero derivative at endpoints */

	return SCROLL_MIN_LINES + (uint32_t)(t * (max_lines - SCROLL_MIN_LINES));
}

/*
 * Toggle a task checkbox at the given row/column.
 * Changes [ ] to [x] or [x]/[X] to [ ].
 */
static void editor_toggle_task_checkbox(uint32_t row, uint32_t checkbox_col)
{
	if (row >= E_BUF->line_count)
		return;

	struct line *line = &E_BUF->lines[row];
	line_warm(line, E_BUF);

	/* checkbox_col points to '[', inner char is at checkbox_col + 1 */
	uint32_t inner_col = checkbox_col + 1;
	if (inner_col >= line->cell_count)
		return;

	uint32_t inner_cp = line->cells[inner_col].codepoint;

	/* Toggle: space -> 'x', x/X -> space */
	if (inner_cp == ' ') {
		line->cells[inner_col].codepoint = 'x';
	} else if (inner_cp == 'x' || inner_cp == 'X') {
		line->cells[inner_col].codepoint = ' ';
	} else {
		return;  /* Not a valid checkbox */
	}

	/* Update line state */
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	neighbor_compute_line(line);
	syntax_highlight_line(line, E_BUF, row);
	line_invalidate_wrap_cache(line);

	E_BUF->is_modified = true;
}

/*
 * Handle a mouse event by updating cursor position and selection state.
 * Handles click, drag, scroll, and multi-click for word/line selection.
 */
void editor_handle_mouse(struct mouse_input *mouse)
{
	/* Don't process mouse events after context close */
	if (editor.context_just_closed) {
		return;
	}
	switch (mouse->event) {
		case MOUSE_LEFT_PRESS: {
			/* Check for tab bar click (row 0 when tab bar visible) */
			if (editor.context_count > 1 && mouse->row == 0) {
				uint32_t x = 0;
				for (uint32_t i = 0; i < editor.context_count; i++) {
					struct buffer *buf = &editor.contexts[i].buffer;
					const char *name = buf->filename;
					const char *display_name;
					if (name) {
						const char *slash = strrchr(name, '/');
						display_name = slash ? slash + 1 : name;
					} else {
						display_name = "[No Name]";
					}
					/* Tab width: space + name + optional [+] + space */
					size_t name_len = strlen(display_name);
					if (name_len > 20) name_len = 20;  /* Truncated */
					uint32_t tab_width = 2 + (uint32_t)name_len;
					if (buf->is_modified) tab_width += 4;  /* " [+]" */
					if (mouse->column >= x && mouse->column < x + tab_width) {
						editor_context_switch(i);
						return;
					}
					x += tab_width;
				}
				return;  /* Click on tab bar but not on a tab */
			}
			/* Convert screen position to buffer position */
			uint32_t file_row;
			uint16_t segment;
			uint32_t cell_col;

			/* Account for gutter in screen column */
			uint32_t screen_col = mouse->column;
			if (screen_col < E_CTX->gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= E_CTX->gutter_width;
			}

			/* Map screen row to logical line and segment */
			if (screen_row_to_line_segment(mouse->row, &file_row, &segment)) {
				/*
				 * Convert visual position to cell column.
				 * In wrap mode, visual position is just screen_col.
				 * In no-wrap mode, add column_offset.
				 */
				struct line *line = &E_BUF->lines[file_row];
				if (editor.wrap_mode != WRAP_NONE) {
					cell_col = line_find_column_at_visual(
						line, E_BUF, segment, screen_col);
				} else {
					cell_col = screen_column_to_cell(
						file_row, screen_col + E_CTX->column_offset);
				}
			} else {
				/* Click below content: go to last line */
				if (E_BUF->line_count > 0) {
					file_row = E_BUF->line_count - 1;
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
				/* Check if clicking on a task checkbox */
				struct line *line = &E_BUF->lines[file_row];
				uint32_t checkbox_col;
				if (syntax_is_markdown_file(E_BUF->filename) &&
				    md_is_task_checkbox(line, cell_col, &checkbox_col)) {
					editor_toggle_task_checkbox(file_row, checkbox_col);
				} else {
					/* Double-click: select word using neighbor layer */
					E_CTX->cursor_row = file_row;
					editor_select_word(file_row, cell_col);
				}
			} else if (click_count >= 3) {
				/* Triple-click: select entire line */
				editor_select_line(file_row);
				click_count = 0;  /* Reset */
			} else {
				/* Single click: position cursor and start selection */
				E_CTX->cursor_row = file_row;
				E_CTX->cursor_column = cell_col;
				selection_start();
			}
			break;
		}

		case MOUSE_LEFT_DRAG: {
			/* Auto-scroll when dragging at screen edges */
			uint32_t content_top = (editor.context_count > 1) ? 1 : 0;  /* Skip tab bar */
			uint32_t content_bottom = content_top + editor.screen_rows - 1;  /* Last content row */

			/* Track drag state for timer-based auto-scroll */
			editor.drag_scroll.active = true;
			editor.drag_scroll.last_row = mouse->row;
			editor.drag_scroll.last_column = mouse->column;
			editor.drag_scroll.at_top_edge = (mouse->row <= content_top && E_CTX->row_offset > 0);
			editor.drag_scroll.at_bottom_edge = (mouse->row >= content_bottom);

			if (editor.drag_scroll.at_top_edge) {
				/* At top edge - scroll up */
				E_CTX->row_offset--;
			} else if (editor.drag_scroll.at_bottom_edge) {
				/* At bottom edge - scroll down */
				uint32_t max_offset = (E_BUF->line_count > editor.screen_rows)
					? E_BUF->line_count - editor.screen_rows : 0;
				if (E_CTX->row_offset < max_offset) {
					E_CTX->row_offset++;
				}
			}
			/* Update cursor position; anchor stays fixed */
			uint32_t file_row;
			uint16_t segment;
			uint32_t cell_col;

			uint32_t screen_col = mouse->column;
			if (screen_col < E_CTX->gutter_width) {
				screen_col = 0;
			} else {
				screen_col -= E_CTX->gutter_width;
			}

			/* Map screen row to logical line and segment */
			if (screen_row_to_line_segment(mouse->row, &file_row, &segment)) {
				struct line *line = &E_BUF->lines[file_row];
				if (editor.wrap_mode != WRAP_NONE) {
					cell_col = line_find_column_at_visual(
						line, E_BUF, segment, screen_col);
				} else {
					cell_col = screen_column_to_cell(
						file_row, screen_col + E_CTX->column_offset);
				}
			} else {
				/* Drag below content: clamp to last line */
				if (E_BUF->line_count > 0) {
					file_row = E_BUF->line_count - 1;
				} else {
					file_row = 0;
				}
				cell_col = 0;
			}

			E_CTX->cursor_row = file_row;
			E_CTX->cursor_column = cell_col;

			/* Ensure cursor stays in visible area */
			uint32_t max_visible_row = E_CTX->row_offset + editor.screen_rows - 1;
			if (E_CTX->cursor_row > max_visible_row && max_visible_row < E_BUF->line_count) {
				E_CTX->cursor_row = max_visible_row;
			}

			/* Ensure selection is active during drag */
			if (!E_CTX->selection_active) {
				selection_start();
			}
			break;
		}

		case MOUSE_LEFT_RELEASE:
			/* Selection complete; leave it active */
			editor.drag_scroll.active = false;
			editor.drag_scroll.at_top_edge = false;
			editor.drag_scroll.at_bottom_edge = false;
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

			if (E_CTX->row_offset >= scroll_amount) {
				E_CTX->row_offset -= scroll_amount;
			} else {
				E_CTX->row_offset = 0;
			}

			/* Keep cursor on screen (only if no selection) */
			if (!E_CTX->selection_active) {
				if (E_CTX->cursor_row >= E_CTX->row_offset + editor.screen_rows) {
					E_CTX->cursor_row = E_CTX->row_offset + editor.screen_rows - 1;
					if (E_CTX->cursor_row >= E_BUF->line_count && E_BUF->line_count > 0) {
						E_CTX->cursor_row = E_BUF->line_count - 1;
					}
				}
				/* Also clamp from above (cursor above visible area) */
				if (E_CTX->cursor_row < E_CTX->row_offset) {
					E_CTX->cursor_row = E_CTX->row_offset;
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

			if (E_CTX->row_offset + scroll_amount <= max_offset) {
				E_CTX->row_offset += scroll_amount;
			} else {
				E_CTX->row_offset = max_offset;
			}

			/* Keep cursor on screen (only if no selection) */
			if (!E_CTX->selection_active) {
				if (E_CTX->cursor_row < E_CTX->row_offset) {
					E_CTX->cursor_row = E_CTX->row_offset;
				}
			}
			break;
		}

		default:
			break;
	}
}
/*
 * Handle timer-based auto-scroll during drag selection.
 * Called from main loop when input times out while dragging.
 */
void editor_drag_scroll_tick(void)
{
	if (!editor.drag_scroll.active)
		return;
	if (editor.drag_scroll.at_top_edge && E_CTX->row_offset > 0) {
		/* Scroll up and update cursor */
		E_CTX->row_offset--;
		if (E_CTX->cursor_row > 0) {
			E_CTX->cursor_row--;
			/* Move cursor to start of line when scrolling up */
			E_CTX->cursor_column = 0;
		}
	} else if (editor.drag_scroll.at_bottom_edge) {
		/* Scroll down and update cursor */
		uint32_t max_offset = (E_BUF->line_count > editor.screen_rows)
			? E_BUF->line_count - editor.screen_rows : 0;
		if (E_CTX->row_offset < max_offset) {
			E_CTX->row_offset++;
			if (E_CTX->cursor_row < E_BUF->line_count - 1) {
				E_CTX->cursor_row++;
				/* Clamp cursor to visible area */
				uint32_t max_visible_row = E_CTX->row_offset + editor.screen_rows - 1;
				if (E_CTX->cursor_row > max_visible_row && max_visible_row < E_BUF->line_count) {
					E_CTX->cursor_row = max_visible_row;
				}
				/* Move cursor to end of line when scrolling down */
				struct line *line = &E_BUF->lines[E_CTX->cursor_row];
				line_warm(line, E_BUF);
				E_CTX->cursor_column = line->cell_count;
			}
		}
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
	selection_clear();
	search.active = true;
	search.replace_mode = false;
	search.query[0] = '\0';
	search.query_length = 0;
	search.replace_text[0] = '\0';
	search.replace_length = 0;
	search.editing_replace = false;
	search.saved_cursor_row = E_CTX->cursor_row;
	search.saved_cursor_column = E_CTX->cursor_column;
	search.saved_row_offset = E_CTX->row_offset;
	search.saved_column_offset = E_CTX->column_offset;
	search.has_match = false;
	search.direction = 1;
}

/*
 * Enter find & replace mode. Similar to search_enter but enables replace UI.
 */
static void replace_enter(void)
{
	selection_clear();
	search.active = true;
	search.replace_mode = true;
	search.query[0] = '\0';
	search.query_length = 0;
	search.replace_text[0] = '\0';
	search.replace_length = 0;
	search.editing_replace = false;
	search.saved_cursor_row = E_CTX->cursor_row;
	search.saved_cursor_column = E_CTX->cursor_column;
	search.saved_row_offset = E_CTX->row_offset;
	search.saved_column_offset = E_CTX->column_offset;
	search.has_match = false;
	search.direction = 1;
}

/*
 * Exit search mode, optionally restoring cursor position.
 */
static void search_exit(bool restore_position)
{
	if (restore_position) {
		E_CTX->cursor_row = search.saved_cursor_row;
		E_CTX->cursor_column = search.saved_cursor_column;
		E_CTX->row_offset = search.saved_row_offset;
		E_CTX->column_offset = search.saved_column_offset;
	}

	/* Free compiled regex */
	if (search.regex_compiled) {
		regfree(&search.compiled_regex);
		search.regex_compiled = false;
	}

	/* Cancel async search and clear results */
	search_async_cancel();
	search_async_clear_results();

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

	/* Validate pattern length to mitigate ReDoS attacks */
	if (search.query_length > MAX_REGEX_PATTERN_LENGTH) {
		snprintf(search.regex_error, sizeof(search.regex_error),
		         "Pattern too long (max %d)", MAX_REGEX_PATTERN_LENGTH);
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
void search_center_on_match(void)
{
	if (!search.has_match) {
		return;
	}

	/* Calculate the row offset that would center the match */
	uint32_t target_row = search.match_row;
	uint32_t half_screen = editor.screen_rows / 2;

	if (target_row >= half_screen) {
		E_CTX->row_offset = target_row - half_screen;
	} else {
		E_CTX->row_offset = 0;
	}

	/* Clamp to valid range (wrap-aware) */
	uint32_t max_offset = calculate_max_row_offset();
	if (E_CTX->row_offset > max_offset) {
		E_CTX->row_offset = max_offset;
	}
}

/*
 * Find the next match starting from current position.
 * If wrap is true, wraps around to beginning of file.
 * Returns true if a match was found.
 */
bool search_find_next(bool wrap)
{
	if (search.query_length == 0) {
		return false;
	}

	uint32_t start_row = E_CTX->cursor_row;
	uint32_t start_col = E_CTX->cursor_column + 1;

	/* Search from current position to end of file */
	for (uint32_t row = start_row; row < E_BUF->line_count; row++) {
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

		uint32_t col_start = (row == start_row) ? start_col : 0;

		for (uint32_t col = col_start; col < line->cell_count; col++) {
			if (search_matches_at(line, E_BUF, col, search.query, search.query_length)) {
				E_CTX->cursor_row = row;
				E_CTX->cursor_column = col;
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
			struct line *line = &E_BUF->lines[row];
			line_warm(line, E_BUF);

			uint32_t col_end = (row == start_row) ? start_col : line->cell_count;

			for (uint32_t col = 0; col < col_end; col++) {
				if (search_matches_at(line, E_BUF, col, search.query, search.query_length)) {
					E_CTX->cursor_row = row;
					E_CTX->cursor_column = col;
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
bool search_find_previous(bool wrap)
{
	if (search.query_length == 0) {
		return false;
	}

	int32_t start_row = (int32_t)E_CTX->cursor_row;
	int32_t start_col = (int32_t)E_CTX->cursor_column - 1;

	/* Search from current position to beginning of file */
	for (int32_t row = start_row; row >= 0; row--) {
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

		int32_t col_start = (row == start_row) ? start_col : (int32_t)line->cell_count - 1;
		if (col_start < 0) continue;

		for (int32_t col = col_start; col >= 0; col--) {
			if (search_matches_at(line, E_BUF, (uint32_t)col, search.query, search.query_length)) {
				E_CTX->cursor_row = (uint32_t)row;
				E_CTX->cursor_column = (uint32_t)col;
				search.match_row = (uint32_t)row;
				search.match_column = (uint32_t)col;
				search.has_match = true;
				search_center_on_match();
				return true;
			}
		}
	}

	/* Wrap around to end */
	if (wrap && E_BUF->line_count > 0) {
		for (int32_t row = (int32_t)E_BUF->line_count - 1; row >= start_row; row--) {
			struct line *line = &E_BUF->lines[row];
			line_warm(line, E_BUF);

			int32_t col_start = (row == start_row) ? start_col : (int32_t)line->cell_count - 1;
			if (col_start < 0) continue;

			for (int32_t col = col_start; col >= 0; col--) {
				if (search_matches_at(line, E_BUF, (uint32_t)col, search.query, search.query_length)) {
					E_CTX->cursor_row = (uint32_t)row;
					E_CTX->cursor_column = (uint32_t)col;
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
		E_CTX->cursor_row = search.saved_cursor_row;
		E_CTX->cursor_column = search.saved_cursor_column;
		search.has_match = false;
		search_async_cancel();
		search_async_clear_results();
		return;
	}

	/* Use async search for large files */
	if (search_should_use_async()) {
		search_async_start(search.query, search.use_regex,
		                   search.case_sensitive, search.whole_word);
		/* Results will come via worker_process_results() */
		return;
	}

	/* Synchronous search for small files */

	/* Start search from saved position */
	E_CTX->cursor_row = search.saved_cursor_row;
	E_CTX->cursor_column = search.saved_cursor_column;

	/* First check if there's a match at current position */
	if (E_CTX->cursor_row < E_BUF->line_count) {
		struct line *line = &E_BUF->lines[E_CTX->cursor_row];
		if (search_matches_at(line, E_BUF, E_CTX->cursor_column,
		                      search.query, search.query_length)) {
			search.match_row = E_CTX->cursor_row;
			search.match_column = E_CTX->cursor_column;
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

	if (E_CTX->cursor_row >= E_BUF->line_count) {
		return false;
	}

	struct line *line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);

	/* Get match length - use search_match_length_at for regex support */
	uint32_t match_cells = search_match_length_at(line, E_CTX->cursor_column);
	if (match_cells == 0) {
		return false;
	}

	/* Determine final replacement text (with backreference expansion) */
	char *final_replacement = NULL;

	if (search.use_regex && search.regex_compiled) {
		/* Build line string for backreference extraction */
		size_t string_length;
		uint32_t *byte_to_cell;
		char *line_string = line_to_string(line, E_CTX->cursor_column,
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

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Delete the match characters (from end to start for correct undo) */
	for (uint32_t i = 0; i < match_cells; i++) {
		uint32_t delete_position = E_CTX->cursor_column + match_cells - 1 - i;
		if (delete_position < line->cell_count) {
			uint32_t codepoint = line->cells[delete_position].codepoint;
			undo_record_delete_char(E_BUF, E_CTX->cursor_row,
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
	uint32_t insert_position = E_CTX->cursor_column;

	while (*r) {
		uint32_t codepoint;
		int bytes = utflite_decode(r, strlen(r), &codepoint);
		if (bytes <= 0) {
			break;
		}
		r += bytes;

		undo_record_insert_char(E_BUF, E_CTX->cursor_row,
		                        insert_position, codepoint);

		/* Make room and insert */
		int ret = line_ensure_capacity_checked(line, line->cell_count + 1);
		if (ret) {
			editor_set_status_message("Replace failed: %s", edit_strerror(ret));
			free(final_replacement);
			undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
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
	syntax_highlight_line(line, E_BUF, E_CTX->cursor_row);
	line_invalidate_wrap_cache(line);

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

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
		E_CTX->cursor_column += replace_cells;

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

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	uint32_t count = 0;

	/* Save current position */
	uint32_t saved_row = E_CTX->cursor_row;
	uint32_t saved_column = E_CTX->cursor_column;

	/* Start from beginning of file */
	E_CTX->cursor_row = 0;
	E_CTX->cursor_column = 0;

	/* Find and replace all matches without wrapping */
	while (search_find_next(false)) {
		if (search_replace_current()) {
			count++;

			/* Move past replacement to avoid infinite loop */
			uint32_t replace_cells = replace_count_cells(search.replace_text,
			                                              search.replace_length);
			E_CTX->cursor_column += replace_cells;
		} else {
			/* No replacement made, move forward to avoid infinite loop */
			E_CTX->cursor_column++;
			if (E_CTX->cursor_row < E_BUF->line_count) {
				struct line *line = &E_BUF->lines[E_CTX->cursor_row];
				line_warm(line, E_BUF);
				if (E_CTX->cursor_column >= line->cell_count) {
					E_CTX->cursor_row++;
					E_CTX->cursor_column = 0;
				}
			}
		}
	}

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Restore cursor to beginning if no replacements, or keep at last position */
	if (count == 0) {
		E_CTX->cursor_row = saved_row;
		E_CTX->cursor_column = saved_column;
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

	if (row >= E_BUF->line_count) {
		return 0;
	}

	struct line *line = &E_BUF->lines[row];
	line_warm(line, E_BUF);

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
	 *
	 * Both modes iterate by grapheme cluster to correctly handle
	 * multi-codepoint characters like emoji with skin tone modifiers.
	 */
	if (end_cell == UINT32_MAX) {
		/* Scroll mode: start_cell is actually a visual column offset */
		uint32_t column_offset = start_cell;
		while (cell_index < line->cell_count && visual_column < (int)column_offset) {
			uint32_t grapheme_end = cursor_next_grapheme(line, buffer, cell_index);
			int width = grapheme_display_width(line, cell_index, grapheme_end, visual_column);
			visual_column += width;
			cell_index = grapheme_end;
		}
		end_cell = line->cell_count;
	} else {
		/* Segment mode: compute visual column at start of segment */
		uint32_t i = 0;
		while (i < start_cell && i < line->cell_count) {
			uint32_t grapheme_end = cursor_next_grapheme(line, buffer, i);
			int width = grapheme_display_width(line, i, grapheme_end, visual_column);
			visual_column += width;
			i = grapheme_end;
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
		int length = snprintf(escape, sizeof(escape),
		         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
		         bg.red, bg.green, bg.blue,
		         style->fg.red, style->fg.green, style->fg.blue);
		length += attr_to_escape(style->attr, escape + length, sizeof(escape) - length);
		output_buffer_append(output, escape, length);
	}

	/*
	 * Hybrid mode: compute reveal range if cursor is on this line.
	 * When cursor is inside a markdown element, we reveal the raw syntax
	 * for that element to enable editing.
	 */
	uint32_t reveal_start = UINT32_MAX;
	uint32_t reveal_end = 0;
	if (E_CTX->hybrid_mode && is_cursor_line &&
	    syntax_is_markdown_file(buffer->filename)) {
		/* Ensure element cache is populated for HOT lines that were
		 * highlighted before hybrid mode was enabled */
		if (!line->md_elements || !line->md_elements->valid) {
			md_compute_elements(line);
			md_mark_hideable_cells(line);
		}
		md_should_reveal_element(line, E_CTX->cursor_column, &reveal_start, &reveal_end);
	}
	/*
	 * Hybrid mode: Check if cursor is inside a code block.
	 * When cursor is anywhere in a code block (fences or content), we reveal
	 * the entire block as raw markdown - no [language] label, no hidden fences,
	 * no special background.
	 */
	bool cursor_in_code_block = false;
	if (E_CTX->hybrid_mode && syntax_is_markdown_file(buffer->filename) &&
	    E_CTX->cursor_row < buffer->line_count) {
		struct line *cursor_line = &buffer->lines[E_CTX->cursor_row];
		uint32_t cursor_cell_count = line_get_cell_count(cursor_line, buffer);
		if (cursor_cell_count > 0) {
			uint16_t cursor_syntax = cursor_line->cells[0].syntax;
			if (cursor_syntax == SYNTAX_MD_CODE_BLOCK ||
			    cursor_syntax == SYNTAX_MD_CODE_FENCE_OPEN ||
			    cursor_syntax == SYNTAX_MD_CODE_FENCE_CLOSE) {
				cursor_in_code_block = true;
			}
		}
	}
	/*
	 * Hybrid mode: render [language] label for code fence opening lines.
	 * This replaces the raw ```language with a clean [language] indicator.
	 */
	if (E_CTX->hybrid_mode && line->cell_count > 0 &&
	    line->cells[0].syntax == SYNTAX_MD_CODE_FENCE_OPEN &&
	    syntax_is_markdown_file(buffer->filename) &&
	    reveal_start == UINT32_MAX &&  /* Not revealing this element */
	    !cursor_in_code_block) {       /* Cursor not in any code block */
		/* Extract language from cells after the fence characters */
		char language[17] = "code";  /* Default if no language specified */
		int lang_len = 0;
		bool found_lang = false;
		
		for (uint32_t i = 0; i < line->cell_count && lang_len < 16; i++) {
			uint32_t cp = line->cells[i].codepoint;
			/* Skip fence characters and leading whitespace */
			if (cp == '`' || cp == '~') continue;
			if (!found_lang && cp == ' ') continue;
			if (cp == ' ' || cp == '\t') break;  /* End of language */
			found_lang = true;
			/* Simple ASCII conversion for language name */
			if (cp < 128) {
				language[lang_len++] = (char)cp;
			}
		}
		if (found_lang && lang_len > 0) {
			language[lang_len] = '\0';
		}
		
		/* Render [language] with code fence style */
		struct style *style = &active_theme.syntax[SYNTAX_MD_CODE_FENCE_OPEN];
		struct syntax_color bg = is_cursor_line ? active_theme.cursor_line : style->bg;
		char escape[128];
		int esc_len = snprintf(escape, sizeof(escape),
		         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
		         bg.red, bg.green, bg.blue,
		         style->fg.red, style->fg.green, style->fg.blue);
		esc_len += attr_to_escape(style->attr, escape + esc_len, sizeof(escape) - esc_len);
		output_buffer_append(output, escape, esc_len);
		
		/* Output the label */
		output_buffer_append_string(output, "[");
		output_buffer_append_string(output, language);
		output_buffer_append_string(output, "]");
		
		return;  /* Done with this line */
	}
	/*
	 * Hybrid mode: hide closing fence lines entirely.
	 */
	if (E_CTX->hybrid_mode && line->cell_count > 0 &&
	    line->cells[0].syntax == SYNTAX_MD_CODE_FENCE_CLOSE &&
	    syntax_is_markdown_file(buffer->filename) &&
	    reveal_start == UINT32_MAX &&  /* Not revealing */
	    !cursor_in_code_block) {       /* Cursor not in any code block */
		return;  /* Output nothing for closing fence */
	}

	/* Render visible content up to end_cell or max_width */
	int rendered_width = 0;
	while (cell_index < end_cell && rendered_width < max_width) {
		uint32_t codepoint = line->cells[cell_index].codepoint;
		enum syntax_token syntax = line->cells[cell_index].syntax;
		uint8_t flags = line->cells[cell_index].flags;

		/*
		 * Hybrid mode: skip hideable cells unless cursor is revealing them
		 * or cursor is inside a code block (show entire block raw).
		 */
		if (E_CTX->hybrid_mode && (flags & CELL_FLAG_HIDEABLE) &&
		    syntax_is_markdown_file(buffer->filename) &&
		    !cursor_in_code_block) {
			/* Check if this cell is in the reveal range */
			bool in_reveal_range = (cell_index >= reveal_start && cell_index < reveal_end);
			if (!in_reveal_range) {
				/* Skip this cell - advance to next */
				cell_index++;
				continue;
			}
		}

		/*
		 * Determine highlight type with priority:
		 * secondary cursor > search current > search other > selection > trailing ws > cursor line
		 */
		int highlight = 0;
		int cursor_idx = multicursor_cursor_at(file_row, cell_index);
		if (cursor_idx >= 0 && (uint32_t)cursor_idx != E_CTX->primary_cursor) {
			highlight = 5;  /* Secondary cursor - inverted colors */
		} else {
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
		}

		/* Change colors if syntax or highlight changed */
		if (syntax != current_syntax || highlight != current_highlight) {
			char escape[128];
			struct style *style = &active_theme.syntax[syntax];
			struct syntax_color bg;
			struct syntax_color fg;
			bool inverted = false;

			switch (highlight) {
				case 5:  /* Secondary cursor - inverted colors */
					inverted = true;
					if (is_cursor_line) {
						bg = active_theme.cursor_line;
					} else {
						bg = style->bg;
					}
					break;
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
					} else if (!E_CTX->hybrid_mode &&
					           (syntax == SYNTAX_MD_CODE_BLOCK ||
					            syntax == SYNTAX_MD_CODE_FENCE_OPEN ||
					            syntax == SYNTAX_MD_CODE_FENCE_CLOSE)) {
						/* Code block bg only in hybrid mode */
						bg = active_theme.background;
					} else {
						bg = style->bg;
					}
					break;
			}

			/* For secondary cursor, swap foreground and background */
			if (inverted) {
				fg = bg;
				bg = style->fg;
			} else {
				/* Ensure foreground has sufficient contrast with background */
				fg = color_ensure_contrast(style->fg, bg);
			}

			int length = snprintf(escape, sizeof(escape),
			         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm",
			         bg.red, bg.green, bg.blue,
			         fg.red, fg.green, fg.blue);

			/* Add text attributes (strikethrough only in hybrid mode) */
			text_attr attr = style->attr;
			if (E_CTX->hybrid_mode && syntax == SYNTAX_MD_STRIKETHROUGH) {
				attr |= ATTR_STRIKE;
			}
			length += attr_to_escape(attr, escape + length, sizeof(escape) - length);

			output_buffer_append(output, escape, length);
			current_syntax = syntax;
			current_highlight = highlight;
		}

		int width;
		if (codepoint == '\t') {
			width = editor.tab_width - (visual_column % editor.tab_width);
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
			/*
			 * Handle grapheme cluster: get all codepoints that form
			 * a single visual character (emoji + modifiers, ZWJ sequences, etc.)
			 */
			uint32_t grapheme_end = cursor_next_grapheme(line, buffer, cell_index);
			width = grapheme_display_width(line, cell_index, grapheme_end, visual_column);

			/* Only render if we have room for the full grapheme width */
			if (rendered_width + width <= max_width) {
				/* Output all codepoints in the grapheme cluster */
				for (uint32_t gi = cell_index; gi < grapheme_end && gi < line->cell_count; gi++) {
					char utf8_buffer[UTFLITE_MAX_BYTES];
					uint32_t output_codepoint = line->cells[gi].codepoint;

					/* Hybrid mode substitutions */
					if (E_CTX->hybrid_mode) {
						enum syntax_token gi_syntax = line->cells[gi].syntax;
						bool in_reveal = (gi >= reveal_start && gi < reveal_end);

						/* Horizontal rule: - * _ → box drawing horizontal */
						if (gi_syntax == SYNTAX_MD_HORIZONTAL_RULE && !in_reveal) {
							if (output_codepoint == '-' || output_codepoint == '*' ||
							    output_codepoint == '_') {
								output_codepoint = 0x2500;  /* ─ BOX DRAWINGS LIGHT HORIZONTAL */
							}
						}

						/* List markers: - * + → bullet */
						if (gi_syntax == SYNTAX_MD_LIST_MARKER) {
							if (output_codepoint == '-' || output_codepoint == '*' ||
							    output_codepoint == '+') {
								output_codepoint = 0x2022;  /* • BULLET */
							}
						}

						/* Table pipes: | → box drawing vertical */
						if (gi_syntax == SYNTAX_MD_TABLE ||
						    gi_syntax == SYNTAX_MD_TABLE_HEADER) {
							if (output_codepoint == '|') {
								output_codepoint = 0x2502;  /* │ BOX DRAWINGS LIGHT VERTICAL */
							}
						}

						/* Table separator: dashes → box drawing horizontal */
						if (gi_syntax == SYNTAX_MD_TABLE_SEPARATOR) {
							if (output_codepoint == '-') {
								output_codepoint = 0x2500;  /* ─ BOX DRAWINGS LIGHT HORIZONTAL */
							} else if (output_codepoint == '|') {
								output_codepoint = 0x2502;  /* │ BOX DRAWINGS LIGHT VERTICAL */
							}
						}
						/* Blockquote: > → box drawing vertical */
						if (gi_syntax == SYNTAX_MD_BLOCKQUOTE) {
							if (output_codepoint == '>') {
								output_codepoint = 0x2502;  /* │ BOX DRAWINGS LIGHT VERTICAL */
							}
						}
					}

					int bytes = utflite_encode(output_codepoint, utf8_buffer);
					output_buffer_append(output, utf8_buffer, bytes);
				}
				rendered_width += width;
			} else {
				/* Not enough room for wide character, fill with spaces */
				while (rendered_width < max_width) {
					output_buffer_append_string(output, " ");
					rendered_width++;
				}
			}

			visual_column += width;
			cell_index = grapheme_end;
			continue;  /* Skip the cell_index++ at end of loop */
		}

		visual_column += width;
		cell_index++;
	}
	/*
	 * Check for secondary cursors at end of line.
	 * These are cursors positioned at cell_count (after the last character).
	 */
	if (rendered_width < max_width) {
		int cursor_idx = multicursor_cursor_at(file_row, line->cell_count);
		if (cursor_idx >= 0 && (uint32_t)cursor_idx != E_CTX->primary_cursor) {
			/* Render secondary cursor at end of line with inverted space */
			struct style *style = &active_theme.syntax[SYNTAX_NORMAL];
			struct syntax_color fg_color = is_cursor_line
				? active_theme.cursor_line : style->bg;
			struct syntax_color bg_color = style->fg;
			char escape[128];
			int length = snprintf(escape, sizeof(escape),
			         "\x1b[0;48;2;%d;%d;%d;38;2;%d;%d;%dm ",
			         bg_color.red, bg_color.green, bg_color.blue,
			         fg_color.red, fg_color.green, fg_color.blue);
			output_buffer_append(output, escape, length);
			rendered_width++;
		}
	}
	/*
	 * Hybrid mode: extend code block background to terminal width.
	 * This creates a visual "box" effect for code blocks.
	 */
	if (E_CTX->hybrid_mode && syntax_is_markdown_file(buffer->filename) &&
	    line->cell_count > 0 && !cursor_in_code_block) {
		uint16_t first_syntax = line->cells[0].syntax;
		if (first_syntax == SYNTAX_MD_CODE_BLOCK ||
		    first_syntax == SYNTAX_MD_CODE_FENCE_OPEN) {
			/* Calculate remaining width to fill */
			int remaining = max_width - rendered_width;
			if (remaining > 0) {
				/* Set code block background color */
				struct style *style = &active_theme.syntax[SYNTAX_MD_CODE_BLOCK];
				struct syntax_color bg = is_cursor_line ?
					active_theme.cursor_line : style->bg;
				char escape[64];
				int esc_len = snprintf(escape, sizeof(escape),
					"\x1b[0;48;2;%d;%d;%dm",
					bg.red, bg.green, bg.blue);
				output_buffer_append(output, escape, esc_len);
				/* Fill remaining width with spaces */
				for (int i = 0; i < remaining; i++) {
					output_buffer_append_string(output, " ");
				}
				rendered_width = max_width;  /* Mark as filled */
			}
		}
	}

	/*
	 * Set background color for any remaining content.
	 * The actual filling happens in render_draw_rows after this function
	 * returns, where the color column (if any) is also handled.
	 */
	struct syntax_color fill_bg = is_cursor_line ? active_theme.cursor_line : active_theme.background;
	char bg_escape[48];
	snprintf(bg_escape, sizeof(bg_escape), "\x1b[48;2;%d;%d;%dm",
	         fill_bg.red, fill_bg.green, fill_bg.blue);
	output_buffer_append_string(output, bg_escape);
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
	/* ASCII art welcome banner - 8 lines total (6 art + 1 blank + 1 tagline) */
	static const char *welcome_art[] = {
		"███████╗██████╗ ██╗████████╗",
		"██╔════╝██╔══██╗██║╚══██╔══╝",
		"█████╗  ██║  ██║██║   ██║   ",
		"██╔══╝  ██║  ██║██║   ██║   ",
		"███████╗██████╔╝██║   ██║   ",
		"╚══════╝╚═════╝ ╚═╝   ╚═╝   ",
		"",
		"Enter. Delete. Insert. Type."
	};
	static const int welcome_art_lines = 8;
	static const int welcome_art_width = 28;  /* Display width of ASCII art */
	uint32_t welcome_start = (editor.screen_rows > (uint32_t)welcome_art_lines)
	                         ? (editor.screen_rows - welcome_art_lines) / 2 : 0;
	int text_area_width = editor.screen_columns - E_CTX->gutter_width;

	/*
	 * Track current position in the buffer.
	 * file_row = logical line index
	 * segment = which segment of that line (0 = first/only)
	 */
	uint32_t file_row = E_CTX->row_offset;
	uint16_t segment = 0;

	/*
	 * Cursor segment is needed to determine if we should highlight
	 * the current screen row as "cursor line".
	 */
	uint16_t cursor_segment = 0;
	if (E_CTX->cursor_row < E_BUF->line_count) {
		struct line *cursor_line = &E_BUF->lines[E_CTX->cursor_row];
		cursor_segment = line_get_segment_for_column(cursor_line,
		                                             E_BUF,
		                                             E_CTX->cursor_column);
	}

	for (uint32_t screen_row = 0; screen_row < editor.screen_rows; screen_row++) {
		output_buffer_append_string(output, ESCAPE_CLEAR_LINE);

		bool is_empty_buffer = (E_BUF->line_count == 1 &&
		                        E_BUF->lines[0].cell_count == 0);
		bool is_empty_buffer_first_line = (is_empty_buffer && file_row == 0);

		if (file_row >= E_BUF->line_count && !is_empty_buffer_first_line) {
			/* Empty line past end of file */
			int welcome_line = (int)screen_row - (int)welcome_start;
			if (is_empty_buffer && welcome_line >= 0 && welcome_line < welcome_art_lines) {
				/* Welcome ASCII art */
				char color_escape[128];
				int escape_len = style_to_escape(&active_theme.welcome,
				                                 color_escape, sizeof(color_escape));
				output_buffer_append(output, color_escape, escape_len);

				const char *line = welcome_art[welcome_line];
				int line_width = (welcome_line == 7) ? 28 : welcome_art_width;  /* Tagline is 28 chars */
				int padding = (text_area_width - line_width) / 2;
				if (padding < 0) padding = 0;

				for (uint32_t i = 0; i < E_CTX->gutter_width; i++) {
					output_buffer_append_string(output, " ");
				}
				for (int i = 0; i < padding; i++) {
					output_buffer_append_string(output, " ");
				}
				output_buffer_append_string(output, line);
			} else if (editor.color_column > 0) {
				/* Draw color column marker on empty lines */
				uint32_t col_pos = editor.color_column - 1;
				if (col_pos < (uint32_t)text_area_width) {
					for (uint32_t i = 0; i < E_CTX->gutter_width + col_pos; i++) {
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
		} else if (file_row < E_BUF->line_count) {
			struct line *line = &E_BUF->lines[file_row];

			/* Ensure line is warmed before rendering */
			ensure_line_warm_for_render(line, E_BUF, file_row);

			line_ensure_wrap_cache(line, E_BUF);

			/*
			 * Determine if this screen row should have cursor line highlight.
			 * Only highlight the segment containing the cursor.
			 */
			bool is_cursor_line_segment =
				(file_row == E_CTX->cursor_row && segment == cursor_segment);

			/* Draw gutter: line number for segment 0, indicator for continuations */
			if (editor.show_line_numbers && E_CTX->gutter_width > 0) {
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
					         "%-*u ", E_CTX->gutter_width - 1, file_row + 1);
					output_buffer_append(output, line_number_buffer,
					                     E_CTX->gutter_width);
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
					for (uint32_t i = 0; i < E_CTX->gutter_width - 2; i++) {
						output_buffer_append_string(output, " ");
					}
					output_buffer_append_string(output, indicator);
					output_buffer_append_string(output, " ");
				}
			}

			/* Calculate segment bounds */
			uint32_t start_cell = line_get_segment_start(line, E_BUF, segment);
			uint32_t end_cell = line_get_segment_end(line, E_BUF, segment);

			/*
			 * For WRAP_NONE mode, use horizontal scrolling.
			 * For wrap modes, render the segment directly.
			 */
			if (editor.wrap_mode == WRAP_NONE) {
				/* No wrap: use column_offset for horizontal scrolling */
				render_line_content(output, line, E_BUF, file_row,
				                    E_CTX->column_offset, UINT32_MAX,
				                    text_area_width, is_cursor_line_segment);
			} else {
				/* Wrap enabled: render this segment */
				render_line_content(output, line, E_BUF, file_row,
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
					uint32_t idx = 0;
					while (idx < line->cell_count) {
						uint32_t grapheme_end = cursor_next_grapheme(line, E_BUF, idx);
						int w = grapheme_display_width(line, idx, grapheme_end, line_visual_width);
						line_visual_width += w;
						idx = grapheme_end;
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
				line_warm(line, E_BUF);
				uint32_t idx = 0;
				while (idx < line->cell_count) {
					uint32_t grapheme_end = cursor_next_grapheme(line, E_BUF, idx);
					int w = grapheme_display_width(line, idx, grapheme_end, line_visual_width);
					line_visual_width += w;
					idx = grapheme_end;
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
			} else {
				/* No color column on non-cursor line - just clear to end */
				char fill_escape[64];
				snprintf(fill_escape, sizeof(fill_escape),
				         "\x1b[48;2;%d;%d;%dm\x1b[K",
				         active_theme.background.red,
				         active_theme.background.green,
				         active_theme.background.blue);
				output_buffer_append_string(output, fill_escape);
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
 * Draw the tab bar showing all open buffers.
 * Only drawn when there's more than one context open.
 */
static void render_draw_tab_bar(struct output_buffer *output)
{
	if (editor.context_count <= 1) {
		return;  /* No tab bar for single buffer */
	}
	char escape[128];
	int current_pos = 0;
	/* Tab bar background from theme */
	snprintf(escape, sizeof(escape), "\x1b[0m\x1b[48;2;%u;%u;%um",
	         active_theme.tab_bar.bg.red,
	         active_theme.tab_bar.bg.green,
	         active_theme.tab_bar.bg.blue);
	output_buffer_append_string(output, escape);
	/* Draw each tab */
	for (uint32_t i = 0; i < editor.context_count; i++) {
		struct editor_context *ctx = &editor.contexts[i];
		struct buffer *buf = &ctx->buffer;
		/* Get display name (basename of filename or "[No Name]") */
		const char *name = buf->filename;
		const char *display_name;
		if (name) {
			/* Extract basename */
			const char *slash = strrchr(name, '/');
			display_name = slash ? slash + 1 : name;
		} else {
			display_name = "[No Name]";
		}
		/* Truncate long names */
		char name_buf[32];
		size_t name_len = strlen(display_name);
		if (name_len > 20) {
			snprintf(name_buf, sizeof(name_buf), "%.17s...", display_name);
			display_name = name_buf;
		}
		/* Calculate tab width */
		int tab_width = (int)strlen(display_name) + 3;  /* " name " + modified indicator */
		if (buf->is_modified) tab_width += 4;  /* " [+]" */
		/* Check if tab fits */
		if (current_pos + tab_width > (int)editor.screen_columns - 1) {
			/* Tab doesn't fit, show "..." indicator */
			int remaining = (int)editor.screen_columns - current_pos;
			if (remaining > 3) {
				output_buffer_append_string(output, "...");
				current_pos += 3;
			}
			break;
		}
		/* Active vs inactive tab styling from theme */
		if (i == editor.active_context) {
			/* Active tab */
			snprintf(escape, sizeof(escape),
			         "\x1b[48;2;%u;%u;%um\x1b[38;2;%u;%u;%um",
			         active_theme.tab_active.bg.red,
			         active_theme.tab_active.bg.green,
			         active_theme.tab_active.bg.blue,
			         active_theme.tab_active.fg.red,
			         active_theme.tab_active.fg.green,
			         active_theme.tab_active.fg.blue);
		} else {
			/* Inactive tab */
			snprintf(escape, sizeof(escape),
			         "\x1b[48;2;%u;%u;%um\x1b[38;2;%u;%u;%um",
			         active_theme.tab_inactive.bg.red,
			         active_theme.tab_inactive.bg.green,
			         active_theme.tab_inactive.bg.blue,
			         active_theme.tab_inactive.fg.red,
			         active_theme.tab_inactive.fg.green,
			         active_theme.tab_inactive.fg.blue);
		}
		output_buffer_append_string(output, escape);
		/* Draw tab content: " name [+]" */
		output_buffer_append_char(output, ' ');
		output_buffer_append_string(output, display_name);
		current_pos += 1 + (int)strlen(display_name);
		if (buf->is_modified) {
			/* Modified indicator from theme */
			snprintf(escape, sizeof(escape),
			         "\x1b[38;2;%u;%u;%um",
			         active_theme.tab_modified.fg.red,
			         active_theme.tab_modified.fg.green,
			         active_theme.tab_modified.fg.blue);
			output_buffer_append_string(output, escape);
			output_buffer_append_string(output, " [+]");
			current_pos += 4;
		}
		output_buffer_append_char(output, ' ');
		current_pos++;
		/* Restore tab bar background between tabs */
		snprintf(escape, sizeof(escape), "\x1b[48;2;%u;%u;%um",
		         active_theme.tab_bar.bg.red,
		         active_theme.tab_bar.bg.green,
		         active_theme.tab_bar.bg.blue);
		output_buffer_append_string(output, escape);
	}
	/* Fill rest of row */
	while (current_pos < (int)editor.screen_columns) {
		output_buffer_append_char(output, ' ');
		current_pos++;
	}
	output_buffer_append_string(output, ESCAPE_RESET);
	output_buffer_append_string(output, "\r\n");
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
	const char *filename = E_BUF->filename ? E_BUF->filename : "[No Name]";
	char filename_buf[104];
	int filename_len = snprintf(filename_buf, sizeof(filename_buf), " %.100s ", filename);

	escape_len = style_to_escape(&active_theme.status_filename, color_escape,
	                             sizeof(color_escape));
	output_buffer_append(output, color_escape, escape_len);
	output_buffer_append(output, filename_buf, filename_len);

	int current_pos = filename_len;

	/* Modified indicator with its own style */
	if (E_BUF->is_modified) {
		escape_len = style_to_escape(&active_theme.status_modified, color_escape,
		                             sizeof(color_escape));
		output_buffer_append(output, color_escape, escape_len);
		output_buffer_append_string(output, "[+]");
		current_pos += 3;
	}

	/* Right-aligned content: link URL or position */
	char right_status[256];
	int right_length;

	if (E_CTX->link_preview_active && E_CTX->link_url_preview[0] != '\0') {
		/* Show link URL in status bar */
		int max_url_len = (int)editor.screen_columns - current_pos - 8;
		if (max_url_len < 20) max_url_len = 20;
		if (max_url_len > 200) max_url_len = 200;

		size_t url_len = strlen(E_CTX->link_url_preview);
		if ((int)url_len > max_url_len) {
			/* Truncate with ellipsis */
			right_length = snprintf(right_status, sizeof(right_status),
			                        "%.200s... ", E_CTX->link_url_preview);
			right_status[max_url_len] = '\0';
			right_length = (int)strlen(right_status);
		} else {
			right_length = snprintf(right_status, sizeof(right_status),
			                        "%s ", E_CTX->link_url_preview);
		}
	} else {
		/* Normal position indicator */
		right_length = snprintf(right_status, sizeof(right_status), "%u/%u ",
		                        E_CTX->cursor_row + 1, E_BUF->line_count);
	}

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
	output_buffer_append_string(output, ESCAPE_RESET);
	output_buffer_append_string(output, "\r\n");
}

/*
 * Helper to set message bar style.
 */
static void message_bar_set_style(struct output_buffer *output,
                                  const struct style *style)
{
	char escape[128];
	int length = style_to_escape(style, escape, sizeof(escape));
	output_buffer_append(output, escape, length);
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
	output_buffer_append_string(output, ESCAPE_CLEAR_TO_EOL);

	if (save_as_is_active()) {
		if (save_as_is_confirm_overwrite()) {
			/* Warning prompt */
			message_bar_set_style(output, &active_theme.prompt_warning);
			output_buffer_append_string(output, "File exists. Overwrite? (y/n)");
		} else {
			/* Label */
			message_bar_set_style(output, &active_theme.prompt_label);
			output_buffer_append_string(output, "Save as: ");

			/* Input */
			message_bar_set_style(output, &active_theme.prompt_input);
			const char *path = save_as_get_path();
			char path_buf[PATH_MAX];
			int path_len = strlen(path);
			int max_len = (int)editor.screen_columns - 9;
			if (path_len > max_len && max_len > 4) {
				/* Truncate from the left */
				snprintf(path_buf, sizeof(path_buf), "...%s",
				         path + path_len - max_len + 3);
				output_buffer_append_string(output, path_buf);
			} else {
				output_buffer_append(output, path, path_len);
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

	if (goto_line_is_active()) {
		/* Label */
		message_bar_set_style(output, &active_theme.prompt_label);
		output_buffer_append_string(output, "Go to line: ");

		/* Input */
		message_bar_set_style(output, &active_theme.prompt_input);
		output_buffer_append_string(output, goto_line_get_input());
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
int __must_check render_refresh_screen(void)
{
	/* Validate state before rendering */
	if (editor.context_count == 0 || editor.active_context >= editor.context_count) {
		fprintf(stderr, "render: invalid context state count=%u active=%u\n",
		        editor.context_count, editor.active_context);
		return -1;
	}
	if (E_BUF->lines == NULL) {
		fprintf(stderr, "render: buffer lines is NULL\n");
		return -1;
	}
	editor_update_gutter_width();
	editor_scroll();

	/* Update link preview state for hybrid mode */
	md_update_link_preview();

	struct output_buffer output;
	int ret = output_buffer_init_checked(&output);
	if (ret)
		return ret;

	/* Hide cursor */
	output_buffer_append_string(&output, ESCAPE_CURSOR_HIDE);
	/* Move cursor to home */
	output_buffer_append_string(&output, ESCAPE_CURSOR_HOME);

	/* Set background color for the entire screen */
	char bg_escape[32];
	snprintf(bg_escape, sizeof(bg_escape), "\x1b[48;2;%d;%d;%dm",
	         active_theme.background.red, active_theme.background.green, active_theme.background.blue);
	output_buffer_append_string(&output, bg_escape);
	render_draw_tab_bar(&output);

	render_draw_rows(&output);
	render_draw_status_bar(&output);
	render_draw_message_bar(&output);

	/* Position cursor - account for wrapped segments in wrap mode */
	char cursor_position[32];
	uint32_t cursor_screen_row;
	uint32_t cursor_screen_col;

	if (editor.wrap_mode == WRAP_NONE) {
		/* No wrap: simple calculation */
		cursor_screen_row = (E_CTX->cursor_row - E_CTX->row_offset) + 1;
		uint32_t render_column = editor_get_render_column(
			E_CTX->cursor_row, E_CTX->cursor_column);
		cursor_screen_col = (render_column - E_CTX->column_offset) +
		                    E_CTX->gutter_width + 1;
	} else {
		/*
		 * Wrap enabled: sum screen rows from row_offset to cursor_row,
		 * then add the cursor's segment within its line.
		 */
		cursor_screen_row = 1;  /* 1-based terminal rows */
		for (uint32_t row = E_CTX->row_offset; row < E_CTX->cursor_row &&
		     row < E_BUF->line_count; row++) {
			struct line *line = &E_BUF->lines[row];
			line_ensure_wrap_cache(line, E_BUF);
			cursor_screen_row += line->wrap_segment_count;
		}

		/* Add the segment offset within cursor's line */
		if (E_CTX->cursor_row < E_BUF->line_count) {
			struct line *cursor_line = &E_BUF->lines[E_CTX->cursor_row];
			line_ensure_wrap_cache(cursor_line, E_BUF);
			uint16_t cursor_segment = line_get_segment_for_column(
				cursor_line, E_BUF, E_CTX->cursor_column);
			cursor_screen_row += cursor_segment;

			/* Column is visual position within segment */
			uint32_t visual_col = line_get_visual_column_in_segment(
				cursor_line, E_BUF, cursor_segment,
				E_CTX->cursor_column);
			cursor_screen_col = visual_col + E_CTX->gutter_width + 1;
		} else {
			cursor_screen_col = E_CTX->gutter_width + 1;
		}
	}
	/* Account for tab bar row when multiple buffers open */
	if (editor.context_count > 1) {
		cursor_screen_row += TAB_BAR_ROWS;
	}

	/* Clamp cursor_screen_row to content area (prevent cursor in status bar) */
	uint32_t max_screen_row = editor.screen_rows;
	if (editor.context_count > 1) {
		max_screen_row += TAB_BAR_ROWS;
	}
	if (cursor_screen_row > max_screen_row) {
		cursor_screen_row = max_screen_row;
	}

	snprintf(cursor_position, sizeof(cursor_position), "\x1b[%u;%uH",
	         cursor_screen_row, cursor_screen_col);
	output_buffer_append_string(&output, cursor_position);

	/* Show cursor */
	output_buffer_append_string(&output, ESCAPE_CURSOR_SHOW);

	output_buffer_flush(&output);
	output_buffer_free(&output);
	return 0;
}

/*
 * Open a file, replacing current buffer.
 * Returns true if file was opened successfully.
 */
static bool editor_open_file(const char *path)
{
	/* Clear existing buffer */
	debug_log("editor_open_file: clearing buffer for path=%s", path);
	buffer_free(E_BUF);
	/* Reset editor state */
	E_CTX->cursor_row = 0;
	E_CTX->cursor_column = 0;
	E_CTX->row_offset = 0;
	E_CTX->column_offset = 0;
	E_CTX->selection_active = false;

	/* Exit multi-cursor mode if active */
	if (E_CTX->cursor_count > 0) {
		multicursor_exit();
	}

	/* Initialize new buffer */
	buffer_init(E_BUF);

	/* Load the file */
	int ret = file_open(E_BUF, path);
	if (ret) {
		/* Failed to load - show error with reason */
		editor_set_status_message("Cannot open file: %s", edit_strerror(ret));
		return false;
	}

	editor_set_status_message("Opened: %s (%u lines)", path, E_BUF->line_count);

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
	if (E_BUF->is_modified && !warned) {
		editor_set_status_message("Unsaved changes! Press Ctrl+O again to open anyway");
		warned = true;
		return;
	}
	warned = false;

	/* Show open file dialog */
	char *path = open_file_dialog();

	/* Redraw screen after dialog closes */
	{ int ret_ignored = render_refresh_screen(); (void)ret_ignored; }

	if (path) {
		editor_open_file(path);
		free(path);
	} else {
		editor_set_status_message("Open cancelled");
	}
}
/*
 * Toggle help file display.
 * If help is open, return to previous file. Otherwise open help.
 */
static void editor_toggle_help(void)
{
	char help_path[PATH_MAX];
	const char *home = getenv("HOME");
	if (!home) {
		editor_set_status_message("Cannot find HOME directory");
		return;
	}
	snprintf(help_path, sizeof(help_path), "%s%s", home, HELP_FILE);

	/* Check if we're currently in the help context */
	if (editor.help_context_index >= 0 &&
	    editor.active_context == (uint32_t)editor.help_context_index) {
		/* Close the help context - this will auto-switch to another context */
		uint32_t help_idx = (uint32_t)editor.help_context_index;
		editor_context_close(help_idx);
		/* help_context_index is reset to -1 by editor_context_close */
		editor_set_status_message("Help closed");
	} else {
		/* Save current context and open/switch to help */
		editor.previous_context_before_help = editor.active_context;

		if (editor.help_context_index >= 0) {
			/* Help context already exists, just switch to it */
			editor_context_switch((uint32_t)editor.help_context_index);
			editor_set_status_message("Press F1 to close help");
		} else {
			/* Create new help context */
			int new_index = editor_context_new();
			if (new_index < 0) {
				editor_set_status_message("Too many buffers open");
				return;
			}
			editor.help_context_index = new_index;
			editor_context_switch((uint32_t)new_index);

			/* Load help file into new context */
			int ret = file_open(E_BUF, help_path);
			if (ret != 0) {
				editor_set_status_message("Help file not found: %s", help_path);
				/* Close the help context we just created */
				editor_context_close((uint32_t)editor.help_context_index);
				editor.help_context_index = -1;
				editor_context_switch(editor.previous_context_before_help);
				return;
			}
			editor_update_gutter_width();
			editor_set_status_message("Press F1 to close help");
		}
	}
}

/*
 * Handle the F5/Ctrl+T command to open theme picker.
 */
static void editor_command_theme_picker(void)
{
	int selected = theme_picker_dialog();

	/* Redraw screen after dialog closes */
	{ int ret_ignored = render_refresh_screen(); (void)ret_ignored; }

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
				if (search_should_use_async_replace()) {
					search_async_replace_start(search.query, search.replace_text,
					                    search.use_regex, search.case_sensitive,
					                    search.whole_word);
				} else {
					search_replace_all();
				}
			}
			return true;

		case KEY_BACKSPACE:
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
		case KEY_F3:
		case KEY_ARROW_DOWN:
		case KEY_ARROW_RIGHT:
			/* Find next match */
			search.direction = 1;
			if (search_should_use_async()) {
				if (!search_async_next_match()) {
					editor_set_status_message("No more matches");
				}
			} else {
				if (!search_find_next(true)) {
					editor_set_status_message("No more matches");
				}
			}
			return true;

		case KEY_ALT_P:
		case KEY_SHIFT_F3:
		case KEY_ARROW_UP:
		case KEY_ARROW_LEFT:
			/* Find previous match */
			search.direction = -1;
			if (search_should_use_async()) {
				if (!search_async_prev_match()) {
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
 * Line Operations - Delete and Duplicate
 *****************************************************************************/

/*
 * Select all text in the buffer.
 */
static void editor_select_all(void)
{
	if (E_BUF->line_count == 0) {
		return;
	}

	/* Anchor at start */
	E_CTX->selection_anchor_row = 0;
	E_CTX->selection_anchor_column = 0;

	/* Cursor at end */
	E_CTX->cursor_row = E_BUF->line_count - 1;
	struct line *last_line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(last_line, E_BUF);
	E_CTX->cursor_column = last_line->cell_count;

	E_CTX->selection_active = true;
	editor_set_status_message("Selected all");
}

/*
 * Delete the current line. Records undo operation for the deleted content.
 */
static void editor_delete_line(void)
{
	if (E_BUF->line_count == 0) {
		return;
	}

	uint32_t row = E_CTX->cursor_row;
	if (row >= E_BUF->line_count) {
		row = E_BUF->line_count - 1;
	}

	struct line *line = &E_BUF->lines[row];
	line_warm(line, E_BUF);

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

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
		if (row < E_BUF->line_count - 1) {
			text[text_len++] = '\n';
		}
		text[text_len] = '\0';

		/* Record the deletion - from start of this line to start of next line */
		uint32_t end_row = (row < E_BUF->line_count - 1) ? row + 1 : row;
		uint32_t end_col = (row < E_BUF->line_count - 1) ? 0 : line->cell_count;
		undo_record_delete_text(E_BUF, row, 0, end_row, end_col, text, text_len);
		free(text);
	}

	/* Delete the line */
	buffer_delete_line(E_BUF, row);

	/* Handle empty buffer */
	if (E_BUF->line_count == 0) {
		buffer_ensure_capacity(E_BUF, 1);
		line_init(&E_BUF->lines[0]);
		E_BUF->line_count = 1;
	}

	/* Adjust cursor */
	if (E_CTX->cursor_row >= E_BUF->line_count) {
		E_CTX->cursor_row = E_BUF->line_count - 1;
	}
	E_CTX->cursor_column = 0;

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
	selection_clear();

	editor_set_status_message("Line deleted");
}

/*
 * Duplicate the current line. Inserts a copy below the current line.
 */
static void editor_duplicate_line(void)
{
	if (E_BUF->line_count == 0) {
		return;
	}

	uint32_t row = E_CTX->cursor_row;
	if (row >= E_BUF->line_count) {
		row = E_BUF->line_count - 1;
	}

	struct line *source = &E_BUF->lines[row];
	line_warm(source, E_BUF);

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Save cursor at end of line, insert newline (creates a new line) */
	uint32_t saved_col = E_CTX->cursor_column;

	E_CTX->cursor_row = row;
	E_CTX->cursor_column = source->cell_count;

	/* Record and insert newline */
	undo_record_insert_newline(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);
	buffer_insert_newline(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Move to the new line */
	E_CTX->cursor_row = row + 1;
	E_CTX->cursor_column = 0;

	/* Re-get source pointer (may have moved due to realloc) */
	source = &E_BUF->lines[row];

	/* Copy each character from source into the new line */
	for (uint32_t i = 0; i < source->cell_count; i++) {
		uint32_t codepoint = source->cells[i].codepoint;
		undo_record_insert_char(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, codepoint);
		buffer_insert_cell_at_column(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column, codepoint);
		E_CTX->cursor_column++;
	}

	/* Restore cursor column on the duplicated line */
	struct line *dest = &E_BUF->lines[row + 1];
	E_CTX->cursor_column = saved_col;
	if (E_CTX->cursor_column > dest->cell_count) {
		E_CTX->cursor_column = dest->cell_count;
	}

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

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
	if (E_CTX->cursor_row >= E_BUF->line_count) {
		editor_set_status_message("No bracket found");
		return;
	}

	struct line *line = &E_BUF->lines[E_CTX->cursor_row];
	line_warm(line, E_BUF);

	uint32_t search_column = E_CTX->cursor_column;
	uint32_t match_row, match_column;
	bool found = false;

	/*
	 * First try at the cursor position. If not a bracket, scan forward
	 * on the current line to find one.
	 */
	if (search_column < line->cell_count &&
	    is_matchable_bracket(line->cells[search_column].codepoint)) {
		found = buffer_find_pair_partner(E_BUF,
		                                 E_CTX->cursor_row, search_column,
		                                 &match_row, &match_column);
	}

	/* If not found at cursor, scan forward on the line */
	if (!found) {
		for (uint32_t column = search_column + 1; column < line->cell_count; column++) {
			if (is_matchable_bracket(line->cells[column].codepoint)) {
				found = buffer_find_pair_partner(E_BUF,
				                                 E_CTX->cursor_row, column,
				                                 &match_row, &match_column);
				if (found) {
					break;
				}
			}
		}
	}

	if (found) {
		selection_clear();
		E_CTX->cursor_row = match_row;
		E_CTX->cursor_column = match_column;

		/* Ensure cursor is visible by scrolling if needed */
		if (E_CTX->cursor_row < E_CTX->row_offset) {
			E_CTX->row_offset = E_CTX->cursor_row;
		} else if (E_CTX->cursor_row >= E_CTX->row_offset + editor.screen_rows) {
			E_CTX->row_offset = E_CTX->cursor_row - editor.screen_rows + 1;
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

	if (E_CTX->selection_active && !selection_is_empty()) {
		uint32_t start_column, end_column;
		selection_get_range(&start_row, &start_column, &end_row, &end_column);
	} else {
		start_row = E_CTX->cursor_row;
		end_row = E_CTX->cursor_row;
	}

	if (start_row >= E_BUF->line_count) {
		return;
	}
	if (end_row >= E_BUF->line_count) {
		end_row = E_BUF->line_count - 1;
	}

	/*
	 * Determine action: if ALL non-empty lines are commented, uncomment.
	 * Otherwise, comment all lines.
	 */
	bool all_commented = true;
	bool has_content = false;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

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

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/*
	 * Find the minimum indent across all lines with content. We insert
	 * comments at this position for visual alignment.
	 */
	uint32_t min_indent = UINT32_MAX;
	if (should_comment) {
		for (uint32_t row = start_row; row <= end_row; row++) {
			struct line *line = &E_BUF->lines[row];

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
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

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
			undo_record_insert_char(E_BUF, row, insert_position, '/');
			undo_record_insert_char(E_BUF, row, insert_position + 1, '/');
			undo_record_insert_char(E_BUF, row, insert_position + 2, ' ');

			/* Adjust cursor if on this line */
			if (row == E_CTX->cursor_row && E_CTX->cursor_column >= insert_position) {
				E_CTX->cursor_column += 3;
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
				undo_record_delete_char(E_BUF, row, delete_position, codepoint);
			}

			/* Shift cells left to remove the comment */
			memmove(&line->cells[comment_start],
				&line->cells[comment_start + chars_to_remove],
				(line->cell_count - comment_start - chars_to_remove) * sizeof(struct cell));

			line->cell_count -= chars_to_remove;
			line_set_temperature(line, LINE_TEMPERATURE_HOT);

			/* Adjust cursor if on this line */
			if (row == E_CTX->cursor_row && E_CTX->cursor_column > comment_start) {
				if (E_CTX->cursor_column >= comment_start + chars_to_remove) {
					E_CTX->cursor_column -= chars_to_remove;
				} else {
					E_CTX->cursor_column = comment_start;
				}
			}
		}

		/* Recompute line metadata */
		neighbor_compute_line(line);
		syntax_highlight_line(line, E_BUF, row);
		line_invalidate_wrap_cache(line);
	}

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

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
	if (E_BUF->line_count < 2) {
		return;
	}

	uint32_t row = E_CTX->cursor_row;
	if (row == 0) {
		return;
	}
	if (row >= E_BUF->line_count) {
		row = E_BUF->line_count - 1;
	}

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Swap with line above */
	buffer_swap_lines(E_BUF, row, row - 1);

	/* Invalidate wrap caches */
	line_invalidate_wrap_cache(&E_BUF->lines[row]);
	line_invalidate_wrap_cache(&E_BUF->lines[row - 1]);

	/* Move cursor up */
	E_CTX->cursor_row--;

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	editor_set_status_message("Line moved up");
}

/*
 * Move the current line down one position.
 */
static void editor_move_line_down(void)
{
	if (E_BUF->line_count < 2) {
		return;
	}

	uint32_t row = E_CTX->cursor_row;
	if (row >= E_BUF->line_count - 1) {
		return;
	}

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	/* Swap with line below */
	buffer_swap_lines(E_BUF, row, row + 1);

	/* Invalidate wrap caches */
	line_invalidate_wrap_cache(&E_BUF->lines[row]);
	line_invalidate_wrap_cache(&E_BUF->lines[row + 1]);

	/* Move cursor down */
	E_CTX->cursor_row++;

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	editor_set_status_message("Line moved down");
}

/*
 * Indent the current line or all lines in selection.
 * Inserts a tab character at the start of each line.
 */
static void editor_indent_lines(void)
{
	uint32_t start_row, end_row;

	if (E_CTX->selection_active && !selection_is_empty()) {
		uint32_t start_col, end_col;
		selection_get_range(&start_row, &start_col, &end_row, &end_col);
	} else {
		start_row = E_CTX->cursor_row;
		end_row = E_CTX->cursor_row;
	}

	if (start_row >= E_BUF->line_count) {
		return;
	}
	if (end_row >= E_BUF->line_count) {
		end_row = E_BUF->line_count - 1;
	}

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

		/* Skip empty lines */
		if (line->cell_count == 0) {
			continue;
		}

		/* Record for undo before inserting */
		undo_record_insert_char(E_BUF, row, 0, '\t');

		/* Insert tab at position 0 */
		line_insert_cell(line, 0, '\t');
		line_set_temperature(line, LINE_TEMPERATURE_HOT);

		/* Recompute line metadata */
		neighbor_compute_line(line);
		syntax_highlight_line(line, E_BUF, row);
		line_invalidate_wrap_cache(line);
	}

	/* Adjust cursor column to account for inserted tab */
	if (E_CTX->cursor_row >= start_row && E_CTX->cursor_row <= end_row) {
		E_CTX->cursor_column++;
	}

	/* Adjust selection anchor if needed */
	if (E_CTX->selection_active) {
		if (E_CTX->selection_anchor_row >= start_row &&
		    E_CTX->selection_anchor_row <= end_row) {
			E_CTX->selection_anchor_column++;
		}
	}

	E_BUF->is_modified = true;
	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

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

	if (E_CTX->selection_active && !selection_is_empty()) {
		uint32_t start_col, end_col;
		selection_get_range(&start_row, &start_col, &end_row, &end_col);
	} else {
		start_row = E_CTX->cursor_row;
		end_row = E_CTX->cursor_row;
	}

	if (start_row >= E_BUF->line_count) {
		return;
	}
	if (end_row >= E_BUF->line_count) {
		end_row = E_BUF->line_count - 1;
	}

	undo_begin_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	uint32_t lines_modified = 0;

	for (uint32_t row = start_row; row <= end_row; row++) {
		struct line *line = &E_BUF->lines[row];
		line_warm(line, E_BUF);

		if (line->cell_count == 0) {
			continue;
		}

		uint32_t chars_to_remove = 0;

		/* Check what's at the start of the line */
		if (line->cells[0].codepoint == '\t') {
			chars_to_remove = 1;
		} else if (line->cells[0].codepoint == ' ') {
			/* Remove up to editor.tab_width spaces */
			while (chars_to_remove < (uint32_t)editor.tab_width &&
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
			undo_record_delete_char(E_BUF, row, 0, line->cells[0].codepoint);
			line_delete_cell(line, 0);
		}

		line_set_temperature(line, LINE_TEMPERATURE_HOT);
		neighbor_compute_line(line);
		syntax_highlight_line(line, E_BUF, row);
		line_invalidate_wrap_cache(line);
		lines_modified++;

		/* Adjust cursor column */
		if (row == E_CTX->cursor_row) {
			if (E_CTX->cursor_column >= chars_to_remove) {
				E_CTX->cursor_column -= chars_to_remove;
			} else {
				E_CTX->cursor_column = 0;
			}
		}

		/* Adjust selection anchor if needed */
		if (E_CTX->selection_active && row == E_CTX->selection_anchor_row) {
			if (E_CTX->selection_anchor_column >= chars_to_remove) {
				E_CTX->selection_anchor_column -= chars_to_remove;
			} else {
				E_CTX->selection_anchor_column = 0;
			}
		}
	}

	if (lines_modified > 0) {
		E_BUF->is_modified = true;
	}

	undo_end_group(E_BUF, E_CTX->cursor_row, E_CTX->cursor_column);

	editor_set_status_message("Outdented %u line%s", lines_modified,
	                          lines_modified != 1 ? "s" : "");
}

/*****************************************************************************
 * Action Execution
 *
 * Execute editor actions by enum value. This decouples the key handling
 * from the action logic, enabling customizable keybindings.
 *****************************************************************************/

/*
 * Execute an editor action.
 * Returns true if the action was handled, false otherwise.
 */
bool
execute_action(enum editor_action action)
{
	switch (action) {
	case ACTION_NONE:
		return false;

	/* File operations */
	case ACTION_QUIT:
		if (E_BUF->is_modified) {
			quit_prompt_enter();
		} else {
			editor_perform_exit();
		}
		return true;

	case ACTION_SAVE:
		editor_save();
		return true;

	case ACTION_SAVE_AS:
		save_as_enter();
		return true;

	case ACTION_OPEN:
		editor_command_open_file();
		return true;

	case ACTION_NEW:
		if (E_BUF->is_modified) {
			editor_set_status_message("Save changes? (y/n)");
		}
		debug_log("ACTION_NEW: clearing current buffer");
		buffer_free(E_BUF);
		buffer_init(E_BUF);
		E_CTX->cursor_row = 0;
		E_CTX->cursor_column = 0;
		E_CTX->row_offset = 0;
		E_CTX->column_offset = 0;
		selection_clear();
		editor_set_status_message("New file");
		return true;

	/* Edit operations */
	case ACTION_UNDO:
		editor_undo();
		return true;

	case ACTION_REDO:
		editor_redo();
		return true;

	case ACTION_CUT:
		editor_cut();
		return true;

	case ACTION_COPY:
		editor_copy();
		return true;

	case ACTION_PASTE:
		editor_paste();
		return true;

	case ACTION_DELETE_LINE:
		editor_delete_line();
		return true;

	case ACTION_DUPLICATE_LINE:
		editor_duplicate_line();
		return true;

	/* Cursor movement */
	case ACTION_MOVE_UP:
		editor_move_cursor(KEY_ARROW_UP);
		return true;

	case ACTION_MOVE_DOWN:
		editor_move_cursor(KEY_ARROW_DOWN);
		return true;

	case ACTION_MOVE_LEFT:
		editor_move_cursor(KEY_ARROW_LEFT);
		return true;

	case ACTION_MOVE_RIGHT:
		editor_move_cursor(KEY_ARROW_RIGHT);
		return true;

	case ACTION_MOVE_WORD_LEFT:
		editor_move_cursor(KEY_CTRL_ARROW_LEFT);
		return true;

	case ACTION_MOVE_WORD_RIGHT:
		editor_move_cursor(KEY_CTRL_ARROW_RIGHT);
		return true;

	case ACTION_MOVE_LINE_START:
		editor_move_cursor(KEY_HOME);
		return true;

	case ACTION_MOVE_LINE_END:
		editor_move_cursor(KEY_END);
		return true;

	case ACTION_MOVE_PAGE_UP:
		editor_move_cursor(KEY_PAGE_UP);
		return true;

	case ACTION_MOVE_PAGE_DOWN:
		editor_move_cursor(KEY_PAGE_DOWN);
		return true;

	case ACTION_MOVE_FILE_START:
		E_CTX->cursor_row = 0;
		E_CTX->cursor_column = 0;
		E_CTX->row_offset = 0;
		E_CTX->column_offset = 0;
		selection_clear();
		return true;

	case ACTION_MOVE_FILE_END:
		if (E_BUF->line_count > 0) {
			E_CTX->cursor_row = E_BUF->line_count - 1;
			struct line *last_line = &E_BUF->lines[E_CTX->cursor_row];
			line_warm(last_line, E_BUF);
			E_CTX->cursor_column = last_line->cell_count;
		}
		selection_clear();
		return true;

	case ACTION_GO_TO_LINE:
		goto_line_enter();
		return true;

	/* Selection */
	case ACTION_SELECT_UP:
		editor_move_cursor(KEY_SHIFT_ARROW_UP);
		return true;

	case ACTION_SELECT_DOWN:
		editor_move_cursor(KEY_SHIFT_ARROW_DOWN);
		return true;

	case ACTION_SELECT_LEFT:
		editor_move_cursor(KEY_SHIFT_ARROW_LEFT);
		return true;

	case ACTION_SELECT_RIGHT:
		editor_move_cursor(KEY_SHIFT_ARROW_RIGHT);
		return true;

	case ACTION_SELECT_WORD_LEFT:
		editor_move_cursor(KEY_CTRL_SHIFT_ARROW_LEFT);
		return true;

	case ACTION_SELECT_WORD_RIGHT:
		editor_move_cursor(KEY_CTRL_SHIFT_ARROW_RIGHT);
		return true;

	case ACTION_SELECT_LINE_START:
		editor_move_cursor(KEY_SHIFT_HOME);
		return true;

	case ACTION_SELECT_LINE_END:
		editor_move_cursor(KEY_SHIFT_END);
		return true;

	case ACTION_SELECT_PAGE_UP:
		editor_move_cursor(KEY_SHIFT_PAGE_UP);
		return true;

	case ACTION_SELECT_PAGE_DOWN:
		editor_move_cursor(KEY_SHIFT_PAGE_DOWN);
		return true;

	case ACTION_SELECT_ALL:
		editor_select_all();
		return true;

	case ACTION_SELECT_WORD:
		editor_select_word(E_CTX->cursor_row, E_CTX->cursor_column);
		return true;

	case ACTION_ADD_CURSOR_NEXT:
		editor_select_next_occurrence();
		return true;

	/* Search */
	case ACTION_FIND:
		search_enter();
		return true;

	case ACTION_FIND_REPLACE:
		replace_enter();
		return true;

	case ACTION_FIND_NEXT:
		search_find_next(true);
		return true;

	case ACTION_FIND_PREV:
		search_find_previous(true);
		return true;

	/* Line operations */
	case ACTION_MOVE_LINE_UP:
		editor_move_line_up();
		return true;

	case ACTION_MOVE_LINE_DOWN:
		editor_move_line_down();
		return true;

	case ACTION_TOGGLE_COMMENT:
		editor_toggle_comment();
		return true;

	case ACTION_JUMP_TO_MATCH:
		editor_jump_to_match();
		return true;

	/* View toggles */
	case ACTION_TOGGLE_LINE_NUMBERS:
		editor.show_line_numbers = !editor.show_line_numbers;
		editor_update_gutter_width();
		editor_set_status_message("Line numbers %s",
		                          editor.show_line_numbers ? "on" : "off");
		return true;

	case ACTION_TOGGLE_WHITESPACE:
		editor.show_whitespace = !editor.show_whitespace;
		editor_set_status_message("Whitespace %s",
		                          editor.show_whitespace ? "visible" : "hidden");
		return true;

	case ACTION_CYCLE_WRAP_MODE:
		editor_cycle_wrap_mode();
		return true;

	case ACTION_CYCLE_WRAP_INDICATOR:
		editor_cycle_wrap_indicator();
		return true;

	case ACTION_CYCLE_COLOR_COLUMN:
		if (editor.color_column == 0) {
			editor.color_column = 80;
		} else if (editor.color_column == 80) {
			editor.color_column = 120;
		} else {
			editor.color_column = 0;
		}
		if (editor.color_column > 0) {
			editor_set_status_message("Column %u (%s)",
			                          editor.color_column,
			                          color_column_style_name(editor.color_column_style));
		} else {
			editor_set_status_message("Color column off");
		}
		return true;

	case ACTION_TOGGLE_HYBRID_MODE:
		if (syntax_is_markdown_file(E_BUF->filename)) {
			E_CTX->hybrid_mode = !E_CTX->hybrid_mode;
			editor_set_status_message("Markdown %s mode",
			                          E_CTX->hybrid_mode ? "hybrid" : "raw");
			for (uint32_t i = E_CTX->row_offset;
			     i < E_CTX->row_offset + editor.screen_rows &&
			     i < E_BUF->line_count; i++) {
				struct line *line = &E_BUF->lines[i];
				if (line_get_temperature(line) != LINE_TEMPERATURE_COLD) {
					syntax_highlight_line(line, E_BUF, i);
				}
			}
		} else {
			editor_set_status_message("Hybrid mode only available for Markdown files");
		}
		return true;

	/* Dialogs */
	case ACTION_HELP:
		editor_toggle_help();
		return true;

	case ACTION_THEME_PICKER:
		editor_command_theme_picker();
		return true;

	case ACTION_CHECK_UPDATES:
		editor_check_for_updates();
		return true;
	case ACTION_FORMAT_TABLES:
		if (syntax_is_markdown_file(E_BUF->filename)) {
			int count = tables_reformat_all(E_BUF);
			if (count > 0) {
				E_BUF->is_modified = true;
				editor_set_status_message("%d table(s) formatted", count);
			} else {
				editor_set_status_message("No tables to format");
			}
		} else {
			editor_set_status_message("Table formatting only available in markdown files");
		}
		return true;
	/* Buffer/Context Switching */
	case ACTION_CONTEXT_PREV:
		editor_context_prev();
		return true;
	case ACTION_CONTEXT_NEXT:
		editor_context_next();
		return true;
	case ACTION_CONTEXT_CLOSE:
		if (editor.context_count == 1) {
			/* Last tab - use quit behavior */
			if (E_BUF->is_modified) {
				quit_prompt_enter();
			} else {
				editor_perform_exit();
			}
		} else {
			/* Multiple tabs - prompt if modified */
			if (E_BUF->is_modified) {
				close_prompt_enter();
			} else {
				editor_context_close(editor.active_context);
			}
		}
		return true;
	case ACTION_NEW_TAB: {
		int idx = editor_context_new();
		if (idx >= 0) {
			editor_context_switch((uint32_t)idx);
			editor_set_status_message("New buffer");
		}
		return true;
	}
	case ACTION_OPEN_TAB: {
		int idx = editor_context_new();
		if (idx >= 0) {
			editor_context_switch((uint32_t)idx);
			editor_command_open_file();
		}
		return true;
	}

	/* Special */
	case ACTION_ESCAPE:
		if (E_CTX->cursor_count > 0) {
			multicursor_exit();
		} else {
			selection_clear();
		}
		return true;

	case ACTION_INSERT_TAB:
		if (E_CTX->selection_active && !selection_is_empty()) {
			editor_indent_lines();
		} else if (!editor_table_next_cell()) {
			editor_insert_character('\t');
		}
		return true;

	case ACTION_INSERT_BACKTAB:
		if (!editor_table_prev_cell()) {
			editor_outdent_lines();
		}
		return true;

	case ACTION_INSERT_NEWLINE:
		editor_insert_newline();
		return true;

	case ACTION_BACKSPACE:
		multicursor_backspace();
		return true;

	case ACTION_DELETE:
		editor_delete_character();
		return true;

	case ACTION_COUNT:
		return false;
	}

	return false;
}

void editor_process_keypress(void)
{
	int key;

	/* Use timeout-based input during drag selection for auto-scroll */
	if (editor.drag_scroll.active) {
		key = input_read_key_timeout(50);  /* 50ms for responsive scrolling */
		if (key == 0) {
			/* Timeout - trigger auto-scroll */
			editor_drag_scroll_tick();
			return;
		}
	} else {
		key = input_read_key();
	}

	if (key == -1)
		return;

	/* Handle terminal resize directly */
	if (key == KEY_RESIZE) {
		editor_update_screen_size();
		return;
	}

	/* Handle mouse events directly (processed in input_read_key) */
	if (key == KEY_MOUSE_EVENT)
		return;
	/* Leader key (Ctrl+Space) enters command mode */
	if (key == 0) {
		command_mode_enter();
		return;
	}
	/* Handle command mode input */
	if (command_mode_handle_key(key))
		return;

	/* Handle mode-specific input */
	if (save_as_handle_key(key))
		return;
	if (search_handle_key(key))
		return;
	if (goto_handle_key(key))
		return;
	if (quit_prompt_handle_key(key))
		return;
	if (close_prompt_handle_key(key))
		return;
	if (reload_prompt_handle_key(key))
		return;

	/* Look up action for this key */
	enum editor_action action = keybinding_lookup(key);
	if (execute_action(action))
		return;

	/* Ctrl-L: refresh (ignore) */
	if (key == CONTROL_KEY('l'))
		return;

	/* Insert printable ASCII (32-126) and Unicode codepoints (>= 128) */
	if ((key >= 32 && key < 127) || key >= 128)
		multicursor_insert_character((uint32_t)key);
}
