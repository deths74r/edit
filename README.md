# edit

A minimal terminal text editor written in C.

## Contents

- [The Design](#the-design)
- [Features](#features)
- [Installation](#installation)
- [Key Bindings](#key-bindings)
- [Architecture](#architecture)
  - [Module Overview](#module-overview)
  - [The Three-Temperature Model](#the-three-temperature-model)
  - [The Cell](#the-cell)
  - [The Neighbor Layer](#the-neighbor-layer)
  - [Pair Entanglement](#pair-entanglement)
  - [Syntax Highlighting](#syntax-highlighting)
  - [Soft Line Wrapping](#soft-line-wrapping)
  - [Undo and Redo](#undo-and-redo)
  - [Multi-Cursor Editing](#multi-cursor-editing)
  - [The Rendering Pipeline](#the-rendering-pipeline)
  - [Background Worker Thread](#background-worker-thread)
  - [Crash Recovery](#crash-recovery)
  - [Error Handling](#error-handling)
  - [The Theme System](#the-theme-system)
- [Extending the Editor](#extending-the-editor)
- [Building from Source](#building-from-source)

---

## The Design

Every text editor embodies a theory about what editing should be. Vim believes editing is a language—verbs, nouns, modifiers—and rewards those who become fluent. Emacs believes an editor should be a Lisp environment that happens to edit text. VS Code believes an editor should be a platform that extensions transform into whatever you need.

This editor has a different theory: that a small C codebase, compiled without dependencies, can handle daily text editing tasks while remaining something one person can understand completely. Not understand in the abstract, but actually read—every function, every data structure, every bit of state. The entire implementation fits in roughly 19,000 lines across 16 modules.

The constraint shapes everything. A plugin system means an API, and an API means backwards compatibility concerns, documentation, version negotiation. Skip all that. Language server protocol support means async communication, JSON parsing, protocol state machines. Let a different tool handle that. Split panes mean layout algorithms, focus management, per-pane state. The terminal multiplexer already solved this problem.

What remains is an editor that does exactly what it appears to do. It handles Unicode correctly—grapheme clusters, combining marks, double-width CJK characters—using utflite, a UTF-8 library that grew out of this project. It highlights C syntax, because the editor is written in C and should be pleasant for editing itself. It wraps long lines, because horizontal scrolling is tedious. It supports multiple cursors, because some edits are naturally parallel. It has undo that groups rapid keystrokes together, because that's how typing feels. It searches with regular expressions, because pattern matching is fundamental to working with text.

The modular architecture keeps related code together while maintaining clear boundaries between subsystems. Each module owns its data and exposes a minimal interface.

---

## Features

The editor handles **Unicode** as humans perceive it. The cursor moves by grapheme clusters, not bytes or codepoints—a flag emoji is one cursor movement, not eight. CJK characters occupy their proper double-width cells. Combining marks stay attached to their base characters. The implementation uses the bundled utflite library for UAX #29 grapheme segmentation and wcwidth calculations.

**Syntax highlighting** covers C and C-like languages: keywords, types, strings with escape sequences, comments (line and block), preprocessor directives, numbers (decimal, hex, octal, binary, floats), operators, brackets, and function calls. Highlighting is computed once when a line is edited, then cached—scrolling through an unmodified file does no highlighting work.

**Line wrapping** operates in three modes. Off means long lines scroll horizontally. Word wrap breaks at word boundaries, using precomputed token information to find appropriate break points. Character wrap breaks anywhere when necessary. Eight visual indicators show where lines continue.

**Search** is incremental—matches highlight as you type. The current match shows in gold, others in blue. Case sensitivity, whole-word matching, and POSIX extended regular expressions can be toggled independently. Replace mode supports backreferences (`\1`, `\2`) in replacement text. Replace-all operates as a single undo group. Search runs asynchronously via the background worker thread, keeping the UI responsive during large file searches.

**Multiple cursors** emerge from repeated Ctrl+D presses: the first selects the word under the cursor, subsequent presses add cursors at each next occurrence. All cursors receive typed characters simultaneously. The implementation processes cursors in reverse document order so insertions don't shift positions of cursors yet to be processed.

**Bracket matching** tracks paired delimiters across the entire buffer: parentheses, brackets, braces, quotes, and block comments. Each pair shares a unique ID stored directly in the character cells. Jumping to the match is a lookup, not a search.

**Undo** records operations, not snapshots. Rapid keystrokes within one second group together; pauses start new groups. Cursor position is restored alongside text changes. Memory consumption scales with edit count, not file size.

**Mouse** support includes click positioning, drag selection, double-click for words, triple-click for lines, and scroll wheel with velocity-based adaptive speed—slow scrolling for precision, fast for navigation.

**File dialogs** provide visual navigation: a file browser for opening files, a theme picker with live preview. Both support mouse and keyboard.

**Two built-in themes** ship with the editor—Dark and Light—with support for custom themes in `~/.edit/themes/`. All enforce WCAG 2.1 AA contrast requirements—the editor adjusts colors automatically when necessary.

**Crash recovery** automatically saves your work to a swap file every 30 seconds when there are unsaved changes. If the editor crashes or your system loses power, the next time you open the file, you'll be prompted to recover your work.

**Visual aids** include an optional column marker at 80 or 120 characters (five display styles), trailing whitespace highlighting, visible whitespace mode (tabs and spaces rendered as symbols), and cursor line highlighting.

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
| Ctrl+Q | Quit (prompts if unsaved changes) |

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

The architecture reflects a particular set of priorities: fast startup, predictable memory use, and the ability to open large files without paying for lines you never look at. The modular design keeps the codebase navigable while maintaining these properties.

### Module Overview

The editor is organized into 16 modules, each with a clear responsibility:

| Module | Lines | Purpose |
|--------|------:|---------|
| `types.h` | 1,145 | Shared structs, enums, and constants |
| `edit.h` | 69 | Master header including all modules |
| `edit.c` | 6,277 | Core editing operations and keypress handling |
| `main.c` | 141 | Entry point, initialization, main loop |
| `terminal.c/h` | 314 | Raw mode, resize handling, mouse tracking |
| `theme.c/h` | 1,933 | Colors, themes, contrast enforcement, config |
| `buffer.c/h` | 893 | Line and buffer operations |
| `syntax.c/h` | 945 | Syntax highlighting, neighbor layer, pair matching |
| `undo.c/h` | 781 | Undo/redo history with operation grouping |
| `input.c/h` | 448 | Key and mouse input parsing |
| `render.c/h` | 477 | Screen refresh, output buffering |
| `worker.c/h` | 569 | Background thread for async operations |
| `search.c/h` | 963 | Find/replace with async search |
| `autosave.c/h` | 788 | Crash recovery via swap files |
| `dialog.c/h` | 1,394 | Theme picker, file browser |
| `clipboard.c/h` | 452 | System clipboard integration |
| `editor.c/h` | 965 | Editor state, status messages, dialogs |
| `error.c/h` | 407 | Linux kernel-style error handling |

Dependencies flow downward: `main.c` depends on everything, `edit.c` depends on most modules, leaf modules like `terminal.c` depend only on `types.h` and standard libraries.

### The Three-Temperature Model

The obvious way to load a file is to read it into memory and parse it into whatever data structures you need. This works fine for small files but scales poorly. A 10MB log file with 200,000 lines would require allocating 200,000 line structures and decoding every UTF-8 byte, even if you only want to look at the last few lines.

The editor uses a different approach borrowed from database buffer management. When a file is opened, it's memory-mapped with `mmap()`. The kernel handles paging—file contents are loaded only when accessed. Each line stores just a pointer into the mapped region and a byte count. These lines are *cold*.

When you scroll to a line and it needs to be displayed, it's *warmed*. The UTF-8 bytes are decoded into cells, syntax is highlighted, neighbor data is computed. Now the line is ready for rendering and editing.

When you edit a line, it becomes *hot*. The mmap content is stale; the cell array is the source of truth. Before saving, cold lines must be warmed to capture their content before the mmap is unmapped.

```
COLD   →   mmap-backed, no cells allocated (~24 bytes per line)
WARM   →   cells decoded, ready for display (~24 + 12n bytes, n = characters)
HOT    →   cells edited, mmap content stale (same memory as warm)
```

The practical result: opening a 100,000-line file allocates only line metadata. Scroll to line 50,000 and only the visible lines warm up. Lines you never view never consume more than 24 bytes each.

### The Cell

Each character could be stored as a codepoint—four bytes. But then every operation that cares about syntax, word boundaries, or bracket matching would need to recompute that information. The question is where to pay the cost: once during editing, or repeatedly during every scroll, every cursor movement, every search.

The editor pays once:

```c
struct cell {
    uint32_t codepoint;    // Unicode scalar value
    uint16_t syntax;       // Token type for highlighting
    uint8_t  neighbor;     // Word boundary information
    uint8_t  flags;        // Reserved
    uint32_t context;      // Delimiter matching data
};
```

Twelve bytes per character is three times the minimum, but it eliminates repeated work. Scrolling doesn't re-highlight. Ctrl+Arrow doesn't rescan for word boundaries. Jump-to-matching-bracket doesn't search the file. The cost is paid when a line is edited; after that, the information is immediately available.

### The Neighbor Layer

Word movement (Ctrl+Arrow) and word selection (double-click) need to know where words begin and end. The naive implementation scans characters looking for transitions between word and non-word characters. This is O(n) in the distance moved, which feels fine until you hold Ctrl+Right and watch the cursor stutter through a long line.

The neighbor layer precomputes this information. The `neighbor` byte encodes:

```
Bits 0-2:  Character class (whitespace, letter, digit, underscore, punctuation, bracket, quote, other)
Bits 3-4:  Token position (solo, start, middle, end)
Bits 5-7:  Reserved
```

The token position is the key insight. A character marked START is at the beginning of a word. A character marked END is at the end. SOLO means a single-character token. MIDDLE is everything else.

Ctrl+Right becomes: check current position; if END or SOLO, advance one cell; check new position; if START or SOLO, stop. Total cost: a few comparisons and memory accesses. No scanning, no loops over the line content.

### Pair Entanglement

Jump-to-matching-bracket is conceptually simple: find the opener, search for the closer, counting nested pairs. But this is O(n) in the distance between brackets, and for block comments in large files, that distance can be thousands of lines.

The editor inverts the problem. When a file is loaded (or when brackets are edited), a single pass matches all delimiters and assigns each pair a unique ID:

```
Bits 0-23:   Pair ID (unique identifier, up to 16 million pairs)
Bits 24-26:  Pair type (parenthesis, bracket, brace, quote, comment)
Bits 27-28:  Pair role (opener or closer)
Bits 29-31:  Reserved
```

Now jump-to-bracket is: read the pair ID from the current cell; scan for a cell with the same ID and opposite role. The scan is still O(n) in the worst case, but typically much faster because you're looking for a specific ID, not counting nesting levels.

More importantly, this enables correct syntax highlighting for block comments. A `/*` on line 100 and its `*/` on line 500 share a pair ID. Highlighting line 300 can check whether it's inside a comment by examining the pair data, without scanning backward to find the comment start.

### Syntax Highlighting

Highlighting could run on every render. Most editors do this, and it's fine when the visible region is small and changes are localized. But it means scrolling has a cost proportional to screen height times line length, paid sixty times per second.

The editor highlights once per edit. Each cell's `syntax` field stores the token type—keyword, string, comment, etc. Rendering just reads the field and indexes into the theme's color table.

The algorithm is straightforward:

1. Preprocessor directives: lines starting with `#`
2. Strings: quoted regions, with escape sequence detection
3. Block comments: using pair entanglement to detect regions
4. Line comments: `//` to end of line
5. Keywords: `if`, `else`, `for`, `while`, `return`, etc.
6. Types: `int`, `char`, `void`, `struct`, `uint32_t`, etc.
7. Function calls: identifier immediately followed by `(`
8. Numbers: decimal, hex, octal, binary, floats with suffixes
9. Operators and brackets: remaining punctuation

This is a lexer, not a parser. It doesn't understand that `int` in a variable name isn't a type. But lexer-level highlighting handles 99% of cases correctly, and the implementation is a few hundred lines rather than a few thousand.

### Soft Line Wrapping

Line wrapping interacts with almost everything: cursor movement, mouse clicks, scrolling, line numbers, selection rendering. The temptation is to convert wrapped lines to actual line breaks and let the rest of the code handle them normally. This would be a mistake—it destroys the correspondence between file lines and buffer lines, breaks line numbers, and complicates saving.

Instead, each line caches where it would break at the current screen width:

```c
uint32_t *wrap_columns;      // Array of segment start columns
uint16_t wrap_segment_count; // Number of visual segments
uint16_t wrap_cache_width;   // Screen width when computed
enum wrap_mode wrap_cache_mode;
```

A 200-character line on an 80-column display becomes three segments: columns 0-77, 78-155, 156-199. The cache stores `{0, 78, 156}` with count 3.

Cursor movement checks which segment it's in and moves within that segment for horizontal motion, between segments for vertical. Mouse clicks map screen coordinates to segments, then to cell positions. Selection rendering knows to highlight across segment breaks within a single line.

The cache is invalidated on edit, resize, or wrap mode change. Recomputation is lazy—triggered when a line is about to render and its cache is stale.

### Undo and Redo

There are two approaches to undo: snapshot the entire buffer at each edit point, or record the operations themselves. Snapshots are simpler—undo just restores the previous snapshot—but memory use scales with file size times edit count. For a 10MB file with a thousand edits, that's 10GB of undo history.

The editor records operations:

```c
struct edit_operation {
    enum edit_operation_type type;  // INSERT_CHAR, DELETE_CHAR, INSERT_NEWLINE, etc.
    uint32_t row, column;           // Position
    uint32_t codepoint;             // For single-character operations
    char *text;                     // For multi-character operations
    size_t text_length;
};
```

Memory scales with edit count, not file size. Undo reverses operations: INSERT_CHAR becomes delete, DELETE_CHAR becomes insert, INSERT_NEWLINE rejoins, DELETE_NEWLINE splits.

Grouping matters for usability. Typing "hello" as five rapid keystrokes should undo as "hello", not as five individual characters. The editor groups operations that occur within one second. A pause starts a new group.

### Multi-Cursor Editing

Multi-cursor seems like a feature that requires pervasive changes—every editing operation needs to know about multiple cursors. In practice, it's simpler: process each cursor in turn, applying the same operation.

The trick is ordering. If you have cursors at columns 10 and 20, and you insert a character at each, inserting at column 10 first shifts the second cursor to column 21. The solution is to process cursors in reverse document order: bottom to top, right to left.

### The Rendering Pipeline

Flicker happens when the terminal shows intermediate states during updates. The solution is batching: build the entire frame in memory, then send it as a single write.

```
1. Hide cursor
2. Move to home
3. For each row: clear, draw gutter, draw content
4. Draw status bar
5. Draw message bar
6. Position cursor
7. Show cursor
8. Flush with single write()
```

The output buffer accumulates escape sequences and text. Only step 8 touches the terminal. The terminal receives a complete frame atomically.

### Background Worker Thread

Long-running operations like searching large files would freeze the UI if run synchronously. The worker module provides a background thread that processes tasks asynchronously:

```c
struct worker_task {
    enum task_type type;      // TASK_SEARCH, TASK_REPLACE, TASK_AUTOSAVE, etc.
    union { ... } data;       // Task-specific parameters
};
```

The main thread pushes tasks to a queue. The worker thread processes them and pushes results back. The main loop polls for results between renders, keeping the UI responsive.

Search results stream in as they're found, highlighting matches incrementally. Replace-all previews the change count before committing. Autosave writes to disk without blocking typing.

### Crash Recovery

Losing unsaved work to a crash is unacceptable. The autosave module provides crash recovery:

1. Every 30 seconds (if modified), save buffer contents to `~/.edit/swap/<filename>.swp`
2. On clean exit, delete the swap file
3. On next open, if a swap file exists, offer to recover

The swap file is written atomically: write to a temp file, then rename. This prevents corrupt swap files if the system crashes mid-write.

Fatal signal handlers (SIGSEGV, SIGBUS, etc.) attempt emergency save before terminating. The goal is that no crash loses unsaved work.

### Error Handling

The editor borrows idioms from the Linux kernel. The `error.h` header provides:

**ERR_PTR**: Functions returning pointers can encode error codes in the pointer value itself. The top 4095 values of the address space are reserved for errors.

```c
void *ptr = edit_malloc(size);
if (IS_ERR(ptr))
    return (int)PTR_ERR(ptr);
```

**BUG/WARN Macros**: Assertions with different severity. `WARN_ON()` logs and continues. `BUG_ON()` triggers emergency save and abort.

**Emergency Save**: Fatal errors trigger signal handlers that save buffer contents before terminating.

### The Theme System

Two themes are built in—Dark and Light—providing good defaults without requiring configuration. Custom themes can be added to `~/.edit/themes/` using INI format:

```ini
[theme]
name = My Theme
background = 1a1a2e
foreground = eaeaea
keyword = ff79c6
string = f1fa8c
```

Contrast enforcement is automatic. When a foreground color would have insufficient contrast against a background, the editor adjusts it until WCAG 2.1 AA's 4.5:1 ratio is achieved.

The theme picker provides live preview—selecting a theme applies it immediately so you can see how your code looks before confirming.

---

## Extending the Editor

The modular architecture makes extension straightforward:

**Adding a key binding:** Define a key code in `types.h`, detect it in `input.c`, handle it in `edit.c`.

**Adding a syntax token:** Add to `enum syntax_token` in `types.h`, define its color in theme files, update `syntax.c` to detect and mark it.

**Adding a new mode:** Follow the pattern of search or goto: a state struct in `types.h`, enter/exit functions, a key handler that returns true if it consumed the key.

**Adding a background task:** Define the task type in `worker.h`, handle it in the worker thread switch statement, process results in the main loop.

The codebase follows Linux kernel style: tabs for indentation, explicit `struct` keywords, `snake_case` naming, functions named `module_verb_object()`. See CODING_STANDARDS.md for details.

---

## Building from Source

Requirements: a C17 compiler, make, and pthreads. The editor compiles on Linux, macOS, and BSD. The utflite UTF-8 library is bundled.

```bash
make              # Build the editor
make test         # Run UTF-8 validation tests
make clean        # Remove build artifacts
make install      # Install to ~/.local/bin and themes to ~/.edit/themes
make uninstall    # Remove installed binary and themes
```

The compiler is invoked with `-Wall -Wextra -pedantic -O2`. Expected warnings are documented in CLAUDE.md.

---

## License

MIT
