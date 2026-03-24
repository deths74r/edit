/* Test suite for edit -- the terminal text editor.
 * Compiles with -DEDIT_TESTING and includes edit.c directly so all
 * internal functions and globals are accessible. */

#define EDIT_TESTING
#include "edit.c"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*** Test harness ***/

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
	tests_run++; \
	if (!(condition)) { \
		tests_failed++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message); \
	} else { \
		tests_passed++; \
	} \
} while (0)

#define TEST_ASSERT_INT(expected, actual) do { \
	tests_run++; \
	int _e = (expected), _a = (actual); \
	if (_e != _a) { \
		tests_failed++; \
		fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", \
			__FILE__, __LINE__, _e, _a); \
	} else { \
		tests_passed++; \
	} \
} while (0)

#define TEST_ASSERT_STR(expected, actual) do { \
	tests_run++; \
	if (strcmp((expected), (actual)) != 0) { \
		tests_failed++; \
		fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
			__FILE__, __LINE__, (expected), (actual)); \
	} else { \
		tests_passed++; \
	} \
} while (0)

/*** State reset helper ***/

/* Resets the global editor state between tests so each test starts clean.
 * Frees all lines, undo stack, clipboard, and search history, then
 * reinitializes with a mocked 80x24 terminal. */
static void test_reset_editor(void)
{
	/* Free all lines */
	for (int i = 0; i < editor.line_count; i++)
		line_free(&editor.lines[i]);
	free(editor.lines);
	editor.lines = NULL;
	editor.line_count = 0;
	editor.line_capacity = 0;

	/* Free undo stack */
	undo_stack_destroy(&editor.undo);

	/* Free clipboard */
	clipboard_clear();

	/* Free search history */
	for (int i = 0; i < editor.search_history_count; i++)
		free(editor.search_history[i]);
	editor.search_history_count = 0;
	free(editor.search_history_stash);
	editor.search_history_stash = NULL;

	/* Free filename */
	free(editor.filename);
	editor.filename = NULL;

	/* Free scratch buffers */
	free(editor.scratch_buffer);
	editor.scratch_buffer = NULL;
	editor.scratch_capacity = 0;
	free(editor.scratch_offsets);
	editor.scratch_offsets = NULL;
	editor.scratch_offsets_capacity = 0;

	/* Free replace state */
	free(editor.replace_query);
	editor.replace_query = NULL;
	free(editor.replace_with);
	editor.replace_with = NULL;
	free(editor.search_highlight_query);
	editor.search_highlight_query = NULL;

	/* Free search saved syntax */
	free(editor.search_saved_syntax);
	editor.search_saved_syntax = NULL;
	editor.search_saved_syntax_count = 0;

	/* Free compiled regex */
	if (editor.search_regex_valid) {
		regfree(&editor.search_regex_compiled);
		editor.search_regex_valid = 0;
	}

	/* Free swap file path */
	free(editor.swap_file_path);
	editor.swap_file_path = NULL;

	/* Reinitialize editor state */
	editor_init();

	/* Override screen rows for tests (24 - 2 status/message bars) */
	editor.screen_rows = 22;
}

/*** Category 1: Cell and Line Operations ***/

static void test_line_init_creates_empty_line(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	TEST_ASSERT_INT(0, (int)ln.cell_count);
	TEST_ASSERT_INT(LINE_INITIAL_CAPACITY, (int)ln.cell_capacity);
	TEST_ASSERT(ln.cells != NULL, "cells should be allocated");
	TEST_ASSERT_INT(LINE_HOT, ln.temperature);
	TEST_ASSERT_INT(0, ln.line_index);
	line_free(&ln);
}

static void test_line_populate_from_bytes_ascii(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "Hello", 5);
	TEST_ASSERT_INT(5, (int)ln.cell_count);
	TEST_ASSERT_INT('H', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('e', (int)ln.cells[1].codepoint);
	TEST_ASSERT_INT('l', (int)ln.cells[2].codepoint);
	TEST_ASSERT_INT('l', (int)ln.cells[3].codepoint);
	TEST_ASSERT_INT('o', (int)ln.cells[4].codepoint);
	line_free(&ln);
}

static void test_line_populate_from_bytes_utf8(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	/* Two-byte: e-acute U+00E9 = 0xC3 0xA9 */
	const char *utf8 = "caf\xC3\xA9";
	line_populate_from_bytes(&ln, utf8, strlen(utf8));
	TEST_ASSERT_INT(4, (int)ln.cell_count);
	TEST_ASSERT_INT('c', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('a', (int)ln.cells[1].codepoint);
	TEST_ASSERT_INT('f', (int)ln.cells[2].codepoint);
	TEST_ASSERT_INT(0x00E9, (int)ln.cells[3].codepoint);
	line_free(&ln);
}

static void test_line_populate_from_bytes_empty(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "", 0);
	TEST_ASSERT_INT(0, (int)ln.cell_count);
	line_free(&ln);
}

static void test_line_insert_cell_at_beginning(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "BC", 2);
	struct cell c = {.codepoint = 'A'};
	line_insert_cell(&ln, 0, c);
	TEST_ASSERT_INT(3, (int)ln.cell_count);
	TEST_ASSERT_INT('A', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)ln.cells[1].codepoint);
	TEST_ASSERT_INT('C', (int)ln.cells[2].codepoint);
	line_free(&ln);
}

static void test_line_insert_cell_at_middle(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "AC", 2);
	struct cell c = {.codepoint = 'B'};
	line_insert_cell(&ln, 1, c);
	TEST_ASSERT_INT(3, (int)ln.cell_count);
	TEST_ASSERT_INT('A', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)ln.cells[1].codepoint);
	TEST_ASSERT_INT('C', (int)ln.cells[2].codepoint);
	line_free(&ln);
}

static void test_line_insert_cell_at_end(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "AB", 2);
	struct cell c = {.codepoint = 'C'};
	line_insert_cell(&ln, 2, c);
	TEST_ASSERT_INT(3, (int)ln.cell_count);
	TEST_ASSERT_INT('A', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)ln.cells[1].codepoint);
	TEST_ASSERT_INT('C', (int)ln.cells[2].codepoint);
	line_free(&ln);
}

static void test_line_delete_cell_first(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "ABC", 3);
	line_delete_cell(&ln, 0);
	TEST_ASSERT_INT(2, (int)ln.cell_count);
	TEST_ASSERT_INT('B', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('C', (int)ln.cells[1].codepoint);
	line_free(&ln);
}

static void test_line_delete_cell_last(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "ABC", 3);
	line_delete_cell(&ln, 2);
	TEST_ASSERT_INT(2, (int)ln.cell_count);
	TEST_ASSERT_INT('A', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)ln.cells[1].codepoint);
	line_free(&ln);
}

static void test_line_delete_cell_middle(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "ABC", 3);
	line_delete_cell(&ln, 1);
	TEST_ASSERT_INT(2, (int)ln.cell_count);
	TEST_ASSERT_INT('A', (int)ln.cells[0].codepoint);
	TEST_ASSERT_INT('C', (int)ln.cells[1].codepoint);
	line_free(&ln);
}

static void test_line_to_bytes_roundtrip_ascii(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "Hello", 5);
	size_t length;
	char *bytes = line_to_bytes(&ln, &length);
	TEST_ASSERT(bytes != NULL, "line_to_bytes should not return NULL");
	TEST_ASSERT_INT(5, (int)length);
	TEST_ASSERT_STR("Hello", bytes);
	free(bytes);
	line_free(&ln);
}

static void test_line_to_bytes_roundtrip_utf8(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	const char *original = "caf\xC3\xA9";
	line_populate_from_bytes(&ln, original, strlen(original));
	size_t length;
	char *bytes = line_to_bytes(&ln, &length);
	TEST_ASSERT(bytes != NULL, "line_to_bytes should not return NULL");
	TEST_ASSERT_INT((int)strlen(original), (int)length);
	TEST_ASSERT(memcmp(bytes, original, length) == 0,
		    "UTF-8 roundtrip should produce identical bytes");
	free(bytes);
	line_free(&ln);
}

static void test_cell_display_width_normal(void)
{
	test_reset_editor();
	struct cell c = {.codepoint = 'A'};
	TEST_ASSERT_INT(1, cell_display_width(&c, 0));
}

static void test_cell_display_width_tab_col0(void)
{
	test_reset_editor();
	struct cell c = {.codepoint = '\t'};
	TEST_ASSERT_INT(EDIT_TAB_STOP, cell_display_width(&c, 0));
}

static void test_cell_display_width_tab_col3(void)
{
	test_reset_editor();
	struct cell c = {.codepoint = '\t'};
	TEST_ASSERT_INT(EDIT_TAB_STOP - 3, cell_display_width(&c, 3));
}

static void test_cell_display_width_wide_char(void)
{
	test_reset_editor();
	/* CJK ideograph U+4E16 (width 2) */
	struct cell c = {.codepoint = 0x4E16};
	TEST_ASSERT_INT(2, cell_display_width(&c, 0));
}

static void test_line_ensure_capacity_grows(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	uint32_t original = ln.cell_capacity;
	line_ensure_capacity(&ln, original + 1);
	TEST_ASSERT(ln.cell_capacity > original,
		    "capacity should have grown");
	TEST_ASSERT(ln.cell_capacity >= original + 1,
		    "capacity should be at least the requested amount");
	line_free(&ln);
}

/*** Category 2: Editor Operations ***/

static void test_editor_insert_char_empty_file(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(0, editor.line_count);
	editor_insert_char('A');
	TEST_ASSERT_INT(1, editor.line_count);
	TEST_ASSERT_INT(1, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('A', (int)editor.lines[0].cells[0].codepoint);
	TEST_ASSERT_INT(1, editor.cursor_x);
}

static void test_editor_insert_char_end_of_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "AB", 2);
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_insert_char('C');
	TEST_ASSERT_INT(3, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('C', (int)editor.lines[0].cells[2].codepoint);
	TEST_ASSERT_INT(3, editor.cursor_x);
}

static void test_editor_insert_char_middle_of_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "AC", 2);
	editor.cursor_x = 1;
	editor.cursor_y = 0;
	editor_insert_char('B');
	TEST_ASSERT_INT(3, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('A', (int)editor.lines[0].cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)editor.lines[0].cells[1].codepoint);
	TEST_ASSERT_INT('C', (int)editor.lines[0].cells[2].codepoint);
}

static void test_editor_insert_newline_at_start(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_insert_newline();
	TEST_ASSERT_INT(2, editor.line_count);
	TEST_ASSERT_INT(0, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT(5, (int)editor.lines[1].cell_count);
	TEST_ASSERT_INT(1, editor.cursor_y);
	TEST_ASSERT_INT(0, editor.cursor_x);
}

static void test_editor_insert_newline_at_end(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor.cursor_x = 5;
	editor.cursor_y = 0;
	editor_insert_newline();
	TEST_ASSERT_INT(2, editor.line_count);
	TEST_ASSERT_INT(5, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT(0, (int)editor.lines[1].cell_count);
	TEST_ASSERT_INT(1, editor.cursor_y);
}

static void test_editor_insert_newline_in_middle(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello World", 11);
	editor.cursor_x = 5;
	editor.cursor_y = 0;
	editor_insert_newline();
	TEST_ASSERT_INT(2, editor.line_count);
	TEST_ASSERT_INT(5, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT(6, (int)editor.lines[1].cell_count);
	TEST_ASSERT_INT(1, editor.cursor_y);
}

static void test_editor_insert_newline_auto_indent(void)
{
	test_reset_editor();
	editor_line_insert(0, "\tHello", 6);
	editor.cursor_x = 6;
	editor.cursor_y = 0;
	editor_insert_newline();
	TEST_ASSERT_INT(2, editor.line_count);
	/* New line should start with a tab from auto-indent */
	TEST_ASSERT(editor.lines[1].cell_count >= 1,
		    "new line should have at least one cell (indent)");
	TEST_ASSERT_INT('\t', (int)editor.lines[1].cells[0].codepoint);
	/* Cursor should be after the indent */
	TEST_ASSERT_INT(1, editor.cursor_x);
}

static void test_editor_delete_char_end_of_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "ABC", 3);
	editor.cursor_x = 3;
	editor.cursor_y = 0;
	editor_delete_char();
	TEST_ASSERT_INT(2, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('A', (int)editor.lines[0].cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)editor.lines[0].cells[1].codepoint);
	TEST_ASSERT_INT(2, editor.cursor_x);
}

static void test_editor_delete_char_joins_lines(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor_line_insert(1, "World", 5);
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_delete_char();
	TEST_ASSERT_INT(1, editor.line_count);
	TEST_ASSERT_INT(10, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT(0, editor.cursor_y);
	TEST_ASSERT_INT(5, editor.cursor_x);
}

static void test_editor_delete_char_noop_at_start(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	int before_count = (int)editor.lines[0].cell_count;
	editor_delete_char();
	TEST_ASSERT_INT(before_count, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT(0, editor.cursor_x);
	TEST_ASSERT_INT(0, editor.cursor_y);
}

static void test_editor_line_insert_and_delete(void)
{
	test_reset_editor();
	editor_line_insert(0, "Line 0", 6);
	editor_line_insert(1, "Line 1", 6);
	editor_line_insert(2, "Line 2", 6);
	TEST_ASSERT_INT(3, editor.line_count);
	editor_line_delete(1);
	TEST_ASSERT_INT(2, editor.line_count);
	/* Remaining lines should have updated indices */
	TEST_ASSERT_INT(0, editor.lines[0].line_index);
	TEST_ASSERT_INT(1, editor.lines[1].line_index);
}

/*** Category 3: Undo/Redo ***/

static void test_undo_after_insert_char(void)
{
	test_reset_editor();
	editor_line_insert(0, "AB", 2);
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_insert_char('C');
	TEST_ASSERT_INT(3, (int)editor.lines[0].cell_count);
	editor_undo();
	TEST_ASSERT_INT(2, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('A', (int)editor.lines[0].cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)editor.lines[0].cells[1].codepoint);
}

static void test_redo_after_undo(void)
{
	test_reset_editor();
	editor_line_insert(0, "AB", 2);
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_insert_char('C');
	editor_undo();
	TEST_ASSERT_INT(2, (int)editor.lines[0].cell_count);
	editor_redo();
	TEST_ASSERT_INT(3, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('C', (int)editor.lines[0].cells[2].codepoint);
}

static void test_undo_after_newline(void)
{
	test_reset_editor();
	editor_line_insert(0, "HelloWorld", 10);
	editor.cursor_x = 5;
	editor.cursor_y = 0;
	editor_insert_newline();
	TEST_ASSERT_INT(2, editor.line_count);
	editor_undo();
	TEST_ASSERT_INT(1, editor.line_count);
	TEST_ASSERT_INT(10, (int)editor.lines[0].cell_count);
}

static void test_undo_after_delete_char(void)
{
	test_reset_editor();
	editor_line_insert(0, "ABC", 3);
	editor.cursor_x = 3;
	editor.cursor_y = 0;
	editor_delete_char();
	TEST_ASSERT_INT(2, (int)editor.lines[0].cell_count);
	editor_undo();
	TEST_ASSERT_INT(3, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('C', (int)editor.lines[0].cells[2].codepoint);
}

static void test_undo_after_line_join(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor_line_insert(1, "World", 5);
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_delete_char();
	TEST_ASSERT_INT(1, editor.line_count);
	editor_undo();
	TEST_ASSERT_INT(2, editor.line_count);
	TEST_ASSERT_INT(5, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT(5, (int)editor.lines[1].cell_count);
}

static void test_new_edit_after_undo_discards_redo(void)
{
	test_reset_editor();
	editor_line_insert(0, "A", 1);
	editor.cursor_x = 1;
	editor.cursor_y = 0;
	editor_insert_char('B');
	editor_undo();
	/* Insert a different character -- should discard redo history */
	editor.cursor_x = 1;
	editor_insert_char('X');
	/* Redo should now have nothing to redo */
	int redo_available = (editor.undo.current < editor.undo.group_count);
	TEST_ASSERT_INT(0, redo_available);
}

static void test_undo_empty_stack_shows_message(void)
{
	test_reset_editor();
	editor_undo();
	TEST_ASSERT(strstr(editor.status_message, "Nothing to undo") != NULL,
		    "should show 'Nothing to undo' message");
}

static void test_redo_empty_stack_shows_message(void)
{
	test_reset_editor();
	editor_redo();
	TEST_ASSERT(strstr(editor.status_message, "Nothing to redo") != NULL,
		    "should show 'Nothing to redo' message");
}

static void test_undo_group_boundary_on_line_change(void)
{
	test_reset_editor();
	editor_line_insert(0, "", 0);
	editor_line_insert(1, "", 0);
	/* Type on line 0 */
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_insert_char('A');
	/* Type on line 1 -- should force a new group boundary */
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_insert_char('B');
	/* Undo should only undo the 'B' on line 1 */
	editor_undo();
	TEST_ASSERT_INT(0, (int)editor.lines[1].cell_count);
	TEST_ASSERT_INT(1, (int)editor.lines[0].cell_count);
}

/*** Category 4: Selection and Clipboard ***/

static void test_selection_start_sets_anchor(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	selection_start();
	TEST_ASSERT_INT(1, editor.selection.active);
	TEST_ASSERT_INT(0, editor.selection.anchor_y);
	TEST_ASSERT_INT(2, editor.selection.anchor_x);
}

static void test_selection_get_range_cursor_after_anchor(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 1;
	editor.cursor_y = 0;
	editor.cursor_x = 4;
	int start_y, start_x, end_y, end_x;
	int result = selection_get_range(&start_y, &start_x, &end_y, &end_x);
	TEST_ASSERT_INT(1, result);
	TEST_ASSERT_INT(0, start_y);
	TEST_ASSERT_INT(1, start_x);
	TEST_ASSERT_INT(0, end_y);
	TEST_ASSERT_INT(4, end_x);
}

static void test_selection_get_range_cursor_before_anchor(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 4;
	editor.cursor_y = 0;
	editor.cursor_x = 1;
	int start_y, start_x, end_y, end_x;
	int result = selection_get_range(&start_y, &start_x, &end_y, &end_x);
	TEST_ASSERT_INT(1, result);
	TEST_ASSERT_INT(0, start_y);
	TEST_ASSERT_INT(1, start_x);
	TEST_ASSERT_INT(0, end_y);
	TEST_ASSERT_INT(4, end_x);
}

static void test_selection_to_string_single_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello World", 11);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 0;
	editor.cursor_y = 0;
	editor.cursor_x = 5;
	size_t length;
	char *text = selection_to_string(&length);
	TEST_ASSERT(text != NULL, "selection_to_string should not return NULL");
	TEST_ASSERT_INT(5, (int)length);
	TEST_ASSERT_STR("Hello", text);
	free(text);
}

static void test_selection_to_string_multi_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor_line_insert(1, "World", 5);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 3;
	editor.cursor_y = 1;
	editor.cursor_x = 3;
	size_t length;
	char *text = selection_to_string(&length);
	TEST_ASSERT(text != NULL, "selection_to_string should not return NULL");
	/* Should be "lo\nWor" */
	TEST_ASSERT_STR("lo\nWor", text);
	free(text);
}

static void test_selection_delete_single_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello World", 11);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 5;
	editor.cursor_y = 0;
	editor.cursor_x = 11;
	selection_delete();
	TEST_ASSERT_INT(5, (int)editor.lines[0].cell_count);
	size_t length;
	char *bytes = line_to_bytes(&editor.lines[0], &length);
	TEST_ASSERT_STR("Hello", bytes);
	free(bytes);
}

static void test_selection_delete_multi_line(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor_line_insert(1, "Beautiful", 9);
	editor_line_insert(2, "World", 5);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 3;
	editor.cursor_y = 2;
	editor.cursor_x = 3;
	selection_delete();
	TEST_ASSERT_INT(1, editor.line_count);
	size_t length;
	char *bytes = line_to_bytes(&editor.lines[0], &length);
	TEST_ASSERT_STR("Helld", bytes);
	free(bytes);
}

static void test_clipboard_store_and_retrieve(void)
{
	test_reset_editor();
	clipboard_store("Hello", 5, 0);
	TEST_ASSERT_INT(1, editor.clipboard.line_count);
	TEST_ASSERT_STR("Hello", editor.clipboard.lines[0]);
	TEST_ASSERT_INT(5, (int)editor.clipboard.line_lengths[0]);
}

static void test_clipboard_store_multiline(void)
{
	test_reset_editor();
	clipboard_store("Hello\nWorld", 11, 0);
	TEST_ASSERT_INT(2, editor.clipboard.line_count);
	TEST_ASSERT_STR("Hello", editor.clipboard.lines[0]);
	TEST_ASSERT_STR("World", editor.clipboard.lines[1]);
}

static void test_clipboard_clear_frees_memory(void)
{
	test_reset_editor();
	clipboard_store("test data", 9, 0);
	TEST_ASSERT_INT(1, editor.clipboard.line_count);
	clipboard_clear();
	TEST_ASSERT_INT(0, editor.clipboard.line_count);
	TEST_ASSERT(editor.clipboard.lines == NULL,
		    "lines pointer should be NULL after clear");
}

static void test_word_char_class_whitespace(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(0, word_char_class(' '));
	TEST_ASSERT_INT(0, word_char_class('\t'));
	TEST_ASSERT_INT(0, word_char_class('\0'));
}

static void test_word_char_class_punctuation(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(1, word_char_class('('));
	TEST_ASSERT_INT(1, word_char_class(')'));
	TEST_ASSERT_INT(1, word_char_class('+'));
	TEST_ASSERT_INT(1, word_char_class(';'));
}

static void test_word_char_class_word(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(2, word_char_class('a'));
	TEST_ASSERT_INT(2, word_char_class('Z'));
	TEST_ASSERT_INT(2, word_char_class('0'));
	TEST_ASSERT_INT(2, word_char_class('_'));
}

/*** Category 5: Search ***/

static void test_editor_memmem_case_sensitive(void)
{
	test_reset_editor();
	const char *haystack = "Hello World";
	char *found = editor_memmem(haystack, strlen(haystack),
				    "World", 5, 0);
	TEST_ASSERT(found != NULL, "should find 'World'");
	TEST_ASSERT(found == haystack + 6,
		    "should point to the start of 'World'");
}

static void test_editor_memmem_case_insensitive(void)
{
	test_reset_editor();
	const char *haystack = "Hello World";
	char *found = editor_memmem(haystack, strlen(haystack),
				    "world", 5, 1);
	TEST_ASSERT(found != NULL, "should find 'world' case-insensitively");
	TEST_ASSERT(found == haystack + 6,
		    "should point to the start of 'World'");
}

static void test_editor_memmem_no_match(void)
{
	test_reset_editor();
	const char *haystack = "Hello World";
	char *found = editor_memmem(haystack, strlen(haystack),
				    "xyz", 3, 0);
	TEST_ASSERT(found == NULL, "should not find 'xyz'");
}

static void test_editor_memmem_empty_needle(void)
{
	test_reset_editor();
	const char *haystack = "Hello";
	char *found = editor_memmem(haystack, strlen(haystack),
				    "", 0, 1);
	/* Per the implementation, empty needle returns haystack */
	TEST_ASSERT(found == haystack,
		    "empty needle should return haystack pointer");
}

static void test_editor_memmem_case_sensitive_no_match(void)
{
	test_reset_editor();
	const char *haystack = "Hello World";
	char *found = editor_memmem(haystack, strlen(haystack),
				    "world", 5, 0);
	TEST_ASSERT(found == NULL,
		    "case-sensitive search should not match different case");
}

/*** Category 6: Syntax ***/

static void test_syntax_is_separator_space(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(1, syntax_is_separator(' '));
}

static void test_syntax_is_separator_letter(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(0, syntax_is_separator('a'));
	TEST_ASSERT_INT(0, syntax_is_separator('Z'));
}

static void test_syntax_is_separator_punctuation(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(1, syntax_is_separator(','));
	TEST_ASSERT_INT(1, syntax_is_separator('('));
	TEST_ASSERT_INT(1, syntax_is_separator(';'));
}

static void test_syntax_is_separator_null(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(1, syntax_is_separator('\0'));
}

static void test_syntax_select_highlight_c_file(void)
{
	test_reset_editor();
	editor.filename = strdup("test.c");
	syntax_select_highlight();
	TEST_ASSERT(editor.syntax != NULL, "should find syntax for .c file");
	TEST_ASSERT_STR("c", editor.syntax->filetype);
}

static void test_syntax_select_highlight_unknown(void)
{
	test_reset_editor();
	editor.filename = strdup("test.xyz");
	syntax_select_highlight();
	TEST_ASSERT(editor.syntax == NULL,
		    "should return NULL for unknown extension");
}

static void test_cursor_next_word(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "hello world", 11);
	int next = cursor_next_word(&ln, 0);
	TEST_ASSERT_INT(6, next);
	line_free(&ln);
}

static void test_cursor_prev_word(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "hello world", 11);
	int prev = cursor_prev_word(&ln, 11);
	TEST_ASSERT_INT(6, prev);
	line_free(&ln);
}

/*** Category 7: MC/DC Decision Coverage ***/

static void test_line_ensure_warm_cold_to_warm(void)
{
	test_reset_editor();
	/* Create a cold line by simulating mmap state.
	 * We need a valid mmap region for the COLD->WARM path. */
	struct line ln = {0};
	ln.temperature = LINE_COLD;
	ln.cells = NULL;
	ln.cell_count = 0;
	ln.cell_capacity = 0;
	ln.line_index = 0;
	ln.syntax_stale = 0;

	/* Provide a fake mmap region with some bytes */
	static char fake_mmap[] = "test";
	char *saved_mmap = editor.mmap_base;
	editor.mmap_base = fake_mmap;
	ln.mmap_offset = 0;
	ln.mmap_length = 4;

	line_ensure_warm(&ln);
	TEST_ASSERT(ln.temperature != LINE_COLD,
		    "should no longer be COLD after warming");
	TEST_ASSERT_INT(4, (int)ln.cell_count);
	TEST_ASSERT_INT('t', (int)ln.cells[0].codepoint);

	editor.mmap_base = saved_mmap;
	line_free(&ln);
}

static void test_line_ensure_warm_already_warm(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "AB", 2);
	ln.temperature = LINE_WARM;
	ln.syntax_stale = 0;
	/* Warming an already-warm line should be a no-op */
	line_ensure_warm(&ln);
	TEST_ASSERT_INT(LINE_WARM, ln.temperature);
	TEST_ASSERT_INT(2, (int)ln.cell_count);
	line_free(&ln);
}

static void test_editor_scroll_cursor_above_margin(void)
{
	test_reset_editor();
	/* Create enough lines to have meaningful scroll behavior */
	for (int i = 0; i < 50; i++)
		editor_line_insert(i, "line", 4);
	editor.row_offset = 20;
	editor.cursor_y = 15;
	editor.cursor_x = 0;
	editor_scroll();
	/* Cursor is above viewport+margin, so row_offset should adjust */
	TEST_ASSERT(editor.row_offset <= editor.cursor_y,
		    "row_offset should be at or before cursor");
}

static void test_editor_scroll_cursor_below_margin(void)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		editor_line_insert(i, "line", 4);
	editor.row_offset = 0;
	editor.cursor_y = 40;
	editor.cursor_x = 0;
	editor_scroll();
	/* Cursor is well below viewport, row_offset should have moved down */
	TEST_ASSERT(editor.row_offset > 0,
		    "row_offset should have moved down to show cursor");
	TEST_ASSERT(editor.cursor_y < editor.row_offset + editor.screen_rows,
		    "cursor should be within visible area");
}

static void test_editor_scroll_cursor_in_safe_zone(void)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		editor_line_insert(i, "line", 4);
	editor.row_offset = 10;
	editor.cursor_y = 20;
	editor.cursor_x = 0;
	int saved_offset = editor.row_offset;
	editor_scroll();
	/* Cursor is comfortably in the viewport, offset should not change */
	TEST_ASSERT_INT(saved_offset, editor.row_offset);
}

static void test_editor_delete_char_cursor_x_positive(void)
{
	test_reset_editor();
	editor_line_insert(0, "ABC", 3);
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_delete_char();
	/* Should delete the character before cursor (at position 1) */
	TEST_ASSERT_INT(2, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('A', (int)editor.lines[0].cells[0].codepoint);
	TEST_ASSERT_INT('C', (int)editor.lines[0].cells[1].codepoint);
}

static void test_editor_delete_char_cursor_x_zero(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor_line_insert(1, "World", 5);
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_delete_char();
	/* Should join lines */
	TEST_ASSERT_INT(1, editor.line_count);
	TEST_ASSERT_INT(10, (int)editor.lines[0].cell_count);
}

static void test_editor_insert_char_creates_line_at_end(void)
{
	test_reset_editor();
	/* cursor_y == line_count means we're past the last line */
	editor.cursor_y = 0;
	editor.cursor_x = 0;
	TEST_ASSERT_INT(0, editor.line_count);
	editor_insert_char('X');
	TEST_ASSERT_INT(1, editor.line_count);
	TEST_ASSERT_INT('X', (int)editor.lines[0].cells[0].codepoint);
}

static void test_editor_insert_char_within_existing_lines(void)
{
	test_reset_editor();
	editor_line_insert(0, "test", 4);
	editor.cursor_y = 0;
	editor.cursor_x = 0;
	editor_insert_char('X');
	/* Should not create new lines, just insert into existing */
	TEST_ASSERT_INT(1, editor.line_count);
	TEST_ASSERT_INT(5, (int)editor.lines[0].cell_count);
	TEST_ASSERT_INT('X', (int)editor.lines[0].cells[0].codepoint);
}

static void test_selection_get_range_anchor_before_cursor(void)
{
	test_reset_editor();
	editor_line_insert(0, "ABCDE", 5);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 1;
	editor.cursor_y = 0;
	editor.cursor_x = 3;
	int start_y, start_x, end_y, end_x;
	selection_get_range(&start_y, &start_x, &end_y, &end_x);
	TEST_ASSERT_INT(0, start_y);
	TEST_ASSERT_INT(1, start_x);
	TEST_ASSERT_INT(0, end_y);
	TEST_ASSERT_INT(3, end_x);
}

static void test_selection_get_range_anchor_after_cursor(void)
{
	test_reset_editor();
	editor_line_insert(0, "ABCDE", 5);
	editor.selection.active = 1;
	editor.selection.anchor_y = 0;
	editor.selection.anchor_x = 4;
	editor.cursor_y = 0;
	editor.cursor_x = 1;
	int start_y, start_x, end_y, end_x;
	selection_get_range(&start_y, &start_x, &end_y, &end_x);
	TEST_ASSERT_INT(0, start_y);
	TEST_ASSERT_INT(1, start_x);
	TEST_ASSERT_INT(0, end_y);
	TEST_ASSERT_INT(4, end_x);
}

static void test_word_char_class_mcdc_whitespace(void)
{
	test_reset_editor();
	TEST_ASSERT_INT(0, word_char_class(' '));
	TEST_ASSERT_INT(0, word_char_class('\t'));
	TEST_ASSERT_INT(0, word_char_class('\0'));
}

static void test_word_char_class_mcdc_punctuation(void)
{
	test_reset_editor();
	/* Non-whitespace ASCII separator */
	TEST_ASSERT_INT(1, word_char_class('.'));
	TEST_ASSERT_INT(1, word_char_class(','));
	TEST_ASSERT_INT(1, word_char_class('*'));
}

static void test_word_char_class_mcdc_word_char(void)
{
	test_reset_editor();
	/* Not whitespace, not separator => word */
	TEST_ASSERT_INT(2, word_char_class('a'));
	TEST_ASSERT_INT(2, word_char_class('9'));
	/* Non-ASCII codepoint => word */
	TEST_ASSERT_INT(2, word_char_class(0x00E9));
}

/*** Additional tests for completeness ***/

static void test_line_append_cells(void)
{
	test_reset_editor();
	struct line dest, src;
	line_init(&dest, 0);
	line_init(&src, 1);
	line_populate_from_bytes(&dest, "AB", 2);
	line_populate_from_bytes(&src, "CDE", 3);
	line_append_cells(&dest, &src, 1);
	TEST_ASSERT_INT(4, (int)dest.cell_count);
	TEST_ASSERT_INT('A', (int)dest.cells[0].codepoint);
	TEST_ASSERT_INT('B', (int)dest.cells[1].codepoint);
	TEST_ASSERT_INT('D', (int)dest.cells[2].codepoint);
	TEST_ASSERT_INT('E', (int)dest.cells[3].codepoint);
	line_free(&dest);
	line_free(&src);
}

static void test_line_render_width_ascii(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "Hello", 5);
	TEST_ASSERT_INT(5, line_render_width(&ln));
	line_free(&ln);
}

static void test_line_render_width_with_tab(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "\tX", 2);
	/* Tab at column 0 expands to EDIT_TAB_STOP columns, then 'X' is 1 */
	TEST_ASSERT_INT(EDIT_TAB_STOP + 1, line_render_width(&ln));
	line_free(&ln);
}

static void test_line_cell_to_render_column(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "AB\tC", 4);
	/* 'A' at col 0, 'B' at col 1, tab at col 2..EDIT_TAB_STOP-1,
	 * 'C' at col EDIT_TAB_STOP */
	int render_col = line_cell_to_render_column(&ln, 3);
	TEST_ASSERT_INT(EDIT_TAB_STOP, render_col);
	line_free(&ln);
}

static void test_line_render_column_to_cell(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "AB\tC", 4);
	/* Render column EDIT_TAB_STOP should map to cell 3 ('C') */
	int cell = line_render_column_to_cell(&ln, EDIT_TAB_STOP);
	TEST_ASSERT_INT(3, cell);
	line_free(&ln);
}

static void test_line_delete_cell_out_of_bounds(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "AB", 2);
	/* Deleting past the end should be a no-op */
	line_delete_cell(&ln, 5);
	TEST_ASSERT_INT(2, (int)ln.cell_count);
	line_free(&ln);
}

static void test_editor_lines_ensure_capacity(void)
{
	test_reset_editor();
	editor_lines_ensure_capacity(100);
	TEST_ASSERT(editor.line_capacity >= 100,
		    "should have capacity for at least 100 lines");
}

static void test_editor_update_gutter_width(void)
{
	test_reset_editor();
	editor.show_line_numbers = 1;
	/* Insert 1 line and test gutter width */
	editor_line_insert(0, "line", 4);
	editor_update_gutter_width();
	/* 1 line => 1 digit + 1 space = 2 */
	TEST_ASSERT_INT(2, editor.line_number_width);

	/* Add more lines to test wider gutter */
	for (int i = 1; i < 100; i++)
		editor_line_insert(i, "line", 4);
	editor_update_gutter_width();
	/* 100 lines => 3 digits + 1 space = 4 */
	TEST_ASSERT_INT(4, editor.line_number_width);
}

static void test_editor_update_gutter_width_off(void)
{
	test_reset_editor();
	editor.show_line_numbers = 0;
	editor_update_gutter_width();
	TEST_ASSERT_INT(0, editor.line_number_width);
}

static void test_selection_get_range_inactive(void)
{
	test_reset_editor();
	editor.selection.active = 0;
	int start_y, start_x, end_y, end_x;
	int result = selection_get_range(&start_y, &start_x, &end_y, &end_x);
	TEST_ASSERT_INT(0, result);
}

static void test_line_populate_from_bytes_three_byte_utf8(void)
{
	test_reset_editor();
	struct line ln;
	line_init(&ln, 0);
	/* U+4E16 (CJK ideograph) = 0xE4 0xB8 0x96 */
	const char *utf8 = "\xE4\xB8\x96";
	line_populate_from_bytes(&ln, utf8, 3);
	TEST_ASSERT_INT(1, (int)ln.cell_count);
	TEST_ASSERT_INT(0x4E16, (int)ln.cells[0].codepoint);
	line_free(&ln);
}

static void test_editor_rows_to_string(void)
{
	test_reset_editor();
	editor_line_insert(0, "Hello", 5);
	editor_line_insert(1, "World", 5);
	size_t length;
	char *result = editor_rows_to_string(&length);
	TEST_ASSERT(result != NULL, "editor_rows_to_string should not return NULL");
	/* Should be "Hello\nWorld\n" */
	TEST_ASSERT_INT(12, (int)length);
	TEST_ASSERT(memcmp(result, "Hello\nWorld\n", 12) == 0,
		    "should produce 'Hello\\nWorld\\n'");
	free(result);
}

static void test_syntax_select_highlight_python(void)
{
	test_reset_editor();
	editor.filename = strdup("script.py");
	syntax_select_highlight();
	TEST_ASSERT(editor.syntax != NULL, "should find syntax for .py file");
	TEST_ASSERT_STR("python", editor.syntax->filetype);
}

static void test_editor_insert_newline_empty_file(void)
{
	test_reset_editor();
	/* Insert newline into empty file (cursor_y == line_count == 0) */
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_insert_newline();
	TEST_ASSERT(editor.line_count >= 1,
		    "should create at least one line");
}

/*** Test runner ***/

int main(void)
{
	/* Category 1: Cell and Line Operations */
	test_line_init_creates_empty_line();
	test_line_populate_from_bytes_ascii();
	test_line_populate_from_bytes_utf8();
	test_line_populate_from_bytes_empty();
	test_line_populate_from_bytes_three_byte_utf8();
	test_line_insert_cell_at_beginning();
	test_line_insert_cell_at_middle();
	test_line_insert_cell_at_end();
	test_line_delete_cell_first();
	test_line_delete_cell_last();
	test_line_delete_cell_middle();
	test_line_delete_cell_out_of_bounds();
	test_line_to_bytes_roundtrip_ascii();
	test_line_to_bytes_roundtrip_utf8();
	test_cell_display_width_normal();
	test_cell_display_width_tab_col0();
	test_cell_display_width_tab_col3();
	test_cell_display_width_wide_char();
	test_line_ensure_capacity_grows();
	test_line_append_cells();
	test_line_render_width_ascii();
	test_line_render_width_with_tab();
	test_line_cell_to_render_column();
	test_line_render_column_to_cell();

	/* Category 2: Editor Operations */
	test_editor_insert_char_empty_file();
	test_editor_insert_char_end_of_line();
	test_editor_insert_char_middle_of_line();
	test_editor_insert_newline_at_start();
	test_editor_insert_newline_at_end();
	test_editor_insert_newline_in_middle();
	test_editor_insert_newline_auto_indent();
	test_editor_insert_newline_empty_file();
	test_editor_delete_char_end_of_line();
	test_editor_delete_char_joins_lines();
	test_editor_delete_char_noop_at_start();
	test_editor_line_insert_and_delete();
	test_editor_lines_ensure_capacity();
	test_editor_update_gutter_width();
	test_editor_update_gutter_width_off();
	test_editor_rows_to_string();

	/* Category 3: Undo/Redo */
	test_undo_after_insert_char();
	test_redo_after_undo();
	test_undo_after_newline();
	test_undo_after_delete_char();
	test_undo_after_line_join();
	test_new_edit_after_undo_discards_redo();
	test_undo_empty_stack_shows_message();
	test_redo_empty_stack_shows_message();
	test_undo_group_boundary_on_line_change();

	/* Category 4: Selection and Clipboard */
	test_selection_start_sets_anchor();
	test_selection_get_range_cursor_after_anchor();
	test_selection_get_range_cursor_before_anchor();
	test_selection_get_range_inactive();
	test_selection_to_string_single_line();
	test_selection_to_string_multi_line();
	test_selection_delete_single_line();
	test_selection_delete_multi_line();
	test_clipboard_store_and_retrieve();
	test_clipboard_store_multiline();
	test_clipboard_clear_frees_memory();
	test_word_char_class_whitespace();
	test_word_char_class_punctuation();
	test_word_char_class_word();

	/* Category 5: Search */
	test_editor_memmem_case_sensitive();
	test_editor_memmem_case_insensitive();
	test_editor_memmem_no_match();
	test_editor_memmem_empty_needle();
	test_editor_memmem_case_sensitive_no_match();

	/* Category 6: Syntax */
	test_syntax_is_separator_space();
	test_syntax_is_separator_letter();
	test_syntax_is_separator_punctuation();
	test_syntax_is_separator_null();
	test_syntax_select_highlight_c_file();
	test_syntax_select_highlight_unknown();
	test_syntax_select_highlight_python();
	test_cursor_next_word();
	test_cursor_prev_word();

	/* Category 7: MC/DC Decision Coverage */
	test_line_ensure_warm_cold_to_warm();
	test_line_ensure_warm_already_warm();
	test_editor_scroll_cursor_above_margin();
	test_editor_scroll_cursor_below_margin();
	test_editor_scroll_cursor_in_safe_zone();
	test_editor_delete_char_cursor_x_positive();
	test_editor_delete_char_cursor_x_zero();
	test_editor_insert_char_creates_line_at_end();
	test_editor_insert_char_within_existing_lines();
	test_selection_get_range_anchor_before_cursor();
	test_selection_get_range_anchor_after_cursor();
	test_word_char_class_mcdc_whitespace();
	test_word_char_class_mcdc_punctuation();
	test_word_char_class_mcdc_word_char();

	printf("\n%d tests run, %d passed, %d failed\n",
	       tests_run, tests_passed, tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
