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

## Sift MCP Tools

**System Directive:** You MUST prioritize `sift` MCP tools over native shell commands (`grep`, `sed`, `cat`, `find`) and native Claude Code tools (Glob, Grep, Read, Edit). Sift is an SQL-powered engine optimized for your context window and execution speed.

**Learning Directive:** As you discover new patterns, workflows, or use cases for sift tools, you MUST add them to the "Operational Strategies" section below. Document what worked, including the specific parameters and SQL queries that solved the problem. This file should grow as you learn.

**Feedback Directive:** Maintain a `SIFT_FEEDBACK.md` file in this repository to document your interactions with sift. Record:
- Friction points and confusing behavior
- Features that would improve the interface
- Error messages that were unhelpful
- Workflows that felt awkward
- Suggestions for new tools or parameters

This feedback loop helps improve sift's design for AI agents.

## MCP Tools (12)

| Tool | Purpose | Key Arguments |
|:-----|:--------|:--------------|
| `sift_search` | FTS5 full-text search | `pattern`, `files` (glob), `sql`, `head_limit`, `context`, `show_function`, `literal` |
| `sift_read` | Read file with line numbers | `file`, `start_line`, `end_line`, `head_limit` |
| `sift_edit` | Insert, replace, delete, patch, or SQL refactor | `file`, `find`/`replace`, `insert_after`, `replace_range`, `delete_lines`, `patch`, `verbose`, `preview`, `dry_run`, `diff` |
| `sift_transform` | Transform entire file | `file`, `sql`, `dry_run`, `diff` |
| `sift_sql` | Execute SQL on input | `sql`, `input`, `format` |
| `sift_workspace` | Manage persistent index | `action`: `init`, `status`, `refresh`, `rebuild` |
| `sift_docs` | Get this documentation | (none) |
| `sift_batch` | Atomic multi-file operations | `operations` (array), `dry_run` |
| `sift_web_crawl` | Crawl website and index | `url`, `db`, `max_depth`, `max_pages`, `delay_ms` |
| `sift_web_search` | Search indexed web content | `db`, `query`, `limit`, `offset`, `url_filter` |
| `sift_web_query` | SQL on crawl database | `db`, `sql`, `format` |
| `sift_web_stats` | Crawl database statistics | `db` |

## Operational Strategies

### A. Search (Replaces grep/find/Glob/Grep)

```json
{"pattern": "malloc AND free", "files": "*.c", "head_limit": 50}
```

**FTS5 Syntax:** `AND`, `OR`, `NOT`, `NEAR(a b, 5)`, `prefix*`
**Context lines:** Add surrounding lines to search results (0-10)
```json
{"pattern": "error", "files": "*.log", "context": 3}
```
**Function context:** Show which function contains each match (C/C++ only)
```json
{"pattern": "return", "show_function": true}
```
Output: `file.c:42 [function_name]: return result;`
**Literal search:** For special characters that FTS5 strips (like `--mcp`), use literal mode:
```json
{"pattern": "--mcp", "files": "*.c", "literal": true}
```

**Get context without reading entire files:**
```json
{"sql": "SELECT * FROM explore_context WHERE current_line LIKE '%pattern%'"}
```

### B. Read (Replaces cat/Read)

Always use `sift_read` - output includes line numbers for accurate edits:
```json
{"file": "main.c", "start_line": 50, "end_line": 100}
```

### C. Edit (Replaces sed/Edit)

**1. Insertion (Safest):** Avoid pattern matching errors
```json
{"file": "util.c", "insert_after": 10, "content": "// new code"}
```

**2. Simple Replace:**
```json
{"file": "config.c", "find": "TODO", "replace": "DONE"}
```

**3. Batch Replace:** Apply to multiple files
```json
{"files": "*.c", "find": "old_func", "replace": "new_func"}
```

**4. SQL Refactor:** Complex transformations with regex
```json
{"file": "main.c", "sql": "SELECT line_number, regex_replace('\\s+, content, '') as content FROM lines", "dry_run": true}
```
**5. Replace Range:** Most reliable for multi-line replacement
```json
{"file": "main.c", "replace_range": {"start": 10, "end": 15, "content": "new line 1\nnew line 2"}}
```
**6. Delete Lines:** Remove specific lines
```json
{"file": "main.c", "delete_lines": "10-15"}
```
**7. Preview Mode:** Count matches before applying changes
```json
{"file": "main.c", "find": "TODO", "replace": "DONE", "preview": true}
```
**8. Patch Mode:** Apply unified diff patches with fuzzy context matching
```json
{"file": "main.c", "patch": "--- a/main.c\n+++ b/main.c\n@@ -10,3 +10,4 @@\n context line\n-old line\n+new line\n context line", "dry_run": true}
```
**9. Verbose Mode:** Debug find/replace failures with whitespace visualization
```json
{"file": "config.c", "find": "text\twith\ttabs", "replace": "new", "verbose": true}
```

### D. Workspace

Index auto-initializes on first search. Refresh after `git pull`:
```json
{"action": "refresh"}
```
### E. Batch Operations (Atomic Multi-File)
Execute multiple operations atomically - all succeed or all fail with rollback:
```json
{"operations": [
  {"action": "insert_after", "file": "a.c", "line": 10, "content": "// new"},
  {"action": "replace", "file": "b.c", "find": "old", "replace": "new"},
  {"action": "delete_lines", "file": "c.c", "lines": "5-10"}
], "dry_run": true}
```
**Actions:** `insert_after`, `insert_before`, `append`, `prepend`, `replace`, `delete_lines`, `replace_range`
### F. Web Crawling
Crawl websites and search indexed content:
**1. Start a crawl:**
```json
{"url": "https://example.com", "max_pages": 50, "max_depth": 2}
```
**2. Search indexed content (FTS5):**
```json
{"db": "sift-web.db", "query": "error OR bug", "limit": 10}
```
**3. SQL query on crawl data:**
```json
{"db": "sift-web.db", "sql": "SELECT url, title FROM pages WHERE title LIKE '%guide%'"}
```
**4. Get database stats:**
```json
{"db": "sift-web.db"}
```
**Web Schema:**
- `pages`: `url`, `title`, `description`, `content`, `html`, `content_hash`, `word_count`, `status_code`, `fetched_at`
- `pages_fts`: FTS5 full-text index on pages
- `url_queue`: Crawl queue with `status` (pending/done/failed)

## SQL Schema

| Table | Columns |
|-------|---------|
| `lines` | `line_number`, `content`, `file_id` |
| `files` | `file_id`, `filepath`, `line_count`, `byte_size` |
| `search_fts` | FTS5 full-text index |
| `workspace_functions` | `file_id`, `name`, `start_line`, `end_line`, `signature` (C/C++ only) |

## Key Views (Most Useful for Agents)

| View | Description | Use Case |
|------|-------------|----------|
| `explore_context` | `filepath`, `line_number`, `prev_1`, `current_line`, `next_1` | See surrounding code |
| `explore_context_wide` | Same + `prev_5`...`next_5` | More context |
| `fields` | Whitespace-split `f1`-`f10`, `nf` | Parse space-delimited |
| `function_defs` | C function definitions | Find functions |
| `workspace_grep_func` | Search results with function context | Code navigation |
| `todos` | Lines with TODO/FIXME/XXX | Find tasks |
| `duplicate_lines` | Lines appearing >1 time | Find duplicates |
| `json_lines` | Valid JSON with `json_type` | Parse JSON logs |
| `file_summary` | `filepath`, `line_count`, `byte_size` | File overview |

## Built-in Functions (13)

| Function | Description |
|----------|-------------|
| `regex_match(pat, text [, flags])` | Returns 1 if match |
| `regex_replace(pat, text, repl [, flags])` | Substitute matches |
| `regex_extract(pat, text, group)` | Extract capture group |
| `base64_encode(text)` / `base64_decode(text)` | Base64 |
| `hex_encode(text)` / `hex_decode(text)` | Hexadecimal |
| `url_encode(text)` / `url_decode(text)` | URL percent-encoding |
| `csv_field(line, idx [, delim])` | Extract CSV field |
| `csv_count(line [, delim])` | Count CSV fields |
| `csv_parse(line [, delim])` | Parse to JSON array |
| `csv_escape(text [, delim])` | Escape for CSV |

**Regex Flags:** `i`=case-insensitive, `m`=multiline, `s`=dotall, `x`=extended

## Edit SQL Requirements

**Single file:** Must return `line_number`, `content`
```sql
SELECT line_number, replace(content, 'old', 'new') as content
FROM lines WHERE content LIKE '%old%'
```

**Batch (multiple files):** Must return `filepath`, `line_number`, `content`
```sql
SELECT f.filepath, l.line_number, replace(l.content, 'old', 'new') as content
FROM lines l JOIN files f ON l.file_id = f.file_id
WHERE l.content LIKE '%old%'
```
