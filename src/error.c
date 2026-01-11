/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * error.c - Error handling implementation for edit
 *
 * This file provides:
 * - edit_strerror() for converting error codes to strings
 */

#include "error.h"

#include <stdarg.h>
#include <time.h>
#include <unistd.h>

/*****************************************************************************
 * Error Code to String Conversion
 *
 * Returns human-readable descriptions for both standard errno values
 * and custom EEDIT_* error codes.
 *****************************************************************************/

/*
 * edit_strerror - Convert an error code to a descriptive string.
 * @err: The error code (can be positive or negative)
 *
 * Handles:
 * - Standard errno values (delegated to system strerror)
 * - Custom EEDIT_* error codes
 * - Positive values (caller forgot to negate)
 *
 * Returns: A string describing the error. The string is either a static
 *          constant or comes from the system strerror(), so it should
 *          not be modified or freed.
 */
const char *edit_strerror(int err)
{
	/* Handle positive error codes (caller may have forgotten to negate) */
	if (err > 0)
		err = -err;

	/* Handle custom EEDIT_* error codes */
	switch (-err) {
	case EEDIT_TOOBIG:
		return "File too large";
	case EEDIT_BINARY:
		return "Binary file detected";
	case EEDIT_ENCODING:
		return "Invalid UTF-8 encoding";
	case EEDIT_NOTTY:
		return "Standard input is not a terminal";
	case EEDIT_TERMSIZE:
		return "Cannot determine terminal size";
	case EEDIT_TERMRAW:
		return "Cannot set terminal raw mode";
	case EEDIT_CORRUPT:
		return "Internal data corruption detected";
	case EEDIT_INVARIANT:
		return "Internal invariant violation";
	case EEDIT_BOUNDS:
		return "Index out of bounds";
	case EEDIT_READONLY:
		return "Buffer is read-only";
	case EEDIT_NOUNDO:
		return "Nothing to undo";
	case EEDIT_NOREDO:
		return "Nothing to redo";
	case EEDIT_NOCLIP:
		return "Clipboard is empty";
	case EEDIT_THREAD:
		return "Thread creation failed";
	case EEDIT_MUTEX:
		return "Mutex operation failed";
	case EEDIT_CANCELLED:
		return "Operation cancelled";
	case EEDIT_QUEUEFULL:
		return "Task queue is full";
	}

	/* Handle standard errno values */
	if (-err > 0 && -err < EEDIT_BASE)
		return strerror(-err);

	/* Unknown error code */
	return "Unknown error";
}

/*****************************************************************************
 * Debug Log File
 *
 * File-based debug logging for crash debugging. Writes to debug.log in the
 * current directory. Each entry is timestamped and flushed immediately.
 *****************************************************************************/
FILE *debug_log_file = NULL;
/*
 * debug_log_init - Initialize debug logging to file.
 *
 * Opens debug.log in append mode in the current directory.
 * If the file cannot be opened, logging is silently disabled.
 */
void debug_log_init(void)
{
	debug_log_file = fopen("debug.log", "a");
	if (debug_log_file) {
		debug_log("=== edit started (pid %d) ===", getpid());
	}
}
/*
 * debug_log_close - Close debug log file.
 *
 * Writes a closing message and closes the file handle.
 */
void debug_log_close(void)
{
	if (debug_log_file) {
		debug_log("=== edit exiting ===");
		fclose(debug_log_file);
		debug_log_file = NULL;
	}
}
/*
 * debug_log - Write timestamped message to debug log.
 * @fmt: printf-style format string
 * @...: format arguments
 *
 * Each message is prefixed with HH:MM:SS timestamp and flushed
 * immediately to ensure data is captured before potential crashes.
 */
void debug_log(const char *fmt, ...)
{
	if (!debug_log_file)
		return;
	/* Timestamp */
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	fprintf(debug_log_file, "[%02d:%02d:%02d] ",
	        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
	/* Message */
	va_list args;
	va_start(args, fmt);
	vfprintf(debug_log_file, fmt, args);
	va_end(args);
	fprintf(debug_log_file, "\n");
	fflush(debug_log_file);  /* Ensure written before potential crash */
}
