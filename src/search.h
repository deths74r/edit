/*
 * search.h - Search and replace for edit
 *
 * Provides incremental search, regex support,
 * and background search for large files.
 */

#ifndef EDIT_SEARCH_H
#define EDIT_SEARCH_H

#include "types.h"

/*****************************************************************************
 * Search System Lifecycle
 *****************************************************************************/

/*
 * Initialize the search system.
 * Call once at startup after worker_init().
 * Returns 0 on success, negative error code on failure.
 */
int search_init(void);

/*
 * Cleanup search resources.
 * Call before worker_shutdown().
 */
void search_cleanup(void);

/*****************************************************************************
 * Synchronous Search
 *****************************************************************************/

/*
 * Find next match from current position.
 * If wrap is true, wraps around to start of file.
 * Returns true if match found.
 */
bool search_find_next(bool wrap);

/*
 * Find previous match from current position.
 * If wrap is true, wraps around to end of file.
 * Returns true if match found.
 */
bool search_find_previous(bool wrap);

/*
 * Center the view on the current match.
 */
void search_center_on_match(void);

/*
 * Execute search with current query in search state.
 */
void search_execute(void);

/*
 * Replace all occurrences (synchronous version for small files).
 */
void search_replace_all_sync(void);

/*****************************************************************************
 * Async Search
 *****************************************************************************/

/*
 * Check if async search should be used for current buffer.
 */
bool search_should_use_async(void);

/*
 * Start async search.
 */
void search_async_start(const char *pattern, bool use_regex,
                        bool case_sensitive, bool whole_word);

/*
 * Cancel async search.
 */
void search_async_cancel(void);

/*
 * Get async search progress.
 * Returns match count.
 */
uint32_t search_async_get_progress(bool *complete, uint32_t *rows_searched,
                                   uint32_t *total_rows);

/*
 * Navigate to next async match.
 * Returns true if navigated successfully.
 */
bool search_async_next_match(void);

/*
 * Navigate to previous async match.
 * Returns true if navigated successfully.
 */
bool search_async_prev_match(void);

/*
 * Check if a cell is in a search match.
 * Returns: 0 = no match, 1 = match, 2 = current match
 */
int search_async_get_match_state(uint32_t row, uint32_t col);

/*****************************************************************************
 * Async Replace
 *****************************************************************************/

/*
 * Start async replace all.
 */
void search_async_replace_start(const char *pattern, const char *replacement,
                                bool use_regex, bool case_sensitive,
                                bool whole_word);

/*
 * Cancel async replace.
 */
void search_async_replace_cancel(void);

/*
 * Get async replace progress.
 * Returns replacement count.
 */
uint32_t search_async_replace_get_progress(bool *search_complete,
                                           bool *apply_complete,
                                           uint32_t *total);

/*
 * Check if async replace should be used.
 */
bool search_should_use_async_replace(void);

/*
 * Apply pending async replacements.
 */
void search_async_replace_apply(void);

/*****************************************************************************
 * Search Results (Worker Thread Interface)
 *****************************************************************************/

/*
 * Add a match to results (call from worker thread).
 * Returns 0 on success, -ENOMEM if at capacity.
 */
int search_results_add_match(uint32_t row, uint32_t start_col, uint32_t end_col);

/*
 * Update search progress (call from worker thread).
 */
void search_results_update_progress(uint32_t rows_searched, uint32_t total_rows);

/*
 * Mark search as complete (call from worker thread).
 */
void search_results_mark_complete(void);

/*****************************************************************************
 * Replace Results (Worker Thread Interface)
 *****************************************************************************/

/*
 * Add a replacement to pending list (call from worker thread).
 * Returns 0 on success, negative error code on failure.
 */
int replace_results_add(uint32_t row, uint32_t start_col, uint32_t end_col,
                        const char *replacement, uint32_t replacement_len);

/*
 * Update replace progress (call from worker thread).
 */
void replace_results_update_progress(uint32_t rows_searched, uint32_t total_rows);

/*
 * Mark replace search phase as complete (call from worker thread).
 */
void replace_results_mark_complete(void);

/*****************************************************************************
 * Async State Accessors (for worker_process_results)
 *****************************************************************************/

/*
 * Clear async search results.
 */
void search_async_clear_results(void);

/*
 * Check if async search is active.
 */
bool search_async_is_active(void);

/*
 * Get async search task ID.
 */
uint64_t search_async_get_task_id(void);

/*
 * Mark async search as inactive.
 */
void search_async_set_inactive(void);

/*
 * Get current match index.
 */
int32_t search_async_get_current_match_index(void);

/*
 * Check if async replace is active.
 */
bool search_async_replace_is_active(void);

/*
 * Get async replace task ID.
 */
uint64_t search_async_replace_get_task_id(void);

/*
 * Mark async replace as inactive.
 */
void search_async_replace_set_inactive(void);

#endif /* EDIT_SEARCH_H */
