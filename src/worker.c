/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * worker.c - Background worker thread for edit
 *
 * Provides task queue, result queue, and worker thread
 * for background operations like search and line warming.
 */

#include "edit.h"

/*****************************************************************************
 * Global State
 *****************************************************************************/

static struct worker_state worker = {0};

/*****************************************************************************
 * Task ID Generation
 *****************************************************************************/

uint64_t task_generate_id(void)
{
	return atomic_fetch_add(&worker.next_task_id, 1);
}

/*****************************************************************************
 * Task Queue Operations
 *****************************************************************************/

int task_queue_push(struct task *task)
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
			ts.tv_sec += timeout_ms / MSEC_PER_SEC;
			ts.tv_nsec += (timeout_ms % MSEC_PER_SEC) * NSEC_PER_MSEC;
			if (ts.tv_nsec >= NSEC_PER_SEC) {
				ts.tv_sec++;
				ts.tv_nsec -= NSEC_PER_SEC;
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

bool task_cancel(uint64_t task_id)
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

void task_cancel_all_of_type(enum task_type type)
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

/*****************************************************************************
 * Result Queue Operations
 *****************************************************************************/

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
bool result_queue_pop(struct task_result *out)
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

bool worker_has_pending_results(void)
{
	pthread_mutex_lock(&worker.result_mutex);
	bool has_results = (worker.result_count > 0);
	pthread_mutex_unlock(&worker.result_mutex);
	return has_results;
}

/*****************************************************************************
 * Worker Thread
 *****************************************************************************/

/*
 * Worker thread main function.
 */
static void *worker_thread_main(void *arg)
{
	(void)arg;
	log_info("Worker thread started");

	while (1) {
		struct task task;
		/* Wait for task with 100ms timeout */
		int err = task_queue_pop(&task, 100);

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

/*****************************************************************************
 * Worker Lifecycle
 *****************************************************************************/

bool worker_is_initialized(void)
{
	return worker.initialized;
}

int worker_init(void)
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

void worker_shutdown(void)
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
