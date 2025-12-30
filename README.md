# edit

A minimal terminal text editor written in C.

## Contents

- [The Design](#the-design)
- [Features](#features)
- [Installation](#installation)
- [Key Bindings](#key-bindings)
- [Architecture](#architecture)
  - [The Three-Temperature Model](#the-three-temperature-model)
  - [The Cell](#the-cell)
  - [The Neighbor Layer](#the-neighbor-layer)
  - [Pair Entanglement](#pair-entanglement)
  - [Syntax Highlighting](#syntax-highlighting)
  - [Soft Line Wrapping](#soft-line-wrapping)
  - [Undo and Redo](#undo-and-redo)
  - [Multi-Cursor Editing](#multi-cursor-editing)
  - [The Rendering Pipeline](#the-rendering-pipeline)
  - [Color and Accessibility](#color-and-accessibility)
- [Extending the Editor](#extending-the-editor)
- [Building from Source](#building-from-source)

---

## The Design

Most terminal editors fall into two camps. The first—nano, micro, and their kin—offer simplicity at the cost of power. The second—vim, emacs, kakoune—offer power at the cost of a learning curve that resembles a cliff face. Both camps assume you'll eventually want plugins, configuration files, and an ecosystem of extensions.

This editor makes a different bet: that a single C file, compiled without dependencies beyond libc, can provide enough functionality for daily use while remaining small enough to understand completely. The entire implementation fits in roughly 8,500 lines. There are no plugins, no configuration files, no runtime dependencies. What you compile is what you get.

The feature set reflects this philosophy. Full Unicode support, because text in 2025 is Unicode. C syntax highlighting, because the editor is written in C and should be able to edit itself pleasantly. Soft line wrapping, because modern displays are wide and horizontal scrolling is tedious. Multiple cursors, because some edits are naturally parallel. Undo with automatic grouping, because mistakes happen. Find and replace with regex support, because pattern matching is fundamental to text editing.

What's deliberately absent is equally telling. No plugin system, because plugins require an API, and APIs require maintenance. No configuration file, because sensible defaults should suffice. No language server protocol support, because that's a different tool's job. No split panes, because a terminal multiplexer handles that better.

The result is an editor that starts instantly, uses minimal memory, and does exactly what it appears to do. The entire state of the program lives in a single global struct. The entire rendering pipeline fits in one function. If something breaks, there's only one place to look.

---

## Features

The editor provides:

- **Full Unicode support** — UTF-8 encoding, grapheme cluster navigation, proper display widths for CJK characters and emoji. The cursor moves by what humans perceive as characters, not by bytes or codepoints.

- **C syntax highlighting** — Keywords, types, strings, comments, preprocessor directives, function calls, operators, and escape sequences are colored distinctly.

- **Soft line wrapping** — Three modes: no wrapping (horizontal scroll), word wrap (break at word boundaries), and character wrap (break anywhere). Configurable continuation indicators.

- **Find and replace** — Incremental search with real-time highlighting. Case sensitivity, whole-word matching, and POSIX extended regular expressions with backreference support in replacements.

- **Multiple cursors** — Select a word with Ctrl+D, press again to add a cursor at the next occurrence. Type to insert at all cursors simultaneously.

- **Bracket matching** — Paired delimiters are tracked across the entire buffer. Jump between matching brackets with Ctrl+].

- **Undo and redo** — Full history with automatic grouping. Rapid keystrokes are grouped together; pauses create new undo points.

- **Mouse support** — Click to position cursor, drag to select, double-click for word selection, triple-click for line selection, scroll wheel with adaptive speed.

- **Visual aids** — Optional color column at 80 or 120 characters, trailing whitespace highlighting, visible whitespace mode, cursor line highlighting.

---

## Installation

```bash
git clone https://github.com/yourusername/edit.git
cd edit
make
make install    # Installs to ~/.local/bin
```

To uninstall:

```bash
make uninstall
```

---

## Key Bindings

### File Operations

| Key | Action |
|-----|--------|
| Ctrl+S | Save |
| F12 or Alt+Shift+S | Save As |
| Ctrl+Q | Quit (press 3 times if unsaved changes) |

### Navigation

| Key | Action |
|-----|--------|
| Arrows | Move cursor |
| Ctrl+Arrow | Move by word |
| Home / End | Start / end of line (or segment in wrap mode) |
| Page Up / Page Down | Scroll by screen |
| Ctrl+G | Go to line number |
| Ctrl+] or Alt+] | Jump to matching bracket |

### Selection

| Key | Action |
|-----|--------|
| Shift+Arrow | Extend selection |
| Ctrl+Shift+Arrow | Extend selection by word |
| Ctrl+A | Select all |
| Ctrl+D | Select word, then add cursor at next occurrence |
| Double-click | Select word |
| Triple-click | Select line |

### Editing

| Key | Action |
|-----|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+C | Copy |
| Ctrl+X | Cut |
| Ctrl+V | Paste |
| Alt+K | Delete line |
| Alt+D | Duplicate line |
| Alt+Up / Alt+Down | Move line up / down |
| Tab (with selection) | Indent selected lines |
| Shift+Tab | Outdent selected lines |
| Ctrl+/ or Alt+/ | Toggle line comment |

### Search and Replace

| Key | Action |
|-----|--------|
| Ctrl+F | Enter search mode |
| Ctrl+R | Enter find and replace mode |
| Alt+N or Down | Next match |
| Alt+P or Up | Previous match |
| Alt+C | Toggle case sensitivity |
| Alt+W | Toggle whole word matching |
| Alt+R | Toggle regex mode |
| Tab | Switch between search and replace fields |
| Enter | Replace current match |
| Alt+A | Replace all matches |
| Escape | Cancel search |

### Display

| Key | Action |
|-----|--------|
| F2 | Toggle line numbers |
| F3 | Toggle whitespace visibility |
| F4 | Cycle color column (off / 80 / 120) |
| Shift+F4 | Cycle color column style |
| Alt+Z | Cycle wrap mode (off / word / character) |
| Alt+Shift+Z | Cycle wrap indicator style |

---

## Architecture

The following sections describe the internal design for developers who want to understand or modify the editor.

### The Three-Temperature Model

Opening a large file shouldn't consume proportionally large amounts of memory. The editor addresses this with a thermal model for line storage.

When a file is opened, it's memory-mapped with `mmap()`. Each line in the buffer stores only metadata: an offset into the mapped region and a byte length. No character data is copied. These lines are *cold*—their content exists only in the kernel's page cache.

When a line needs to be displayed or edited, it's *warmed*. The UTF-8 bytes are decoded into an array of cells, each cell holding a codepoint and associated metadata. This decoded form is what the rendering and editing code operates on.

When a line is edited, it becomes *hot*. The mmap content is now stale; the cells are the source of truth. Before saving, all cold lines are warmed (to capture their content before the mmap is invalidated), then the file is written from the cell arrays.

```
COLD   →   mmap-backed, no cells allocated (~24 bytes per line)
WARM   →   cells decoded, ready for display (~24 + 12n bytes, n = characters)
HOT    →   cells edited, mmap content stale (same memory as warm)
```

The practical effect is that opening a 100,000-line file allocates only line metadata until you scroll to a particular region. Lines you never view never consume more than a few bytes each.

### The Cell

Each character is stored as a 12-byte structure:

```c
struct cell {
    uint32_t codepoint;    // Unicode scalar value
    uint16_t syntax;       // Token type for highlighting
    uint8_t  neighbor;     // Word boundary information
    uint8_t  flags;        // Reserved
    uint32_t context;      // Delimiter matching data
};
```

This is larger than a simple character buffer, but the overhead buys several capabilities. Syntax highlighting is computed once when a line is edited, not on every render. Word boundaries are precomputed, making Ctrl+Arrow navigation O(1) per step. Delimiter matching is encoded directly in the cells, eliminating the need to search for matching brackets.

The `flags` field is currently unused, reserved for future features like fold markers or bookmarks.

### The Neighbor Layer

The `neighbor` byte encodes information about word boundaries:

```
Bits 0-2:  Character class (whitespace, letter, digit, underscore, punctuation, bracket, quote, other)
Bits 3-4:  Token position (solo, start, middle, end)
Bits 5-7:  Reserved
```

A character's class determines what it can form a word with. Letters, digits, and underscores form words together; everything else breaks words. The token position indicates whether this character is alone, at the start of a multi-character sequence, in the middle, or at the end.

When you press Ctrl+Right, the cursor doesn't scan forward character by character looking for a word boundary. Instead, it checks the token position of the current cell: if it's END or SOLO, advance to the next cell; if that cell's position is START or SOLO, stop there. The entire operation is a handful of comparisons.

Double-click word selection uses the same data. Find the word boundaries by walking backward to a START or SOLO, forward to an END or SOLO, and the selection range is defined.

### Pair Entanglement

The `context` field links matching delimiters:

```
Bits 0-23:   Pair ID (unique identifier, up to 16 million pairs)
Bits 24-26:  Pair type (parenthesis, bracket, brace, quote, comment)
Bits 27-28:  Pair role (opener or closer)
```

When a file is loaded, the editor scans for delimiters and assigns each matched pair a unique ID. An opening parenthesis and its matching closer share the same pair ID. This makes jumping to the matching bracket trivial: read the pair ID from the current cell, search for another cell with the same ID and opposite role.

Block comments receive the same treatment. The `/*` and `*/` of a multi-line comment share a pair ID, which enables correct syntax highlighting of comments that span hundreds of lines without rescanning from the top of the file.

The 24-bit pair ID field supports up to 16 million unique pairs in a single file—enough for any reasonable codebase.

### Syntax Highlighting

Highlighting is performed per-line in `syntax_highlight_line()`. The algorithm:

1. Check if the line starts with `#` (preprocessor directive)
2. Scan for string literals, handling escape sequences
3. Check pair entanglement to detect block comment regions
4. Scan for line comments (`//`)
5. Match keywords (`if`, `else`, `for`, `while`, etc.)
6. Match type keywords (`int`, `char`, `void`, `struct`, etc.)
7. Detect function calls (identifier immediately followed by `(`)
8. Mark numbers, operators, and brackets

Each cell's `syntax` field is set to the appropriate token type. During rendering, the token type indexes into a color table to determine the foreground color.

Highlighting is recomputed only when a line is edited. Scrolling through a file that hasn't been modified performs no highlighting work—the colors were computed when the lines were first warmed.

### Soft Line Wrapping

Long lines can be displayed in three modes: no wrapping (horizontal scroll only), word wrapping (break at word boundaries), and character wrapping (break anywhere).

Each line caches its wrap points—the column indices where the line should break to fit the current screen width. The cache stores:

```c
uint32_t *wrap_columns;      // Array of segment start columns
uint16_t wrap_segment_count; // Number of visual segments
uint16_t wrap_cache_width;   // Screen width when computed
enum wrap_mode wrap_cache_mode;
```

When wrap mode is enabled, `wrap_columns[0]` is always 0 (the first segment starts at column 0). Subsequent entries indicate where later segments begin. A 200-character line on an 80-column display might have `wrap_columns = {0, 78, 156}` and `wrap_segment_count = 3`.

The cache is invalidated when the line is edited, the terminal is resized, or the wrap mode changes. Recomputation is lazy—a line's wrap points are calculated only when it's about to be rendered.

Word wrapping uses the neighbor layer to find break points. The algorithm walks backward from the maximum width looking for a word boundary, preferring breaks after whitespace, then after punctuation, then at any boundary.

### Undo and Redo

The undo system records operations, not snapshots. Each edit generates an operation record:

```c
struct edit_operation {
    enum edit_operation_type type;  // INSERT_CHAR, DELETE_CHAR, etc.
    uint32_t row, column;           // Position
    uint32_t codepoint;             // For single-character operations
    char *text;                     // For multi-character operations
    size_t text_length;
};
```

Operations are grouped. Rapid keystrokes within one second become a single undo group; a pause longer than one second starts a new group. This means typing "hello" quickly and pressing Ctrl+Z removes all five characters at once, while typing with pauses between letters removes them individually.

Undo applies operations in reverse. An INSERT_CHAR becomes a deletion; a DELETE_CHAR becomes an insertion; an INSERT_NEWLINE rejoins lines; a DELETE_NEWLINE splits them.

The cursor position is recorded at the start and end of each group. Undoing restores the cursor to where it was before the group; redoing restores it to where it was after. This means undo doesn't just restore text—it restores context.

### Multi-Cursor Editing

When you press Ctrl+D on a word, the editor enters multi-cursor mode. The current selection is remembered, and pressing Ctrl+D again searches for the next occurrence, placing an additional cursor there.

```c
struct cursor {
    uint32_t row, column;
    uint32_t anchor_row, anchor_column;  // Selection anchor
    bool has_selection;
};
```

The editor maintains an array of up to 100 cursors. When you type, the character is inserted at every cursor position. The key is processing cursors in reverse document order (bottom to top, right to left) so that insertions don't shift the positions of cursors yet to be processed.

Pressing Escape exits multi-cursor mode, leaving only the primary cursor. There are intentional limitations: newlines can't be inserted in multi-cursor mode, and backspace at the start of a line won't join lines. These restrictions keep the implementation simple while covering the common case of parallel text insertion.

### The Rendering Pipeline

Rendering happens in a single pass, accumulating output in a buffer before flushing to the terminal:

1. Hide the cursor (prevents flicker)
2. Move to home position
3. For each screen row:
   - Clear the line
   - Draw the gutter (line number or wrap indicator)
   - Draw the line content with syntax colors and highlights
   - Handle the color column if enabled
4. Draw the status bar
5. Draw the message bar
6. Position the cursor
7. Show the cursor
8. Flush the buffer with a single `write()` call

The single-write approach minimizes flicker. The terminal receives a complete frame atomically rather than seeing partial updates.

Line content rendering operates in two modes. In segment mode (wrap enabled), it renders cells from a start index to an end index—one segment of a wrapped line. In scroll mode (wrap disabled), it skips cells until reaching the horizontal scroll offset, then renders from there.

### Color and Accessibility

The color scheme is designed for Tritanopia (blue-yellow color blindness) while meeting WCAG 2.1 AA accessibility standards. Every foreground/background combination maintains at least a 4.5:1 contrast ratio.

The design principles:

- **Luminance as primary differentiator** — Colors are distinguishable by brightness alone, not just hue
- **Red-cyan axis** — These colors remain distinct to people with Tritanopia
- **No blue-yellow distinctions** — Critical information is never conveyed through blue vs. yellow

When text appears on a colored background (selection, search highlight), the editor automatically adjusts the foreground color if the original combination would have insufficient contrast:

```c
struct syntax_color color_ensure_contrast(struct syntax_color fg, struct syntax_color bg) {
    double ratio = color_contrast_ratio(fg, bg);
    if (ratio >= 4.5) return fg;  // Already compliant
    // Iteratively adjust toward lighter or darker...
}
```

---

## Extending the Editor

The single-file architecture makes extension straightforward. Common modifications:

**Adding a key binding:** Define a key code in `enum key_code`, detect it in `input_read_key()`, handle it in `editor_process_keypress()`.

**Adding a syntax token:** Add to `enum syntax_token`, define its color in `THEME_COLORS[]`, update `syntax_highlight_line()` to detect and mark it.

**Adding a new mode:** Follow the pattern of search, goto, or save-as: a state struct, enter/exit functions, a key handler that returns true if it consumed the key.

The codebase follows Linux kernel style: tabs for indentation, explicit `struct` keywords, `snake_case` naming, functions named `module_verb_object()`. The CODING_STANDARDS.md file documents the conventions in detail.

---

## Building from Source

Requirements: a C17 compiler and make. The editor compiles on Linux, macOS, and BSD. Dependencies (the utflite UTF-8 library) are bundled.

```bash
make              # Build the editor
make test         # Run UTF-8 validation tests
make clean        # Remove build artifacts
make install      # Install to ~/.local/bin
make uninstall    # Remove installed binary
```

The compiler is invoked with `-Wall -Wextra -pedantic -O2`. One warning is expected: `cell_is_word_end` is marked unused (reserved for future double-click behavior).

---

## License

MIT
