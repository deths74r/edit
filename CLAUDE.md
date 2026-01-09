# CLAUDE.md

This file provides guidance to Claude Code when working with the `edit` codebase.

## Project Overview

`edit` is a minimal terminal text editor written in C. Features include:
- Full UTF-8/Unicode 17.0 support with grapheme cluster navigation
- Syntax highlighting for C and Markdown
- Paired delimiter matching and jump to bracket
- Select word/next occurrence
- Soft line wrapping and auto-indent
- Comment toggling and find & replace
- Theme system with 90+ customizable themes
- Linux kernel-style error handling with emergency save

## Build Commands

```bash
make              # Build the editor
make test         # Run UTF-8 validation tests
make clean        # Remove build artifacts
make install      # Install to ~/.local/bin
```

Compiler: C17 with `-Wall -Wextra -pedantic -O2`.

## Source Structure

| File | Purpose |
|------|---------|
| `src/edit.c` | Core editor logic, rendering, input handling |
| `src/syntax.c` | C and Markdown syntax highlighting |
| `src/theme.c` | Theme parsing and management |
| `src/buffer.c` | Buffer and line management |
| `src/types.h` | Core data structures and enums |
| `themes/*.ini` | Theme definition files |

## Core Data Model

The editor uses a three-temperature line system for memory efficiency:
- **COLD**: Backed by mmap, no cell allocation
- **WARM**: Decoded from mmap, cells allocated but not edited
- **HOT**: Edited in-memory, mmap content stale

Each character is a `struct cell` (12 bytes):
- `uint32_t codepoint` - Unicode codepoint
- `uint16_t syntax` - Syntax highlight token
- `uint8_t neighbor` - Character class + token position (word boundaries)
- `uint8_t flags` - Reserved
- `uint32_t context` - Pair ID + type + role (delimiter matching)

## Syntax Highlighting

**C syntax** (`syntax_is_c_file()`): Keywords, types, strings, numbers, comments, preprocessor, functions, operators, brackets, escapes.

**Markdown syntax** (`syntax_highlight_markdown_line()`):
- Headers H1-H6 with level-specific styling
- Bold, italic, bold+italic with terminal attributes
- Inline code and fenced code blocks
- Links, images, blockquotes
- Lists (ordered, unordered, task lists)
- Tables with header/separator detection
- Horizontal rules with render-time character substitution

Theme files can customize all syntax colors via `syntax_md_*` keys.

## Coding Standards

Follow CODING_STANDARDS.md. Key rules:
- **Naming**: `snake_case` for functions/variables, `SCREAMING_SNAKE_CASE` for macros
- **No typedefs for structs** - always use explicit `struct name`
- **Indentation**: Tabs (8-char width)
- **Braces**: Linux kernel style - function braces on new line, control structures on same line
- **No magic numbers**: All numeric literals must be named constants (except -1, 0, 1, 2)
- **No abbreviations**: Use `buffer` not `buf`, `length` not `len`

## Function Naming Convention

Functions follow `module_verb_object` pattern:
- `buffer_insert_cell_at_column()`
- `syntax_highlight_line()`
- `neighbor_compute_line()`

## Testing

```bash
./edit <filename>           # Interactive testing
timeout 0.3 ./edit <file>   # Quick visual test with auto-exit
```

## Session Recovery

Read TRANSACTION_LOG.md to understand current state after crashes or to resume work. Keep it updated after completing significant work.

---

## Tool Preferences

When available, prefer `sift` MCP tools over native commands for search and edit operations. Sift provides SQL-powered search with FTS5 indexing. Use `sift_docs` tool for full sift documentation.
