/*
 * UTF-8 implementation tests for edit
 * Compile: cc -std=c17 -o test_utf8 test_utf8.c
 */

#define UTFLITE_IMPLEMENTATION
#include "third_party/utflite/single_include/utflite.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  Testing: %s... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)

/* Count graphemes in a string */
static int count_graphemes(const char *text, int length)
{
    int count = 0;
    int offset = 0;
    while (offset < length) {
        offset = utflite_next_grapheme(text, length, offset);
        count++;
    }
    return count;
}

/* Calculate display width of a string */
static int string_display_width(const char *text, int length)
{
    int width = 0;
    int offset = 0;
    while (offset < length) {
        uint32_t codepoint;
        int bytes = utflite_decode(text + offset, length - offset, &codepoint);
        int w = utflite_codepoint_width(codepoint);
        if (w > 0) width += w;
        offset += bytes;
    }
    return width;
}

void test_ascii(void)
{
    printf("\n[ASCII Tests]\n");

    TEST("ASCII decode");
    uint32_t cp;
    int bytes = utflite_decode("A", 1, &cp);
    if (bytes == 1 && cp == 'A') PASS(); else FAIL("wrong decode");

    TEST("ASCII encode");
    char buf[4];
    bytes = utflite_encode('Z', buf);
    if (bytes == 1 && buf[0] == 'Z') PASS(); else FAIL("wrong encode");

    TEST("ASCII width");
    if (utflite_codepoint_width('a') == 1) PASS(); else FAIL("wrong width");

    TEST("ASCII string graphemes");
    if (count_graphemes("Hello", 5) == 5) PASS(); else FAIL("wrong count");
}

void test_multibyte(void)
{
    printf("\n[Multi-byte UTF-8 Tests]\n");

    TEST("2-byte decode (é)");
    uint32_t cp;
    const char *e_acute = "é";  /* U+00E9 */
    int bytes = utflite_decode(e_acute, strlen(e_acute), &cp);
    if (bytes == 2 && cp == 0x00E9) PASS(); else FAIL("wrong decode");

    TEST("3-byte decode (中)");
    const char *zhong = "中";  /* U+4E2D */
    bytes = utflite_decode(zhong, strlen(zhong), &cp);
    if (bytes == 3 && cp == 0x4E2D) PASS(); else FAIL("wrong decode");

    TEST("4-byte decode (😀)");
    const char *emoji = "😀";  /* U+1F600 */
    bytes = utflite_decode(emoji, strlen(emoji), &cp);
    if (bytes == 4 && cp == 0x1F600) PASS(); else FAIL("wrong decode");

    TEST("Encode roundtrip (中)");
    char buf[4];
    bytes = utflite_encode(0x4E2D, buf);
    if (bytes == 3 && memcmp(buf, zhong, 3) == 0) PASS(); else FAIL("roundtrip failed");
}

void test_widths(void)
{
    printf("\n[Character Width Tests]\n");

    TEST("ASCII width = 1");
    if (utflite_codepoint_width('A') == 1) PASS(); else FAIL("wrong");

    TEST("CJK width = 2 (中)");
    if (utflite_codepoint_width(0x4E2D) == 2) PASS(); else FAIL("wrong");

    TEST("Emoji width = 2 (😀)");
    if (utflite_codepoint_width(0x1F600) == 2) PASS(); else FAIL("wrong");

    TEST("Combining mark width = 0");
    if (utflite_codepoint_width(0x0301) == 0) PASS(); else FAIL("wrong");  /* combining acute */

    TEST("Control char width = -1");
    if (utflite_codepoint_width(0x01) == -1) PASS(); else FAIL("wrong");

    TEST("Mixed string width");
    const char *mixed = "A中😀";  /* 1 + 2 + 2 = 5 */
    int width = string_display_width(mixed, strlen(mixed));
    if (width == 5) PASS(); else { printf("got %d, ", width); FAIL("wrong"); }
}

void test_graphemes(void)
{
    printf("\n[Grapheme Cluster Tests]\n");

    TEST("Simple ASCII graphemes");
    if (count_graphemes("abc", 3) == 3) PASS(); else FAIL("wrong count");

    TEST("CJK graphemes");
    const char *cjk = "中文";
    if (count_graphemes(cjk, strlen(cjk)) == 2) PASS(); else FAIL("wrong count");

    TEST("Emoji graphemes");
    const char *emojis = "😀🎉";
    if (count_graphemes(emojis, strlen(emojis)) == 2) PASS(); else FAIL("wrong count");

    TEST("Combining character (e + acute = 1 grapheme)");
    const char *e_combining = "e\xcc\x81";  /* e + combining acute accent */
    int count = count_graphemes(e_combining, strlen(e_combining));
    if (count == 1) PASS(); else { printf("got %d, ", count); FAIL("wrong count"); }

    TEST("Precomposed vs combining (both = 1 grapheme)");
    const char *precomposed = "é";  /* single codepoint */
    if (count_graphemes(precomposed, strlen(precomposed)) == 1) PASS(); else FAIL("wrong count");

    TEST("Flag emoji (regional indicators)");
    const char *flag = "🇺🇸";  /* U+1F1FA U+1F1F8 */
    int flag_count = count_graphemes(flag, strlen(flag));
    if (flag_count == 1) PASS(); else { printf("got %d, ", flag_count); FAIL("should be 1"); }

    TEST("Skin tone emoji");
    const char *wave = "👋🏽";  /* wave + skin tone modifier */
    int wave_count = count_graphemes(wave, strlen(wave));
    if (wave_count == 1) PASS(); else { printf("got %d, ", wave_count); FAIL("should be 1"); }

    TEST("ZWJ family emoji");
    const char *family = "👨‍👩‍👧";  /* man ZWJ woman ZWJ girl */
    int family_count = count_graphemes(family, strlen(family));
    if (family_count == 1) PASS(); else { printf("got %d, ", family_count); FAIL("should be 1"); }
}

void test_navigation(void)
{
    printf("\n[Navigation Tests]\n");

    TEST("next_char ASCII");
    const char *ascii = "abc";
    if (utflite_next_char(ascii, 3, 0) == 1) PASS(); else FAIL("wrong");

    TEST("next_char multibyte");
    const char *multi = "中文";
    int next = utflite_next_char(multi, strlen(multi), 0);
    if (next == 3) PASS(); else { printf("got %d, ", next); FAIL("wrong"); }

    TEST("prev_char ASCII");
    if (utflite_prev_char("abc", 2) == 1) PASS(); else FAIL("wrong");

    TEST("prev_char multibyte");
    const char *multi2 = "中文";
    int prev = utflite_prev_char(multi2, 6);  /* from end */
    if (prev == 3) PASS(); else { printf("got %d, ", prev); FAIL("wrong"); }

    TEST("next_grapheme with combining");
    const char *combining = "e\xcc\x81x";  /* é followed by x */
    int next_g = utflite_next_grapheme(combining, strlen(combining), 0);
    if (next_g == 3) PASS(); else { printf("got %d, ", next_g); FAIL("should skip combining mark"); }

    TEST("prev_grapheme with combining");
    const char *combining2 = "e\xcc\x81";
    int prev_g = utflite_prev_grapheme(combining2, 3);
    if (prev_g == 0) PASS(); else { printf("got %d, ", prev_g); FAIL("should go to start"); }
}

void test_edge_cases(void)
{
    printf("\n[Edge Case Tests]\n");

    TEST("Empty string graphemes");
    if (count_graphemes("", 0) == 0) PASS(); else FAIL("should be 0");

    TEST("Invalid UTF-8 (lone continuation byte)");
    uint32_t cp;
    utflite_decode("\x80", 1, &cp);
    if (cp == UTFLITE_REPLACEMENT_CHAR) PASS(); else FAIL("should return replacement char");

    TEST("Truncated sequence");
    utflite_decode("\xc3", 1, &cp);  /* Start of 2-byte, but only 1 byte */
    if (cp == UTFLITE_REPLACEMENT_CHAR) PASS(); else FAIL("should return replacement char");

    TEST("Overlong encoding rejected");
    utflite_decode("\xc0\x80", 2, &cp);  /* Overlong NUL */
    if (cp == UTFLITE_REPLACEMENT_CHAR) PASS(); else FAIL("should reject overlong");
}

int main(void)
{
    printf("=== UTF-8 Implementation Tests ===\n");

    test_ascii();
    test_multibyte();
    test_widths();
    test_graphemes();
    test_navigation();
    test_edge_cases();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
