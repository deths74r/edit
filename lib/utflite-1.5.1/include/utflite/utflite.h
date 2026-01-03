/*
 * utflite.h - Lightweight UTF-8 handling library
 *
 * Unicode 17.0 compliant UTF-8 encoding/decoding with character width support.
 * Zero external dependencies beyond standard C.
 *
 * Usage:
 *   #include <utflite/utflite.h>
 *
 * Or use the single-header version:
 *   #define UTFLITE_IMPLEMENTATION
 *   #include "utflite.h"
 */

#ifndef UTFLITE_H
#define UTFLITE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Library version: major.minor.patch */
#define UTFLITE_VERSION_MAJOR 1
#define UTFLITE_VERSION_MINOR 5
#define UTFLITE_VERSION_PATCH 1
#define UTFLITE_VERSION_STRING "1.5.1"

#define UTFLITE_UNICODE_VERSION 170  /* Unicode 17.0 */

/* Unicode replacement character (returned on decode errors) */
#define UTFLITE_REPLACEMENT_CHAR 0xFFFD

/* Maximum bytes needed to encode any Unicode codepoint */
#define UTFLITE_MAX_BYTES 4

/* ============================================================================
 * Core Encoding/Decoding
 * ============================================================================ */

/*
 * Decodes a UTF-8 byte sequence into a Unicode codepoint.
 *
 * Parameters:
 *   bytes     - Pointer to UTF-8 encoded bytes
 *   length    - Number of bytes available in buffer
 *   codepoint - Output: decoded codepoint (U+FFFD on error)
 *
 * Returns:
 *   Number of bytes consumed (1-4), or 0 for empty/null input.
 *
 * Error handling:
 *   - Invalid sequences set *codepoint to U+FFFD
 *   - Overlong encodings are rejected
 *   - Surrogate pairs (U+D800-U+DFFF) are rejected
 *   - Values > U+10FFFF are rejected
 */
int utflite_decode(const char *bytes, int length, uint32_t *codepoint);

/*
 * Encodes a Unicode codepoint as UTF-8.
 *
 * Parameters:
 *   codepoint - Unicode codepoint to encode (0x0000-0x10FFFF)
 *   buffer    - Output buffer (must have at least UTFLITE_MAX_BYTES space)
 *
 * Returns:
 *   Number of bytes written (1-4), or 0 for invalid codepoints.
 *
 * Note: Does not null-terminate. Caller manages buffer.
 */
int utflite_encode(uint32_t codepoint, char *buffer);

/* ============================================================================
 * Character Width (Display Columns)
 * ============================================================================ */

/*
 * Returns the display width of a Unicode codepoint.
 *
 * Returns:
 *   -1  Non-printable control character (C0/C1 except NUL)
 *    0  Zero-width (combining marks, format chars, ZWJ, etc.)
 *    1  Normal width (ASCII, Latin, most scripts)
 *    2  Double-width (CJK ideographs, fullwidth forms, emoji)
 */
int utflite_codepoint_width(uint32_t codepoint);

/*
 * Returns display width of UTF-8 character at given offset.
 * Convenience function combining decode + width lookup.
 */
int utflite_char_width(const char *text, int length, int offset);

/* ============================================================================
 * String Navigation
 * ============================================================================ */

/*
 * Returns byte offset of the next character.
 *
 * Parameters:
 *   text   - UTF-8 string
 *   length - Total bytes in string
 *   offset - Current byte position
 *
 * Returns:
 *   Byte offset of next character, or 'length' if at end.
 */
int utflite_next_char(const char *text, int length, int offset);

/*
 * Returns byte offset of the previous character.
 *
 * Parameters:
 *   text   - UTF-8 string
 *   offset - Current byte position
 *
 * Returns:
 *   Byte offset of previous character start, or 0 if at beginning.
 */
int utflite_prev_char(const char *text, int offset);

/* ============================================================================
 * Grapheme Cluster Navigation (UAX #29)
 * ============================================================================ */

/*
 * Returns byte offset of next grapheme cluster boundary.
 * Implements UAX #29 extended grapheme cluster segmentation.
 * Handles emoji sequences, combining marks, flags, and Hangul.
 */
int utflite_next_grapheme(const char *text, int length, int offset);

/*
 * Returns byte offset of previous grapheme cluster boundary.
 * Implements UAX #29 extended grapheme cluster segmentation.
 */
int utflite_prev_grapheme(const char *text, int offset);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/*
 * Validates a UTF-8 string.
 *
 * Parameters:
 *   text         - UTF-8 string to validate
 *   length       - Number of bytes in string
 *   error_offset - Optional: set to byte offset of first invalid sequence
 *
 * Returns:
 *   1 if valid UTF-8, 0 if invalid.
 */
int utflite_validate(const char *text, int length, int *error_offset);

/*
 * Counts the number of Unicode codepoints in a UTF-8 string.
 *
 * Returns:
 *   Number of codepoints (not bytes, not graphemes).
 */
int utflite_codepoint_count(const char *text, int length);

/*
 * Calculates display width of a UTF-8 string.
 *
 * Returns:
 *   Total display columns needed. Control characters count as 0.
 */
int utflite_string_width(const char *text, int length);

/*
 * Checks if a codepoint is zero-width (combining marks, format chars, ZWJ, etc).
 *
 * Returns:
 *   1 if codepoint is zero-width, 0 otherwise.
 */
int utflite_is_zero_width(uint32_t codepoint);

/*
 * Checks if a codepoint is double-width (CJK, emoji, etc).
 *
 * Returns:
 *   1 if codepoint is double-width, 0 otherwise.
 */
int utflite_is_wide(uint32_t codepoint);

/*
 * Finds byte offset to truncate string at a maximum display width.
 *
 * Parameters:
 *   text     - UTF-8 string
 *   length   - Number of bytes in string
 *   max_cols - Maximum display columns
 *
 * Returns:
 *   Byte offset where string should be truncated to fit within max_cols.
 *   Returns length if string already fits.
 */
int utflite_truncate(const char *text, int length, int max_cols);

#ifdef __cplusplus
}
#endif

#endif /* UTFLITE_H */
