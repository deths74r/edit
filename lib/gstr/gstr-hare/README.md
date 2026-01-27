# gstr-hare

Pure Hare implementation of gstr - a grapheme-aware string library implementing UAX #29 grapheme cluster segmentation.

## Features

- **Zero C dependencies** - Pure Hare implementation
- **UAX #29 compliant** - Extended grapheme cluster segmentation
- **Terminal width** - Accurate column width calculation for CJK, emoji, combining marks
- **Full Unicode 17.0** - Complete property tables

## Module Structure

```
gstr/
├── tables.ha       # Unicode property tables (zero-width ranges)
├── tables_gcb.ha   # GCB ranges, double-width, InCB tables
├── tables_props.ha # GCB property lookup
├── width.ha        # Terminal width calculation
├── grapheme.ha     # UAX #29 grapheme segmentation
├── strings.ha      # High-level string API
└── iter.ha         # Grapheme iterator
```

## Building

```bash
# Build the library
hare build gstr/

# Run tests
hare run cmd/test/
```

## API Reference

### Length & Width

```hare
use gstr;

gstr::gstrlen("👨‍👩‍👧");     // 1 (graphemes)
gstr::gstrwidth("世界");    // 4 (terminal columns)
gstr::gstrnlen(s, 10);      // count up to 10 graphemes
```

### Indexing

```hare
gstr::gstrat(s, 0);         // First grapheme (str | void)
gstr::gstroffset(s, 5);     // Byte offset of 5th grapheme
```

### Comparison

```hare
gstr::gstrcmp(a, b);        // -1, 0, or 1
gstr::gstrncmp(a, b, n);    // Compare first n graphemes
gstr::gstrcasecmp(a, b);    // Case-insensitive (ASCII)
```

### Search

```hare
gstr::gstrchr(s, "a");      // Find grapheme (size | void)
gstr::gstrrchr(s, "a");     // Find last occurrence
gstr::gstrindex(s, needle); // Find substring
gstr::gstrcount(s, needle); // Count occurrences
```

### Prefix/Suffix

```hare
gstr::gstrstartswith(s, prefix);
gstr::gstrendswith(s, suffix);
```

### Span

```hare
gstr::gstrspan(s, accept);  // Leading chars in set
gstr::gstrcspan(s, reject); // Leading chars NOT in set
gstr::gstrpbrk(s, accept);  // First char from set
```

### Iterator

```hare
let it = gstr::iter("hello");
for (true) {
    match (gstr::next(&it)) {
    case let g: str =>
        // Process grapheme g
    case void =>
        break;
    };
};
```

### Width Functions

```hare
gstr::cpwidth(cp);          // Width of codepoint (-1, 0, 1, 2)
gstr::is_zerowidth(cp);     // Is combining/format char?
gstr::is_wide(cp);          // Is CJK/emoji?
```

### Grapheme Navigation

```hare
gstr::next_grapheme(bytes, offset);  // Next grapheme boundary
gstr::prev_grapheme(bytes, offset);  // Previous boundary
gstr::grapheme_width(bytes, start, end);
```

## Performance Notes

### Sequential Iteration: Use `next_grapheme`, Not `gstroff`

`gstroff(s, n)` and `gstrat(s, n)` scan from the beginning of the string each time - they are O(n) operations. Using them in a loop creates O(n²) complexity:

```hare
// BAD: O(n²) - gstroff rescans from start each iteration
for (let g: size = 0; g < count; g += 1) {
    const off = gstr::gstroff(s, g + 1);  // O(g) each call!
    // ...
}

// GOOD: O(n) - next_grapheme advances incrementally
let bytes = strings::toutf8(s);
let off: size = 0;
for (let g: size = 0; g < count; g += 1) {
    const next_off = gstr::next_grapheme(bytes, off);  // O(1)
    // Process bytes[off..next_off]
    off = next_off;
}

// ALSO GOOD: Use the iterator
let it = gstr::iter(s);
for (true) {
    match (gstr::next(&it)) {
    case let g: str =>
        // Process grapheme g
    case void =>
        break;
    };
};
```

**Rule of thumb:**
- **Random access** (jump to grapheme N): Use `gstroff` / `gstrat`
- **Sequential iteration**: Use `next_grapheme` or the iterator

## Test Cases Covered

- Empty strings
- ASCII only
- CJK characters (世界)
- Simple emoji (👋)
- Emoji with skin tone (👋🏽)
- ZWJ family sequences (👨‍👩‍👧)
- Regional indicator flags (🇨🇦)
- Combining marks (café with U+0301)
- Korean Hangul (한글)

## Unicode Version

Unicode 17.0 property tables.

## License

MIT License - see parent project for details.
