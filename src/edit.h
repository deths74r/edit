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

/*
 * Module headers will be added as modules are extracted:
 * (theme.h already included above)
 * (buffer.h already included above)
 * (syntax.h already included above)
 * (undo.h already included above)
 * (input.h already included above)
 * (render.h already included above)
 * (worker.h already included above)
 * (search.h already included above)
 * (autosave.h already included above)
 * (dialog.h already included above)
 * #include "clipboard.h"
 * #include "editor.h"
 */

#endif /* EDIT_H */
