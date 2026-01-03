# utflite

A lightweight, zero-dependency UTF-8 library for C. See [UTF-8 Everywhere](https://utf8everywhere.org/) for why UTF-8 is the recommended encoding for all text.

## News

### v1.5.2 (January 2026)
- **Fix**: Skin tone modifiers (U+1F3FB-U+1F3FF) now correctly zero-width
- **Fix**: Regional Indicators (U+1F1E6-U+1F1FF) now correctly double-width for flag emoji
- **Fix**: Split double-width range to exclude skin tone modifiers
- Enables correct cursor positioning for emoji like 👋🏻 and flags like 🇨🇦

### v1.5.1 (December 2025)
- **Breaking fix**: `utflite_decode()` now returns 0 (not 1) for empty/null input, preventing infinite loops
- Added `UTFLITE_UNICODE_VERSION` macro (value: 170 for Unicode 17.0)

### v1.5.0 (December 2025)
- Added detailed documentation for `utflite__is_extended_pictographic()` explaining GB11 behavior
- Added complexity documentation for `utflite_prev_grapheme()` (O(k) where k ≤ 128)
- Documented that double-width table is used as Extended_Pictographic approximation for GB11

### v1.4.0 (December 2025)
- Code compliance with CODING_STANDARDS.md (removed typedefs, added named constants)
- Increased grapheme backtrack limit to 128 codepoints (handles longest emoji sequences)
- Internal improvements (no API changes)

### v1.3.0 (December 2025)
- **UAX #29 grapheme cluster segmentation** - proper cursor movement over emoji sequences, combining marks, Hangul, and Indic scripts
- 100% pass rate on official Unicode GraphemeBreakTest.txt (766 tests)
- Implements all GB rules including GB9c (Indic conjunct sequences)
- Library size: ~18KB compiled (still tiny!)

### v1.2.0 (December 2025)
- Renamed `utflite_is_combining()` to `utflite_is_zero_width()` for accuracy
- Fixed zero-width detection for BiDi control characters
- Added bounded scan to `utflite_prev_char()` for safety

### v1.1.0 (December 2025)
- Initial public release
- UTF-8 encoding/decoding with full validation
- Unicode 17.0 character width tables

## Features

- UTF-8 encoding and decoding with full validation
- Unicode 17.0 character width tables (wcwidth alternative)
- **UAX #29 grapheme cluster segmentation** (emoji, flags, combining marks, Hangul)
- String navigation (next/prev character and grapheme)
- Utility functions: validate, count, width, truncate
- Single-header option for easy integration
- C17 compliant, no external dependencies

## Use Cases

**Terminal/CLI applications**
- Text editors that need cursor positioning over Unicode text
- Progress bars and status lines with proper column alignment
- Tree views and tables that must align with CJK/emoji content

**Text processing tools**
- Validating UTF-8 input from files or network
- Counting characters (not bytes) for length limits
- Truncating strings to fit display widths without breaking characters

**Embedded systems**
- Lightweight alternative to ICU or libunistring
- Single-header makes it easy to integrate into firmware
- No heap allocation, no dependencies

**Game development**
- Chat systems handling international text
- UI text measurement for layout
- Input validation for player names

**When NOT to use utflite**
- Full Unicode normalization (NFC/NFD) - use ICU
- Complex text shaping/rendering (Arabic, Indic scripts) - use HarfBuzz
- Locale-aware sorting/comparison - use ICU
- Bidirectional text layout - use FriBidi

## Project Structure

```
utflite/
├── include/utflite/      # Public headers
│   └── utflite.h
├── single_include/       # Single-header distribution
│   └── utflite.h
├── src/                  # Implementation
│   └── utflite.c
├── test/
│   └── test_utflite.c
└── build/                # Build artifacts (gitignored)
```

## Installation

### Single Header (Easiest)

Copy `single_include/utflite.h` to your project. In **one** .c file:

```c
#define UTFLITE_IMPLEMENTATION
#include "utflite.h"
```

In all other files, just include normally:

```c
#include "utflite.h"
```

### Static Library

```bash
make
make install  # installs to /usr/local
```

Then in your code:

```c
#include <utflite/utflite.h>
```

And compile with `-lutflite`.

## Tutorial

### Decoding UTF-8

UTF-8 encodes Unicode codepoints as 1-4 bytes. Use `utflite_decode()` to convert bytes to a codepoint:

```c
#define UTFLITE_IMPLEMENTATION
#include "utflite.h"
#include <stdio.h>

int main(void) {
    const char *text = "A";           // ASCII: 1 byte
    const char *text2 = "\xC3\xA9";   // e-acute (U+00E9): 2 bytes
    const char *text3 = "\xE4\xB8\xAD"; // Chinese character (U+4E2D): 3 bytes

    uint32_t codepoint;
    int bytes;

    bytes = utflite_decode(text, 1, &codepoint);
    printf("'A' = U+%04X (%d byte)\n", codepoint, bytes);
    // Output: 'A' = U+0041 (1 byte)

    bytes = utflite_decode(text2, 2, &codepoint);
    printf("e-acute = U+%04X (%d bytes)\n", codepoint, bytes);
    // Output: e-acute = U+00E9 (2 bytes)

    bytes = utflite_decode(text3, 3, &codepoint);
    printf("Chinese = U+%04X (%d bytes)\n", codepoint, bytes);
    // Output: Chinese = U+4E2D (3 bytes)

    return 0;
}
```

### Encoding to UTF-8

Convert a codepoint back to UTF-8 bytes:

```c
#include "utflite.h"
#include <stdio.h>

int main(void) {
    char buffer[UTFLITE_MAX_BYTES];  // Always 4 bytes max
    int len;

    // Encode emoji (U+1F600 = grinning face)
    len = utflite_encode(0x1F600, buffer);

    printf("Emoji is %d bytes: ", len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", (unsigned char)buffer[i]);
    }
    printf("\n");
    // Output: Emoji is 4 bytes: F0 9F 98 80

    return 0;
}
```

### Iterating Over Characters

Use `utflite_next_char()` to walk through a UTF-8 string character by character:

```c
#include "utflite.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    // Mixed string: ASCII + Chinese + emoji
    const char *text = "Hi\xE4\xB8\xAD\xF0\x9F\x98\x80";
    int len = strlen(text);  // 10 bytes, but only 4 characters

    int offset = 0;
    int char_num = 0;

    while (offset < len) {
        uint32_t codepoint;
        int bytes = utflite_decode(text + offset, len - offset, &codepoint);

        printf("Char %d: U+%04X at byte %d (%d bytes)\n",
               char_num, codepoint, offset, bytes);

        offset = utflite_next_char(text, len, offset);
        char_num++;
    }
    // Output:
    // Char 0: U+0048 at byte 0 (1 bytes)   'H'
    // Char 1: U+0069 at byte 1 (1 bytes)   'i'
    // Char 2: U+4E2D at byte 2 (3 bytes)   Chinese
    // Char 3: U+1F600 at byte 5 (4 bytes)  Emoji

    return 0;
}
```

### Grapheme Clusters (User-Perceived Characters)

A "character" to users isn't always a single codepoint. Emoji sequences, combining marks, and flags are multiple codepoints that display as one unit. Use `utflite_next_grapheme()` to iterate by what users actually see:

```c
#include "utflite.h"
#include <stdio.h>
#include <string.h>

int count_graphemes(const char *text, int len) {
    int count = 0, offset = 0;
    while (offset < len) {
        offset = utflite_next_grapheme(text, len, offset);
        count++;
    }
    return count;
}

int main(void) {
    // Family emoji: 7 codepoints, but 1 grapheme
    const char *family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    int len = strlen(family);

    printf("Family emoji:\n");
    printf("  Bytes: %d\n", len);                              // 18
    printf("  Codepoints: %d\n", utflite_codepoint_count(family, len));  // 7
    printf("  Graphemes: %d\n", count_graphemes(family, len)); // 1

    // e + combining acute accent = 1 grapheme
    const char *accent = "e\xCC\x81";  // é as e + U+0301
    len = strlen(accent);
    printf("\ne + acute accent:\n");
    printf("  Codepoints: %d\n", utflite_codepoint_count(accent, len));  // 2
    printf("  Graphemes: %d\n", count_graphemes(accent, len)); // 1

    // Flag: 2 regional indicators = 1 grapheme
    const char *flag = "\xF0\x9F\x87\xA8\xF0\x9F\x87\xA6";  // 🇨🇦
    len = strlen(flag);
    printf("\nCanadian flag:\n");
    printf("  Codepoints: %d\n", utflite_codepoint_count(flag, len));  // 2
    printf("  Graphemes: %d\n", count_graphemes(flag, len)); // 1

    return 0;
}
```

This is essential for:
- Cursor movement in text editors
- Character counting for length limits
- Proper text selection
- Backspace/delete operations

### Character Display Width

Terminals display characters with different widths. ASCII is 1 column, CJK and emoji are 2 columns, combining marks are 0 columns:

```c
#include "utflite.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    // Check individual codepoints
    printf("'A' width: %d\n", utflite_codepoint_width(0x0041));      // 1
    printf("Chinese width: %d\n", utflite_codepoint_width(0x4E2D));  // 2
    printf("Emoji width: %d\n", utflite_codepoint_width(0x1F600));   // 2
    printf("Combining accent width: %d\n", utflite_codepoint_width(0x0301)); // 0

    // Calculate total display width of a string
    const char *text = "Hello\xE4\xB8\xAD";  // "Hello" + Chinese char
    int width = utflite_string_width(text, strlen(text));
    printf("'Hello中' display width: %d columns\n", width);  // 7 (5 + 2)

    return 0;
}
```

### Validating UTF-8

Check if a string contains valid UTF-8:

```c
#include "utflite.h"
#include <stdio.h>

int main(void) {
    const char *valid = "Hello \xE4\xB8\xAD";
    const char *invalid = "Bad \x80\x81 data";  // Invalid continuation bytes

    int error_pos;

    if (utflite_validate(valid, 9, NULL)) {
        printf("String 1: valid UTF-8\n");
    }

    if (!utflite_validate(invalid, 11, &error_pos)) {
        printf("String 2: invalid at byte %d\n", error_pos);
    }
    // Output:
    // String 1: valid UTF-8
    // String 2: invalid at byte 4

    return 0;
}
```

### Truncating for Display

When you need to fit text in a fixed-width column, use `utflite_truncate()` to find the correct byte offset:

```c
#include "utflite.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    // String with mixed widths
    const char *text = "AB\xE4\xB8\xAD\xE4\xB8\xAD" "CD";
    // A(1) B(1) 中(2) 中(2) C(1) D(1) = 8 columns total

    int len = strlen(text);

    // Truncate to fit in 5 columns
    int cut = utflite_truncate(text, len, 5);

    printf("Original (%d cols): %s\n", utflite_string_width(text, len), text);
    printf("Truncated to 5 cols: %.*s\n", cut, text);
    // Output:
    // Original (8 cols): AB中中CD
    // Truncated to 5 cols: AB中

    return 0;
}
```

### Counting Characters

Count codepoints (not bytes) in a string:

```c
#include "utflite.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *text = "Hello\xE4\xB8\xAD\xF0\x9F\x98\x80";
    int len = strlen(text);

    printf("Bytes: %d\n", len);
    printf("Characters: %d\n", utflite_codepoint_count(text, len));
    printf("Display width: %d columns\n", utflite_string_width(text, len));
    // Output:
    // Bytes: 12
    // Characters: 7
    // Display width: 9

    return 0;
}
```

### Handling Invalid Input

`utflite_decode()` never crashes on invalid input. It returns the replacement character (U+FFFD) and advances by 1 byte:

```c
#include "utflite.h"
#include <stdio.h>

int main(void) {
    // Invalid UTF-8: bare continuation byte
    const char *bad = "\x80\x81\x82";

    uint32_t cp;
    int offset = 0;

    while (offset < 3) {
        int bytes = utflite_decode(bad + offset, 3 - offset, &cp);

        if (cp == UTFLITE_REPLACEMENT_CHAR) {
            printf("Invalid byte at %d\n", offset);
        }
        offset += bytes;
    }
    // Output:
    // Invalid byte at 0
    // Invalid byte at 1
    // Invalid byte at 2

    return 0;
}
```

## API Reference

### Constants

```c
#define UTFLITE_REPLACEMENT_CHAR 0xFFFD  // Returned on decode errors
#define UTFLITE_MAX_BYTES 4              // Max bytes per codepoint
#define UTFLITE_UNICODE_VERSION 170      // Unicode 17.0
```

### Core Functions

```c
// Decode UTF-8 to codepoint. Returns bytes consumed (1-4), or 0 for empty/null input.
int utflite_decode(const char *bytes, int length, uint32_t *codepoint);

// Encode codepoint to UTF-8. Returns bytes written (1-4), 0 on error.
int utflite_encode(uint32_t codepoint, char *buffer);
```

### Character Width

```c
// Returns display width: -1 (control), 0 (combining), 1 (normal), 2 (wide)
int utflite_codepoint_width(uint32_t codepoint);

// Width of character at byte offset
int utflite_char_width(const char *text, int length, int offset);
```

### Navigation

```c
// Get byte offset of next/previous character (codepoint)
int utflite_next_char(const char *text, int length, int offset);
int utflite_prev_char(const char *text, int offset);

// Grapheme cluster navigation (UAX #29 compliant)
// Handles emoji sequences, combining marks, flags, Hangul, Indic scripts
int utflite_next_grapheme(const char *text, int length, int offset);
int utflite_prev_grapheme(const char *text, int offset);
```

### Utilities

```c
// Validate UTF-8 string. Returns 1 if valid, 0 if invalid.
int utflite_validate(const char *text, int length, int *error_offset);

// Count codepoints in string
int utflite_codepoint_count(const char *text, int length);

// Calculate display width of string
int utflite_string_width(const char *text, int length);

// Check character properties
int utflite_is_zero_width(uint32_t codepoint);  // Zero-width (combining, ZWJ, etc)?
int utflite_is_wide(uint32_t codepoint);        // Double-width (CJK, emoji)?

// Find truncation point for max display columns
int utflite_truncate(const char *text, int length, int max_cols);
```

## Building

```bash
make              # Build static library (output in build/)
make test         # Run tests with static library
make test-single  # Run tests with single-header version
make install      # Install to /usr/local
make clean        # Clean build artifacts
```

## License

MIT License. See LICENSE file.
