# Bug: Incomplete Grapheme Cluster Navigation

## Summary

Cursor movement (arrow keys) treats multi-codepoint grapheme clusters as separate characters. For example, flag emoji like 🇨🇦 require 2 keypresses instead of 1.

## Affected Inputs

| Input | Codepoints | Expected Keypresses | Actual Keypresses |
|-------|------------|---------------------|-------------------|
| 🇨🇦 (flag) | 2 | 1 | 2 |
| 👨‍👩‍👧 (family) | 5 | 1 | 5 |
| 👋🏽 (skin tone) | 2 | 1 | 2 |
| ❤️ (with VS16) | 2 | 1 | 2 |
| é (decomposed) | 2 | 1 | 1 (works) |

## Root Cause

**File:** `src/buffer.c:274-327`

The `cursor_next_grapheme()` and `cursor_prev_grapheme()` functions use `codepoint_is_combining_mark()` to detect grapheme boundaries:

```c
// buffer.c:321-324
column++;
while (column < line->cell_count && codepoint_is_combining_mark(line->cells[column].codepoint)) {
    column++;
}
```

The `codepoint_is_combining_mark()` function (lines 274-288) only handles diacritical marks:
- U+0300-U+036F (Combining Diacritical Marks)
- U+1AB0-U+1AFF (Combining Diacritical Marks Extended)
- U+1DC0-U+1DFF (Combining Diacritical Marks Supplement)
- U+20D0-U+20FF (Combining Diacritical Marks for Symbols)
- U+FE20-U+FE2F (Combining Half Marks)

This misses all UAX #29 grapheme cluster rules:
- **GB12/GB13**: Regional Indicators (flags) - pairs like 🇨 + 🇦
- **GB11**: ZWJ sequences (emoji joined by U+200D)
- **GB9**: Extend characters (variation selectors U+FE00-U+FE0F)
- **GB6-GB8**: Hangul jamo sequences
- **GB9c**: Indic conjunct sequences

## Suggested Fix

Replace the custom combining mark logic with proper UAX #29 grapheme segmentation using utflite's `utflite_next_grapheme()` and `utflite_prev_grapheme()`.

### Challenge

The editor stores text as a `cells[]` array of codepoints, but utflite's grapheme functions operate on UTF-8 byte strings. Two approaches:

### Option A: Reconstruct UTF-8 on demand (simpler)

```c
// In cursor_next_grapheme():
// 1. Encode cells from current position into a temporary UTF-8 buffer
// 2. Call utflite_next_grapheme() to find the boundary
// 3. Count how many codepoints were consumed
// 4. Return column + consumed_codepoints

uint32_t cursor_next_grapheme(struct line *line, struct buffer *buffer, uint32_t column)
{
    line_warm(line, buffer);

    if (column >= line->cell_count) {
        return line->cell_count;
    }

    // Encode enough codepoints to cover any grapheme cluster (max ~32 codepoints)
    char utf8_buf[128];
    int byte_len = 0;
    int codepoints_encoded = 0;
    int max_codepoints = 32;  // Enough for longest emoji sequence

    for (uint32_t i = column; i < line->cell_count && codepoints_encoded < max_codepoints; i++) {
        int bytes = utflite_encode(line->cells[i].codepoint, utf8_buf + byte_len);
        byte_len += bytes;
        codepoints_encoded++;
    }

    // Find next grapheme boundary in UTF-8
    int next_byte = utflite_next_grapheme(utf8_buf, byte_len, 0);

    // Count codepoints consumed
    int offset = 0;
    int codepoints_in_grapheme = 0;
    while (offset < next_byte) {
        uint32_t cp;
        offset += utflite_decode(utf8_buf + offset, byte_len - offset, &cp);
        codepoints_in_grapheme++;
    }

    return column + codepoints_in_grapheme;
}
```

### Option B: Request codepoint-based API from utflite

Add new functions to utflite that operate on codepoint arrays instead of UTF-8 bytes:

```c
// Proposed new utflite API
int utflite_next_grapheme_cp(const uint32_t *codepoints, int count, int offset);
int utflite_prev_grapheme_cp(const uint32_t *codepoints, int count, int offset);
```

This would be more efficient but requires changes to utflite.

## Test Cases

Use the test file at `../utflite/test/utf8_test_samples.txt` to verify the fix:

```
Hi🇨🇦中👨‍👩‍👧!
```

Expected: 6 arrow keypresses to traverse (H, i, 🇨🇦, 中, 👨‍👩‍👧, !)

## Files to Modify

1. `src/buffer.c` - Replace `cursor_next_grapheme()` and `cursor_prev_grapheme()`
2. `src/buffer.c` - Can remove `codepoint_is_combining_mark()` after fix
3. `src/buffer.h` - Update function signatures if needed

## References

- [UAX #29: Unicode Text Segmentation](https://unicode.org/reports/tr29/)
- [utflite grapheme functions](../utflite/include/utflite/utflite.h:129-140)
