/*
 * error.h - Linux kernel-style error handling infrastructure for edit
 *
 * This header provides:
 * - Custom error codes extending errno
 * - ERR_PTR system for encoding errors in pointers
 * - Branch prediction hints
 * - Must-check annotations
 * - WARN/BUG macros for invariant checking
 * - Checked allocation wrappers
 * - Error propagation helpers
 * - Logging infrastructure
 */

#ifndef EDIT_ERROR_H
#define EDIT_ERROR_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************
 * Custom Error Codes
 *
 * These extend errno values, starting at EEDIT_BASE to avoid conflicts.
 * All custom errors should be returned as negative values (like -EEDIT_TOOBIG).
 *****************************************************************************/

#define EEDIT_BASE		1000

#define EEDIT_TOOBIG		(EEDIT_BASE + 1)   /* File too large */
#define EEDIT_BINARY		(EEDIT_BASE + 2)   /* Binary file detected */
#define EEDIT_ENCODING		(EEDIT_BASE + 3)   /* Invalid UTF-8 encoding */
#define EEDIT_NOTTY		(EEDIT_BASE + 4)   /* stdin not a terminal */
#define EEDIT_TERMSIZE		(EEDIT_BASE + 5)   /* Cannot determine terminal size */
#define EEDIT_TERMRAW		(EEDIT_BASE + 6)   /* Cannot set raw mode */
#define EEDIT_CORRUPT		(EEDIT_BASE + 7)   /* Internal data corruption */
#define EEDIT_INVARIANT		(EEDIT_BASE + 8)   /* Invariant violation */
#define EEDIT_BOUNDS		(EEDIT_BASE + 9)   /* Index out of bounds */
#define EEDIT_READONLY		(EEDIT_BASE + 10)  /* Buffer is read-only */
#define EEDIT_NOUNDO		(EEDIT_BASE + 11)  /* Nothing to undo */
#define EEDIT_NOREDO		(EEDIT_BASE + 12)  /* Nothing to redo */
#define EEDIT_NOCLIP		(EEDIT_BASE + 13)  /* Clipboard empty */
#define EEDIT_THREAD		(EEDIT_BASE + 14)  /* Thread creation failed */
#define EEDIT_MUTEX		(EEDIT_BASE + 15)  /* Mutex operation failed */
#define EEDIT_CANCELLED		(EEDIT_BASE + 16)  /* Task was cancelled */
#define EEDIT_QUEUEFULL		(EEDIT_BASE + 17)  /* Task queue is full */

#define EEDIT_MAX		(EEDIT_BASE + 17)

/*****************************************************************************
 * ERR_PTR System
 *
 * Encode error codes in pointer values. This allows functions that return
 * pointers to also return error codes without using out-parameters.
 *
 * The top MAX_ERRNO values of the address space are reserved for errors.
 * On most systems, these addresses are in kernel space and never valid
 * for user pointers.
 *****************************************************************************/

#define MAX_ERRNO	4095

/*
 * Convert an error code to a pointer value.
 * Usage: return ERR_PTR(-ENOMEM);
 */
static inline void *ERR_PTR(long error)
{
	return (void *)(intptr_t)error;
}

/*
 * Extract the error code from an error pointer.
 * Usage: int err = PTR_ERR(ptr);
 */
static inline long PTR_ERR(const void *ptr)
{
	return (long)(intptr_t)ptr;
}

/*
 * Check if a pointer is actually an encoded error.
 * Error pointers are in the range [-MAX_ERRNO, -1].
 */
static inline bool IS_ERR(const void *ptr)
{
	return (uintptr_t)ptr >= (uintptr_t)-MAX_ERRNO;
}

/*
 * Check if a pointer is NULL or an encoded error.
 */
static inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return ptr == NULL || IS_ERR(ptr);
}

/*****************************************************************************
 * Branch Prediction Hints
 *
 * Help the compiler optimize branch prediction for common cases.
 *****************************************************************************/

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/*****************************************************************************
 * Must-Check Annotation
 *
 * Mark functions whose return value must not be ignored.
 *****************************************************************************/

#define __must_check	__attribute__((warn_unused_result))

/*****************************************************************************
 * WARN/BUG Macros
 *
 * For runtime invariant checking and fatal error handling.
 *
 * WARN variants log but continue execution.
 * BUG variants attempt emergency save and abort.
 *****************************************************************************/

/* External functions defined in edit.c - called by BUG macros */
extern void emergency_save(void);
extern void terminal_disable_raw_mode(void);

/*
 * WARN - Log a warning if condition is true, with formatted message.
 */
#define WARN(condition, ...) do {					\
	if (unlikely(condition)) {					\
		fprintf(stderr, "WARNING: %s:%d: ", __FILE__, __LINE__);	\
		fprintf(stderr, __VA_ARGS__);				\
		fprintf(stderr, "\n");					\
	}								\
} while (0)

/*
 * WARN_ON - Log a warning if condition is true.
 */
#define WARN_ON(condition) do {					\
	if (unlikely(condition)) {					\
		fprintf(stderr, "WARNING: %s:%d: %s\n",		\
			__FILE__, __LINE__, #condition);		\
	}								\
} while (0)

/*
 * WARN_ON_ONCE - Like WARN_ON but only triggers once per call site.
 * Note: This version doesn't return a value (use WARN_ON if needed).
 */
#define WARN_ON_ONCE(condition) do {					\
	static int __warned = 0;					\
	if (unlikely(condition) && !__warned) {				\
		__warned = 1;						\
		fprintf(stderr, "WARNING: %s:%d: %s\n",			\
			__FILE__, __LINE__, #condition);		\
	}								\
} while (0)

/*
 * BUG - Unconditional fatal error. Attempts emergency save and aborts.
 */
#define BUG() do {							\
	fprintf(stderr, "BUG: %s:%d: fatal error\n",			\
		__FILE__, __LINE__);					\
	terminal_disable_raw_mode();					\
	emergency_save();						\
	abort();							\
} while (0)

/*
 * BUG_ON - Fatal error if condition is true.
 */
#define BUG_ON(condition) do {						\
	if (unlikely(condition)) {					\
		fprintf(stderr, "BUG: %s:%d: %s\n",			\
			__FILE__, __LINE__, #condition);		\
		terminal_disable_raw_mode();				\
		emergency_save();					\
		abort();						\
	}								\
} while (0)

/*****************************************************************************
 * Checked Allocation Wrappers
 *
 * These return ERR_PTR(-ENOMEM) on allocation failure instead of NULL,
 * making them compatible with the ERR_PTR error handling pattern.
 *****************************************************************************/

static inline void * __must_check edit_malloc(size_t size)
{
	void *ptr = malloc(size);
	if (unlikely(ptr == NULL && size != 0))
		return ERR_PTR(-ENOMEM);
	return ptr;
}

static inline void * __must_check edit_calloc(size_t nmemb, size_t size)
{
	void *ptr = calloc(nmemb, size);
	if (unlikely(ptr == NULL && nmemb != 0 && size != 0))
		return ERR_PTR(-ENOMEM);
	return ptr;
}

static inline void * __must_check edit_realloc(void *ptr, size_t size)
{
	void *new_ptr = realloc(ptr, size);
	if (unlikely(new_ptr == NULL && size != 0))
		return ERR_PTR(-ENOMEM);
	return new_ptr;
}

static inline char * __must_check edit_strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *dup = malloc(len);
	if (unlikely(dup == NULL))
		return ERR_PTR(-ENOMEM);
	memcpy(dup, s, len);
	return dup;
}

/*****************************************************************************
 * Error Propagation Helpers
 *
 * Macros to simplify early return on error conditions.
 *****************************************************************************/

/*
 * PROPAGATE - Return immediately if expression is non-zero.
 * For functions returning int error codes.
 */
#define PROPAGATE(expr) do {						\
	int __err = (expr);						\
	if (unlikely(__err))						\
		return __err;						\
} while (0)

/*
 * PROPAGATE_PTR - Return immediately if expression is an error pointer.
 * For functions returning pointers.
 */
#define PROPAGATE_PTR(expr) do {					\
	void *__ptr = (expr);						\
	if (unlikely(IS_ERR(__ptr)))					\
		return __ptr;						\
} while (0)

/*
 * GET_PTR_OR_RETURN - Assign pointer to variable, or return error as int.
 * Usage: GET_PTR_OR_RETURN(data, some_alloc_function());
 */
#define GET_PTR_OR_RETURN(var, expr) do {				\
	void *__ptr = (expr);						\
	if (unlikely(IS_ERR(__ptr)))					\
		return (int)PTR_ERR(__ptr);				\
	(var) = __ptr;							\
} while (0)

/*
 * GET_PTR_OR_RETURN_PTR - Assign pointer to variable, or return error pointer.
 * Usage: GET_PTR_OR_RETURN_PTR(data, some_alloc_function());
 */
#define GET_PTR_OR_RETURN_PTR(var, expr) do {				\
	void *__ptr = (expr);						\
	if (unlikely(IS_ERR(__ptr)))					\
		return __ptr;						\
	(var) = __ptr;							\
} while (0)

/*****************************************************************************
 * Logging Infrastructure
 *
 * Simple logging with levels. Messages are written to stderr.
 * LOG_DEBUG only compiles in when DEBUG is defined.
 *****************************************************************************/

#define LOG_ERR		0
#define LOG_WARN	1
#define LOG_INFO	2
#define LOG_DEBUG	3

#ifndef LOG_LEVEL
#define LOG_LEVEL	LOG_WARN
#endif

#define edit_log(level, ...) do {					\
	if ((level) <= LOG_LEVEL) {					\
		const char *__level_str[] = {"ERR", "WARN", "INFO", "DBG"}; \
		fprintf(stderr, "[%s] %s:%d: ",			\
			__level_str[level], __FILE__, __LINE__);	\
		fprintf(stderr, __VA_ARGS__);				\
		fprintf(stderr, "\n");					\
	}								\
} while (0)

#define log_err(...)	edit_log(LOG_ERR, __VA_ARGS__)
#define log_warn(...)	edit_log(LOG_WARN, __VA_ARGS__)
#define log_info(...)	edit_log(LOG_INFO, __VA_ARGS__)

#ifdef DEBUG
#define log_debug(...)	edit_log(LOG_DEBUG, __VA_ARGS__)
#else
#define log_debug(...)	do { } while (0)
#endif

/*****************************************************************************
 * Error String Function
 *
 * Convert error codes (both standard and custom) to human-readable strings.
 *****************************************************************************/

const char *edit_strerror(int err);

#endif /* EDIT_ERROR_H */
