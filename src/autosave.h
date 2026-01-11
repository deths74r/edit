/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * autosave.h - Auto-save and crash recovery for edit
 *
 * Provides periodic automatic saves to swap files and
 * recovery from previous crashed sessions.
 */

#ifndef EDIT_AUTOSAVE_H
#define EDIT_AUTOSAVE_H

#include "types.h"

/*****************************************************************************
 * Autosave Configuration
 *****************************************************************************/

/*
 * Enable or disable autosave.
 */
void autosave_set_enabled(bool enabled);

/*
 * Check if autosave is enabled.
 */
bool autosave_is_enabled(void);

/*****************************************************************************
 * Autosave Operations
 *****************************************************************************/

/*
 * Check if autosave should run and trigger if needed.
 * Call periodically from main loop.
 */
void autosave_check(void);

/*
 * Mark buffer as modified (for autosave tracking).
 */
void autosave_mark_modified(void);

/*
 * Update swap file path after filename change.
 */
void autosave_update_path(void);

/*
 * Remove swap file (after clean save or exit).
 */
void autosave_remove_swap(void);

/*
 * Reset autosave state after successful save.
 */
void autosave_on_save(void);

/*
 * Set whether a swap file exists (for recovery tracking).
 */
void autosave_set_swap_exists(bool exists);

/*****************************************************************************
 * Crash Recovery
 *****************************************************************************/

/*
 * Check if a swap file exists for the given filename.
 * Returns the swap file path if found, NULL otherwise.
 */
const char *autosave_check_recovery(const char *filename);

/*
 * Prompt user for recovery decision.
 * Returns true if user chose to recover.
 */
bool autosave_prompt_recovery(const char *filename, const char *swap_path);

/*****************************************************************************
 * Buffer Snapshot (for background saving)
 *****************************************************************************/

/*
 * Create a snapshot of current buffer for background saving.
 * Returns NULL on failure.
 */
struct buffer_snapshot *buffer_snapshot_create(void);

/*
 * Free a buffer snapshot.
 */
void buffer_snapshot_free(struct buffer_snapshot *snapshot);

/*****************************************************************************
 * Worker Thread Interface
 *****************************************************************************/

/*
 * Process autosave task on worker thread.
 * Called from worker_thread_main().
 */
int worker_process_autosave(struct task *task, struct task_result *result);

/*
 * Handle autosave task result.
 * Called from worker_process_results().
 */
void autosave_handle_result(struct task_result *result);

#endif /* EDIT_AUTOSAVE_H */
