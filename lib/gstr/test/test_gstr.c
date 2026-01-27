/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
 *
 * test_gstr.c - Unit tests for gstr grapheme string library
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gstr.h>

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
    if ((a) != (b)) {                                                          \
      printf(" FAIL\n    Expected %d, got %d\n    at %s:%d\n", (int)(b),       \
             (int)(a), __FILE__, __LINE__);                                    \
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

/* ============================================================================
 * Test Strings
 * ============================================================================
 */

/* ASCII */
static const char *ASCII = "Hello";
static const size_t ASCII_LEN = 5;

/* UTF-8 with combining marks: cafe with acute accent */
/* "café" where é is e + combining acute (U+0065 U+0301) */
static const char *CAFE_DECOMPOSED = "cafe\xCC\x81";
static const size_t CAFE_DECOMPOSED_LEN = 6;

/* "café" where é is precomposed (U+00E9) */
static const char *CAFE_COMPOSED = "caf\xC3\xA9";
static const size_t CAFE_COMPOSED_LEN = 5;

/* Single emoji: 😀 (U+1F600) */
static const char *EMOJI_SIMPLE = "\xF0\x9F\x98\x80";
static const size_t EMOJI_SIMPLE_LEN = 4;

/* ZWJ family sequence: 👨‍👩‍👧 (man + ZWJ + woman + ZWJ + girl) */
static const char *FAMILY = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"
                            "\xE2\x80\x8D\xF0\x9F\x91\xA7";
static const size_t FAMILY_LEN = 18;

/* Just the woman from family: 👩 (U+1F469) */
static const char *WOMAN = "\xF0\x9F\x91\xA9";
static const size_t WOMAN_LEN = 4;

/* Flag: 🇨🇦 (Canada - two regional indicators) */
static const char *FLAG_CA = "\xF0\x9F\x87\xA8\xF0\x9F\x87\xA6";
static const size_t FLAG_CA_LEN = 8;

/* Emoji with skin tone: 👋🏽 (waving hand + medium skin tone) */
static const char *WAVE_SKIN = "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD";
static const size_t WAVE_SKIN_LEN = 8;

/* Mixed string: "Hi 👋🏽!" */
static const char *MIXED = "Hi \xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD!";
static const size_t MIXED_LEN = 12;

/* Korean Hangul: 한글 */
static const char *HANGUL = "\xED\x95\x9C\xEA\xB8\x80";
static const size_t HANGUL_LEN = 6;

/* ============================================================================
 * Length Tests (gstrlen, gstrnlen)
 * ============================================================================
 */

TEST(gstrlen_ascii) { ASSERT_EQ_SIZE(gstrlen(ASCII, ASCII_LEN), 5); }

TEST(gstrlen_empty) { ASSERT_EQ_SIZE(gstrlen("", 0), 0); }

TEST(gstrlen_null) { ASSERT_EQ_SIZE(gstrlen(NULL, 0), 0); }

TEST(gstrlen_emoji_simple) {
  ASSERT_EQ_SIZE(gstrlen(EMOJI_SIMPLE, EMOJI_SIMPLE_LEN), 1);
}

TEST(gstrlen_family_zwj) {
  /* Family emoji is ONE grapheme despite being 18 bytes */
  ASSERT_EQ_SIZE(gstrlen(FAMILY, FAMILY_LEN), 1);
}

TEST(gstrlen_flag) {
  /* Flag is ONE grapheme (two regional indicators) */
  ASSERT_EQ_SIZE(gstrlen(FLAG_CA, FLAG_CA_LEN), 1);
}

TEST(gstrlen_skin_tone) {
  /* Emoji + skin tone is ONE grapheme */
  ASSERT_EQ_SIZE(gstrlen(WAVE_SKIN, WAVE_SKIN_LEN), 1);
}

TEST(gstrlen_combining_marks) {
  /* "café" with decomposed é is 4 graphemes */
  ASSERT_EQ_SIZE(gstrlen(CAFE_DECOMPOSED, CAFE_DECOMPOSED_LEN), 4);
}

TEST(gstrlen_mixed) {
  /* "Hi 👋🏽!" = H + i + space + wave+skin + ! = 5 graphemes */
  ASSERT_EQ_SIZE(gstrlen(MIXED, MIXED_LEN), 5);
}

TEST(gstrlen_hangul) {
  /* 한글 = 2 graphemes */
  ASSERT_EQ_SIZE(gstrlen(HANGUL, HANGUL_LEN), 2);
}

TEST(gstrnlen_basic) {
  ASSERT_EQ_SIZE(gstrnlen(ASCII, ASCII_LEN, 3), 3);
  ASSERT_EQ_SIZE(gstrnlen(ASCII, ASCII_LEN, 10), 5);
}

TEST(gstrnlen_mixed) {
  /* "Hi 👋🏽!" - count first 3 graphemes */
  ASSERT_EQ_SIZE(gstrnlen(MIXED, MIXED_LEN, 3), 3);
}

/* ============================================================================
 * Indexing Tests (gstroff, gstrat)
 * ============================================================================
 */

TEST(gstroff_ascii) {
  ASSERT_EQ_SIZE(gstroff(ASCII, ASCII_LEN, 0), 0);
  ASSERT_EQ_SIZE(gstroff(ASCII, ASCII_LEN, 1), 1);
  ASSERT_EQ_SIZE(gstroff(ASCII, ASCII_LEN, 4), 4);
  ASSERT_EQ_SIZE(gstroff(ASCII, ASCII_LEN, 5), 5); /* past end */
}

TEST(gstroff_mixed) {
  /* "Hi 👋🏽!" */
  ASSERT_EQ_SIZE(gstroff(MIXED, MIXED_LEN, 0), 0);  /* H */
  ASSERT_EQ_SIZE(gstroff(MIXED, MIXED_LEN, 1), 1);  /* i */
  ASSERT_EQ_SIZE(gstroff(MIXED, MIXED_LEN, 2), 2);  /* space */
  ASSERT_EQ_SIZE(gstroff(MIXED, MIXED_LEN, 3), 3);  /* wave+skin (8 bytes) */
  ASSERT_EQ_SIZE(gstroff(MIXED, MIXED_LEN, 4), 11); /* ! */
}

TEST(gstrat_ascii) {
  size_t len;
  const char *p = gstrat(ASCII, ASCII_LEN, 0, &len);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE(len, 1);
  ASSERT(*p == 'H');
}

TEST(gstrat_emoji) {
  size_t len;
  const char *p = gstrat(MIXED, MIXED_LEN, 3, &len);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE(len, 8); /* wave + skin tone modifier */
  ASSERT(memcmp(p, WAVE_SKIN, 8) == 0);
}

TEST(gstrat_out_of_bounds) {
  size_t len;
  const char *p = gstrat(ASCII, ASCII_LEN, 10, &len);
  ASSERT_NULL(p);
}

TEST(gstrat_family) {
  size_t len;
  const char *p = gstrat(FAMILY, FAMILY_LEN, 0, &len);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE(len, 18); /* entire family emoji is one grapheme */
}

/* ============================================================================
 * Comparison Tests (gstrcmp, gstrncmp, gstrcasecmp)
 * ============================================================================
 */

TEST(gstrcmp_equal_ascii) { ASSERT_EQ(gstrcmp("hello", 5, "hello", 5), 0); }

TEST(gstrcmp_less_ascii) { ASSERT(gstrcmp("abc", 3, "abd", 3) < 0); }

TEST(gstrcmp_greater_ascii) { ASSERT(gstrcmp("abd", 3, "abc", 3) > 0); }

TEST(gstrcmp_shorter) {
  /* Shorter string is "less" */
  ASSERT(gstrcmp("ab", 2, "abc", 3) < 0);
}

TEST(gstrcmp_longer) { ASSERT(gstrcmp("abc", 3, "ab", 2) > 0); }

TEST(gstrcmp_emoji) {
  ASSERT_EQ(gstrcmp(FAMILY, FAMILY_LEN, FAMILY, FAMILY_LEN), 0);
}

TEST(gstrcmp_different_normalization) {
  /* café composed vs decomposed should NOT be equal (byte-exact) */
  ASSERT(gstrcmp(CAFE_COMPOSED, CAFE_COMPOSED_LEN, CAFE_DECOMPOSED,
                 CAFE_DECOMPOSED_LEN) != 0);
}

TEST(gstrncmp_basic) {
  ASSERT_EQ(gstrncmp("hello", 5, "help", 4, 3), 0);
  ASSERT(gstrncmp("hello", 5, "help", 4, 4) < 0);
}

TEST(gstrncmp_mixed) {
  /* Compare first 3 graphemes of "Hi 👋🏽!" */
  ASSERT_EQ(gstrncmp(MIXED, MIXED_LEN, "Hi X", 4, 3), 0);
}

TEST(gstrcasecmp_basic) {
  ASSERT_EQ(gstrcasecmp("Hello", 5, "hello", 5), 0);
  ASSERT_EQ(gstrcasecmp("HELLO", 5, "hello", 5), 0);
}

TEST(gstrcasecmp_different) { ASSERT(gstrcasecmp("abc", 3, "ABD", 3) < 0); }

/* ============================================================================
 * Search Tests (gstrchr, gstrrchr, gstrstr)
 * ============================================================================
 */

TEST(gstrchr_ascii) {
  const char *p = gstrchr("hello", 5, "l", 1);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello"), 2);
}

TEST(gstrchr_not_found) {
  const char *p = gstrchr("hello", 5, "x", 1);
  ASSERT_NULL(p);
}

TEST(gstrchr_emoji) {
  /* Find wave emoji in mixed string */
  const char *p = gstrchr(MIXED, MIXED_LEN, WAVE_SKIN, WAVE_SKIN_LEN);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - MIXED), 3);
}

TEST(gstrchr_partial_emoji_not_found) {
  /* Should NOT find 👩 inside 👨‍👩‍👧 - grapheme boundary
   * semantics
   */
  const char *p = gstrchr(FAMILY, FAMILY_LEN, WOMAN, WOMAN_LEN);
  ASSERT_NULL(p);
}

TEST(gstrrchr_basic) {
  const char *p = gstrrchr("hello", 5, "l", 1);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello"), 3); /* second 'l' */
}

TEST(gstrrchr_single_match) {
  const char *p = gstrrchr("hello", 5, "h", 1);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello"), 0);
}

TEST(gstrstr_basic) {
  const char *p = gstrstr("hello world", 11, "world", 5);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello world"), 6);
}

TEST(gstrstr_at_start) {
  const char *p = gstrstr("hello", 5, "hel", 3);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello"), 0);
}

TEST(gstrstr_at_end) {
  const char *p = gstrstr("hello", 5, "llo", 3);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello"), 2);
}

TEST(gstrstr_not_found) {
  const char *p = gstrstr("hello", 5, "xyz", 3);
  ASSERT_NULL(p);
}

TEST(gstrstr_empty_needle) {
  const char *haystack = "hello";
  const char *p = gstrstr(haystack, 5, "", 0);
  ASSERT_NOT_NULL(p);
  ASSERT(p == haystack);
}

TEST(gstrstr_emoji) {
  /* Search for single emoji in family - should NOT find */
  const char *p = gstrstr(FAMILY, FAMILY_LEN, WOMAN, WOMAN_LEN);
  ASSERT_NULL(p);
}

TEST(gstrstr_full_match) {
  /* Search for entire family - should find */
  const char *p = gstrstr(FAMILY, FAMILY_LEN, FAMILY, FAMILY_LEN);
  ASSERT_NOT_NULL(p);
}

/* ============================================================================
 * Span Tests (gstrspn, gstrcspn, gstrpbrk)
 * ============================================================================
 */

TEST(gstrspn_basic) {
  size_t n = gstrspn("aaabbc", 6, "ab", 2);
  ASSERT_EQ_SIZE(n, 5);
}

TEST(gstrspn_no_match) {
  size_t n = gstrspn("hello", 5, "xyz", 3);
  ASSERT_EQ_SIZE(n, 0);
}

TEST(gstrspn_all_match) {
  size_t n = gstrspn("aaa", 3, "a", 1);
  ASSERT_EQ_SIZE(n, 3);
}

TEST(gstrcspn_basic) {
  size_t n = gstrcspn("hello", 5, "lo", 2);
  ASSERT_EQ_SIZE(n, 2); /* 'h' and 'e' before 'l' */
}

TEST(gstrcspn_no_reject) {
  size_t n = gstrcspn("hello", 5, "xyz", 3);
  ASSERT_EQ_SIZE(n, 5);
}

TEST(gstrpbrk_basic) {
  const char *p = gstrpbrk("hello", 5, "lo", 2);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello"), 2);
}

TEST(gstrpbrk_not_found) {
  const char *p = gstrpbrk("hello", 5, "xyz", 3);
  ASSERT_NULL(p);
}

/* ============================================================================
 * Extraction Tests (gstrsub)
 * ============================================================================
 */

TEST(gstrsub_basic) {
  char buf[32];
  size_t n = gstrsub(buf, sizeof(buf), "hello world", 11, 0, 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrsub_middle) {
  char buf[32];
  size_t n = gstrsub(buf, sizeof(buf), "hello world", 11, 6, 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "world");
}

TEST(gstrsub_emoji) {
  char buf[32];
  /* Extract the wave emoji from "Hi 👋🏽!" */
  size_t n = gstrsub(buf, sizeof(buf), MIXED, MIXED_LEN, 3, 1);
  ASSERT_EQ_SIZE(n, 8);
  ASSERT(memcmp(buf, WAVE_SKIN, 8) == 0);
}

TEST(gstrsub_beyond_end) {
  char buf[32];
  size_t n = gstrsub(buf, sizeof(buf), "hello", 5, 3, 10);
  ASSERT_EQ_SIZE(n, 2);
  ASSERT_STR_EQ(buf, "lo");
}

TEST(gstrsub_buffer_overflow) {
  char buf[4];
  size_t n = gstrsub(buf, sizeof(buf), "hello", 5, 0, 5);
  ASSERT_EQ_SIZE(n, 3); /* Only "hel" fits with null terminator */
  ASSERT_STR_EQ(buf, "hel");
}

/* ============================================================================
 * Copy Tests (gstrcpy, gstrncpy)
 * ============================================================================
 */

TEST(gstrcpy_basic) {
  char buf[32];
  size_t n = gstrcpy(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrcpy_emoji) {
  char buf[32];
  size_t n = gstrcpy(buf, sizeof(buf), FAMILY, FAMILY_LEN);
  ASSERT_EQ_SIZE(n, 18);
  ASSERT(memcmp(buf, FAMILY, 18) == 0);
}

TEST(gstrcpy_buffer_too_small) {
  char buf[4];
  size_t n = gstrcpy(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 3); /* Only complete graphemes that fit */
  ASSERT_STR_EQ(buf, "hel");
}

TEST(gstrcpy_emoji_truncate) {
  char buf[6]; /* Not enough for 8-byte emoji */
  size_t n = gstrcpy(buf, sizeof(buf), WAVE_SKIN, WAVE_SKIN_LEN);
  ASSERT_EQ_SIZE(n, 0); /* Can't fit complete grapheme */
  ASSERT_STR_EQ(buf, "");
}

TEST(gstrncpy_basic) {
  char buf[32];
  size_t n = gstrncpy(buf, sizeof(buf), "hello", 5, 3);
  ASSERT_EQ_SIZE(n, 3);
  ASSERT_STR_EQ(buf, "hel");
}

TEST(gstrncpy_more_than_available) {
  char buf[32];
  size_t n = gstrncpy(buf, sizeof(buf), "hi", 2, 10);
  ASSERT_EQ_SIZE(n, 2);
  ASSERT_STR_EQ(buf, "hi");
}

TEST(gstrncpy_mixed) {
  char buf[32];
  /* Copy first 4 graphemes of "Hi 👋🏽!" = "Hi 👋🏽" */
  size_t n = gstrncpy(buf, sizeof(buf), MIXED, MIXED_LEN, 4);
  ASSERT_EQ_SIZE(n, 11);
  ASSERT(memcmp(buf, "Hi \xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD", 11) == 0);
}

/* ============================================================================
 * Concatenation Tests (gstrcat, gstrncat)
 * ============================================================================
 */

TEST(gstrcat_basic) {
  char buf[32] = "hello";
  size_t n = gstrcat(buf, sizeof(buf), " world", 6);
  ASSERT_EQ_SIZE(n, 11);
  ASSERT_STR_EQ(buf, "hello world");
}

TEST(gstrcat_buffer_limit) {
  char buf[8] = "hi";
  size_t n = gstrcat(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 7); /* "hi" (2) + "hello" (5) = 7, fits in 8 with null */
  ASSERT_STR_EQ(buf, "hihello");
}

TEST(gstrcat_truncate) {
  char buf[6] = "hi";
  size_t n = gstrcat(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 5); /* "hi" (2) + "hel" (3) = 5, fits in 6 with null */
  ASSERT_STR_EQ(buf, "hihel");
}

TEST(gstrcat_emoji) {
  char buf[32] = "Hi ";
  size_t n = gstrcat(buf, sizeof(buf), WAVE_SKIN, WAVE_SKIN_LEN);
  ASSERT_EQ_SIZE(n, 11);
}

TEST(gstrncat_basic) {
  char buf[32] = "hello";
  size_t n = gstrncat(buf, sizeof(buf), " world", 6, 3);
  ASSERT_EQ_SIZE(n, 8);
  ASSERT_STR_EQ(buf, "hello wo");
}

TEST(gstrncat_emoji) {
  char buf[32] = "Hi";
  /* Append 1 grapheme (the wave+skin) from mixed string starting at offset 3
   */
  size_t n = gstrncat(buf, sizeof(buf), WAVE_SKIN, WAVE_SKIN_LEN, 1);
  ASSERT_EQ_SIZE(n, 10);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================
 */

TEST(null_inputs) {
  ASSERT_EQ_SIZE(gstrlen(NULL, 5), 0);
  ASSERT_EQ_SIZE(gstroff(NULL, 5, 0), 0);
  ASSERT_NULL(gstrat(NULL, 5, 0, NULL));
  ASSERT_EQ(gstrcmp(NULL, 0, NULL, 0), 0);
  ASSERT(gstrcmp(NULL, 0, "a", 1) < 0);
  ASSERT(gstrcmp("a", 1, NULL, 0) > 0);
  ASSERT_NULL(gstrchr(NULL, 5, "a", 1));
  ASSERT_NULL(gstrstr(NULL, 5, "a", 1));
}

TEST(empty_strings) {
  ASSERT_EQ_SIZE(gstrlen("", 0), 0);
  ASSERT_EQ(gstrcmp("", 0, "", 0), 0);
  ASSERT(gstrcmp("", 0, "a", 1) < 0);

  char buf[8] = "";
  ASSERT_EQ_SIZE(gstrcat(buf, sizeof(buf), "", 0), 0);
}

TEST(single_grapheme_strings) {
  ASSERT_EQ_SIZE(gstrlen("a", 1), 1);
  ASSERT_EQ_SIZE(gstrlen(EMOJI_SIMPLE, EMOJI_SIMPLE_LEN), 1);
  ASSERT_EQ_SIZE(gstrlen(FAMILY, FAMILY_LEN), 1);
}

/* ============================================================================
 * gstrncasecmp Tests
 * ============================================================================
 */

TEST(gstrncasecmp_basic) {
  ASSERT_EQ(gstrncasecmp("Hello", 5, "HELLO", 5, 3), 0);
  ASSERT_EQ(gstrncasecmp("Hello", 5, "Help", 4, 3), 0);
  ASSERT(gstrncasecmp("Hello", 5, "Help", 4, 4) < 0);
}

TEST(gstrncasecmp_zero_n) { ASSERT_EQ(gstrncasecmp("abc", 3, "xyz", 3, 0), 0); }

TEST(gstrncasecmp_null) {
  ASSERT(gstrncasecmp(NULL, 0, "a", 1, 1) < 0);
  ASSERT(gstrncasecmp("a", 1, NULL, 0, 1) > 0);
  ASSERT_EQ(gstrncasecmp(NULL, 0, NULL, 0, 1), 0);
}

TEST(gstrncasecmp_emoji) {
  /* Emoji should compare byte-exact */
  ASSERT_EQ(gstrncasecmp(EMOJI_SIMPLE, EMOJI_SIMPLE_LEN, EMOJI_SIMPLE,
                         EMOJI_SIMPLE_LEN, 1),
            0);
}

/* ============================================================================
 * gstrdup / gstrndup Tests
 * ============================================================================
 */

TEST(gstrdup_basic) {
  char *dup = gstrdup("hello", 5);
  ASSERT_NOT_NULL(dup);
  ASSERT_STR_EQ(dup, "hello");
  free(dup);
}

TEST(gstrdup_null) {
  char *dup = gstrdup(NULL, 5);
  ASSERT_NULL(dup);
}

TEST(gstrdup_emoji) {
  char *dup = gstrdup(FAMILY, FAMILY_LEN);
  ASSERT_NOT_NULL(dup);
  ASSERT(memcmp(dup, FAMILY, FAMILY_LEN) == 0);
  ASSERT(dup[FAMILY_LEN] == '\0');
  free(dup);
}

TEST(gstrndup_basic) {
  char *dup = gstrndup("hello", 5, 3);
  ASSERT_NOT_NULL(dup);
  ASSERT_STR_EQ(dup, "hel");
  free(dup);
}

TEST(gstrndup_null) {
  char *dup = gstrndup(NULL, 5, 3);
  ASSERT_NULL(dup);
}

TEST(gstrndup_zero_n) {
  char *dup = gstrndup("hello", 5, 0);
  ASSERT_NOT_NULL(dup);
  ASSERT_STR_EQ(dup, "");
  free(dup);
}

TEST(gstrndup_emoji) {
  /* Copy first 3 graphemes of "Hi 👋🏽!" */
  char *dup = gstrndup(MIXED, MIXED_LEN, 4);
  ASSERT_NOT_NULL(dup);
  ASSERT_EQ_SIZE(strlen(dup), 11); /* "Hi " + 8 byte emoji */
  free(dup);
}

TEST(gstrndup_more_than_available) {
  char *dup = gstrndup("hi", 2, 10);
  ASSERT_NOT_NULL(dup);
  ASSERT_STR_EQ(dup, "hi");
  free(dup);
}

/* ============================================================================
 * gstrrstr Tests
 * ============================================================================
 */

TEST(gstrrstr_basic) {
  const char *p = gstrrstr("hello hello", 11, "hello", 5);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello hello"), 6); /* second occurrence */
}

TEST(gstrrstr_single_match) {
  const char *p = gstrrstr("hello world", 11, "world", 5);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "hello world"), 6);
}

TEST(gstrrstr_not_found) {
  const char *p = gstrrstr("hello", 5, "xyz", 3);
  ASSERT_NULL(p);
}

TEST(gstrrstr_empty_needle) {
  const char *haystack = "hello";
  const char *p = gstrrstr(haystack, 5, "", 0);
  ASSERT_NOT_NULL(p);
  ASSERT(p == haystack + 5); /* Points to end */
}

TEST(gstrrstr_emoji) {
  /* Should NOT find partial emoji in ZWJ sequence */
  const char *p = gstrrstr(FAMILY, FAMILY_LEN, WOMAN, WOMAN_LEN);
  ASSERT_NULL(p);
}

/* ============================================================================
 * gstrcasestr Tests
 * ============================================================================
 */

TEST(gstrcasestr_basic) {
  const char *p = gstrcasestr("Hello World", 11, "WORLD", 5);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "Hello World"), 6);
}

TEST(gstrcasestr_not_found) {
  const char *p = gstrcasestr("hello", 5, "XYZ", 3);
  ASSERT_NULL(p);
}

TEST(gstrcasestr_empty_needle) {
  const char *haystack = "hello";
  const char *p = gstrcasestr(haystack, 5, "", 0);
  ASSERT_NOT_NULL(p);
  ASSERT(p == haystack);
}

TEST(gstrcasestr_mixed_case) {
  const char *p = gstrcasestr("HeLLo WoRLd", 11, "hello", 5);
  ASSERT_NOT_NULL(p);
  ASSERT_EQ_SIZE((size_t)(p - "HeLLo WoRLd"), 0);
}

/* ============================================================================
 * gstrcount Tests
 * ============================================================================
 */

TEST(gstrcount_basic) {
  ASSERT_EQ_SIZE(gstrcount("abcabcabc", 9, "abc", 3), 3);
}

TEST(gstrcount_single) { ASSERT_EQ_SIZE(gstrcount("hello", 5, "ell", 3), 1); }

TEST(gstrcount_not_found) {
  ASSERT_EQ_SIZE(gstrcount("hello", 5, "xyz", 3), 0);
}

TEST(gstrcount_empty_needle) {
  ASSERT_EQ_SIZE(gstrcount("hello", 5, "", 0), 0);
}

TEST(gstrcount_overlapping) {
  /* Non-overlapping: "aaa" in "aaaa" should be 1 (not 2) */
  ASSERT_EQ_SIZE(gstrcount("aaaa", 4, "aaa", 3), 1);
}

TEST(gstrcount_emoji) {
  /* Count emoji in mixed string */
  char s[32];
  memcpy(s, WAVE_SKIN, WAVE_SKIN_LEN);
  memcpy(s + WAVE_SKIN_LEN, "X", 1);
  memcpy(s + WAVE_SKIN_LEN + 1, WAVE_SKIN, WAVE_SKIN_LEN);
  s[WAVE_SKIN_LEN * 2 + 1] = '\0';
  ASSERT_EQ_SIZE(gstrcount(s, WAVE_SKIN_LEN * 2 + 1, WAVE_SKIN, WAVE_SKIN_LEN),
                 2);
}

/* ============================================================================
 * gstrsep Tests
 * ============================================================================
 */

TEST(gstrsep_basic) {
  const char *s = "a,b,c";
  size_t len = 5;
  size_t tok_len;

  const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 1);
  ASSERT(*tok == 'a');

  tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 1);
  ASSERT(*tok == 'b');

  tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 1);
  ASSERT(*tok == 'c');

  tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NULL(tok);
}

TEST(gstrsep_empty_token) {
  const char *s = "a,,b";
  size_t len = 4;
  size_t tok_len;

  const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 1);

  tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 0); /* Empty token */

  tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 1);
}

TEST(gstrsep_no_delimiter) {
  const char *s = "hello";
  size_t len = 5;
  size_t tok_len;

  const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 5);
  ASSERT_NULL(s);
}

TEST(gstrsep_multi_delimiter) {
  const char *s = "a;b,c";
  size_t len = 5;
  size_t tok_len;

  const char *tok = gstrsep(&s, &len, ",;", 2, &tok_len);
  ASSERT_NOT_NULL(tok);
  ASSERT_EQ_SIZE(tok_len, 1);
  ASSERT(*tok == 'a');
}

/* ============================================================================
 * gstrltrim / gstrrtrim / gstrtrim Tests
 * ============================================================================
 */

TEST(gstrltrim_basic) {
  char buf[32];
  size_t n = gstrltrim(buf, sizeof(buf), "  hello", 7);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrltrim_tabs) {
  char buf[32];
  size_t n = gstrltrim(buf, sizeof(buf), "\t\thello", 7);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrltrim_all_whitespace) {
  char buf[32];
  size_t n = gstrltrim(buf, sizeof(buf), "   \t\n", 5);
  ASSERT_EQ_SIZE(n, 0);
  ASSERT_STR_EQ(buf, "");
}

TEST(gstrltrim_no_whitespace) {
  char buf[32];
  size_t n = gstrltrim(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrltrim_emoji) {
  char buf[32];
  /* Space + emoji */
  char src[16];
  src[0] = ' ';
  memcpy(src + 1, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  size_t n = gstrltrim(buf, sizeof(buf), src, 1 + EMOJI_SIMPLE_LEN);
  ASSERT_EQ_SIZE(n, EMOJI_SIMPLE_LEN);
  ASSERT(memcmp(buf, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN) == 0);
}

TEST(gstrrtrim_basic) {
  char buf[32];
  size_t n = gstrrtrim(buf, sizeof(buf), "hello  ", 7);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrrtrim_mixed_whitespace) {
  char buf[32];
  size_t n = gstrrtrim(buf, sizeof(buf), "hello \t\n", 8);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrrtrim_no_whitespace) {
  char buf[32];
  size_t n = gstrrtrim(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrtrim_basic) {
  char buf[32];
  size_t n = gstrtrim(buf, sizeof(buf), "  hello  ", 9);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrtrim_tabs_and_newlines) {
  char buf[32];
  size_t n = gstrtrim(buf, sizeof(buf), "\t\nhello\r\n", 9);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrtrim_only_whitespace) {
  char buf[32];
  size_t n = gstrtrim(buf, sizeof(buf), "   \t\n  ", 7);
  ASSERT_EQ_SIZE(n, 0);
  ASSERT_STR_EQ(buf, "");
}

TEST(gstrtrim_emoji) {
  char buf[32];
  /* Space + emoji + space */
  char src[16];
  src[0] = ' ';
  memcpy(src + 1, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  src[1 + EMOJI_SIMPLE_LEN] = ' ';
  size_t n = gstrtrim(buf, sizeof(buf), src, 2 + EMOJI_SIMPLE_LEN);
  ASSERT_EQ_SIZE(n, EMOJI_SIMPLE_LEN);
  ASSERT(memcmp(buf, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN) == 0);
}

/* ============================================================================
 * gstrrev Tests
 * ============================================================================
 */

TEST(gstrrev_ascii) {
  char buf[32];
  size_t n = gstrrev(buf, sizeof(buf), "hello", 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "olleh");
}

TEST(gstrrev_single_char) {
  char buf[32];
  size_t n = gstrrev(buf, sizeof(buf), "a", 1);
  ASSERT_EQ_SIZE(n, 1);
  ASSERT_STR_EQ(buf, "a");
}

TEST(gstrrev_empty) {
  char buf[32];
  size_t n = gstrrev(buf, sizeof(buf), "", 0);
  ASSERT_EQ_SIZE(n, 0);
  ASSERT_STR_EQ(buf, "");
}

TEST(gstrrev_emoji) {
  char buf[32];
  /* Reverse "A👋🏽B" */
  char src[16];
  src[0] = 'A';
  memcpy(src + 1, WAVE_SKIN, WAVE_SKIN_LEN);
  src[1 + WAVE_SKIN_LEN] = 'B';
  size_t src_len = 2 + WAVE_SKIN_LEN;

  size_t n = gstrrev(buf, sizeof(buf), src, src_len);
  ASSERT_EQ_SIZE(n, src_len);
  /* Should be "B👋🏽A" */
  ASSERT(buf[0] == 'B');
  ASSERT(memcmp(buf + 1, WAVE_SKIN, WAVE_SKIN_LEN) == 0);
  ASSERT(buf[1 + WAVE_SKIN_LEN] == 'A');
}

TEST(gstrrev_family) {
  char buf[32];
  /* Reversing a single grapheme should return the same */
  size_t n = gstrrev(buf, sizeof(buf), FAMILY, FAMILY_LEN);
  ASSERT_EQ_SIZE(n, FAMILY_LEN);
  ASSERT(memcmp(buf, FAMILY, FAMILY_LEN) == 0);
}

TEST(gstrrev_buffer_overflow) {
  char buf[4];
  size_t n = gstrrev(buf, sizeof(buf), "hello", 5);
  /* Can fit 3 chars + null */
  ASSERT_EQ_SIZE(n, 3);
  ASSERT_STR_EQ(buf, "oll"); /* Last 3 chars reversed */
}

/* ============================================================================
 * gstrreplace Tests
 * ============================================================================
 */

TEST(gstrreplace_basic) {
  char buf[32];
  size_t n =
      gstrreplace(buf, sizeof(buf), "hello world", 11, "world", 5, "there", 5);
  ASSERT_EQ_SIZE(n, 11);
  ASSERT_STR_EQ(buf, "hello there");
}

TEST(gstrreplace_multiple) {
  char buf[32];
  size_t n = gstrreplace(buf, sizeof(buf), "aXaXa", 5, "X", 1, "Y", 1);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "aYaYa");
}

TEST(gstrreplace_grow) {
  char buf[32];
  size_t n = gstrreplace(buf, sizeof(buf), "a-b-c", 5, "-", 1, "---", 3);
  ASSERT_EQ_SIZE(n, 9);
  ASSERT_STR_EQ(buf, "a---b---c");
}

TEST(gstrreplace_shrink) {
  char buf[32];
  size_t n = gstrreplace(buf, sizeof(buf), "aXXXb", 5, "XXX", 3, "Y", 1);
  ASSERT_EQ_SIZE(n, 3);
  ASSERT_STR_EQ(buf, "aYb");
}

TEST(gstrreplace_delete) {
  char buf[32];
  size_t n = gstrreplace(buf, sizeof(buf), "a-b-c", 5, "-", 1, "", 0);
  ASSERT_EQ_SIZE(n, 3);
  ASSERT_STR_EQ(buf, "abc");
}

TEST(gstrreplace_no_match) {
  char buf[32];
  size_t n = gstrreplace(buf, sizeof(buf), "hello", 5, "xyz", 3, "abc", 3);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrreplace_empty_old) {
  char buf[32];
  size_t n = gstrreplace(buf, sizeof(buf), "hello", 5, "", 0, "X", 1);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello"); /* No replacement for empty pattern */
}

TEST(gstrreplace_emoji) {
  char buf[64];
  /* Replace emoji with text */
  char src[16];
  src[0] = 'H';
  src[1] = 'i';
  memcpy(src + 2, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  size_t n = gstrreplace(buf, sizeof(buf), src, 2 + EMOJI_SIMPLE_LEN,
                         EMOJI_SIMPLE, EMOJI_SIMPLE_LEN, ":)", 2);
  ASSERT_EQ_SIZE(n, 4);
  ASSERT_STR_EQ(buf, "Hi:)");
}

TEST(gstrreplace_buffer_overflow) {
  char buf[8];
  size_t n = gstrreplace(buf, sizeof(buf), "aXb", 3, "X", 1, "YYYY", 4);
  /* "aYYYYb" is 6 bytes, fits in 8 with null */
  ASSERT_EQ_SIZE(n, 6);
  ASSERT_STR_EQ(buf, "aYYYYb");
}

/* ============================================================================
 * gstrstartswith/gstrendswith Tests
 * ============================================================================
 */

TEST(gstrstartswith_basic) {
  ASSERT(gstrstartswith("hello world", 11, "hello", 5) == 1);
  ASSERT(gstrstartswith("hello world", 11, "world", 5) == 0);
}

TEST(gstrstartswith_emoji) {
  /* Family emoji + text */
  char src[32];
  memcpy(src, FAMILY, FAMILY_LEN);
  memcpy(src + FAMILY_LEN, " hello", 6);
  ASSERT(gstrstartswith(src, FAMILY_LEN + 6, FAMILY, FAMILY_LEN) == 1);
  /* Just the first codepoint should NOT match */
  ASSERT(gstrstartswith(src, FAMILY_LEN + 6, "\xF0\x9F\x91\xA8", 4) == 0);
}

TEST(gstrstartswith_empty) {
  ASSERT(gstrstartswith("hello", 5, "", 0) == 1);
  ASSERT(gstrstartswith("", 0, "", 0) == 1);
}

TEST(gstrendswith_basic) {
  ASSERT(gstrendswith("hello.txt", 9, ".txt", 4) == 1);
  ASSERT(gstrendswith("hello.txt", 9, ".md", 3) == 0);
}

TEST(gstrendswith_emoji) {
  char src[32];
  memcpy(src, "test", 4);
  memcpy(src + 4, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  ASSERT(gstrendswith(src, 4 + EMOJI_SIMPLE_LEN, EMOJI_SIMPLE,
                      EMOJI_SIMPLE_LEN) == 1);
}

TEST(gstrendswith_empty) { ASSERT(gstrendswith("hello", 5, "", 0) == 1); }

/* ============================================================================
 * gstrwidth Tests
 * ============================================================================
 */

TEST(gstrwidth_ascii) { ASSERT_EQ_SIZE(gstrwidth("Hello", 5), 5); }

TEST(gstrwidth_cjk) {
  /* "日本" - 2 wide characters = 4 columns */
  ASSERT_EQ_SIZE(gstrwidth("\xE6\x97\xA5\xE6\x9C\xAC", 6), 4);
}

TEST(gstrwidth_emoji) {
  /* Single emoji should be 2 columns */
  ASSERT_EQ_SIZE(gstrwidth(EMOJI_SIMPLE, EMOJI_SIMPLE_LEN), 2);
}

TEST(gstrwidth_combining) {
  /* "cafe" + combining acute = 4 visible chars (combining mark is 0 width) */
  ASSERT_EQ_SIZE(gstrwidth(CAFE_DECOMPOSED, CAFE_DECOMPOSED_LEN), 4);
}

TEST(gstrwidth_zwj_family) {
  /* ZWJ family emoji (👨‍👩‍👧) should be 2 columns, not 6 */
  ASSERT_EQ_SIZE(gstrwidth(FAMILY, FAMILY_LEN), 2);
}

TEST(gstrwidth_flag) {
  /* Flag emoji (🇨🇦) should be 2 columns, not 4 */
  ASSERT_EQ_SIZE(gstrwidth(FLAG_CA, FLAG_CA_LEN), 2);
}

TEST(gstrwidth_skin_tone) {
  /* Emoji with skin tone modifier should be 2 columns */
  ASSERT_EQ_SIZE(gstrwidth(WAVE_SKIN, WAVE_SKIN_LEN), 2);
}

/* ============================================================================
 * Column-Width Truncation Tests (gstrwtrunc)
 * ============================================================================
 */

TEST(gstrwtrunc_ascii) {
  char buf[32];
  size_t n = gstrwtrunc(buf, sizeof(buf), "Hello World", 11, 5);
  ASSERT_STR_EQ(buf, "Hello");
  ASSERT_EQ_SIZE(n, 5);
}

TEST(gstrwtrunc_cjk) {
  /* "日本語" - each CJK char is 2 columns */
  const char *cjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
  char buf[32];
  /* Truncate to 4 columns = 2 CJK characters */
  size_t n = gstrwtrunc(buf, sizeof(buf), cjk, 9, 4);
  ASSERT_EQ_SIZE(n, 6); /* 2 chars × 3 bytes each */
  /* Truncate to 3 columns = only 1 CJK char fits (2 cols), can't fit half */
  n = gstrwtrunc(buf, sizeof(buf), cjk, 9, 3);
  ASSERT_EQ_SIZE(n, 3); /* 1 char × 3 bytes */
}

TEST(gstrwtrunc_emoji) {
  char buf[32];
  /* Family emoji is 2 columns; truncate to 1 = nothing fits */
  size_t n = gstrwtrunc(buf, sizeof(buf), FAMILY, FAMILY_LEN, 1);
  ASSERT_EQ_SIZE(n, 0);
  ASSERT_STR_EQ(buf, "");
  /* Truncate to 2 = emoji fits */
  n = gstrwtrunc(buf, sizeof(buf), FAMILY, FAMILY_LEN, 2);
  ASSERT_EQ_SIZE(n, FAMILY_LEN);
}

TEST(gstrwtrunc_mixed) {
  /* "Hi 👋🏽!" = H(1) + i(1) + space(1) + wave(2) + !(1) = 6 columns */
  char buf[32];
  /* Truncate to 4 columns = "Hi " + wave doesn't fit = "Hi " */
  size_t n = gstrwtrunc(buf, sizeof(buf), MIXED, MIXED_LEN, 4);
  ASSERT_STR_EQ(buf, "Hi ");
  ASSERT_EQ_SIZE(n, 3);
}

TEST(gstrwtrunc_empty) {
  char buf[32];
  size_t n = gstrwtrunc(buf, sizeof(buf), "", 0, 10);
  ASSERT_EQ_SIZE(n, 0);
  ASSERT_STR_EQ(buf, "");
}

TEST(gstrwtrunc_zero_cols) {
  char buf[32];
  size_t n = gstrwtrunc(buf, sizeof(buf), "Hello", 5, 0);
  ASSERT_EQ_SIZE(n, 0);
  ASSERT_STR_EQ(buf, "");
}

/* ============================================================================
 * Column-Width Padding Tests (gstrwlpad, gstrwrpad, gstrwpad)
 * ============================================================================
 */

TEST(gstrwlpad_basic) {
  char buf[32];
  size_t n = gstrwlpad(buf, sizeof(buf), "Hi", 2, 5, NULL, 0);
  ASSERT_STR_EQ(buf, "   Hi");
  ASSERT_EQ_SIZE(n, 5);
}

TEST(gstrwlpad_cjk) {
  /* Pad CJK string to 6 columns */
  const char *cjk = "\xE6\x97\xA5"; /* "日" = 2 columns */
  char buf[32];
  size_t n = gstrwlpad(buf, sizeof(buf), cjk, 3, 6, NULL, 0);
  /* Need 4 columns of padding = 4 spaces */
  ASSERT_STR_EQ(buf, "    \xE6\x97\xA5");
  ASSERT_EQ_SIZE(n, 7); /* 4 spaces + 3 bytes */
}

TEST(gstrwlpad_already_wide) {
  char buf[32];
  size_t n = gstrwlpad(buf, sizeof(buf), "Hello", 5, 3, NULL, 0);
  /* Source wider than target, should truncate */
  ASSERT_STR_EQ(buf, "Hel");
  ASSERT_EQ_SIZE(n, 3);
}

TEST(gstrwrpad_basic) {
  char buf[32];
  size_t n = gstrwrpad(buf, sizeof(buf), "Hi", 2, 5, NULL, 0);
  ASSERT_STR_EQ(buf, "Hi   ");
  ASSERT_EQ_SIZE(n, 5);
}

TEST(gstrwrpad_cjk) {
  const char *cjk = "\xE6\x97\xA5"; /* "日" = 2 columns */
  char buf[32];
  size_t n = gstrwrpad(buf, sizeof(buf), cjk, 3, 6, NULL, 0);
  ASSERT_STR_EQ(buf, "\xE6\x97\xA5    ");
  ASSERT_EQ_SIZE(n, 7);
}

TEST(gstrwpad_basic) {
  char buf[32];
  size_t n = gstrwpad(buf, sizeof(buf), "Hi", 2, 6, NULL, 0);
  /* 4 columns padding total, split 2 left + 2 right */
  ASSERT_STR_EQ(buf, "  Hi  ");
  ASSERT_EQ_SIZE(n, 6);
}

TEST(gstrwpad_odd_padding) {
  char buf[32];
  size_t n = gstrwpad(buf, sizeof(buf), "X", 1, 6, NULL, 0);
  /* 5 columns padding, split 2 left + 3 right */
  ASSERT_STR_EQ(buf, "  X   ");
  ASSERT_EQ_SIZE(n, 6);
}

TEST(gstrwpad_emoji_source) {
  char buf[64];
  /* Family emoji is 2 columns, pad to 6 */
  size_t n = gstrwpad(buf, sizeof(buf), FAMILY, FAMILY_LEN, 6, NULL, 0);
  /* 4 columns padding, split 2 left + 2 right */
  ASSERT_EQ_SIZE(n, FAMILY_LEN + 4); /* 18 bytes + 4 spaces */
}

TEST(gstrwpad_wide_pad_char) {
  char buf[32];
  const char *wide_pad = "\xE3\x80\x80"; /* Ideographic space (2 cols) */
  size_t n = gstrwlpad(buf, sizeof(buf), "X", 1, 5, wide_pad, 3);
  /* Need 4 columns = 2 wide pads */
  ASSERT_EQ_SIZE(n, 7); /* 6 bytes for pads + 1 for X */
}

/* ============================================================================
 * gstrlower/gstrupper Tests
 * ============================================================================
 */

TEST(gstrlower_basic) {
  char buf[32];
  size_t n = gstrlower(buf, sizeof(buf), "HELLO World", 11);
  ASSERT_EQ_SIZE(n, 11);
  ASSERT_STR_EQ(buf, "hello world");
}

TEST(gstrlower_emoji) {
  char buf[32];
  char src[32];
  memcpy(src, "ABC", 3);
  memcpy(src + 3, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  size_t n = gstrlower(buf, sizeof(buf), src, 3 + EMOJI_SIMPLE_LEN);
  ASSERT_EQ_SIZE(n, 3 + EMOJI_SIMPLE_LEN);
  ASSERT(memcmp(buf, "abc", 3) == 0);
  ASSERT(memcmp(buf + 3, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN) == 0);
}

TEST(gstrupper_basic) {
  char buf[32];
  size_t n = gstrupper(buf, sizeof(buf), "Hello World", 11);
  ASSERT_EQ_SIZE(n, 11);
  ASSERT_STR_EQ(buf, "HELLO WORLD");
}

/* ============================================================================
 * gstrellipsis Tests
 * ============================================================================
 */

TEST(gstrellipsis_no_truncate) {
  char buf[32];
  size_t n = gstrellipsis(buf, sizeof(buf), "hi", 2, 10, "...", 3);
  ASSERT_EQ_SIZE(n, 2);
  ASSERT_STR_EQ(buf, "hi");
}

TEST(gstrellipsis_truncate) {
  char buf[32];
  gstrellipsis(buf, sizeof(buf), "hello world", 11, 8, "...", 3);
  /* 8 graphemes max: 5 text + 3 ellipsis */
  ASSERT_STR_EQ(buf, "hello...");
}

TEST(gstrellipsis_emoji) {
  char buf[64];
  char src[32];
  memcpy(src, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  memcpy(src + EMOJI_SIMPLE_LEN, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  memcpy(src + 2 * EMOJI_SIMPLE_LEN, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  /* 3 emoji, truncate to 2 + ellipsis */
  size_t n =
      gstrellipsis(buf, sizeof(buf), src, 3 * EMOJI_SIMPLE_LEN, 3, ".", 1);
  /* Should be 1 emoji + "." */
  ASSERT(n > 0);
}

/* ============================================================================
 * gstrfill Tests
 * ============================================================================
 */

TEST(gstrfill_basic) {
  char buf[32];
  size_t n = gstrfill(buf, sizeof(buf), "-", 1, 5);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "-----");
}

TEST(gstrfill_emoji) {
  char buf[64];
  size_t n = gstrfill(buf, sizeof(buf), EMOJI_SIMPLE, EMOJI_SIMPLE_LEN, 3);
  ASSERT_EQ_SIZE(n, 3 * EMOJI_SIMPLE_LEN);
}

TEST(gstrfill_overflow) {
  char buf[4];
  size_t n = gstrfill(buf, sizeof(buf), "ab", 2, 10);
  /* Can only fit 1 "ab" (2 bytes + null) */
  ASSERT_EQ_SIZE(n, 2);
  ASSERT_STR_EQ(buf, "ab");
}

/* ============================================================================
 * gstrlpad/gstrrpad/gstrpad Tests
 * ============================================================================
 */

TEST(gstrlpad_basic) {
  char buf[32];
  size_t n = gstrlpad(buf, sizeof(buf), "hi", 2, 5, " ", 1);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "   hi");
}

TEST(gstrlpad_already_wide) {
  char buf[32];
  size_t n = gstrlpad(buf, sizeof(buf), "hello", 5, 3, " ", 1);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hello");
}

TEST(gstrrpad_basic) {
  char buf[32];
  size_t n = gstrrpad(buf, sizeof(buf), "hi", 2, 5, " ", 1);
  ASSERT_EQ_SIZE(n, 5);
  ASSERT_STR_EQ(buf, "hi   ");
}

TEST(gstrpad_basic) {
  char buf[32];
  size_t n = gstrpad(buf, sizeof(buf), "hi", 2, 6, " ", 1);
  /* 6 - 2 = 4 padding, split as 2 left + 2 right */
  ASSERT_EQ_SIZE(n, 6);
  ASSERT_STR_EQ(buf, "  hi  ");
}

TEST(gstrpad_emoji_padding) {
  char buf[64];
  size_t n =
      gstrpad(buf, sizeof(buf), "x", 1, 3, EMOJI_SIMPLE, EMOJI_SIMPLE_LEN);
  /* 3 graphemes: 1 emoji + "x" + 1 emoji */
  ASSERT(n > 1);
}

/* ============================================================================
 * utf8_* API Tests
 * ============================================================================
 */

TEST(utf8_decode_ascii) {
  uint32_t cp;
  int bytes = utf8_decode("A", 1, &cp);
  ASSERT_EQ(bytes, 1);
  ASSERT_EQ(cp, 'A');
}

TEST(utf8_decode_multibyte) {
  uint32_t cp;
  /* U+00E9 (é) = 0xC3 0xA9 */
  int bytes = utf8_decode("\xC3\xA9", 2, &cp);
  ASSERT_EQ(bytes, 2);
  ASSERT_EQ(cp, 0xE9);
}

TEST(utf8_encode_ascii) {
  char buf[4];
  int bytes = utf8_encode('A', buf);
  ASSERT_EQ(bytes, 1);
  ASSERT_EQ(buf[0], 'A');
}

TEST(utf8_encode_multibyte) {
  char buf[4];
  /* U+00E9 (é) */
  int bytes = utf8_encode(0xE9, buf);
  ASSERT_EQ(bytes, 2);
  ASSERT_EQ((unsigned char)buf[0], 0xC3);
  ASSERT_EQ((unsigned char)buf[1], 0xA9);
}

TEST(utf8_valid_ok) {
  int err;
  ASSERT(utf8_valid("Hello", 5, &err) == 1);
}

TEST(utf8_valid_bad) {
  int err;
  /* Invalid continuation byte */
  ASSERT(utf8_valid("\xFF\x00", 2, &err) == 0);
  ASSERT_EQ(err, 0);
}

TEST(utf8_cpcount_basic) {
  /* "café" with precomposed é = 5 codepoints */
  ASSERT_EQ(utf8_cpcount("caf\xC3\xA9", 5), 4);
}

TEST(utf8_cpwidth_basic) {
  ASSERT_EQ(utf8_cpwidth('A'), 1);
  ASSERT_EQ(utf8_cpwidth(0x3042), 2); /* Hiragana 'a' - wide */
  ASSERT_EQ(utf8_cpwidth(0x0301), 0); /* Combining acute - zero width */
}

TEST(utf8_truncate_basic) {
  /* CJK string: "日本語" (each char is 2 columns) */
  const char *s = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
  int offset = utf8_truncate(s, 9, 4);
  /* 4 columns = 2 characters = 6 bytes */
  ASSERT_EQ(offset, 6);
}

/* ============================================================================
 * Main
 * ============================================================================
 */

int main(void) {
  printf("\ngstr.h - Grapheme String Library Tests\n");
  printf("Version: %s\n", GSTR_VERSION);
  printf("Build: %s\n\n", GSTR_BUILD_ID);

  printf("Length Tests:\n");
  RUN(gstrlen_ascii);
  RUN(gstrlen_empty);
  RUN(gstrlen_null);
  RUN(gstrlen_emoji_simple);
  RUN(gstrlen_family_zwj);
  RUN(gstrlen_flag);
  RUN(gstrlen_skin_tone);
  RUN(gstrlen_combining_marks);
  RUN(gstrlen_mixed);
  RUN(gstrlen_hangul);
  RUN(gstrnlen_basic);
  RUN(gstrnlen_mixed);

  printf("\nIndexing Tests:\n");
  RUN(gstroff_ascii);
  RUN(gstroff_mixed);
  RUN(gstrat_ascii);
  RUN(gstrat_emoji);
  RUN(gstrat_out_of_bounds);
  RUN(gstrat_family);

  printf("\nComparison Tests:\n");
  RUN(gstrcmp_equal_ascii);
  RUN(gstrcmp_less_ascii);
  RUN(gstrcmp_greater_ascii);
  RUN(gstrcmp_shorter);
  RUN(gstrcmp_longer);
  RUN(gstrcmp_emoji);
  RUN(gstrcmp_different_normalization);
  RUN(gstrncmp_basic);
  RUN(gstrncmp_mixed);
  RUN(gstrcasecmp_basic);
  RUN(gstrcasecmp_different);

  printf("\nSearch Tests:\n");
  RUN(gstrchr_ascii);
  RUN(gstrchr_not_found);
  RUN(gstrchr_emoji);
  RUN(gstrchr_partial_emoji_not_found);
  RUN(gstrrchr_basic);
  RUN(gstrrchr_single_match);
  RUN(gstrstr_basic);
  RUN(gstrstr_at_start);
  RUN(gstrstr_at_end);
  RUN(gstrstr_not_found);
  RUN(gstrstr_empty_needle);
  RUN(gstrstr_emoji);
  RUN(gstrstr_full_match);

  printf("\nSpan Tests:\n");
  RUN(gstrspn_basic);
  RUN(gstrspn_no_match);
  RUN(gstrspn_all_match);
  RUN(gstrcspn_basic);
  RUN(gstrcspn_no_reject);
  RUN(gstrpbrk_basic);
  RUN(gstrpbrk_not_found);

  printf("\nExtraction Tests:\n");
  RUN(gstrsub_basic);
  RUN(gstrsub_middle);
  RUN(gstrsub_emoji);
  RUN(gstrsub_beyond_end);
  RUN(gstrsub_buffer_overflow);

  printf("\nCopy Tests:\n");
  RUN(gstrcpy_basic);
  RUN(gstrcpy_emoji);
  RUN(gstrcpy_buffer_too_small);
  RUN(gstrcpy_emoji_truncate);
  RUN(gstrncpy_basic);
  RUN(gstrncpy_more_than_available);
  RUN(gstrncpy_mixed);

  printf("\nConcatenation Tests:\n");
  RUN(gstrcat_basic);
  RUN(gstrcat_buffer_limit);
  RUN(gstrcat_truncate);
  RUN(gstrcat_emoji);
  RUN(gstrncat_basic);
  RUN(gstrncat_emoji);

  printf("\nEdge Cases:\n");
  RUN(null_inputs);
  RUN(empty_strings);
  RUN(single_grapheme_strings);

  printf("\ngstrncasecmp Tests:\n");
  RUN(gstrncasecmp_basic);
  RUN(gstrncasecmp_zero_n);
  RUN(gstrncasecmp_null);
  RUN(gstrncasecmp_emoji);

  printf("\ngstrdup/gstrndup Tests:\n");
  RUN(gstrdup_basic);
  RUN(gstrdup_null);
  RUN(gstrdup_emoji);
  RUN(gstrndup_basic);
  RUN(gstrndup_null);
  RUN(gstrndup_zero_n);
  RUN(gstrndup_emoji);
  RUN(gstrndup_more_than_available);

  printf("\ngstrrstr Tests:\n");
  RUN(gstrrstr_basic);
  RUN(gstrrstr_single_match);
  RUN(gstrrstr_not_found);
  RUN(gstrrstr_empty_needle);
  RUN(gstrrstr_emoji);

  printf("\ngstrcasestr Tests:\n");
  RUN(gstrcasestr_basic);
  RUN(gstrcasestr_not_found);
  RUN(gstrcasestr_empty_needle);
  RUN(gstrcasestr_mixed_case);

  printf("\ngstrcount Tests:\n");
  RUN(gstrcount_basic);
  RUN(gstrcount_single);
  RUN(gstrcount_not_found);
  RUN(gstrcount_empty_needle);
  RUN(gstrcount_overlapping);
  RUN(gstrcount_emoji);

  printf("\ngstrsep Tests:\n");
  RUN(gstrsep_basic);
  RUN(gstrsep_empty_token);
  RUN(gstrsep_no_delimiter);
  RUN(gstrsep_multi_delimiter);

  printf("\nTrim Tests:\n");
  RUN(gstrltrim_basic);
  RUN(gstrltrim_tabs);
  RUN(gstrltrim_all_whitespace);
  RUN(gstrltrim_no_whitespace);
  RUN(gstrltrim_emoji);
  RUN(gstrrtrim_basic);
  RUN(gstrrtrim_mixed_whitespace);
  RUN(gstrrtrim_no_whitespace);
  RUN(gstrtrim_basic);
  RUN(gstrtrim_tabs_and_newlines);
  RUN(gstrtrim_only_whitespace);
  RUN(gstrtrim_emoji);

  printf("\ngstrrev Tests:\n");
  RUN(gstrrev_ascii);
  RUN(gstrrev_single_char);
  RUN(gstrrev_empty);
  RUN(gstrrev_emoji);
  RUN(gstrrev_family);
  RUN(gstrrev_buffer_overflow);

  printf("\ngstrreplace Tests:\n");
  RUN(gstrreplace_basic);
  RUN(gstrreplace_multiple);
  RUN(gstrreplace_grow);
  RUN(gstrreplace_shrink);
  RUN(gstrreplace_delete);
  RUN(gstrreplace_no_match);
  RUN(gstrreplace_empty_old);
  RUN(gstrreplace_emoji);
  RUN(gstrreplace_buffer_overflow);

  printf("\ngstrstartswith/gstrendswith Tests:\n");
  RUN(gstrstartswith_basic);
  RUN(gstrstartswith_emoji);
  RUN(gstrstartswith_empty);
  RUN(gstrendswith_basic);
  RUN(gstrendswith_emoji);
  RUN(gstrendswith_empty);

  printf("\ngstrwidth Tests:\n");
  RUN(gstrwidth_ascii);
  RUN(gstrwidth_cjk);
  RUN(gstrwidth_emoji);
  RUN(gstrwidth_combining);
  RUN(gstrwidth_zwj_family);
  RUN(gstrwidth_flag);
  RUN(gstrwidth_skin_tone);

  printf("\ngstrwtrunc Tests:\n");
  RUN(gstrwtrunc_ascii);
  RUN(gstrwtrunc_cjk);
  RUN(gstrwtrunc_emoji);
  RUN(gstrwtrunc_mixed);
  RUN(gstrwtrunc_empty);
  RUN(gstrwtrunc_zero_cols);

  printf("\ngstrwlpad/gstrwrpad/gstrwpad Tests:\n");
  RUN(gstrwlpad_basic);
  RUN(gstrwlpad_cjk);
  RUN(gstrwlpad_already_wide);
  RUN(gstrwrpad_basic);
  RUN(gstrwrpad_cjk);
  RUN(gstrwpad_basic);
  RUN(gstrwpad_odd_padding);
  RUN(gstrwpad_emoji_source);
  RUN(gstrwpad_wide_pad_char);

  printf("\ngstrlower/gstrupper Tests:\n");
  RUN(gstrlower_basic);
  RUN(gstrlower_emoji);
  RUN(gstrupper_basic);

  printf("\ngstrellipsis Tests:\n");
  RUN(gstrellipsis_no_truncate);
  RUN(gstrellipsis_truncate);
  RUN(gstrellipsis_emoji);

  printf("\ngstrfill Tests:\n");
  RUN(gstrfill_basic);
  RUN(gstrfill_emoji);
  RUN(gstrfill_overflow);

  printf("\ngstrlpad/gstrrpad/gstrpad Tests:\n");
  RUN(gstrlpad_basic);
  RUN(gstrlpad_already_wide);
  RUN(gstrrpad_basic);
  RUN(gstrpad_basic);
  RUN(gstrpad_emoji_padding);

  printf("\nutf8_* API Tests:\n");
  RUN(utf8_decode_ascii);
  RUN(utf8_decode_multibyte);
  RUN(utf8_encode_ascii);
  RUN(utf8_encode_multibyte);
  RUN(utf8_valid_ok);
  RUN(utf8_valid_bad);
  RUN(utf8_cpcount_basic);
  RUN(utf8_cpwidth_basic);
  RUN(utf8_truncate_basic);

  printf("\n----------------------------------------\n");
  printf("Tests passed: %d\n", tests_passed);
  printf("Tests failed: %d\n", tests_failed);
  printf("----------------------------------------\n");

  return tests_failed > 0 ? 1 : 0;
}
