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
  - [Error Handling](#error-handling)
  - [The Theme System](#the-theme-system)
- [Extending the Editor](#extending-the-editor)
- [Building from Source](#building-from-source)

---

## The Design

Most terminal editors fall into two camps. The first—nano, micro, and their kin—offer simplicity at the cost of power. The second—vim, emacs, kakoune—offer power at the cost of a learning curve that resembles a cliff face. Both camps assume you'll eventually want plugins, configuration files, and an ecosystem of extensions.

This editor makes a different bet: that a small C codebase, compiled without dependencies beyond libc, can provide enough functionality for daily use while remaining small enough to understand completely. The implementation spans roughly 12,000 lines across three source files. There are no plugins, no runtime dependencies. What you compile is what you get.

The feature set reflects this philosophy. Full Unicode support, because text in 2025 is Unicode. C syntax highlighting, because the editor is written in C and should be able to edit itself pleasantly. Soft line wrapping, because modern displays are wide and horizontal scrolling is tedious. Multiple cursors, because some edits are naturally parallel. Undo with automatic grouping, because mistakes happen. Find and replace with regex support, because pattern matching is fundamental to text editing. A theme system with 49 built-in themes, because aesthetics matter.

What's deliberately absent is equally telling. No plugin system, because plugins require an API, and APIs require maintenance. No language server protocol support, because that's a different tool's job. No split panes, because a terminal multiplexer handles that better.

The result is an editor that starts instantly, uses minimal memory, and does exactly what it appears to do. The entire state of the program lives in a single global struct. If something breaks, there's one place to look.

---

## Features

The editor provides:

- **Full Unicode support** — UTF-8 encoding, grapheme cluster navigation, proper display widths for CJK characters and emoji. The cursor moves by what humans perceive as characters, not by bytes or codepoints.

- **C syntax highlighting** — Keywords, types, strings, comments, preprocessor directives, function calls, operators, numbers, and escape sequences are colored distinctly. Highlighting is computed once per edit, not on every render.

- **Soft line wrapping** — Three modes: no wrapping (horizontal scroll), word wrap (break at word boundaries), and character wrap (break anywhere). Eight wrap indicator styles available.

- **Find and replace** — Incremental search with real-time highlighting. Case sensitivity, whole-word matching, and POSIX extended regular expressions with backreference support in replacements.

- **Multiple cursors** — Select a word with Ctrl+D, press again to add a cursor at the next occurrence. Type to insert at all cursors simultaneously. Supports up to 100 concurrent cursors.

- **Bracket matching** — Paired delimiters are tracked across the entire buffer. Jump between matching brackets with Ctrl+]. Supports parentheses, brackets, braces, quotes, and block comments.

- **Undo and redo** — Full history with automatic grouping. Rapid keystrokes within one second are grouped together; pauses create new undo points. Cursor position is restored on undo/redo.

- **Mouse support** — Click to position cursor, drag to select, double-click for word selection, triple-click for line selection. Scroll wheel with adaptive velocity-based scrolling.

- **File dialogs** — Visual file browser for opening files, theme picker with live preview. Mouse and keyboard navigation.

- **Theme system** — 49 built-in themes plus support for custom themes in `~/.edit/themes/`. WCAG 2.1 AA compliant contrast ratios.

- **Visual aids** — Optional color column at 80 or 120 characters with five display styles, trailing whitespace highlighting, visible whitespace mode, cursor line highlighting.

---

## Installation

```bash
git clone https://github.com/yourusername/edit.git
cd edit
make
make install    # Installs to ~/.local/bin and themes to ~/.edit/themes
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
| Ctrl+O | Open file dialog |
| F12 or Alt+Shift+S | Save As |
| Ctrl+Q | Quit (press 3 times if unsaved changes) |

### Navigation

| Key | Action |
|-----|--------|
| Arrows | Move cursor |
| Ctrl+Arrow | Move by word |
| Home / End | Start / end of line |
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
| Enter | Replace current match and find next |
| Alt+A | Replace all matches |
| Escape | Cancel search |

### Display

| Key | Action |
|-----|--------|
| F2 | Toggle line numbers |
| F3 | Toggle whitespace visibility |
| F4 | Cycle color column (off / 80 / 120) |
| Shift+F4 | Cycle color column style |
| F5 or Ctrl+T | Open theme picker |
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

The practical effect is that opening a 100,000-line file allocates only line metadata until you scroll to a particular region. Lines you never view never consume more than their metadata overhead.

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
Bits 29-31:  Reserved
```

When a file is loaded, the editor scans for delimiters and assigns each matched pair a unique ID. An opening parenthesis and its matching closer share the same pair ID. This makes jumping to the matching bracket trivial: read the pair ID from the current cell, search for another cell with the same ID and opposite role.

Block comments receive the same treatment. The `/*` and `*/` of a multi-line comment share a pair ID, which enables correct syntax highlighting of comments that span hundreds of lines without rescanning from the top of the file.

The 24-bit pair ID field supports up to 16 million unique pairs in a single file—enough for any reasonable codebase.

### Syntax Highlighting

Highlighting is performed per-line. The algorithm:

1. Check if the line starts with `#` (preprocessor directive)
2. Scan for string literals, handling escape sequences
3. Check pair entanglement to detect block comment regions
4. Scan for line comments (`//`)
5. Match keywords (`if`, `else`, `for`, `while`, `return`, etc.)
6. Match type keywords (`int`, `char`, `void`, `struct`, `uint32_t`, etc.)
7. Detect function calls (identifier immediately followed by `(`)
8. Mark numbers (decimal, hex, floats with suffixes), operators, and brackets

Each cell's `syntax` field is set to one of eleven token types. During rendering, the token type indexes into the active theme's color table to determine the foreground color.

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
    enum edit_operation_type type;  // INSERT_CHAR, DELETE_CHAR, INSERT_NEWLINE, DELETE_NEWLINE, DELETE_TEXT
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

### Error Handling

The editor uses a Linux kernel-inspired error handling system designed to prevent data loss. The infrastructure lives in `error.h` and `error.c`, providing:

**ERR_PTR System** — Functions that return pointers can encode error codes in the pointer value itself, eliminating the need for separate out-parameters:

```c
void *ptr = edit_malloc(size);
if (IS_ERR(ptr))
    return (int)PTR_ERR(ptr);  // Propagate error code
```

**Checked Functions** — Core operations have `_checked` variants that return error codes instead of crashing. Editor commands use these and display errors in the status bar:

```c
int ret = buffer_insert_cell_at_column_checked(&buffer, row, col, codepoint);
if (ret) {
    editor_set_status_message("Insert failed: %s", edit_strerror(ret));
    return;
}
```

**Emergency Save** — Fatal errors trigger `emergency_save()`, which writes buffer contents to a recovery file before terminating. Signal handlers for SIGSEGV, SIGBUS, SIGABRT, SIGFPE, and SIGILL invoke this automatically.

**BUG/WARN Macros** — Invariant violations are caught with `BUG_ON()` (fatal, triggers emergency save) and `WARN_ON()` (logs but continues). These serve as runtime assertions for conditions that indicate programming errors rather than recoverable failures.

The error handling philosophy is defense in depth: editor commands show user-friendly messages for recoverable errors (out of memory during paste), while internal invariant violations trigger emergency save to preserve work. The goal is that no crash should lose unsaved edits.

### The Theme System

The editor includes 49 built-in themes and supports custom themes loaded from `~/.edit/themes/`.

Each theme defines over 30 colors:

```c
struct theme {
    char *name;
    struct syntax_color background, foreground;
    struct syntax_color line_number, line_number_active;
    struct syntax_color status_bg, status_fg;
    struct syntax_color message_bg, message_fg;
    struct syntax_color selection, search_match, search_current;
    struct syntax_color cursor_line, whitespace, trailing_ws;
    struct syntax_color color_column, color_column_line;
    struct syntax_color dialog_bg, dialog_fg;
    struct syntax_color dialog_header_bg, dialog_header_fg;
    struct syntax_color dialog_footer_bg, dialog_footer_fg;
    struct syntax_color dialog_highlight_bg, dialog_highlight_fg;
    struct syntax_color syntax[11];      // Per-token colors
    struct syntax_color syntax_bg[11];   // Optional token backgrounds
    bool syntax_bg_set[11];
};
```

Custom themes use INI format with hex color values:

```ini
[theme]
name = My Theme
background = 1a1a2e
foreground = eaeaea
keyword = ff79c6
string = f1fa8c
```

The theme picker (F5 or Ctrl+T) provides live preview—selecting a theme immediately applies it so you can see how your code looks before confirming.

All themes enforce WCAG 2.1 AA contrast requirements. The editor automatically adjusts foreground colors when they would have insufficient contrast against backgrounds (selection, search highlights), iteratively lightening or darkening until the 4.5:1 ratio is achieved.

---

## Extending the Editor

The architecture makes extension straightforward. Common modifications:

**Adding a key binding:** Define a key code in `enum key_code`, detect it in `input_read_key()`, handle it in `editor_process_keypress()`.

**Adding a syntax token:** Add to `enum syntax_token`, define its color in theme files, update `syntax_highlight_line()` to detect and mark it.

**Adding a new mode:** Follow the pattern of search, goto, or save-as: a state struct, enter/exit functions, a key handler that returns true if it consumed the key.

The codebase follows Linux kernel style: tabs for indentation, explicit `struct` keywords, `snake_case` naming, functions named `module_verb_object()`. The CODING_STANDARDS.md file documents the conventions in detail.

---

## Building from Source

Requirements: a C17 compiler and make. The editor compiles on Linux, macOS, and BSD. The utflite UTF-8 library is bundled.

The source consists of three files:
- `src/edit.c` — Main editor implementation (~11,800 lines)
- `src/error.h` — Error handling infrastructure (~320 lines)
- `src/error.c` — Error string conversion (~70 lines)

```bash
make              # Build the editor
make test         # Run UTF-8 validation tests
make clean        # Remove build artifacts
make install      # Install to ~/.local/bin and themes to ~/.edit/themes
make uninstall    # Remove installed binary and themes
```

The compiler is invoked with `-Wall -Wextra -pedantic -O2`. The build should complete with no warnings.

---

## License

MIT
