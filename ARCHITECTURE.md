# Architecture Manual

This document provides a comprehensive technical reference for developers working on the `edit` text editor. It covers the internal architecture, data structures, subsystems, and design decisions.

## Table of Contents

- [Overview](#overview)
- [Source Organization](#source-organization)
- [Memory Model](#memory-model)
- [Core Data Structures](#core-data-structures)
- [Subsystems](#subsystems)
  - [UTF-8 Stack](#utf-8-stack)
  - [Neighbor Layer](#neighbor-layer)
  - [Pair Entanglement](#pair-entanglement)
  - [Syntax Highlighting](#syntax-highlighting)
  - [Soft Line Wrapping](#soft-line-wrapping)
  - [Undo/Redo History](#undoredo-history)
  - [Selection System](#selection-system)
  - [Multi-Cursor Editing](#multi-cursor-editing)
  - [Incremental Search](#incremental-search)
  - [Clipboard Integration](#clipboard-integration)
- [Rendering Pipeline](#rendering-pipeline)
- [Input Processing](#input-processing)
- [Terminal Interface](#terminal-interface)
- [File I/O](#file-io)
- [Color System](#color-system)
- [Extending the Editor](#extending-the-editor)
- [Performance Considerations](#performance-considerations)

---

## Overview

`edit` is a single-file terminal text editor (~8500 lines of C17) with these design principles:

1. **Memory efficiency** - Three-temperature line system minimizes RAM usage
2. **Unicode-first** - Full UTF-8/Unicode 17.0 support including grapheme clusters
3. **Responsive rendering** - Batched terminal output with minimal flicker
4. **Accessibility** - WCAG 2.1 AA compliant colors, Tritanopia-friendly theme

The editor uses a cell-based internal representation where each character is stored with its syntax highlighting, word boundary information, and delimiter matching context.

---

## Source Organization

### File Structure

```
edit/
├── src/
│   └── edit.c              # Main source (~8500 lines)
├── third_party/
│   └── utflite/            # UTF-8 library (Unicode 17.0)
│       ├── src/utflite.c
│       ├── include/utflite/utflite.h
│       └── single_include/utflite.h
├── Makefile
├── ARCHITECTURE.md         # This file
├── CODING_STANDARDS.md     # Style guide
├── TRANSACTION_LOG.md      # Development history
└── CLAUDE.md               # AI assistant guidance
```

### Section Layout in edit.c

The source file is organized into logical sections with banner comments:

| Line Range | Section |
|------------|---------|
| 1-77 | Includes and Configuration |
| 78-415 | Data Structures (enums, structs) |
| 416-544 | WCAG Color Contrast Utilities |
| 545-764 | Soft Wrap (cell, line, buffer structs) |
| 765-864 | Global State |
| 865-1350 | Neighbor Layer and Pair Entanglement |
| 1351-1652 | Line Temperature Management |
| 1653-1742 | Buffer Operations |
| 1743-1792 | Line Cell Operations |
| 1793-1990 | Syntax Highlighting |
| 1991-2150 | Soft Wrap Implementation |
| 2151-2207 | Wrap Cache |
| 2208-2432 | Terminal Interface |
| 2433-2590 | File I/O |
| 2591-2823 | Editor Operations |
| 2824-2898 | Selection Range Functions |
| 2899-3522 | Undo/Redo History |
| 3523-3823 | Clipboard Integration |
| 3824-5209 | Cursor Movement and Editing |
| 5210-5339 | Multi-Cursor Editing |
| 5340-6026 | File Operations and Word Selection |
| 6027-6929 | Incremental Search |
| 6930-7688 | Rendering |
| 7689-7996 | Main Loop (Search/Goto handlers) |
| 7997-8337 | Save As Mode |
| 8338+ | Input Reading and Main |

---

## Memory Model

### Three-Temperature Line System

Lines exist in one of three thermal states, minimizing memory usage for large files:

```
┌─────────────────────────────────────────────────────────────┐
│                    LINE TEMPERATURE                          │
├─────────────────────────────────────────────────────────────┤
│  COLD (0)    │  Content in mmap, no cells allocated         │
│              │  Memory: ~24 bytes per line (metadata only)  │
├─────────────────────────────────────────────────────────────┤
│  WARM (1)    │  Cells decoded from mmap, ready for display  │
│              │  Memory: ~24 + (12 × cell_count) bytes       │
├─────────────────────────────────────────────────────────────┤
│  HOT (2)     │  Line has been edited, mmap content stale    │
│              │  Memory: same as WARM                         │
└─────────────────────────────────────────────────────────────┘
```

**Warming a line** (`line_warm()`):
1. Check if already warm/hot - return immediately
2. Decode UTF-8 bytes from mmap into cells
3. Compute neighbor data for word boundaries
4. Apply syntax highlighting
5. Set temperature to WARM

**Heating a line** (implicit on edit):
- Temperature changes from WARM to HOT
- mmap content is now stale and will not be used

### Memory-Mapped File Backing

Files are opened with `mmap()` using `MAP_PRIVATE` and `PROT_READ`:

```c
mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
madvise(mapped, file_size, MADV_RANDOM);  // Hint for random access
```

Each line stores:
- `mmap_offset` - Byte offset into mmap where line content starts
- `mmap_length` - Byte length (excluding newline)

Before saving, all lines are warmed to copy content from mmap to cells, then mmap is unmapped before the file is overwritten.

---

## Core Data Structures

### Cell (12 bytes)

The fundamental unit of text storage:

```c
struct cell {
    uint32_t codepoint;    // Unicode codepoint
    uint16_t syntax;       // Syntax token type
    uint8_t  neighbor;     // Character class + token position
    uint8_t  flags;        // Reserved
    uint32_t context;      // Pair ID + type + role
};
```

**Field breakdown:**

| Field | Size | Purpose |
|-------|------|---------|
| `codepoint` | 32 bits | Unicode scalar value (0-0x10FFFF) |
| `syntax` | 16 bits | Token type for highlighting |
| `neighbor` | 8 bits | Word boundary detection (see Neighbor Layer) |
| `flags` | 8 bits | Reserved for future use |
| `context` | 32 bits | Delimiter matching (see Pair Entanglement) |

### Line

```c
struct line {
    struct cell *cells;           // Dynamic array of characters
    uint32_t cell_count;          // Number of cells in use
    uint32_t cell_capacity;       // Allocated capacity

    size_t   mmap_offset;         // Position in mmap
    uint32_t mmap_length;         // Byte length in mmap
    enum line_temperature temperature;

    // Wrap cache (computed on demand)
    uint32_t *wrap_columns;       // Segment start columns
    uint16_t wrap_segment_count;  // Number of visual segments
    uint16_t wrap_cache_width;    // Width when computed
    enum wrap_mode wrap_cache_mode;
};
```

### Buffer

```c
struct buffer {
    struct line *lines;           // Dynamic array of lines
    uint32_t line_count;
    uint32_t line_capacity;

    char *filename;               // Path on disk (NULL if unsaved)
    bool is_modified;             // Dirty flag

    // Memory-mapped file backing
    int   file_descriptor;
    char *mmap_base;
    size_t mmap_size;

    uint32_t next_pair_id;        // Counter for delimiter matching
    struct undo_history undo_history;
};
```

### Editor State

```c
struct editor_state {
    struct buffer buffer;

    // Cursor position
    uint32_t cursor_row;          // 0-based line index
    uint32_t cursor_column;       // 0-based cell index

    // Viewport
    uint32_t row_offset;          // First visible line
    uint32_t column_offset;       // Horizontal scroll (WRAP_NONE only)

    // Screen dimensions
    uint32_t screen_rows;         // Excludes status bars
    uint32_t screen_columns;
    uint32_t gutter_width;        // Line number column width

    // Selection (single-cursor mode)
    uint32_t selection_anchor_row;
    uint32_t selection_anchor_column;
    bool selection_active;

    // Display settings
    enum wrap_mode wrap_mode;
    enum wrap_indicator wrap_indicator;
    bool show_whitespace;
    uint32_t color_column;
    enum color_column_style color_column_style;

    // Multi-cursor mode
    struct cursor cursors[MAX_CURSORS];
    uint32_t cursor_count;        // 0 = single-cursor mode
    uint32_t primary_cursor;

    // UI state
    char status_message[256];
    time_t status_message_time;
    int quit_confirm_counter;
    bool show_line_numbers;
};
```

---

## Subsystems

### UTF-8 Stack

**Library:** `utflite` (third_party/utflite/)

Provides Unicode 17.0 support:

| Function | Purpose |
|----------|---------|
| `utflite_encode()` | Codepoint → UTF-8 bytes (1-4) |
| `utflite_decode()` | UTF-8 bytes → codepoint |
| `utflite_codepoint_width()` | Display width (0, 1, or 2) |
| `utflite_grapheme_break()` | Grapheme cluster boundaries |

**Width determination:**
- Zero-width: Combining marks, format characters
- Single-width: ASCII, most alphabets
- Double-width: CJK ideographs, fullwidth forms

**Grapheme clusters:** The cursor moves by grapheme cluster, not by codepoint. A grapheme cluster is what users perceive as a single character (e.g., emoji with skin tone modifiers).

### Neighbor Layer

The neighbor field (8 bits) encodes word boundary information:

```
Bits 0-2: Character class (0-7)
Bits 3-4: Token position (0-3)
Bits 5-7: Reserved
```

**Character Classes:**

| Value | Class | Examples |
|-------|-------|----------|
| 0 | WHITESPACE | Space, tab |
| 1 | LETTER | a-z, A-Z, Unicode letters |
| 2 | DIGIT | 0-9 |
| 3 | UNDERSCORE | _ |
| 4 | PUNCTUATION | +, -, *, /, etc. |
| 5 | BRACKET | (), [], {} |
| 6 | QUOTE | ", ', ` |
| 7 | OTHER | Everything else |

**Token Positions:**

| Value | Position | Description |
|-------|----------|-------------|
| 0 | SOLO | Single-character token |
| 1 | START | First character of multi-char token |
| 2 | MIDDLE | Middle character |
| 3 | END | Last character |

**Word Formation Rules:**
- LETTER, DIGIT, and UNDERSCORE form words together
- Other classes break words

**Usage:**
- Ctrl+Arrow word navigation
- Double-click word selection
- Whole-word search matching

### Pair Entanglement

The context field (32 bits) links matching delimiters:

```
Bits 0-23:  Pair ID (up to 16 million unique pairs)
Bits 24-26: Pair type (0-7)
Bits 27-28: Pair role (0-3)
Bits 29-31: Reserved
```

**Pair Types:**

| Value | Type | Example |
|-------|------|---------|
| 0 | NONE | Non-delimiter |
| 1 | COMMENT | /* ... */ |
| 2 | PAREN | ( ... ) |
| 3 | BRACKET | [ ... ] |
| 4 | BRACE | { ... } |
| 5 | DOUBLE_QUOTE | " ... " |
| 6 | SINGLE_QUOTE | ' ... ' |

**Pair Roles:**
- 0: NONE (not a delimiter)
- 1: OPENER (opening delimiter)
- 2: CLOSER (closing delimiter)

**Matching Algorithm** (`buffer_compute_pairs()`):
1. Allocate unique pair IDs from `buffer.next_pair_id`
2. Use a stack to track unmatched openers
3. When closer found, pop matching opener and link via shared ID
4. Block comments are matched across lines

**Usage:**
- Jump to matching bracket (Ctrl+])
- Multiline comment highlighting
- Bracket highlighting (future)

### Syntax Highlighting

**Token Types:**

| Token | Color | Pattern |
|-------|-------|---------|
| NORMAL | Light gray | Default text |
| KEYWORD | Bright magenta | if, else, for, while, return, etc. |
| TYPE | Bright cyan | int, char, void, struct, etc. |
| STRING | Coral | "..." and '...' |
| NUMBER | Light coral | Integers, floats, hex |
| COMMENT | Medium gray | // and /* */ |
| PREPROCESSOR | Light pink | #include, #define, etc. |
| FUNCTION | Light cyan | Identifier before ( |
| OPERATOR | White | +, -, *, /, =, etc. |
| BRACKET | Light gray | (), [], {} |
| ESCAPE | Light red | \n, \t, etc. in strings |

**Highlighting Algorithm** (`syntax_highlight_line()`):
1. Default all cells to SYNTAX_NORMAL
2. Check for preprocessor (line starts with #)
3. Scan for strings, handling escape sequences
4. Detect block comments via pair entanglement
5. Scan for line comments (//)
6. Match keywords (if, else, etc.)
7. Match type keywords (int, void, etc.)
8. Detect function calls (identifier before `(`)
9. Mark numbers, operators, brackets

### Soft Line Wrapping

**Wrap Modes:**

| Mode | Behavior |
|------|----------|
| WRAP_NONE | Horizontal scroll, no wrapping |
| WRAP_WORD | Wrap at word boundaries |
| WRAP_CHAR | Wrap at any character |

**Wrap Indicators:** Visual markers in gutter for continuation lines:
- NONE, CORNER(⎿), HOOK(↪), ARROW(→), DOT(·), FLOOR(⌊), BOTTOM(⌞), RETURN(↳), BOX(└)

**Wrap Cache:**
Each line caches its wrap points:
```c
wrap_columns[0] = 0;              // Segment 0 starts at column 0
wrap_columns[1] = 42;             // Segment 1 starts at column 42
wrap_columns[2] = 85;             // Segment 2 starts at column 85
wrap_segment_count = 3;           // Three visual segments
wrap_cache_width = 80;            // Computed for 80-char width
wrap_cache_mode = WRAP_WORD;      // Computed in word wrap mode
```

**Wrap Point Finding** (`line_find_wrap_point()`):
1. Walk cells, accumulating visual width
2. When exceeding max_width:
   - WRAP_WORD: Search backward for word boundary
   - WRAP_CHAR: Break at current position
3. Prefer breaking after whitespace, then punctuation

**Cache Invalidation:**
- Line edited → invalidate that line
- Terminal resized → invalidate all lines
- Wrap mode changed → invalidate all lines

### Undo/Redo History

**Data Model:**

```c
struct edit_operation {
    enum edit_operation_type type;  // INSERT_CHAR, DELETE_CHAR, etc.
    uint32_t row, column;           // Position
    uint32_t codepoint;             // For single-char ops
    char *text;                     // For multi-char ops
    size_t text_length;
    uint32_t end_row, end_column;   // For range ops
};

struct undo_group {
    struct edit_operation *operations;
    uint32_t operation_count;
    uint32_t cursor_row_before;     // Restore on undo
    uint32_t cursor_column_before;
    uint32_t cursor_row_after;      // Restore on redo
    uint32_t cursor_column_after;
};
```

**Operation Types:**

| Type | Forward | Reverse |
|------|---------|---------|
| INSERT_CHAR | Insert character | Delete character |
| DELETE_CHAR | Delete character | Insert character |
| INSERT_NEWLINE | Split line | Join lines |
| DELETE_NEWLINE | Join lines | Split line |
| DELETE_TEXT | Delete range | Insert saved text |

**Auto-Grouping:**
Operations within `UNDO_GROUP_TIMEOUT` (1.0 second) are grouped together. Typing "hello" quickly creates one undo group; typing with pauses creates multiple.

**Undo Algorithm:**
1. Get current group at `history->current_index - 1`
2. Apply operations in reverse order, reversed
3. Restore cursor to `cursor_row_before`, `cursor_column_before`
4. Decrement `current_index`

**Redo Algorithm:**
1. Get group at `history->current_index`
2. Apply operations in forward order
3. Restore cursor to `cursor_row_after`, `cursor_column_after`
4. Increment `current_index`

### Selection System

**Single-Cursor Selection:**
```c
selection_anchor_row    // Fixed point
selection_anchor_column
cursor_row              // Moving point
cursor_column
selection_active        // Whether selection exists
```

Selection range is normalized (start ≤ end) when queried via `selection_get_range()`.

**Selection Operations:**
- `selection_start()` - Begin selection at cursor
- `selection_clear()` - Deactivate selection
- `selection_contains()` - Test if position is selected
- `selection_is_empty()` - Check if anchor == cursor

### Multi-Cursor Editing

**Data Model:**
```c
struct cursor {
    uint32_t row, column;
    uint32_t anchor_row, anchor_column;
    bool has_selection;
};

struct editor_state {
    struct cursor cursors[MAX_CURSORS];  // Up to 100 cursors
    uint32_t cursor_count;               // 0 = single-cursor mode
    uint32_t primary_cursor;             // For viewport tracking
};
```

**Mode Transitions:**
- `multicursor_enter()` - Copy single cursor to `cursors[0]`
- `multicursor_exit()` - Copy primary cursor back to legacy fields

**Multi-Cursor Editing:**
1. Process cursors in reverse order (bottom-to-top)
2. Adjust earlier cursor positions after each operation
3. Normalize (sort and deduplicate) after batch

**Limitations:**
- No newline insertion in multi-cursor mode
- No line-joining backspace in multi-cursor mode

### Incremental Search

**Search State:**
```c
struct search_state {
    bool active;
    char query[256];
    uint32_t query_length;

    // Saved position for cancel
    uint32_t saved_cursor_row, saved_cursor_column;
    uint32_t saved_row_offset, saved_column_offset;

    // Current match
    uint32_t match_row, match_column;
    bool has_match;
    int direction;  // 1 = forward, -1 = backward

    // Replace mode
    bool replace_mode;
    char replace_text[256];
    uint32_t replace_length;
    bool editing_replace;

    // Search options
    bool case_sensitive;
    bool whole_word;
    bool use_regex;

    // Compiled regex
    regex_t compiled_regex;
    bool regex_compiled;
    char regex_error[128];
};
```

**Search Algorithm:**
1. Literal: Compare codepoints with optional case folding
2. Regex: Convert line to UTF-8, use POSIX `regexec()`
3. Whole word: Check boundaries at match start/end

**Match Highlighting:**
- Current match: Gold background
- Other matches: Blue background
- Priority: current > other > selection > trailing whitespace > cursor line

### Clipboard Integration

**Detection Priority:**
1. Wayland (`wl-copy`/`wl-paste`) if `WAYLAND_DISPLAY` set
2. X11 `xclip`
3. X11 `xsel`
4. Internal buffer (fallback)

**Copy:** Pipe selection text to clipboard command
**Paste:** Read from clipboard command, insert at cursor

---

## Rendering Pipeline

### Output Buffering

```c
struct output_buffer {
    char *data;
    size_t length;
    size_t capacity;
};
```

All output is accumulated in a buffer, then flushed in a single `write()` call to minimize terminal flicker.

### Render Flow

```
render_refresh_screen()
    │
    ├─→ output_buffer_init()
    │
    ├─→ Hide cursor: "\x1b[?25l"
    │
    ├─→ Move to home: "\x1b[H"
    │
    ├─→ render_draw_rows()
    │   │
    │   └─→ For each screen row:
    │       ├─→ Clear line: "\x1b[2K"
    │       ├─→ Render gutter (line number or wrap indicator)
    │       ├─→ Render line content
    │       │   ├─→ For each cell:
    │       │   │   ├─→ Determine highlight (search/selection/etc.)
    │       │   │   ├─→ Set foreground/background colors
    │       │   │   └─→ Output UTF-8 character
    │       │   └─→ Handle color column
    │       └─→ Move to next row: "\r\n"
    │
    ├─→ render_draw_status_bar()
    │
    ├─→ render_draw_message_bar()
    │
    ├─→ Position cursor: "\x1b[row;colH"
    │
    ├─→ Show cursor: "\x1b[?25h"
    │
    └─→ output_buffer_flush()
```

### Line Content Rendering

Two modes in `render_line_content()`:

1. **Segment Mode** (soft wrap enabled):
   - Render cells from `start_cell` to `end_cell`
   - Used for wrapped line segments

2. **Scroll Mode** (WRAP_NONE):
   - Skip cells until visual column offset reached
   - Horizontal scrolling

### Color Escape Sequences

True color (24-bit) ANSI escapes:
```
\x1b[38;2;R;G;Bm   # Set foreground (R, G, B are 0-255)
\x1b[48;2;R;G;Bm   # Set background
\x1b[0m            # Reset all attributes
```

---

## Input Processing

### Key Reading

`input_read_key()` handles:

| Input | Result |
|-------|--------|
| Single byte 0x00-0x7F | ASCII character or control key |
| Multi-byte UTF-8 | Decoded codepoint |
| `\x1b[A` | KEY_ARROW_UP |
| `\x1b[B` | KEY_ARROW_DOWN |
| `\x1b[C` | KEY_ARROW_RIGHT |
| `\x1b[D` | KEY_ARROW_LEFT |
| `\x1b[1;5C` | KEY_CTRL_ARROW_RIGHT |
| `\x1b[1;2A` | KEY_SHIFT_ARROW_UP |
| `\x1b[<...` | SGR mouse event |

### Mouse Support

SGR (1006) extended mouse encoding:
```
\x1b[<button;column;row;M   # Press
\x1b[<button;column;row;m   # Release
```

**Events:**
- Left click: Position cursor
- Left drag: Extend selection
- Double-click: Select word
- Triple-click: Select line
- Scroll wheel: Adaptive-speed scrolling

### Adaptive Scroll Speed

Scroll velocity is tracked with exponential smoothing:
```c
dt = time_since_last_scroll;
if (dt < TIMEOUT) {
    velocity = velocity * DECAY + (1.0 / dt) * (1.0 - DECAY);
}
scroll_lines = interpolate(velocity, SLOW_THRESHOLD, FAST_THRESHOLD,
                           MIN_LINES, MAX_LINES);
```

---

## Terminal Interface

### Initialization

```c
terminal_enable_raw_mode()
    │
    ├─→ tcgetattr()           # Save original settings
    ├─→ Disable ECHO, ICANON  # No echo, character-at-a-time input
    ├─→ Disable IXON, ICRNL   # No Ctrl-S/Q, no CR translation
    ├─→ Set VMIN=0, VTIME=1   # Non-blocking with 100ms timeout
    └─→ tcsetattr()           # Apply settings
```

### Cleanup

```c
terminal_disable_raw_mode()
    │
    └─→ tcsetattr()           # Restore original settings
```

### Resize Handling

```c
SIGWINCH handler:
    └─→ terminal_resized = 1;  # Flag checked in main loop

Main loop:
    if (terminal_resized) {
        terminal_resized = 0;
        editor_update_screen_size();
        buffer_invalidate_all_wrap_caches();
    }
```

---

## File I/O

### Opening Files

```
file_open(filename)
    │
    ├─→ open() with O_RDONLY
    ├─→ fstat() for file size
    ├─→ mmap() with MAP_PRIVATE
    ├─→ madvise(MADV_RANDOM)
    ├─→ file_build_line_index()  # Scan for newlines
    ├─→ buffer_compute_pairs()   # Link delimiters
    └─→ syntax_highlight_line()  # For each line
```

### Saving Files

```
file_save()
    │
    ├─→ Warm all lines         # Copy from mmap to cells
    ├─→ munmap()               # Release mmap before overwrite
    ├─→ fopen() with "w"
    ├─→ For each line:
    │   ├─→ Encode cells to UTF-8
    │   └─→ Write to file
    └─→ fclose()
```

---

## Color System

### WCAG Compliance

The editor ensures all text meets WCAG 2.1 AA standards (4.5:1 contrast ratio):

```c
color_ensure_contrast(foreground, background)
    │
    ├─→ Calculate contrast ratio
    ├─→ If >= 4.5, return original
    ├─→ Else iteratively adjust:
    │   ├─→ Try lightening if dark background
    │   └─→ Try darkening if light background
    └─→ Last resort: pure black or white
```

### Tritanopia-Friendly Theme

Design principles:
- Luminance as primary differentiator
- Red-cyan axis (visible to Tritanopia)
- Avoids blue-yellow distinctions

| Element | Color | Rationale |
|---------|-------|-----------|
| Background | #121212 | Near-black for maximum contrast |
| Normal text | #E0E0E0 | High contrast, neutral |
| Keywords | #FF79C6 | Bright magenta, stands out |
| Types | #8BE9FD | Bright cyan, distinct from magenta |
| Strings | #FF9580 | Coral, warm and readable |
| Comments | #909090 | Subdued but WCAG compliant |

---

## Extending the Editor

### Adding a New Key Binding

1. **Define key code** (if special key):
   ```c
   enum key_code {
       // ...
       KEY_MY_NEW_KEY = -XX,
   };
   ```

2. **Detect key** in `input_read_key()`:
   ```c
   case 'x':
       if (escape_sequence) return KEY_MY_NEW_KEY;
   ```

3. **Handle key** in `editor_process_keypress()`:
   ```c
   case KEY_MY_NEW_KEY:
       editor_my_new_function();
       break;
   ```

### Adding a New Syntax Token

1. **Add to enum**:
   ```c
   enum syntax_token {
       // ...
       SYNTAX_MY_TOKEN,
       SYNTAX_TOKEN_COUNT
   };
   ```

2. **Define color**:
   ```c
   static const struct syntax_color THEME_COLORS[] = {
       // ...
       [SYNTAX_MY_TOKEN] = {0xRR, 0xGG, 0xBB},
   };
   ```

3. **Update highlighting** in `syntax_highlight_line()`.

### Adding a New Modal State

Follow the pattern of search/goto/save_as:

```c
struct my_mode_state {
    bool active;
    // Mode-specific fields
    uint32_t saved_cursor_row;
    uint32_t saved_cursor_column;
};
static struct my_mode_state my_mode = {0};

static void my_mode_enter(void) {
    my_mode.active = true;
    my_mode.saved_cursor_row = editor.cursor_row;
    // ...
}

static void my_mode_exit(bool restore) {
    if (restore) {
        editor.cursor_row = my_mode.saved_cursor_row;
        // ...
    }
    my_mode.active = false;
}

static bool my_mode_handle_key(int key) {
    if (!my_mode.active) return false;
    // Handle keys...
    return true;
}
```

---

## Performance Considerations

### Memory Usage

| Component | Per-Item Size | Notes |
|-----------|---------------|-------|
| Cell | 12 bytes | Codepoint + metadata |
| Line (cold) | ~32 bytes | Metadata only |
| Line (warm) | ~32 + 12n | n = character count |
| Undo operation | ~32 bytes | Plus text for DELETE_TEXT |

### Algorithmic Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Character insert | O(n) | Shift cells in line |
| Line insert | O(n) | Shift lines in buffer |
| Syntax highlight | O(n) | Per line |
| Pair matching | O(n²) worst | Usually O(n) |
| Search | O(nm) | n = buffer size, m = query |
| Wrap computation | O(n) | Per line |

### Optimization Techniques

1. **Lazy warming** - Lines warmed only when visible
2. **Wrap caching** - Recomputed only on edit/resize
3. **Output batching** - Single write() per frame
4. **Incremental highlighting** - Only edited lines re-highlighted
5. **Adaptive scrolling** - Reduces scroll events for fast scrolling

---

## Appendix: Escape Sequences Reference

### Cursor Control

| Sequence | Action |
|----------|--------|
| `\x1b[H` | Move to home (1,1) |
| `\x1b[row;colH` | Move to (row, col) |
| `\x1b[?25l` | Hide cursor |
| `\x1b[?25h` | Show cursor |

### Line Control

| Sequence | Action |
|----------|--------|
| `\x1b[2K` | Clear entire line |
| `\x1b[K` | Clear to end of line |
| `\x1b[2J` | Clear entire screen |

### Colors (True Color)

| Sequence | Action |
|----------|--------|
| `\x1b[38;2;R;G;Bm` | Set foreground |
| `\x1b[48;2;R;G;Bm` | Set background |
| `\x1b[0m` | Reset attributes |

### Mouse Control

| Sequence | Action |
|----------|--------|
| `\x1b[?1000h` | Enable mouse click reporting |
| `\x1b[?1002h` | Enable mouse drag reporting |
| `\x1b[?1006h` | Enable SGR extended mode |
| `\x1b[?1000l` | Disable all mouse modes |
