# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`edit` is a minimal terminal text editor written in C (v0.19.0). It features full UTF-8/Unicode 17.0 support, real-time C syntax highlighting, grapheme cluster navigation, paired delimiter matching, jump to matching bracket, select word/next occurrence, soft line wrapping, auto-indent, comment toggling, and find & replace.

## Build Commands

```bash
make              # Build the editor
make test         # Run UTF-8 validation tests
make clean        # Remove build artifacts
make install      # Install to ~/.local/bin
make uninstall    # Remove installed binary
```

Compiler: C17 with `-Wall -Wextra -pedantic -O2`. One expected warning: `cell_is_word_end` is marked unused (reserved for future double-click selection).

## Testing

```bash
./edit <filename>           # Interactive testing
timeout 0.3 ./edit <file>   # Quick visual test with auto-exit
```

## Architecture

### Source Structure

Single-file architecture: `src/edit.c` (~7500 lines) organized into sections with banner comments (`/*** Title ***/`).

### Core Data Model

The editor uses a three-temperature line system for memory efficiency:
- **COLD**: Backed by mmap, no cell allocation
- **WARM**: Decoded from mmap, cells allocated but not edited
- **HOT**: Edited in-memory, mmap content stale

Each character is a `struct cell` (12 bytes):
- `uint32_t codepoint` - Unicode codepoint
- `uint16_t syntax` - Syntax highlight type
- `uint8_t neighbor` - Character class + token position (word boundaries)
- `uint8_t flags` - Reserved
- `uint32_t context` - Pair ID + type + role (delimiter matching)

### Key Subsystems

**Neighbor Layer** - 8-bit field encoding character class (whitespace, letter, digit, punctuation, bracket, quote) and token position (solo, start, middle, end). Enables O(1) word boundary detection for Ctrl+Arrow navigation.

**Pair Entanglement** - 32-bit context field linking matching delimiters (parens, brackets, braces, quotes, block comments). 24-bit pair ID supports up to 16 million unique pairs.

**UTF-8 Stack** - Uses embedded `utflite` library (third_party/utflite/) for encoding/decoding, grapheme cluster segmentation (UAX #29), and character width calculation. Cursor movement operates on grapheme clusters, not bytes.

### Function Naming Convention

Functions follow `module_verb_object` pattern:
- `buffer_insert_cell_at_column()`
- `syntax_highlight_line()`
- `neighbor_compute_line()`

## Coding Standards

All code must follow CODING_STANDARDS.md. Key rules:

- **Naming**: `snake_case` for functions/variables, `SCREAMING_SNAKE_CASE` for macros
- **No typedefs for structs** - always use explicit `struct name`
- **Indentation**: Tabs (8-char width)
- **Braces**: Linux kernel style - function braces on new line, control structures on same line
- **Comments**: Block-style above code, written for beginners, natural language variety
- **No magic numbers**: All numeric literals must be named constants (except -1, 0, 1, 2)
- **No abbreviations**: Use `buffer` not `buf`, `length` not `len`, `index` not `idx`

## Session Recovery

Read TRANSACTION_LOG.md to understand current state after crashes or to resume work. It tracks all phases, commits, and outstanding tasks.

**IMPORTANT**: Always keep TRANSACTION_LOG.md up to date. After completing any significant work (new features, bug fixes, phase completions), update the log with what was done before ending the session.
