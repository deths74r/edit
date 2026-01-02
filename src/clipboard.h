/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * clipboard.h - System clipboard integration for edit
 *
 * Provides copy, cut, and paste with system clipboard.
 */

#ifndef EDIT_CLIPBOARD_H
#define EDIT_CLIPBOARD_H

#include "types.h"

/*****************************************************************************
 * Clipboard Operations
 *****************************************************************************/

/*
 * Copy text to system clipboard.
 * Falls back to internal buffer if no clipboard tool is available.
 * Returns true on success.
 */
bool clipboard_copy(const char *text, size_t length);

/*
 * Paste from system clipboard.
 * Returns a newly allocated string that the caller must free,
 * or NULL on failure. Sets *out_length to the length of the
 * returned string (excluding null terminator).
 */
char *clipboard_paste(size_t *out_length);

/*****************************************************************************
 * Internal Clipboard (fallback)
 *****************************************************************************/

/*
 * Free internal clipboard resources.
 * Call during program shutdown.
 */
void clipboard_cleanup(void);

/*****************************************************************************
 * Editor Clipboard Operations
 *****************************************************************************/

/*
 * Copy current selection to clipboard.
 */
void editor_copy(void);

/*
 * Cut current selection to clipboard.
 */
void editor_cut(void);

/*
 * Paste from clipboard at cursor.
 * If there's a selection, replaces it with pasted content.
 */
void editor_paste(void);

#endif /* EDIT_CLIPBOARD_H */
