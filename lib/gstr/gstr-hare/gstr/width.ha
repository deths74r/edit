// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
//
// Terminal width calculation for Unicode codepoints

// Returns the display width of a Unicode codepoint:
// -1 for control characters
//  0 for zero-width (combining marks, format chars)
//  1 for normal characters
//  2 for wide characters (CJK, emoji)
export fn cpwidth(cp: u32) i8 = {
	// Control characters (C0)
	if (cp < 0x20) {
		if (cp == 0x00) return 0;
		return -1;
	};
	
	// ASCII printable
	if (cp < 0x7F) {
		return 1;
	};
	
	// DEL
	if (cp == 0x7F) {
		return -1;
	};
	
	// C1 control characters
	if (cp < 0xA0) {
		return -1;
	};
	
	// Soft hyphen is width 1 (special case)
	if (cp == 0x00AD) {
		return 1;
	};
	
	// Zero-width characters
	if (range_contains(ZERO_WIDTH_RANGES, cp)) {
		return 0;
	};
	
	// Double-width characters
	if (range_contains(DOUBLE_WIDTH_RANGES, cp)) {
		return 2;
	};
	
	return 1;
};

// Returns true if the codepoint is zero-width
export fn is_zerowidth(cp: u32) bool = {
	return range_contains(ZERO_WIDTH_RANGES, cp);
};

// Returns true if the codepoint is double-width
export fn is_wide(cp: u32) bool = {
	return range_contains(DOUBLE_WIDTH_RANGES, cp);
};
