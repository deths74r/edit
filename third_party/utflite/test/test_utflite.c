/*
 * test_utflite.c - Unit tests for utflite library
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef UTFLITE_SINGLE_HEADER
#define UTFLITE_IMPLEMENTATION
#include "utflite.h"
#else
#include <utflite/utflite.h>
#endif

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-40s", #name); \
    test_##name(); \
    printf(" PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf(" FAIL\n    Expected %d, got %d\n    at %s:%d\n", \
               (int)(b), (int)(a), __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Decode Tests
 * ============================================================================ */

TEST(decode_ascii) {
    uint32_t cp;
    int bytes = utflite_decode("A", 1, &cp);
    ASSERT_EQ(bytes, 1);
    ASSERT_EQ(cp, 0x41);
}

TEST(decode_2byte) {
    uint32_t cp;
    /* e-acute: U+00E9 = 0xC3 0xA9 */
    int bytes = utflite_decode("\xC3\xA9", 2, &cp);
    ASSERT_EQ(bytes, 2);
    ASSERT_EQ(cp, 0x00E9);
}

TEST(decode_3byte) {
    uint32_t cp;
    /* Chinese character U+4E2D = 0xE4 0xB8 0xAD */
    int bytes = utflite_decode("\xE4\xB8\xAD", 3, &cp);
    ASSERT_EQ(bytes, 3);
    ASSERT_EQ(cp, 0x4E2D);
}

TEST(decode_4byte) {
    uint32_t cp;
    /* Emoji grinning face U+1F600 = 0xF0 0x9F 0x98 0x80 */
    int bytes = utflite_decode("\xF0\x9F\x98\x80", 4, &cp);
    ASSERT_EQ(bytes, 4);
    ASSERT_EQ(cp, 0x1F600);
}

TEST(decode_invalid_continuation) {
    uint32_t cp;
    /* Invalid: starts with continuation byte */
    int bytes = utflite_decode("\x80", 1, &cp);
    ASSERT_EQ(bytes, 1);
    ASSERT_EQ(cp, UTFLITE_REPLACEMENT_CHAR);
}

TEST(decode_truncated) {
    uint32_t cp;
    /* Truncated 3-byte sequence */
    int bytes = utflite_decode("\xE4\xB8", 2, &cp);
    ASSERT_EQ(bytes, 1);
    ASSERT_EQ(cp, UTFLITE_REPLACEMENT_CHAR);
}

TEST(decode_overlong) {
    uint32_t cp;
    /* Overlong encoding of NUL (should be rejected) */
    int bytes = utflite_decode("\xC0\x80", 2, &cp);
    ASSERT_EQ(bytes, 2);
    ASSERT_EQ(cp, UTFLITE_REPLACEMENT_CHAR);
}

TEST(decode_surrogate) {
    uint32_t cp;
    /* Surrogate U+D800 (invalid in UTF-8) */
    int bytes = utflite_decode("\xED\xA0\x80", 3, &cp);
    ASSERT_EQ(bytes, 3);
    ASSERT_EQ(cp, UTFLITE_REPLACEMENT_CHAR);
}

TEST(decode_null_input) {
    uint32_t cp;
    int bytes = utflite_decode(NULL, 0, &cp);
    ASSERT_EQ(bytes, 1);
    ASSERT_EQ(cp, UTFLITE_REPLACEMENT_CHAR);
}

/* ============================================================================
 * Encode Tests
 * ============================================================================ */

TEST(encode_ascii) {
    char buf[4];
    int bytes = utflite_encode(0x41, buf);
    ASSERT_EQ(bytes, 1);
    ASSERT_EQ(buf[0], 'A');
}

TEST(encode_2byte) {
    char buf[4];
    int bytes = utflite_encode(0x00E9, buf);
    ASSERT_EQ(bytes, 2);
    ASSERT_EQ((unsigned char)buf[0], 0xC3);
    ASSERT_EQ((unsigned char)buf[1], 0xA9);
}

TEST(encode_3byte) {
    char buf[4];
    int bytes = utflite_encode(0x4E2D, buf);
    ASSERT_EQ(bytes, 3);
    ASSERT_EQ((unsigned char)buf[0], 0xE4);
    ASSERT_EQ((unsigned char)buf[1], 0xB8);
    ASSERT_EQ((unsigned char)buf[2], 0xAD);
}

TEST(encode_4byte) {
    char buf[4];
    int bytes = utflite_encode(0x1F600, buf);
    ASSERT_EQ(bytes, 4);
    ASSERT_EQ((unsigned char)buf[0], 0xF0);
    ASSERT_EQ((unsigned char)buf[1], 0x9F);
    ASSERT_EQ((unsigned char)buf[2], 0x98);
    ASSERT_EQ((unsigned char)buf[3], 0x80);
}

TEST(encode_surrogate) {
    char buf[4];
    int bytes = utflite_encode(0xD800, buf);
    ASSERT_EQ(bytes, 0);
}

TEST(encode_too_large) {
    char buf[4];
    int bytes = utflite_encode(0x110000, buf);
    ASSERT_EQ(bytes, 0);
}

/* ============================================================================
 * Roundtrip Tests
 * ============================================================================ */

TEST(roundtrip) {
    uint32_t test_cps[] = {
        0x0041,     /* ASCII 'A' */
        0x00E9,     /* Latin e-acute */
        0x4E2D,     /* CJK character */
        0x1F600,    /* Emoji */
        0x10FFFF,   /* Max valid codepoint */
    };

    for (size_t i = 0; i < sizeof(test_cps) / sizeof(test_cps[0]); i++) {
        char buf[UTFLITE_MAX_BYTES];
        uint32_t decoded;

        int enc_bytes = utflite_encode(test_cps[i], buf);
        ASSERT(enc_bytes > 0);

        int dec_bytes = utflite_decode(buf, enc_bytes, &decoded);
        ASSERT_EQ(dec_bytes, enc_bytes);
        ASSERT_EQ(decoded, test_cps[i]);
    }
}

/* ============================================================================
 * Width Tests
 * ============================================================================ */

TEST(width_ascii) {
    ASSERT_EQ(utflite_codepoint_width(0x41), 1);   /* 'A' */
    ASSERT_EQ(utflite_codepoint_width(0x20), 1);   /* space */
    ASSERT_EQ(utflite_codepoint_width(0x7E), 1);   /* '~' */
}

TEST(width_control) {
    ASSERT_EQ(utflite_codepoint_width(0x00), 0);   /* NUL */
    ASSERT_EQ(utflite_codepoint_width(0x01), -1);  /* SOH */
    ASSERT_EQ(utflite_codepoint_width(0x7F), -1);  /* DEL */
    ASSERT_EQ(utflite_codepoint_width(0x9F), -1);  /* C1 control */
}

TEST(width_combining) {
    ASSERT_EQ(utflite_codepoint_width(0x0300), 0);   /* Combining grave */
    ASSERT_EQ(utflite_codepoint_width(0x0301), 0);   /* Combining acute */
    ASSERT_EQ(utflite_codepoint_width(0xFE00), 0);   /* Variation selector */
}

TEST(width_cjk) {
    ASSERT_EQ(utflite_codepoint_width(0x4E2D), 2);   /* CJK character */
    ASSERT_EQ(utflite_codepoint_width(0x3042), 2);   /* Hiragana 'a' */
    ASSERT_EQ(utflite_codepoint_width(0xAC00), 2);   /* Hangul syllable */
}

TEST(width_emoji) {
    ASSERT_EQ(utflite_codepoint_width(0x1F600), 2);  /* Grinning face */
    ASSERT_EQ(utflite_codepoint_width(0x2764), 2);   /* Heart */
    ASSERT_EQ(utflite_codepoint_width(0x1F4A9), 2);  /* Pile of poo */
}

TEST(width_fullwidth) {
    ASSERT_EQ(utflite_codepoint_width(0xFF01), 2);   /* Fullwidth ! */
    ASSERT_EQ(utflite_codepoint_width(0xFF21), 2);   /* Fullwidth A */
}

/* ============================================================================
 * Navigation Tests
 * ============================================================================ */

TEST(next_char) {
    /* "Ae\xC3\xA9\xE4\xB8\xAD" = A + e-acute + CJK */
    const char *text = "A\xC3\xA9\xE4\xB8\xAD";
    int len = 6;

    ASSERT_EQ(utflite_next_char(text, len, 0), 1);   /* After 'A' */
    ASSERT_EQ(utflite_next_char(text, len, 1), 3);   /* After e-acute */
    ASSERT_EQ(utflite_next_char(text, len, 3), 6);   /* After CJK */
    ASSERT_EQ(utflite_next_char(text, len, 6), 6);   /* At end */
}

TEST(prev_char) {
    const char *text = "A\xC3\xA9\xE4\xB8\xAD";

    ASSERT_EQ(utflite_prev_char(text, 6), 3);   /* Before CJK */
    ASSERT_EQ(utflite_prev_char(text, 3), 1);   /* Before e-acute */
    ASSERT_EQ(utflite_prev_char(text, 1), 0);   /* Before 'A' */
    ASSERT_EQ(utflite_prev_char(text, 0), 0);   /* At start */
}

/* ============================================================================
 * Utility Tests
 * ============================================================================ */

TEST(validate_valid) {
    ASSERT_EQ(utflite_validate("Hello", 5, NULL), 1);
    ASSERT_EQ(utflite_validate("\xE4\xB8\xAD", 3, NULL), 1);
    ASSERT_EQ(utflite_validate("", 0, NULL), 1);
}

TEST(validate_invalid) {
    int error_pos = -1;
    ASSERT_EQ(utflite_validate("\x80", 1, &error_pos), 0);
    ASSERT_EQ(error_pos, 0);

    error_pos = -1;
    ASSERT_EQ(utflite_validate("A\xC0\x80", 3, &error_pos), 0);
    ASSERT_EQ(error_pos, 1);
}

TEST(codepoint_count) {
    ASSERT_EQ(utflite_codepoint_count("Hello", 5), 5);
    ASSERT_EQ(utflite_codepoint_count("A\xC3\xA9\xE4\xB8\xAD", 6), 3);
    ASSERT_EQ(utflite_codepoint_count("", 0), 0);
}

TEST(string_width) {
    ASSERT_EQ(utflite_string_width("Hello", 5), 5);
    ASSERT_EQ(utflite_string_width("\xE4\xB8\xAD", 3), 2);   /* CJK = 2 cols */
    ASSERT_EQ(utflite_string_width("A\xE4\xB8\xAD", 4), 3);  /* A + CJK */
}

TEST(is_zero_width) {
    ASSERT_EQ(utflite_is_zero_width(0x0300), 1);   /* Combining grave */
    ASSERT_EQ(utflite_is_zero_width(0x0041), 0);   /* 'A' */
    ASSERT_EQ(utflite_is_zero_width(0xFE0F), 1);   /* Variation selector */
}

TEST(zero_width_bidi_chars) {
    /* Verify binary search finds zero-width chars after table sort fix */
    ASSERT_EQ(utflite_is_zero_width(0x200B), 1);   /* Zero-width space */
    ASSERT_EQ(utflite_is_zero_width(0x200D), 1);   /* Zero-width joiner (ZWJ) */
    ASSERT_EQ(utflite_is_zero_width(0x202A), 1);   /* Left-to-right embedding */
    ASSERT_EQ(utflite_is_zero_width(0x2060), 1);   /* Word joiner */
    ASSERT_EQ(utflite_is_zero_width(0x2066), 1);   /* Left-to-right isolate */
    ASSERT_EQ(utflite_codepoint_width(0x200B), 0); /* Zero-width space = 0 cols */
    ASSERT_EQ(utflite_codepoint_width(0x200D), 0); /* ZWJ = 0 cols */
}

TEST(prev_char_bounded_scan) {
    /* Create malformed input: 10 continuation bytes (0x80) */
    char malformed[11];
    for (int i = 0; i < 10; i++) {
        malformed[i] = (char)0x80;
    }
    malformed[10] = '\0';

    /* Starting at offset 10, should not scan more than 4 bytes back */
    /* With limit, it should stop at offset 6 (10 - 4), not 0 */
    int result = utflite_prev_char(malformed, 10);
    ASSERT(result >= 6);  /* Should be bounded to at most 4 bytes back */
}

TEST(is_wide) {
    ASSERT_EQ(utflite_is_wide(0x4E2D), 1);   /* CJK */
    ASSERT_EQ(utflite_is_wide(0x0041), 0);   /* 'A' */
    ASSERT_EQ(utflite_is_wide(0x1F600), 1);  /* Emoji */
}

TEST(truncate) {
    /* "ABC" = 3 columns */
    ASSERT_EQ(utflite_truncate("ABC", 3, 2), 2);
    ASSERT_EQ(utflite_truncate("ABC", 3, 3), 3);
    ASSERT_EQ(utflite_truncate("ABC", 3, 10), 3);

    /* CJK char (2 cols) + A (1 col) */
    const char *text = "\xE4\xB8\xAD" "A";  /* 3 bytes CJK + 1 byte A */
    ASSERT_EQ(utflite_truncate(text, 4, 1), 0);   /* Can't fit CJK */
    ASSERT_EQ(utflite_truncate(text, 4, 2), 3);   /* CJK fits */
    ASSERT_EQ(utflite_truncate(text, 4, 3), 4);   /* CJK + A fits */
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("utflite test suite\n");
    printf("==================\n\n");

    printf("Decode tests:\n");
    RUN(decode_ascii);
    RUN(decode_2byte);
    RUN(decode_3byte);
    RUN(decode_4byte);
    RUN(decode_invalid_continuation);
    RUN(decode_truncated);
    RUN(decode_overlong);
    RUN(decode_surrogate);
    RUN(decode_null_input);

    printf("\nEncode tests:\n");
    RUN(encode_ascii);
    RUN(encode_2byte);
    RUN(encode_3byte);
    RUN(encode_4byte);
    RUN(encode_surrogate);
    RUN(encode_too_large);

    printf("\nRoundtrip tests:\n");
    RUN(roundtrip);

    printf("\nWidth tests:\n");
    RUN(width_ascii);
    RUN(width_control);
    RUN(width_combining);
    RUN(width_cjk);
    RUN(width_emoji);
    RUN(width_fullwidth);

    printf("\nNavigation tests:\n");
    RUN(next_char);
    RUN(prev_char);

    printf("\nUtility tests:\n");
    RUN(validate_valid);
    RUN(validate_invalid);
    RUN(codepoint_count);
    RUN(string_width);
    RUN(is_zero_width);
    RUN(zero_width_bidi_chars);
    RUN(prev_char_bounded_scan);
    RUN(is_wide);
    RUN(truncate);

    printf("\n==================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
