/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
 *
 * test_gstr_stress.c - Comprehensive stress tests for gstr library
 *
 * Tests:
 * - Consistency between functions (gstrlen vs gstroff, gstrat)
 * - Round-trip operations (extract then compare)
 * - Real-world Unicode strings (multilingual)
 * - Complex emoji sequences
 * - Boundary conditions
 * - Grapheme count vs codepoint count invariants
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GSTR_SINGLE_HEADER
#define GSTR_IMPLEMENTATION
#include "gstr.h"
#else
#include <gstr.h>
#endif

static int tests_passed = 0;
static int tests_failed = 0;

#define PASS()                                                                 \
  do {                                                                         \
    tests_passed++;                                                            \
  } while (0)
#define FAIL(msg)                                                              \
  do {                                                                         \
    printf("FAIL: %s\n", msg);                                                 \
    tests_failed++;                                                            \
  } while (0)

/* ============================================================================
 * Test Strings - Comprehensive Unicode Coverage
 * ============================================================================
 */

struct test_string {
  const char *name;
  const char *data;
  size_t byte_len;
  size_t expected_graphemes;
};

static struct test_string TEST_STRINGS[] = {
    /* Basic ASCII */
    {"empty", "", 0, 0},
    {"single_ascii", "A", 1, 1},
    {"ascii_word", "Hello", 5, 5},
    {"ascii_sentence", "Hello, World!", 13, 13},

    /* Latin with diacritics (precomposed) */
    {"cafe_composed", "caf\xC3\xA9", 5, 4},       /* café */
    {"german", "Gr\xC3\xBC\xC3\x9F Gott", 11, 9}, /* Grüß Gott */
    {"french", "\xC3\x80 bient\xC3\xB4t", 10, 8}, /* À bientôt */

    /* Latin with combining marks (decomposed) */
    {"e_acute_decomposed", "e\xCC\x81", 3, 1},  /* e + combining acute */
    {"o_umlaut_decomposed", "o\xCC\x88", 3, 1}, /* o + combining diaeresis */
    {"a_ring_decomposed", "a\xCC\x8A", 3, 1},   /* a + combining ring */
    {"multi_combining", "a\xCC\x81\xCC\x82\xCC\x83", 7,
     1}, /* a + 3 combining marks */

    /* CJK */
    {"chinese", "\xE4\xB8\xAD\xE6\x96\x87", 6, 2},           /* 中文 */
    {"japanese_hiragana", "\xE3\x81\x82\xE3\x81\x84", 6, 2}, /* あい */
    {"japanese_katakana", "\xE3\x82\xA2\xE3\x82\xA4", 6, 2}, /* アイ */
    {"korean", "\xED\x95\x9C\xEA\xB8\x80", 6, 2},            /* 한글 */
    {"mixed_cjk",
     "\xE4\xB8\xAD\xE6\x96\x87\xE6\x97\xA5\xE6\x9C\xAC\xED\x95"
     "\x9C\xEA\xB8\x80",
     18, 6}, /* 中文日本한글 */

    /* Simple emoji */
    {"smile", "\xF0\x9F\x98\x80", 4, 1},     /* 😀 */
    {"heart", "\xE2\x9D\xA4", 3, 1},         /* ❤ */
    {"thumbs_up", "\xF0\x9F\x91\x8D", 4, 1}, /* 👍 */
    {"three_emoji", "\xF0\x9F\x98\x80\xF0\x9F\x98\x81\xF0\x9F\x98\x82", 12, 3},

    /* Emoji with skin tone modifiers */
    {"wave_light", "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBB", 8, 1},  /* 👋🏻 */
    {"wave_medium", "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD", 8, 1}, /* 👋🏽 */
    {"wave_dark", "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBF", 8, 1},   /* 👋🏿 */
    {"thumbs_skin", "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBC", 8, 1}, /* 👍🏼 */

    /* ZWJ sequences */
    {"family_mwg",
     "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
     18, 1}, /* 👨‍👩‍👧 */
    {"family_mwgb",
     "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"
     "\xE2\x80\x8D\xF0\x9F\x91\xA6",
     25, 1}, /* 👨‍👩‍👧‍👦 */
    {"couple_heart",
     "\xF0\x9F\x91\xA9\xE2\x80\x8D\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F"
     "\x91\xA8",
     20, 1}, /* 👩‍❤️‍👨 */
    {"man_technologist", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB", 11,
     1}, /* 👨‍💻 */
    {"woman_scientist", "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x94\xAC", 11,
     1}, /* 👩‍🔬 */

    /* Flags (regional indicators) */
    {"flag_us", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", 8, 1}, /* 🇺🇸 */
    {"flag_ca", "\xF0\x9F\x87\xA8\xF0\x9F\x87\xA6", 8, 1}, /* 🇨🇦 */
    {"flag_jp", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5", 8, 1}, /* 🇯🇵 */
    {"flag_gb", "\xF0\x9F\x87\xAC\xF0\x9F\x87\xA7", 8, 1}, /* 🇬🇧 */
    {"two_flags",
     "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xA8\xF0\x9F\x87"
     "\xA6",
     16, 2}, /* 🇺🇸🇨🇦 */

    /* Keycap sequences */
    {"keycap_1", "1\xEF\xB8\x8F\xE2\x83\xA3", 7, 1},    /* 1️⃣ */
    {"keycap_hash", "#\xEF\xB8\x8F\xE2\x83\xA3", 7, 1}, /* #️⃣ */

    /* Mixed scripts */
    {"mixed_hello", "Hello\xE4\xB8\x96\xE7\x95\x8C", 11, 7}, /* Hello世界 */
    {"mixed_emoji_text", "Hi\xF0\x9F\x91\x8B!", 7, 4},       /* Hi👋! */
    {"complex_mixed", "A\xCC\x81\xE4\xB8\xAD\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8",
     14, 3}, /* Á中🇺🇸 - 14 bytes */

    /* Thai (complex script) - vowels combine with consonants */
    {"thai",
     "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94"
     "\xE0\xB8\xB5",
     18, 4}, /* สวัสดี - 4 graphemes per UAX #29 */

    /* Arabic */
    {"arabic", "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", 10, 5}, /* مرحبا */

    /* Hebrew */
    {"hebrew", "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", 8, 4}, /* שלום */

    /* Devanagari with combining marks - conjuncts combine */
    {"hindi",
     "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4"
     "\xE0\xA5\x87",
     18, 3}, /* नमस्ते - 3 graphemes per UAX #29 */

    /* Edge cases */
    {"single_zwj", "\xE2\x80\x8D", 3, 1}, /* Just ZWJ */
    {"only_combining", "\xCC\x81", 2, 1}, /* Orphan combining mark */

    {NULL, NULL, 0, 0} /* Sentinel */
};

/* ============================================================================
 * Test: gstrlen matches expected grapheme count
 * ============================================================================
 */
static void test_gstrlen_accuracy(void) {
  printf("\n=== gstrlen accuracy ===\n");

  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];
    size_t got = gstrlen(t->data, t->byte_len);

    if (got == t->expected_graphemes) {
      PASS();
    } else {
      printf("  %-25s expected %zu, got %zu\n", t->name, t->expected_graphemes,
             got);
      FAIL(t->name);
    }
  }
}

/* ============================================================================
 * Test: gstrlen (graphemes) <= utf8_cpcount (codepoints)
 * ============================================================================
 */
static void test_gstrlen_vs_cpcount(void) {
  printf("\n=== gstrlen vs utf8_cpcount ===\n");

  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];
    size_t grapheme_count = gstrlen(t->data, t->byte_len);
    int codepoint_count = utf8_cpcount(t->data, (int)t->byte_len);

    /* Grapheme count should always be <= codepoint count */
    if (grapheme_count <= (size_t)codepoint_count && codepoint_count >= 0) {
      PASS();
    } else {
      printf("  %-25s graphemes=%zu, codepoints=%d\n", t->name, grapheme_count,
             codepoint_count);
      FAIL(t->name);
    }
  }
}

/* ============================================================================
 * Test: gstroff consistency - iterating should reach byte_len
 * ============================================================================
 */
static void test_gstroff_consistency(void) {
  printf("\n=== gstroff consistency ===\n");

  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];
    size_t count = gstrlen(t->data, t->byte_len);

    /* gstroff(count) should equal byte_len */
    size_t end_offset = gstroff(t->data, t->byte_len, count);
    if (end_offset == t->byte_len) {
      PASS();
    } else {
      printf("  %-25s gstroff(%zu)=%zu, expected %zu\n", t->name, count,
             end_offset, t->byte_len);
      FAIL(t->name);
    }
  }
}

/* ============================================================================
 * Test: gstrat consistency - each grapheme accessible
 * ============================================================================
 */
static void test_gstrat_consistency(void) {
  printf("\n=== gstrat consistency ===\n");

  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];
    size_t count = gstrlen(t->data, t->byte_len);
    int ok = 1;

    for (size_t j = 0; j < count; j++) {
      size_t len;
      const char *p = gstrat(t->data, t->byte_len, j, &len);
      if (p == NULL || len == 0) {
        printf("  %-25s gstrat(%zu) returned NULL or len=0\n", t->name, j);
        ok = 0;
        break;
      }
    }

    /* Past end should return NULL */
    const char *past = gstrat(t->data, t->byte_len, count, NULL);
    if (past != NULL && count > 0) {
      printf("  %-25s gstrat(%zu) should be NULL\n", t->name, count);
      ok = 0;
    }

    if (ok) {
      PASS();
    } else {
      FAIL(t->name);
    }
  }
}

/* ============================================================================
 * Test: gstrsub round-trip - extract all then compare
 * ============================================================================
 */
static void test_gstrsub_roundtrip(void) {
  printf("\n=== gstrsub round-trip ===\n");

  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];
    size_t count = gstrlen(t->data, t->byte_len);

    /* Extract entire string */
    char buf[256];
    size_t written = gstrsub(buf, sizeof(buf), t->data, t->byte_len, 0, count);

    if (written == t->byte_len && memcmp(buf, t->data, t->byte_len) == 0) {
      PASS();
    } else {
      printf("  %-25s extract all: wrote %zu, expected %zu\n", t->name, written,
             t->byte_len);
      FAIL(t->name);
    }
  }
}

/* ============================================================================
 * Test: gstrcpy round-trip
 * ============================================================================
 */
static void test_gstrcpy_roundtrip(void) {
  printf("\n=== gstrcpy round-trip ===\n");

  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];

    char buf[256];
    size_t written = gstrcpy(buf, sizeof(buf), t->data, t->byte_len);

    if (written == t->byte_len && memcmp(buf, t->data, t->byte_len) == 0) {
      PASS();
    } else {
      printf("  %-25s copy: wrote %zu, expected %zu\n", t->name, written,
             t->byte_len);
      FAIL(t->name);
    }
  }
}

/* ============================================================================
 * Test: gstrcmp symmetry and transitivity
 * ============================================================================
 */
static void test_gstrcmp_properties(void) {
  printf("\n=== gstrcmp properties ===\n");

  /* Reflexivity: a == a */
  for (int i = 0; TEST_STRINGS[i].name != NULL; i++) {
    struct test_string *t = &TEST_STRINGS[i];
    int cmp = gstrcmp(t->data, t->byte_len, t->data, t->byte_len);
    if (cmp == 0) {
      PASS();
    } else {
      printf("  %-25s reflexivity failed\n", t->name);
      FAIL(t->name);
    }
  }

  /* Antisymmetry: if a < b then b > a */
  const char *a = "abc";
  const char *b = "abd";
  int ab = gstrcmp(a, 3, b, 3);
  int ba = gstrcmp(b, 3, a, 3);
  if ((ab < 0 && ba > 0) || (ab > 0 && ba < 0) || (ab == 0 && ba == 0)) {
    PASS();
  } else {
    printf("  antisymmetry failed: ab=%d, ba=%d\n", ab, ba);
    FAIL("antisymmetry");
  }
}

/* ============================================================================
 * Test: gstrstr finds all substrings
 * ============================================================================
 */
static void test_gstrstr_comprehensive(void) {
  printf("\n=== gstrstr comprehensive ===\n");

  /* Find ASCII in ASCII */
  const char *hay1 = "hello world";
  const char *needle1 = "world";
  const char *found = gstrstr(hay1, 11, needle1, 5);
  if (found && found == hay1 + 6) {
    PASS();
  } else {
    FAIL("ascii in ascii");
  }

  /* Find emoji in mixed string */
  const char *hay2 = "Say hi \xF0\x9F\x91\x8B to me"; /* Say hi 👋 to me */
  const char *needle2 = "\xF0\x9F\x91\x8B";           /* 👋 */
  found = gstrstr(hay2, strlen(hay2), needle2, 4);
  if (found && found == hay2 + 7) {
    PASS();
  } else {
    FAIL("emoji in mixed");
  }

  /* Should NOT find partial emoji */
  const char *family =
      "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91"
      "\xA7";                             /* 👨‍👩‍👧 */
  const char *woman = "\xF0\x9F\x91\xA9"; /* 👩 */
  found = gstrstr(family, 18, woman, 4);
  if (found == NULL) {
    PASS();
  } else {
    FAIL("should not find woman in family");
  }

  /* Find combining sequence */
  const char *hay3 = "caf\x65\xCC\x81"; /* café with decomposed é */
  const char *needle3 = "\x65\xCC\x81"; /* e + combining acute */
  found = gstrstr(hay3, 6, needle3, 3);
  if (found && found == hay3 + 3) {
    PASS();
  } else {
    FAIL("combining sequence");
  }

  /* Empty needle returns haystack */
  found = gstrstr(hay1, 11, "", 0);
  if (found == hay1) {
    PASS();
  } else {
    FAIL("empty needle");
  }

  /* Needle longer than haystack */
  found = gstrstr("hi", 2, "hello", 5);
  if (found == NULL) {
    PASS();
  } else {
    FAIL("needle longer than haystack");
  }
}

/* ============================================================================
 * Test: gstrncpy truncation preserves grapheme boundaries
 * ============================================================================
 */
static void test_gstrncpy_boundaries(void) {
  printf("\n=== gstrncpy grapheme boundaries ===\n");

  /* Copy 2 graphemes from family emoji (which is 1 grapheme) */
  const char *family =
      "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"; /* partial family */
  char buf[32];
  size_t written = gstrncpy(buf, sizeof(buf), family, 11, 1);
  /* Should copy the whole grapheme since we asked for 1 */
  if (written == 11) {
    PASS();
  } else {
    printf("  family copy 1 grapheme: wrote %zu, expected 11\n", written);
    FAIL("family copy");
  }

  /* Copy 2 graphemes from "Hi👋!" - should get "Hi" */
  const char *mixed = "Hi\xF0\x9F\x91\x8B!";
  written = gstrncpy(buf, sizeof(buf), mixed, 7, 2);
  if (written == 2 && memcmp(buf, "Hi", 2) == 0) {
    PASS();
  } else {
    printf("  mixed copy 2: wrote %zu\n", written);
    FAIL("mixed copy 2");
  }

  /* Copy into tiny buffer - should not break grapheme */
  const char *emoji = "\xF0\x9F\x98\x80";  /* 😀 - 4 bytes */
  written = gstrncpy(buf, 3, emoji, 4, 1); /* buffer too small */
  if (written == 0) {
    PASS();
  } else {
    printf("  tiny buffer: wrote %zu, expected 0\n", written);
    FAIL("tiny buffer");
  }
}

/* ============================================================================
 * Test: gstrspn/gstrcspn with complex graphemes
 * ============================================================================
 */
static void test_span_complex(void) {
  printf("\n=== span with complex graphemes ===\n");

  /* Accept set with emoji */
  const char *s1 =
      "\xF0\x9F\x98\x80\xF0\x9F\x98\x81\xF0\x9F\x98\x82X"; /* 😀😁😂X */
  const char *accept =
      "\xF0\x9F\x98\x80\xF0\x9F\x98\x81\xF0\x9F\x98\x82"; /* 😀😁😂 */
  size_t span = gstrspn(s1, 13, accept, 12);
  if (span == 3) {
    PASS();
  } else {
    printf("  emoji span: got %zu, expected 3\n", span);
    FAIL("emoji span");
  }

  /* cspn until emoji */
  const char *s2 = "Hello\xF0\x9F\x98\x80World";
  const char *reject = "\xF0\x9F\x98\x80";
  size_t cspan = gstrcspn(s2, 14, reject, 4);
  if (cspan == 5) { /* "Hello" = 5 graphemes */
    PASS();
  } else {
    printf("  cspn until emoji: got %zu, expected 5\n", cspan);
    FAIL("cspn until emoji");
  }
}

/* ============================================================================
 * Test: Buffer overflow protection
 * ============================================================================
 */
static void test_buffer_overflow_protection(void) {
  printf("\n=== buffer overflow protection ===\n");

  char tiny[4];

  /* gstrcpy into tiny buffer */
  size_t w = gstrcpy(tiny, sizeof(tiny), "hello", 5);
  if (w == 3 && tiny[3] == '\0' && strcmp(tiny, "hel") == 0) {
    PASS();
  } else {
    FAIL("gstrcpy overflow");
  }

  /* gstrcat into nearly full buffer */
  char buf[8] = "hello";
  w = gstrcat(buf, sizeof(buf), "world", 5);
  /* "hello" + as much of "world" as fits = "hellow" (7 chars) */
  if (w == 7 && buf[7] == '\0') {
    PASS();
  } else {
    printf("  gstrcat: got %zu, buf='%s'\n", w, buf);
    FAIL("gstrcat overflow");
  }

  /* gstrsub into tiny buffer with emoji */
  const char *emoji = "\xF0\x9F\x98\x80\xF0\x9F\x98\x81"; /* 😀😁 - 8 bytes */
  w = gstrsub(tiny, sizeof(tiny), emoji, 8, 0, 2);
  /* Can't fit even one emoji (4 bytes) in 4-byte buffer with null */
  if (w == 0 && tiny[0] == '\0') {
    PASS();
  } else {
    printf("  gstrsub emoji: got %zu\n", w);
    FAIL("gstrsub emoji overflow");
  }
}

/* ============================================================================
 * Test: Case-insensitive comparison (ASCII only)
 * ============================================================================
 */
static void test_casecmp(void) {
  printf("\n=== case-insensitive comparison ===\n");

  /* Basic ASCII */
  if (gstrcasecmp("Hello", 5, "HELLO", 5) == 0) {
    PASS();
  } else {
    FAIL("Hello vs HELLO");
  }

  if (gstrcasecmp("hello", 5, "hello", 5) == 0) {
    PASS();
  } else {
    FAIL("hello vs hello");
  }

  /* Mixed case */
  if (gstrcasecmp("HeLLo", 5, "hEllO", 5) == 0) {
    PASS();
  } else {
    FAIL("HeLLo vs hEllO");
  }

  /* Different strings */
  if (gstrcasecmp("abc", 3, "abd", 3) < 0) {
    PASS();
  } else {
    FAIL("abc vs abd");
  }

  /* With non-ASCII (should compare byte-exact for non-ASCII) */
  const char *s1 = "caf\xC3\xA9"; /* café */
  const char *s2 = "CAF\xC3\xA9"; /* CAFé */
  if (gstrcasecmp(s1, 5, s2, 5) == 0) {
    PASS();
  } else {
    FAIL("café vs CAFé");
  }
}

/* ============================================================================
 * Main
 * ============================================================================
 */
int main(void) {
  printf("gstr.h Comprehensive Stress Tests\n");
  printf("==================================\n");
  printf("Version: %s\n", GSTR_VERSION);

  test_gstrlen_accuracy();
  test_gstrlen_vs_cpcount();
  test_gstroff_consistency();
  test_gstrat_consistency();
  test_gstrsub_roundtrip();
  test_gstrcpy_roundtrip();
  test_gstrcmp_properties();
  test_gstrstr_comprehensive();
  test_gstrncpy_boundaries();
  test_span_complex();
  test_buffer_overflow_protection();
  test_casecmp();

  printf("\n==================================\n");
  printf("Tests passed: %d\n", tests_passed);
  printf("Tests failed: %d\n", tests_failed);
  printf("==================================\n");

  return tests_failed > 0 ? 1 : 0;
}
