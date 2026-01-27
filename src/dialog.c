/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * dialog.c - Dialog panels for edit
 *
 * Provides modal dialog infrastructure and shared dialog utilities.
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "../lib/gstr/include/gstr.h"
#include "edit.h"

/*****************************************************************************
 * External Dependencies
 *****************************************************************************/

/* Editor state from edit.c */
extern struct editor_state editor;

/* Functions from edit.c */
extern void editor_set_status_message(const char *format, ...);
extern void editor_request_background_warming(void);
extern void multicursor_exit(void);
extern const char *edit_strerror(int error_code);

/* Functions from input.c */
extern void input_set_dialog_mouse_mode(bool enabled);
extern int input_read_key(void);
extern struct mouse_input input_get_last_mouse(void);

/* Functions from theme.c */
extern void theme_apply_by_index(int index);
extern void config_save(void);
extern int style_to_escape(const struct style *style, char *buffer,
                           size_t buffer_size);

/*****************************************************************************
 * Dialog Rendering Helpers
 *****************************************************************************/

/*
 * Move cursor to dialog row and column (1-based terminal coordinates).
 */
void dialog_goto(struct output_buffer *output, int row, int column) {
  char escape[32];
  snprintf(escape, sizeof(escape), "\x1b[%d;%dH", row, column);
  output_buffer_append_string(output, escape);
}

/*
 * Set foreground color for dialog output.
 */
void dialog_set_fg(struct output_buffer *output, struct syntax_color color) {
  char escape[32];
  snprintf(escape, sizeof(escape), "\x1b[38;2;%d;%d;%dm", color.red,
           color.green, color.blue);
  output_buffer_append_string(output, escape);
}

/*
 * Set background color for dialog output.
 */
void dialog_set_bg(struct output_buffer *output, struct syntax_color color) {
  char escape[32];
  snprintf(escape, sizeof(escape), "\x1b[48;2;%d;%d;%dm", color.red,
           color.green, color.blue);
  output_buffer_append_string(output, escape);
}

/*
 * Set full style (fg, bg, attributes) for dialog output.
 */
void dialog_set_style(struct output_buffer *output, const struct style *style) {
  char escape[128];
  int length = style_to_escape(style, escape, sizeof(escape));
  output_buffer_append(output, escape, length);
}

/*
 * Draw dialog header with title centered.
 */
void dialog_draw_header(struct output_buffer *output,
                        struct dialog_state *dialog, const char *title) {
  dialog_goto(output, dialog->panel_top + 1, dialog->panel_left + 1);
  dialog_set_style(output, &active_theme.dialog_header);

  /* Calculate title position for centering */
  int title_length = strlen(title);
  int padding_left = (dialog->panel_width - title_length) / 2;
  if (padding_left < 1) {
    padding_left = 1;
  }

  /* Draw header with centered title */
  for (int i = 0; i < dialog->panel_width; i++) {
    if (i >= padding_left && i < padding_left + title_length) {
      output_buffer_append_char(output, title[i - padding_left]);
    } else {
      output_buffer_append_char(output, ' ');
    }
  }
}

/*
 * Draw dialog footer with hint text.
 */
void dialog_draw_footer(struct output_buffer *output,
                        struct dialog_state *dialog, const char *hint) {
  int footer_row = dialog->panel_top + dialog->panel_height;
  dialog_goto(output, footer_row, dialog->panel_left + 1);
  dialog_set_style(output, &active_theme.dialog_footer);

  /* Draw hint left-aligned with padding */
  int hint_length = strlen(hint);
  int chars_written = 0;

  output_buffer_append_char(output, ' ');
  chars_written++;

  for (int i = 0; i < hint_length && chars_written < dialog->panel_width - 1;
       i++) {
    output_buffer_append_char(output, hint[i]);
    chars_written++;
  }

  /* Fill rest with spaces */
  while (chars_written < dialog->panel_width) {
    output_buffer_append_char(output, ' ');
    chars_written++;
  }
}

/*
 * Draw an empty row in the dialog content area.
 */
void dialog_draw_empty_row(struct output_buffer *output,
                           struct dialog_state *dialog, int row_index) {
  int screen_row = dialog->panel_top + 2 + row_index;
  dialog_goto(output, screen_row, dialog->panel_left + 1);
  dialog_set_bg(output, active_theme.dialog.bg);

  for (int i = 0; i < dialog->panel_width; i++) {
    output_buffer_append_char(output, ' ');
  }
}

/*
 * Draw a single list item in the dialog.
 */
void dialog_draw_list_item(struct output_buffer *output,
                           struct dialog_state *dialog, int row_index,
                           const char *text, bool is_selected,
                           bool is_directory) {
  int screen_row = dialog->panel_top + 2 + row_index;
  dialog_goto(output, screen_row, dialog->panel_left + 1);

  if (is_selected) {
    dialog_set_style(output, &active_theme.dialog_highlight);
  } else if (is_directory) {
    dialog_set_style(output, &active_theme.dialog_directory);
  } else {
    dialog_set_style(output, &active_theme.dialog);
  }

  /* Draw text with padding */
  int text_length = strlen(text);
  int chars_written = 0;

  output_buffer_append_char(output, ' ');
  chars_written++;

  /* Draw folder icon or matching indent for alignment */
  if (editor.show_file_icons) {
    if (is_directory) {
      output_buffer_append_string(output, "🗁  ");
    } else {
      output_buffer_append_string(output, "   ");
    }
    chars_written += 3; /* Icon/indent is 3 cells */
  }

  for (int i = 0; i < text_length && chars_written < dialog->panel_width - 1;
       i++) {
    output_buffer_append_char(output, text[i]);
    chars_written++;
  }

  /* Fill rest with spaces */
  while (chars_written < dialog->panel_width) {
    output_buffer_append_char(output, ' ');
    chars_written++;
  }
}

/*****************************************************************************
 * Dialog State Management
 *****************************************************************************/

/*
 * Calculate dialog panel dimensions based on screen size.
 * Panel is centered, 50% height, 70% width, with minimum sizes.
 */
void dialog_calculate_dimensions(struct dialog_state *dialog) {
  const int minimum_width = DIALOG_MIN_WIDTH;
  const int minimum_height = DIALOG_MIN_HEIGHT;

  /* 70% of screen width, at least minimum */
  dialog->panel_width = (editor.screen_columns * DIALOG_WIDTH_PERCENT) / 100;
  if (dialog->panel_width < minimum_width) {
    dialog->panel_width = minimum_width;
  }
  if (dialog->panel_width > (int)editor.screen_columns - DIALOG_SCREEN_MARGIN) {
    dialog->panel_width = (int)editor.screen_columns - DIALOG_SCREEN_MARGIN;
  }

  /* 50% of screen height, at least minimum */
  dialog->panel_height = (editor.screen_rows * DIALOG_HEIGHT_PERCENT) / 100;
  if (dialog->panel_height < minimum_height) {
    dialog->panel_height = minimum_height;
  }
  if (dialog->panel_height > (int)editor.screen_rows - DIALOG_SCREEN_MARGIN) {
    dialog->panel_height = (int)editor.screen_rows - DIALOG_SCREEN_MARGIN;
  }

  /* Center on screen */
  dialog->panel_left = (editor.screen_columns - dialog->panel_width) / 2;
  dialog->panel_top = (editor.screen_rows - dialog->panel_height) / 2;

  /* Content area: subtract 2 for header and footer */
  dialog->visible_rows = dialog->panel_height - 2;
  if (dialog->visible_rows < 1) {
    dialog->visible_rows = 1;
  }
}

/*
 * Ensure the selected item is visible by adjusting scroll offset.
 */
void dialog_ensure_visible(struct dialog_state *dialog) {
  if (dialog->selected_index < dialog->scroll_offset) {
    dialog->scroll_offset = dialog->selected_index;
  }
  if (dialog->selected_index >= dialog->scroll_offset + dialog->visible_rows) {
    dialog->scroll_offset = dialog->selected_index - dialog->visible_rows + 1;
  }
}

/*
 * Clamp selection index to valid range and ensure visibility.
 */
void dialog_clamp_selection(struct dialog_state *dialog) {
  if (dialog->selected_index < 0) {
    dialog->selected_index = 0;
  }
  if (dialog->selected_index >= dialog->item_count) {
    dialog->selected_index = dialog->item_count - 1;
  }
  if (dialog->selected_index < 0) {
    dialog->selected_index = 0;
  }
  dialog_ensure_visible(dialog);
}

/*
 * Close the dialog and restore normal editor state.
 */
void dialog_close(struct dialog_state *dialog) {
  dialog->active = false;
  input_set_dialog_mouse_mode(false);

  /* Show cursor again now that dialog is closed */
  write(STDOUT_FILENO, ESCAPE_CURSOR_SHOW, ESCAPE_CURSOR_SHOW_LENGTH);
}

/*****************************************************************************
 * Dialog Input Handling
 *****************************************************************************/

/*
 * Check if a click qualifies as a double-click based on timing and position.
 */
static bool dialog_is_double_click(struct dialog_state *dialog,
                                   int item_index) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  /* Check if same item was clicked */
  if (item_index != dialog->last_click_index) {
    dialog->last_click = now;
    dialog->last_click_index = item_index;
    return false;
  }

  /* Calculate elapsed milliseconds */
  long elapsed_ms = (now.tv_sec - dialog->last_click.tv_sec) * 1000 +
                    (now.tv_nsec - dialog->last_click.tv_nsec) / 1000000;

  dialog->last_click = now;
  dialog->last_click_index = item_index;

  return elapsed_ms <= DIALOG_DOUBLE_CLICK_MS;
}

/*
 * Handle keyboard input for dialog navigation.
 * Returns the action to take.
 */
enum dialog_result dialog_handle_key(struct dialog_state *dialog, int key) {
  switch (key) {
  case KEY_ARROW_UP:
    dialog->selected_index--;
    dialog_clamp_selection(dialog);
    return DIALOG_CONTINUE;

  case KEY_ARROW_DOWN:
    dialog->selected_index++;
    dialog_clamp_selection(dialog);
    return DIALOG_CONTINUE;

  case KEY_PAGE_UP:
    dialog->selected_index -= dialog->visible_rows;
    dialog_clamp_selection(dialog);
    return DIALOG_CONTINUE;

  case KEY_PAGE_DOWN:
    dialog->selected_index += dialog->visible_rows;
    dialog_clamp_selection(dialog);
    return DIALOG_CONTINUE;

  case KEY_HOME:
    dialog->selected_index = 0;
    dialog_clamp_selection(dialog);
    return DIALOG_CONTINUE;

  case KEY_END:
    dialog->selected_index = dialog->item_count - 1;
    dialog_clamp_selection(dialog);
    return DIALOG_CONTINUE;

  case '\r':
  case '\n':
    return DIALOG_CONFIRM;

  case '\x1b':
    return DIALOG_CANCEL;

  default:
    return DIALOG_CONTINUE;
  }
}

/*
 * Handle mouse input for dialog interaction.
 * Returns the action to take.
 */
enum dialog_result dialog_handle_mouse(struct dialog_state *dialog,
                                       struct mouse_input *mouse) {
  /*
   * Mouse coordinates are 0-based (parsed from 1-based terminal coords).
   * Dialog panel_top is 0-based. content_offset indicates how many rows
   * from panel_top the list content starts (default 1 for header only,
   * 2 for header + extra row like query input).
   */
  int offset = dialog->content_offset > 0 ? dialog->content_offset : 1;
  int content_top = dialog->panel_top + offset;
  int content_bottom = dialog->panel_top + dialog->panel_height - 1;
  int content_left = dialog->panel_left + 1;
  int content_right = dialog->panel_left + dialog->panel_width;

  /* Handle scroll wheel */
  if (mouse->event == MOUSE_SCROLL_UP) {
    dialog->scroll_offset -= 3;
    if (dialog->scroll_offset < 0) {
      dialog->scroll_offset = 0;
    }
    return DIALOG_CONTINUE;
  }

  if (mouse->event == MOUSE_SCROLL_DOWN) {
    int max_scroll = dialog->item_count - dialog->visible_rows;
    if (max_scroll < 0) {
      max_scroll = 0;
    }
    dialog->scroll_offset += 3;
    if (dialog->scroll_offset > max_scroll) {
      dialog->scroll_offset = max_scroll;
    }
    return DIALOG_CONTINUE;
  }

  /* Check if within content area */
  if ((int)mouse->column < content_left ||
      (int)mouse->column >= content_right) {
    return DIALOG_CONTINUE;
  }
  if ((int)mouse->row < content_top || (int)mouse->row >= content_bottom) {
    return DIALOG_CONTINUE;
  }

  /* Calculate which item was clicked */
  int row_offset = mouse->row - content_top;
  int item_index = dialog->scroll_offset + row_offset;

  if (item_index < 0 || item_index >= dialog->item_count) {
    return DIALOG_CONTINUE;
  }

  /* Handle click */
  if (mouse->event == MOUSE_LEFT_PRESS) {
    dialog->mouse_down = true;

    /* Check for double-click */
    if (dialog_is_double_click(dialog, item_index)) {
      dialog->selected_index = item_index;
      return DIALOG_CONFIRM;
    }

    dialog->selected_index = item_index;
    return DIALOG_CONTINUE;
  }

  if (mouse->event == MOUSE_LEFT_RELEASE) {
    dialog->mouse_down = false;
  }

  return DIALOG_CONTINUE;
}
