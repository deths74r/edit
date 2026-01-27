/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
 *
 * test_new_functions_stress.c - Comprehensive stress tests for 12 new gstr
 * functions
 *
 * Tests: gstrncasecmp, gstrdup, gstrndup, gstrrstr, gstrcasestr, gstrcount,
 *        gstrsep, gstrltrim, gstrrtrim, gstrtrim, gstrrev, gstrreplace
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gstr.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS()                                                            \
  do {                                                                         \
    tests_passed++;                                                            \
  } while (0)
#define TEST_FAIL(msg)                                                         \
  do {                                                                         \
    printf("    FAIL: %s\n", msg);                                             \
    tests_failed++;                                                            \
  } while (0)

/* Complex test strings */

/* Simple emoji: 😀 */
static const char *EMOJI = "\xF0\x9F\x98\x80";
static const size_t EMOJI_LEN = 4;

/* ZWJ family: 👨‍👩‍👧 */
static const char *FAMILY = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"
                            "\xE2\x80\x8D\xF0\x9F\x91\xA7";
static const size_t FAMILY_LEN = 18;

/* Flag: 🇨🇦 */
static const char *FLAG = "\xF0\x9F\x87\xA8\xF0\x9F\x87\xA6";
static const size_t FLAG_LEN = 8;

/* Wave with skin tone: 👋🏽 */
static const char *WAVE = "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD";
static const size_t WAVE_LEN = 8;

/* Combining mark: é as e + combining acute */
static const char *COMBINING = "e\xCC\x81";
static const size_t COMBINING_LEN = 3;

/* Korean Hangul: 한글 */
static const char *HANGUL = "\xED\x95\x9C\xEA\xB8\x80";
static const size_t HANGUL_LEN = 6;

/* ============================================================================
 * gstrncasecmp comprehensive tests
 * ============================================================================
 */
static void test_gstrncasecmp_comprehensive(void) {
  printf("\n=== gstrncasecmp comprehensive ===\n");
  int passed = 0, failed = 0;

  /* Basic case folding */
  if (gstrncasecmp("ABC", 3, "abc", 3, 3) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ABC vs abc\n");
  }

  if (gstrncasecmp("AbCdEf", 6, "aBcDeF", 6, 6) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: mixed case\n");
  }

  /* Partial comparison */
  if (gstrncasecmp("HELLO", 5, "help", 4, 3) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: partial HEL vs hel\n");
  }

  if (gstrncasecmp("HELLO", 5, "help", 4, 4) < 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: HELL vs help should be <0\n");
  }

  /* n=0 always equal */
  if (gstrncasecmp("xyz", 3, "ABC", 3, 0) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: n=0 should be equal\n");
  }

  /* With emoji (should compare byte-exact) */
  if (gstrncasecmp(EMOJI, EMOJI_LEN, EMOJI, EMOJI_LEN, 1) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: emoji self-compare\n");
  }

  /* Mixed ASCII and emoji */
  char s1[32], s2[32];
  snprintf(s1, sizeof(s1), "A%sB", EMOJI);
  snprintf(s2, sizeof(s2), "a%sb", EMOJI);
  size_t len = 1 + EMOJI_LEN + 1;
  if (gstrncasecmp(s1, len, s2, len, 3) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: AemojiB vs aemojib\n");
  }

  /* Shorter string */
  if (gstrncasecmp("AB", 2, "ABC", 3, 3) < 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: shorter string\n");
  }

  /* NULL handling */
  if (gstrncasecmp(NULL, 0, "a", 1, 1) < 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: NULL first\n");
  }

  if (gstrncasecmp("a", 1, NULL, 0, 1) > 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: NULL second\n");
  }

  /* Numbers and special chars (unchanged) */
  if (gstrncasecmp("123!@#", 6, "123!@#", 6, 6) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: numbers unchanged\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrdup / gstrndup comprehensive tests
 * ============================================================================
 */
static void test_gstrdup_comprehensive(void) {
  printf("\n=== gstrdup/gstrndup comprehensive ===\n");
  int passed = 0, failed = 0;
  char *dup;

  /* Basic ASCII */
  dup = gstrdup("hello world", 11);
  if (dup && strcmp(dup, "hello world") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: basic ASCII\n");
  }
  free(dup);

  /* Empty string */
  dup = gstrdup("", 0);
  if (dup && strcmp(dup, "") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty string\n");
  }
  free(dup);

  /* Single emoji */
  dup = gstrdup(EMOJI, EMOJI_LEN);
  if (dup && memcmp(dup, EMOJI, EMOJI_LEN) == 0 && dup[EMOJI_LEN] == '\0')
    passed++;
  else {
    failed++;
    printf("  FAIL: single emoji\n");
  }
  free(dup);

  /* ZWJ family */
  dup = gstrdup(FAMILY, FAMILY_LEN);
  if (dup && memcmp(dup, FAMILY, FAMILY_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ZWJ family\n");
  }
  free(dup);

  /* Flag */
  dup = gstrdup(FLAG, FLAG_LEN);
  if (dup && memcmp(dup, FLAG, FLAG_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: flag\n");
  }
  free(dup);

  /* Combining marks */
  dup = gstrdup(COMBINING, COMBINING_LEN);
  if (dup && memcmp(dup, COMBINING, COMBINING_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: combining\n");
  }
  free(dup);

  /* NULL returns NULL */
  dup = gstrdup(NULL, 5);
  if (dup == NULL)
    passed++;
  else {
    failed++;
    printf("  FAIL: NULL should return NULL\n");
    free(dup);
  }

  /* gstrndup tests */

  /* First 3 graphemes of ASCII */
  dup = gstrndup("hello", 5, 3);
  if (dup && strcmp(dup, "hel") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ndup 3 chars\n");
  }
  free(dup);

  /* n=0 returns empty */
  dup = gstrndup("hello", 5, 0);
  if (dup && strcmp(dup, "") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ndup n=0\n");
  }
  free(dup);

  /* n > available */
  dup = gstrndup("hi", 2, 100);
  if (dup && strcmp(dup, "hi") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ndup n>available\n");
  }
  free(dup);

  /* Mixed with emoji: "Hi👋🏽!" - first 3 graphemes = "Hi👋🏽" */
  char mixed[32];
  snprintf(mixed, sizeof(mixed), "Hi%s!", WAVE);
  size_t mixed_len = 2 + WAVE_LEN + 1;
  dup = gstrndup(mixed, mixed_len, 3);
  if (dup && strlen(dup) == 2 + WAVE_LEN)
    passed++;
  else {
    failed++;
    printf("  FAIL: ndup mixed\n");
  }
  free(dup);

  /* Single ZWJ family = 1 grapheme */
  dup = gstrndup(FAMILY, FAMILY_LEN, 1);
  if (dup && memcmp(dup, FAMILY, FAMILY_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ndup family 1\n");
  }
  free(dup);

  /* Hangul: 한글 = 2 graphemes, take 1 */
  dup = gstrndup(HANGUL, HANGUL_LEN, 1);
  if (dup && strlen(dup) == 3)
    passed++; /* 한 is 3 bytes */
  else {
    failed++;
    printf("  FAIL: ndup hangul 1\n");
  }
  free(dup);

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrrstr comprehensive tests
 * ============================================================================
 */
static void test_gstrrstr_comprehensive(void) {
  printf("\n=== gstrrstr comprehensive ===\n");
  int passed = 0, failed = 0;
  const char *p;

  /* Multiple occurrences - should find last */
  p = gstrrstr("abcabcabc", 9, "abc", 3);
  if (p && (p - "abcabcabc") == 6)
    passed++;
  else {
    failed++;
    printf("  FAIL: multiple abc\n");
  }

  /* Single occurrence */
  p = gstrrstr("hello world", 11, "world", 5);
  if (p && (p - "hello world") == 6)
    passed++;
  else {
    failed++;
    printf("  FAIL: single world\n");
  }

  /* At start */
  p = gstrrstr("hello", 5, "hel", 3);
  if (p && (p - "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: at start\n");
  }

  /* At end */
  p = gstrrstr("hello", 5, "llo", 3);
  if (p && (p - "hello") == 2)
    passed++;
  else {
    failed++;
    printf("  FAIL: at end\n");
  }

  /* Not found */
  p = gstrrstr("hello", 5, "xyz", 3);
  if (p == NULL)
    passed++;
  else {
    failed++;
    printf("  FAIL: not found\n");
  }

  /* Empty needle - returns end */
  const char *hay = "hello";
  p = gstrrstr(hay, 5, "", 0);
  if (p == hay + 5)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty needle\n");
  }

  /* Emoji not in ZWJ sequence */
  p = gstrrstr(FAMILY, FAMILY_LEN, "\xF0\x9F\x91\xA9", 4); /* woman */
  if (p == NULL)
    passed++;
  else {
    failed++;
    printf("  FAIL: partial emoji\n");
  }

  /* Find emoji in string with multiple */
  char s[64];
  snprintf(s, sizeof(s), "A%sB%sC", EMOJI, EMOJI);
  size_t slen = 1 + EMOJI_LEN + 1 + EMOJI_LEN + 1;
  p = gstrrstr(s, slen, EMOJI, EMOJI_LEN);
  if (p && (p - s) == (int)(1 + EMOJI_LEN + 1))
    passed++;
  else {
    failed++;
    printf("  FAIL: last emoji\n");
  }

  /* Single char multiple times */
  p = gstrrstr("aaaaaa", 6, "a", 1);
  if (p && (p - "aaaaaa") == 5)
    passed++;
  else {
    failed++;
    printf("  FAIL: last a\n");
  }

  /* Overlapping pattern - find last non-overlapping start */
  p = gstrrstr("aaaa", 4, "aa", 2);
  if (p && (p - "aaaa") == 2)
    passed++;
  else {
    failed++;
    printf("  FAIL: overlapping\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrcasestr comprehensive tests
 * ============================================================================
 */
static void test_gstrcasestr_comprehensive(void) {
  printf("\n=== gstrcasestr comprehensive ===\n");
  int passed = 0, failed = 0;
  const char *p;

  /* Basic case insensitive */
  p = gstrcasestr("Hello World", 11, "WORLD", 5);
  if (p && (p - "Hello World") == 6)
    passed++;
  else {
    failed++;
    printf("  FAIL: WORLD in Hello World\n");
  }

  p = gstrcasestr("HELLO WORLD", 11, "hello", 5);
  if (p && (p - "HELLO WORLD") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: hello in HELLO\n");
  }

  /* Mixed case needle */
  p = gstrcasestr("The Quick Brown Fox", 19, "qUiCk", 5);
  if (p && (p - "The Quick Brown Fox") == 4)
    passed++;
  else {
    failed++;
    printf("  FAIL: mixed case needle\n");
  }

  /* Not found */
  p = gstrcasestr("hello", 5, "XYZ", 3);
  if (p == NULL)
    passed++;
  else {
    failed++;
    printf("  FAIL: not found\n");
  }

  /* Empty needle */
  const char *hay = "hello";
  p = gstrcasestr(hay, 5, "", 0);
  if (p == hay)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty needle\n");
  }

  /* Numbers unchanged */
  p = gstrcasestr("abc123def", 9, "123", 3);
  if (p && (p - "abc123def") == 3)
    passed++;
  else {
    failed++;
    printf("  FAIL: numbers\n");
  }

  /* With emoji (byte exact) */
  char s1[32], s2[32];
  snprintf(s1, sizeof(s1), "ABC%sDEF", EMOJI);
  snprintf(s2, sizeof(s2), "%s", EMOJI);
  p = gstrcasestr(s1, 3 + EMOJI_LEN + 3, s2, EMOJI_LEN);
  if (p && (p - s1) == 3)
    passed++;
  else {
    failed++;
    printf("  FAIL: emoji search\n");
  }

  /* Case around emoji */
  snprintf(s1, sizeof(s1), "ABC%sdef", EMOJI);
  p = gstrcasestr(s1, 3 + EMOJI_LEN + 3, "DEF", 3);
  if (p && (p - s1) == (int)(3 + EMOJI_LEN))
    passed++;
  else {
    failed++;
    printf("  FAIL: case after emoji\n");
  }

  /* Single char */
  p = gstrcasestr("AbCdEf", 6, "D", 1);
  if (p && (p - "AbCdEf") == 3)
    passed++;
  else {
    failed++;
    printf("  FAIL: single char\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrcount comprehensive tests
 * ============================================================================
 */
static void test_gstrcount_comprehensive(void) {
  printf("\n=== gstrcount comprehensive ===\n");
  int passed = 0, failed = 0;

  /* Multiple occurrences */
  if (gstrcount("abcabcabc", 9, "abc", 3) == 3)
    passed++;
  else {
    failed++;
    printf("  FAIL: 3 abc\n");
  }

  /* Single char */
  if (gstrcount("aaaaaa", 6, "a", 1) == 6)
    passed++;
  else {
    failed++;
    printf("  FAIL: 6 a's\n");
  }

  /* Non-overlapping */
  if (gstrcount("aaaa", 4, "aa", 2) == 2)
    passed++;
  else {
    failed++;
    printf("  FAIL: non-overlapping aa\n");
  }

  if (gstrcount("aaaaa", 5, "aa", 2) == 2)
    passed++;
  else {
    failed++;
    printf("  FAIL: non-overlapping aaaaa\n");
  }

  /* Not found */
  if (gstrcount("hello", 5, "xyz", 3) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: not found\n");
  }

  /* Empty needle */
  if (gstrcount("hello", 5, "", 0) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty needle\n");
  }

  /* Empty haystack */
  if (gstrcount("", 0, "a", 1) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty haystack\n");
  }

  /* Emoji counting */
  char s[64];
  snprintf(s, sizeof(s), "%s%s%s", EMOJI, EMOJI, EMOJI);
  if (gstrcount(s, EMOJI_LEN * 3, EMOJI, EMOJI_LEN) == 3)
    passed++;
  else {
    failed++;
    printf("  FAIL: 3 emoji\n");
  }

  /* Mixed */
  snprintf(s, sizeof(s), "X%sX%sX", EMOJI, EMOJI);
  if (gstrcount(s, 1 + EMOJI_LEN + 1 + EMOJI_LEN + 1, "X", 1) == 3)
    passed++;
  else {
    failed++;
    printf("  FAIL: 3 X with emoji\n");
  }

  /* Single occurrence */
  if (gstrcount("hello world", 11, "world", 5) == 1)
    passed++;
  else {
    failed++;
    printf("  FAIL: single world\n");
  }

  /* At boundaries */
  if (gstrcount("abc", 3, "abc", 3) == 1)
    passed++;
  else {
    failed++;
    printf("  FAIL: exact match\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrsep comprehensive tests
 * ============================================================================
 */
static void test_gstrsep_comprehensive(void) {
  printf("\n=== gstrsep comprehensive ===\n");
  int passed = 0, failed = 0;

  /* Basic tokenization */
  {
    const char *s = "a,b,c";
    size_t len = 5;
    size_t tok_len;
    int count = 0;

    while (s) {
      const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
      if (tok)
        count++;
    }
    if (count == 3)
      passed++;
    else {
      failed++;
      printf("  FAIL: basic 3 tokens\n");
    }
  }

  /* Empty tokens */
  {
    const char *s = "a,,b";
    size_t len = 4;
    size_t tok_len;
    const char *tok;

    tok = gstrsep(&s, &len, ",", 1, &tok_len);
    if (tok && tok_len == 1)
      passed++;
    else {
      failed++;
      printf("  FAIL: first token\n");
    }

    tok = gstrsep(&s, &len, ",", 1, &tok_len);
    if (tok && tok_len == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: empty token\n");
    }

    tok = gstrsep(&s, &len, ",", 1, &tok_len);
    if (tok && tok_len == 1)
      passed++;
    else {
      failed++;
      printf("  FAIL: last token\n");
    }
  }

  /* Multiple delimiters */
  {
    const char *s = "a;b,c:d";
    size_t len = 7;
    size_t tok_len;
    int count = 0;

    while (s) {
      const char *tok = gstrsep(&s, &len, ",;:", 3, &tok_len);
      if (tok)
        count++;
    }
    if (count == 4)
      passed++;
    else {
      failed++;
      printf("  FAIL: multi delim 4 tokens\n");
    }
  }

  /* No delimiter */
  {
    const char *s = "hello";
    size_t len = 5;
    size_t tok_len;

    const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
    if (tok && tok_len == 5 && s == NULL)
      passed++;
    else {
      failed++;
      printf("  FAIL: no delim\n");
    }
  }

  /* Empty delimiter - return whole string */
  {
    const char *s = "hello";
    size_t len = 5;
    size_t tok_len;

    const char *tok = gstrsep(&s, &len, "", 0, &tok_len);
    if (tok && tok_len == 5)
      passed++;
    else {
      failed++;
      printf("  FAIL: empty delim\n");
    }
  }

  /* Delimiter at start */
  {
    const char *s = ",a,b";
    size_t len = 4;
    size_t tok_len;

    const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
    if (tok && tok_len == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: delim at start\n");
    }
  }

  /* Delimiter at end - "a," returns just "a" (no trailing empty token) */
  {
    const char *s = "a,";
    size_t len = 2;
    size_t tok_len;
    int count = 0;

    while (s) {
      const char *tok = gstrsep(&s, &len, ",", 1, &tok_len);
      if (tok)
        count++;
    }
    if (count == 1)
      passed++;
    else {
      failed++;
      printf("  FAIL: delim at end (got %d)\n", count);
    }
  }

  /* With emoji delimiter */
  {
    char s_buf[64];
    snprintf(s_buf, sizeof(s_buf), "a%sb%sc", EMOJI, EMOJI);
    const char *s = s_buf;
    size_t len = 1 + EMOJI_LEN + 1 + EMOJI_LEN + 1;
    size_t tok_len;
    int count = 0;

    while (s) {
      const char *tok = gstrsep(&s, &len, EMOJI, EMOJI_LEN, &tok_len);
      if (tok)
        count++;
    }
    if (count == 3)
      passed++;
    else {
      failed++;
      printf("  FAIL: emoji delim\n");
    }
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrltrim / gstrrtrim / gstrtrim comprehensive tests
 * ============================================================================
 */
static void test_trim_comprehensive(void) {
  printf("\n=== trim functions comprehensive ===\n");
  int passed = 0, failed = 0;
  char buf[64];
  size_t n;

  /* gstrltrim */

  /* Basic spaces */
  n = gstrltrim(buf, sizeof(buf), "   hello", 8);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ltrim spaces\n");
  }

  /* Tabs and newlines */
  n = gstrltrim(buf, sizeof(buf), "\t\n\r hello", 9);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ltrim tabs/newlines\n");
  }

  /* CRLF (single grapheme) */
  n = gstrltrim(buf, sizeof(buf), "\r\nhello", 7);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ltrim CRLF\n");
  }

  /* All whitespace */
  n = gstrltrim(buf, sizeof(buf), "   \t\n", 5);
  if (n == 0 && strcmp(buf, "") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ltrim all ws\n");
  }

  /* No whitespace */
  n = gstrltrim(buf, sizeof(buf), "hello", 5);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ltrim no ws\n");
  }

  /* With emoji */
  {
    char src[32];
    snprintf(src, sizeof(src), "  %s", EMOJI);
    n = gstrltrim(buf, sizeof(buf), src, 2 + EMOJI_LEN);
    if (n == EMOJI_LEN && memcmp(buf, EMOJI, EMOJI_LEN) == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: ltrim emoji\n");
    }
  }

  /* gstrrtrim */

  /* Basic spaces */
  n = gstrrtrim(buf, sizeof(buf), "hello   ", 8);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: rtrim spaces\n");
  }

  /* Multiple whitespace types */
  n = gstrrtrim(buf, sizeof(buf), "hello\t\n ", 8);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: rtrim mixed\n");
  }

  /* CRLF */
  n = gstrrtrim(buf, sizeof(buf), "hello\r\n", 7);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: rtrim CRLF\n");
  }

  /* No whitespace */
  n = gstrrtrim(buf, sizeof(buf), "hello", 5);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: rtrim no ws\n");
  }

  /* With emoji */
  {
    char src[32];
    snprintf(src, sizeof(src), "%s  ", EMOJI);
    n = gstrrtrim(buf, sizeof(buf), src, EMOJI_LEN + 2);
    if (n == EMOJI_LEN && memcmp(buf, EMOJI, EMOJI_LEN) == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: rtrim emoji\n");
    }
  }

  /* gstrtrim */

  /* Both ends */
  n = gstrtrim(buf, sizeof(buf), "  hello  ", 9);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: trim both\n");
  }

  /* Complex whitespace */
  n = gstrtrim(buf, sizeof(buf), "\t\r\n hello \t\n", 12);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: trim complex\n");
  }

  /* Internal whitespace preserved */
  n = gstrtrim(buf, sizeof(buf), "  hello world  ", 15);
  if (n == 11 && strcmp(buf, "hello world") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: internal ws preserved\n");
  }

  /* Only whitespace: "   \t\n\r\n   " = 3 spaces + tab + LF + CRLF + 3 spaces =
   * 10 bytes */
  n = gstrtrim(buf, sizeof(buf), "   \t\n\r\n   ", 10);
  if (n == 0 && strcmp(buf, "") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: trim only ws (n=%zu)\n", n);
  }

  /* Emoji with whitespace */
  {
    char src[32];
    snprintf(src, sizeof(src), " %s ", EMOJI);
    n = gstrtrim(buf, sizeof(buf), src, 1 + EMOJI_LEN + 1);
    if (n == EMOJI_LEN && memcmp(buf, EMOJI, EMOJI_LEN) == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: trim emoji\n");
    }
  }

  /* Multiple CRLF */
  n = gstrtrim(buf, sizeof(buf), "\r\n\r\nhello\r\n\r\n", 13);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: multiple CRLF\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrrev comprehensive tests
 * ============================================================================
 */
static void test_gstrrev_comprehensive(void) {
  printf("\n=== gstrrev comprehensive ===\n");
  int passed = 0, failed = 0;
  char buf[128];
  size_t n;

  /* Basic ASCII */
  n = gstrrev(buf, sizeof(buf), "hello", 5);
  if (n == 5 && strcmp(buf, "olleh") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: basic ASCII\n");
  }

  /* Palindrome */
  n = gstrrev(buf, sizeof(buf), "radar", 5);
  if (n == 5 && strcmp(buf, "radar") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: palindrome\n");
  }

  /* Single char */
  n = gstrrev(buf, sizeof(buf), "a", 1);
  if (n == 1 && strcmp(buf, "a") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: single char\n");
  }

  /* Empty */
  n = gstrrev(buf, sizeof(buf), "", 0);
  if (n == 0 && strcmp(buf, "") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty\n");
  }

  /* Single emoji (stays same) */
  n = gstrrev(buf, sizeof(buf), EMOJI, EMOJI_LEN);
  if (n == EMOJI_LEN && memcmp(buf, EMOJI, EMOJI_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: single emoji\n");
  }

  /* ZWJ family (single grapheme, stays same) */
  n = gstrrev(buf, sizeof(buf), FAMILY, FAMILY_LEN);
  if (n == FAMILY_LEN && memcmp(buf, FAMILY, FAMILY_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ZWJ family\n");
  }

  /* Mixed ASCII and emoji: "A😀B" -> "B😀A" */
  {
    char src[32];
    snprintf(src, sizeof(src), "A%sB", EMOJI);
    size_t src_len = 1 + EMOJI_LEN + 1;
    n = gstrrev(buf, sizeof(buf), src, src_len);
    if (n == src_len && buf[0] == 'B' &&
        memcmp(buf + 1, EMOJI, EMOJI_LEN) == 0 && buf[1 + EMOJI_LEN] == 'A')
      passed++;
    else {
      failed++;
      printf("  FAIL: A emoji B\n");
    }
  }

  /* Multiple emoji: "😀🇨🇦👨‍👩‍👧" ->
   * "👨‍👩‍👧🇨🇦😀"
   */
  {
    char src[64];
    snprintf(src, sizeof(src), "%s%s%s", EMOJI, FLAG, FAMILY);
    size_t src_len = EMOJI_LEN + FLAG_LEN + FAMILY_LEN;
    n = gstrrev(buf, sizeof(buf), src, src_len);
    if (n == src_len && memcmp(buf, FAMILY, FAMILY_LEN) == 0 &&
        memcmp(buf + FAMILY_LEN, FLAG, FLAG_LEN) == 0 &&
        memcmp(buf + FAMILY_LEN + FLAG_LEN, EMOJI, EMOJI_LEN) == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: multiple emoji\n");
    }
  }

  /* Hangul: 한글 -> 글한 */
  n = gstrrev(buf, sizeof(buf), HANGUL, HANGUL_LEN);
  if (n == HANGUL_LEN) {
    /* 글 is bytes 3-5, 한 is bytes 0-2 */
    if (memcmp(buf, HANGUL + 3, 3) == 0 && memcmp(buf + 3, HANGUL, 3) == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: hangul order\n");
    }
  } else {
    failed++;
    printf("  FAIL: hangul len\n");
  }

  /* Buffer too small - truncate at grapheme boundary */
  n = gstrrev(buf, 4, "hello", 5);
  if (n == 3 && strcmp(buf, "oll") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: buffer overflow\n");
  }

  /* Buffer too small for multi-byte */
  n = gstrrev(buf, 5, WAVE, WAVE_LEN);
  if (n == 0 && strcmp(buf, "") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: buffer too small for emoji\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * gstrreplace comprehensive tests
 * ============================================================================
 */
static void test_gstrreplace_comprehensive(void) {
  printf("\n=== gstrreplace comprehensive ===\n");
  int passed = 0, failed = 0;
  char buf[128];
  size_t n;

  /* Basic replacement */
  n = gstrreplace(buf, sizeof(buf), "hello world", 11, "world", 5, "there", 5);
  if (n == 11 && strcmp(buf, "hello there") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: basic replace\n");
  }

  /* Multiple occurrences */
  n = gstrreplace(buf, sizeof(buf), "aXbXc", 5, "X", 1, "Y", 1);
  if (n == 5 && strcmp(buf, "aYbYc") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: multiple replace\n");
  }

  /* Grow (replacement longer) */
  n = gstrreplace(buf, sizeof(buf), "a-b-c", 5, "-", 1, "---", 3);
  if (n == 9 && strcmp(buf, "a---b---c") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: grow\n");
  }

  /* Shrink (replacement shorter) */
  n = gstrreplace(buf, sizeof(buf), "aXXXb", 5, "XXX", 3, "Y", 1);
  if (n == 3 && strcmp(buf, "aYb") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: shrink\n");
  }

  /* Delete (empty replacement) */
  n = gstrreplace(buf, sizeof(buf), "a-b-c", 5, "-", 1, "", 0);
  if (n == 3 && strcmp(buf, "abc") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: delete\n");
  }

  /* No match */
  n = gstrreplace(buf, sizeof(buf), "hello", 5, "xyz", 3, "abc", 3);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: no match\n");
  }

  /* Empty old (returns copy) */
  n = gstrreplace(buf, sizeof(buf), "hello", 5, "", 0, "X", 1);
  if (n == 5 && strcmp(buf, "hello") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: empty old\n");
  }

  /* Replace at start */
  n = gstrreplace(buf, sizeof(buf), "hello", 5, "hel", 3, "X", 1);
  if (n == 3 && strcmp(buf, "Xlo") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: at start\n");
  }

  /* Replace at end */
  n = gstrreplace(buf, sizeof(buf), "hello", 5, "llo", 3, "X", 1);
  if (n == 3 && strcmp(buf, "heX") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: at end\n");
  }

  /* Replace entire string */
  n = gstrreplace(buf, sizeof(buf), "hello", 5, "hello", 5, "world", 5);
  if (n == 5 && strcmp(buf, "world") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: entire string\n");
  }

  /* Emoji replacement */
  {
    char src[32];
    snprintf(src, sizeof(src), "Hi%s!", EMOJI);
    n = gstrreplace(buf, sizeof(buf), src, 2 + EMOJI_LEN + 1, EMOJI, EMOJI_LEN,
                    ":)", 2);
    if (n == 5 && strcmp(buf, "Hi:)!") == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: emoji replace\n");
    }
  }

  /* Replace with emoji: "Hi X!" (5) -> "Hi " (3) + emoji (4) + "!" (1) = 8 */
  n = gstrreplace(buf, sizeof(buf), "Hi X!", 5, "X", 1, EMOJI, EMOJI_LEN);
  if (n == 3 + EMOJI_LEN + 1 && memcmp(buf + 3, EMOJI, EMOJI_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: replace with emoji (n=%zu, expected %zu)\n", n,
           3 + EMOJI_LEN + 1);
  }

  /* ZWJ not partial match */
  n = gstrreplace(buf, sizeof(buf), FAMILY, FAMILY_LEN, "\xF0\x9F\x91\xA9", 4,
                  "X", 1);
  if (n == FAMILY_LEN && memcmp(buf, FAMILY, FAMILY_LEN) == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: ZWJ partial\n");
  }

  /* Buffer overflow - truncate */
  n = gstrreplace(buf, 8, "aXbXcXd", 7, "X", 1, "YYY", 3);
  /* "aYYYbYYYcYYYd" = 13 bytes, truncated to 7 */
  if (n <= 7 && buf[n] == '\0')
    passed++;
  else {
    failed++;
    printf("  FAIL: buffer overflow\n");
  }

  /* Consecutive matches */
  n = gstrreplace(buf, sizeof(buf), "XXX", 3, "X", 1, "ab", 2);
  if (n == 6 && strcmp(buf, "ababab") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: consecutive\n");
  }

  /* Non-overlapping (already tested in gstrcount) */
  n = gstrreplace(buf, sizeof(buf), "aaaa", 4, "aa", 2, "X", 1);
  if (n == 2 && strcmp(buf, "XX") == 0)
    passed++;
  else {
    failed++;
    printf("  FAIL: non-overlapping\n");
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * Stress tests with random/edge inputs
 * ============================================================================
 */
static void test_stress_edge_cases(void) {
  printf("\n=== Stress edge cases ===\n");
  int passed = 0, failed = 0;
  char buf[256];

  /* Very long string of emoji */
  {
    char long_str[512];
    size_t pos = 0;
    for (int i = 0; i < 50 && pos + EMOJI_LEN < sizeof(long_str); i++) {
      memcpy(long_str + pos, EMOJI, EMOJI_LEN);
      pos += EMOJI_LEN;
    }
    long_str[pos] = '\0';

    if (gstrlen(long_str, pos) == 50)
      passed++;
    else {
      failed++;
      printf("  FAIL: 50 emoji strlen\n");
    }

    if (gstrcount(long_str, pos, EMOJI, EMOJI_LEN) == 50)
      passed++;
    else {
      failed++;
      printf("  FAIL: 50 emoji count\n");
    }

    char *dup = gstrndup(long_str, pos, 25);
    if (dup && gstrlen(dup, strlen(dup)) == 25)
      passed++;
    else {
      failed++;
      printf("  FAIL: 50 emoji ndup 25\n");
    }
    free(dup);
  }

  /* String with all whitespace types */
  {
    const char *ws = " \t\n\r\v\f\r\n";
    size_t n = gstrtrim(buf, sizeof(buf), ws, 8);
    if (n == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: all whitespace types\n");
    }
  }

  /* Combining marks */
  {
    /* Multiple combining marks: e + acute + grave + circumflex */
    const char *multi = "e\xCC\x81\xCC\x80\xCC\x82";
    size_t len = 7;
    if (gstrlen(multi, len) == 1)
      passed++;
    else {
      failed++;
      printf("  FAIL: multiple combining\n");
    }

    char *dup = gstrdup(multi, len);
    if (dup && memcmp(dup, multi, len) == 0)
      passed++;
    else {
      failed++;
      printf("  FAIL: dup combining\n");
    }
    free(dup);
  }

  /* Alternating ASCII and emoji */
  {
    char alt[128];
    size_t pos = 0;
    for (int i = 0; i < 10; i++) {
      alt[pos++] = 'A' + i;
      memcpy(alt + pos, EMOJI, EMOJI_LEN);
      pos += EMOJI_LEN;
    }
    alt[pos] = '\0';

    if (gstrlen(alt, pos) == 20)
      passed++;
    else {
      failed++;
      printf("  FAIL: alternating len\n");
    }

    size_t n = gstrrev(buf, sizeof(buf), alt, pos);
    if (n == pos)
      passed++;
    else {
      failed++;
      printf("  FAIL: alternating rev\n");
    }

    /* Verify reverse structure */
    int valid = 1;
    for (int i = 0; i < 10; i++) {
      size_t idx = i * (1 + EMOJI_LEN);
      if (memcmp(buf + idx, EMOJI, EMOJI_LEN) != 0)
        valid = 0;
      if (buf[idx + EMOJI_LEN] != 'J' - i)
        valid = 0;
    }
    if (valid)
      passed++;
    else {
      failed++;
      printf("  FAIL: alternating rev structure\n");
    }
  }

  /* NULL handling across all functions */
  {
    int null_ok = 1;
    if (gstrncasecmp(NULL, 0, NULL, 0, 5) != 0)
      null_ok = 0;
    if (gstrdup(NULL, 5) != NULL)
      null_ok = 0;
    if (gstrndup(NULL, 5, 3) != NULL)
      null_ok = 0;
    if (gstrrstr(NULL, 5, "a", 1) != NULL)
      null_ok = 0;
    if (gstrcasestr(NULL, 5, "a", 1) != NULL)
      null_ok = 0;
    if (gstrcount(NULL, 5, "a", 1) != 0)
      null_ok = 0;

    const char *nullp = NULL;
    size_t nulllen = 0;
    if (gstrsep(&nullp, &nulllen, ",", 1, NULL) != NULL)
      null_ok = 0;

    if (gstrltrim(buf, sizeof(buf), NULL, 5) != 0)
      null_ok = 0;
    if (gstrrtrim(buf, sizeof(buf), NULL, 5) != 0)
      null_ok = 0;
    if (gstrtrim(buf, sizeof(buf), NULL, 5) != 0)
      null_ok = 0;
    if (gstrrev(buf, sizeof(buf), NULL, 5) != 0)
      null_ok = 0;
    if (gstrreplace(buf, sizeof(buf), NULL, 5, "a", 1, "b", 1) != 0)
      null_ok = 0;

    if (null_ok)
      passed++;
    else {
      failed++;
      printf("  FAIL: NULL handling\n");
    }
  }

  /* Zero-size buffer */
  {
    int zero_ok = 1;
    if (gstrltrim(buf, 0, "hello", 5) != 0)
      zero_ok = 0;
    if (gstrrtrim(buf, 0, "hello", 5) != 0)
      zero_ok = 0;
    if (gstrtrim(buf, 0, "hello", 5) != 0)
      zero_ok = 0;
    if (gstrrev(buf, 0, "hello", 5) != 0)
      zero_ok = 0;
    if (gstrreplace(buf, 0, "hello", 5, "l", 1, "X", 1) != 0)
      zero_ok = 0;

    if (zero_ok)
      passed++;
    else {
      failed++;
      printf("  FAIL: zero buffer\n");
    }
  }

  printf("  Passed: %d, Failed: %d\n", passed, failed);
  tests_passed += passed;
  tests_failed += failed;
}

/* ============================================================================
 * Main
 * ============================================================================
 */
int main(void) {
  printf("Comprehensive Stress Tests for New gstr.h Functions\n");
  printf("====================================================\n");

  test_gstrncasecmp_comprehensive();
  test_gstrdup_comprehensive();
  test_gstrrstr_comprehensive();
  test_gstrcasestr_comprehensive();
  test_gstrcount_comprehensive();
  test_gstrsep_comprehensive();
  test_trim_comprehensive();
  test_gstrrev_comprehensive();
  test_gstrreplace_comprehensive();
  test_stress_edge_cases();

  printf("\n====================================================\n");
  printf("Total: %d passed, %d failed\n", tests_passed, tests_failed);
  printf("====================================================\n");

  return tests_failed > 0 ? 1 : 0;
}
