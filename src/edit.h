/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * edit.h - Master header for edit text editor
 *
 * Include this header to get access to all editor functionality.
 * Individual module headers can also be included separately.
 */

#ifndef EDIT_H
#define EDIT_H

/* Standard library headers */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <regex.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>

/* Edit headers */
#include "types.h"
#include "error.h"

#include "terminal.h"
#include "theme.h"
#include "buffer.h"
#include "syntax.h"
#include "undo.h"
#include "input.h"
#include "render.h"
#include "worker.h"
#include "search.h"
#include "autosave.h"
#include "dialog.h"
#include "clipboard.h"
#include "editor.h"
#include "update.h"

/*****************************************************************************
 * Security Utilities
 *****************************************************************************/

/*
 * Safely get HOME environment variable with validation.
 * Returns HOME if it's a valid absolute path, NULL otherwise.
 * This prevents attacks via malicious HOME values.
 */
static inline const char *safe_get_home(void)
{
	const char *home = getenv("HOME");
	if (home == NULL)
		return NULL;

	/* Must be an absolute path */
	if (home[0] != '/')
		return NULL;

	/* Must not be too long (leave room for subdirs) */
	if (strlen(home) > PATH_MAX - 64)
		return NULL;

	/* Must not contain dangerous sequences */
	if (strstr(home, "..") != NULL)
		return NULL;

	return home;
}

#endif /* EDIT_H */
