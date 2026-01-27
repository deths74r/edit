<!--
SPDX-License-Identifier: MIT
Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
-->

# gstr.h

A single-header UTF-8 and grapheme string library for C. No dependencies, just drop it in and go.

## What Problem Does This Solve?

Standard C string functions like `strlen()` and `strchr()` work on **bytes**, not **characters**. This causes problems with Unicode text:

```c
#include <string.h>

strlen("Hello");      // Returns 5 ✓ (5 letters, 5 bytes)
strlen("你好");        // Returns 6 ✗ (2 characters, but 6 bytes!)
strlen("👨‍👩‍👧");         // Returns 18 ✗ (1 emoji, but 18 bytes!)
```

**gstr.h** fixes this by operating on **grapheme clusters** - what users actually perceive as "characters":

```c
#include "gstr.h"

gstrlen("Hello", 5);      // Returns 5 ✓
gstrlen("你好", 6);        // Returns 2 ✓
gstrlen("👨‍👩‍👧", 18);        // Returns 1 ✓
```

## Feature Comparison

| Feature | gstr.h | libgrapheme | utf8proc | libunistring | ICU |
|---------|--------|-------------|----------|--------------|-----|
| **Integration** | `#include` | Build + link | Build + link | Build + link | Build + link |
| **Files** | 1 | ~15 | ~10 | ~100+ | 1000+ |
| **Binary size** | 0* | ~30KB | ~300KB | ~2MB | ~38MB |
| **Unicode version** | 17.0 | 15.0 | 16.0 | 16.0 | 16.0 |
| **Language** | C99 | C99 | C99 | C99 | C/C++ |
| **High-level string API** | ✅ | ❌ | ❌ | Partial | ✅ |
| **Grapheme segmentation** | ✅ | ✅ | ✅ | ✅ | ✅ |
| **Display width** | ✅ | ❌ | ✅ | ✅ | ✅ |
| **Terminal-width padding** | ✅ | ❌ | ❌ | ❌ | ❌ |
| **Dependencies** | None | None | None | libc | libc++ |
| **Freestanding** | ✅ | ✅ | ❌ | ❌ | ❌ |

*\* Header-only with `static inline` functions - no library binary. Compiler emits only the functions you call; unused code is eliminated.*

## Quick Start

### Installation

1. Copy `include/gstr.h` to your project
2. Include it:

```c
#include "gstr.h"
```

That's it. All functions are `static inline` - no separate compilation needed.

### Your First Program

```c
#include "gstr.h"
#include <stdio.h>

int main(void) {
    const char *text = "Hello 世界! 👋";
    size_t len = 14;  // byte length
    
    printf("Bytes: %zu\n", len);                    // 14
    printf("Graphemes: %zu\n", gstrlen(text, len)); // 10
    printf("Display width: %zu\n", gstrwidth(text, len)); // 12 columns
    
    return 0;
}
```

Compile: `gcc -o hello hello.c`


## Understanding the Basics

Before diving into the functions, let's understand three important concepts:

### 1. Bytes vs Codepoints vs Graphemes

| Term | What It Is | Example |
|------|------------|---------|
| **Byte** | A single 8-bit value (0-255) | The letter 'A' is 1 byte |
| **Codepoint** | A Unicode number (U+0000 to U+10FFFF) | 'A' is U+0041, '中' is U+4E2D |
| **Grapheme** | What users see as "one character" | 👨‍👩‍👧 is 1 grapheme (but 7 codepoints!) |

UTF-8 encodes codepoints using 1-4 bytes:
- ASCII (U+0000-U+007F): 1 byte
- Latin, Greek, etc. (U+0080-U+07FF): 2 bytes  
- Most languages (U+0800-U+FFFF): 3 bytes
- Emoji, rare scripts (U+10000+): 4 bytes

### 2. Why Graphemes Matter

A grapheme cluster is what users perceive as a single character. Some examples:

| What You See | Codepoints | Bytes |
|--------------|------------|-------|
| é | 1 (U+00E9) or 2 (e + ´) | 2 or 3 |
| 🇨🇦 | 2 (🇨 + 🇦) | 8 |
| 👨‍👩‍👧 | 7 (👨 + ZWJ + 👩 + ZWJ + 👧) | 18 |
| 한 | 1 (precomposed) or 3 (ㅎ + ㅏ + ㄴ) | 3 or 9 |

**gstr.h** handles all of these correctly.

### 3. The Two Layers

**gstr.h** has two layers of functions:

| Prefix | Level | Purpose |
|--------|-------|---------|
| `utf8_*` | Low-level | Encoding, decoding, validation, byte navigation |
| `gstr*` | High-level | String operations on graphemes (like string.h) |

Most of the time, you'll use the `gstr*` functions.


## Tutorial: Working with Strings

### Counting Characters

**Problem:** How many characters are in my string?

```c
#include "gstr.h"
#include <string.h>

const char *text = "café ☕";
size_t bytes = strlen(text);  // 9 bytes

// Count graphemes (what users see)
size_t chars = gstrlen(text, bytes);
printf("Characters: %zu\n", chars);  // 6: c, a, f, é, space, ☕
```

**Limit the count:**

```c
// Count at most 3 graphemes
size_t n = gstrnlen(text, bytes, 3);
printf("First 3: %zu\n", n);  // 3
```

### Getting Display Width

**Problem:** How many terminal columns does my string take?

```c
const char *text = "Hello世界";
size_t bytes = strlen(text);

size_t width = gstrwidth(text, bytes);
printf("Width: %zu columns\n", width);  // 9 (5 + 2 + 2)
// ASCII = 1 column each, Chinese = 2 columns each
```

**Emoji handling:**

```c
// ZWJ sequences (family emoji) = 2 columns
printf("%zu\n", gstrwidth("👨‍👩‍👧", 18));  // 2

// Flag emoji = 2 columns  
printf("%zu\n", gstrwidth("🇨🇦", 8));     // 2

// Emoji with skin tone = 2 columns
printf("%zu\n", gstrwidth("👋🏽", 8));    // 2
```

### Accessing Individual Characters

**Problem:** Get the 3rd character of a string.

```c
const char *text = "Hello 👋 World";
size_t bytes = strlen(text);

// Get pointer to 3rd grapheme (0-indexed, so index 2)
size_t char_len;
const char *third = gstrat(text, bytes, 2, &char_len);

printf("Third character: %.*s\n", (int)char_len, third);  // "l"
```

**Get byte offset instead:**

```c
size_t offset = gstroff(text, bytes, 6);  // Byte offset of grapheme 6
printf("Offset of char 6: %zu\n", offset);  // Points to 👋
```

### Comparing Strings

**Problem:** Are these two strings equal?

```c
const char *a = "hello";
const char *b = "hello";
const char *c = "world";

if (gstrcmp(a, 5, b, 5) == 0) {
    printf("a and b are equal\n");  // This prints
}

if (gstrcmp(a, 5, c, 5) < 0) {
    printf("a comes before c\n");   // This prints (h < w)
}
```

**Compare only first N graphemes:**

```c
const char *a = "hello world";
const char *b = "hello there";

if (gstrncmp(a, 11, b, 11, 5) == 0) {
    printf("First 5 chars match\n");  // This prints
}
```

**Case-insensitive comparison:**

```c
const char *a = "Hello";
const char *b = "HELLO";

if (gstrcasecmp(a, 5, b, 5) == 0) {
    printf("Equal ignoring case\n");  // This prints
}
```

### Searching in Strings

**Problem:** Find a substring in my string.

```c
const char *text = "The quick brown fox";
size_t len = strlen(text);

// Find "quick"
const char *found = gstrstr(text, len, "quick", 5);
if (found) {
    printf("Found at position: %ld\n", found - text);  // 4
}

// Case-insensitive search
const char *found2 = gstrcasestr(text, len, "QUICK", 5);
if (found2) {
    printf("Found (ignoring case)\n");
}
```

**Find a single character:**

```c
const char *text = "hello";
size_t len = 5;

// Find first 'l'
const char *first_l = gstrchr(text, len, "l", 1);
printf("First 'l' at: %ld\n", first_l - text);  // 2

// Find last 'l'
const char *last_l = gstrrchr(text, len, "l", 1);
printf("Last 'l' at: %ld\n", last_l - text);   // 3
```

**Count occurrences:**

```c
const char *text = "banana";
size_t count = gstrcount(text, 6, "a", 1);
printf("'a' appears %zu times\n", count);  // 3
```

### Checking Prefix and Suffix

**Problem:** Does my filename end with ".txt"?

```c
const char *filename = "document.txt";
size_t len = strlen(filename);

if (gstrendswith(filename, len, ".txt", 4)) {
    printf("It's a text file!\n");
}

if (gstrstartswith(filename, len, "doc", 3)) {
    printf("Starts with 'doc'\n");
}
```

### Copying Strings

**Problem:** Copy a string to a buffer safely.

```c
char buffer[32];
const char *source = "Hello 世界!";
size_t src_len = strlen(source);

// Copy entire string
size_t copied = gstrcpy(buffer, sizeof(buffer), source, src_len);
printf("Copied %zu bytes: %s\n", copied, buffer);

// Copy only first 5 graphemes
size_t copied2 = gstrncpy(buffer, sizeof(buffer), source, src_len, 5);
printf("First 5 chars: %s\n", buffer);  // "Hello"
```

### Extracting Substrings

**Problem:** Get characters 2-5 from a string.

```c
char buffer[32];
const char *text = "Hello World";
size_t len = strlen(text);

// Extract 4 graphemes starting at position 2
size_t extracted = gstrsub(buffer, sizeof(buffer), text, len, 2, 4);
printf("Extracted: %s\n", buffer);  // "llo "
```

### Concatenating Strings

**Problem:** Append one string to another.

```c
char buffer[32] = "Hello";
const char *suffix = " World!";

// Append entire suffix
gstrcat(buffer, sizeof(buffer), suffix, strlen(suffix));
printf("%s\n", buffer);  // "Hello World!"

// Or append only N graphemes
char buf2[32] = "Hi";
gstrncat(buf2, sizeof(buf2), " there friend", 13, 6);
printf("%s\n", buf2);  // "Hi there"
```

### Trimming Whitespace

**Problem:** Remove leading/trailing spaces.

```c
char buffer[32];
const char *text = "   hello world   ";
size_t len = strlen(text);

// Trim leading whitespace
gstrltrim(buffer, sizeof(buffer), text, len);
printf("[%s]\n", buffer);  // "[hello world   ]"

// Trim trailing whitespace  
gstrrtrim(buffer, sizeof(buffer), text, len);
printf("[%s]\n", buffer);  // "[   hello world]"

// Trim both
gstrtrim(buffer, sizeof(buffer), text, len);
printf("[%s]\n", buffer);  // "[hello world]"
```

### Changing Case

**Problem:** Convert to lowercase or uppercase.

```c
char buffer[32];

gstrlower(buffer, sizeof(buffer), "Hello WORLD", 11);
printf("%s\n", buffer);  // "hello world"

gstrupper(buffer, sizeof(buffer), "Hello World", 11);
printf("%s\n", buffer);  // "HELLO WORLD"
```

Note: These only convert ASCII letters (a-z, A-Z).

### Reversing Strings

**Problem:** Reverse the characters in a string.

```c
char buffer[32];

gstrrev(buffer, sizeof(buffer), "Hello", 5);
printf("%s\n", buffer);  // "olleH"

// Works with emoji too!
gstrrev(buffer, sizeof(buffer), "Hi👋", 6);
printf("%s\n", buffer);  // "👋iH"
```

### Replacing Text

**Problem:** Replace all occurrences of a substring.

```c
char buffer[64];
const char *text = "one two one three one";
size_t len = strlen(text);

gstrreplace(buffer, sizeof(buffer), text, len, "one", 3, "1", 1);
printf("%s\n", buffer);  // "1 two 1 three 1"
```

### Truncating with Ellipsis

**Problem:** Shorten text but show it was truncated.

```c
char buffer[32];

gstrellipsis(buffer, sizeof(buffer), "Hello World", 11, 8, "...", 3);
printf("%s\n", buffer);  // "Hello..."

// Works with custom ellipsis
gstrellipsis(buffer, sizeof(buffer), "Hello World", 11, 8, "…", 3);
printf("%s\n", buffer);  // "Hello…"
```

### Padding Strings

**Problem:** Pad a string to a fixed width.

```c
char buffer[32];

// Right-pad (align left)
gstrrpad(buffer, sizeof(buffer), "Hi", 2, 10, " ", 1);
printf("[%s]\n", buffer);  // "[Hi        ]"

// Left-pad (align right)
gstrlpad(buffer, sizeof(buffer), "Hi", 2, 10, " ", 1);
printf("[%s]\n", buffer);  // "[        Hi]"

// Center
gstrpad(buffer, sizeof(buffer), "Hi", 2, 10, " ", 1);
printf("[%s]\n", buffer);  // "[    Hi    ]"
```

### Padding by Terminal Width

**Problem:** Pad strings with wide characters (CJK, emoji) to exact column width.

The `gstrlpad`/`gstrrpad`/`gstrpad` functions pad to a **grapheme count**. But when displaying CJK or emoji, you often need to pad to a **column width** instead:

```c
char buffer[64];

// Problem: CJK characters are 2 columns wide
const char *cjk = "日本";  // 2 graphemes, but 4 columns!
gstrrpad(buffer, sizeof(buffer), cjk, 6, 10, " ", 1);
// Result: "日本        " - 8 spaces added (wrong!)

// Solution: Use column-width-aware padding
gstrwrpad(buffer, sizeof(buffer), cjk, 6, 10, " ", 1);
// Result: "日本      " - 6 spaces added (correct: 4+6=10 columns)
```

**Truncate to fit terminal width:**

```c
const char *text = "Hello世界!";  // 5 + 4 + 1 = 10 columns
size_t bytes = strlen(text);

// Truncate to 8 columns
size_t written = gstrwtrunc(buffer, sizeof(buffer), text, bytes, 8);
printf("%s\n", buffer);  // "Hello世" (5+2=7, can't fit next char)
```

**Center with emoji:**

```c
const char *emoji = "👋";  // 1 grapheme, 2 columns
gstrwpad(buffer, sizeof(buffer), emoji, 4, 10, "-", 1);
printf("[%s]\n", buffer);  // "[----👋----]" (4+2+4=10 columns)
```

### Filling with Characters

**Problem:** Create a string of repeated characters.

```c
char buffer[32];

gstrfill(buffer, sizeof(buffer), "-", 1, 20);
printf("%s\n", buffer);  // "--------------------"

// Works with multi-byte characters
gstrfill(buffer, sizeof(buffer), "→", 3, 5);
printf("%s\n", buffer);  // "→→→→→"
```

### Duplicating Strings

**Problem:** Create a copy of a string (malloc).

```c
const char *text = "Hello";

// Duplicate entire string
char *copy = gstrdup(text, 5);
printf("%s\n", copy);
free(copy);

// Duplicate first N graphemes
char *partial = gstrndup(text, 5, 3);
printf("%s\n", partial);  // "Hel"
free(partial);
```

### Tokenizing (Splitting)

**Problem:** Split a string by delimiters.

```c
const char *text = "one,two,three";
const char *ptr = text;
size_t len = strlen(text);

const char *token;
size_t tok_len;

while ((token = gstrsep(&ptr, &len, ",", 1, &tok_len)) != NULL) {
    printf("Token: %.*s\n", (int)tok_len, token);
}
// Output:
// Token: one
// Token: two
// Token: three
```

### Span Functions

**Problem:** Count characters matching a set.

```c
const char *text = "123abc456";
size_t len = strlen(text);

// Count leading digits
size_t digits = gstrspn(text, len, "0123456789", 10);
printf("Leading digits: %zu\n", digits);  // 3

// Count leading non-letters
size_t non_alpha = gstrcspn(text, len, "abcdefghijklmnopqrstuvwxyz", 26);
printf("Before first letter: %zu\n", non_alpha);  // 3

// Find first vowel
const char *vowel = gstrpbrk(text, len, "aeiou", 5);
printf("First vowel: %c\n", *vowel);  // 'a'
```


## Tutorial: Low-Level UTF-8 Operations

### Decoding UTF-8

**Problem:** Get the Unicode codepoint from UTF-8 bytes.

```c
uint32_t codepoint;
int bytes_consumed;

// Decode ASCII
bytes_consumed = utf8_decode("A", 1, &codepoint);
printf("U+%04X (%d byte)\n", codepoint, bytes_consumed);  // U+0041 (1 byte)

// Decode Chinese character
bytes_consumed = utf8_decode("中", 3, &codepoint);
printf("U+%04X (%d bytes)\n", codepoint, bytes_consumed);  // U+4E2D (3 bytes)

// Decode emoji
bytes_consumed = utf8_decode("😀", 4, &codepoint);
printf("U+%04X (%d bytes)\n", codepoint, bytes_consumed);  // U+1F600 (4 bytes)
```

### Encoding to UTF-8

**Problem:** Convert a codepoint to UTF-8 bytes.

```c
char buffer[UTF8_MAX_BYTES];  // Always 4 bytes max
int bytes_written;

// Encode ASCII
bytes_written = utf8_encode('A', buffer);
printf("Bytes: %d\n", bytes_written);  // 1

// Encode emoji
bytes_written = utf8_encode(0x1F600, buffer);
printf("Bytes: %d\n", bytes_written);  // 4

// Now buffer contains the UTF-8 bytes
```

### Validating UTF-8

**Problem:** Check if input is valid UTF-8.

```c
int error_offset;

// Valid UTF-8
if (utf8_valid("Hello 世界", 12, NULL)) {
    printf("Valid!\n");
}

// Invalid UTF-8
const char *bad = "Hello \x80\x81 world";  // Invalid bytes
if (!utf8_valid(bad, 14, &error_offset)) {
    printf("Invalid at byte %d\n", error_offset);  // 6
}
```

### Counting Codepoints

**Problem:** How many Unicode codepoints (not graphemes)?

```c
// Family emoji: 1 grapheme, but 7 codepoints
const char *family = "👨‍👩‍👧";

int codepoints = utf8_cpcount(family, 18);
printf("Codepoints: %d\n", codepoints);  // 7 (👨 + ZWJ + 👩 + ZWJ + 👧 = 5 emoji + 2 ZWJ)
```

### Navigating by Codepoint

**Problem:** Move through a string codepoint by codepoint.

```c
const char *text = "A中😀";
int len = 8;  // 1 + 3 + 4 bytes
int offset = 0;

while (offset < len) {
    uint32_t cp;
    utf8_decode(text + offset, len - offset, &cp);
    printf("U+%04X at byte %d\n", cp, offset);
    
    offset = utf8_next(text, len, offset);
}
// U+0041 at byte 0
// U+4E2D at byte 1  
// U+1F600 at byte 4
```

### Navigating by Grapheme

**Problem:** Move through a string grapheme by grapheme.

```c
const char *text = "e\xCC\x81 👨‍👩‍👧";  // "é" (decomposed) + space + family emoji
int len = 22;
int offset = 0;
int grapheme = 0;

while (offset < len) {
    int next = utf8_next_grapheme(text, len, offset);
    printf("Grapheme %d: bytes %d-%d\n", grapheme, offset, next);
    offset = next;
    grapheme++;
}
// Grapheme 0: bytes 0-3   (e + combining accent)
// Grapheme 1: bytes 3-4   (space)
// Grapheme 2: bytes 4-22  (family emoji - 18 bytes!)
```

### Checking Character Width

**Problem:** How wide is this character in a terminal?

```c
// ASCII: 1 column
printf("'A' width: %d\n", utf8_cpwidth('A'));  // 1

// Chinese: 2 columns (double-width)
printf("'中' width: %d\n", utf8_cpwidth(0x4E2D));  // 2

// Emoji: 2 columns
printf("'😀' width: %d\n", utf8_cpwidth(0x1F600));  // 2

// Combining mark: 0 columns (overlays previous)
printf("'́' width: %d\n", utf8_cpwidth(0x0301));  // 0

// Control character: -1 (not printable)
printf("'\\n' width: %d\n", utf8_cpwidth('\n'));  // -1
```

### Truncating by Display Width

**Problem:** Cut a string to fit N terminal columns.

```c
const char *text = "Hello世界";  // 5 + 2 + 2 = 9 columns
int len = 11;  // 5 + 3 + 3 bytes

// Truncate to 7 columns
int cut = utf8_truncate(text, len, 7);
printf("Truncated: %.*s\n", cut, text);  // "Hello世" (5+2=7 cols)

// Truncate to 6 columns (can't fit 世, which needs 2)
cut = utf8_truncate(text, len, 6);
printf("Truncated: %.*s\n", cut, text);  // "Hello" (only 5 cols fit)
```


## Complete Function Reference

### Constants

```c
#define UTF8_REPLACEMENT_CHAR  0xFFFD   // Returned on decode errors
#define UTF8_MAX_BYTES         4        // Maximum bytes per codepoint
```


### UTF-8 Layer (14 functions)

#### Encoding/Decoding

| Function | Description |
|----------|-------------|
| `utf8_decode(bytes, len, *cp)` | Decode UTF-8 → codepoint. Returns bytes consumed (1-4), 0 on error. |
| `utf8_encode(codepoint, buffer)` | Encode codepoint → UTF-8. Returns bytes written (1-4), 0 on error. |

#### Validation

| Function | Description |
|----------|-------------|
| `utf8_valid(text, len, *err_offset)` | Returns 1 if valid UTF-8, 0 if invalid. Sets error offset if provided. |

#### Codepoint Navigation

| Function | Description |
|----------|-------------|
| `utf8_next(text, len, offset)` | Byte offset of next codepoint. |
| `utf8_prev(text, offset)` | Byte offset of previous codepoint. |
| `utf8_cpcount(text, len)` | Count codepoints in string. |

#### Grapheme Navigation

| Function | Description |
|----------|-------------|
| `utf8_next_grapheme(text, len, offset)` | Byte offset of next grapheme cluster boundary. |
| `utf8_prev_grapheme(text, offset)` | Byte offset of previous grapheme cluster boundary. |

#### Display Width

| Function | Description |
|----------|-------------|
| `utf8_cpwidth(codepoint)` | Width of codepoint: -1 (control), 0 (combining), 1 (normal), 2 (wide). |
| `utf8_charwidth(text, len, offset)` | Width of UTF-8 character at byte offset. |
| `utf8_is_zerowidth(codepoint)` | Returns 1 if codepoint is zero-width (combining marks, ZWJ, etc). |
| `utf8_is_wide(codepoint)` | Returns 1 if codepoint is double-width (CJK, emoji). |
| `utf8_truncate(text, len, max_cols)` | Byte offset to truncate at max display width. |


### Grapheme String Layer (46 functions)

#### Length Functions

```c
size_t gstrlen(const char *s, size_t byte_len);
```
Count grapheme clusters in string.

```c
size_t gstrnlen(const char *s, size_t byte_len, size_t max_graphemes);
```
Count graphemes up to max_graphemes.

```c
size_t gstrwidth(const char *s, size_t byte_len);
```
Calculate display width in terminal columns.


#### Indexing Functions

```c
size_t gstroff(const char *s, size_t byte_len, size_t grapheme_n);
```
Get byte offset of Nth grapheme (0-indexed).

```c
const char *gstrat(const char *s, size_t byte_len, size_t grapheme_n, size_t *out_len);
```
Get pointer to Nth grapheme. Stores grapheme's byte length in `out_len`.

> **Performance Note:** `gstroff` and `gstrat` are O(n) — they scan from the start
> of the string each time. For sequential iteration, use `utf8_next_grapheme()`
> directly to avoid O(n²) complexity:
>
> ```c
> // BAD: O(n²) - rescans from start each iteration
> for (size_t g = 0; g < count; g++) {
>     size_t off = gstroff(s, len, g);  // O(g) each call
> }
>
> // GOOD: O(n) - incremental advancement
> int off = 0;
> while (off < len) {
>     int next = utf8_next_grapheme(s, len, off);  // O(1)
>     // process s[off..next)
>     off = next;
> }
> ```


#### Comparison Functions

```c
int gstrcmp(const char *a, size_t a_len, const char *b, size_t b_len);
```
Compare two strings grapheme-by-grapheme. Returns <0, 0, or >0.

```c
int gstrncmp(const char *a, size_t a_len, const char *b, size_t b_len, size_t n);
```
Compare first N graphemes.

```c
int gstrcasecmp(const char *a, size_t a_len, const char *b, size_t b_len);
```
Case-insensitive compare (ASCII letters only).

```c
int gstrncasecmp(const char *a, size_t a_len, const char *b, size_t b_len, size_t n);
```
Case-insensitive compare of first N graphemes.


#### Prefix/Suffix Functions

```c
int gstrstartswith(const char *s, size_t s_len, const char *prefix, size_t prefix_len);
```
Returns 1 if string starts with prefix, 0 otherwise.

```c
int gstrendswith(const char *s, size_t s_len, const char *suffix, size_t suffix_len);
```
Returns 1 if string ends with suffix, 0 otherwise.


#### Search Functions

```c
const char *gstrchr(const char *s, size_t len, const char *grapheme, size_t g_len);
```
Find first occurrence of grapheme. Returns pointer or NULL.

```c
const char *gstrrchr(const char *s, size_t len, const char *grapheme, size_t g_len);
```
Find last occurrence of grapheme. Returns pointer or NULL.

```c
const char *gstrstr(const char *haystack, size_t h_len, const char *needle, size_t n_len);
```
Find first occurrence of substring. Returns pointer or NULL.

```c
const char *gstrrstr(const char *haystack, size_t h_len, const char *needle, size_t n_len);
```
Find last occurrence of substring. Returns pointer or NULL.

```c
const char *gstrcasestr(const char *haystack, size_t h_len, const char *needle, size_t n_len);
```
Case-insensitive substring search (ASCII only). Returns pointer or NULL.

```c
size_t gstrcount(const char *s, size_t len, const char *needle, size_t n_len);
```
Count non-overlapping occurrences of needle.


#### Span Functions

```c
size_t gstrspn(const char *s, size_t len, const char *accept, size_t a_len);
```
Count leading graphemes that are in accept set.

```c
size_t gstrcspn(const char *s, size_t len, const char *reject, size_t r_len);
```
Count leading graphemes that are NOT in reject set.

```c
const char *gstrpbrk(const char *s, size_t len, const char *accept, size_t a_len);
```
Find first grapheme that is in accept set. Returns pointer or NULL.


#### Extraction Functions

```c
size_t gstrsub(char *dst, size_t dst_size, const char *src, size_t src_len,
               size_t start_grapheme, size_t count);
```
Extract `count` graphemes starting at `start_grapheme` into dst. Returns bytes written (excluding null terminator).


#### Copy Functions

```c
size_t gstrcpy(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Copy entire string to dst. Returns bytes written.

```c
size_t gstrncpy(char *dst, size_t dst_size, const char *src, size_t src_len, size_t n);
```
Copy up to N graphemes to dst. Returns bytes written.


#### Concatenation Functions

```c
size_t gstrcat(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Append src to dst. Returns total bytes in dst.

```c
size_t gstrncat(char *dst, size_t dst_size, const char *src, size_t src_len, size_t n);
```
Append up to N graphemes from src to dst. Returns total bytes in dst.


#### Allocation Functions

```c
char *gstrdup(const char *s, size_t len);
```
Duplicate entire string. Returns malloc'd copy (caller must free).

```c
char *gstrndup(const char *s, size_t len, size_t n);
```
Duplicate first N graphemes. Returns malloc'd copy (caller must free).


#### Tokenization Functions

```c
const char *gstrsep(const char **stringp, size_t *lenp, const char *delim,
                    size_t d_len, size_t *tok_len);
```
Extract next token from string, updating `*stringp` and `*lenp`. Stores token length in `tok_len`. Returns pointer to token or NULL when done.


#### Trimming Functions

```c
size_t gstrltrim(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Copy src to dst, removing leading ASCII whitespace. Returns bytes written.

```c
size_t gstrrtrim(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Copy src to dst, removing trailing ASCII whitespace. Returns bytes written.

```c
size_t gstrtrim(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Copy src to dst, removing leading and trailing ASCII whitespace. Returns bytes written.


#### Transformation Functions

```c
size_t gstrrev(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Reverse string by graphemes. Returns bytes written.

```c
size_t gstrreplace(char *dst, size_t dst_size, const char *src, size_t src_len,
                   const char *old, size_t old_len, const char *new_str, size_t new_len);
```
Replace all occurrences of `old` with `new_str`. Returns bytes written.


#### Case Conversion Functions

```c
size_t gstrlower(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Convert ASCII letters to lowercase. Returns bytes written.

```c
size_t gstrupper(char *dst, size_t dst_size, const char *src, size_t src_len);
```
Convert ASCII letters to uppercase. Returns bytes written.


#### Display/Truncation Functions

```c
size_t gstrellipsis(char *dst, size_t dst_size, const char *src, size_t src_len,
                    size_t max_graphemes, const char *ellipsis, size_t ellipsis_len);
```
Truncate to max_graphemes and append ellipsis if truncated. Returns bytes written.


#### Fill/Padding Functions

```c
size_t gstrfill(char *dst, size_t dst_size, const char *grapheme, size_t g_len, size_t count);
```
Fill buffer with grapheme repeated `count` times. Returns bytes written.

```c
size_t gstrlpad(char *dst, size_t dst_size, const char *src, size_t src_len,
                size_t width, const char *pad, size_t pad_len);
```
Left-pad string to reach `width` graphemes. Returns bytes written.

```c
size_t gstrrpad(char *dst, size_t dst_size, const char *src, size_t src_len,
                size_t width, const char *pad, size_t pad_len);
```
Right-pad string to reach `width` graphemes. Returns bytes written.

```c
size_t gstrpad(char *dst, size_t dst_size, const char *src, size_t src_len,
               size_t width, const char *pad, size_t pad_len);
```
Center string by padding both sides. Returns bytes written.


#### Column-Width-Aware Functions

These functions work like their grapheme-count counterparts, but use **terminal column width** instead. Essential for proper alignment with CJK characters and emoji.

```c
size_t gstrwtrunc(char *dst, size_t dst_size, const char *src, size_t src_len,
                  size_t max_cols);
```
Truncate string to fit within `max_cols` terminal columns, preserving grapheme boundaries. Returns bytes written.

```c
size_t gstrwlpad(char *dst, size_t dst_size, const char *src, size_t src_len,
                 size_t target_cols, const char *pad, size_t pad_len);
```
Left-pad string to reach `target_cols` terminal columns. Returns bytes written.

```c
size_t gstrwrpad(char *dst, size_t dst_size, const char *src, size_t src_len,
                 size_t target_cols, const char *pad, size_t pad_len);
```
Right-pad string to reach `target_cols` terminal columns. Returns bytes written.

```c
size_t gstrwpad(char *dst, size_t dst_size, const char *src, size_t src_len,
                size_t target_cols, const char *pad, size_t pad_len);
```
Center string by padding both sides to reach `target_cols` terminal columns. Returns bytes written.


## Building from Source

### Using Make

```bash
make              # Build library and single-header
make test         # Run tests with static library
make test-single  # Run tests with single-header
make test-all     # Run all tests
make install      # Install to /usr/local
make clean        # Clean build artifacts
```

### Using the Static Library

After `make install`:

```c
#include <gstr.h>
```

Compile with: `gcc -o myprogram myprogram.c -lgstr`


## FAQ

### Why do all functions take byte length?

Because UTF-8 strings aren't null-terminated in the sense that you might have embedded nulls, and because calculating length with `strlen()` on every call would be wasteful. Pass `strlen(s)` if your string is null-terminated.

### Why "grapheme" instead of "character"?

"Character" is ambiguous - it could mean byte, codepoint, or grapheme. "Grapheme cluster" is the Unicode term for what users perceive as a single character. We shorten it to "grapheme" for the API.

### Does this handle all Unicode correctly?

gstr.h implements UAX #29 grapheme cluster segmentation, which handles:
- Emoji sequences (👨‍👩‍👧, 👋🏻, 🏳️‍🌈)
- Combining marks (é as e + ́)
- Regional indicators (🇨🇦)
- Hangul syllables
- Indic conjuncts

It does **not** handle:
- Unicode normalization (NFC/NFD) - use ICU
- Text shaping (Arabic cursive) - use HarfBuzz
- Bidirectional text - use FriBidi

### How big is the library?

- Single header: ~110KB source
- Compiled library: ~25KB
- No runtime allocations (except gstrdup/gstrndup)
- No dependencies

### Is it thread-safe?

Yes. All functions are pure (no global state) and only read from their inputs.


## License

MIT License. See LICENSE file.
