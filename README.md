# edit

A terminal text editor written in C23. Single file, zero dependencies beyond libc.

`edit` is built on two ideas: a **cell-based architecture** where each
codepoint carries inline syntax and rendering metadata, and **mmap lazy
loading with line temperatures** (COLD/WARM/HOT) for instant file opens
regardless of size. It also serves as a real-world test case for
[gstr](https://github.com/deths74r/gstr), a grapheme-based string
library for C.

**v0.3.0** | gstr v3.0.0 | MIT License

## Features

- **Single-file implementation** -- the entire editor is `edit.c` plus the
  header-only `gstr.h`. No build system magic, no generated files.

- **Full Unicode / grapheme-aware editing** -- cursor movement, insertion,
  deletion, and rendering all operate on grapheme cluster boundaries via gstr.
  CJK wide characters, emoji, ZWJ sequences, and regional indicators display
  and navigate correctly.

- **mmap lazy loading** -- files are memory-mapped on open. Lines start COLD
  (only byte offsets stored), become WARM when first decoded into cells, and
  go HOT on edit. Opening a multi-megabyte file is near-instant.

- **Undo/redo** -- operation stack with 256 undo groups, time-based coalescing
  (edits within 500ms group together), and line-change boundaries. Ctrl+U to
  undo, Ctrl+R to redo.

- **Text selection** -- Shift+Arrow for keyboard selection, mouse click-and-drag,
  Shift+Ctrl+Arrow for word selection, Alt+A/E for select-to-start/end-of-line.
  Selected text renders in reverse video.

- **Clipboard** -- Alt+C copy, Alt+X cut, Alt+V paste. Internal clipboard plus
  OSC 52 system clipboard integration (works in xterm, kitty, alacritty,
  WezTerm, foot, iTerm2, Windows Terminal). Zero external dependencies.

- **Word movement** -- Ctrl+Left/Right jumps by word boundaries with a
  three-class model (whitespace, punctuation, word characters).

- **Syntax highlighting** -- C/C++ aware: keywords, type keywords, strings,
  numbers, single-line and multi-line comments. Highlight state is stored
  inline in each cell.

- **6 color themes (24-bit RGB)** -- Cyberpunk (default), Nightwatch, Daywatch,
  Tokyo Night, Akira, Tokyo Cyberpunk. Cycle with Alt+T.

- **Mouse support** -- SGR mouse reporting: click to position cursor,
  click-and-drag to select, scroll wheel with acceleration.

- **Incremental search** -- results update on each keypress, matches
  highlighted inline, arrow keys navigate forward/backward with wrap-around.

- **Auto-indent** -- Enter copies leading whitespace from the current line.

- **Scroll margin** -- 5 lines of context always visible above and below the
  cursor during keyboard navigation.

- **Virtual column preservation** -- vertical movement through short lines
  remembers your column and restores it on return to a longer line.

- **Help screen** -- F11 or Alt+? loads an interactive help reference into the
  editor buffer. Browse with scroll/search, ESC to return to your file.

- **Suspend** -- Ctrl+Z suspends to the shell, `fg` to resume.

- **Atomic saves** -- writes to a temp file then renames, so the original is
  never corrupted on write failure. fsync before rename for durability.

- **Signal safety** -- SIGTERM/SIGHUP trigger emergency save, SIGWINCH handles
  resize, SIGPIPE ignored.

- **Live terminal resize** -- re-queries dimensions, clamps cursor, redraws.

- **poll()-based main loop** -- non-blocking input, efficient paste handling,
  single `write()` per frame to avoid flicker.

## Build

Requires a C23-capable compiler (GCC 14+ or Clang 18+) and a POSIX system.
The only compile-time dependency is `gstr.h`, included as a git submodule.

```
git clone --recurse-submodules https://github.com/deths74r/edit.git
cd edit
make              # debug build (-g -O0)
make release      # optimized build (-O2)
make clean        # remove artifacts
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

## Keybindings

### File Operations

| Key | Action |
|-----|--------|
| Alt+S / Ctrl+S | Save |
| Alt+Shift+S | Save As |
| Alt+Q / Ctrl+Q | Quit |
| Ctrl+Z | Suspend (return to shell) |

### Navigation

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor (grapheme-aware) |
| Alt+H/J/K/L | Move cursor (vim-style) |
| Home / End | Start / end of line |
| Ctrl+A / Ctrl+E | Start / end of line |
| Ctrl+Left / Ctrl+Right | Jump by word |
| Page Up / Page Down | Scroll by one screenful |
| Alt+G / Ctrl+G | Jump to line number |
| Mouse click | Position cursor |
| Scroll wheel | Scroll with acceleration |

### Selection

| Key | Action |
|-----|--------|
| Shift+Arrow | Select by character / line |
| Shift+Ctrl+Left / Right | Select by word |
| Alt+A / Alt+E | Select to line start / end |
| Mouse drag | Select with mouse |
| ESC | Clear selection |

### Clipboard

| Key | Action |
|-----|--------|
| Alt+C | Copy |
| Alt+X | Cut |
| Alt+V | Paste |
| Alt+Shift+K | Cut entire line |
| Alt+D | Duplicate line |

### Editing

| Key | Action |
|-----|--------|
| Backspace / Ctrl+H | Delete character before cursor |
| Delete | Delete character at cursor |
| Enter | Insert newline (with auto-indent) |
| Ctrl+U | Undo |
| Ctrl+R | Redo |

### Search and Display

| Key | Action |
|-----|--------|
| Alt+F / Ctrl+F | Incremental search |
| Alt+T | Cycle color theme |
| Alt+N | Toggle line numbers |
| F11 / Alt+? | Help screen |

## Architecture

### Cell Model

Every codepoint is a `struct cell` carrying inline metadata:

```c
struct cell {
    uint32_t codepoint;   // Unicode codepoint
    uint16_t syntax;      // highlight category
    uint8_t  neighbor;    // word boundary info (reserved)
    uint8_t  flags;       // rendering flags (selection state, etc.)
    uint32_t context;     // pair matching ID (reserved)
};
```

### Line Temperature

Lines use a three-level temperature system for lazy loading from mmap:

- **COLD** -- Only the mmap byte offset and length are stored. No cells allocated.
- **WARM** -- Cells decoded from mmap bytes. The mmap region is still valid backing.
- **HOT** -- Cells edited, mmap content stale.

Transitions are one-way: COLD -> WARM -> HOT. Opening a large file is
near-instant -- only lines that scroll into view pay the decode cost.

### Undo System

The undo system records 6 operation types (INSERT_CHAR, DELETE_CHAR,
INSERT_LINE, DELETE_LINE, SPLIT_LINE, JOIN_LINE) in a bounded stack of
256 groups. Edits within 500ms are coalesced into a single undo group.
Structural operations (Enter, line joins) and cursor line changes force
new group boundaries.

### Rendering Pipeline

Each frame is assembled in a heap-allocated append buffer and flushed
with a single `write()` call:

1. `editor_scroll()` -- viewport follows cursor with 5-line margin
2. `editor_draw_rows()` -- lines, syntax colors, selection, grapheme-aware encoding
3. `editor_draw_status_bar()` -- filename, dirty flag, position
4. `editor_draw_message_bar()` -- transient messages
5. Single `write()` flush -- cursor hidden during drawing

### Error Handling

- Functions return negative `errno` on failure, use `goto` cleanup.
- OOM is fatal (`terminal_die()` with reentrancy guard).
- I/O errors are non-fatal (status bar message).
- `terminal_die()` attempts emergency save before exit.

## Dependencies

- **C23 compiler** -- GCC 14+ or Clang 18+
- **POSIX environment** -- Linux, macOS, or *BSD
- **gstr** -- header-only grapheme library, included as a submodule.
  No other third-party libraries; links only against libc.

## Themes

All 6 themes use 24-bit RGB color. Cycle with Alt+T. Default is Cyberpunk.

| Theme | Description |
|-------|-------------|
| **Cyberpunk** | Dark background with neon magenta, cyan, and green accents |
| **Nightwatch** | Monochrome dark palette using shades of gray and white |
| **Daywatch** | Monochrome light palette -- dark text on near-white background |
| **Tokyo Night** | Deep indigo base with soft purple, blue, and green tones |
| **Akira** | Neo-Tokyo aesthetic with red and cyan on a near-black warm base |
| **Tokyo Cyberpunk** | Tokyo Night's indigo base with neon magenta and cyan accents |

## Limitations

- **C syntax only** -- highlighting limited to C/C++. No plugin system.
- **No find-and-replace** -- search highlights matches but cannot substitute.
- **No split panes or tabs** -- single file, single view by design. Use
  terminal tabs/splits for multiple files.
- **No bracket matching** -- the reserved `context` field exists for this.
- **No configuration file** -- theme, tab width, keybindings are compiled in.
- **UTF-8 only** -- non-UTF-8 bytes produce replacement characters.

## License

MIT -- see [LICENSE](LICENSE).
