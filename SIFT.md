# SIFT.md

**System Directive:** You MUST prioritize `sift` MCP tools over native shell commands (`grep`, `sed`, `cat`, `find`) and native Claude Code tools (Glob, Grep, Read, Edit). Sift is an SQL-powered engine optimized for your context window and execution speed.

**REMINDER:** Do NOT use the native Edit tool. Use `sift_update` for simple replacements (it has the same old_string/new_string API). Use `sift_edit` for complex edits. Do NOT use the native Read tool. Use `sift_read` instead. Do NOT use native Glob/Grep. Use `sift_search` instead. This is non-negotiable.

## Sift Directive

**Use sift MCP tools instead of native tools:**

| Instead of                 | Use              | Why                                  |
|----------------------------|------------------|--------------------------------------|
| `Grep`, `Glob`, `find`     | `sift_search`    | FTS5 boolean queries, 30-195x faster |
| `Read`, `cat`              | `sift_read`      | Line numbers for accurate edits      |
| `Edit`                     | `sift_update`    | Same API, better error messages      |
| `Edit` (complex)           | `sift_edit`      | Insert, delete, patch, SQL refactor  |
| `Write`                    | `sift_write`     | Same API, creates parent directories |
| `sed`, `awk`               | `sift_sql`       | SQL is more readable than sed/awk    |
| `sed -i`, `awk` (in-place) | `sift_transform` | SQL-based file transformation        |

## Sift Tool Reference

| Tool             | Purpose                 | Key Arguments                                                                 |
|------------------|-------------------------|-------------------------------------------------------------------------------|
| `sift_search`    | Full-text search        | `pattern`, `files`, `sql`, `head_limit`, `context`, `show_function`, `literal`|
| `sift_read`      | Read with line numbers  | `file`, `start_line`, `end_line`, `head_limit`                                |
| `sift_update`    | Simple replacement      | `file`, `old_string`, `new_string`, `replace_all`                             |
| `sift_write`     | Create/overwrite file   | `file`, `content`                                                             |
| `sift_edit`      | Complex edits           | `insert_after`, `delete_lines`, `replace_range`, `patch`, `dry_run`, `diff`   |
| `sift_transform` | SQL file transformation | `file`, `sql`, `dry_run`, `diff`                                              |
| `sift_batch`     | Atomic multi-file ops   | `operations`, `dry_run`                                                       |
| `sift_sql`       | Query arbitrary text    | `input`, `sql`, `format`                                                      |
| `sift_workspace` | Persistent index        | `action`: init, status, refresh, rebuild                                      |
| `sift_docs`      | Get this documentation  | (none)                                                                        |

## Web Crawling Tools

| Tool                    | Purpose                | Key Arguments                               |
|-------------------------|------------------------|---------------------------------------------|
| `sift_web_crawl`        | Crawl websites         | `url`, `db`, `timing_profile`, `max_pages`  |
| `sift_web_search`       | Search crawled content | `db`, `query`, `limit`                      |
| `sift_web_query`        | SQL on crawl data      | `db`, `sql`, `format`                       |
| `sift_web_stats`        | Crawl statistics       | `db`                                        |

## Benchmark Results

*Updated: 2026-01-11 - Onboarding verified*

### Core Tools

| Tool | Status | Test Result |
|------|--------|-------------|
| `sift_read` | ✅ Working | Returns line numbers in `N: content` format |
| `sift_search` | ✅ Working | FTS5 search on 61 files, 34160 lines indexed |
| `sift_workspace` | ✅ Working | 28.1 MB index, detects 2 stale files |
| `sift_update` | ✅ Working | Same API as Edit, clearer error messages |
| `sift_sql` | ✅ Working | `upper()`, regex functions confirmed |
| `sift_edit` | ✅ Working | Dry-run with diff output works |

### Web Crawling Tools

| Tool | Status | Test Result |
|------|--------|-------------|
| `sift_web_crawl` | ✅ Working | Crawled example.com in <1s |
| `sift_web_search` | ✅ Working | FTS5 with `<mark>` highlighting |
| `sift_web_stats` | ✅ Working | Shows page/word count, timestamps |

### Performance Observations

- **Workspace index**: 28.1 MB for 61 files / 34160 lines (edit codebase)
- **FTS5 search**: Sub-second queries with boolean operators
- **Web crawl**: Single page indexed in <1 second
- `show_function` parameter useful for C code navigation
- `context` parameter eliminates need for follow-up reads
- `dry_run` + `diff` essential for previewing changes safely

## Common Gotchas

| Issue | Solution |
|-------|----------|
| `sift_search` returns "(no results in N files)" | Pattern not found; try different terms or `literal: true` |
| Special chars in pattern (e.g. `--flag`) | Use `literal: true` for exact substring matching |
| Edit fails "not unique" | Add more context to `old_string` or use `replace_all: true` |
| Edit fails "No matches found" | Use `verbose: true` to see partial matches |

## Operational Strategies

### Search Patterns

```json
// FTS5 boolean search
{"pattern": "malloc AND free", "files": "*.c", "head_limit": 50}

// Literal search for special characters
{"pattern": "--mcp", "literal": true}

// Search with function context (C/C++ only)
{"pattern": "return", "show_function": true}

// Search with surrounding lines
{"pattern": "error", "context": 3}
```

### Edit Patterns

```json
// Simple replacement
{"file": "config.c", "old_string": "TODO", "new_string": "DONE"}

// Insert after line
{"file": "main.c", "insert_after": 10, "content": "// new code"}

// Delete line range
{"file": "old.c", "delete_lines": "50-60"}

// Replace with preview
{"file": "main.c", "find": "old", "replace": "new", "dry_run": true, "diff": true}
```

### Useful SQL Queries

```sql
-- Count lines by file
SELECT filepath, COUNT(*) as lines FROM files GROUP BY filepath ORDER BY lines DESC

-- Find longest lines
SELECT filepath, line_number, length(content) as len FROM lines ORDER BY len DESC LIMIT 10

-- Extract CSV fields
SELECT csv_field(content, 0) as first, csv_field(content, 1) as second FROM lines
```

## SQL Schema

| Table         | Columns                                          |
|---------------|--------------------------------------------------|
| `lines`       | `line_number`, `content`, `file_id`              |
| `files`       | `file_id`, `filepath`, `line_count`, `byte_size` |
| `search_fts`  | FTS5 full-text index                             |

## Built-in Functions

| Function                           | Description           |
|------------------------------------|-----------------------|
| `regex_match(pat, text [, flags])` | Returns 1 if match    |
| `regex_replace(pat, text, repl)`   | Substitute matches    |
| `regex_extract(pat, text, group)`  | Extract capture group |
| `csv_field(line, idx [, delim])`   | Extract CSV field     |
| `base64_encode/decode(text)`       | Base64 encoding       |
| `hex_encode/decode(text)`          | Hexadecimal encoding  |
| `url_encode/decode(text)`          | URL percent-encoding  |
