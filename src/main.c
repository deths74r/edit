/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * main.c - Entry point for edit text editor
 *
 * Provides program initialization, signal handling setup,
 * and the main event loop.
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "edit.h"
#include "keybindings.h"

/*****************************************************************************
 * External Dependencies
 *****************************************************************************/

/* Global editor state from edit.c */
extern struct editor_state editor;

/* Functions from edit.c */
extern void fatal_signal_handler(int sig);
extern int file_open(struct buffer *buffer, const char *filename);
extern char *stdin_read_all(size_t *out_size);
extern void editor_handle_mouse(struct mouse_input *mouse);
extern void editor_process_keypress(void);

/*****************************************************************************
 * Entry Point
 *****************************************************************************/

/*
 * Program entry point. Initializes the terminal in raw mode, sets up
 * the window resize signal handler, and opens the file specified on
 * the command line (or starts with an empty buffer). Enters the main
 * loop which alternates between refreshing the screen and processing
 * keypresses.
 */
int main(int argument_count, char *argument_values[]) {
  /* Detect if stdin is a pipe (not a terminal) */
  bool stdin_is_pipe = !isatty(STDIN_FILENO);
  char *stdin_content = NULL;
  size_t stdin_size = 0;

  if (stdin_is_pipe) {
    /*
     * Only read stdin content if no file argument given.
     * When a file is specified, stdin content is discarded.
     */
    if (argument_count < 2) {
      stdin_content = stdin_read_all(&stdin_size);
      if (stdin_content == NULL && stdin_size > 0) {
        fprintf(stderr, "edit: failed to read stdin\n");
        return 1;
      }
    }

    /* Reopen stdin from /dev/tty for interactive input */
    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd < 0) {
      fprintf(stderr, "edit: cannot open /dev/tty: %s\n", strerror(errno));
      free(stdin_content);
      return 1;
    }
    dup2(tty_fd, STDIN_FILENO);
    close(tty_fd);
  }

  int ret = terminal_enable_raw_mode();
  if (ret) {
    fprintf(stderr, "edit: %s\n", edit_strerror(ret));
    free(stdin_content);
    return 1;
  }

  terminal_enable_mouse();
  input_set_mouse_handler(editor_handle_mouse);

  /* Set up signal handler for window resize */
  struct sigaction signal_action;
  signal_action.sa_handler = terminal_handle_resize;
  sigemptyset(&signal_action.sa_mask);
  signal_action.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &signal_action, NULL);

  /* Set up fatal signal handlers for crash recovery */
  signal(SIGSEGV, fatal_signal_handler);
  signal(SIGABRT, fatal_signal_handler);
  signal(SIGBUS, fatal_signal_handler);
  signal(SIGFPE, fatal_signal_handler);
  signal(SIGILL, fatal_signal_handler);

  editor_init();
  keybinding_init();
  editor_update_screen_size();

  /* Load content from stdin pipe if available */
  if (stdin_content != NULL) {
    int ret =
        buffer_load_from_memory(&editor.buffer, stdin_content, stdin_size);
    free(stdin_content);
    stdin_content = NULL;

    if (ret != 0) {
      editor_set_status_message("Failed to load stdin: %s", edit_strerror(ret));
    } else {
      editor.buffer.filename = strdup("<stdin>");
      editor.buffer.is_modified = true;
      editor_set_status_message("Read %zu bytes from stdin", stdin_size);
    }
    autosave_update_path();
  } else if (argument_count >= 2) {
    const char *filename = argument_values[1];

    /* Check for swap file (crash recovery) */
    const char *swap_path = autosave_check_recovery(filename);
    if (swap_path != NULL) {
      bool recover = autosave_prompt_recovery(filename, swap_path);
      if (recover) {
        /* Load from swap file */
        int ret = file_open(&editor.buffer, swap_path);
        if (ret == 0) {
          /* Set original filename */
          free(editor.buffer.filename);
          editor.buffer.filename = strdup(filename);
          editor.buffer.is_modified = true;
          autosave_update_path();
          autosave_set_swap_exists(true);
          editor_set_status_message(
              "Recovered from swap - save to keep changes");
        } else {
          editor_set_status_message("Recovery failed: %s", edit_strerror(ret));
          /* Fall through to normal open */
          swap_path = NULL;
        }
      } else {
        swap_path = NULL; /* User chose not to recover */
      }
    }

    if (swap_path == NULL) {
      /* Normal file open */
      int ret = file_open(&editor.buffer, filename);
      if (ret) {
        if (ret == -ENOENT) {
          /* File doesn't exist yet - that's OK, new file */
          editor.buffer.filename = strdup(filename);
          editor.buffer.is_modified = false;
        } else {
          /* Real error - show message but continue with empty buffer */
          editor_set_status_message("Cannot open file: %s", edit_strerror(ret));
        }
      }
      autosave_update_path();
    }
  } else {
    /* No file specified - also update swap path for unnamed */
    autosave_update_path();
  }

  editor_update_gutter_width();
  editor_set_status_message("Alt+O Open | Alt+S Save | Alt+Q Quit | F1 Help");

  /* Track last check times */
  static time_t last_autosave_check = 0;
  static time_t last_filechange_check = 0;

/* How often to check for external file changes (seconds) */
#define FILECHANGE_CHECK_INTERVAL_SECONDS 2

  while (1) {
    int ret = render_refresh_screen();
    if (ret) {
      /* Minimal recovery on render failure */
      write(STDOUT_FILENO, ESCAPE_CLEAR_SCREEN_HOME,
            ESCAPE_CLEAR_SCREEN_HOME_LENGTH);
      fprintf(stderr, "Render error: %s\n", edit_strerror(ret));
      usleep(100000);
    }
    editor_process_keypress();
    worker_process_results();

    time_t now = time(NULL);

    /* Check auto-save periodically */
    if (now - last_autosave_check >= AUTOSAVE_CHECK_INTERVAL_SECONDS) {
      autosave_check();
      last_autosave_check = now;
    }

    /* Check for external file changes periodically */
    if (now - last_filechange_check >= FILECHANGE_CHECK_INTERVAL_SECONDS) {
      if (!reload_prompt_is_active() &&
          file_check_external_change(&editor.buffer)) {
        reload_prompt_enter();
      }
      last_filechange_check = now;
    }
  }

  return 0;
}
