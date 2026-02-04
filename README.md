# Edit

A terminal text editor written in C. Single-file implementation, no
dependencies beyond libc and POSIX.

## Build

Requires GCC with C23 support.

```
make              # debug build
make release      # optimized build
make clean        # remove artifacts
```

## Usage

```
./edit [filename]
```

Opens the given file or starts with an empty buffer.

## Key Bindings

| Key | Action |
|-----|--------|
| Alt-S | Save |
| Alt-Shift-S | Save As |
| Alt-Q | Quit (prompts if unsaved) |
| Alt-F | Find (incremental search) |
| Alt-G | Jump to line |
| Alt-T | Cycle theme |
| Alt-N | Toggle line numbers |
| Alt-HJKL | Cursor movement (vim-style) |
| Arrow keys | Cursor movement |
| Page Up/Down | Scroll by page |
| Home/End | Start/end of line |
| F11 | Show version |

Search supports forward/backward navigation with arrow keys and
highlights matches inline.

## Features

**Syntax highlighting** for C files with support for keywords, type
keywords, strings, numbers, single-line comments, and multi-line
comments. The highlighting engine walks each line's cells and assigns
syntax categories that map to theme colors.

**6 color themes** using 24-bit RGB: Cyberpunk, Nightwatch, Daywatch,
Tokyo Night, Akira, and Tokyo Cyberpunk. Themes define colors for
background, foreground, line numbers, status bar, comments, keywords,
strings, numbers, and search matches. Cycle through them with Alt-T.

**Mouse support** via SGR mouse reporting. Click to position the cursor,
scroll wheel to scroll with acceleration (rapid scrolling increases
speed up to 10x, pausing resets it).

**mmap lazy loading** for fast file opens. Files are memory-mapped and
lines start COLD with only byte offsets recorded. Cell arrays are
allocated on demand when a line is first accessed (WARM), and marked HOT
once edited. This means opening a large file is nearly instant -- only
visible lines pay the decode cost.

**Full Unicode support** with grapheme-aware cursor movement. The editor
uses the gstr library for UTF-8 decoding/encoding, codepoint width
calculation (CJK wide characters, emoji), and grapheme cluster boundary
detection. The cursor snaps to grapheme boundaries so it never lands in
the middle of a flag emoji or ZWJ sequence.

**Incremental search** with match highlighting. The find prompt updates
results on each keypress. Arrow keys navigate between matches
forward/backward, wrapping around the file. The original syntax
highlighting is saved and restored when the search ends.

**Live terminal resize** via SIGWINCH handling. The editor re-queries
the terminal size, clamps the cursor to stay within bounds, and redraws
immediately.

**Toggle line numbers** with Alt-N. The gutter width adjusts
automatically based on the file's line count.

## Architecture

### Cell Model

The editor represents text as a grid of cells rather than raw byte
buffers. Each character is a `struct cell`:

```c
struct cell {
    uint32_t codepoint;   // Unicode codepoint
    uint16_t syntax;      // highlight category (HL_NORMAL, HL_KEYWORD1, etc.)
    uint8_t  neighbor;    // word boundary info (reserved)
    uint8_t  flags;       // rendering flags (reserved)
    uint32_t context;     // pair matching ID (reserved)
};
```

Each line is a dynamic array of cells with a doubling growth strategy:

```c
struct line {
    struct cell *cells;
    uint32_t cell_count;
    uint32_t cell_capacity;
    int      line_index;
    int      open_comment;    // multi-line comment state
    size_t   mmap_offset;     // byte offset in mmap region
    uint32_t mmap_length;     // byte length in mmap
    int      temperature;     // COLD, WARM, or HOT
};
```

This design stores syntax metadata inline with each character, so
rendering never needs a separate highlight buffer. The reserved fields
(`neighbor`, `flags`, `context`) exist for future features like bracket
matching and word-level operations without requiring a struct layout
change.

### Line Temperature

Lines use a three-level temperature system for lazy loading from mmap:

- **COLD** -- Only the mmap byte offset and length are stored. No cells
  allocated. This is the initial state for all lines when a file is
  opened.
- **WARM** -- Cells have been decoded from the mmap bytes. The line is
  read-only from the editor's perspective (mmap content is still valid).
- **HOT** -- The line has been edited. The mmap content is stale and the
  cell array is the source of truth.

The transition is one-way: COLD -> WARM -> HOT. When a line is accessed
for rendering or editing, `line_ensure_warm()` decodes the mmap bytes
into cells. Any mutation (insert, delete) marks the line HOT.

### Input Events

All terminal input flows through a single path: `input_buffer_fill()`
reads stdin into a buffer inside `editor_state`, then
`terminal_decode_key()` decodes one event at a time, returning a
`struct input_event` by value:

```c
struct input_event {
    int key;       // key code or special key enum
    int mouse_x;   // column (zero for non-mouse events)
    int mouse_y;   // row (zero for non-mouse events)
};
```

Keyboard events carry only a key code. Mouse events (click, scroll)
also carry screen coordinates. The main loop threads this struct through
the dispatch chain (`editor_process_keypress`, `editor_move_cursor`,
`prompt_handle_key`, `editor_handle_confirm`) so mouse coordinates
travel with the event rather than through a global.

### Rendering Pipeline

Each frame follows the same sequence:

1. `editor_scroll()` -- adjust viewport so the cursor stays visible
2. `editor_draw_rows()` -- render lines with line numbers, syntax
   colors, tab expansion, and grapheme-aware UTF-8 encoding
3. `editor_draw_status_bar()` -- filename, dirty flag, cursor position
4. `editor_draw_message_bar()` -- temporary status messages
5. Single `write()` call -- the entire frame is built in an append
   buffer and flushed at once to avoid flicker

### Error Handling

The editor uses kernel-style error conventions:

- Functions that can fail return `int`: 0 on success, negative `errno`
  on failure (e.g., `-ENOENT`, `-ECANCELED`).
- Resource cleanup uses `goto` unwinding. Functions like
  `editor_open_mmap()` and `editor_save()` acquire resources (fd, mmap,
  malloc) in sequence and jump to a single `out:` label on failure,
  which releases everything in reverse order.
- OOM is fatal. All `malloc`/`realloc` failures call `terminal_die()`,
  which clears the screen and exits. There is no recovery from
  allocation failure.
- I/O errors are non-fatal. Failing to open or save a file sets a status
  bar message with `strerror()` and returns the error code to the
  caller. The editor keeps running.

## License

[MIT](LICENSE)
