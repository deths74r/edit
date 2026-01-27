# gstr-zig

Idiomatic Zig bindings for [gstr](../README.md) - a grapheme-based string library.

## Overview

Where Zig's standard library operates on bytes, gstr operates on **grapheme clusters** - the units humans perceive as "characters".

```zig
const gstr = @import("gstr");

// ASCII
gstr.len("Hello")        // 5 graphemes
gstr.width("Hello")      // 5 columns

// CJK characters (2 columns each)
gstr.len("世界")          // 2 graphemes
gstr.width("世界")        // 4 columns

// Emoji with ZWJ sequence
gstr.len("👨‍👩‍👧")          // 1 grapheme (family emoji)
gstr.width("👨‍👩‍👧")         // 2 columns
```

## Installation

Add gstr as a dependency in your `build.zig.zon`:

```zig
.dependencies = .{
    .gstr = .{
        .path = "path/to/gstr-zig",
    },
},
```

Then in `build.zig`:

```zig
const gstr_dep = b.dependency("gstr", .{
    .target = target,
    .optimize = optimize,
});
exe.root_module.addImport("gstr", gstr_dep.module("gstr"));
```

## API Reference

### Two Access Levels

```zig
const gstr = @import("gstr");

// 1. High-level idiomatic API (slices, optionals, allocators)
const count = gstr.len("Hello 世界");           // 8
const w = gstr.width("Hello 世界");             // 10
const g = gstr.at("Hello", 1) orelse return;   // "e"

// 2. Raw C access for zero-overhead direct calls
const raw = gstr.c.gstrlen(ptr, byte_len);
```

### Length/Width

```zig
gstr.len(s)              // grapheme count
gstr.nlen(s, max)        // count up to max
gstr.width(s)            // terminal column width
```

### Indexing

```zig
gstr.at(s, n)            // ?[]const u8 - get Nth grapheme
gstr.offset(s, n)        // byte offset of Nth grapheme
```

### Comparison

```zig
gstr.cmp(a, b)           // Order (.less, .equal, .greater)
gstr.ncmp(a, b, n)       // compare first n graphemes
gstr.caseCmp(a, b)       // case-insensitive (ASCII)
gstr.nCaseCmp(a, b, n)   // case-insensitive first n
```

### Prefix/Suffix

```zig
gstr.startsWith(s, prefix)   // bool
gstr.endsWith(s, suffix)     // bool
```

### Search

```zig
gstr.chr(s, grapheme)    // ?usize - first occurrence (byte offset)
gstr.rchr(s, grapheme)   // ?usize - last occurrence
gstr.str(haystack, needle)     // ?usize - find substring
gstr.rstr(haystack, needle)    // ?usize - find last
gstr.caseStr(haystack, needle) // ?usize - case-insensitive
gstr.count(s, needle)    // usize - count occurrences
```

### Span

```zig
gstr.span(s, accept)     // count leading graphemes in accept
gstr.cspan(s, reject)    // count leading NOT in reject
gstr.pbrk(s, accept)     // ?usize - first grapheme in accept
```

### Extraction (allocating)

```zig
const sub = try gstr.sub(allocator, s, start, count);
defer allocator.free(sub);
```

### Copy/Concat/Dup (allocating)

```zig
const copied = try gstr.copy(allocator, src);
const first_n = try gstr.ncopy(allocator, src, n);
const joined = try gstr.cat(allocator, a, b);
const joined_n = try gstr.ncat(allocator, a, b, n);
```

### Tokenize

```zig
var iter = gstr.sep(s, ",");
while (iter.next()) |token| {
    // process token
}
```

### Trim (allocating)

```zig
const trimmed = try gstr.trim(allocator, s);    // both ends
const left = try gstr.ltrim(allocator, s);      // left only
const right = try gstr.rtrim(allocator, s);     // right only
```

### Transform (allocating)

```zig
const rev = try gstr.reverse(allocator, s);
const replaced = try gstr.replace(allocator, s, old, new);
const low = try gstr.lower(allocator, s);   // ASCII
const up = try gstr.upper(allocator, s);    // ASCII
```

### Fill/Ellipsis (allocating)

```zig
const repeated = try gstr.fill(allocator, grapheme, count);
const truncated = try gstr.ellipsis(allocator, s, max_graphemes, "...");
```

### Padding - Grapheme-based (allocating)

```zig
const left = try gstr.lpad(allocator, s, width, " ");   // left pad
const right = try gstr.rpad(allocator, s, width, " ");  // right pad
const center = try gstr.pad(allocator, s, width, " ");  // center
```

### Padding - Column-width-based (allocating)

For terminal alignment with mixed-width characters:

```zig
const left = try gstr.wlpad(allocator, s, cols, " ");   // left pad
const right = try gstr.wrpad(allocator, s, cols, " ");  // right pad
const center = try gstr.wpad(allocator, s, cols, " ");  // center
const truncated = try gstr.wtrunc(allocator, s, max_cols);
```

### UTF-8 Low-Level

```zig
const dec = gstr.decode(bytes);     // .{ .codepoint, .len }
const enc = gstr.encode(codepoint); // .{ .bytes, .len }
const result = gstr.valid(s);       // .{ .valid, .error_offset }
gstr.cpWidth(codepoint)             // display width of codepoint
gstr.charWidth(s, offset)           // width at byte position
gstr.next(s, offset)                // next codepoint boundary
gstr.prev(s, offset)                // previous codepoint boundary
gstr.nextGrapheme(s, offset)        // next grapheme boundary
gstr.prevGrapheme(s, offset)        // previous grapheme boundary
gstr.cpCount(s)                     // codepoint count (not graphemes)
gstr.truncate(s, max_cols)          // byte length to fit in cols
gstr.isZeroWidth(codepoint)         // bool
gstr.isWide(codepoint)              // bool
```

### Zig-Only: Grapheme Iterator

```zig
var iter = gstr.graphemes("Hello 世界");
while (iter.next()) |grapheme| {
    std.debug.print("{s}\n", .{grapheme});
}
// Prints: H, e, l, l, o, (space), 世, 界
```

## Testing

```bash
cd gstr-zig
zig build test
```

## License

MIT - see [LICENSE](../LICENSE)
