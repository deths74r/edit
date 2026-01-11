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
