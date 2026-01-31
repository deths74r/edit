# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make              # debug build (output: ./edit)
make release      # optimized build (-O2)
make clean        # remove build artifacts
make lint         # check for stray control characters in source
```

Requires a C23-capable compiler (gcc/clang). No external dependencies beyond libc and POSIX.

## What This Is

A terminal text editor written in C23 (~2600 lines, single file `edit.c`). Features Unicode/grapheme-aware editing, mmap-based lazy loading, 6 color themes, C syntax highlighting, incremental search, and mouse support.

## Architecture

### Cell Model

Every character is a `struct cell` carrying its codepoint and inline syntax metadata. This avoids a separate highlight buffer and allows future per-character features (bracket matching, etc.) without layout changes.

### Lazy Loading (Temperature System)

Lines have three states, transitioning one-way: **COLD** → **WARM** → **HOT**.

- **COLD**: Only mmap byte offset/length stored. No cells allocated. This is the initial state for all lines when a file is opened.
- **WARM**: Cells decoded from mmap bytes on demand (e.g., when scrolled into view).
- **HOT**: Line has been edited. Cells are the source of truth; mmap data is stale.

This means files open instantly regardless of size — only visible lines get decoded.

### Rendering Pipeline

Each frame: `editor_scroll()` → `editor_draw_rows()` → status bar → message bar. All output accumulates in an `struct append_buffer`, then gets flushed in a single `write()` call to avoid flicker.

### Syntax Highlighting

Line-based state machine tracking comment/string/number state. Multi-line comment state (`open_comment` flag) carries across lines. Keywords detected at word boundaries with a type distinction (control flow vs type keywords).

### Grapheme-Aware Cursor

Uses the **gstr** library (`lib/gstr/include/gstr.h`) for UTF-8 decode/encode, grapheme boundary detection (UAX #29), and codepoint display width. The cursor never lands mid-cluster — ZWJ sequences, flag emoji, and combining marks are handled correctly.

### Error Handling

Kernel-style: functions return negative `errno` on failure, use `goto` unwinding for cleanup. OOM is fatal (`terminal_die()`); I/O errors are non-fatal (status bar message).

## Key Source Files

- **`edit.c`** — The entire editor. Organized by section banners (`/*** Terminal ***/`, `/*** Row Operations ***/`, etc.)
- **`lib/gstr/`** — Unicode grapheme library (header-only via `gstr.h`). Has its own test suite in `lib/gstr/test/`
- **`test_utf8.txt`** — Manual test file for Unicode rendering (emoji, wide chars, ZWJ sequences)

## Coding Conventions

Defined in `CODING_STANDARDS.md`. The important parts:

- **Tabs** for indentation (8-char display width)
- **K&R brace style**: opening brace on new line for functions, same line for control structures
- **`struct name`** always explicit — no typedefs for structs
- **No abbreviations**: `cursor_x` not `cx`, `line_count` not `lc`, `file_descriptor` not `fd`
- **snake_case** for functions/variables/structs, **SCREAMING_SNAKE_CASE** for constants/macros
- **Comments above entities**, never trailing. Conversational tone, not formal labeled sections
- **Pointer asterisk** on the variable: `char *buffer`
- Preserve existing **section banners** when editing

## Global State

The editor uses a single global `struct editor_state editor` singleton containing all state: cursor position, viewport offset, terminal dimensions, lines array, theme, mmap info, and terminal settings.
