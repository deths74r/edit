# edit

A terminal text editor in a single C file. Fast, Unicode-aware, no runtime dependencies.

`edit` is a ~3000-line C23 text editor built for developers who want something
between `nano` and `vim` -- readable source, real Unicode support, and enough
features to edit code comfortably. It uses the
[gstr](https://github.com/eedmonds/gstr) library for grapheme cluster
segmentation so the cursor never lands inside a multi-codepoint character.

**Current release: v0.1.3** | gstr v3.0.0 | MIT License

## Features

- **Single-file implementation** -- the entire editor is `edit.c` plus the
  header-only `gstr.h`. No build system magic, no generated files.

- **Full Unicode / grapheme-aware editing** -- cursor movement, insertion,
  deletion, and rendering all operate on grapheme cluster boundaries via gstr.
  CJK wide characters, emoji, ZWJ sequences, and regional indicators display
  and navigate correctly.

- **mmap lazy loading** -- files are memory-mapped on open. Lines start COLD
  (only byte offsets stored), become WARM when first decoded into cells, and
  go HOT on edit. Opening a multi-megabyte file is near-instant because only
  visible lines pay the decode cost.

- **Syntax highlighting** -- C/C++ aware: keywords, type keywords, strings,
  numbers, single-line and multi-line comments. Highlight state is stored
  inline in each cell, so rendering never needs a separate pass.

- **6 color themes (24-bit RGB)** -- Cyberpunk, Nightwatch, Daywatch, Tokyo
  Night, Akira, Tokyo Cyberpunk. Cycle with Alt-T.

- **Mouse support** -- SGR mouse reporting: click to position cursor, scroll
  wheel with acceleration (rapid scrolling ramps up to 10x, pausing resets).

- **Incremental search** -- results update on each keypress, matches
  highlighted inline, arrow keys navigate forward/backward with wrap-around.

- **Live terminal resize** -- SIGWINCH handler re-queries terminal dimensions,
  clamps cursor, and redraws immediately. No manual refresh needed.

- **Toggle line numbers** -- gutter width adapts to the file's line count.

- **poll()-based main loop** -- non-blocking input, efficient paste handling,
  single `write()` per frame to avoid flicker.

## Build

Requires GCC (or any C23-capable compiler) and a POSIX system. The only
compile-time dependency is `gstr.h`, included as a git submodule under
`lib/gstr/`.

```
git clone --recurse-submodules https://github.com/eedmonds/edit.git
cd edit
make              # debug build (-g -O0)
make release      # optimized build (-O2)
make clean        # remove artifacts
make test         # build and run tests
make lint         # check for stray control characters
```

If you already cloned without `--recurse-submodules`:

```
git submodule update --init
```

## Usage

```
./edit [filename]
```

Opens the given file or starts with an empty buffer. The status bar shows the
file name, modification state, line count, file type, and cursor position.

On startup, the message bar displays the keybinding reference:

```
Alt: S=save Q=quit F=find G=goto N=lines T=theme HJKL=move
```

## Keybindings

### File Operations

| Key | Action |
|-----|--------|
| Alt-S | Save |
| Alt-Shift-S | Save As (prompts for filename) |
| Alt-Q | Quit (prompts if unsaved changes) |

### Navigation

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor (grapheme-aware left/right) |
| Alt-H/J/K/L | Move cursor (vim-style) |
| Home / End | Start / end of line |
| Page Up / Page Down | Scroll by one screenful |
| Alt-G | Jump to line number |
| Mouse click | Position cursor |
| Scroll wheel | Scroll with acceleration |

### Editing

| Key | Action |
|-----|--------|
| Backspace / Ctrl-H | Delete character before cursor |
| Delete | Delete character at cursor |
| Enter | Insert newline |
| Any printable key | Insert character |

### Search and Display

| Key | Action |
|-----|--------|
| Alt-F | Incremental search (arrow keys navigate matches) |
| Alt-T | Cycle color theme |
| Alt-N | Toggle line numbers |
| F11 | Show version |
| Escape | Cancel current prompt |

## Architecture

### Cell Model

The editor represents text as a grid of cells rather than raw byte buffers. Each codepoint occupies one `struct cell`, which carries syntax metadata inline so the renderer never needs a separate highlight buffer:

```c
struct cell {
    uint32_t codepoint;   // Unicode codepoint
    uint16_t syntax;      // highlight category (HL_NORMAL, HL_KEYWORD1, etc.)
    uint8_t  neighbor;    // word boundary info (reserved, zeroed)
    uint8_t  flags;       // rendering flags (reserved, zeroed)
    uint32_t context;     // pair matching ID (reserved, zeroed)
};
```

Each line is a dynamic array of cells with a doubling growth strategy (starting at 128, doubled when space runs out):

```c
struct line {
    struct cell *cells;
    uint32_t cell_count;
    uint32_t cell_capacity;
    int      line_index;      // zero-based line number in file
    int      open_comment;    // multi-line comment state
    size_t   mmap_offset;     // byte offset in mmap region
    uint32_t mmap_length;     // byte length in mmap (excluding newline)
    int      temperature;     // COLD, WARM, or HOT
};
```

The reserved fields (`neighbor`, `flags`, `context`) exist for planned features like bracket matching and word-level operations.

### Line Temperature

Lines use a three-level temperature system for lazy loading from mmap:

- **COLD** -- Only the mmap byte offset and length are stored. No cell array allocated. This is the initial state for every line when a file is opened.
- **WARM** -- Cells have been decoded from the mmap bytes. The mmap region is still valid backing storage.
- **HOT** -- The line has been mutated (insert, delete, join). The cell array is the source of truth; the mmap content is stale.

Transitions are one-way: COLD -> WARM -> HOT. Any function that touches a line's cells calls `line_ensure_warm()` first. Opening a large file is nearly instant -- only lines that scroll into the viewport or are edited pay the UTF-8 decode cost.

### Input Events

All terminal input flows through a single path. The main loop calls `poll()` on stdin, then `input_buffer_fill()` reads bytes into a ring buffer. The decoder `terminal_decode_key()` drains this buffer one event at a time, returning by value:

```c
struct input_event {
    int key;       // key code, Unicode codepoint, or special key enum
    int mouse_x;   // column (zero for non-mouse events)
    int mouse_y;   // row (zero for non-mouse events)
};
```

The decoder handles single ASCII bytes, multi-byte UTF-8, CSI escape sequences (arrow keys, Home/End, Page Up/Down, F11, SGR mouse), and Alt+key combinations. The main loop dispatches based on editor mode (normal, prompt, confirm).

### Rendering Pipeline

Each frame is assembled in a heap-allocated append buffer and flushed to stdout with a single `write()` call:

1. **`editor_scroll()`** -- Adjusts viewport so the cursor stays visible.
2. **`editor_draw_rows()`** -- Renders lines with line numbers, syntax colors, tab expansion, and grapheme-aware UTF-8 encoding. Iterates by grapheme cluster, not by cell.
3. **`editor_draw_status_bar()`** -- Filename, dirty flag, line count, cursor position.
4. **`editor_draw_message_bar()`** -- Temporary status messages (auto-clear after 5 seconds).
5. **Single `write()` flush** -- Cursor hidden during drawing to eliminate flicker.

### Error Handling

- **Return values.** Functions that can fail return `int`: 0 on success, negative `errno` on failure.
- **`goto` cleanup.** Functions acquiring multiple resources use a single `out:` label, releasing in reverse order.
- **OOM is fatal.** Allocation failures call `terminal_die()`, which restores the terminal and exits.
- **I/O errors are non-fatal.** File open/save failures set a status bar message and return the error code.

### Grapheme Handling

The cell model stores one codepoint per cell, but multi-codepoint grapheme clusters (flags, ZWJ sequences, combining marks) span multiple adjacent cells. Two functions bridge between cell indices and grapheme boundaries:

- **`cursor_next_grapheme()`** -- Encodes cells into a UTF-8 buffer, calls `utf8_next_grapheme()` to find the boundary, and maps back to a cell index.
- **`cursor_prev_grapheme()`** -- Same strategy in reverse using `utf8_prev_grapheme()`.

After vertical movement, the cursor snaps to grapheme cluster starts so it never lands between the two regional indicators of a flag or inside a ZWJ sequence.

`grapheme_display_width()` determines terminal column width per cluster: 2 for CJK/emoji/flags, 1 for most other characters, with VS16 emoji presentation detection.

## Dependencies

- **C23 compiler** -- GCC 14+ or Clang 18+. The source uses C23
  features (`constexpr`, `auto` type inference, unnamed parameters).
- **POSIX environment** -- Linux, macOS, or *BSD. The editor relies on
  `mmap`, `poll`, `termios`, `ioctl`, `SIGWINCH`, and standard POSIX
  file I/O.
- **gstr** -- a header-only grapheme string library, included as a git
  submodule at `lib/gstr`. gstr v3.0.0 ships with Unicode 17.0
  character property tables. No other third-party libraries are needed;
  the editor links only against libc.

Initialize the submodule after cloning:

```
git submodule update --init
```

## Syntax Highlighting

Syntax highlighting activates automatically for C source files (`.c`,
`.h`, `.cpp`). The highlighting engine walks each line's cell array and
classifies characters into categories that map to theme colors:

- **Keywords** -- control flow and structure: `if`, `while`, `for`,
  `switch`, `return`, `break`, `continue`, `else`, `case`, `struct`,
  `union`, `typedef`, `static`, `enum`, `class`
- **Type keywords** -- data types in a distinct color: `int`, `long`,
  `double`, `float`, `char`, `unsigned`, `signed`, `void`
- **String literals** -- double-quoted and single-quoted, with
  backslash-escape handling so `\"` does not end the string early
- **Numeric literals** -- integers and decimal numbers, including `.`
  continuation (e.g., `3.14`)
- **Single-line comments** -- `//` through end of line
- **Multi-line comments** -- `/* ... */` spanning any number of lines,
  with open/close state tracked across line boundaries

Highlight state is stored inline in each cell's `syntax` field, so
rendering never needs a separate highlight buffer. Files that do not
match a known extension render as plain uncolored text.

## Themes

All 6 themes use 24-bit RGB color. Cycle through them at runtime with
Alt-T. The default theme is Tokyo Night.

| Theme | Description |
|-------|-------------|
| **Cyberpunk** | Dark background with neon magenta, cyan, and green accents |
| **Nightwatch** | Monochrome dark palette using shades of gray and white |
| **Daywatch** | Monochrome light palette -- dark text on a near-white background |
| **Tokyo Night** | Deep indigo base with soft purple, blue, and green tones |
| **Akira** | Neo-Tokyo aesthetic with red and cyan on a near-black warm base |
| **Tokyo Cyberpunk** | Tokyo Night's indigo base combined with neon magenta and cyan accents |

Each theme defines 14 color slots: background, foreground, line numbers,
status bar (background and text), message bar, highlight
(background and foreground), comments, keywords (primary and secondary),
strings, numbers, and search matches.

## Unicode Support

The editor is **grapheme-aware**: cursor movement, insertion, deletion,
display width calculation, and rendering all operate on grapheme
clusters -- the units humans perceive as single characters. This is
powered by [gstr](https://github.com/eedmonds/gstr) v3.0.0 with
Unicode 17.0 tables for grapheme break, East Asian width, and extended
pictographic properties.

What this means in practice:

- **Flag emoji** -- A flag like `🇯🇵` is two regional indicator
  codepoints (U+1F1EF U+1F1F5) that form one grapheme cluster. The
  cursor treats the entire flag as a single unit occupying two terminal
  columns. One press of backspace deletes both codepoints.

- **ZWJ family emoji** -- A sequence like `👨‍👩‍👧` is 7+ codepoints
  joined by zero-width joiners. The editor sees this as one grapheme
  cluster, displays it in two columns, and deletes the entire sequence
  with a single backspace.

- **CJK wide characters** -- Characters like `漢` and `字` are each one
  grapheme cluster but occupy two terminal columns. The width calculation
  accounts for this so columns stay aligned and the cursor advances by
  the correct amount.

After any vertical cursor movement, the editor snaps the cursor to the
nearest grapheme cluster boundary. If an up/down move places the cursor
in the middle of a multi-codepoint cluster, it snaps back to the start
of that cluster.

## Limitations / Known Issues

- **C syntax only** -- Syntax highlighting is limited to C/C++ files.
  There is no plugin system; adding a new language requires editing the
  `syntax_highlight_database` array in the source.
- **No undo/redo** -- All edits are immediate and permanent. There is no
  undo history.
- **No text selection or clipboard** -- There is no visual selection
  mode, no copy/cut/paste, and no system clipboard integration.
- **No find-and-replace** -- Incremental search highlights matches but
  cannot substitute text.
- **No split panes or tabs** -- The editor operates on a single file in
  a single view.
- **No auto-indent or bracket matching** -- The reserved `context` field
  in the cell struct exists for future bracket matching, but it is not
  yet implemented. Enter inserts a plain newline with no indentation.
- **No configuration file** -- Theme, tab width, and keybindings are
  compiled in. Customization requires editing the source.
- **UTF-8 only** -- Files in other encodings (Latin-1, UTF-16, etc.)
  are not detected or converted. Non-UTF-8 bytes produce U+FFFD
  replacement characters.
- **OOM is fatal** -- Memory allocation failure terminates the editor
  immediately via `terminal_die()`.

## License

MIT -- see [LICENSE](LICENSE).
