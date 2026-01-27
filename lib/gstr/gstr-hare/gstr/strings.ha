// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
//
// High-level grapheme-aware string operations

// Count grapheme clusters in a string
export fn gstrlen(s: str) size = {
	const bytes = toutf8(s);
	if (len(bytes) == 0) {
		return 0;
	};
	
	let count: size = 0;
	let off: size = 0;
	
	for (off < len(bytes)) {
		off = next_grapheme(bytes, off);
		count += 1;
	};
	
	return count;
};

// Count grapheme clusters, stopping at max
export fn gstrnlen(s: str, max: size) size = {
	const bytes = toutf8(s);
	if (len(bytes) == 0) {
		return 0;
	};
	
	let count: size = 0;
	let off: size = 0;
	
	for (off < len(bytes) && count < max) {
		off = next_grapheme(bytes, off);
		count += 1;
	};
	
	return count;
};

// Calculate display width of a string
export fn gstrwidth(s: str) size = {
	const bytes = toutf8(s);
	if (len(bytes) == 0) {
		return 0;
	};
	
	let width: size = 0;
	let off: size = 0;
	
	for (off < len(bytes)) {
		const next_off = next_grapheme(bytes, off);
		width += grapheme_width(bytes, off, next_off);
		off = next_off;
	};
	
	return width;
};

// Get byte offset of nth grapheme. O(n) - scans from start.
// For sequential iteration, use next_grapheme() instead to avoid O(n²).
export fn gstroff(s: str, n: size) size = {
	const bytes = toutf8(s);
	if (len(bytes) == 0) {
		return 0;
	};
	
	let count: size = 0;
	let off: size = 0;
	
	for (off < len(bytes) && count < n) {
		off = next_grapheme(bytes, off);
		count += 1;
	};
	
	return off;
};

// Get nth grapheme as a string slice. O(n) - scans from start.
// For sequential iteration, use next_grapheme() or iter() instead to avoid O(n²).
export fn gstrat(s: str, n: size) (str | void) = {
	const bytes = toutf8(s);
	if (len(bytes) == 0) {
		return void;
	};
	
	let count: size = 0;
	let off: size = 0;
	
	// Skip to nth grapheme
	for (off < len(bytes) && count < n) {
		off = next_grapheme(bytes, off);
		count += 1;
	};
	
	if (off >= len(bytes)) {
		return void;
	};
	
	const start = off;
	const end = next_grapheme(bytes, start);
	
	return fromutf8_unsafe(bytes[start..end]);
};

// Compare two strings by grapheme
export fn gstrcmp(a: str, b: str) i8 = {
	let it_a = iter(a);
	let it_b = iter(b);
	
	for (true) {
		const ga = next(&it_a);
		const gb = next(&it_b);
		
		match (ga) {
		case void =>
			match (gb) {
			case void =>
				return 0;  // Both exhausted, equal
			case str =>
				return -1; // a shorter than b
			};
		case let sa: str =>
			match (gb) {
			case void =>
				return 1;  // a longer than b
			case let sb: str =>
				const cmp = cmp_grapheme(sa, sb);
				if (cmp != 0) {
					return cmp;
				};
			};
		};
	};
};

// Compare first n graphemes
export fn gstrncmp(a: str, b: str, n: size) i8 = {
	let it_a = iter(a);
	let it_b = iter(b);
	let count: size = 0;
	
	for (count < n) {
		const ga = next(&it_a);
		const gb = next(&it_b);
		
		match (ga) {
		case void =>
			match (gb) {
			case void =>
				return 0;
			case str =>
				return -1;
			};
		case let sa: str =>
			match (gb) {
			case void =>
				return 1;
			case let sb: str =>
				const cmp = cmp_grapheme(sa, sb);
				if (cmp != 0) {
					return cmp;
				};
			};
		};
		count += 1;
	};
	
	return 0;
};

// Case-insensitive compare (ASCII only)
export fn gstrcasecmp(a: str, b: str) i8 = {
	let it_a = iter(a);
	let it_b = iter(b);
	
	for (true) {
		const ga = next(&it_a);
		const gb = next(&it_b);
		
		match (ga) {
		case void =>
			match (gb) {
			case void =>
				return 0;
			case str =>
				return -1;
			};
		case let sa: str =>
			match (gb) {
			case void =>
				return 1;
			case let sb: str =>
				const cmp = cmp_grapheme_icase(sa, sb);
				if (cmp != 0) {
					return cmp;
				};
			};
		};
	};
};

// Check if string starts with prefix
export fn gstrstartswith(s: str, prefix: str) bool = {
	let it_s = iter(s);
	let it_p = iter(prefix);
	
	for (true) {
		const gp = next(&it_p);
		match (gp) {
		case void =>
			return true;  // Prefix exhausted, match
		case let sp: str =>
			const gs = next(&it_s);
			match (gs) {
			case void =>
				return false;  // s shorter than prefix
			case let ss: str =>
				if (cmp_grapheme(ss, sp) != 0) {
					return false;
				};
			};
		};
	};
};

// Check if string ends with suffix
export fn gstrendswith(s: str, suffix: str) bool = {
	const s_len = gstrlen(s);
	const suffix_len = gstrlen(suffix);
	
	if (suffix_len > s_len) {
		return false;
	};
	
	const start = gstroff(s, s_len - suffix_len);
	const bytes = toutf8(s);
	const tail = fromutf8_unsafe(bytes[start..]);
	
	return gstrcmp(tail, suffix) == 0;
};

// Find first occurrence of grapheme in string
export fn gstrchr(s: str, g: str) (size | void) = {
	let it = iter(s);
	let idx: size = 0;
	
	for (true) {
		const curr = next(&it);
		match (curr) {
		case void =>
			return void;
		case let gs: str =>
			if (cmp_grapheme(gs, g) == 0) {
				return idx;
			};
			idx += 1;
		};
	};
};

// Find last occurrence of grapheme in string
export fn gstrrchr(s: str, g: str) (size | void) = {
	let it = iter(s);
	let idx: size = 0;
	let last: (size | void) = void;
	
	for (true) {
		const curr = next(&it);
		match (curr) {
		case void =>
			return last;
		case let gs: str =>
			if (cmp_grapheme(gs, g) == 0) {
				last = idx;
			};
			idx += 1;
		};
	};
};

// Find substring in string (returns grapheme index)
export fn gstrindex(haystack: str, needle: str) (size | void) = {
	const needle_len = gstrlen(needle);
	if (needle_len == 0) {
		return 0;
	};
	
	const haystack_len = gstrlen(haystack);
	if (needle_len > haystack_len) {
		return void;
	};
	
	for (let i: size = 0; i <= haystack_len - needle_len; i += 1) {
		if (gstrncmp(gstrslice(haystack, i), needle, needle_len) == 0) {
			return i;
		};
	};
	
	return void;
};

// Count occurrences of needle in haystack
export fn gstrcount(haystack: str, needle: str) size = {
	const needle_len = gstrlen(needle);
	if (needle_len == 0) {
		return 0;
	};
	
	let count: size = 0;
	let it = iter(haystack);
	let idx: size = 0;
	
	for (true) {
		const curr = next(&it);
		match (curr) {
		case void =>
			break;
		case str =>
			if (gstrncmp(gstrslice(haystack, idx), needle, needle_len) == 0) {
				count += 1;
			};
			idx += 1;
		};
	};
	
	return count;
};

// Get string slice starting at grapheme index
fn gstrslice(s: str, start: size) str = {
	const bytes = toutf8(s);
	const byte_off = gstroff(s, start);
	if (byte_off >= len(bytes)) {
		return "";
	};
	return fromutf8_unsafe(bytes[byte_off..]);
};

// Count leading graphemes in accept set
export fn gstrspan(s: str, accept: str) size = {
	let it = iter(s);
	let count: size = 0;
	
	for (true) {
		const g = next(&it);
		match (g) {
		case void =>
			break;
		case let gs: str =>
			if (!grapheme_in_set(gs, accept)) {
				break;
			};
			count += 1;
		};
	};
	
	return count;
};

// Count leading graphemes NOT in reject set
export fn gstrcspan(s: str, reject: str) size = {
	let it = iter(s);
	let count: size = 0;
	
	for (true) {
		const g = next(&it);
		match (g) {
		case void =>
			break;
		case let gs: str =>
			if (grapheme_in_set(gs, reject)) {
				break;
			};
			count += 1;
		};
	};
	
	return count;
};

// Find first grapheme from accept set
export fn gstrpbrk(s: str, accept: str) (size | void) = {
	let it = iter(s);
	let idx: size = 0;
	
	for (true) {
		const g = next(&it);
		match (g) {
		case void =>
			return void;
		case let gs: str =>
			if (grapheme_in_set(gs, accept)) {
				return idx;
			};
			idx += 1;
		};
	};
};

// Helper: compare two grapheme strings
fn cmp_grapheme(a: str, b: str) i8 = {
	const ba = toutf8(a);
	const bb = toutf8(b);
	const min_len = if (len(ba) < len(bb)) len(ba) else len(bb);
	
	for (let i: size = 0; i < min_len; i += 1) {
		if (ba[i] < bb[i]) return -1;
		if (ba[i] > bb[i]) return 1;
	};
	
	if (len(ba) < len(bb)) return -1;
	if (len(ba) > len(bb)) return 1;
	return 0;
};

// Helper: case-insensitive compare (ASCII only)
fn cmp_grapheme_icase(a: str, b: str) i8 = {
	const ba = toutf8(a);
	const bb = toutf8(b);
	const min_len = if (len(ba) < len(bb)) len(ba) else len(bb);
	
	for (let i: size = 0; i < min_len; i += 1) {
		const ca = ascii_lower(ba[i]);
		const cb = ascii_lower(bb[i]);
		if (ca < cb) return -1;
		if (ca > cb) return 1;
	};
	
	if (len(ba) < len(bb)) return -1;
	if (len(ba) > len(bb)) return 1;
	return 0;
};

// Helper: ASCII lowercase
fn ascii_lower(c: u8) u8 = {
	if (c >= 0x41 && c <= 0x5A) {  // 'A' to 'Z'
		return c + 0x20;  // 'a' - 'A' = 32
	};
	return c;
};

// Helper: check if grapheme is in set
fn grapheme_in_set(g: str, set: str) bool = {
	let it = iter(set);
	
	for (true) {
		const sg = next(&it);
		match (sg) {
		case void =>
			return false;
		case let s: str =>
			if (cmp_grapheme(g, s) == 0) {
				return true;
			};
		};
	};
};

// Helper: convert str to []u8
fn toutf8(s: str) []u8 = {
	return *(&s: *[]u8);
};

// Helper: convert []u8 to str
fn fromutf8_unsafe(bytes: []u8) str = {
	return *(&bytes: *str);
};
