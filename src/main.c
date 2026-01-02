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

/*****************************************************************************
 * External Dependencies
 *****************************************************************************/

/* Global editor state from edit.c */
extern struct editor_state editor;

/* Functions from edit.c */
extern void fatal_signal_handler(int sig);
extern int file_open(struct buffer *buffer, const char *filename);
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
int main(int argument_count, char *argument_values[])
{
	int ret = terminal_enable_raw_mode();
	if (ret) {
		fprintf(stderr, "edit: %s\n", edit_strerror(ret));
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
	editor_update_screen_size();

	if (argument_count >= 2) {
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
					editor_set_status_message("Recovered from swap - save to keep changes");
				} else {
					editor_set_status_message("Recovery failed: %s", edit_strerror(ret));
					/* Fall through to normal open */
					swap_path = NULL;
				}
			} else {
				swap_path = NULL;  /* User chose not to recover */
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
					editor_set_status_message("Cannot open file: %s",
					                          edit_strerror(ret));
				}
			}
			autosave_update_path();
		}
	} else {
		/* No file specified - also update swap path for unnamed */
		autosave_update_path();
	}

	editor_update_gutter_width();
	editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit | F2 = toggle line numbers");

	/* Track last autosave check time */
	static time_t last_autosave_check = 0;

	while (1) {
		int ret = render_refresh_screen();
		if (ret) {
			/* Minimal recovery on render failure */
			write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
			fprintf(stderr, "Render error: %s\n", edit_strerror(ret));
			usleep(100000);
		}
		editor_process_keypress();
		worker_process_results();

		/* Check auto-save periodically */
		time_t now = time(NULL);
		if (now - last_autosave_check >= 5) {
			autosave_check();
			last_autosave_check = now;
		}
	}

	return 0;
}
