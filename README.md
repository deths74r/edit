# edit

A terminal text editor written in C.

## Contents

- [Philosophy](#philosophy)
- [Features](#features)
- [Installation](#installation)
- [Key Bindings](#key-bindings)
- [Architecture](#architecture)
  - [Module Structure](#module-structure)
  - [The Three-Temperature Model](#the-three-temperature-model)
  - [The Cell](#the-cell)
  - [Word Boundaries](#word-boundaries)
  - [Bracket Matching](#bracket-matching)
  - [Syntax Highlighting](#syntax-highlighting)
  - [Line Wrapping](#line-wrapping)
  - [Undo System](#undo-system)
  - [Multiple Cursors](#multiple-cursors)
  - [Background Processing](#background-processing)
  - [Crash Recovery](#crash-recovery)
  - [Rendering](#rendering)
  - [Error Handling](#error-handling)
  - [Themes](#themes)
- [Extending](#extending)
- [Building](#building)

---

## Philosophy

Every text editor makes a bet about what matters most.

Vim bets on efficiency. It treats editing as a language—verbs act on nouns, modifiers adjust scope—and rewards users who internalize its grammar. The learning curve is steep, but fluent Vim users edit text with remarkable economy of motion.

Emacs bets on extensibility. At its core sits a Lisp interpreter that happens to manipulate text buffers. Given enough configuration, Emacs becomes email client, news reader, IDE, operating system. The editor disappears into whatever you build on top of it.

VS Code bets on the ecosystem. A minimal core hosts thousands of extensions. Language servers provide intelligence. The editor is a platform, and the platform attracts contributors who make it better at everything.

This editor makes a different bet: that simplicity has its own value.

The entire implementation is 19,000 lines of C across 16 modules. No extension API to maintain. No protocol handlers. No embedded scripting language. One person can read and understand the complete source—not abstractly, but actually trace through every function, examine every data structure, comprehend every state transition.

This constraint eliminates features that other editors handle well. Need IntelliSense? Use VS Code. Want org-mode? Use Emacs. Prefer modal editing? Use Vim. This editor won't replace them for those use cases.

What remains is a capable editor for the tasks that don't require an ecosystem: config files, quick scripts, prose, commit messages, code in languages without tooling. It opens instantly, handles Unicode correctly, highlights C syntax, and stays out of your way.

The codebase serves a secondary purpose: it's readable. The architecture demonstrates practical solutions to problems every text editor faces—efficient memory use, responsive UI, correct cursor movement through Unicode text. If you've ever wondered how editors work, this one shows you.

---

## Features

**Unicode done right.** The cursor moves through text the way humans perceive it. A family emoji (👨‍👩‍👧‍👦) is one cursor position, not seven codepoints. Korean syllables stay intact. Combining marks attach to their bases. The implementation handles grapheme clusters per UAX #29, using the bundled utflite library.

**Syntax highlighting.** C and C-like languages: keywords, types, strings (with escape sequences), comments, preprocessor directives, numbers in all bases, function calls, operators, brackets. Highlighting runs once when you edit a line, then caches—scrolling through a thousand lines of unmodified code does zero highlighting work.

**Soft line wrapping.** Three modes: off (horizontal scroll), word wrap (break at boundaries), character wrap (break anywhere). Eight indicator styles show where lines continue. Wrap points are cached and recomputed lazily.

**Incremental search.** Matches highlight as you type—current match in gold, others in blue. Toggle case sensitivity, whole-word matching, and POSIX regex independently. Replace supports backreferences. Search runs on a background thread, so the UI stays responsive even in large files.

**Multiple cursors.** Press Ctrl+D to select the word under the cursor; press again to add a cursor at the next occurrence. All cursors receive keystrokes simultaneously. The implementation processes cursors in reverse document order so insertions don't invalidate positions.

**Bracket matching.** Parentheses, brackets, braces, quotes, and block comments are paired when the file loads. Each pair shares a unique ID stored in the characters themselves. Jump-to-bracket is a lookup, not a search.

**Smart undo.** Records operations, not snapshots, so memory scales with edit count rather than file size. Rapid keystrokes group together; pauses start new groups. Undo feels like rewinding your actions, not stepping through individual characters.

**Mouse support.** Click to position, drag to select, double-click for words, triple-click for lines. Scroll wheel adapts to velocity—slow scrolling for precision, fast for navigation.

**Crash recovery.** A swap file saves your work every 30 seconds when there are unsaved changes. If the editor crashes, the next launch offers to recover. Fatal signals attempt emergency save before terminating. Your work survives.

**Two built-in themes.** Dark and Light, designed for readability. Custom themes load from `~/.edit/themes/`. All colors auto-adjust for WCAG 2.1 AA contrast (4.5:1 ratio).

**Visual aids.** Optional column marker at 80 or 120 characters. Trailing whitespace highlighting. Visible whitespace mode. Cursor line highlighting.

---

## Installation

```bash
git clone https://github.com/yourusername/edit.git
cd edit
make
make install    # installs to ~/.local/bin
```

To uninstall:

```bash
make uninstall
```

---

## Key Bindings

### Files

| Key | Action |
|-----|--------|
| Ctrl+S | Save |
| Ctrl+O | Open file browser |
| F12 | Save As |
| Ctrl+Q | Quit |

### Navigation

| Key | Action |
|-----|--------|
| Arrows | Move cursor |
| Ctrl+Arrow | Move by word |
| Home / End | Line start / end |
| Page Up / Down | Scroll by screen |
| Ctrl+G | Go to line |
| Ctrl+] | Jump to matching bracket |

### Selection

| Key | Action |
|-----|--------|
| Shift+Arrow | Extend selection |
| Ctrl+Shift+Arrow | Extend by word |
| Ctrl+A | Select all |
| Ctrl+D | Select word / add cursor at next |
| Double-click | Select word |
| Triple-click | Select line |

### Editing

| Key | Action |
|-----|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+C / X / V | Copy / Cut / Paste |
| Alt+K | Delete line |
| Alt+D | Duplicate line |
| Alt+Up / Down | Move line |
| Tab / Shift+Tab | Indent / Outdent |
| Ctrl+/ | Toggle comment |

### Search

| Key | Action |
|-----|--------|
| Ctrl+F | Find |
| Ctrl+R | Find and replace |
| Alt+N / P | Next / Previous match |
| Alt+C | Toggle case sensitivity |
| Alt+W | Toggle whole word |
| Alt+R | Toggle regex |
| Alt+A | Replace all |
| Escape | Cancel |

### Display

| Key | Action |
|-----|--------|
| F2 | Toggle line numbers |
| F3 | Toggle whitespace visibility |
| F4 | Cycle column marker |
| F5 | Theme picker |
| Alt+Z | Cycle wrap mode |

---

## Architecture

The architecture optimizes for two things: opening large files without delay, and keeping the UI responsive during edits. These goals drive every significant design decision.

### Module Structure

The codebase is organized into 16 modules, each owning a specific responsibility:

| Module | Purpose |
|--------|---------|
| `types.h` | All shared structures, enums, constants |
| `edit.h` | Master header that includes everything |
| `main.c` | Entry point and main loop |
| `edit.c` | Core editing operations |
| `editor.c` | Global state, status messages, mode handling |
| `buffer.c` | Line storage and manipulation |
| `syntax.c` | Highlighting and character classification |
| `undo.c` | Operation recording and replay |
| `input.c` | Keyboard and mouse input parsing |
| `render.c` | Screen drawing and output buffering |
| `terminal.c` | Raw mode, resize handling |
| `theme.c` | Color management and contrast enforcement |
| `search.c` | Find and replace |
| `worker.c` | Background thread infrastructure |
| `autosave.c` | Swap files and crash recovery |
| `dialog.c` | Theme picker and file browser |
| `clipboard.c` | System clipboard integration |
| `error.c` | Error codes and emergency save |

The total is about 19,000 lines. Dependencies flow from specific to general: `main.c` sits at the top, `types.h` at the bottom. Modules communicate through well-defined interfaces, not shared globals.

### The Three-Temperature Model

Loading a file by reading it entirely into memory works fine for small files. But a 10MB log with 200,000 lines would require decoding every UTF-8 byte and allocating structures for every line—even if you only want to see the last page.

The editor borrows an idea from database buffer management. Files are memory-mapped with `mmap()`. The operating system handles paging; file contents load only when accessed. Each line stores just a pointer into the mapped region and a byte count. These lines are *cold*—they exist, but consume almost no memory.

When you scroll to a line and it needs to render, it *warms up*. The UTF-8 bytes decode into a cell array. Syntax highlighting runs. Word boundaries are computed. Now the line is ready.

When you edit a line, it becomes *hot*. The mmap content is stale; the cell array is authoritative. Before saving, cold lines must warm up so their content can be captured.

```
COLD  →  pointer into mmap, ~24 bytes
WARM  →  decoded cells, ready to display
HOT   →  edited cells, mmap content stale
```

Opening a 100,000-line file allocates only line metadata—about 2.4MB. Scroll to line 50,000 and only the visible lines warm up. Lines you never visit never decode.

### The Cell

A character could be stored as just its codepoint—four bytes. But every operation that cares about syntax, word boundaries, or bracket matching would then recompute that information. The question is where to pay: once during editing, or repeatedly during display and navigation.

The editor pays once:

```c
struct cell {
    uint32_t codepoint;    // Unicode scalar value
    uint16_t syntax;       // Token type for highlighting
    uint8_t  neighbor;     // Character class and word position
    uint8_t  flags;        // Reserved
    uint32_t context;      // Bracket pair ID and type
};
```

Twelve bytes per character. Three times the minimum, but it eliminates repeated work. Scrolling doesn't rehighlight. Word movement doesn't rescan. Bracket jumping doesn't search. The cost is paid when a line warms up or is edited; after that, the information is instant.

### Word Boundaries

Ctrl+Arrow should move by words. The obvious implementation scans characters looking for transitions between word and non-word characters. This is O(n) in distance traveled—fine for short jumps, sluggish when holding the key through a long line.

The `neighbor` byte precomputes word structure:

```
Bits 0-2:  Character class (letter, digit, whitespace, punctuation, etc.)
Bits 3-4:  Position in token (start, middle, end, solo)
```

A character marked "start" begins a word. Marked "end" concludes one. "Solo" means a single-character token. Word movement becomes: if at end or solo, advance one cell. If now at start or solo, stop. No scanning, no loops—just a few comparisons.

This matters because keyboard repeat is fast. Users holding Ctrl+Arrow expect smooth, consistent motion, not a cursor that stutters through long identifiers.

### Bracket Matching

Jump-to-bracket typically searches for the matching delimiter while counting nesting. This is O(n) in the distance between brackets—tolerable for adjacent parens, slow for block comments spanning thousands of lines.

The editor inverts the approach. When a file loads, one pass matches all delimiters and assigns each pair a unique ID:

```
Bits 0-23:   Pair ID (unique, supports 16 million pairs)
Bits 24-26:  Pair type (paren, bracket, brace, quote, comment)
Bits 27-28:  Role (opener or closer)
```

Jump-to-bracket reads the current cell's pair ID, then scans for the cell with the same ID and opposite role. The scan is still O(n) worst case, but typically fast because you're matching a specific ID, not counting nesting.

More importantly, this enables correct block comment highlighting. A `/*` on line 100 and `*/` on line 500 share a pair ID. Rendering line 300 can check whether it's inside a comment without scanning backward.

### Syntax Highlighting

Most editors highlight on every render. For small files and localized changes, this works fine. But it means scrolling costs O(lines × line_length), paid on every frame.

This editor highlights once per edit. Each cell's `syntax` field stores the token type. Rendering reads the field and looks up the color—O(1) per character.

The highlighter is a lexer, not a parser:

1. Lines starting with `#` are preprocessor directives
2. Quoted regions are strings (with escape sequence detection)
3. `/*` to `*/` are block comments (using pair data)
4. `//` to end-of-line are line comments
5. Reserved words are keywords or types
6. Identifiers followed by `(` are function calls
7. Numeric literals in all bases (decimal, hex, octal, binary, float)
8. Everything else is operators or punctuation

This handles C and C-like languages. It doesn't understand context—`int` in a variable name still highlights as a type. But lexer-level accuracy covers 99% of cases, and the implementation stays simple.

### Line Wrapping

Wrapping interacts with everything: cursor movement, selection, mouse clicks, line numbers. The tempting shortcut—split long lines into real line breaks—would destroy the correspondence between file lines and display lines, break numbering, and complicate saving.

Instead, each line caches its wrap points:

```c
uint32_t *wrap_columns;      // Segment start columns
uint16_t wrap_segment_count; // Number of visual lines
uint16_t wrap_cache_width;   // Terminal width when computed
```

A 200-character line on an 80-column screen becomes three segments. Cursor movement checks which segment it's in. Mouse clicks map screen coordinates to segments, then to cell positions. Selection knows to highlight across segment breaks.

The cache invalidates on edit, resize, or mode change. Recomputation is lazy—triggered when a stale line is about to render.

### Undo System

Undo can snapshot the entire buffer at each edit. Simple, but memory scales with file size times edit count. A 10MB file with a thousand edits means 10GB of undo history.

The editor records operations instead:

```c
struct edit_operation {
    enum type type;        // INSERT_CHAR, DELETE_CHAR, INSERT_LINE, etc.
    uint32_t row, column;  // Position
    uint32_t codepoint;    // For character operations
    char *text;            // For multi-character operations
};
```

Undo reverses operations: inserts become deletes, deletes become inserts. Memory scales with edit count, not file size.

Grouping makes undo feel natural. Typing "hello" quickly should undo as "hello", not five separate characters. Operations within one second group together. Pauses start new groups. This matches how typing feels—bursts of activity separated by thought.

### Multiple Cursors

Multi-cursor editing seems to require threading cursor awareness through every operation. In practice, it's simpler: apply the same operation to each cursor in turn.

The trick is ordering. Inserting at column 10 shifts everything after it, including the cursor at column 20. Solution: process cursors in reverse document order—bottom to top, right to left. Later cursors don't shift because earlier ones haven't been processed.

Some operations are disabled with multiple cursors (newline insertion, backspace at line start) to keep the implementation tractable. The common case—inserting or deleting the same text at multiple positions—works perfectly.

### Background Processing

Search in a large file would freeze the UI if run synchronously. The worker module provides a background thread:

```c
struct worker_task {
    enum task_type type;  // TASK_SEARCH, TASK_REPLACE, TASK_AUTOSAVE
    union { ... } data;
};
```

The main thread pushes tasks to a queue. The worker processes them and pushes results back. The main loop polls for results between renders, updating the display as matches arrive.

Search results stream in incrementally. Replace-all previews the count before committing. Autosave writes without blocking typing.

### Crash Recovery

Losing unsaved work is unacceptable. The autosave module defends against it:

1. Every 30 seconds (when modified), write buffer contents to `~/.edit/swap/<filename>.swp`
2. On clean exit, delete the swap file
3. On next launch, if a swap file exists, offer recovery

The swap file writes atomically: create a temp file, write contents, rename over the target. This prevents corruption if the system crashes mid-write.

Fatal signal handlers (SIGSEGV, SIGBUS, etc.) attempt emergency save before terminating. The buffer might be corrupted, but something is better than nothing.

### Rendering

Terminal updates flicker when they show intermediate states—background drawn, then text, then cursor positioned. The solution is batching: build the entire frame in memory, emit it as a single write.

```
1. Hide cursor
2. Move to home position
3. For each row: clear to end, draw line number, draw content
4. Draw status bar
5. Draw message bar
6. Position cursor
7. Show cursor
8. Single write() to terminal
```

The output buffer accumulates escape sequences and text. Only step 8 touches the terminal, which receives a complete frame atomically.

### Error Handling

The editor borrows from the Linux kernel. Functions returning pointers can encode errors in the pointer itself—the top 4095 values of address space are reserved:

```c
void *result = some_operation();
if (IS_ERR(result))
    return PTR_ERR(result);
```

Assertions come in two severities. `WARN_ON()` logs and continues—something unexpected happened, but we can proceed. `BUG_ON()` triggers emergency save and abort—the invariant violation is unrecoverable.

### Themes

Two themes are built in: Dark and Light. Custom themes live in `~/.edit/themes/` as INI files:

```ini
[theme]
name = My Theme
background = 1a1a2e
foreground = eaeaea
keyword = ff79c6
string = f1fa8c
comment = 6272a4
```

Contrast enforcement is automatic. When a foreground color would be illegible against its background (in selections, search highlights, etc.), the editor adjusts it—lightening or darkening until the WCAG 2.1 AA ratio of 4.5:1 is met. Theme authors don't need to understand color theory.

The theme picker shows live preview. Selecting a theme applies it immediately so you can see your code before confirming.

---

## Extending

The modular structure makes extension straightforward:

**New key binding:** Define the key code in `types.h`, detect it in `input.c`, handle it in `edit.c`.

**New syntax token:** Add to `enum syntax_token` in `types.h`, detect it in `syntax.c`, assign colors in theme files.

**New editing mode:** Create a state struct in `types.h`, add enter/exit functions and a key handler that returns true when it consumes a key. Follow the pattern of search mode or goto-line mode.

**New background task:** Define the task type in `worker.h`, handle it in the worker thread's switch statement, process results in the main loop.

The codebase follows Linux kernel style: tabs for indentation, `snake_case` naming, explicit `struct` keywords, functions named `module_verb_object()`. See CODING_STANDARDS.md for complete guidelines.

---

## Building

Requirements: C17 compiler, make, pthreads. Tested on Linux, macOS, and BSD. The UTF-8 library (utflite) is bundled—no external dependencies.

```bash
make              # build
make test         # run tests
make clean        # remove artifacts
make install      # install to ~/.local/bin
make uninstall    # remove
```

The build uses `-Wall -Wextra -pedantic -O2`. A few warnings are expected and documented in CLAUDE.md.

---

## License

GPL-2.0-only
