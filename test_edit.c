/*
 * test_edit.c - Unit tests for the edit text editor
 *
 * Includes edit.c directly with main renamed to avoid symbol collision.
 * Uses the same test framework as lib/gstr/test/test_gstr.c.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

/* Include edit.c with main renamed to avoid duplicate symbol */
#define main editor_main
#include "edit.c"
#undef main

/*** Test Framework ***/

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name)                                                              \
  do {                                                                         \
    printf("  %-50s", #name);                                                  \
    test_##name();                                                             \
    printf(" PASS\n");                                                         \
    tests_passed++;                                                            \
  } while (0)

#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", #cond,         \
             __FILE__, __LINE__);                                              \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    long long _a = (long long)(a);                                             \
    long long _b = (long long)(b);                                             \
    if (_a != _b) {                                                            \
      printf(" FAIL\n    Expected %lld, got %lld\n    at %s:%d\n", _b,         \
             _a, __FILE__, __LINE__);                                          \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ_SIZE(a, b)                                                   \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      printf(" FAIL\n    Expected %zu, got %zu\n    at %s:%d\n", (size_t)(b),  \
             (size_t)(a), __FILE__, __LINE__);                                 \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_STR_EQ(a, b)                                                    \
  do {                                                                         \
    if (strcmp((a), (b)) != 0) {                                               \
      printf(" FAIL\n    Expected \"%s\", got \"%s\"\n    at %s:%d\n", (b),    \
             (a), __FILE__, __LINE__);                                         \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NULL(ptr)                                                       \
  do {                                                                         \
    if ((ptr) != NULL) {                                                       \
      printf(" FAIL\n    Expected NULL, got non-NULL\n    at %s:%d\n",         \
             __FILE__, __LINE__);                                              \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
  do {                                                                         \
    if ((ptr) == NULL) {                                                       \
      printf(" FAIL\n    Expected non-NULL, got NULL\n    at %s:%d\n",         \
             __FILE__, __LINE__);                                              \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

/*** Test Helpers ***/

/* Reset editor global state without calling editor_init() (which does
 * ioctl/signal setup). Called before every test touching global state. */
static void test_reset_editor(void)
{
	memset(&editor, 0, sizeof(editor));
	editor.screen_rows = 24;
	editor.screen_columns = 80;
	editor_set_theme(0);
	editor.file_descriptor = -1;
	editor.search_last_match = -1;
	editor.search_last_match_offset = -1;
	editor.search_direction = 1;
	editor.search_saved_syntax = NULL;
	editor.search_saved_syntax_count = 0;
	editor.mode = MODE_NORMAL;
	editor.quit_after_save = 0;
	editor.show_line_numbers = 1;
	editor_update_gutter_width();
}

/* Create a HOT line from a string. Caller must line_free(). */
static struct line test_make_line(const char *text)
{
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, text, strlen(text));
	return ln;
}

/* Add a line to the editor at the end. */
static void test_add_editor_line(const char *text)
{
	editor_line_insert(editor.line_count, text, strlen(text));
}

/* Populate input_buffer directly, bypassing stdin read. */
static void test_populate_input_buffer(const unsigned char *data, int length)
{
	memset(&editor.input, 0, sizeof(editor.input));
	memcpy(editor.input.data, data, length);
	editor.input.read_position = 0;
	editor.input.count = length;
}

/* Free all editor lines and the array. */
static void test_free_editor_lines(void)
{
	for (int i = 0; i < editor.line_count; i++)
		line_free(&editor.lines[i]);
	free(editor.lines);
	editor.lines = NULL;
	editor.line_count = 0;
}

/*** Section 1: Append Buffer ***/

TEST(append_buffer_init_zeroes)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	ASSERT_NULL(ab.buffer);
	ASSERT_EQ(ab.length, 0);
	ASSERT_EQ(ab.capacity, 0);
}

TEST(append_buffer_single_write)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	append_buffer_write(&ab, "hello", 5);
	ASSERT_EQ(ab.length, 5);
	ASSERT(memcmp(ab.buffer, "hello", 5) == 0);
	append_buffer_free(&ab);
}

TEST(append_buffer_multiple_writes)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	append_buffer_write(&ab, "foo", 3);
	append_buffer_write(&ab, "bar", 3);
	ASSERT_EQ(ab.length, 6);
	ASSERT(memcmp(ab.buffer, "foobar", 6) == 0);
	append_buffer_free(&ab);
}

TEST(append_buffer_growth_on_overflow)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	/* Write enough to force multiple capacity doublings */
	char data[2048];
	memset(data, 'x', sizeof(data));
	append_buffer_write(&ab, data, sizeof(data));
	ASSERT_EQ(ab.length, 2048);
	ASSERT(ab.capacity >= 2048);
	append_buffer_free(&ab);
}

TEST(append_buffer_free_after_writes)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	append_buffer_write(&ab, "test", 4);
	append_buffer_free(&ab);
	/* No crash = pass */
}

TEST(append_buffer_fg_color_red)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	append_buffer_write_color(&ab, "FF0000");
	ASSERT(ab.length > 0);
	/* Should contain ESC[38;2;255;0;0m */
	ASSERT(strstr(ab.buffer, "255;0;0") != NULL ||
	       memcmp(ab.buffer, "\x1b[38;2;255;0;0m", ab.length) == 0);
	append_buffer_free(&ab);
}

TEST(append_buffer_bg_color_white)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	append_buffer_write_background(&ab, "FFFFFF");
	ASSERT(ab.length > 0);
	ASSERT(strstr(ab.buffer, "255;255;255") != NULL ||
	       memcmp(ab.buffer, "\x1b[48;2;255;255;255m", ab.length) == 0);
	append_buffer_free(&ab);
}

TEST(append_buffer_hex_parsing)
{
	struct append_buffer ab = APPEND_BUFFER_INIT;
	append_buffer_write_color(&ab, "1A2B3C");
	/* 0x1A=26, 0x2B=43, 0x3C=60 */
	ASSERT(strstr(ab.buffer, "26;43;60") != NULL);
	append_buffer_free(&ab);
}

/*** Section 2: Pure Logic ***/

TEST(cell_display_width_ascii)
{
	struct cell c = {.codepoint = 'A'};
	ASSERT_EQ(cell_display_width(&c, 0), 1);
}

TEST(cell_display_width_tab_col0)
{
	struct cell c = {.codepoint = '\t'};
	ASSERT_EQ(cell_display_width(&c, 0), EDIT_TAB_STOP);
}

TEST(cell_display_width_tab_col3)
{
	struct cell c = {.codepoint = '\t'};
	ASSERT_EQ(cell_display_width(&c, 3), EDIT_TAB_STOP - 3);
}

TEST(cell_display_width_cjk)
{
	/* U+4E2D (中) is a wide character */
	struct cell c = {.codepoint = 0x4E2D};
	ASSERT_EQ(cell_display_width(&c, 0), 2);
}

TEST(cell_display_width_combining)
{
	/* U+0301 combining acute accent — forced minimum 1 */
	struct cell c = {.codepoint = 0x0301};
	ASSERT_EQ(cell_display_width(&c, 0), 1);
}

TEST(syntax_is_separator_null)
{
	ASSERT_EQ(syntax_is_separator('\0'), 1);
}

TEST(syntax_is_separator_space)
{
	ASSERT_EQ(syntax_is_separator(' '), 1);
}

TEST(syntax_is_separator_comma)
{
	ASSERT_EQ(syntax_is_separator(','), 1);
}

TEST(syntax_is_separator_letter)
{
	ASSERT_EQ(syntax_is_separator('a'), 0);
}

TEST(syntax_is_separator_digit)
{
	ASSERT_EQ(syntax_is_separator('5'), 0);
}

TEST(syntax_is_separator_negative)
{
	ASSERT_EQ(syntax_is_separator(-1), 0);
}

/*** Section 3: Line/Cell Operations ***/

TEST(line_init_defaults)
{
	struct line ln;
	line_init(&ln, 5);
	ASSERT_NOT_NULL(ln.cells);
	ASSERT_EQ(ln.cell_count, 0);
	ASSERT_EQ(ln.cell_capacity, LINE_INITIAL_CAPACITY);
	ASSERT_EQ(ln.line_index, 5);
	ASSERT_EQ(ln.temperature, LINE_HOT);
	ASSERT_EQ(ln.open_comment, 0);
	line_free(&ln);
}

TEST(line_free_hot)
{
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "hello", 5);
	line_free(&ln);
	ASSERT_NULL(ln.cells);
	ASSERT_EQ(ln.cell_count, 0);
	ASSERT_EQ(ln.temperature, LINE_COLD);
}

TEST(line_free_cold)
{
	struct line ln = {0};
	ln.temperature = LINE_COLD;
	line_free(&ln);
	/* Should not crash trying to free NULL */
	ASSERT_EQ(ln.temperature, LINE_COLD);
}

TEST(line_populate_ascii)
{
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "abc", 3);
	ASSERT_EQ(ln.cell_count, 3);
	ASSERT_EQ(ln.cells[0].codepoint, 'a');
	ASSERT_EQ(ln.cells[1].codepoint, 'b');
	ASSERT_EQ(ln.cells[2].codepoint, 'c');
	line_free(&ln);
}

TEST(line_populate_utf8_2byte)
{
	/* é = U+00E9 = 0xC3 0xA9 */
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "\xC3\xA9", 2);
	ASSERT_EQ(ln.cell_count, 1);
	ASSERT_EQ(ln.cells[0].codepoint, 0xE9);
	line_free(&ln);
}

TEST(line_populate_utf8_3byte)
{
	/* € = U+20AC = 0xE2 0x82 0xAC */
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "\xE2\x82\xAC", 3);
	ASSERT_EQ(ln.cell_count, 1);
	ASSERT_EQ(ln.cells[0].codepoint, 0x20AC);
	line_free(&ln);
}

TEST(line_populate_utf8_4byte)
{
	/* 😀 = U+1F600 = 0xF0 0x9F 0x98 0x80 */
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "\xF0\x9F\x98\x80", 4);
	ASSERT_EQ(ln.cell_count, 1);
	ASSERT_EQ(ln.cells[0].codepoint, 0x1F600);
	line_free(&ln);
}

TEST(line_populate_invalid_utf8)
{
	/* Invalid byte 0xFF should produce replacement char U+FFFD */
	struct line ln;
	line_init(&ln, 0);
	line_populate_from_bytes(&ln, "\xFF", 1);
	ASSERT_EQ(ln.cell_count, 1);
	ASSERT_EQ(ln.cells[0].codepoint, UTF8_REPLACEMENT_CHAR);
	line_free(&ln);
}

TEST(line_to_bytes_ascii)
{
	struct line ln = test_make_line("hello");
	size_t len;
	char *bytes = line_to_bytes(&ln, &len);
	ASSERT_EQ_SIZE(len, 5);
	ASSERT_STR_EQ(bytes, "hello");
	free(bytes);
	line_free(&ln);
}

TEST(line_roundtrip_ascii)
{
	struct line ln = test_make_line("test123");
	size_t len;
	char *bytes = line_to_bytes(&ln, &len);
	ASSERT_STR_EQ(bytes, "test123");
	free(bytes);
	line_free(&ln);
}

TEST(line_roundtrip_unicode)
{
	const char *input = "h\xC3\xA9llo \xE2\x82\xAC";
	struct line ln = test_make_line(input);
	size_t len;
	char *bytes = line_to_bytes(&ln, &len);
	ASSERT_STR_EQ(bytes, input);
	free(bytes);
	line_free(&ln);
}

TEST(line_insert_cell_start)
{
	struct line ln = test_make_line("bc");
	struct cell c = {.codepoint = 'a'};
	line_insert_cell(&ln, 0, c);
	ASSERT_EQ(ln.cell_count, 3);
	ASSERT_EQ(ln.cells[0].codepoint, 'a');
	ASSERT_EQ(ln.cells[1].codepoint, 'b');
	line_free(&ln);
}

TEST(line_insert_cell_end)
{
	struct line ln = test_make_line("ab");
	struct cell c = {.codepoint = 'c'};
	line_insert_cell(&ln, 2, c);
	ASSERT_EQ(ln.cell_count, 3);
	ASSERT_EQ(ln.cells[2].codepoint, 'c');
	line_free(&ln);
}

TEST(line_insert_cell_middle)
{
	struct line ln = test_make_line("ac");
	struct cell c = {.codepoint = 'b'};
	line_insert_cell(&ln, 1, c);
	ASSERT_EQ(ln.cell_count, 3);
	ASSERT_EQ(ln.cells[1].codepoint, 'b');
	line_free(&ln);
}

TEST(line_delete_cell_start)
{
	struct line ln = test_make_line("abc");
	line_delete_cell(&ln, 0);
	ASSERT_EQ(ln.cell_count, 2);
	ASSERT_EQ(ln.cells[0].codepoint, 'b');
	line_free(&ln);
}

TEST(line_delete_cell_end)
{
	struct line ln = test_make_line("abc");
	line_delete_cell(&ln, 2);
	ASSERT_EQ(ln.cell_count, 2);
	ASSERT_EQ(ln.cells[1].codepoint, 'b');
	line_free(&ln);
}

TEST(line_delete_cell_out_of_bounds)
{
	struct line ln = test_make_line("ab");
	line_delete_cell(&ln, 10);
	ASSERT_EQ(ln.cell_count, 2);
	line_free(&ln);
}

TEST(line_append_cells_basic)
{
	struct line dest = test_make_line("hello");
	struct line src = test_make_line(" world");
	line_append_cells(&dest, &src, 0);
	ASSERT_EQ(dest.cell_count, 11);
	ASSERT_EQ(dest.cells[5].codepoint, ' ');
	ASSERT_EQ(dest.cells[6].codepoint, 'w');
	line_free(&dest);
	line_free(&src);
}

TEST(line_append_cells_from_offset)
{
	struct line dest = test_make_line("hello");
	struct line src = test_make_line("xxworld");
	line_append_cells(&dest, &src, 2);
	ASSERT_EQ(dest.cell_count, 10);
	ASSERT_EQ(dest.cells[5].codepoint, 'w');
	line_free(&dest);
	line_free(&src);
}

TEST(line_append_cells_past_end)
{
	struct line dest = test_make_line("hello");
	struct line src = test_make_line("ab");
	line_append_cells(&dest, &src, 10);
	ASSERT_EQ(dest.cell_count, 5);
	line_free(&dest);
	line_free(&src);
}

TEST(line_ensure_capacity_no_grow)
{
	struct line ln = test_make_line("hi");
	uint32_t old_cap = ln.cell_capacity;
	line_ensure_capacity(&ln, 4);
	ASSERT(ln.cell_capacity >= 4);
	ASSERT(ln.cell_capacity == old_cap);
	line_free(&ln);
}

TEST(line_ensure_capacity_doubling)
{
	struct line ln;
	line_init(&ln, 0);
	uint32_t old_cap = ln.cell_capacity;
	line_ensure_capacity(&ln, old_cap + 1);
	ASSERT(ln.cell_capacity >= old_cap + 1);
	ASSERT(ln.cell_capacity >= old_cap * 2);
	line_free(&ln);
}

TEST(line_render_width_ascii)
{
	struct line ln = test_make_line("hello");
	ASSERT_EQ(line_render_width(&ln), 5);
	line_free(&ln);
}

TEST(line_render_width_tabs)
{
	struct line ln = test_make_line("\t");
	ASSERT_EQ(line_render_width(&ln), EDIT_TAB_STOP);
	line_free(&ln);
}

TEST(line_render_width_wide_chars)
{
	/* 中 = U+4E2D, width 2 */
	struct line ln = test_make_line("\xe4\xb8\xad");
	ASSERT_EQ(line_render_width(&ln), 2);
	line_free(&ln);
}

/*** Section 4: Grapheme Navigation ***/

TEST(cursor_next_grapheme_ascii)
{
	struct line ln = test_make_line("abc");
	ASSERT_EQ(cursor_next_grapheme(&ln, 0), 1);
	ASSERT_EQ(cursor_next_grapheme(&ln, 1), 2);
	line_free(&ln);
}

TEST(cursor_next_grapheme_emoji)
{
	/* Single emoji: 😀 U+1F600 */
	struct line ln = test_make_line("\xF0\x9F\x98\x80");
	ASSERT_EQ(ln.cell_count, 1);
	ASSERT_EQ(cursor_next_grapheme(&ln, 0), 1);
	line_free(&ln);
}

TEST(cursor_next_grapheme_zwj)
{
	/* ZWJ family: 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467 */
	const char *family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
	struct line ln = test_make_line(family);
	/* Should be 5 cells (3 emoji + 2 ZWJ), next grapheme skips all */
	int next = cursor_next_grapheme(&ln, 0);
	ASSERT(next == (int)ln.cell_count);
	line_free(&ln);
}

TEST(cursor_next_grapheme_flag)
{
	/* Flag: 🇺🇸 = U+1F1FA U+1F1F8 */
	const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
	struct line ln = test_make_line(flag);
	ASSERT_EQ(ln.cell_count, 2);
	int next = cursor_next_grapheme(&ln, 0);
	ASSERT_EQ(next, 2);
	line_free(&ln);
}

TEST(cursor_next_grapheme_past_end)
{
	struct line ln = test_make_line("a");
	ASSERT_EQ(cursor_next_grapheme(&ln, 1), 1);
	ASSERT_EQ(cursor_next_grapheme(&ln, 5), 1);
	line_free(&ln);
}

TEST(cursor_prev_grapheme_ascii)
{
	struct line ln = test_make_line("abc");
	ASSERT_EQ(cursor_prev_grapheme(&ln, 3), 2);
	ASSERT_EQ(cursor_prev_grapheme(&ln, 2), 1);
	line_free(&ln);
}

TEST(cursor_prev_grapheme_multi_cell)
{
	/* Flag: U+1F1FA U+1F1F8 — 2 cells, 1 grapheme */
	const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
	struct line ln = test_make_line(flag);
	ASSERT_EQ(cursor_prev_grapheme(&ln, 2), 0);
	line_free(&ln);
}

TEST(cursor_prev_grapheme_at_zero)
{
	struct line ln = test_make_line("abc");
	ASSERT_EQ(cursor_prev_grapheme(&ln, 0), 0);
	line_free(&ln);
}

TEST(grapheme_display_width_ascii)
{
	struct line ln = test_make_line("a");
	ASSERT_EQ(grapheme_display_width(&ln, 0, 1), 1);
	line_free(&ln);
}

TEST(grapheme_display_width_cjk)
{
	struct line ln = test_make_line("\xe4\xb8\xad");
	ASSERT_EQ(grapheme_display_width(&ln, 0, 1), 2);
	line_free(&ln);
}

TEST(grapheme_display_width_flag)
{
	const char *flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
	struct line ln = test_make_line(flag);
	ASSERT_EQ(grapheme_display_width(&ln, 0, 2), 2);
	line_free(&ln);
}

TEST(grapheme_display_width_zwj)
{
	/* 👨‍👩‍👧 */
	const char *family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
	struct line ln = test_make_line(family);
	int end = cursor_next_grapheme(&ln, 0);
	ASSERT_EQ(grapheme_display_width(&ln, 0, end), 2);
	line_free(&ln);
}

TEST(grapheme_display_width_combining)
{
	/* e + combining acute = 2 cells, 1 grapheme */
	struct line ln = test_make_line("e\xCC\x81");
	ASSERT_EQ(ln.cell_count, 2);
	int end = cursor_next_grapheme(&ln, 0);
	ASSERT_EQ(grapheme_display_width(&ln, 0, end), 1);
	line_free(&ln);
}

TEST(line_cell_to_render_ascii)
{
	struct line ln = test_make_line("hello");
	ASSERT_EQ(line_cell_to_render_column(&ln, 3), 3);
	line_free(&ln);
}

TEST(line_cell_to_render_tab)
{
	struct line ln = test_make_line("\tabc");
	/* Tab at col 0 = 8 columns, 'a' at render col 8 */
	ASSERT_EQ(line_cell_to_render_column(&ln, 1), EDIT_TAB_STOP);
	line_free(&ln);
}

TEST(line_cell_to_render_wide)
{
	/* 中 (width 2) + a */
	struct line ln = test_make_line("\xe4\xb8\xad" "a");
	ASSERT_EQ(line_cell_to_render_column(&ln, 1), 2);
	ASSERT_EQ(line_cell_to_render_column(&ln, 2), 3);
	line_free(&ln);
}

TEST(line_render_to_cell_ascii)
{
	struct line ln = test_make_line("hello");
	ASSERT_EQ(line_render_column_to_cell(&ln, 3), 3);
	line_free(&ln);
}

TEST(line_render_to_cell_tab)
{
	struct line ln = test_make_line("\tabc");
	/* Render col 0-7 = tab (cell 0), col 8 = 'a' (cell 1) */
	ASSERT_EQ(line_render_column_to_cell(&ln, 0), 0);
	ASSERT_EQ(line_render_column_to_cell(&ln, EDIT_TAB_STOP), 1);
	line_free(&ln);
}

TEST(line_cell_render_roundtrip)
{
	struct line ln = test_make_line("hello world");
	for (int i = 0; i <= (int)ln.cell_count; i++) {
		int render = line_cell_to_render_column(&ln, i);
		int cell = line_render_column_to_cell(&ln, render);
		ASSERT_EQ(cell, i);
	}
	line_free(&ln);
}

/*** Section 5: Input Buffer & Key Decoding ***/

TEST(input_buffer_available_empty)
{
	memset(&editor.input, 0, sizeof(editor.input));
	ASSERT_EQ(input_buffer_available(), 0);
}

TEST(input_buffer_available_with_data)
{
	unsigned char data[] = {'a', 'b', 'c'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(input_buffer_available(), 3);
}

TEST(input_buffer_read_byte_basic)
{
	unsigned char data[] = {'x', 'y'};
	test_populate_input_buffer(data, 2);
	unsigned char out;
	int ok = input_buffer_read_byte(&out);
	ASSERT_EQ(ok, 1);
	ASSERT_EQ(out, 'x');
	ASSERT_EQ(input_buffer_available(), 1);
}

TEST(input_buffer_drain_resets)
{
	unsigned char data[] = {'a'};
	test_populate_input_buffer(data, 1);
	unsigned char out;
	input_buffer_read_byte(&out);
	ASSERT_EQ(input_buffer_available(), 0);
	ASSERT_EQ(editor.input.read_position, 0);
}

TEST(input_buffer_read_empty)
{
	memset(&editor.input, 0, sizeof(editor.input));
	unsigned char out;
	ASSERT_EQ(input_buffer_read_byte(&out), 0);
}

TEST(decode_ascii_a)
{
	unsigned char data[] = {'a'};
	test_populate_input_buffer(data, 1);
	ASSERT_EQ(terminal_decode_key().key, 'a');
}

TEST(decode_ctrl_a)
{
	unsigned char data[] = {CTRL_KEY('a')};
	test_populate_input_buffer(data, 1);
	ASSERT_EQ(terminal_decode_key().key, CTRL_KEY('a'));
}

TEST(decode_arrow_up)
{
	unsigned char data[] = {ESC_KEY, '[', 'A'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, ARROW_UP);
}

TEST(decode_arrow_down)
{
	unsigned char data[] = {ESC_KEY, '[', 'B'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, ARROW_DOWN);
}

TEST(decode_arrow_right)
{
	unsigned char data[] = {ESC_KEY, '[', 'C'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, ARROW_RIGHT);
}

TEST(decode_arrow_left)
{
	unsigned char data[] = {ESC_KEY, '[', 'D'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, ARROW_LEFT);
}

TEST(decode_home_bracket_H)
{
	unsigned char data[] = {ESC_KEY, '[', 'H'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, HOME_KEY);
}

TEST(decode_end_bracket_F)
{
	unsigned char data[] = {ESC_KEY, '[', 'F'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, END_KEY);
}

TEST(decode_home_tilde)
{
	unsigned char data[] = {ESC_KEY, '[', '1', '~'};
	test_populate_input_buffer(data, 4);
	ASSERT_EQ(terminal_decode_key().key, HOME_KEY);
}

TEST(decode_home_O_H)
{
	unsigned char data[] = {ESC_KEY, 'O', 'H'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, HOME_KEY);
}

TEST(decode_end_O_F)
{
	unsigned char data[] = {ESC_KEY, 'O', 'F'};
	test_populate_input_buffer(data, 3);
	ASSERT_EQ(terminal_decode_key().key, END_KEY);
}

TEST(decode_del)
{
	unsigned char data[] = {ESC_KEY, '[', '3', '~'};
	test_populate_input_buffer(data, 4);
	ASSERT_EQ(terminal_decode_key().key, DEL_KEY);
}

TEST(decode_page_up)
{
	unsigned char data[] = {ESC_KEY, '[', '5', '~'};
	test_populate_input_buffer(data, 4);
	ASSERT_EQ(terminal_decode_key().key, PAGE_UP);
}

TEST(decode_page_down)
{
	unsigned char data[] = {ESC_KEY, '[', '6', '~'};
	test_populate_input_buffer(data, 4);
	ASSERT_EQ(terminal_decode_key().key, PAGE_DOWN);
}

TEST(decode_f11)
{
	unsigned char data[] = {ESC_KEY, '[', '2', '3', '~'};
	test_populate_input_buffer(data, 5);
	ASSERT_EQ(terminal_decode_key().key, F11_KEY);
}

TEST(decode_alt_f)
{
	unsigned char data[] = {ESC_KEY, 'f'};
	test_populate_input_buffer(data, 2);
	struct input_event ev = terminal_decode_key();
	ASSERT_EQ(ev.key, ALT_KEY('f'));
}

TEST(decode_utf8_2byte)
{
	/* é = U+00E9 = 0xC3 0xA9 */
	unsigned char data[] = {0xC3, 0xA9};
	test_populate_input_buffer(data, 2);
	ASSERT_EQ(terminal_decode_key().key, 0xE9);
}

TEST(decode_utf8_4byte)
{
	/* 😀 = U+1F600 */
	unsigned char data[] = {0xF0, 0x9F, 0x98, 0x80};
	test_populate_input_buffer(data, 4);
	ASSERT_EQ(terminal_decode_key().key, 0x1F600);
}

TEST(decode_empty_buffer)
{
	memset(&editor.input, 0, sizeof(editor.input));
	ASSERT_EQ(terminal_decode_key().key, -1);
}

TEST(decode_lone_esc)
{
	unsigned char data[] = {ESC_KEY};
	test_populate_input_buffer(data, 1);
	ASSERT_EQ(terminal_decode_key().key, ESC_KEY);
}

TEST(decode_mouse_left_click)
{
	test_reset_editor();
	/* SGR mouse: ESC [ < 0;10;5 M */
	unsigned char data[] = {ESC_KEY, '[', '<', '0', ';', '1', '0', ';', '5', 'M'};
	test_populate_input_buffer(data, sizeof(data));
	struct input_event ev = terminal_decode_key();
	ASSERT_EQ(ev.key, MOUSE_LEFT_BUTTON_PRESSED);
	/* Column is adjusted by line_number_width + 1 */
	/* mouse_x = 10 - line_number_width - 1 */
	ASSERT_EQ(ev.mouse_y, 4); /* 5 - 1 = 4 */
}

TEST(decode_mouse_scroll_up)
{
	test_reset_editor();
	unsigned char data[] = {ESC_KEY, '[', '<', '6', '4', ';', '1', ';', '1', 'M'};
	test_populate_input_buffer(data, sizeof(data));
	ASSERT_EQ(terminal_decode_key().key, MOUSE_SCROLL_UP);
}

TEST(decode_mouse_scroll_down)
{
	test_reset_editor();
	unsigned char data[] = {ESC_KEY, '[', '<', '6', '5', ';', '1', ';', '1', 'M'};
	test_populate_input_buffer(data, sizeof(data));
	ASSERT_EQ(terminal_decode_key().key, MOUSE_SCROLL_DOWN);
}

TEST(decode_mouse_release)
{
	test_reset_editor();
	/* Mouse release uses lowercase 'm' */
	unsigned char data[] = {ESC_KEY, '[', '<', '0', ';', '1', ';', '1', 'm'};
	test_populate_input_buffer(data, sizeof(data));
	struct input_event ev = terminal_decode_key();
	/* Release events for button 0 should not return a mouse event */
	ASSERT(ev.key == ESC_KEY || ev.key != MOUSE_LEFT_BUTTON_PRESSED);
}

/*** Section 6: Editor Operations ***/

TEST(editor_line_insert_first)
{
	test_reset_editor();
	test_add_editor_line("hello");
	ASSERT_EQ(editor.line_count, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'h');
	test_free_editor_lines();
}

TEST(editor_line_insert_at_end)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	ASSERT_EQ(editor.line_count, 2);
	ASSERT_EQ(editor.lines[1].cells[0].codepoint, 's');
	test_free_editor_lines();
}

TEST(editor_line_insert_at_start)
{
	test_reset_editor();
	test_add_editor_line("second");
	editor_line_insert(0, "first", 5);
	ASSERT_EQ(editor.line_count, 2);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'f');
	ASSERT_EQ(editor.lines[1].cells[0].codepoint, 's');
	test_free_editor_lines();
}

TEST(editor_line_insert_middle)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("third");
	editor_line_insert(1, "second", 6);
	ASSERT_EQ(editor.line_count, 3);
	ASSERT_EQ(editor.lines[1].cells[0].codepoint, 's');
	test_free_editor_lines();
}

TEST(editor_line_delete_first)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor_line_delete(0);
	ASSERT_EQ(editor.line_count, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 's');
	test_free_editor_lines();
}

TEST(editor_line_delete_last)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor_line_delete(1);
	ASSERT_EQ(editor.line_count, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'f');
	test_free_editor_lines();
}

TEST(editor_line_delete_out_of_bounds)
{
	test_reset_editor();
	test_add_editor_line("only");
	editor_line_delete(5);
	ASSERT_EQ(editor.line_count, 1);
	test_free_editor_lines();
}

TEST(editor_insert_char_basic)
{
	test_reset_editor();
	test_add_editor_line("hllo");
	editor.cursor_x = 1;
	editor.cursor_y = 0;
	editor_insert_char('e');
	ASSERT_EQ(editor.lines[0].cell_count, 5);
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 'e');
	ASSERT_EQ(editor.cursor_x, 2);
	test_free_editor_lines();
}

TEST(editor_insert_char_at_end)
{
	test_reset_editor();
	test_add_editor_line("ab");
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_insert_char('c');
	ASSERT_EQ(editor.lines[0].cell_count, 3);
	ASSERT_EQ(editor.lines[0].cells[2].codepoint, 'c');
	test_free_editor_lines();
}

TEST(editor_insert_char_into_empty)
{
	test_reset_editor();
	/* No lines — insert_char should create one */
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_insert_char('a');
	ASSERT_EQ(editor.line_count, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'a');
	test_free_editor_lines();
}

TEST(editor_insert_char_unicode)
{
	test_reset_editor();
	test_add_editor_line("ab");
	editor.cursor_x = 1;
	editor.cursor_y = 0;
	editor_insert_char(0xE9); /* é */
	ASSERT_EQ(editor.lines[0].cell_count, 3);
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 0xE9);
	test_free_editor_lines();
}

TEST(editor_insert_newline_at_start)
{
	test_reset_editor();
	test_add_editor_line("hello");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_insert_newline();
	ASSERT_EQ(editor.line_count, 2);
	ASSERT_EQ(editor.lines[0].cell_count, 0);
	ASSERT_EQ(editor.lines[1].cells[0].codepoint, 'h');
	ASSERT_EQ(editor.cursor_y, 1);
	ASSERT_EQ(editor.cursor_x, 0);
	test_free_editor_lines();
}

TEST(editor_insert_newline_middle)
{
	test_reset_editor();
	test_add_editor_line("hello");
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_insert_newline();
	ASSERT_EQ(editor.line_count, 2);
	ASSERT_EQ(editor.lines[0].cell_count, 2);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'h');
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 'e');
	ASSERT_EQ(editor.lines[1].cells[0].codepoint, 'l');
	test_free_editor_lines();
}

TEST(editor_insert_newline_at_end)
{
	test_reset_editor();
	test_add_editor_line("hello");
	editor.cursor_x = 5;
	editor.cursor_y = 0;
	editor_insert_newline();
	ASSERT_EQ(editor.line_count, 2);
	ASSERT_EQ(editor.lines[0].cell_count, 5);
	ASSERT_EQ(editor.lines[1].cell_count, 0);
	test_free_editor_lines();
}

TEST(editor_insert_newline_unicode)
{
	test_reset_editor();
	/* "hé" in UTF-8 */
	test_add_editor_line("h\xC3\xA9");
	editor.cursor_x = 1;
	editor.cursor_y = 0;
	editor_insert_newline();
	ASSERT_EQ(editor.line_count, 2);
	ASSERT_EQ(editor.lines[0].cell_count, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'h');
	ASSERT_EQ(editor.lines[1].cells[0].codepoint, 0xE9);
	test_free_editor_lines();
}

TEST(editor_delete_char_basic)
{
	test_reset_editor();
	test_add_editor_line("abc");
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_delete_char();
	ASSERT_EQ(editor.lines[0].cell_count, 2);
	ASSERT_EQ(editor.cursor_x, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'a');
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 'c');
	test_free_editor_lines();
}

TEST(editor_delete_char_merge_lines)
{
	test_reset_editor();
	test_add_editor_line("hello");
	test_add_editor_line("world");
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_delete_char();
	ASSERT_EQ(editor.line_count, 1);
	ASSERT_EQ(editor.lines[0].cell_count, 10);
	ASSERT_EQ(editor.cursor_x, 5);
	ASSERT_EQ(editor.cursor_y, 0);
	test_free_editor_lines();
}

TEST(editor_delete_char_file_start)
{
	test_reset_editor();
	test_add_editor_line("abc");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_delete_char();
	/* No-op at file start */
	ASSERT_EQ(editor.lines[0].cell_count, 3);
	test_free_editor_lines();
}

TEST(editor_delete_char_past_end)
{
	test_reset_editor();
	/* cursor_y == line_count means past end */
	editor.cursor_y = 0;
	editor.cursor_x = 0;
	editor_delete_char();
	/* No-op */
}

TEST(editor_delete_char_grapheme)
{
	test_reset_editor();
	/* Flag emoji: 🇺🇸 = 2 cells, but 1 grapheme */
	test_add_editor_line("a\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8");
	/* 'a' at cell 0, U+1F1FA at cell 1, U+1F1F8 at cell 2 */
	editor.cursor_x = 3;
	editor.cursor_y = 0;
	editor_delete_char();
	/* Should delete the entire flag grapheme (2 cells) */
	ASSERT_EQ(editor.lines[0].cell_count, 1);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'a');
	test_free_editor_lines();
}

TEST(editor_update_gutter_hidden)
{
	test_reset_editor();
	editor.show_line_numbers = 0;
	editor_update_gutter_width();
	ASSERT_EQ(editor.line_number_width, 0);
}

TEST(editor_update_gutter_single_digit)
{
	test_reset_editor();
	test_add_editor_line("line1");
	editor_update_gutter_width();
	/* 1 line = 1 digit + 1 padding = 2 */
	ASSERT_EQ(editor.line_number_width, 2);
	test_free_editor_lines();
}

TEST(editor_update_gutter_two_digits)
{
	test_reset_editor();
	for (int i = 0; i < 10; i++)
		test_add_editor_line("line");
	editor_update_gutter_width();
	/* 10 lines = 2 digits + 1 = 3 */
	ASSERT_EQ(editor.line_number_width, 3);
	test_free_editor_lines();
}

TEST(editor_update_gutter_three_digits)
{
	test_reset_editor();
	for (int i = 0; i < 100; i++)
		test_add_editor_line("line");
	editor_update_gutter_width();
	/* 100 lines = 3 digits + 1 = 4 */
	ASSERT_EQ(editor.line_number_width, 4);
	test_free_editor_lines();
}

TEST(editor_move_cursor_left)
{
	test_reset_editor();
	test_add_editor_line("abc");
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor_move_cursor((struct input_event){.key = ARROW_LEFT});
	ASSERT_EQ(editor.cursor_x, 1);
	test_free_editor_lines();
}

TEST(editor_move_cursor_right)
{
	test_reset_editor();
	test_add_editor_line("abc");
	editor.cursor_x = 1;
	editor.cursor_y = 0;
	editor_move_cursor((struct input_event){.key = ARROW_RIGHT});
	ASSERT_EQ(editor.cursor_x, 2);
	test_free_editor_lines();
}

TEST(editor_move_cursor_up)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_move_cursor((struct input_event){.key = ARROW_UP});
	ASSERT_EQ(editor.cursor_y, 0);
	test_free_editor_lines();
}

TEST(editor_move_cursor_down)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_move_cursor((struct input_event){.key = ARROW_DOWN});
	ASSERT_EQ(editor.cursor_y, 1);
	test_free_editor_lines();
}

TEST(editor_move_cursor_left_wrap)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_move_cursor((struct input_event){.key = ARROW_LEFT});
	ASSERT_EQ(editor.cursor_y, 0);
	ASSERT_EQ(editor.cursor_x, 5);
	test_free_editor_lines();
}

TEST(editor_move_cursor_right_wrap)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor.cursor_x = 5;
	editor.cursor_y = 0;
	editor_move_cursor((struct input_event){.key = ARROW_RIGHT});
	ASSERT_EQ(editor.cursor_y, 1);
	ASSERT_EQ(editor.cursor_x, 0);
	test_free_editor_lines();
}

TEST(editor_move_cursor_up_at_top)
{
	test_reset_editor();
	test_add_editor_line("only");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_move_cursor((struct input_event){.key = ARROW_UP});
	ASSERT_EQ(editor.cursor_y, 0);
	test_free_editor_lines();
}

TEST(editor_move_cursor_down_past_end)
{
	test_reset_editor();
	test_add_editor_line("first");
	test_add_editor_line("second");
	editor.cursor_x = 0;
	editor.cursor_y = 1;
	editor_move_cursor((struct input_event){.key = ARROW_DOWN});
	/* cursor_y can go to line_count (past last line) */
	ASSERT_EQ(editor.cursor_y, 2);
	test_free_editor_lines();
}

TEST(editor_move_cursor_clamp_x)
{
	test_reset_editor();
	test_add_editor_line("long line here");
	test_add_editor_line("short");
	editor.cursor_x = 14;
	editor.cursor_y = 0;
	editor_move_cursor((struct input_event){.key = ARROW_DOWN});
	/* cursor_x should be clamped to length of "short" = 5 */
	ASSERT_EQ(editor.cursor_x, 5);
	test_free_editor_lines();
}

TEST(editor_move_cursor_grapheme_snap)
{
	test_reset_editor();
	/* Line with a flag emoji at cells 0-1 and 'a' at cell 2 */
	test_add_editor_line("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8" "a");
	test_add_editor_line("xxa");
	/* Start on second line at x=1, move up */
	editor.cursor_x = 1;
	editor.cursor_y = 1;
	editor_move_cursor((struct input_event){.key = ARROW_UP});
	/* cursor_x=1 is mid-grapheme (inside flag), should snap to 0 */
	ASSERT_EQ(editor.cursor_x, 0);
	test_free_editor_lines();
}

/*** Section 7: Mode System ***/

TEST(prompt_open_sets_mode)
{
	test_reset_editor();
	prompt_open("Test: %s", NULL, NULL, NULL);
	ASSERT_EQ(editor.mode, MODE_PROMPT);
	ASSERT_NOT_NULL(editor.prompt.buffer);
	ASSERT_EQ_SIZE(editor.prompt.buffer_length, 0);
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

TEST(prompt_type_char)
{
	test_reset_editor();
	prompt_open("Test: %s", NULL, NULL, NULL);
	prompt_handle_key((struct input_event){.key = 'a'});
	ASSERT_EQ_SIZE(editor.prompt.buffer_length, 1);
	ASSERT_STR_EQ(editor.prompt.buffer, "a");
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

TEST(prompt_type_multiple)
{
	test_reset_editor();
	prompt_open("Test: %s", NULL, NULL, NULL);
	prompt_handle_key((struct input_event){.key = 'h'});
	prompt_handle_key((struct input_event){.key = 'i'});
	ASSERT_EQ_SIZE(editor.prompt.buffer_length, 2);
	ASSERT_STR_EQ(editor.prompt.buffer, "hi");
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

TEST(prompt_backspace)
{
	test_reset_editor();
	prompt_open("Test: %s", NULL, NULL, NULL);
	prompt_handle_key((struct input_event){.key = 'a'});
	prompt_handle_key((struct input_event){.key = 'b'});
	prompt_handle_key((struct input_event){.key = BACKSPACE});
	ASSERT_EQ_SIZE(editor.prompt.buffer_length, 1);
	ASSERT_STR_EQ(editor.prompt.buffer, "a");
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

TEST(prompt_backspace_empty)
{
	test_reset_editor();
	prompt_open("Test: %s", NULL, NULL, NULL);
	prompt_handle_key((struct input_event){.key = BACKSPACE});
	ASSERT_EQ_SIZE(editor.prompt.buffer_length, 0);
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

static int test_prompt_accepted = 0;
static char *test_prompt_accepted_value = NULL;
static void test_prompt_on_accept(char *value)
{
	test_prompt_accepted = 1;
	test_prompt_accepted_value = value;
}

TEST(prompt_enter_accepts)
{
	test_reset_editor();
	test_prompt_accepted = 0;
	test_prompt_accepted_value = NULL;
	prompt_open("Test: %s", NULL, test_prompt_on_accept, NULL);
	prompt_handle_key((struct input_event){.key = 'o'});
	prompt_handle_key((struct input_event){.key = 'k'});
	prompt_handle_key((struct input_event){.key = '\r'});
	ASSERT_EQ(editor.mode, MODE_NORMAL);
	ASSERT_EQ(test_prompt_accepted, 1);
	ASSERT_NOT_NULL(test_prompt_accepted_value);
	ASSERT_STR_EQ(test_prompt_accepted_value, "ok");
	free(test_prompt_accepted_value);
	test_prompt_accepted_value = NULL;
}

static int test_prompt_cancelled = 0;
static void test_prompt_on_cancel(void)
{
	test_prompt_cancelled = 1;
}

TEST(prompt_esc_cancels)
{
	test_reset_editor();
	test_prompt_cancelled = 0;
	prompt_open("Test: %s", NULL, NULL, test_prompt_on_cancel);
	prompt_handle_key((struct input_event){.key = 'x'});
	prompt_handle_key((struct input_event){.key = ESC_KEY});
	ASSERT_EQ(editor.mode, MODE_NORMAL);
	ASSERT_EQ(test_prompt_cancelled, 1);
}

TEST(prompt_enter_empty_stays)
{
	test_reset_editor();
	test_prompt_accepted = 0;
	prompt_open("Test: %s", NULL, test_prompt_on_accept, NULL);
	prompt_handle_key((struct input_event){.key = '\r'});
	/* Enter on empty buffer should not accept */
	ASSERT_EQ(test_prompt_accepted, 0);
	ASSERT_EQ(editor.mode, MODE_PROMPT);
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

static int test_confirm_key = 0;
static void test_confirm_callback(int key)
{
	test_confirm_key = key;
}

TEST(confirm_open_sets_mode)
{
	test_reset_editor();
	confirm_open("Sure?", test_confirm_callback);
	ASSERT_EQ(editor.mode, MODE_CONFIRM);
	ASSERT_NOT_NULL(editor.confirm_callback);
	editor.mode = MODE_NORMAL;
}

TEST(confirm_handle_y)
{
	test_reset_editor();
	test_confirm_key = 0;
	confirm_open("Sure?", test_confirm_callback);
	editor_handle_confirm((struct input_event){.key = 'y'});
	ASSERT_EQ(editor.mode, MODE_NORMAL);
	ASSERT_EQ(test_confirm_key, 'y');
}

TEST(confirm_handle_n)
{
	test_reset_editor();
	test_confirm_key = 0;
	confirm_open("Sure?", test_confirm_callback);
	editor_handle_confirm((struct input_event){.key = 'n'});
	ASSERT_EQ(editor.mode, MODE_NORMAL);
	ASSERT_EQ(test_confirm_key, 'n');
}

TEST(confirm_handle_esc)
{
	test_reset_editor();
	test_confirm_key = 0;
	confirm_open("Sure?", test_confirm_callback);
	editor_handle_confirm((struct input_event){.key = ESC_KEY});
	ASSERT_EQ(editor.mode, MODE_NORMAL);
	ASSERT_EQ(test_confirm_key, ESC_KEY);
}

/*** Section 8: Syntax Highlighting ***/

TEST(syntax_keyword_if)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("if (x)");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.cells[0].syntax, HL_KEYWORD1);
	ASSERT_EQ(ln.cells[1].syntax, HL_KEYWORD1);
	line_free(&ln);
}

TEST(syntax_keyword_int)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("int x");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.cells[0].syntax, HL_KEYWORD2);
	ASSERT_EQ(ln.cells[1].syntax, HL_KEYWORD2);
	ASSERT_EQ(ln.cells[2].syntax, HL_KEYWORD2);
	line_free(&ln);
}

TEST(syntax_partial_no_highlight)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	/* "interface" is not a C keyword */
	struct line ln = test_make_line("interface");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.cells[0].syntax, HL_NORMAL);
	line_free(&ln);
}

TEST(syntax_double_quote_string)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("x = \"hello\"");
	line_update_syntax(&ln, 0);
	/* Cells 4-10 are the string including quotes */
	ASSERT_EQ(ln.cells[4].syntax, HL_STRING);
	ASSERT_EQ(ln.cells[5].syntax, HL_STRING);
	ASSERT_EQ(ln.cells[10].syntax, HL_STRING);
	line_free(&ln);
}

TEST(syntax_single_quote_string)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("x = 'c'");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.cells[4].syntax, HL_STRING);
	ASSERT_EQ(ln.cells[5].syntax, HL_STRING);
	ASSERT_EQ(ln.cells[6].syntax, HL_STRING);
	line_free(&ln);
}

TEST(syntax_escape_in_string)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("\"a\\nb\"");
	line_update_syntax(&ln, 0);
	/* All 6 cells should be HL_STRING */
	for (int i = 0; i < 6; i++)
		ASSERT_EQ(ln.cells[i].syntax, HL_STRING);
	line_free(&ln);
}

TEST(syntax_number)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("x = 42");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.cells[4].syntax, HL_NUMBER);
	ASSERT_EQ(ln.cells[5].syntax, HL_NUMBER);
	line_free(&ln);
}

TEST(syntax_float)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("x = 3.14");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.cells[4].syntax, HL_NUMBER);
	ASSERT_EQ(ln.cells[5].syntax, HL_NUMBER);
	ASSERT_EQ(ln.cells[6].syntax, HL_NUMBER);
	ASSERT_EQ(ln.cells[7].syntax, HL_NUMBER);
	line_free(&ln);
}

TEST(syntax_single_line_comment)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("x = 1; // comment");
	line_update_syntax(&ln, 0);
	/* Everything from // onward should be HL_COMMENT */
	ASSERT_EQ(ln.cells[7].syntax, HL_COMMENT);
	ASSERT_EQ(ln.cells[8].syntax, HL_COMMENT);
	ASSERT_EQ(ln.cells[9].syntax, HL_COMMENT);
	line_free(&ln);
}

TEST(syntax_no_syntax_null)
{
	test_reset_editor();
	editor.syntax = NULL;
	struct line ln = test_make_line("if (x) return;");
	int changed = line_update_syntax(&ln, 0);
	ASSERT_EQ(changed, 0);
	ASSERT_EQ(ln.cells[0].syntax, HL_NORMAL);
	line_free(&ln);
}

TEST(syntax_multiline_open_comment)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("/* start");
	line_update_syntax(&ln, 0);
	ASSERT_EQ(ln.open_comment, 1);
	ASSERT_EQ(ln.cells[0].syntax, HL_MLCOMMENT);
	line_free(&ln);
}

TEST(syntax_multiline_close_comment)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	struct line ln = test_make_line("end */");
	line_update_syntax(&ln, 1);  /* previous line had open comment */
	ASSERT_EQ(ln.open_comment, 0);
	ASSERT_EQ(ln.cells[0].syntax, HL_MLCOMMENT);
	line_free(&ln);
}

TEST(syntax_multiline_propagation)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	test_add_editor_line("/* start");
	test_add_editor_line("middle");
	test_add_editor_line("end */");
	syntax_propagate(0);
	ASSERT_EQ(editor.lines[0].open_comment, 1);
	ASSERT_EQ(editor.lines[1].open_comment, 1);
	ASSERT_EQ(editor.lines[1].cells[0].syntax, HL_MLCOMMENT);
	ASSERT_EQ(editor.lines[2].open_comment, 0);
	test_free_editor_lines();
}

TEST(syntax_edit_closes_comment)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	test_add_editor_line("/* start");
	test_add_editor_line("middle");
	test_add_editor_line("end */");
	syntax_propagate(0);
	ASSERT_EQ(editor.lines[1].open_comment, 1);
	/* Now close the comment on line 0 by appending a close-comment */
	line_ensure_warm(&editor.lines[0]);
	/* Add space, star, slash to line 0 */
	struct cell space_c = {.codepoint = ' '};
	struct cell star_c = {.codepoint = '*'};
	struct cell slash_c = {.codepoint = '/'};
	line_insert_cell(&editor.lines[0], editor.lines[0].cell_count, space_c);
	line_insert_cell(&editor.lines[0], editor.lines[0].cell_count, star_c);
	line_insert_cell(&editor.lines[0], editor.lines[0].cell_count, slash_c);
	syntax_propagate(0);
	/* Line 1 should no longer be in a comment */
	ASSERT_EQ(editor.lines[0].open_comment, 0);
	ASSERT_EQ(editor.lines[1].open_comment, 0);
	test_free_editor_lines();
}

TEST(syntax_select_c_file)
{
	test_reset_editor();
	editor.filename = strdup("test.c");
	syntax_select_highlight();
	ASSERT_NOT_NULL(editor.syntax);
	ASSERT_STR_EQ(editor.syntax->filetype, "c");
	free(editor.filename);
	editor.filename = NULL;
}

TEST(syntax_select_h_file)
{
	test_reset_editor();
	editor.filename = strdup("test.h");
	syntax_select_highlight();
	ASSERT_NOT_NULL(editor.syntax);
	ASSERT_STR_EQ(editor.syntax->filetype, "c");
	free(editor.filename);
	editor.filename = NULL;
}

TEST(syntax_select_unknown)
{
	test_reset_editor();
	editor.filename = strdup("test.py");
	syntax_select_highlight();
	ASSERT_NULL(editor.syntax);
	free(editor.filename);
	editor.filename = NULL;
}

TEST(syntax_propagate_forward)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	test_add_editor_line("/* open");
	test_add_editor_line("middle");
	test_add_editor_line("close */");
	test_add_editor_line("normal");
	syntax_propagate(0);
	ASSERT_EQ(editor.lines[1].cells[0].syntax, HL_MLCOMMENT);
	ASSERT_EQ(editor.lines[3].cells[0].syntax, HL_NORMAL);
	test_free_editor_lines();
}

TEST(syntax_propagate_stops)
{
	test_reset_editor();
	editor.syntax = &syntax_highlight_database[0];
	test_add_editor_line("int x;");
	test_add_editor_line("int y;");
	syntax_propagate(0);
	/* Both lines should have no open_comment, propagation stops quickly */
	ASSERT_EQ(editor.lines[0].open_comment, 0);
	ASSERT_EQ(editor.lines[1].open_comment, 0);
	test_free_editor_lines();
}

/*** Section 9: Scroll Logic ***/

TEST(scroll_cursor_in_view)
{
	test_reset_editor();
	test_add_editor_line("hello");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.row_offset = 0;
	editor.column_offset = 0;
	editor_scroll();
	ASSERT_EQ(editor.row_offset, 0);
	ASSERT_EQ(editor.column_offset, 0);
	test_free_editor_lines();
}

TEST(scroll_cursor_above_viewport)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		test_add_editor_line("line");
	editor.cursor_y = 5;
	editor.row_offset = 10;
	editor_scroll();
	ASSERT_EQ(editor.row_offset, 5);
	test_free_editor_lines();
}

TEST(scroll_cursor_below_viewport)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		test_add_editor_line("line");
	editor.cursor_y = 30;
	editor.row_offset = 0;
	editor_scroll();
	/* cursor_y should be visible: row_offset = cursor_y - screen_rows + 1 */
	ASSERT_EQ(editor.row_offset, 30 - editor.screen_rows + 1);
	test_free_editor_lines();
}

TEST(scroll_cursor_left_of_viewport)
{
	test_reset_editor();
	test_add_editor_line("hello world test");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.column_offset = 5;
	editor_scroll();
	ASSERT_EQ(editor.column_offset, 0);
	test_free_editor_lines();
}

TEST(scroll_cursor_right_of_viewport)
{
	test_reset_editor();
	/* Create a line longer than screen width */
	char long_line[200];
	memset(long_line, 'x', 199);
	long_line[199] = '\0';
	test_add_editor_line(long_line);
	editor.cursor_x = 150;
	editor.cursor_y = 0;
	editor.column_offset = 0;
	editor_scroll();
	ASSERT(editor.column_offset > 0);
	test_free_editor_lines();
}

TEST(scroll_render_x_with_tab)
{
	test_reset_editor();
	test_add_editor_line("\thello");
	editor.cursor_x = 1;  /* First char after tab */
	editor.cursor_y = 0;
	editor.column_offset = 0;
	editor_scroll();
	ASSERT_EQ(editor.render_x, EDIT_TAB_STOP);
	test_free_editor_lines();
}

TEST(scroll_rows_down)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		test_add_editor_line("line");
	editor.cursor_y = 0;
	editor.row_offset = 0;
	editor_scroll_rows(ARROW_DOWN, 5);
	ASSERT(editor.cursor_y > 0);
	test_free_editor_lines();
}

TEST(scroll_rows_up)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		test_add_editor_line("line");
	editor.cursor_y = 25;
	editor.row_offset = 10;
	editor_scroll_rows(ARROW_UP, 5);
	ASSERT(editor.cursor_y < 25);
	test_free_editor_lines();
}

TEST(scroll_rows_clamp_top)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		test_add_editor_line("line");
	editor.cursor_y = 2;
	editor.row_offset = 0;
	editor_scroll_rows(ARROW_UP, 100);
	ASSERT(editor.cursor_y >= 0);
	test_free_editor_lines();
}

TEST(scroll_rows_clamp_bottom)
{
	test_reset_editor();
	for (int i = 0; i < 50; i++)
		test_add_editor_line("line");
	editor.cursor_y = 48;
	editor.row_offset = 30;
	editor_scroll_rows(ARROW_DOWN, 100);
	ASSERT(editor.cursor_y <= editor.line_count);
	test_free_editor_lines();
}

TEST(scroll_speed_acceleration)
{
	test_reset_editor();
	editor.scroll_speed = 1;
	gettimeofday(&editor.last_scroll_time, NULL);
	/* Immediate call = fast succession → accelerate */
	editor_update_scroll_speed();
	ASSERT(editor.scroll_speed >= 1);
}

TEST(scroll_speed_deceleration)
{
	test_reset_editor();
	editor.scroll_speed = 5;
	/* Set last_scroll_time far in the past */
	gettimeofday(&editor.last_scroll_time, NULL);
	editor.last_scroll_time.tv_sec -= 1;
	editor_update_scroll_speed();
	ASSERT_EQ(editor.scroll_speed, 1);
}

TEST(scroll_speed_max_cap)
{
	test_reset_editor();
	editor.scroll_speed = SCROLL_SPEED_MAX;
	gettimeofday(&editor.last_scroll_time, NULL);
	editor_update_scroll_speed();
	ASSERT(editor.scroll_speed <= SCROLL_SPEED_MAX);
}

/*** Section 10: File I/O ***/

TEST(rows_to_string_single)
{
	test_reset_editor();
	test_add_editor_line("hello");
	size_t len;
	char *str = editor_rows_to_string(&len);
	ASSERT_EQ_SIZE(len, 6); /* "hello\n" */
	ASSERT(memcmp(str, "hello\n", 6) == 0);
	free(str);
	test_free_editor_lines();
}

TEST(rows_to_string_multiple)
{
	test_reset_editor();
	test_add_editor_line("hello");
	test_add_editor_line("world");
	size_t len;
	char *str = editor_rows_to_string(&len);
	ASSERT_EQ_SIZE(len, 12); /* "hello\nworld\n" */
	ASSERT(memcmp(str, "hello\nworld\n", 12) == 0);
	free(str);
	test_free_editor_lines();
}

TEST(rows_to_string_empty_lines)
{
	test_reset_editor();
	test_add_editor_line("");
	test_add_editor_line("");
	size_t len;
	char *str = editor_rows_to_string(&len);
	ASSERT_EQ_SIZE(len, 2); /* "\n\n" */
	ASSERT(memcmp(str, "\n\n", 2) == 0);
	free(str);
	test_free_editor_lines();
}

TEST(file_open_basic)
{
	test_reset_editor();
	/* Create a temp file */
	char tmpfile[] = "/tmp/test_edit_basic_XXXXXX";
	int fd = mkstemp(tmpfile);
	ASSERT(fd != -1);
	write(fd, "hello\nworld\n", 12);
	close(fd);

	int ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(editor.line_count, 2);
	/* Warm up first line to check content */
	line_ensure_warm(&editor.lines[0]);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'h');

	/* Clean up mmap */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;
	unlink(tmpfile);
}

TEST(file_open_empty)
{
	test_reset_editor();
	char tmpfile[] = "/tmp/test_edit_empty_XXXXXX";
	int fd = mkstemp(tmpfile);
	ASSERT(fd != -1);
	close(fd);

	int ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(editor.line_count, 0);

	free(editor.filename);
	editor.filename = NULL;
	unlink(tmpfile);
}

TEST(file_open_nonexistent)
{
	test_reset_editor();
	int ret = editor_open("/tmp/test_edit_nonexistent_file_xyz");
	ASSERT(ret < 0);
	free(editor.filename);
	editor.filename = NULL;
}

TEST(file_open_crlf)
{
	test_reset_editor();
	char tmpfile[] = "/tmp/test_edit_crlf_XXXXXX";
	int fd = mkstemp(tmpfile);
	ASSERT(fd != -1);
	write(fd, "hello\r\nworld\r\n", 14);
	close(fd);

	int ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(editor.line_count, 2);
	/* Check that \r was stripped */
	line_ensure_warm(&editor.lines[0]);
	ASSERT_EQ(editor.lines[0].cell_count, 5);
	ASSERT_EQ(editor.lines[0].cells[4].codepoint, 'o');

	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;
	unlink(tmpfile);
}

TEST(file_roundtrip)
{
	test_reset_editor();
	char tmpfile[] = "/tmp/test_edit_rt_XXXXXX";
	int fd = mkstemp(tmpfile);
	ASSERT(fd != -1);
	write(fd, "hello\nworld\n", 12);
	close(fd);

	int ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);

	/* Modify: insert char at start of first line */
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor_insert_char('X');

	/* Save */
	ret = editor_save_write();
	ASSERT_EQ(ret, 0);

	/* Clean up current state */
	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;

	/* Reopen and verify */
	test_reset_editor();
	ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);
	line_ensure_warm(&editor.lines[0]);
	ASSERT_EQ(editor.lines[0].cells[0].codepoint, 'X');
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 'h');

	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;
	unlink(tmpfile);
}

TEST(file_roundtrip_unicode)
{
	test_reset_editor();
	char tmpfile[] = "/tmp/test_edit_uni_XXXXXX";
	int fd = mkstemp(tmpfile);
	ASSERT(fd != -1);
	/* Write UTF-8: h + é + llo */
	write(fd, "h\xC3\xA9llo\n", 7);
	close(fd);

	int ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);
	line_ensure_warm(&editor.lines[0]);
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 0xE9);

	/* Save and reopen */
	ret = editor_save_write();
	ASSERT_EQ(ret, 0);

	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;

	test_reset_editor();
	ret = editor_open(tmpfile);
	ASSERT_EQ(ret, 0);
	line_ensure_warm(&editor.lines[0]);
	ASSERT_EQ(editor.lines[0].cells[1].codepoint, 0xE9);

	if (editor.mmap_base) {
		munmap(editor.mmap_base, editor.mmap_size);
		editor.mmap_base = NULL;
	}
	if (editor.file_descriptor != -1) {
		close(editor.file_descriptor);
		editor.file_descriptor = -1;
	}
	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;
	unlink(tmpfile);
}

/*** Section 11: Search & Save Flows ***/

TEST(search_start_saves_state)
{
	test_reset_editor();
	test_add_editor_line("hello world");
	editor.cursor_x = 3;
	editor.cursor_y = 0;
	editor.column_offset = 1;
	editor.row_offset = 0;
	editor_find_start();
	ASSERT_EQ(editor.saved_cursor_x, 3);
	ASSERT_EQ(editor.saved_cursor_y, 0);
	ASSERT_EQ(editor.saved_column_offset, 1);
	ASSERT_EQ(editor.mode, MODE_PROMPT);
	/* Clean up */
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
	test_free_editor_lines();
}

TEST(search_callback_finds_match)
{
	test_reset_editor();
	test_add_editor_line("hello world");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.search_last_match = -1;
	editor.search_last_match_offset = -1;
	editor.search_direction = 1;
	editor.syntax = NULL;
	editor_find_callback("world", 'w');
	ASSERT_EQ(editor.cursor_y, 0);
	ASSERT_EQ(editor.cursor_x, 6);
	ASSERT_EQ(editor.search_last_match, 0);
	test_free_editor_lines();
}

TEST(search_callback_no_match)
{
	test_reset_editor();
	test_add_editor_line("hello");
	editor.cursor_x = 0;
	editor.cursor_y = 0;
	editor.search_last_match = -1;
	editor.search_last_match_offset = -1;
	editor.search_direction = 1;
	editor_find_callback("xyz", 'x');
	/* Cursor should not have moved to a match */
	ASSERT_EQ(editor.search_last_match, -1);
	test_free_editor_lines();
}

TEST(search_cancel_restores_cursor)
{
	test_reset_editor();
	test_add_editor_line("hello world");
	editor.cursor_x = 2;
	editor.cursor_y = 0;
	editor.saved_cursor_x = 2;
	editor.saved_cursor_y = 0;
	editor.saved_column_offset = 0;
	editor.saved_row_offset = 0;
	/* Move cursor away (simulate search found something) */
	editor.cursor_x = 6;
	editor_find_cancel();
	ASSERT_EQ(editor.cursor_x, 2);
	ASSERT_EQ(editor.cursor_y, 0);
	test_free_editor_lines();
}

TEST(search_accept_frees_query)
{
	/* editor_find_accept just frees the query */
	char *query = strdup("test");
	editor_find_accept(query);
	/* No crash = pass. The memory was freed. */
}

TEST(save_with_filename_writes)
{
	test_reset_editor();
	char tmpfile[] = "/tmp/test_edit_save_XXXXXX";
	int fd = mkstemp(tmpfile);
	close(fd);

	editor.filename = strdup(tmpfile);
	test_add_editor_line("saved content");
	editor.quit_after_save = 0;
	int ret = editor_save_write();
	ASSERT_EQ(ret, 0);

	/* Verify file content */
	FILE *f = fopen(tmpfile, "r");
	ASSERT_NOT_NULL(f);
	char buf[64];
	char *got = fgets(buf, sizeof(buf), f);
	ASSERT_NOT_NULL(got);
	ASSERT_STR_EQ(buf, "saved content\n");
	fclose(f);

	test_free_editor_lines();
	free(editor.filename);
	editor.filename = NULL;
	unlink(tmpfile);
}

TEST(save_without_filename_opens_prompt)
{
	test_reset_editor();
	editor.filename = NULL;
	editor.quit_after_save = 0;
	editor_save_start();
	ASSERT_EQ(editor.mode, MODE_PROMPT);
	/* Clean up */
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
}

TEST(save_cancel_sets_message)
{
	test_reset_editor();
	editor.quit_after_save = 1;
	editor_save_cancel();
	ASSERT_EQ(editor.quit_after_save, 0);
	ASSERT(strlen(editor.status_message) > 0);
}

TEST(jump_to_line_valid)
{
	test_reset_editor();
	test_add_editor_line("line1");
	test_add_editor_line("line2");
	test_add_editor_line("line3");
	char *input = strdup("2");
	editor_jump_to_line_accept(input);
	ASSERT_EQ(editor.cursor_y, 1); /* 0-based */
	ASSERT_EQ(editor.cursor_x, 0);
	test_free_editor_lines();
}

TEST(jump_to_line_invalid)
{
	test_reset_editor();
	test_add_editor_line("line1");
	editor.cursor_y = 0;
	char *input = strdup("99");
	editor_jump_to_line_accept(input);
	/* Should set error message, cursor unchanged */
	ASSERT(strlen(editor.status_message) > 0);
	test_free_editor_lines();
}

TEST(jump_to_line_opens_prompt)
{
	test_reset_editor();
	test_add_editor_line("line1");
	editor_jump_to_line_start();
	ASSERT_EQ(editor.mode, MODE_PROMPT);
	free(editor.prompt.buffer);
	editor.prompt.buffer = NULL;
	editor.mode = MODE_NORMAL;
	test_free_editor_lines();
}

/*** Main ***/

int main(void)
{
	printf("\n=== Append Buffer ===\n");
	RUN(append_buffer_init_zeroes);
	RUN(append_buffer_single_write);
	RUN(append_buffer_multiple_writes);
	RUN(append_buffer_growth_on_overflow);
	RUN(append_buffer_free_after_writes);
	RUN(append_buffer_fg_color_red);
	RUN(append_buffer_bg_color_white);
	RUN(append_buffer_hex_parsing);

	printf("\n=== Pure Logic ===\n");
	RUN(cell_display_width_ascii);
	RUN(cell_display_width_tab_col0);
	RUN(cell_display_width_tab_col3);
	RUN(cell_display_width_cjk);
	RUN(cell_display_width_combining);
	RUN(syntax_is_separator_null);
	RUN(syntax_is_separator_space);
	RUN(syntax_is_separator_comma);
	RUN(syntax_is_separator_letter);
	RUN(syntax_is_separator_digit);
	RUN(syntax_is_separator_negative);

	printf("\n=== Line/Cell Operations ===\n");
	RUN(line_init_defaults);
	RUN(line_free_hot);
	RUN(line_free_cold);
	RUN(line_populate_ascii);
	RUN(line_populate_utf8_2byte);
	RUN(line_populate_utf8_3byte);
	RUN(line_populate_utf8_4byte);
	RUN(line_populate_invalid_utf8);
	RUN(line_to_bytes_ascii);
	RUN(line_roundtrip_ascii);
	RUN(line_roundtrip_unicode);
	RUN(line_insert_cell_start);
	RUN(line_insert_cell_end);
	RUN(line_insert_cell_middle);
	RUN(line_delete_cell_start);
	RUN(line_delete_cell_end);
	RUN(line_delete_cell_out_of_bounds);
	RUN(line_append_cells_basic);
	RUN(line_append_cells_from_offset);
	RUN(line_append_cells_past_end);
	RUN(line_ensure_capacity_no_grow);
	RUN(line_ensure_capacity_doubling);
	RUN(line_render_width_ascii);
	RUN(line_render_width_tabs);
	RUN(line_render_width_wide_chars);

	printf("\n=== Grapheme Navigation ===\n");
	RUN(cursor_next_grapheme_ascii);
	RUN(cursor_next_grapheme_emoji);
	RUN(cursor_next_grapheme_zwj);
	RUN(cursor_next_grapheme_flag);
	RUN(cursor_next_grapheme_past_end);
	RUN(cursor_prev_grapheme_ascii);
	RUN(cursor_prev_grapheme_multi_cell);
	RUN(cursor_prev_grapheme_at_zero);
	RUN(grapheme_display_width_ascii);
	RUN(grapheme_display_width_cjk);
	RUN(grapheme_display_width_flag);
	RUN(grapheme_display_width_zwj);
	RUN(grapheme_display_width_combining);
	RUN(line_cell_to_render_ascii);
	RUN(line_cell_to_render_tab);
	RUN(line_cell_to_render_wide);
	RUN(line_render_to_cell_ascii);
	RUN(line_render_to_cell_tab);
	RUN(line_cell_render_roundtrip);

	printf("\n=== Input Buffer & Key Decoding ===\n");
	RUN(input_buffer_available_empty);
	RUN(input_buffer_available_with_data);
	RUN(input_buffer_read_byte_basic);
	RUN(input_buffer_drain_resets);
	RUN(input_buffer_read_empty);
	RUN(decode_ascii_a);
	RUN(decode_ctrl_a);
	RUN(decode_arrow_up);
	RUN(decode_arrow_down);
	RUN(decode_arrow_right);
	RUN(decode_arrow_left);
	RUN(decode_home_bracket_H);
	RUN(decode_end_bracket_F);
	RUN(decode_home_tilde);
	RUN(decode_home_O_H);
	RUN(decode_end_O_F);
	RUN(decode_del);
	RUN(decode_page_up);
	RUN(decode_page_down);
	RUN(decode_f11);
	RUN(decode_alt_f);
	RUN(decode_utf8_2byte);
	RUN(decode_utf8_4byte);
	RUN(decode_empty_buffer);
	RUN(decode_lone_esc);
	RUN(decode_mouse_left_click);
	RUN(decode_mouse_scroll_up);
	RUN(decode_mouse_scroll_down);
	RUN(decode_mouse_release);

	printf("\n=== Editor Operations ===\n");
	RUN(editor_line_insert_first);
	RUN(editor_line_insert_at_end);
	RUN(editor_line_insert_at_start);
	RUN(editor_line_insert_middle);
	RUN(editor_line_delete_first);
	RUN(editor_line_delete_last);
	RUN(editor_line_delete_out_of_bounds);
	RUN(editor_insert_char_basic);
	RUN(editor_insert_char_at_end);
	RUN(editor_insert_char_into_empty);
	RUN(editor_insert_char_unicode);
	RUN(editor_insert_newline_at_start);
	RUN(editor_insert_newline_middle);
	RUN(editor_insert_newline_at_end);
	RUN(editor_insert_newline_unicode);
	RUN(editor_delete_char_basic);
	RUN(editor_delete_char_merge_lines);
	RUN(editor_delete_char_file_start);
	RUN(editor_delete_char_past_end);
	RUN(editor_delete_char_grapheme);
	RUN(editor_update_gutter_hidden);
	RUN(editor_update_gutter_single_digit);
	RUN(editor_update_gutter_two_digits);
	RUN(editor_update_gutter_three_digits);
	RUN(editor_move_cursor_left);
	RUN(editor_move_cursor_right);
	RUN(editor_move_cursor_up);
	RUN(editor_move_cursor_down);
	RUN(editor_move_cursor_left_wrap);
	RUN(editor_move_cursor_right_wrap);
	RUN(editor_move_cursor_up_at_top);
	RUN(editor_move_cursor_down_past_end);
	RUN(editor_move_cursor_clamp_x);
	RUN(editor_move_cursor_grapheme_snap);

	printf("\n=== Mode System ===\n");
	RUN(prompt_open_sets_mode);
	RUN(prompt_type_char);
	RUN(prompt_type_multiple);
	RUN(prompt_backspace);
	RUN(prompt_backspace_empty);
	RUN(prompt_enter_accepts);
	RUN(prompt_esc_cancels);
	RUN(prompt_enter_empty_stays);
	RUN(confirm_open_sets_mode);
	RUN(confirm_handle_y);
	RUN(confirm_handle_n);
	RUN(confirm_handle_esc);

	printf("\n=== Syntax Highlighting ===\n");
	RUN(syntax_keyword_if);
	RUN(syntax_keyword_int);
	RUN(syntax_partial_no_highlight);
	RUN(syntax_double_quote_string);
	RUN(syntax_single_quote_string);
	RUN(syntax_escape_in_string);
	RUN(syntax_number);
	RUN(syntax_float);
	RUN(syntax_single_line_comment);
	RUN(syntax_no_syntax_null);
	RUN(syntax_multiline_open_comment);
	RUN(syntax_multiline_close_comment);
	RUN(syntax_multiline_propagation);
	RUN(syntax_edit_closes_comment);
	RUN(syntax_select_c_file);
	RUN(syntax_select_h_file);
	RUN(syntax_select_unknown);
	RUN(syntax_propagate_forward);
	RUN(syntax_propagate_stops);

	printf("\n=== Scroll Logic ===\n");
	RUN(scroll_cursor_in_view);
	RUN(scroll_cursor_above_viewport);
	RUN(scroll_cursor_below_viewport);
	RUN(scroll_cursor_left_of_viewport);
	RUN(scroll_cursor_right_of_viewport);
	RUN(scroll_render_x_with_tab);
	RUN(scroll_rows_down);
	RUN(scroll_rows_up);
	RUN(scroll_rows_clamp_top);
	RUN(scroll_rows_clamp_bottom);
	RUN(scroll_speed_acceleration);
	RUN(scroll_speed_deceleration);
	RUN(scroll_speed_max_cap);

	printf("\n=== File I/O ===\n");
	RUN(rows_to_string_single);
	RUN(rows_to_string_multiple);
	RUN(rows_to_string_empty_lines);
	RUN(file_open_basic);
	RUN(file_open_empty);
	RUN(file_open_nonexistent);
	RUN(file_open_crlf);
	RUN(file_roundtrip);
	RUN(file_roundtrip_unicode);

	printf("\n=== Search & Save Flows ===\n");
	RUN(search_start_saves_state);
	RUN(search_callback_finds_match);
	RUN(search_callback_no_match);
	RUN(search_cancel_restores_cursor);
	RUN(search_accept_frees_query);
	RUN(save_with_filename_writes);
	RUN(save_without_filename_opens_prompt);
	RUN(save_cancel_sets_message);
	RUN(jump_to_line_valid);
	RUN(jump_to_line_invalid);
	RUN(jump_to_line_opens_prompt);

	printf("\n---\nResults: %d passed, %d failed, %d total\n\n",
	       tests_passed, tests_failed, tests_passed + tests_failed);

	return tests_failed ? 1 : 0;
}
