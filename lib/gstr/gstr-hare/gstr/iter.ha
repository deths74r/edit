// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Edward J Edmonds <edward.edmonds@gmail.com>
//
// Grapheme cluster iterator

// Iterator over grapheme clusters in a string
export type grapheme_iter = struct {
	bytes: []u8,
	off: size,
};

// Create a new grapheme iterator from a string
export fn iter(s: str) grapheme_iter = {
	return grapheme_iter {
		bytes = strings_toutf8(s),
		off = 0,
	};
};

// Create a new grapheme iterator from bytes
export fn iter_bytes(bytes: []u8) grapheme_iter = {
	return grapheme_iter {
		bytes = bytes,
		off = 0,
	};
};

// Get the next grapheme cluster, or void if exhausted
export fn next(it: *grapheme_iter) (str | void) = {
	if (it.off >= len(it.bytes)) {
		return void;
	};
	
	const start = it.off;
	const end = next_grapheme(it.bytes, start);
	it.off = end;
	
	return strings_fromutf8_unsafe(it.bytes[start..end]);
};

// Peek at the next grapheme without advancing
export fn peek(it: *grapheme_iter) (str | void) = {
	if (it.off >= len(it.bytes)) {
		return void;
	};
	
	const start = it.off;
	const end = next_grapheme(it.bytes, start);
	
	return strings_fromutf8_unsafe(it.bytes[start..end]);
};

// Reset the iterator to the beginning
export fn reset(it: *grapheme_iter) void = {
	it.off = 0;
};

// Get the remaining unprocessed portion as a string
export fn rest(it: *grapheme_iter) str = {
	if (it.off >= len(it.bytes)) {
		return "";
	};
	return strings_fromutf8_unsafe(it.bytes[it.off..]);
};

// Get current byte offset
export fn byte_offset(it: *grapheme_iter) size = {
	return it.off;
};

// Check if iterator is exhausted
export fn exhausted(it: *grapheme_iter) bool = {
	return it.off >= len(it.bytes);
};

// Helper: convert str to []u8
fn strings_toutf8(s: str) []u8 = {
	return *(&s: *[]u8);
};

// Helper: convert []u8 to str (unsafe, assumes valid UTF-8)
fn strings_fromutf8_unsafe(bytes: []u8) str = {
	return *(&bytes: *str);
};
