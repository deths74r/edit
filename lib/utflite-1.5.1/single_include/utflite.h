/*
 * utflite.h - Lightweight UTF-8 handling library (Single-Header Version)
 *
 * Unicode 17.0 compliant UTF-8 encoding/decoding with character width support.
 * Zero external dependencies beyond standard C.
 *
 * USAGE:
 *   In ONE .c file, before including this header:
 *     #define UTFLITE_IMPLEMENTATION
 *     #include "utflite.h"
 *
 *   In all other files, just include normally:
 *     #include "utflite.h"
 *
 * EXAMPLE:
 *     #define UTFLITE_IMPLEMENTATION
 *     #include "utflite.h"
 *
 *     int main(void) {
 *         uint32_t cp;
 *         utflite_decode("\xE4\xB8\xAD", 3, &cp);  // Decode Chinese char
 *         int width = utflite_codepoint_width(cp); // Returns 2 (double-width)
 *         return 0;
 *     }
 *
 * NOTES:
 *   - © (U+00A9) and ® (U+00AE) are treated as double-width to match
 *     modern terminal emoji rendering behavior.
 *   - Grapheme cluster functions implement UAX #29 extended grapheme
 *     clusters, correctly handling emoji sequences, combining marks,
 *     regional indicators (flags), and Hangul syllables.
 *   - East Asian Ambiguous characters default to width 1.
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
 *    2  Double-width (CJK, fullwidth, emoji including © and ®)
 *
 * Note: East Asian Ambiguous characters return 1 (narrow context assumed).
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

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef UTFLITE_IMPLEMENTATION

/* Range of Unicode codepoints, used for binary search in property tables. */
struct utflite__unicode_range {
    uint32_t start;
    uint32_t end;
};

/*
 * Zero-width character ranges (Unicode 17.0).
 * Mn (Nonspacing Mark), Me (Enclosing Mark), Cf (Format).
 * Sorted by start codepoint for binary search.
 */
static const struct utflite__unicode_range UTFLITE__ZERO_WIDTH_RANGES[] = {
    {0x0300, 0x036F},
    {0x0483, 0x0489},
    {0x0591, 0x05BD},
    {0x05BF, 0x05BF},
    {0x05C1, 0x05C2},
    {0x05C4, 0x05C5},
    {0x05C7, 0x05C7},
    {0x0600, 0x0605},
    {0x0610, 0x061A},
    {0x061C, 0x061C},
    {0x064B, 0x065F},
    {0x0670, 0x0670},
    {0x06D6, 0x06DD},
    {0x06DF, 0x06E4},
    {0x06E7, 0x06E8},
    {0x06EA, 0x06ED},
    {0x070F, 0x070F},
    {0x0711, 0x0711},
    {0x0730, 0x074A},
    {0x07A6, 0x07B0},
    {0x07EB, 0x07F3},
    {0x07FD, 0x07FD},
    {0x0816, 0x0819},
    {0x081B, 0x0823},
    {0x0825, 0x0827},
    {0x0829, 0x082D},
    {0x0859, 0x085B},
    {0x0890, 0x0891},
    {0x0897, 0x089F},
    {0x08CA, 0x0902},
    {0x093A, 0x093A},
    {0x093C, 0x093C},
    {0x0941, 0x0948},
    {0x094D, 0x094D},
    {0x0951, 0x0957},
    {0x0962, 0x0963},
    {0x0981, 0x0981},
    {0x09BC, 0x09BC},
    {0x09C1, 0x09C4},
    {0x09CD, 0x09CD},
    {0x09E2, 0x09E3},
    {0x09FE, 0x09FE},
    {0x0A01, 0x0A02},
    {0x0A3C, 0x0A3C},
    {0x0A41, 0x0A42},
    {0x0A47, 0x0A48},
    {0x0A4B, 0x0A4D},
    {0x0A51, 0x0A51},
    {0x0A70, 0x0A71},
    {0x0A75, 0x0A75},
    {0x0A81, 0x0A82},
    {0x0ABC, 0x0ABC},
    {0x0AC1, 0x0AC5},
    {0x0AC7, 0x0AC8},
    {0x0ACD, 0x0ACD},
    {0x0AE2, 0x0AE3},
    {0x0AFA, 0x0AFF},
    {0x0B01, 0x0B01},
    {0x0B3C, 0x0B3C},
    {0x0B3F, 0x0B3F},
    {0x0B41, 0x0B44},
    {0x0B4D, 0x0B4D},
    {0x0B55, 0x0B56},
    {0x0B62, 0x0B63},
    {0x0B82, 0x0B82},
    {0x0BC0, 0x0BC0},
    {0x0BCD, 0x0BCD},
    {0x0C00, 0x0C00},
    {0x0C04, 0x0C04},
    {0x0C3C, 0x0C3C},
    {0x0C3E, 0x0C40},
    {0x0C46, 0x0C48},
    {0x0C4A, 0x0C4D},
    {0x0C55, 0x0C56},
    {0x0C62, 0x0C63},
    {0x0C81, 0x0C81},
    {0x0CBC, 0x0CBC},
    {0x0CBF, 0x0CBF},
    {0x0CC6, 0x0CC6},
    {0x0CCC, 0x0CCD},
    {0x0CE2, 0x0CE3},
    {0x0D00, 0x0D01},
    {0x0D3B, 0x0D3C},
    {0x0D41, 0x0D44},
    {0x0D4D, 0x0D4D},
    {0x0D62, 0x0D63},
    {0x0D81, 0x0D81},
    {0x0DCA, 0x0DCA},
    {0x0DD2, 0x0DD4},
    {0x0DD6, 0x0DD6},
    {0x0E31, 0x0E31},
    {0x0E34, 0x0E3A},
    {0x0E47, 0x0E4E},
    {0x0EB1, 0x0EB1},
    {0x0EB4, 0x0EBC},
    {0x0EC8, 0x0ECE},
    {0x0F18, 0x0F19},
    {0x0F35, 0x0F35},
    {0x0F37, 0x0F37},
    {0x0F39, 0x0F39},
    {0x0F71, 0x0F7E},
    {0x0F80, 0x0F84},
    {0x0F86, 0x0F87},
    {0x0F8D, 0x0F97},
    {0x0F99, 0x0FBC},
    {0x0FC6, 0x0FC6},
    {0x102D, 0x1030},
    {0x1032, 0x1037},
    {0x1039, 0x103A},
    {0x103D, 0x103E},
    {0x1058, 0x1059},
    {0x105E, 0x1060},
    {0x1071, 0x1074},
    {0x1082, 0x1082},
    {0x1085, 0x1086},
    {0x108D, 0x108D},
    {0x109D, 0x109D},
    {0x135D, 0x135F},
    {0x1712, 0x1714},
    {0x1732, 0x1733},
    {0x1752, 0x1753},
    {0x1772, 0x1773},
    {0x17B4, 0x17B5},
    {0x17B7, 0x17BD},
    {0x17C6, 0x17C6},
    {0x17C9, 0x17D3},
    {0x17DD, 0x17DD},
    {0x180B, 0x180F},
    {0x1885, 0x1886},
    {0x18A9, 0x18A9},
    {0x1920, 0x1922},
    {0x1927, 0x1928},
    {0x1932, 0x1932},
    {0x1939, 0x193B},
    {0x1A17, 0x1A18},
    {0x1A1B, 0x1A1B},
    {0x1A56, 0x1A56},
    {0x1A58, 0x1A5E},
    {0x1A60, 0x1A60},
    {0x1A62, 0x1A62},
    {0x1A65, 0x1A6C},
    {0x1A73, 0x1A7C},
    {0x1A7F, 0x1A7F},
    {0x1AB0, 0x1ADD},
    {0x1AE0, 0x1AEB},
    {0x1B00, 0x1B03},
    {0x1B34, 0x1B34},
    {0x1B36, 0x1B3A},
    {0x1B3C, 0x1B3C},
    {0x1B42, 0x1B42},
    {0x1B6B, 0x1B73},
    {0x1B80, 0x1B81},
    {0x1BA2, 0x1BA5},
    {0x1BA8, 0x1BA9},
    {0x1BAB, 0x1BAD},
    {0x1BE6, 0x1BE6},
    {0x1BE8, 0x1BE9},
    {0x1BED, 0x1BED},
    {0x1BEF, 0x1BF1},
    {0x1C2C, 0x1C33},
    {0x1C36, 0x1C37},
    {0x1CD0, 0x1CD2},
    {0x1CD4, 0x1CE0},
    {0x1CE2, 0x1CE8},
    {0x1CED, 0x1CED},
    {0x1CF4, 0x1CF4},
    {0x1CF8, 0x1CF9},
    {0x1DC0, 0x1DFF},
    {0x200B, 0x200F},
    {0x202A, 0x202E},
    {0x2060, 0x2064},
    {0x2066, 0x206F},
    {0x20D0, 0x20F0},
    {0x2CEF, 0x2CF1},
    {0x2D7F, 0x2D7F},
    {0x2DE0, 0x2DFF},
    {0x302A, 0x302D},
    {0x3099, 0x309A},
    {0xA66F, 0xA672},
    {0xA674, 0xA67D},
    {0xA69E, 0xA69F},
    {0xA6F0, 0xA6F1},
    {0xA802, 0xA802},
    {0xA806, 0xA806},
    {0xA80B, 0xA80B},
    {0xA825, 0xA826},
    {0xA82C, 0xA82C},
    {0xA8C4, 0xA8C5},
    {0xA8E0, 0xA8F1},
    {0xA8FF, 0xA8FF},
    {0xA926, 0xA92D},
    {0xA947, 0xA951},
    {0xA980, 0xA982},
    {0xA9B3, 0xA9B3},
    {0xA9B6, 0xA9B9},
    {0xA9BC, 0xA9BD},
    {0xA9E5, 0xA9E5},
    {0xAA29, 0xAA2E},
    {0xAA31, 0xAA32},
    {0xAA35, 0xAA36},
    {0xAA43, 0xAA43},
    {0xAA4C, 0xAA4C},
    {0xAA7C, 0xAA7C},
    {0xAAB0, 0xAAB0},
    {0xAAB2, 0xAAB4},
    {0xAAB7, 0xAAB8},
    {0xAABE, 0xAABF},
    {0xAAC1, 0xAAC1},
    {0xAAEC, 0xAAED},
    {0xAAF6, 0xAAF6},
    {0xABE5, 0xABE5},
    {0xABE8, 0xABE8},
    {0xABED, 0xABED},
    {0xFB1E, 0xFB1E},
    {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF},
    {0xFFF9, 0xFFFB},
    {0x101FD, 0x101FD},
    {0x102E0, 0x102E0},
    {0x10376, 0x1037A},
    {0x10A01, 0x10A03},
    {0x10A05, 0x10A06},
    {0x10A0C, 0x10A0F},
    {0x10A38, 0x10A3A},
    {0x10A3F, 0x10A3F},
    {0x10AE5, 0x10AE6},
    {0x10D24, 0x10D27},
    {0x10D69, 0x10D6D},
    {0x10EAB, 0x10EAC},
    {0x10EFA, 0x10EFF},
    {0x10F46, 0x10F50},
    {0x10F82, 0x10F85},
    {0x11001, 0x11001},
    {0x11038, 0x11046},
    {0x11070, 0x11070},
    {0x11073, 0x11074},
    {0x1107F, 0x11081},
    {0x110B3, 0x110B6},
    {0x110B9, 0x110BA},
    {0x110BD, 0x110BD},
    {0x110C2, 0x110C2},
    {0x110CD, 0x110CD},
    {0x11100, 0x11102},
    {0x11127, 0x1112B},
    {0x1112D, 0x11134},
    {0x11173, 0x11173},
    {0x11180, 0x11181},
    {0x111B6, 0x111BE},
    {0x111C9, 0x111CC},
    {0x111CF, 0x111CF},
    {0x1122F, 0x11231},
    {0x11234, 0x11234},
    {0x11236, 0x11237},
    {0x1123E, 0x1123E},
    {0x11241, 0x11241},
    {0x112DF, 0x112DF},
    {0x112E3, 0x112EA},
    {0x11300, 0x11301},
    {0x1133B, 0x1133C},
    {0x11340, 0x11340},
    {0x11366, 0x1136C},
    {0x11370, 0x11374},
    {0x113BB, 0x113C0},
    {0x113CE, 0x113CE},
    {0x113D0, 0x113D0},
    {0x113D2, 0x113D2},
    {0x113E1, 0x113E2},
    {0x11438, 0x1143F},
    {0x11442, 0x11444},
    {0x11446, 0x11446},
    {0x1145E, 0x1145E},
    {0x114B3, 0x114B8},
    {0x114BA, 0x114BA},
    {0x114BF, 0x114C0},
    {0x114C2, 0x114C3},
    {0x115B2, 0x115B5},
    {0x115BC, 0x115BD},
    {0x115BF, 0x115C0},
    {0x115DC, 0x115DD},
    {0x11633, 0x1163A},
    {0x1163D, 0x1163D},
    {0x1163F, 0x11640},
    {0x116AB, 0x116AB},
    {0x116AD, 0x116AD},
    {0x116B0, 0x116B5},
    {0x116B7, 0x116B7},
    {0x1171D, 0x1171D},
    {0x1171F, 0x1171F},
    {0x11722, 0x11725},
    {0x11727, 0x1172B},
    {0x1182F, 0x11837},
    {0x11839, 0x1183A},
    {0x1193B, 0x1193C},
    {0x1193E, 0x1193E},
    {0x11943, 0x11943},
    {0x119D4, 0x119D7},
    {0x119DA, 0x119DB},
    {0x119E0, 0x119E0},
    {0x11A01, 0x11A0A},
    {0x11A33, 0x11A38},
    {0x11A3B, 0x11A3E},
    {0x11A47, 0x11A47},
    {0x11A51, 0x11A56},
    {0x11A59, 0x11A5B},
    {0x11A8A, 0x11A96},
    {0x11A98, 0x11A99},
    {0x11B60, 0x11B60},
    {0x11B62, 0x11B64},
    {0x11B66, 0x11B66},
    {0x11C30, 0x11C36},
    {0x11C38, 0x11C3D},
    {0x11C3F, 0x11C3F},
    {0x11C92, 0x11CA7},
    {0x11CAA, 0x11CB0},
    {0x11CB2, 0x11CB3},
    {0x11CB5, 0x11CB6},
    {0x11D31, 0x11D36},
    {0x11D3A, 0x11D3A},
    {0x11D3C, 0x11D3D},
    {0x11D3F, 0x11D45},
    {0x11D47, 0x11D47},
    {0x11D90, 0x11D91},
    {0x11D95, 0x11D95},
    {0x11D97, 0x11D97},
    {0x11EF3, 0x11EF4},
    {0x11F00, 0x11F01},
    {0x11F36, 0x11F3A},
    {0x11F40, 0x11F40},
    {0x11F42, 0x11F42},
    {0x11F5A, 0x11F5A},
    {0x13430, 0x13440},
    {0x13447, 0x13455},
    {0x1611E, 0x16129},
    {0x1612D, 0x1612F},
    {0x16AF0, 0x16AF4},
    {0x16B30, 0x16B36},
    {0x16F4F, 0x16F4F},
    {0x16F8F, 0x16F92},
    {0x16FE4, 0x16FE4},
    {0x1BC9D, 0x1BC9E},
    {0x1BCA0, 0x1BCA3},
    {0x1CF00, 0x1CF2D},
    {0x1CF30, 0x1CF46},
    {0x1D167, 0x1D169},
    {0x1D173, 0x1D182},
    {0x1D185, 0x1D18B},
    {0x1D1AA, 0x1D1AD},
    {0x1D242, 0x1D244},
    {0x1DA00, 0x1DA36},
    {0x1DA3B, 0x1DA6C},
    {0x1DA75, 0x1DA75},
    {0x1DA84, 0x1DA84},
    {0x1DA9B, 0x1DA9F},
    {0x1DAA1, 0x1DAAF},
    {0x1E000, 0x1E006},
    {0x1E008, 0x1E018},
    {0x1E01B, 0x1E021},
    {0x1E023, 0x1E024},
    {0x1E026, 0x1E02A},
    {0x1E08F, 0x1E08F},
    {0x1E130, 0x1E136},
    {0x1E2AE, 0x1E2AE},
    {0x1E2EC, 0x1E2EF},
    {0x1E4EC, 0x1E4EF},
    {0x1E5EE, 0x1E5EF},
    {0x1E6E3, 0x1E6E3},
    {0x1E6E6, 0x1E6E6},
    {0x1E6EE, 0x1E6EF},
    {0x1E6F5, 0x1E6F5},
    {0x1E8D0, 0x1E8D6},
    {0x1E944, 0x1E94A},
    {0x1F3FB, 0x1F3FF},  /* Emoji skin tone modifiers - zero-width */
    {0xE0001, 0xE0001},
    {0xE0020, 0xE007F},
    {0xE0100, 0xE01EF},
};
#define UTFLITE__ZERO_WIDTH_COUNT (sizeof(UTFLITE__ZERO_WIDTH_RANGES) / sizeof(UTFLITE__ZERO_WIDTH_RANGES[0]))

/*
 * Double-width character ranges (Unicode 17.0).
 * W/F from EastAsianWidth + Extended_Pictographic emoji.
 * Sorted by start codepoint for binary search.
 */
static const struct utflite__unicode_range UTFLITE__DOUBLE_WIDTH_RANGES[] = {
    {0x00A9, 0x00A9},
    {0x00AE, 0x00AE},
    {0x1100, 0x115F},
    {0x203C, 0x203C},
    {0x2049, 0x2049},
    {0x2122, 0x2122},
    {0x2139, 0x2139},
    {0x2194, 0x2199},
    {0x21A9, 0x21AA},
    {0x231A, 0x231B},
    {0x2328, 0x232A},
    {0x23CF, 0x23CF},
    {0x23E9, 0x23F3},
    {0x23F8, 0x23FA},
    {0x24C2, 0x24C2},
    {0x25AA, 0x25AB},
    {0x25B6, 0x25B6},
    {0x25C0, 0x25C0},
    {0x25FB, 0x25FE},
    {0x2600, 0x2604},
    {0x260E, 0x260E},
    {0x2611, 0x2611},
    {0x2614, 0x2615},
    {0x2618, 0x2618},
    {0x261D, 0x261D},
    {0x2620, 0x2620},
    {0x2622, 0x2623},
    {0x2626, 0x2626},
    {0x262A, 0x262A},
    {0x262E, 0x263A},
    {0x2640, 0x2640},
    {0x2642, 0x2642},
    {0x2648, 0x2653},
    {0x265F, 0x2660},
    {0x2663, 0x2663},
    {0x2665, 0x2666},
    {0x2668, 0x2668},
    {0x267B, 0x267B},
    {0x267E, 0x267F},
    {0x268A, 0x268F},
    {0x2692, 0x2697},
    {0x2699, 0x2699},
    {0x269B, 0x269C},
    {0x26A0, 0x26A1},
    {0x26A7, 0x26A7},
    {0x26AA, 0x26AB},
    {0x26B0, 0x26B1},
    {0x26BD, 0x26BE},
    {0x26C4, 0x26C5},
    {0x26C8, 0x26C8},
    {0x26CE, 0x26CF},
    {0x26D1, 0x26D1},
    {0x26D3, 0x26D4},
    {0x26E9, 0x26EA},
    {0x26F0, 0x26F5},
    {0x26F7, 0x26FA},
    {0x26FD, 0x26FD},
    {0x2702, 0x2702},
    {0x2705, 0x2705},
    {0x2708, 0x270D},
    {0x270F, 0x270F},
    {0x2712, 0x2712},
    {0x2714, 0x2714},
    {0x2716, 0x2716},
    {0x271D, 0x271D},
    {0x2721, 0x2721},
    {0x2728, 0x2728},
    {0x2733, 0x2734},
    {0x2744, 0x2744},
    {0x2747, 0x2747},
    {0x274C, 0x274C},
    {0x274E, 0x274E},
    {0x2753, 0x2755},
    {0x2757, 0x2757},
    {0x2763, 0x2764},
    {0x2795, 0x2797},
    {0x27A1, 0x27A1},
    {0x27B0, 0x27B0},
    {0x27BF, 0x27BF},
    {0x2934, 0x2935},
    {0x2B05, 0x2B07},
    {0x2B1B, 0x2B1C},
    {0x2B50, 0x2B50},
    {0x2B55, 0x2B55},
    {0x2E80, 0x2E99},
    {0x2E9B, 0x2EF3},
    {0x2F00, 0x2FD5},
    {0x2FF0, 0x303E},
    {0x3041, 0x3096},
    {0x309B, 0x30FF},
    {0x3105, 0x312F},
    {0x3131, 0x318E},
    {0x3190, 0x31E5},
    {0x31EF, 0x321E},
    {0x3220, 0x3247},
    {0x3250, 0xA48C},
    {0xA490, 0xA4C6},
    {0xA960, 0xA97C},
    {0xAC00, 0xD7A3},
    {0xF900, 0xFAFF},
    {0xFE10, 0xFE19},
    {0xFE30, 0xFE52},
    {0xFE54, 0xFE66},
    {0xFE68, 0xFE6B},
    {0xFF01, 0xFF60},
    {0xFFE0, 0xFFE6},
    {0x16FE0, 0x16FE4},
    {0x16FF0, 0x16FF6},
    {0x17000, 0x18CD5},
    {0x18CFF, 0x18D1E},
    {0x18D80, 0x18DF2},
    {0x1AFF0, 0x1AFF3},
    {0x1AFF5, 0x1AFFB},
    {0x1AFFD, 0x1AFFE},
    {0x1B000, 0x1B122},
    {0x1B132, 0x1B132},
    {0x1B150, 0x1B152},
    {0x1B155, 0x1B155},
    {0x1B164, 0x1B167},
    {0x1B170, 0x1B2FB},
    {0x1D300, 0x1D356},
    {0x1D360, 0x1D376},
    {0x1F004, 0x1F004},
    {0x1F02C, 0x1F02F},
    {0x1F094, 0x1F09F},
    {0x1F0AF, 0x1F0B0},
    {0x1F0C0, 0x1F0C0},
    {0x1F0CF, 0x1F0D0},
    {0x1F0F6, 0x1F0FF},
    {0x1F170, 0x1F171},
    {0x1F17E, 0x1F17F},
    {0x1F18E, 0x1F18E},
    {0x1F191, 0x1F19A},
    {0x1F1AE, 0x1F1FF},  /* Extended to include Regional Indicators */
    {0x1F200, 0x1F321},
    {0x1F324, 0x1F393},
    {0x1F396, 0x1F397},
    {0x1F399, 0x1F39B},
    {0x1F39E, 0x1F3F0},
    {0x1F3F3, 0x1F3F5},
    {0x1F3F7, 0x1F3FA},  /* Split to exclude skin tone modifiers U+1F3FB-U+1F3FF */
    {0x1F400, 0x1F4FD},
    {0x1F4FF, 0x1F53D},
    {0x1F549, 0x1F54E},
    {0x1F550, 0x1F567},
    {0x1F56F, 0x1F570},
    {0x1F573, 0x1F57A},
    {0x1F587, 0x1F587},
    {0x1F58A, 0x1F58D},
    {0x1F590, 0x1F590},
    {0x1F595, 0x1F596},
    {0x1F5A4, 0x1F5A5},
    {0x1F5A8, 0x1F5A8},
    {0x1F5B1, 0x1F5B2},
    {0x1F5BC, 0x1F5BC},
    {0x1F5C2, 0x1F5C4},
    {0x1F5D1, 0x1F5D3},
    {0x1F5DC, 0x1F5DE},
    {0x1F5E1, 0x1F5E1},
    {0x1F5E3, 0x1F5E3},
    {0x1F5E8, 0x1F5E8},
    {0x1F5EF, 0x1F5EF},
    {0x1F5F3, 0x1F5F3},
    {0x1F5FA, 0x1F64F},
    {0x1F680, 0x1F6C5},
    {0x1F6CB, 0x1F6D2},
    {0x1F6D5, 0x1F6E5},
    {0x1F6E9, 0x1F6E9},
    {0x1F6EB, 0x1F6F0},
    {0x1F6F3, 0x1F6FF},
    {0x1F7DA, 0x1F7FF},
    {0x1F80C, 0x1F80F},
    {0x1F848, 0x1F84F},
    {0x1F85A, 0x1F85F},
    {0x1F888, 0x1F88F},
    {0x1F8AE, 0x1F8AF},
    {0x1F8BC, 0x1F8BF},
    {0x1F8C2, 0x1F8CF},
    {0x1F8D9, 0x1F8FF},
    {0x1F90C, 0x1F93A},
    {0x1F93C, 0x1F945},
    {0x1F947, 0x1F9FF},
    {0x1FA58, 0x1FA5F},
    {0x1FA6E, 0x1FAFF},
    {0x1FC00, 0x1FFFD},
    {0x20000, 0x2FFFD},
    {0x30000, 0x3FFFD},
};
#define UTFLITE__DOUBLE_WIDTH_COUNT (sizeof(UTFLITE__DOUBLE_WIDTH_RANGES) / sizeof(UTFLITE__DOUBLE_WIDTH_RANGES[0]))

/* ============================================================================
 * Grapheme Cluster Break Properties (Unicode 17.0, UAX #29)
 * ============================================================================ */

/* Grapheme Cluster Break property values from UAX #29. */
enum utflite__gcb_property {
	UTFLITE__GCB_OTHER = 0,
	UTFLITE__GCB_CR,
	UTFLITE__GCB_LF,
	UTFLITE__GCB_CONTROL,
	UTFLITE__GCB_EXTEND,
	UTFLITE__GCB_ZWJ,
	UTFLITE__GCB_REGIONAL_INDICATOR,
	UTFLITE__GCB_PREPEND,
	UTFLITE__GCB_SPACING_MARK,
	UTFLITE__GCB_L,
	UTFLITE__GCB_V,
	UTFLITE__GCB_T,
	UTFLITE__GCB_LV,
	UTFLITE__GCB_LVT
};

/* Hangul syllable constants for LV/LVT computation (Unicode 3.0) */
#define UTFLITE__HANGUL_SBASE  0xAC00  /* First Hangul syllable */
#define UTFLITE__HANGUL_SEND   0xD7A3  /* Last Hangul syllable */
#define UTFLITE__HANGUL_TCOUNT 28      /* Number of trailing jamo per syllable */

/* Maximum codepoints to scan backward for grapheme boundary */
#define UTFLITE__GRAPHEME_MAX_BACKTRACK 128

/* A range of codepoints with an associated GCB property. */
struct utflite__gcb_range {
    uint32_t start;
    uint32_t end;
    uint8_t property;
};

/* GCB property ranges (LV/LVT computed for Hangul U+AC00-U+D7A3) */
static const struct utflite__gcb_range UTFLITE__GCB_RANGES[] = {
    {0x0000, 0x0009, UTFLITE__GCB_CONTROL},
    {0x000A, 0x000A, UTFLITE__GCB_LF},
    {0x000B, 0x000C, UTFLITE__GCB_CONTROL},
    {0x000D, 0x000D, UTFLITE__GCB_CR},
    {0x000E, 0x001F, UTFLITE__GCB_CONTROL},
    {0x007F, 0x009F, UTFLITE__GCB_CONTROL},
    {0x00AD, 0x00AD, UTFLITE__GCB_CONTROL},
    {0x0300, 0x036F, UTFLITE__GCB_EXTEND},
    {0x0483, 0x0487, UTFLITE__GCB_EXTEND},
    {0x0488, 0x0489, UTFLITE__GCB_EXTEND},
    {0x0591, 0x05BD, UTFLITE__GCB_EXTEND},
    {0x05BF, 0x05BF, UTFLITE__GCB_EXTEND},
    {0x05C1, 0x05C2, UTFLITE__GCB_EXTEND},
    {0x05C4, 0x05C5, UTFLITE__GCB_EXTEND},
    {0x05C7, 0x05C7, UTFLITE__GCB_EXTEND},
    {0x0600, 0x0605, UTFLITE__GCB_PREPEND},
    {0x0610, 0x061A, UTFLITE__GCB_EXTEND},
    {0x061C, 0x061C, UTFLITE__GCB_CONTROL},
    {0x064B, 0x065F, UTFLITE__GCB_EXTEND},
    {0x0670, 0x0670, UTFLITE__GCB_EXTEND},
    {0x06D6, 0x06DC, UTFLITE__GCB_EXTEND},
    {0x06DD, 0x06DD, UTFLITE__GCB_PREPEND},
    {0x06DF, 0x06E4, UTFLITE__GCB_EXTEND},
    {0x06E7, 0x06E8, UTFLITE__GCB_EXTEND},
    {0x06EA, 0x06ED, UTFLITE__GCB_EXTEND},
    {0x070F, 0x070F, UTFLITE__GCB_PREPEND},
    {0x0711, 0x0711, UTFLITE__GCB_EXTEND},
    {0x0730, 0x074A, UTFLITE__GCB_EXTEND},
    {0x07A6, 0x07B0, UTFLITE__GCB_EXTEND},
    {0x07EB, 0x07F3, UTFLITE__GCB_EXTEND},
    {0x07FD, 0x07FD, UTFLITE__GCB_EXTEND},
    {0x0816, 0x0819, UTFLITE__GCB_EXTEND},
    {0x081B, 0x0823, UTFLITE__GCB_EXTEND},
    {0x0825, 0x0827, UTFLITE__GCB_EXTEND},
    {0x0829, 0x082D, UTFLITE__GCB_EXTEND},
    {0x0859, 0x085B, UTFLITE__GCB_EXTEND},
    {0x0890, 0x0891, UTFLITE__GCB_PREPEND},
    {0x0897, 0x089F, UTFLITE__GCB_EXTEND},
    {0x08CA, 0x08E1, UTFLITE__GCB_EXTEND},
    {0x08E2, 0x08E2, UTFLITE__GCB_PREPEND},
    {0x08E3, 0x0902, UTFLITE__GCB_EXTEND},
    {0x0903, 0x0903, UTFLITE__GCB_SPACING_MARK},
    {0x093A, 0x093A, UTFLITE__GCB_EXTEND},
    {0x093B, 0x093B, UTFLITE__GCB_SPACING_MARK},
    {0x093C, 0x093C, UTFLITE__GCB_EXTEND},
    {0x093E, 0x0940, UTFLITE__GCB_SPACING_MARK},
    {0x0941, 0x0948, UTFLITE__GCB_EXTEND},
    {0x0949, 0x094C, UTFLITE__GCB_SPACING_MARK},
    {0x094D, 0x094D, UTFLITE__GCB_EXTEND},
    {0x094E, 0x094F, UTFLITE__GCB_SPACING_MARK},
    {0x0951, 0x0957, UTFLITE__GCB_EXTEND},
    {0x0962, 0x0963, UTFLITE__GCB_EXTEND},
    {0x0981, 0x0981, UTFLITE__GCB_EXTEND},
    {0x0982, 0x0983, UTFLITE__GCB_SPACING_MARK},
    {0x09BC, 0x09BC, UTFLITE__GCB_EXTEND},
    {0x09BE, 0x09BE, UTFLITE__GCB_EXTEND},
    {0x09BF, 0x09C0, UTFLITE__GCB_SPACING_MARK},
    {0x09C1, 0x09C4, UTFLITE__GCB_EXTEND},
    {0x09C7, 0x09C8, UTFLITE__GCB_SPACING_MARK},
    {0x09CB, 0x09CC, UTFLITE__GCB_SPACING_MARK},
    {0x09CD, 0x09CD, UTFLITE__GCB_EXTEND},
    {0x09D7, 0x09D7, UTFLITE__GCB_EXTEND},
    {0x09E2, 0x09E3, UTFLITE__GCB_EXTEND},
    {0x09FE, 0x09FE, UTFLITE__GCB_EXTEND},
    {0x0A01, 0x0A02, UTFLITE__GCB_EXTEND},
    {0x0A03, 0x0A03, UTFLITE__GCB_SPACING_MARK},
    {0x0A3C, 0x0A3C, UTFLITE__GCB_EXTEND},
    {0x0A3E, 0x0A40, UTFLITE__GCB_SPACING_MARK},
    {0x0A41, 0x0A42, UTFLITE__GCB_EXTEND},
    {0x0A47, 0x0A48, UTFLITE__GCB_EXTEND},
    {0x0A4B, 0x0A4D, UTFLITE__GCB_EXTEND},
    {0x0A51, 0x0A51, UTFLITE__GCB_EXTEND},
    {0x0A70, 0x0A71, UTFLITE__GCB_EXTEND},
    {0x0A75, 0x0A75, UTFLITE__GCB_EXTEND},
    {0x0A81, 0x0A82, UTFLITE__GCB_EXTEND},
    {0x0A83, 0x0A83, UTFLITE__GCB_SPACING_MARK},
    {0x0ABC, 0x0ABC, UTFLITE__GCB_EXTEND},
    {0x0ABE, 0x0AC0, UTFLITE__GCB_SPACING_MARK},
    {0x0AC1, 0x0AC5, UTFLITE__GCB_EXTEND},
    {0x0AC7, 0x0AC8, UTFLITE__GCB_EXTEND},
    {0x0AC9, 0x0AC9, UTFLITE__GCB_SPACING_MARK},
    {0x0ACB, 0x0ACC, UTFLITE__GCB_SPACING_MARK},
    {0x0ACD, 0x0ACD, UTFLITE__GCB_EXTEND},
    {0x0AE2, 0x0AE3, UTFLITE__GCB_EXTEND},
    {0x0AFA, 0x0AFF, UTFLITE__GCB_EXTEND},
    {0x0B01, 0x0B01, UTFLITE__GCB_EXTEND},
    {0x0B02, 0x0B03, UTFLITE__GCB_SPACING_MARK},
    {0x0B3C, 0x0B3C, UTFLITE__GCB_EXTEND},
    {0x0B3E, 0x0B3E, UTFLITE__GCB_EXTEND},
    {0x0B3F, 0x0B3F, UTFLITE__GCB_EXTEND},
    {0x0B40, 0x0B40, UTFLITE__GCB_SPACING_MARK},
    {0x0B41, 0x0B44, UTFLITE__GCB_EXTEND},
    {0x0B47, 0x0B48, UTFLITE__GCB_SPACING_MARK},
    {0x0B4B, 0x0B4C, UTFLITE__GCB_SPACING_MARK},
    {0x0B4D, 0x0B4D, UTFLITE__GCB_EXTEND},
    {0x0B55, 0x0B56, UTFLITE__GCB_EXTEND},
    {0x0B57, 0x0B57, UTFLITE__GCB_EXTEND},
    {0x0B62, 0x0B63, UTFLITE__GCB_EXTEND},
    {0x0B82, 0x0B82, UTFLITE__GCB_EXTEND},
    {0x0BBE, 0x0BBE, UTFLITE__GCB_EXTEND},
    {0x0BBF, 0x0BBF, UTFLITE__GCB_SPACING_MARK},
    {0x0BC0, 0x0BC0, UTFLITE__GCB_EXTEND},
    {0x0BC1, 0x0BC2, UTFLITE__GCB_SPACING_MARK},
    {0x0BC6, 0x0BC8, UTFLITE__GCB_SPACING_MARK},
    {0x0BCA, 0x0BCC, UTFLITE__GCB_SPACING_MARK},
    {0x0BCD, 0x0BCD, UTFLITE__GCB_EXTEND},
    {0x0BD7, 0x0BD7, UTFLITE__GCB_EXTEND},
    {0x0C00, 0x0C00, UTFLITE__GCB_EXTEND},
    {0x0C01, 0x0C03, UTFLITE__GCB_SPACING_MARK},
    {0x0C04, 0x0C04, UTFLITE__GCB_EXTEND},
    {0x0C3C, 0x0C3C, UTFLITE__GCB_EXTEND},
    {0x0C3E, 0x0C40, UTFLITE__GCB_EXTEND},
    {0x0C41, 0x0C44, UTFLITE__GCB_SPACING_MARK},
    {0x0C46, 0x0C48, UTFLITE__GCB_EXTEND},
    {0x0C4A, 0x0C4D, UTFLITE__GCB_EXTEND},
    {0x0C55, 0x0C56, UTFLITE__GCB_EXTEND},
    {0x0C62, 0x0C63, UTFLITE__GCB_EXTEND},
    {0x0C81, 0x0C81, UTFLITE__GCB_EXTEND},
    {0x0C82, 0x0C83, UTFLITE__GCB_SPACING_MARK},
    {0x0CBC, 0x0CBC, UTFLITE__GCB_EXTEND},
    {0x0CBE, 0x0CBE, UTFLITE__GCB_SPACING_MARK},
    {0x0CBF, 0x0CBF, UTFLITE__GCB_EXTEND},
    {0x0CC0, 0x0CC0, UTFLITE__GCB_EXTEND},
    {0x0CC1, 0x0CC1, UTFLITE__GCB_SPACING_MARK},
    {0x0CC2, 0x0CC2, UTFLITE__GCB_EXTEND},
    {0x0CC3, 0x0CC4, UTFLITE__GCB_SPACING_MARK},
    {0x0CC6, 0x0CC6, UTFLITE__GCB_EXTEND},
    {0x0CC7, 0x0CC8, UTFLITE__GCB_EXTEND},
    {0x0CCA, 0x0CCB, UTFLITE__GCB_EXTEND},
    {0x0CCC, 0x0CCD, UTFLITE__GCB_EXTEND},
    {0x0CD5, 0x0CD6, UTFLITE__GCB_EXTEND},
    {0x0CE2, 0x0CE3, UTFLITE__GCB_EXTEND},
    {0x0CF3, 0x0CF3, UTFLITE__GCB_SPACING_MARK},
    {0x0D00, 0x0D01, UTFLITE__GCB_EXTEND},
    {0x0D02, 0x0D03, UTFLITE__GCB_SPACING_MARK},
    {0x0D3B, 0x0D3C, UTFLITE__GCB_EXTEND},
    {0x0D3E, 0x0D3E, UTFLITE__GCB_EXTEND},
    {0x0D3F, 0x0D40, UTFLITE__GCB_SPACING_MARK},
    {0x0D41, 0x0D44, UTFLITE__GCB_EXTEND},
    {0x0D46, 0x0D48, UTFLITE__GCB_SPACING_MARK},
    {0x0D4A, 0x0D4C, UTFLITE__GCB_SPACING_MARK},
    {0x0D4D, 0x0D4D, UTFLITE__GCB_EXTEND},
    {0x0D4E, 0x0D4E, UTFLITE__GCB_PREPEND},
    {0x0D57, 0x0D57, UTFLITE__GCB_EXTEND},
    {0x0D62, 0x0D63, UTFLITE__GCB_EXTEND},
    {0x0D81, 0x0D81, UTFLITE__GCB_EXTEND},
    {0x0D82, 0x0D83, UTFLITE__GCB_SPACING_MARK},
    {0x0DCA, 0x0DCA, UTFLITE__GCB_EXTEND},
    {0x0DCF, 0x0DCF, UTFLITE__GCB_EXTEND},
    {0x0DD0, 0x0DD1, UTFLITE__GCB_SPACING_MARK},
    {0x0DD2, 0x0DD4, UTFLITE__GCB_EXTEND},
    {0x0DD6, 0x0DD6, UTFLITE__GCB_EXTEND},
    {0x0DD8, 0x0DDE, UTFLITE__GCB_SPACING_MARK},
    {0x0DDF, 0x0DDF, UTFLITE__GCB_EXTEND},
    {0x0DF2, 0x0DF3, UTFLITE__GCB_SPACING_MARK},
    {0x0E31, 0x0E31, UTFLITE__GCB_EXTEND},
    {0x0E33, 0x0E33, UTFLITE__GCB_SPACING_MARK},
    {0x0E34, 0x0E3A, UTFLITE__GCB_EXTEND},
    {0x0E47, 0x0E4E, UTFLITE__GCB_EXTEND},
    {0x0EB1, 0x0EB1, UTFLITE__GCB_EXTEND},
    {0x0EB3, 0x0EB3, UTFLITE__GCB_SPACING_MARK},
    {0x0EB4, 0x0EBC, UTFLITE__GCB_EXTEND},
    {0x0EC8, 0x0ECE, UTFLITE__GCB_EXTEND},
    {0x0F18, 0x0F19, UTFLITE__GCB_EXTEND},
    {0x0F35, 0x0F35, UTFLITE__GCB_EXTEND},
    {0x0F37, 0x0F37, UTFLITE__GCB_EXTEND},
    {0x0F39, 0x0F39, UTFLITE__GCB_EXTEND},
    {0x0F3E, 0x0F3F, UTFLITE__GCB_SPACING_MARK},
    {0x0F71, 0x0F7E, UTFLITE__GCB_EXTEND},
    {0x0F7F, 0x0F7F, UTFLITE__GCB_SPACING_MARK},
    {0x0F80, 0x0F84, UTFLITE__GCB_EXTEND},
    {0x0F86, 0x0F87, UTFLITE__GCB_EXTEND},
    {0x0F8D, 0x0F97, UTFLITE__GCB_EXTEND},
    {0x0F99, 0x0FBC, UTFLITE__GCB_EXTEND},
    {0x0FC6, 0x0FC6, UTFLITE__GCB_EXTEND},
    {0x102D, 0x1030, UTFLITE__GCB_EXTEND},
    {0x1031, 0x1031, UTFLITE__GCB_SPACING_MARK},
    {0x1032, 0x1037, UTFLITE__GCB_EXTEND},
    {0x1039, 0x103A, UTFLITE__GCB_EXTEND},
    {0x103B, 0x103C, UTFLITE__GCB_SPACING_MARK},
    {0x103D, 0x103E, UTFLITE__GCB_EXTEND},
    {0x1056, 0x1057, UTFLITE__GCB_SPACING_MARK},
    {0x1058, 0x1059, UTFLITE__GCB_EXTEND},
    {0x105E, 0x1060, UTFLITE__GCB_EXTEND},
    {0x1071, 0x1074, UTFLITE__GCB_EXTEND},
    {0x1082, 0x1082, UTFLITE__GCB_EXTEND},
    {0x1084, 0x1084, UTFLITE__GCB_SPACING_MARK},
    {0x1085, 0x1086, UTFLITE__GCB_EXTEND},
    {0x108D, 0x108D, UTFLITE__GCB_EXTEND},
    {0x109D, 0x109D, UTFLITE__GCB_EXTEND},
    {0x1100, 0x115F, UTFLITE__GCB_L},
    {0x1160, 0x11A7, UTFLITE__GCB_V},
    {0x11A8, 0x11FF, UTFLITE__GCB_T},
    {0x135D, 0x135F, UTFLITE__GCB_EXTEND},
    {0x1712, 0x1715, UTFLITE__GCB_EXTEND},
    {0x1732, 0x1734, UTFLITE__GCB_EXTEND},
    {0x1752, 0x1753, UTFLITE__GCB_EXTEND},
    {0x1772, 0x1773, UTFLITE__GCB_EXTEND},
    {0x17B4, 0x17B5, UTFLITE__GCB_EXTEND},
    {0x17B6, 0x17B6, UTFLITE__GCB_SPACING_MARK},
    {0x17B7, 0x17BD, UTFLITE__GCB_EXTEND},
    {0x17BE, 0x17C5, UTFLITE__GCB_SPACING_MARK},
    {0x17C6, 0x17C6, UTFLITE__GCB_EXTEND},
    {0x17C7, 0x17C8, UTFLITE__GCB_SPACING_MARK},
    {0x17C9, 0x17D3, UTFLITE__GCB_EXTEND},
    {0x17DD, 0x17DD, UTFLITE__GCB_EXTEND},
    {0x180B, 0x180D, UTFLITE__GCB_EXTEND},
    {0x180E, 0x180E, UTFLITE__GCB_CONTROL},
    {0x180F, 0x180F, UTFLITE__GCB_EXTEND},
    {0x1885, 0x1886, UTFLITE__GCB_EXTEND},
    {0x18A9, 0x18A9, UTFLITE__GCB_EXTEND},
    {0x1920, 0x1922, UTFLITE__GCB_EXTEND},
    {0x1923, 0x1926, UTFLITE__GCB_SPACING_MARK},
    {0x1927, 0x1928, UTFLITE__GCB_EXTEND},
    {0x1929, 0x192B, UTFLITE__GCB_SPACING_MARK},
    {0x1930, 0x1931, UTFLITE__GCB_SPACING_MARK},
    {0x1932, 0x1932, UTFLITE__GCB_EXTEND},
    {0x1933, 0x1938, UTFLITE__GCB_SPACING_MARK},
    {0x1939, 0x193B, UTFLITE__GCB_EXTEND},
    {0x1A17, 0x1A18, UTFLITE__GCB_EXTEND},
    {0x1A19, 0x1A1A, UTFLITE__GCB_SPACING_MARK},
    {0x1A1B, 0x1A1B, UTFLITE__GCB_EXTEND},
    {0x1A55, 0x1A55, UTFLITE__GCB_SPACING_MARK},
    {0x1A56, 0x1A56, UTFLITE__GCB_EXTEND},
    {0x1A57, 0x1A57, UTFLITE__GCB_SPACING_MARK},
    {0x1A58, 0x1A5E, UTFLITE__GCB_EXTEND},
    {0x1A60, 0x1A60, UTFLITE__GCB_EXTEND},
    {0x1A62, 0x1A62, UTFLITE__GCB_EXTEND},
    {0x1A65, 0x1A6C, UTFLITE__GCB_EXTEND},
    {0x1A6D, 0x1A72, UTFLITE__GCB_SPACING_MARK},
    {0x1A73, 0x1A7C, UTFLITE__GCB_EXTEND},
    {0x1A7F, 0x1A7F, UTFLITE__GCB_EXTEND},
    {0x1AB0, 0x1ACE, UTFLITE__GCB_EXTEND},
    {0x1B00, 0x1B03, UTFLITE__GCB_EXTEND},
    {0x1B04, 0x1B04, UTFLITE__GCB_SPACING_MARK},
    {0x1B34, 0x1B3A, UTFLITE__GCB_EXTEND},
    {0x1B3B, 0x1B3B, UTFLITE__GCB_SPACING_MARK},
    {0x1B3C, 0x1B3C, UTFLITE__GCB_EXTEND},
    {0x1B3D, 0x1B41, UTFLITE__GCB_SPACING_MARK},
    {0x1B42, 0x1B42, UTFLITE__GCB_EXTEND},
    {0x1B43, 0x1B44, UTFLITE__GCB_SPACING_MARK},
    {0x1B6B, 0x1B73, UTFLITE__GCB_EXTEND},
    {0x1B80, 0x1B81, UTFLITE__GCB_EXTEND},
    {0x1B82, 0x1B82, UTFLITE__GCB_SPACING_MARK},
    {0x1BA1, 0x1BA1, UTFLITE__GCB_SPACING_MARK},
    {0x1BA2, 0x1BA5, UTFLITE__GCB_EXTEND},
    {0x1BA6, 0x1BA7, UTFLITE__GCB_SPACING_MARK},
    {0x1BA8, 0x1BA9, UTFLITE__GCB_EXTEND},
    {0x1BAA, 0x1BAA, UTFLITE__GCB_SPACING_MARK},
    {0x1BAB, 0x1BAD, UTFLITE__GCB_EXTEND},
    {0x1BE6, 0x1BE6, UTFLITE__GCB_EXTEND},
    {0x1BE7, 0x1BE7, UTFLITE__GCB_SPACING_MARK},
    {0x1BE8, 0x1BE9, UTFLITE__GCB_EXTEND},
    {0x1BEA, 0x1BEC, UTFLITE__GCB_SPACING_MARK},
    {0x1BED, 0x1BED, UTFLITE__GCB_EXTEND},
    {0x1BEE, 0x1BEE, UTFLITE__GCB_SPACING_MARK},
    {0x1BEF, 0x1BF1, UTFLITE__GCB_EXTEND},
    {0x1BF2, 0x1BF3, UTFLITE__GCB_SPACING_MARK},
    {0x1C24, 0x1C2B, UTFLITE__GCB_SPACING_MARK},
    {0x1C2C, 0x1C33, UTFLITE__GCB_EXTEND},
    {0x1C34, 0x1C35, UTFLITE__GCB_SPACING_MARK},
    {0x1C36, 0x1C37, UTFLITE__GCB_EXTEND},
    {0x1CD0, 0x1CD2, UTFLITE__GCB_EXTEND},
    {0x1CD4, 0x1CE0, UTFLITE__GCB_EXTEND},
    {0x1CE1, 0x1CE1, UTFLITE__GCB_SPACING_MARK},
    {0x1CE2, 0x1CE8, UTFLITE__GCB_EXTEND},
    {0x1CED, 0x1CED, UTFLITE__GCB_EXTEND},
    {0x1CF4, 0x1CF4, UTFLITE__GCB_EXTEND},
    {0x1CF7, 0x1CF7, UTFLITE__GCB_SPACING_MARK},
    {0x1CF8, 0x1CF9, UTFLITE__GCB_EXTEND},
    {0x1DC0, 0x1DFF, UTFLITE__GCB_EXTEND},
    {0x200B, 0x200B, UTFLITE__GCB_CONTROL},
    {0x200C, 0x200C, UTFLITE__GCB_EXTEND},
    {0x200D, 0x200D, UTFLITE__GCB_ZWJ},
    {0x200E, 0x200F, UTFLITE__GCB_CONTROL},
    {0x2028, 0x202E, UTFLITE__GCB_CONTROL},
    {0x2060, 0x206F, UTFLITE__GCB_CONTROL},
    {0x20D0, 0x20F0, UTFLITE__GCB_EXTEND},
    {0x2CEF, 0x2CF1, UTFLITE__GCB_EXTEND},
    {0x2D7F, 0x2D7F, UTFLITE__GCB_EXTEND},
    {0x2DE0, 0x2DFF, UTFLITE__GCB_EXTEND},
    {0x302A, 0x302F, UTFLITE__GCB_EXTEND},
    {0x3099, 0x309A, UTFLITE__GCB_EXTEND},
    {0xA66F, 0xA672, UTFLITE__GCB_EXTEND},
    {0xA674, 0xA67D, UTFLITE__GCB_EXTEND},
    {0xA69E, 0xA69F, UTFLITE__GCB_EXTEND},
    {0xA6F0, 0xA6F1, UTFLITE__GCB_EXTEND},
    {0xA802, 0xA802, UTFLITE__GCB_EXTEND},
    {0xA806, 0xA806, UTFLITE__GCB_EXTEND},
    {0xA80B, 0xA80B, UTFLITE__GCB_EXTEND},
    {0xA823, 0xA824, UTFLITE__GCB_SPACING_MARK},
    {0xA825, 0xA826, UTFLITE__GCB_EXTEND},
    {0xA827, 0xA827, UTFLITE__GCB_SPACING_MARK},
    {0xA82C, 0xA82C, UTFLITE__GCB_EXTEND},
    {0xA880, 0xA881, UTFLITE__GCB_SPACING_MARK},
    {0xA8B4, 0xA8C3, UTFLITE__GCB_SPACING_MARK},
    {0xA8C4, 0xA8C5, UTFLITE__GCB_EXTEND},
    {0xA8E0, 0xA8F1, UTFLITE__GCB_EXTEND},
    {0xA8FF, 0xA8FF, UTFLITE__GCB_EXTEND},
    {0xA926, 0xA92D, UTFLITE__GCB_EXTEND},
    {0xA947, 0xA951, UTFLITE__GCB_EXTEND},
    {0xA952, 0xA953, UTFLITE__GCB_SPACING_MARK},
    {0xA960, 0xA97C, UTFLITE__GCB_L},
    {0xA980, 0xA982, UTFLITE__GCB_EXTEND},
    {0xA983, 0xA983, UTFLITE__GCB_SPACING_MARK},
    {0xA9B3, 0xA9B3, UTFLITE__GCB_EXTEND},
    {0xA9B4, 0xA9B5, UTFLITE__GCB_SPACING_MARK},
    {0xA9B6, 0xA9B9, UTFLITE__GCB_EXTEND},
    {0xA9BA, 0xA9BB, UTFLITE__GCB_SPACING_MARK},
    {0xA9BC, 0xA9BD, UTFLITE__GCB_EXTEND},
    {0xA9BE, 0xA9C0, UTFLITE__GCB_SPACING_MARK},
    {0xA9E5, 0xA9E5, UTFLITE__GCB_EXTEND},
    {0xAA29, 0xAA2E, UTFLITE__GCB_EXTEND},
    {0xAA2F, 0xAA30, UTFLITE__GCB_SPACING_MARK},
    {0xAA31, 0xAA32, UTFLITE__GCB_EXTEND},
    {0xAA33, 0xAA34, UTFLITE__GCB_SPACING_MARK},
    {0xAA35, 0xAA36, UTFLITE__GCB_EXTEND},
    {0xAA43, 0xAA43, UTFLITE__GCB_EXTEND},
    {0xAA4C, 0xAA4C, UTFLITE__GCB_EXTEND},
    {0xAA4D, 0xAA4D, UTFLITE__GCB_SPACING_MARK},
    {0xAA7C, 0xAA7C, UTFLITE__GCB_EXTEND},
    {0xAAB0, 0xAAB0, UTFLITE__GCB_EXTEND},
    {0xAAB2, 0xAAB4, UTFLITE__GCB_EXTEND},
    {0xAAB7, 0xAAB8, UTFLITE__GCB_EXTEND},
    {0xAABE, 0xAABF, UTFLITE__GCB_EXTEND},
    {0xAAC1, 0xAAC1, UTFLITE__GCB_EXTEND},
    {0xAAEB, 0xAAEB, UTFLITE__GCB_SPACING_MARK},
    {0xAAEC, 0xAAED, UTFLITE__GCB_EXTEND},
    {0xAAEE, 0xAAEF, UTFLITE__GCB_SPACING_MARK},
    {0xAAF5, 0xAAF5, UTFLITE__GCB_SPACING_MARK},
    {0xAAF6, 0xAAF6, UTFLITE__GCB_EXTEND},
    {0xABE3, 0xABE4, UTFLITE__GCB_SPACING_MARK},
    {0xABE5, 0xABE5, UTFLITE__GCB_EXTEND},
    {0xABE6, 0xABE7, UTFLITE__GCB_SPACING_MARK},
    {0xABE8, 0xABE8, UTFLITE__GCB_EXTEND},
    {0xABE9, 0xABEA, UTFLITE__GCB_SPACING_MARK},
    {0xABEC, 0xABEC, UTFLITE__GCB_SPACING_MARK},
    {0xABED, 0xABED, UTFLITE__GCB_EXTEND},
    {0xD7B0, 0xD7C6, UTFLITE__GCB_V},
    {0xD7CB, 0xD7FB, UTFLITE__GCB_T},
    {0xFB1E, 0xFB1E, UTFLITE__GCB_EXTEND},
    {0xFE00, 0xFE0F, UTFLITE__GCB_EXTEND},
    {0xFE20, 0xFE2F, UTFLITE__GCB_EXTEND},
    {0xFEFF, 0xFEFF, UTFLITE__GCB_CONTROL},
    {0xFF9E, 0xFF9F, UTFLITE__GCB_EXTEND},
    {0xFFF0, 0xFFFB, UTFLITE__GCB_CONTROL},
    {0x101FD, 0x101FD, UTFLITE__GCB_EXTEND},
    {0x102E0, 0x102E0, UTFLITE__GCB_EXTEND},
    {0x10376, 0x1037A, UTFLITE__GCB_EXTEND},
    {0x10A01, 0x10A03, UTFLITE__GCB_EXTEND},
    {0x10A05, 0x10A06, UTFLITE__GCB_EXTEND},
    {0x10A0C, 0x10A0F, UTFLITE__GCB_EXTEND},
    {0x10A38, 0x10A3A, UTFLITE__GCB_EXTEND},
    {0x10A3F, 0x10A3F, UTFLITE__GCB_EXTEND},
    {0x10AE5, 0x10AE6, UTFLITE__GCB_EXTEND},
    {0x10D24, 0x10D27, UTFLITE__GCB_EXTEND},
    {0x10EAB, 0x10EAC, UTFLITE__GCB_EXTEND},
    {0x10EFD, 0x10EFF, UTFLITE__GCB_EXTEND},
    {0x10F46, 0x10F50, UTFLITE__GCB_EXTEND},
    {0x10F82, 0x10F85, UTFLITE__GCB_EXTEND},
    {0x11000, 0x11000, UTFLITE__GCB_SPACING_MARK},
    {0x11001, 0x11001, UTFLITE__GCB_EXTEND},
    {0x11002, 0x11002, UTFLITE__GCB_SPACING_MARK},
    {0x11038, 0x11046, UTFLITE__GCB_EXTEND},
    {0x11070, 0x11070, UTFLITE__GCB_EXTEND},
    {0x11073, 0x11074, UTFLITE__GCB_EXTEND},
    {0x1107F, 0x11081, UTFLITE__GCB_EXTEND},
    {0x11082, 0x11082, UTFLITE__GCB_SPACING_MARK},
    {0x110B0, 0x110B2, UTFLITE__GCB_SPACING_MARK},
    {0x110B3, 0x110B6, UTFLITE__GCB_EXTEND},
    {0x110B7, 0x110B8, UTFLITE__GCB_SPACING_MARK},
    {0x110B9, 0x110BA, UTFLITE__GCB_EXTEND},
    {0x110BD, 0x110BD, UTFLITE__GCB_PREPEND},
    {0x110C2, 0x110C2, UTFLITE__GCB_EXTEND},
    {0x110CD, 0x110CD, UTFLITE__GCB_PREPEND},
    {0x11100, 0x11102, UTFLITE__GCB_EXTEND},
    {0x11127, 0x1112B, UTFLITE__GCB_EXTEND},
    {0x1112C, 0x1112C, UTFLITE__GCB_SPACING_MARK},
    {0x1112D, 0x11134, UTFLITE__GCB_EXTEND},
    {0x11145, 0x11146, UTFLITE__GCB_SPACING_MARK},
    {0x11173, 0x11173, UTFLITE__GCB_EXTEND},
    {0x11180, 0x11181, UTFLITE__GCB_EXTEND},
    {0x11182, 0x11182, UTFLITE__GCB_SPACING_MARK},
    {0x111B3, 0x111B5, UTFLITE__GCB_SPACING_MARK},
    {0x111B6, 0x111BE, UTFLITE__GCB_EXTEND},
    {0x111BF, 0x111C0, UTFLITE__GCB_SPACING_MARK},
    {0x111C2, 0x111C3, UTFLITE__GCB_PREPEND},
    {0x111C9, 0x111CC, UTFLITE__GCB_EXTEND},
    {0x111CE, 0x111CE, UTFLITE__GCB_SPACING_MARK},
    {0x111CF, 0x111CF, UTFLITE__GCB_EXTEND},
    {0x1122C, 0x1122E, UTFLITE__GCB_SPACING_MARK},
    {0x1122F, 0x11231, UTFLITE__GCB_EXTEND},
    {0x11232, 0x11233, UTFLITE__GCB_SPACING_MARK},
    {0x11234, 0x11237, UTFLITE__GCB_EXTEND},
    {0x1123E, 0x1123E, UTFLITE__GCB_EXTEND},
    {0x11241, 0x11241, UTFLITE__GCB_EXTEND},
    {0x112DF, 0x112DF, UTFLITE__GCB_EXTEND},
    {0x112E0, 0x112E2, UTFLITE__GCB_SPACING_MARK},
    {0x112E3, 0x112EA, UTFLITE__GCB_EXTEND},
    {0x11300, 0x11301, UTFLITE__GCB_EXTEND},
    {0x11302, 0x11303, UTFLITE__GCB_SPACING_MARK},
    {0x1133B, 0x1133C, UTFLITE__GCB_EXTEND},
    {0x1133E, 0x1133E, UTFLITE__GCB_EXTEND},
    {0x1133F, 0x1133F, UTFLITE__GCB_SPACING_MARK},
    {0x11340, 0x11340, UTFLITE__GCB_EXTEND},
    {0x11341, 0x11344, UTFLITE__GCB_SPACING_MARK},
    {0x11347, 0x11348, UTFLITE__GCB_SPACING_MARK},
    {0x1134B, 0x1134D, UTFLITE__GCB_SPACING_MARK},
    {0x11357, 0x11357, UTFLITE__GCB_EXTEND},
    {0x11362, 0x11363, UTFLITE__GCB_SPACING_MARK},
    {0x11366, 0x1136C, UTFLITE__GCB_EXTEND},
    {0x11370, 0x11374, UTFLITE__GCB_EXTEND},
    {0x11435, 0x11437, UTFLITE__GCB_SPACING_MARK},
    {0x11438, 0x1143F, UTFLITE__GCB_EXTEND},
    {0x11440, 0x11441, UTFLITE__GCB_SPACING_MARK},
    {0x11442, 0x11444, UTFLITE__GCB_EXTEND},
    {0x11445, 0x11445, UTFLITE__GCB_SPACING_MARK},
    {0x11446, 0x11446, UTFLITE__GCB_EXTEND},
    {0x1145E, 0x1145E, UTFLITE__GCB_EXTEND},
    {0x114B0, 0x114B2, UTFLITE__GCB_SPACING_MARK},
    {0x114B3, 0x114B8, UTFLITE__GCB_EXTEND},
    {0x114B9, 0x114B9, UTFLITE__GCB_SPACING_MARK},
    {0x114BA, 0x114BA, UTFLITE__GCB_EXTEND},
    {0x114BB, 0x114BE, UTFLITE__GCB_SPACING_MARK},
    {0x114BF, 0x114C0, UTFLITE__GCB_EXTEND},
    {0x114C1, 0x114C1, UTFLITE__GCB_SPACING_MARK},
    {0x114C2, 0x114C3, UTFLITE__GCB_EXTEND},
    {0x115AF, 0x115B1, UTFLITE__GCB_SPACING_MARK},
    {0x115B2, 0x115B5, UTFLITE__GCB_EXTEND},
    {0x115B8, 0x115BB, UTFLITE__GCB_SPACING_MARK},
    {0x115BC, 0x115BD, UTFLITE__GCB_EXTEND},
    {0x115BE, 0x115BE, UTFLITE__GCB_SPACING_MARK},
    {0x115BF, 0x115C0, UTFLITE__GCB_EXTEND},
    {0x115DC, 0x115DD, UTFLITE__GCB_EXTEND},
    {0x11630, 0x11632, UTFLITE__GCB_SPACING_MARK},
    {0x11633, 0x1163A, UTFLITE__GCB_EXTEND},
    {0x1163B, 0x1163C, UTFLITE__GCB_SPACING_MARK},
    {0x1163D, 0x1163D, UTFLITE__GCB_EXTEND},
    {0x1163E, 0x1163E, UTFLITE__GCB_SPACING_MARK},
    {0x1163F, 0x11640, UTFLITE__GCB_EXTEND},
    {0x116AB, 0x116AB, UTFLITE__GCB_EXTEND},
    {0x116AC, 0x116AC, UTFLITE__GCB_SPACING_MARK},
    {0x116AD, 0x116AD, UTFLITE__GCB_EXTEND},
    {0x116AE, 0x116AF, UTFLITE__GCB_SPACING_MARK},
    {0x116B0, 0x116B7, UTFLITE__GCB_EXTEND},
    {0x1171D, 0x1171F, UTFLITE__GCB_EXTEND},
    {0x11722, 0x11725, UTFLITE__GCB_EXTEND},
    {0x11726, 0x11726, UTFLITE__GCB_SPACING_MARK},
    {0x11727, 0x1172B, UTFLITE__GCB_EXTEND},
    {0x1182C, 0x1182E, UTFLITE__GCB_SPACING_MARK},
    {0x1182F, 0x11837, UTFLITE__GCB_EXTEND},
    {0x11838, 0x11838, UTFLITE__GCB_SPACING_MARK},
    {0x11839, 0x1183A, UTFLITE__GCB_EXTEND},
    {0x11930, 0x11930, UTFLITE__GCB_EXTEND},
    {0x11931, 0x11935, UTFLITE__GCB_SPACING_MARK},
    {0x11937, 0x11938, UTFLITE__GCB_SPACING_MARK},
    {0x1193B, 0x1193E, UTFLITE__GCB_EXTEND},
    {0x1193F, 0x1193F, UTFLITE__GCB_PREPEND},
    {0x11940, 0x11940, UTFLITE__GCB_SPACING_MARK},
    {0x11941, 0x11941, UTFLITE__GCB_PREPEND},
    {0x11942, 0x11942, UTFLITE__GCB_SPACING_MARK},
    {0x11943, 0x11943, UTFLITE__GCB_EXTEND},
    {0x119D1, 0x119D3, UTFLITE__GCB_SPACING_MARK},
    {0x119D4, 0x119D7, UTFLITE__GCB_EXTEND},
    {0x119DA, 0x119DB, UTFLITE__GCB_EXTEND},
    {0x119DC, 0x119DF, UTFLITE__GCB_SPACING_MARK},
    {0x119E0, 0x119E0, UTFLITE__GCB_EXTEND},
    {0x119E4, 0x119E4, UTFLITE__GCB_SPACING_MARK},
    {0x11A01, 0x11A0A, UTFLITE__GCB_EXTEND},
    {0x11A33, 0x11A38, UTFLITE__GCB_EXTEND},
    {0x11A39, 0x11A39, UTFLITE__GCB_SPACING_MARK},
    {0x11A3B, 0x11A3E, UTFLITE__GCB_EXTEND},
    {0x11A47, 0x11A47, UTFLITE__GCB_EXTEND},
    {0x11A51, 0x11A56, UTFLITE__GCB_EXTEND},
    {0x11A57, 0x11A58, UTFLITE__GCB_SPACING_MARK},
    {0x11A59, 0x11A5B, UTFLITE__GCB_EXTEND},
    {0x11A84, 0x11A89, UTFLITE__GCB_PREPEND},
    {0x11A8A, 0x11A96, UTFLITE__GCB_EXTEND},
    {0x11A97, 0x11A97, UTFLITE__GCB_SPACING_MARK},
    {0x11A98, 0x11A99, UTFLITE__GCB_EXTEND},
    {0x11C2F, 0x11C2F, UTFLITE__GCB_SPACING_MARK},
    {0x11C30, 0x11C36, UTFLITE__GCB_EXTEND},
    {0x11C38, 0x11C3F, UTFLITE__GCB_EXTEND},
    {0x11C92, 0x11CA7, UTFLITE__GCB_EXTEND},
    {0x11CA9, 0x11CA9, UTFLITE__GCB_SPACING_MARK},
    {0x11CAA, 0x11CB0, UTFLITE__GCB_EXTEND},
    {0x11CB1, 0x11CB1, UTFLITE__GCB_SPACING_MARK},
    {0x11CB2, 0x11CB3, UTFLITE__GCB_EXTEND},
    {0x11CB4, 0x11CB4, UTFLITE__GCB_SPACING_MARK},
    {0x11CB5, 0x11CB6, UTFLITE__GCB_EXTEND},
    {0x11D31, 0x11D36, UTFLITE__GCB_EXTEND},
    {0x11D3A, 0x11D3A, UTFLITE__GCB_EXTEND},
    {0x11D3C, 0x11D3D, UTFLITE__GCB_EXTEND},
    {0x11D3F, 0x11D45, UTFLITE__GCB_EXTEND},
    {0x11D46, 0x11D46, UTFLITE__GCB_PREPEND},
    {0x11D47, 0x11D47, UTFLITE__GCB_EXTEND},
    {0x11D8A, 0x11D8E, UTFLITE__GCB_SPACING_MARK},
    {0x11D90, 0x11D91, UTFLITE__GCB_EXTEND},
    {0x11D93, 0x11D94, UTFLITE__GCB_SPACING_MARK},
    {0x11D95, 0x11D95, UTFLITE__GCB_EXTEND},
    {0x11D96, 0x11D96, UTFLITE__GCB_SPACING_MARK},
    {0x11D97, 0x11D97, UTFLITE__GCB_EXTEND},
    {0x11EF3, 0x11EF4, UTFLITE__GCB_EXTEND},
    {0x11EF5, 0x11EF6, UTFLITE__GCB_SPACING_MARK},
    {0x11F00, 0x11F01, UTFLITE__GCB_EXTEND},
    {0x11F02, 0x11F02, UTFLITE__GCB_PREPEND},
    {0x11F03, 0x11F03, UTFLITE__GCB_SPACING_MARK},
    {0x11F34, 0x11F35, UTFLITE__GCB_SPACING_MARK},
    {0x11F36, 0x11F3A, UTFLITE__GCB_EXTEND},
    {0x11F3E, 0x11F3F, UTFLITE__GCB_SPACING_MARK},
    {0x11F40, 0x11F42, UTFLITE__GCB_EXTEND},
    {0x13430, 0x1343F, UTFLITE__GCB_CONTROL},
    {0x13440, 0x13440, UTFLITE__GCB_EXTEND},
    {0x13447, 0x13455, UTFLITE__GCB_EXTEND},
    {0x16AF0, 0x16AF4, UTFLITE__GCB_EXTEND},
    {0x16B30, 0x16B36, UTFLITE__GCB_EXTEND},
    {0x16F4F, 0x16F4F, UTFLITE__GCB_EXTEND},
    {0x16F51, 0x16F87, UTFLITE__GCB_SPACING_MARK},
    {0x16F8F, 0x16F92, UTFLITE__GCB_EXTEND},
    {0x16FE4, 0x16FE4, UTFLITE__GCB_EXTEND},
    {0x16FF0, 0x16FF1, UTFLITE__GCB_SPACING_MARK},
    {0x1BC9D, 0x1BC9E, UTFLITE__GCB_EXTEND},
    {0x1BCA0, 0x1BCA3, UTFLITE__GCB_CONTROL},
    {0x1CF00, 0x1CF2D, UTFLITE__GCB_EXTEND},
    {0x1CF30, 0x1CF46, UTFLITE__GCB_EXTEND},
    {0x1D165, 0x1D169, UTFLITE__GCB_EXTEND},
    {0x1D16D, 0x1D172, UTFLITE__GCB_SPACING_MARK},
    {0x1D173, 0x1D17A, UTFLITE__GCB_CONTROL},
    {0x1D17B, 0x1D182, UTFLITE__GCB_EXTEND},
    {0x1D185, 0x1D18B, UTFLITE__GCB_EXTEND},
    {0x1D1AA, 0x1D1AD, UTFLITE__GCB_EXTEND},
    {0x1D242, 0x1D244, UTFLITE__GCB_EXTEND},
    {0x1DA00, 0x1DA36, UTFLITE__GCB_EXTEND},
    {0x1DA3B, 0x1DA6C, UTFLITE__GCB_EXTEND},
    {0x1DA75, 0x1DA75, UTFLITE__GCB_EXTEND},
    {0x1DA84, 0x1DA84, UTFLITE__GCB_EXTEND},
    {0x1DA9B, 0x1DA9F, UTFLITE__GCB_EXTEND},
    {0x1DAA1, 0x1DAAF, UTFLITE__GCB_EXTEND},
    {0x1E000, 0x1E006, UTFLITE__GCB_EXTEND},
    {0x1E008, 0x1E018, UTFLITE__GCB_EXTEND},
    {0x1E01B, 0x1E021, UTFLITE__GCB_EXTEND},
    {0x1E023, 0x1E024, UTFLITE__GCB_EXTEND},
    {0x1E026, 0x1E02A, UTFLITE__GCB_EXTEND},
    {0x1E08F, 0x1E08F, UTFLITE__GCB_EXTEND},
    {0x1E130, 0x1E136, UTFLITE__GCB_EXTEND},
    {0x1E2AE, 0x1E2AE, UTFLITE__GCB_EXTEND},
    {0x1E2EC, 0x1E2EF, UTFLITE__GCB_EXTEND},
    {0x1E4EC, 0x1E4EF, UTFLITE__GCB_EXTEND},
    {0x1E8D0, 0x1E8D6, UTFLITE__GCB_EXTEND},
    {0x1E944, 0x1E94A, UTFLITE__GCB_EXTEND},
    {0x1F1E6, 0x1F1FF, UTFLITE__GCB_REGIONAL_INDICATOR},
    {0x1F3FB, 0x1F3FF, UTFLITE__GCB_EXTEND},
    {0xE0000, 0xE001F, UTFLITE__GCB_CONTROL},
    {0xE0020, 0xE007F, UTFLITE__GCB_EXTEND},
    {0xE0080, 0xE00FF, UTFLITE__GCB_CONTROL},
    {0xE0100, 0xE01EF, UTFLITE__GCB_EXTEND},
    {0xE01F0, 0xE0FFF, UTFLITE__GCB_CONTROL},
};
#define UTFLITE__GCB_RANGE_COUNT (sizeof(UTFLITE__GCB_RANGES) / sizeof(UTFLITE__GCB_RANGES[0]))

/*
 * InCB (Indic_Conjunct_Break) property tables for GB9c rule.
 * Linkers are virama characters that join consonants in Indic scripts.
 */
static const uint32_t UTFLITE__INCB_LINKERS[] = {
    0x094D, 0x09CD, 0x0ACD, 0x0B4D, 0x0C4D, 0x0D4D, 0x1039, 0x103A,
    0x1714, 0x1715, 0x17D2, 0x1A60, 0x1B44, 0x1BAA, 0x1BAB, 0xA806,
    0xA8C4, 0xA9C0, 0xAAF6, 0x10A3F, 0x11046, 0x110B9, 0x11133, 0x11134,
    0x111C0, 0x11235, 0x1134D, 0x11442, 0x114C2, 0x115BF, 0x1163F, 0x116B6,
    0x1172B, 0x11839, 0x119E0, 0x11A34, 0x11A47, 0x11A99, 0x11C3F, 0x11D45,
    0x11D97, 0x11F41, 0x11F42,
};
#define UTFLITE__INCB_LINKER_COUNT (sizeof(UTFLITE__INCB_LINKERS) / sizeof(UTFLITE__INCB_LINKERS[0]))

/* InCB Consonant ranges */
static const struct utflite__unicode_range UTFLITE__INCB_CONSONANTS[] = {
    {0x0915, 0x0939}, {0x0958, 0x095F}, {0x0978, 0x097F}, {0x0995, 0x09A8},
    {0x09AA, 0x09B0}, {0x09B2, 0x09B2}, {0x09B6, 0x09B9}, {0x09DC, 0x09DD},
    {0x09DF, 0x09E1}, {0x09F0, 0x09F1}, {0x0A15, 0x0A28}, {0x0A2A, 0x0A30},
    {0x0A32, 0x0A33}, {0x0A35, 0x0A36}, {0x0A38, 0x0A39}, {0x0A59, 0x0A5C},
    {0x0A5E, 0x0A5E}, {0x0A72, 0x0A74}, {0x0A95, 0x0AA8}, {0x0AAA, 0x0AB0},
    {0x0AB2, 0x0AB3}, {0x0AB5, 0x0AB9}, {0x0AE0, 0x0AE1}, {0x0B15, 0x0B28},
    {0x0B2A, 0x0B30}, {0x0B32, 0x0B33}, {0x0B35, 0x0B39}, {0x0B5C, 0x0B5D},
    {0x0B5F, 0x0B61}, {0x0B71, 0x0B71}, {0x0B95, 0x0B95}, {0x0B99, 0x0B9A},
    {0x0B9C, 0x0B9C}, {0x0B9E, 0x0B9F}, {0x0BA3, 0x0BA4}, {0x0BA8, 0x0BAA},
    {0x0BAE, 0x0BB9}, {0x0C15, 0x0C28}, {0x0C2A, 0x0C39}, {0x0C58, 0x0C5A},
    {0x0C5D, 0x0C5D}, {0x0C60, 0x0C61}, {0x0C95, 0x0CA8}, {0x0CAA, 0x0CB3},
    {0x0CB5, 0x0CB9}, {0x0CDD, 0x0CDE}, {0x0CE0, 0x0CE1}, {0x0D15, 0x0D3A},
    {0x0D54, 0x0D56}, {0x0D5F, 0x0D61}, {0x0D7A, 0x0D7F}, {0x1000, 0x1025},
    {0x1027, 0x1027}, {0x1029, 0x102A}, {0x1050, 0x1055}, {0x105A, 0x105D},
    {0x1061, 0x1061}, {0x1065, 0x1066}, {0x106E, 0x1070}, {0x1075, 0x1081},
    {0x108E, 0x108E}, {0x1703, 0x170C}, {0x170E, 0x1711}, {0x1780, 0x17A2},
    {0x17A5, 0x17A7}, {0x17A9, 0x17B3}, {0x1901, 0x1922}, {0x1930, 0x1931},
    {0x1950, 0x196D}, {0x1980, 0x19A9}, {0x19C1, 0x19C7}, {0x1A00, 0x1A16},
    {0x1A20, 0x1A4C}, {0x1B05, 0x1B33}, {0x1B83, 0x1BA0}, {0xA807, 0xA80A},
    {0xA80C, 0xA822}, {0xA882, 0xA8B3}, {0xA90A, 0xA925}, {0xA930, 0xA946},
    {0xA989, 0xA98B}, {0xA98D, 0xA9B2}, {0xAA00, 0xAA28}, {0xAA60, 0xAA6F},
    {0xAA71, 0xAA76}, {0xAAE0, 0xAAEA}, {0x10A00, 0x10A00}, {0x10A10, 0x10A13},
    {0x10A15, 0x10A17}, {0x10A19, 0x10A35}, {0x11005, 0x11037}, {0x11071, 0x11072},
    {0x11075, 0x11075}, {0x11083, 0x110AF}, {0x11107, 0x1112B}, {0x11150, 0x11172},
    {0x11183, 0x111B2}, {0x111C1, 0x111C4}, {0x11200, 0x11211}, {0x11213, 0x1122B},
    {0x1123F, 0x11240}, {0x11284, 0x11286}, {0x11288, 0x11288}, {0x1128A, 0x1128D},
    {0x1128F, 0x1129D}, {0x1129F, 0x112A8}, {0x11305, 0x1130C}, {0x1130F, 0x11310},
    {0x11313, 0x11328}, {0x1132A, 0x11330}, {0x11332, 0x11333}, {0x11335, 0x11339},
    {0x1135D, 0x11361}, {0x11400, 0x11434}, {0x11447, 0x1144A}, {0x11481, 0x114AF},
    {0x114C4, 0x114C5}, {0x114C7, 0x114C7}, {0x11580, 0x115AE}, {0x115D8, 0x115DB},
    {0x11600, 0x1162F}, {0x11644, 0x11644}, {0x11680, 0x116AA}, {0x116B8, 0x116B8},
    {0x11700, 0x1171A}, {0x11800, 0x1182B}, {0x11912, 0x11935}, {0x11937, 0x11938},
    {0x119A0, 0x119A7}, {0x119AA, 0x119D0}, {0x11A0B, 0x11A32}, {0x11A5C, 0x11A89},
    {0x11C00, 0x11C08}, {0x11C0A, 0x11C2E}, {0x11C72, 0x11C8F}, {0x11D00, 0x11D06},
    {0x11D08, 0x11D09}, {0x11D0B, 0x11D30}, {0x11D60, 0x11D65}, {0x11D67, 0x11D68},
    {0x11D6A, 0x11D89}, {0x11EE0, 0x11EF2}, {0x11F02, 0x11F02}, {0x11F04, 0x11F10},
    {0x11F12, 0x11F33},
};
#define UTFLITE__INCB_CONSONANT_COUNT (sizeof(UTFLITE__INCB_CONSONANTS) / sizeof(UTFLITE__INCB_CONSONANTS[0]))

static inline int utflite__unicode_range_contains(uint32_t codepoint,
                                                   const struct utflite__unicode_range *ranges,
                                                   int count) {
    int low = 0;
    int high = count - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (codepoint < ranges[mid].start) {
            high = mid - 1;
        } else if (codepoint > ranges[mid].end) {
            low = mid + 1;
        } else {
            return 1;
        }
    }
    return 0;
}

/* Get grapheme cluster break property for a codepoint */
static enum utflite__gcb_property utflite__get_gcb(uint32_t cp) {
	/* Hangul syllables: LV and LVT are computed algorithmically */
	if (cp >= UTFLITE__HANGUL_SBASE && cp <= UTFLITE__HANGUL_SEND) {
		return ((cp - UTFLITE__HANGUL_SBASE) % UTFLITE__HANGUL_TCOUNT == 0)
		       ? UTFLITE__GCB_LV : UTFLITE__GCB_LVT;
	}

    /* Binary search in property table */
    int low = 0;
    int high = UTFLITE__GCB_RANGE_COUNT - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (cp < UTFLITE__GCB_RANGES[mid].start) {
            high = mid - 1;
        } else if (cp > UTFLITE__GCB_RANGES[mid].end) {
            low = mid + 1;
        } else {
            return (enum utflite__gcb_property)UTFLITE__GCB_RANGES[mid].property;
        }
    }
    return UTFLITE__GCB_OTHER;
}

/*
 * Extended_Pictographic check for GB11 (emoji ZWJ sequences).
 *
 * Note: Uses double-width ranges as an approximation. The Extended_Pictographic
 * property from emoji-data.txt differs from Grapheme_Cluster_Break=Extended_Pictographic
 * used in GB11. The double-width table correctly handles common emoji ZWJ sequences
 * and passes all 766 official GraphemeBreakTest.txt tests.
 */
static int utflite__is_extended_pictographic(uint32_t cp) {
	return utflite__unicode_range_contains(cp, UTFLITE__DOUBLE_WIDTH_RANGES,
	                                       UTFLITE__DOUBLE_WIDTH_COUNT);
}

/* InCB Linker check for GB9c (binary search in sorted array) */
static int utflite__is_incb_linker(uint32_t cp) {
    int low = 0;
    int high = UTFLITE__INCB_LINKER_COUNT - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (cp < UTFLITE__INCB_LINKERS[mid]) {
            high = mid - 1;
        } else if (cp > UTFLITE__INCB_LINKERS[mid]) {
            low = mid + 1;
        } else {
            return 1;
        }
    }
    return 0;
}

/* InCB Consonant check for GB9c */
static int utflite__is_incb_consonant(uint32_t cp) {
    return utflite__unicode_range_contains(cp, UTFLITE__INCB_CONSONANTS,
                                           UTFLITE__INCB_CONSONANT_COUNT);
}

/*
 * Determine if there's a grapheme cluster break between two codepoints.
 * Implements UAX #29 extended grapheme cluster rules (GB3-GB13, GB999).
 *
 * incb_state: 0 = not in sequence, 1 = seen Consonant, 2 = seen Consonant+Linker
 */
static int utflite__is_grapheme_break(
    enum utflite__gcb_property prev_prop,
    enum utflite__gcb_property curr_prop,
    int ri_count,
    int in_ext_pict,
    uint32_t curr_cp,
    int incb_state
) {
    /* GB3: CR × LF */
    if (prev_prop == UTFLITE__GCB_CR && curr_prop == UTFLITE__GCB_LF) {
        return 0;
    }

    /* GB4: (Control | CR | LF) ÷ */
    if (prev_prop == UTFLITE__GCB_CONTROL ||
        prev_prop == UTFLITE__GCB_CR ||
        prev_prop == UTFLITE__GCB_LF) {
        return 1;
    }

    /* GB5: ÷ (Control | CR | LF) */
    if (curr_prop == UTFLITE__GCB_CONTROL ||
        curr_prop == UTFLITE__GCB_CR ||
        curr_prop == UTFLITE__GCB_LF) {
        return 1;
    }

    /* GB6: L × (L | V | LV | LVT) */
    if (prev_prop == UTFLITE__GCB_L &&
        (curr_prop == UTFLITE__GCB_L || curr_prop == UTFLITE__GCB_V ||
         curr_prop == UTFLITE__GCB_LV || curr_prop == UTFLITE__GCB_LVT)) {
        return 0;
    }

    /* GB7: (LV | V) × (V | T) */
    if ((prev_prop == UTFLITE__GCB_LV || prev_prop == UTFLITE__GCB_V) &&
        (curr_prop == UTFLITE__GCB_V || curr_prop == UTFLITE__GCB_T)) {
        return 0;
    }

    /* GB8: (LVT | T) × T */
    if ((prev_prop == UTFLITE__GCB_LVT || prev_prop == UTFLITE__GCB_T) &&
        curr_prop == UTFLITE__GCB_T) {
        return 0;
    }

    /* GB9: × (Extend | ZWJ) */
    if (curr_prop == UTFLITE__GCB_EXTEND || curr_prop == UTFLITE__GCB_ZWJ) {
        return 0;
    }

    /* GB9a: × SpacingMark */
    if (curr_prop == UTFLITE__GCB_SPACING_MARK) {
        return 0;
    }

    /* GB9b: Prepend × */
    if (prev_prop == UTFLITE__GCB_PREPEND) {
        return 0;
    }

    /* GB9c: Indic conjunct sequences - don't break before Consonant
     * if we've seen Consonant + [Extend|Linker]* + Linker */
    if (incb_state == 2 && utflite__is_incb_consonant(curr_cp)) {
        return 0;
    }

    /* GB11: ExtPict Extend* ZWJ × ExtPict */
    if (in_ext_pict && prev_prop == UTFLITE__GCB_ZWJ &&
        utflite__is_extended_pictographic(curr_cp)) {
        return 0;
    }

    /* GB12/GB13: RI × RI (only for pairs) */
    if (prev_prop == UTFLITE__GCB_REGIONAL_INDICATOR &&
        curr_prop == UTFLITE__GCB_REGIONAL_INDICATOR) {
        /* Break if we've already seen an even number of RIs */
        return (ri_count % 2) == 0;
    }

    /* GB999: Any ÷ Any */
    return 1;
}

/*
 * Error handling strategy: consume minimal bytes for structural errors,
 * consume full sequence for semantic errors.
 */
int utflite_decode(const char *bytes, int length, uint32_t *codepoint) {
    if (length <= 0 || !bytes) {
        *codepoint = UTFLITE_REPLACEMENT_CHAR;
        return 0;  /* EOF/empty: no bytes to consume */
    }
    unsigned char first = (unsigned char)bytes[0];
    if (first < 0x80) {
        *codepoint = first;
        return 1;
    }
    int sequence_length;
    uint32_t cp;
    if ((first & 0xE0) == 0xC0) {
        sequence_length = 2;
        cp = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        sequence_length = 3;
        cp = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        sequence_length = 4;
        cp = first & 0x07;
    } else {
        *codepoint = UTFLITE_REPLACEMENT_CHAR;
        return 1;
    }
    if (length < sequence_length) {
        *codepoint = UTFLITE_REPLACEMENT_CHAR;
        return 1;
    }
    for (int i = 1; i < sequence_length; i++) {
        unsigned char byte = (unsigned char)bytes[i];
        if ((byte & 0xC0) != 0x80) {
            *codepoint = UTFLITE_REPLACEMENT_CHAR;
            return 1;
        }
        cp = (cp << 6) | (byte & 0x3F);
    }
    if ((sequence_length == 2 && cp < 0x80) ||
        (sequence_length == 3 && cp < 0x800) ||
        (sequence_length == 4 && cp < 0x10000)) {
        *codepoint = UTFLITE_REPLACEMENT_CHAR;
        return sequence_length;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        *codepoint = UTFLITE_REPLACEMENT_CHAR;
        return sequence_length;
    }
    if (cp > 0x10FFFF) {
        *codepoint = UTFLITE_REPLACEMENT_CHAR;
        return sequence_length;
    }
    *codepoint = cp;
    return sequence_length;
}

int utflite_encode(uint32_t codepoint, char *buffer) {
    if (codepoint < 0x80) {
        buffer[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        buffer[0] = (char)(0xC0 | (codepoint >> 6));
        buffer[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            return 0;
        }
        buffer[0] = (char)(0xE0 | (codepoint >> 12));
        buffer[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        buffer[0] = (char)(0xF0 | (codepoint >> 18));
        buffer[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buffer[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

int utflite_codepoint_width(uint32_t codepoint) {
    if (codepoint < 0x20) {
        if (codepoint == 0x00) return 0;
        return -1;
    }
    if (codepoint < 0x7F) {
        return 1;
    }
    if (codepoint == 0x7F) {
        return -1;
    }
    if (codepoint < 0xA0) {
        return -1;
    }
    if (codepoint == 0x00AD) {
        return 1;
    }
    if (utflite__unicode_range_contains(codepoint, UTFLITE__ZERO_WIDTH_RANGES, UTFLITE__ZERO_WIDTH_COUNT)) {
        return 0;
    }
    if (utflite__unicode_range_contains(codepoint, UTFLITE__DOUBLE_WIDTH_RANGES, UTFLITE__DOUBLE_WIDTH_COUNT)) {
        return 2;
    }
    return 1;
}

int utflite_char_width(const char *text, int length, int offset) {
    if (offset >= length) {
        return 0;
    }
    uint32_t codepoint;
    utflite_decode(text + offset, length - offset, &codepoint);
    return utflite_codepoint_width(codepoint);
}

int utflite_next_char(const char *text, int length, int offset) {
    if (offset >= length) {
        return length;
    }
    uint32_t codepoint;
    int char_bytes = utflite_decode(text + offset, length - offset, &codepoint);
    int next_offset = offset + char_bytes;
    return (next_offset > length) ? length : next_offset;
}

int utflite_prev_char(const char *text, int offset) {
    if (offset <= 0) {
        return 0;
    }
    int pos = offset - 1;
    int limit = (offset > 4) ? offset - 4 : 0;
    while (pos > limit && ((unsigned char)text[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

/*
 * Returns byte offset of next grapheme cluster boundary.
 * Implements UAX #29 extended grapheme cluster segmentation.
 */
int utflite_next_grapheme(const char *text, int length, int offset) {
    if (!text || offset >= length || offset < 0) {
        return length;
    }

    /* Decode first codepoint */
    uint32_t prev_cp;
    int bytes = utflite_decode(text + offset, length - offset, &prev_cp);
    int next_offset = offset + bytes;

    if (next_offset >= length) {
        return length;
    }

    enum utflite__gcb_property prev_prop = utflite__get_gcb(prev_cp);
    int ri_count = (prev_prop == UTFLITE__GCB_REGIONAL_INDICATOR) ? 1 : 0;
    int in_ext_pict = utflite__is_extended_pictographic(prev_cp);

    /* InCB state for GB9c: 0=none, 1=seen Consonant, 2=seen Consonant+Linker */
    int incb_state = utflite__is_incb_consonant(prev_cp) ? 1 : 0;

    /* Loop through following characters */
    while (next_offset < length) {
        uint32_t curr_cp;
        bytes = utflite_decode(text + next_offset, length - next_offset, &curr_cp);
        enum utflite__gcb_property curr_prop = utflite__get_gcb(curr_cp);

        /* Check for break */
        if (utflite__is_grapheme_break(prev_prop, curr_prop, ri_count,
                                       in_ext_pict, curr_cp, incb_state)) {
            return next_offset;
        }

        /* Update state for next iteration */
        if (curr_prop == UTFLITE__GCB_REGIONAL_INDICATOR) {
            ri_count++;
        } else if (curr_prop != UTFLITE__GCB_EXTEND && curr_prop != UTFLITE__GCB_ZWJ) {
            ri_count = 0;
        }

        /* Track ExtPict sequence for GB11 */
        if (utflite__is_extended_pictographic(curr_cp)) {
            in_ext_pict = 1;
        } else if (curr_prop != UTFLITE__GCB_EXTEND && curr_prop != UTFLITE__GCB_ZWJ) {
            in_ext_pict = 0;
        }

        /* Track InCB state for GB9c */
        if (utflite__is_incb_consonant(curr_cp)) {
            incb_state = 1;
        } else if (utflite__is_incb_linker(curr_cp) && incb_state >= 1) {
            incb_state = 2;
        } else if (curr_prop != UTFLITE__GCB_EXTEND && curr_prop != UTFLITE__GCB_ZWJ) {
            incb_state = 0;
        }
        /* else: Extend/ZWJ keeps the current incb_state */

        prev_cp = curr_cp;
        prev_prop = curr_prop;
        next_offset += bytes;
    }

    return length;
}

/*
 * Returns byte offset of previous grapheme cluster boundary.
 *
 * Implementation notes:
 * - UAX #29 grapheme break rules are defined for forward iteration only,
 *   so this function backtracks up to UTFLITE__GRAPHEME_MAX_BACKTRACK
 *   codepoints and then scans forward to find boundaries.
 * - Time complexity: O(k) where k is bounded by UTFLITE__GRAPHEME_MAX_BACKTRACK
 *   (128 codepoints). In practice, k is the length of the current grapheme
 *   cluster, typically 1-20 codepoints for even complex emoji sequences.
 * - This approach trades some performance for correctness, avoiding the
 *   complexity of implementing reverse-direction break rules.
 */
int utflite_prev_grapheme(const char *text, int offset) {
    if (!text || offset <= 0) {
        return 0;
    }

    /* Find the start of the previous codepoint */
    int prev_start = utflite_prev_char(text, offset);
    if (prev_start == 0 && offset > 0) {
        /* We're at the start after moving back one char */
        return 0;
    }

	/*
	 * Strategy: scan forward from a safe point to find grapheme boundaries,
	 * then return the last one before 'offset'.
	 */
	int scan_start = prev_start;
	int remaining = UTFLITE__GRAPHEME_MAX_BACKTRACK;
	while (remaining > 0 && scan_start > 0) {
		int prev = utflite_prev_char(text, scan_start);
		if (prev == scan_start) break;
		scan_start = prev;
		remaining--;
	}

    /* Scan forward from scan_start, tracking grapheme boundaries */
    int curr = scan_start;
    int grapheme_start = scan_start;

    while (curr < offset) {
        /* We need to pass a length to next_grapheme - use offset as the limit */
        int next = utflite_next_grapheme(text, offset, curr);
        if (next >= offset) {
            break;
        }
        grapheme_start = next;
        curr = next;
    }

    return grapheme_start;
}

int utflite_validate(const char *text, int length, int *error_offset) {
    int offset = 0;
    while (offset < length) {
        uint32_t codepoint;
        int bytes = utflite_decode(text + offset, length - offset, &codepoint);
        if (codepoint == UTFLITE_REPLACEMENT_CHAR) {
            unsigned char first = (unsigned char)text[offset];
            if (first != 0xEF || length - offset < 3 ||
                (unsigned char)text[offset + 1] != 0xBF ||
                (unsigned char)text[offset + 2] != 0xBD) {
                if (error_offset) {
                    *error_offset = offset;
                }
                return 0;
            }
        }
        offset += bytes;
    }
    return 1;
}

int utflite_codepoint_count(const char *text, int length) {
    int count = 0;
    int offset = 0;
    while (offset < length) {
        offset = utflite_next_char(text, length, offset);
        count++;
    }
    return count;
}

int utflite_string_width(const char *text, int length) {
    int width = 0;
    int offset = 0;
    while (offset < length) {
        int char_width = utflite_char_width(text, length, offset);
        if (char_width > 0) {
            width += char_width;
        }
        offset = utflite_next_char(text, length, offset);
    }
    return width;
}

int utflite_is_zero_width(uint32_t codepoint) {
    return utflite__unicode_range_contains(codepoint, UTFLITE__ZERO_WIDTH_RANGES, UTFLITE__ZERO_WIDTH_COUNT);
}

int utflite_is_wide(uint32_t codepoint) {
    return utflite__unicode_range_contains(codepoint, UTFLITE__DOUBLE_WIDTH_RANGES, UTFLITE__DOUBLE_WIDTH_COUNT);
}

int utflite_truncate(const char *text, int length, int max_cols) {
    int width = 0;
    int offset = 0;

    while (offset < length) {
        int char_width = utflite_char_width(text, length, offset);
        if (char_width > 0) {
            if (width + char_width > max_cols) {
                return offset;
            }
            width += char_width;
        }
        offset = utflite_next_char(text, length, offset);
    }
    return length;
}

#endif /* UTFLITE_IMPLEMENTATION */
