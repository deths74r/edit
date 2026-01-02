/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * worker.h - Background worker thread for edit
 *
 * Provides task queue, result queue, and worker thread
 * for background operations like search and line warming.
 */

#ifndef EDIT_WORKER_H
#define EDIT_WORKER_H

#include "types.h"

/*****************************************************************************
 * Worker Lifecycle
 *****************************************************************************/

/*
 * Initialize the worker thread.
 * Returns 0 on success, negative error code on failure.
 */
int worker_init(void);

/*
 * Shutdown the worker thread.
 */
void worker_shutdown(void);

/*
 * Check if worker is initialized.
 */
bool worker_is_initialized(void);

/*****************************************************************************
 * Task Management
 *****************************************************************************/

/*
 * Generate a unique task ID.
 */
uint64_t task_generate_id(void);

/*
 * Submit a task to the worker.
 * Returns 0 on success, negative error code on failure.
 */
int task_queue_push(struct task *task);

/*
 * Cancel a pending or running task.
 * Returns true if task was found and cancelled.
 */
bool task_cancel(uint64_t task_id);

/*
 * Cancel all tasks of a specific type.
 */
void task_cancel_all_of_type(enum task_type type);

/*
 * Check if a task has been cancelled.
 */
static inline bool task_is_cancelled(struct task *task)
{
	return atomic_load_explicit(&task->cancelled, memory_order_relaxed);
}

/*****************************************************************************
 * Result Processing
 *****************************************************************************/

/*
 * Process pending results from worker.
 * Call periodically from main loop.
 */
void worker_process_results(void);

/*
 * Check if there are pending results.
 */
bool worker_has_pending_results(void);

/*
 * Pop a result from the queue (called by main thread).
 * Non-blocking: returns false immediately if queue is empty.
 */
bool result_queue_pop(struct task_result *out);

/*****************************************************************************
 * Task Processing Callbacks
 *
 * These are implemented in edit.c (or future modules) and called
 * by the worker thread to process specific task types.
 *****************************************************************************/

/*
 * Process warm lines task (decode lines from mmap).
 */
int worker_process_warm_lines(struct task *task, struct task_result *result);

/*
 * Process search task (find matches in buffer).
 */
int worker_process_search(struct task *task, struct task_result *result);

/*
 * Process replace all task (find all replacements).
 */
int worker_process_replace_all(struct task *task, struct task_result *result);

/*
 * Process autosave task (write buffer snapshot to swap file).
 */
int worker_process_autosave(struct task *task, struct task_result *result);

#endif /* EDIT_WORKER_H */
