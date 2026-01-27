// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Edward J Edmonds

// Package gstr provides grapheme-aware string operations for Go.
//
// Go's len() returns bytes, ranging gives runes (codepoints), but neither
// gives graphemes - what users perceive as "characters".
//
// Example:
//
//	len("👨‍👩‍👧")           // 18 (bytes)
//	utf8.RuneCountInString("👨‍👩‍👧") // 7 (codepoints)
//	gstr.Len("👨‍👩‍👧")       // 1 (graphemes)
package gstr

/*
#cgo CFLAGS: -I${SRCDIR}/../include
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gstr.h"

// Shim functions - wrap static inline functions with external linkage
// so cgo can call them

size_t gstr_shim_len(const char *s, size_t byte_len) {
    return gstrlen(s, byte_len);
}

size_t gstr_shim_nlen(const char *s, size_t byte_len, size_t max_graphemes) {
    return gstrnlen(s, byte_len, max_graphemes);
}

size_t gstr_shim_width(const char *s, size_t byte_len) {
    return gstrwidth(s, byte_len);
}

size_t gstr_shim_offset(const char *s, size_t byte_len, size_t grapheme_index) {
    return gstroff(s, byte_len, grapheme_index);
}

// gstrat returns pointer and grapheme length via out_len
const char *gstr_shim_at(const char *s, size_t byte_len, size_t grapheme_index,
                         size_t *out_len) {
    return gstrat(s, byte_len, grapheme_index, out_len);
}

int gstr_shim_cmp(const char *s1, size_t len1, const char *s2, size_t len2) {
    return gstrcmp(s1, len1, s2, len2);
}

int gstr_shim_ncmp(const char *s1, size_t len1, const char *s2, size_t len2, size_t n) {
    return gstrncmp(s1, len1, s2, len2, n);
}

int gstr_shim_casecmp(const char *s1, size_t len1, const char *s2, size_t len2) {
    return gstrcasecmp(s1, len1, s2, len2);
}

int gstr_shim_startswith(const char *s, size_t s_len, const char *prefix, size_t prefix_len) {
    return gstrstartswith(s, s_len, prefix, prefix_len);
}

int gstr_shim_endswith(const char *s, size_t s_len, const char *suffix, size_t suffix_len) {
    return gstrendswith(s, s_len, suffix, suffix_len);
}

// Search functions return pointers - we convert to offset (-1 if NULL)
ptrdiff_t gstr_shim_chr(const char *s, size_t s_len, const char *grapheme, size_t g_len) {
    const char *result = gstrchr(s, s_len, grapheme, g_len);
    return result ? (result - s) : -1;
}

ptrdiff_t gstr_shim_rchr(const char *s, size_t s_len, const char *grapheme, size_t g_len) {
    const char *result = gstrrchr(s, s_len, grapheme, g_len);
    return result ? (result - s) : -1;
}

ptrdiff_t gstr_shim_str(const char *haystack, size_t h_len, const char *needle, size_t n_len) {
    const char *result = gstrstr(haystack, h_len, needle, n_len);
    return result ? (result - haystack) : -1;
}

ptrdiff_t gstr_shim_rstr(const char *haystack, size_t h_len, const char *needle, size_t n_len) {
    const char *result = gstrrstr(haystack, h_len, needle, n_len);
    return result ? (result - haystack) : -1;
}

ptrdiff_t gstr_shim_casestr(const char *haystack, size_t h_len, const char *needle, size_t n_len) {
    const char *result = gstrcasestr(haystack, h_len, needle, n_len);
    return result ? (result - haystack) : -1;
}

size_t gstr_shim_count(const char *s, size_t s_len, const char *needle, size_t n_len) {
    return gstrcount(s, s_len, needle, n_len);
}

size_t gstr_shim_sub(char *dst, size_t dst_size, const char *s, size_t s_len,
                     size_t start, size_t count) {
    return gstrsub(dst, dst_size, s, s_len, start, count);
}

size_t gstr_shim_reverse(char *dst, size_t dst_size, const char *s, size_t s_len) {
    return gstrrev(dst, dst_size, s, s_len);
}

size_t gstr_shim_replace(char *dst, size_t dst_size, const char *s, size_t s_len,
                         const char *old, size_t old_len, const char *new_str, size_t new_len) {
    return gstrreplace(dst, dst_size, s, s_len, old, old_len, new_str, new_len);
}

size_t gstr_shim_lower(char *dst, size_t dst_size, const char *s, size_t s_len) {
    return gstrlower(dst, dst_size, s, s_len);
}

size_t gstr_shim_upper(char *dst, size_t dst_size, const char *s, size_t s_len) {
    return gstrupper(dst, dst_size, s, s_len);
}

size_t gstr_shim_ltrim(char *dst, size_t dst_size, const char *s, size_t s_len) {
    return gstrltrim(dst, dst_size, s, s_len);
}

size_t gstr_shim_rtrim(char *dst, size_t dst_size, const char *s, size_t s_len) {
    return gstrrtrim(dst, dst_size, s, s_len);
}

size_t gstr_shim_trim(char *dst, size_t dst_size, const char *s, size_t s_len) {
    return gstrtrim(dst, dst_size, s, s_len);
}

size_t gstr_shim_ellipsis(char *dst, size_t dst_size, const char *s, size_t s_len,
                          size_t max_graphemes, const char *suffix, size_t suffix_len) {
    return gstrellipsis(dst, dst_size, s, s_len, max_graphemes, suffix, suffix_len);
}

size_t gstr_shim_wlpad(char *dst, size_t dst_size, const char *s, size_t s_len,
                       size_t width, const char *pad, size_t pad_len) {
    return gstrwlpad(dst, dst_size, s, s_len, width, pad, pad_len);
}

size_t gstr_shim_wrpad(char *dst, size_t dst_size, const char *s, size_t s_len,
                       size_t width, const char *pad, size_t pad_len) {
    return gstrwrpad(dst, dst_size, s, s_len, width, pad, pad_len);
}

size_t gstr_shim_wpad(char *dst, size_t dst_size, const char *s, size_t s_len,
                      size_t width, const char *pad, size_t pad_len) {
    return gstrwpad(dst, dst_size, s, s_len, width, pad, pad_len);
}

size_t gstr_shim_wtrunc(char *dst, size_t dst_size, const char *s, size_t s_len,
                        size_t max_width) {
    return gstrwtrunc(dst, dst_size, s, s_len, max_width);
}

int gstr_shim_utf8_valid(const char *s, int byte_len, int *error_offset) {
    return utf8_valid(s, byte_len, error_offset);
}

int gstr_shim_utf8_next_grapheme(const char *s, int byte_len, int offset) {
    return utf8_next_grapheme(s, byte_len, offset);
}
*/
import "C"
import "unsafe"

// Len returns the number of grapheme clusters in s.
// A grapheme cluster is what users perceive as a single "character".
func Len(s string) int {
	if len(s) == 0 {
		return 0
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))
	return int(C.gstr_shim_len(cs, C.size_t(len(s))))
}

// NLen returns the number of grapheme clusters in s, up to max.
func NLen(s string, max int) int {
	if len(s) == 0 {
		return 0
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))
	return int(C.gstr_shim_nlen(cs, C.size_t(len(s)), C.size_t(max)))
}

// Width returns the display width in terminal columns.
// CJK characters and emoji are typically 2 columns wide.
func Width(s string) int {
	if len(s) == 0 {
		return 0
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))
	return int(C.gstr_shim_width(cs, C.size_t(len(s))))
}

// Offset returns the byte offset of the nth grapheme cluster (0-indexed).
// Returns len(s) if n is beyond the end.
//
// O(n) - scans from start. For sequential iteration, use an Iterator instead
// to avoid O(n²) when called in a loop.
func Offset(s string, n int) int {
	if len(s) == 0 {
		return 0
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))
	return int(C.gstr_shim_offset(cs, C.size_t(len(s)), C.size_t(n)))
}

// At returns the nth grapheme cluster (0-indexed).
// Returns empty string if n is out of range.
//
// O(n) - scans from start. For sequential iteration, use an Iterator instead
// to avoid O(n²) when called in a loop.
func At(s string, n int) string {
	if len(s) == 0 || n < 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	var outLen C.size_t
	result := C.gstr_shim_at(cs, C.size_t(len(s)), C.size_t(n), &outLen)
	if result == nil || outLen == 0 {
		return ""
	}
	return C.GoStringN(result, C.int(outLen))
}

// Compare compares two strings grapheme-by-grapheme.
// Returns negative if a < b, zero if a == b, positive if a > b.
func Compare(a, b string) int {
	ca := C.CString(a)
	cb := C.CString(b)
	defer C.free(unsafe.Pointer(ca))
	defer C.free(unsafe.Pointer(cb))
	return int(C.gstr_shim_cmp(ca, C.size_t(len(a)), cb, C.size_t(len(b))))
}

// CompareN compares the first n graphemes of two strings.
func CompareN(a, b string, n int) int {
	ca := C.CString(a)
	cb := C.CString(b)
	defer C.free(unsafe.Pointer(ca))
	defer C.free(unsafe.Pointer(cb))
	return int(C.gstr_shim_ncmp(ca, C.size_t(len(a)), cb, C.size_t(len(b)), C.size_t(n)))
}

// CaseCompare compares two strings case-insensitively (ASCII only).
func CaseCompare(a, b string) int {
	ca := C.CString(a)
	cb := C.CString(b)
	defer C.free(unsafe.Pointer(ca))
	defer C.free(unsafe.Pointer(cb))
	return int(C.gstr_shim_casecmp(ca, C.size_t(len(a)), cb, C.size_t(len(b))))
}

// HasPrefix reports whether s begins with prefix.
func HasPrefix(s, prefix string) bool {
	if len(prefix) == 0 {
		return true
	}
	if len(s) == 0 {
		return false
	}
	cs := C.CString(s)
	cp := C.CString(prefix)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cp))
	return C.gstr_shim_startswith(cs, C.size_t(len(s)), cp, C.size_t(len(prefix))) != 0
}

// HasSuffix reports whether s ends with suffix.
func HasSuffix(s, suffix string) bool {
	if len(suffix) == 0 {
		return true
	}
	if len(s) == 0 {
		return false
	}
	cs := C.CString(s)
	csuf := C.CString(suffix)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(csuf))
	return C.gstr_shim_endswith(cs, C.size_t(len(s)), csuf, C.size_t(len(suffix))) != 0
}

// Index returns the byte index of the first occurrence of grapheme in s,
// or -1 if not found.
func Index(s, grapheme string) int {
	if len(s) == 0 || len(grapheme) == 0 {
		return -1
	}
	cs := C.CString(s)
	cg := C.CString(grapheme)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cg))
	return int(C.gstr_shim_chr(cs, C.size_t(len(s)), cg, C.size_t(len(grapheme))))
}

// LastIndex returns the byte index of the last occurrence of grapheme in s,
// or -1 if not found.
func LastIndex(s, grapheme string) int {
	if len(s) == 0 || len(grapheme) == 0 {
		return -1
	}
	cs := C.CString(s)
	cg := C.CString(grapheme)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cg))
	return int(C.gstr_shim_rchr(cs, C.size_t(len(s)), cg, C.size_t(len(grapheme))))
}

// Contains reports whether needle is within s.
func Contains(s, needle string) bool {
	return IndexSubstring(s, needle) >= 0
}

// IndexSubstring returns the byte index of the first occurrence of needle in s,
// or -1 if not found.
func IndexSubstring(s, needle string) int {
	if len(needle) == 0 {
		return 0
	}
	if len(s) == 0 {
		return -1
	}
	cs := C.CString(s)
	cn := C.CString(needle)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cn))
	return int(C.gstr_shim_str(cs, C.size_t(len(s)), cn, C.size_t(len(needle))))
}

// LastIndexSubstring returns the byte index of the last occurrence of needle in s,
// or -1 if not found.
func LastIndexSubstring(s, needle string) int {
	if len(needle) == 0 {
		return len(s)
	}
	if len(s) == 0 {
		return -1
	}
	cs := C.CString(s)
	cn := C.CString(needle)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cn))
	return int(C.gstr_shim_rstr(cs, C.size_t(len(s)), cn, C.size_t(len(needle))))
}

// IndexFold returns the byte index of the first case-insensitive occurrence
// of needle in s, or -1 if not found.
func IndexFold(s, needle string) int {
	if len(needle) == 0 {
		return 0
	}
	if len(s) == 0 {
		return -1
	}
	cs := C.CString(s)
	cn := C.CString(needle)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cn))
	return int(C.gstr_shim_casestr(cs, C.size_t(len(s)), cn, C.size_t(len(needle))))
}

// Count returns the number of non-overlapping occurrences of needle in s.
func Count(s, needle string) int {
	if len(needle) == 0 {
		return Len(s) + 1
	}
	if len(s) == 0 {
		return 0
	}
	cs := C.CString(s)
	cn := C.CString(needle)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cn))
	return int(C.gstr_shim_count(cs, C.size_t(len(s)), cn, C.size_t(len(needle))))
}

// Sub returns a substring from start grapheme for count graphemes.
func Sub(s string, start, count int) string {
	if len(s) == 0 || count <= 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	// Allocate buffer (worst case: same size as input)
	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_sub((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		C.size_t(start), C.size_t(count))
	if n == 0 {
		return ""
	}
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// Reverse returns s with grapheme clusters in reverse order.
func Reverse(s string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_reverse((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// Replace returns s with all non-overlapping occurrences of old replaced by new.
func Replace(s, old, new string) string {
	if len(s) == 0 || len(old) == 0 {
		return s
	}
	cs := C.CString(s)
	co := C.CString(old)
	cn := C.CString(new)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(co))
	defer C.free(unsafe.Pointer(cn))

	// Estimate buffer size
	bufSize := len(s)*2 + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_replace((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		co, C.size_t(len(old)), cn, C.size_t(len(new)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// ToLower returns s with ASCII characters converted to lowercase.
func ToLower(s string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_lower((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// ToUpper returns s with ASCII characters converted to uppercase.
func ToUpper(s string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_upper((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// TrimLeft returns s with leading whitespace removed.
func TrimLeft(s string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_ltrim((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// TrimRight returns s with trailing whitespace removed.
func TrimRight(s string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_rtrim((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// Trim returns s with leading and trailing whitespace removed.
func Trim(s string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_trim((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// Ellipsis truncates s to maxGraphemes and appends suffix if truncated.
func Ellipsis(s string, maxGraphemes int, suffix string) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	csuf := C.CString(suffix)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(csuf))

	bufSize := len(s) + len(suffix) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_ellipsis((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		C.size_t(maxGraphemes), csuf, C.size_t(len(suffix)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// PadLeft pads s on the left to the specified column width.
func PadLeft(s string, width int, pad string) string {
	if len(pad) == 0 {
		pad = " "
	}
	cs := C.CString(s)
	cp := C.CString(pad)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cp))

	bufSize := len(s) + width*4 + 1 // worst case for multi-byte pad
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_wlpad((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		C.size_t(width), cp, C.size_t(len(pad)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// PadRight pads s on the right to the specified column width.
func PadRight(s string, width int, pad string) string {
	if len(pad) == 0 {
		pad = " "
	}
	cs := C.CString(s)
	cp := C.CString(pad)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cp))

	bufSize := len(s) + width*4 + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_wrpad((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		C.size_t(width), cp, C.size_t(len(pad)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// PadCenter pads s on both sides to center it within the specified column width.
func PadCenter(s string, width int, pad string) string {
	if len(pad) == 0 {
		pad = " "
	}
	cs := C.CString(s)
	cp := C.CString(pad)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cp))

	bufSize := len(s) + width*4 + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_wpad((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		C.size_t(width), cp, C.size_t(len(pad)))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// Truncate truncates s to fit within maxWidth terminal columns.
func Truncate(s string, maxWidth int) string {
	if len(s) == 0 {
		return ""
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	bufSize := len(s) + 1
	buf := C.malloc(C.size_t(bufSize))
	defer C.free(buf)

	n := C.gstr_shim_wtrunc((*C.char)(buf), C.size_t(bufSize), cs, C.size_t(len(s)),
		C.size_t(maxWidth))
	return C.GoStringN((*C.char)(buf), C.int(n))
}

// Valid reports whether s is valid UTF-8.
func Valid(s string) bool {
	if len(s) == 0 {
		return true
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))
	return C.gstr_shim_utf8_valid(cs, C.int(len(s)), nil) != 0
}

// ValidIndex returns the byte offset of the first invalid UTF-8 sequence,
// or -1 if the string is valid.
func ValidIndex(s string) int {
	if len(s) == 0 {
		return -1
	}
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	var errorOffset C.int
	if C.gstr_shim_utf8_valid(cs, C.int(len(s)), &errorOffset) != 0 {
		return -1
	}
	return int(errorOffset)
}

// Graphemes returns an iterator over the grapheme clusters in s.
func Graphemes(s string) *GraphemeIterator {
	return &GraphemeIterator{s: s, pos: 0}
}

// GraphemeIterator iterates over grapheme clusters in a string.
type GraphemeIterator struct {
	s   string
	pos int
}

// Next returns the next grapheme cluster and advances the iterator.
// Returns empty string when iteration is complete.
func (it *GraphemeIterator) Next() string {
	if it.pos >= len(it.s) {
		return ""
	}

	cs := C.CString(it.s)
	defer C.free(unsafe.Pointer(cs))

	nextPos := int(C.gstr_shim_utf8_next_grapheme(cs, C.int(len(it.s)), C.int(it.pos)))
	if nextPos <= it.pos || nextPos > len(it.s) {
		it.pos = len(it.s)
		return ""
	}

	grapheme := it.s[it.pos:nextPos]
	it.pos = nextPos
	return grapheme
}

// Reset resets the iterator to the beginning of the string.
func (it *GraphemeIterator) Reset() {
	it.pos = 0
}
