// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
//
// UAX #29 Extended Grapheme Cluster Segmentation

// Unicode replacement character (for decode errors)
export def REPLACEMENT_CHAR: rune = '\uFFFD';

// State for grapheme break detection
export type break_state = struct {
	ri_count: size,     // Regional indicator count (for flags)
	ext_pict: bool,     // In extended pictographic sequence
	incb_state: u8,     // 0=none, 1=after_consonant, 2=after_linker
};

// Determine if there's a grapheme cluster break between two codepoints.
// Implements UAX #29 extended grapheme cluster rules.
export fn is_grapheme_break(
	prev_prop: gcb,
	curr_prop: gcb,
	ri_count: size,
	in_ext_pict: bool,
	curr_cp: u32,
	incb_state: u8,
) bool = {
	// GB3: CR × LF
	if (prev_prop == gcb::CR && curr_prop == gcb::LF) {
		return false;
	};

	// GB4: (Control | CR | LF) ÷
	if (prev_prop == gcb::CONTROL || prev_prop == gcb::CR || prev_prop == gcb::LF) {
		return true;
	};

	// GB5: ÷ (Control | CR | LF)
	if (curr_prop == gcb::CONTROL || curr_prop == gcb::CR || curr_prop == gcb::LF) {
		return true;
	};

	// GB6: L × (L | V | LV | LVT)
	if (prev_prop == gcb::L && (curr_prop == gcb::L || curr_prop == gcb::V ||
	                            curr_prop == gcb::LV || curr_prop == gcb::LVT)) {
		return false;
	};

	// GB7: (LV | V) × (V | T)
	if ((prev_prop == gcb::LV || prev_prop == gcb::V) &&
	    (curr_prop == gcb::V || curr_prop == gcb::T)) {
		return false;
	};

	// GB8: (LVT | T) × T
	if ((prev_prop == gcb::LVT || prev_prop == gcb::T) && curr_prop == gcb::T) {
		return false;
	};

	// GB9: × (Extend | ZWJ)
	if (curr_prop == gcb::EXTEND || curr_prop == gcb::ZWJ) {
		return false;
	};

	// GB9a: × SpacingMark
	if (curr_prop == gcb::SPACING_MARK) {
		return false;
	};

	// GB9b: Prepend ×
	if (prev_prop == gcb::PREPEND) {
		return false;
	};

	// GB9c: Indic conjunct sequences
	if (incb_state == 2 && is_incb_consonant(curr_cp)) {
		return false;
	};

	// GB11: ExtPict Extend* ZWJ × ExtPict
	if (in_ext_pict && prev_prop == gcb::ZWJ && is_extended_pictographic(curr_cp)) {
		return false;
	};

	// GB12/GB13: RI × RI (only for pairs)
	if (prev_prop == gcb::REGIONAL_INDICATOR && curr_prop == gcb::REGIONAL_INDICATOR) {
		return (ri_count % 2) == 0;
	};

	// GB999: Any ÷ Any
	return true;
};

// Decode a UTF-8 sequence starting at off, returning (codepoint, bytes_consumed)
fn decode_at(bytes: []u8, off: size) (rune, size) = {
	if (off >= len(bytes)) {
		return (REPLACEMENT_CHAR, 0);
	};
	
	const first = bytes[off];
	
	// ASCII fast path
	if (first < 0x80) {
		return (first: rune, 1);
	};
	
	// Determine sequence length
	let seq_len: size = 0;
	let cp: u32 = 0;
	
	if ((first & 0xE0) == 0xC0) {
		seq_len = 2;
		cp = (first & 0x1F): u32;
	} else if ((first & 0xF0) == 0xE0) {
		seq_len = 3;
		cp = (first & 0x0F): u32;
	} else if ((first & 0xF8) == 0xF0) {
		seq_len = 4;
		cp = (first & 0x07): u32;
	} else {
		return (REPLACEMENT_CHAR, 1);
	};
	
	if (off + seq_len > len(bytes)) {
		return (REPLACEMENT_CHAR, 1);
	};
	
	for (let i: size = 1; i < seq_len; i += 1) {
		const b = bytes[off + i];
		if ((b & 0xC0) != 0x80) {
			return (REPLACEMENT_CHAR, 1);
		};
		cp = (cp << 6) | ((b & 0x3F): u32);
	};
	
	// Check for overlong encodings
	if ((seq_len == 2 && cp < 0x80) ||
	    (seq_len == 3 && cp < 0x800) ||
	    (seq_len == 4 && cp < 0x10000)) {
		return (REPLACEMENT_CHAR, seq_len);
	};
	
	// Check for surrogates
	if (cp >= 0xD800 && cp <= 0xDFFF) {
		return (REPLACEMENT_CHAR, seq_len);
	};
	
	// Check for out of range
	if (cp > 0x10FFFF) {
		return (REPLACEMENT_CHAR, seq_len);
	};
	
	return (cp: rune, seq_len);
};

// Find start of previous UTF-8 character
fn prev_char_start(bytes: []u8, off: size) size = {
	if (off == 0) {
		return 0;
	};
	
	let pos = off - 1;
	const limit = if (off > 4) off - 4 else 0z;
	
	for (pos > limit && (bytes[pos] & 0xC0) == 0x80) {
		pos -= 1;
	};
	
	return pos;
};

// Returns the byte off after the next grapheme cluster
export fn next_grapheme(bytes: []u8, off: size) size = {
	if (off >= len(bytes)) {
		return len(bytes);
	};

	const (prev_cp, char_bytes) = decode_at(bytes, off);
	let next_off = off + char_bytes;

	if (next_off >= len(bytes)) {
		return len(bytes);
	};

	let prev_prop = get_gcb(prev_cp: u32);
	let ri_count: size = if (prev_prop == gcb::REGIONAL_INDICATOR) 1 else 0;
	let in_ext_pict = is_extended_pictographic(prev_cp: u32);
	let incb_state: u8 = if (is_incb_consonant(prev_cp: u32)) 1 else 0;

	for (next_off < len(bytes)) {
		const (curr_cp, bytes_consumed) = decode_at(bytes, next_off);
		const curr_prop = get_gcb(curr_cp: u32);

		if (is_grapheme_break(prev_prop, curr_prop, ri_count, in_ext_pict,
		                      curr_cp: u32, incb_state)) {
			return next_off;
		};

		// Update state
		if (curr_prop == gcb::REGIONAL_INDICATOR) {
			ri_count += 1;
		} else if (curr_prop != gcb::EXTEND && curr_prop != gcb::ZWJ) {
			ri_count = 0;
		};

		if (is_extended_pictographic(curr_cp: u32)) {
			in_ext_pict = true;
		} else if (curr_prop != gcb::EXTEND && curr_prop != gcb::ZWJ) {
			in_ext_pict = false;
		};

		if (is_incb_consonant(curr_cp: u32)) {
			incb_state = 1;
		} else if (is_incb_linker(curr_cp: u32) && incb_state >= 1) {
			incb_state = 2;
		} else if (curr_prop != gcb::EXTEND && curr_prop != gcb::ZWJ) {
			incb_state = 0;
		};

		prev_prop = curr_prop;
		next_off += bytes_consumed;
	};

	return len(bytes);
};

// Returns the byte off of the previous grapheme cluster start
export fn prev_grapheme(bytes: []u8, off: size) size = {
	if (off == 0) {
		return 0;
	};

	let prev_start = prev_char_start(bytes, off);
	if (prev_start == 0 && off > 0) {
		return 0;
	};

	// Scan backwards to find a safe starting point
	let scan_start = prev_start;
	let remaining: size = GRAPHEME_MAX_BACKTRACK;
	
	for (remaining > 0 && scan_start > 0) {
		const prev = prev_char_start(bytes, scan_start);
		if (prev == scan_start) break;
		scan_start = prev;
		remaining -= 1;
	};

	// Scan forward to find the grapheme containing off
	let curr = scan_start;
	let grapheme_start = scan_start;

	for (curr < off) {
		const next = next_grapheme(bytes, curr);
		if (next >= off) {
			break;
		};
		grapheme_start = next;
		curr = next;
	};

	return grapheme_start;
};

// Calculate display width of a grapheme cluster
export fn grapheme_width(bytes: []u8, start: size, end: size) size = {
	if (start >= end || start >= len(bytes)) {
		return 0;
	};
	
	// First codepoint determines base width
	const (cp, _) = decode_at(bytes, start);
	const w = cpwidth(cp: u32);
	
	// Negative width (control) or zero returns 0
	if (w <= 0) {
		return 0;
	};
	
	return w: size;
};
