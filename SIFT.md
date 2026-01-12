# SIFT.md

**System Directive:** You MUST prioritize `sift` MCP tools over native shell commands (`grep`, `sed`, `cat`, `find`) and native Claude Code tools (Glob, Grep, Read, Edit). Sift is an SQL-powered engine optimized for your context window and execution speed.

**REMINDER:** Do NOT use the native Edit tool. Use `sift_update` for simple replacements (it has the same old_string/new_string API). Use `sift_edit` for complex edits. Do NOT use the native Read tool. Use `sift_read` instead. Do NOT use native Glob/Grep. Use `sift_search` instead. This is non-negotiable.

**Output Directive:** When logging sift tool calls that include multi-line `content` parameters, display the content as formatted code blocks rather than escaped JSON strings. Example - instead of `content: "line1\n\tline2"`, show:
```
line1
    line2
```

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
| `head`, `tail`             | `sift_read`      | Use `start_line`/`end_line` params   |
| `wc`                       | `sift_sql`       | `SELECT COUNT(*) FROM lines`         |
| `sort`                     | `sift_sql`       | `ORDER BY` clause                    |
| `uniq`                     | `sift_sql`       | `deduped`, `unique_lines` views      |
| `cut`                      | `sift_sql`       | `csv_field()` or `fields` view       |
| `tr`                       | `sift_sql`       | `regex_replace()` or `REPLACE()`     |
| `jq`                       | `sift_sql`       | `json_lines`, `json_keys` views      |
| `base64`                   | `sift_sql`       | `base64_encode()`, `base64_decode()` |
| `xxd`                      | `sift_sql`       | `hex_encode()`, `hex_decode()`       |
| `curl` (scraping)          | `sift_web_crawl` | Crawl and index for SQL queries      |

Run `sift_docs` for full documentation.

## Onboarding Checklist

> **Re-running onboarding?** When a new sift version is released, re-run this checklist. Your existing SIFT.md will be amended (not overwritten) - your Discovered Patterns, Benchmark Results, and customizations are preserved.

When first connecting to sift MCP server, verify ALL tool groups work:

### Core Tools (test these first)
```json
sift_read(file: "SIFT.md", end_line: 10)
sift_search(pattern: "function", files: "*.c", head_limit: 5)
sift_workspace(action: "status")
```

### Web Crawling Tools (test with a simple crawl)
```json
sift_web_crawl(url: "https://example.com", max_pages: 1, db: "test.db")
sift_web_search(db: "test.db", query: "example", limit: 5)
sift_web_stats(db: "test.db")
```

### Edit Tools (test with dry_run)
```json
sift_update(file: "test.txt", old_string: "foo", new_string: "bar")  // will fail if no file, that's OK
sift_sql(input: "hello world", sql: "SELECT upper(content) FROM lines")
sift_edit(file: "SIFT.md", insert_after: 0, content: "test", dry_run: true)
sift_batch(operations: [...], dry_run: true)
```

**Important:** If any tool group fails, report the specific error before proceeding.

### After Testing: Document Your Findings
Update SIFT.md with what you learned:
1. Add any new patterns to the **Operational Strategies** section
2. Update **Benchmark Results** with performance observations
3. Add gotchas to **Common Gotchas** table

## Sift Tool Reference

| Tool             | Purpose                 | Key Arguments                                                                                                         |
|------------------|-------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `sift_search`    | Full-text search        | `pattern`, `files`, `sql`, `head_limit`, `context`, `show_function`, `literal`                                        |
| `sift_read`      | Read with line numbers  | `file`, `start_line`, `end_line`, `head_limit`                                                                        |
| `sift_update`    | Simple replacement      | `file`, `old_string`, `new_string`, `replace_all`                                                                     |
| `sift_write`     | Create/overwrite file   | `file`, `content`                                                                                                     |
| `sift_edit`      | Complex edits           | `insert_after`, `delete_lines`, `replace_range`, `patch`, `verbose`, `fuzzy_whitespace`, `preview`, `dry_run`, `diff` |
| `sift_transform` | SQL file transformation | `file`, `sql`, `dry_run`, `diff`                                                                                      |
| `sift_batch`     | Atomic multi-file ops   | `operations`, `dry_run`                                                                                               |
| `sift_sql`       | Query arbitrary text    | `input`, `sql`, `format`                                                                                              |
| `sift_workspace` | Persistent index        | `action`: init, status, refresh, rebuild                                                                              |
| `sift_docs`      | Get this documentation  | (none)                                                                                                                |

## Web Crawling Tools

| Tool                    | Purpose                | Key Arguments                               |
|-------------------------|------------------------|---------------------------------------------|
| `sift_web_crawl`        | Crawl websites         | `url`, `db`, `timing_profile`, `user_agent` |
| `sift_web_search`       | Search crawled content | `db`, `query`, `limit`                      |
| `sift_web_query`        | SQL on crawl data      | `db`, `sql`, `format`                       |
| `sift_web_stats`        | Crawl statistics       | `db`                                        |
| `sift_web_manifest`     | Corpus metadata        | `db`                                        |
| `sift_web_search_multi` | Search multiple DBs    | `dbs`, `query`, `limit`                     |
| `sift_web_merge`        | Merge crawl DBs        | `output`, `sources`                         |
| `sift_web_refresh`      | Update stale pages     | `db`, `max_age_days`, `max_pages`           |

### Web Crawling Workflow

```
1. Crawl:   sift_web_crawl(url, max_pages=100, db="docs.db")
2. Search:  sift_web_search(db="docs.db", query="error handling")
3. Query:   sift_web_query(db="docs.db", sql="SELECT url, title FROM pages")
4. Stats:   sift_web_stats(db="docs.db")
5. Refresh: sift_web_refresh(db="docs.db", max_age_days=7)
```

### Crawl Options

| Option            | Description                                    |
|-------------------|------------------------------------------------|
| `max_depth`       | 0 = unlimited, 1 = seed page only, 2+ = follow |
| `max_pages`       | Limit total pages crawled                      |
| `same_domain`     | Stay on seed domain (default: true)            |
| `allowed_domains` | Whitelist for multi-domain crawling            |
| `delay_ms`        | Base delay between requests (default: 100ms)   |
| `user_agent`      | Custom User-Agent header                       |
| `timing_profile`  | Preset: `stealth`, `polite`, `aggressive`      |
| `jitter_percent`  | Random delay variation 0-100% (default: 50)    |
| `backoff_enabled` | Exponential backoff on 429/5xx (default: true) |
| `burst_mode`      | Human-like burst patterns (default: false)     |

### Timing Profiles

| Profile      | Delay  | Jitter | Backoff | Burst | Use Case              |
|--------------|--------|--------|---------|-------|-----------------------|
| `stealth`    | 2000ms | 75%    | yes     | yes   | Strict rate-limiting  |
| `polite`     | 500ms  | 50%    | yes     | no    | Default, respectful   |
| `aggressive` | 100ms  | 0%     | no      | no    | Permissive sites only |

## Benchmark Results

*Updated: 2026-01-12 - Onboarding verified*

### Core Tools

| Tool | Status | Test Result |
|------|--------|-------------|
| `sift_read` | ✅ Working | Returns line numbers in `N: content` format |
| `sift_search` | ✅ Working | FTS5 search on 64 files, 35139 lines indexed |
| `sift_workspace` | ✅ Working | 30.8 MB index, indexed 24 min ago |
| `sift_update` | ✅ Working | Same API as Edit, clearer error messages |
| `sift_sql` | ✅ Working | `upper()`, regex functions confirmed |
| `sift_edit` | ✅ Working | Dry-run with insert_after preview works |
| `sift_batch` | ✅ Working | Dry-run shows operation preview |

### Web Crawling Tools

| Tool | Status | Test Result |
|------|--------|-------------|
| `sift_web_crawl` | ✅ Working | Crawled example.com in 1s, 1 page indexed |
| `sift_web_search` | ✅ Working | FTS5 with `<mark>` highlighting, 0.000s |
| `sift_web_stats` | ✅ Working | Shows page/word count (19 words), timestamps |

### Performance Observations

- **Workspace index**: 30.8 MB for 64 files / 35139 lines (edit codebase)
- **FTS5 search**: Sub-second queries with boolean operators
- **Web crawl**: Single page indexed in 1 second
- `show_function` parameter useful for C code navigation
- `context` parameter eliminates need for follow-up reads
- `dry_run` + `diff` essential for previewing changes safely

## Common Gotchas

| Issue | Solution |
|-------|----------|
| `sift_search` returns "(no results in N files)" | Pattern not found; try different terms or `literal: true` |
| `sift_search` returns "(no results - run sift_workspace...)" | Workspace not indexed; run `sift_workspace(action: "init")` |
| `AND` query finds nothing | FTS5 AND requires both terms on same line; use separate searches |
| Special chars in pattern (e.g. `--flag`) | Use `literal: true` for exact substring matching |
| Pattern spans multiple lines | Use `multiline: true` for cross-line patterns |
| Edit fails "not unique" | Add more context to `old_string` or use `replace_all: true` |
| Edit fails "No matches found" | Use `verbose: true` to see partial matches and whitespace issues |
| Whitespace mismatch in find/replace | Use `fuzzy_whitespace: true` to normalize tabs/spaces |

## SQL Schema

### Standard Tables (sift_sql)

| Table        | Columns                                          |
|--------------|--------------------------------------------------|
| `lines`      | `line_number`, `content`, `file_id`              |
| `files`      | `file_id`, `filepath`, `line_count`, `byte_size` |
| `search_fts` | FTS5 full-text index                             |

### Workspace Tables (sift_workspace)

| Table                   | Columns                                                               |
|-------------------------|-----------------------------------------------------------------------|
| `workspace_files`       | `file_id`, `filepath`, `mtime`, `size`, `indexed_at`                  |
| `workspace_lines`       | `line_id`, `file_id`, `line_number`, `content`                        |
| `workspace_fts`         | FTS5 full-text index on workspace_lines                               |
| `workspace_functions`   | `file_id`, `name`, `start_line`, `end_line`, `signature` (C/C++ only) |
| `workspace_directories` | `dir_id`, `dirpath`, `mtime` (for optimized refresh)                  |
| `workspace_meta`        | `key`, `value` (metadata like last_refresh_time)                      |

## Built-in Functions (13)

| Function                                      | Description           |
|-----------------------------------------------|-----------------------|
| `regex_match(pat, text [, flags])`            | Returns 1 if match    |
| `regex_replace(pat, text, repl [, flags])`    | Substitute matches    |
| `regex_extract(pat, text, group)`             | Extract capture group |
| `base64_encode(text)` / `base64_decode(text)` | Base64                |
| `hex_encode(text)` / `hex_decode(text)`       | Hexadecimal           |
| `url_encode(text)` / `url_decode(text)`       | URL percent-encoding  |
| `csv_field(line, idx [, delim])`              | Extract CSV field     |
| `csv_count(line [, delim])`                   | Count CSV fields      |
| `csv_parse(line [, delim])`                   | Parse to JSON array   |
| `csv_escape(text [, delim])`                  | Escape for CSV        |

**Regex Flags:** `i`=case-insensitive, `m`=multiline, `s`=dotall, `x`=extended

## Key Views (Most Useful for Agents)

| View                   | Description                                                   | Use Case              |
|------------------------|---------------------------------------------------------------|-----------------------|
| `explore_context`      | `filepath`, `line_number`, `prev_1`, `current_line`, `next_1` | See surrounding code  |
| `explore_context_wide` | Same + `prev_5`...`next_5`                                    | More context          |
| `fields`               | Whitespace-split `f1`-`f10`, `nf`                             | Parse space-delimited |
| `function_defs`        | C function definitions                                        | Find functions        |
| `workspace_grep_func`  | Search results with function context                          | Code navigation       |
| `todos`                | Lines with TODO/FIXME/XXX                                     | Find tasks            |
| `duplicate_lines`      | Lines appearing >1 time                                       | Find duplicates       |
| `json_lines`           | Valid JSON with `json_type`                                   | Parse JSON logs       |
| `file_summary`         | `filepath`, `line_count`, `byte_size`                         | File overview         |

## sift_batch Operations

```json
{
  "operations": [
    {"action": "replace", "file": "config.c", "find": "v1.0", "replace": "v2.0"},
    {"action": "insert_after", "file": "main.c", "line": 10, "content": "// new code"},
    {"action": "delete_lines", "file": "old.c", "lines": "50-60"},
    {"action": "replace_range", "file": "func.c", "start": 20, "end": 25, "content": "new block"},
    {"action": "append", "file": "log.txt", "content": "appended line"},
    {"action": "prepend", "file": "header.h", "content": "// Copyright 2024"}
  ],
  "dry_run": true
}
```

## Output Formats

| Format   | Use Case                          |
|----------|-----------------------------------|
| `plain`  | Human-readable (default)          |
| `json`   | Structured data, API integration  |
| `csv`    | Spreadsheet import                |
| `tsv`    | Tab-separated, shell piping       |
| `ndjson` | Newline-delimited JSON, streaming |

## Feedback Directive

**Maintain a `SIFT_FEEDBACK.md` file in this repository to document your interactions with sift.** Record:
- Friction points and confusing behavior
- Features that would improve the interface
- Error messages that were unhelpful
- Workflows that felt awkward
- Suggestions for new tools or parameters

## Learning Directive

**As you discover new patterns, workflows, or use cases for sift tools, you MUST add them to the "Operational Strategies" section below.** Document what worked, including the specific parameters and SQL queries that solved the problem. This file should grow as you learn.

## Operational Strategies

<!-- Add new learnings below this line -->

### Discovered Patterns

| Date       | Pattern                                 | Solution                                                                      |
|------------|-----------------------------------------|-------------------------------------------------------------------------------|
| 2026-01-10 | Search for special chars like `--flag`  | Use `literal: true` parameter                                                 |
| 2026-01-10 | Replace across multiple occurrences     | Use `replace_all: true` in sift_update                                        |
| 2026-01-10 | Preview changes before applying         | Use `dry_run: true` with `diff: true`                                         |
| 2026-01-10 | Batch replace with backticks in pattern | Use `replace_range` instead of find/replace                                   |
| 2026-01-11 | v0.7.5 directory mtime optimization     | `workspace_directories` table now tracks dir mtimes for O(d) staleness checks |
| 2026-01-12 | Onboarding re-run after version update  | All tool groups verified working; index size: 30.8 MB for 64 files            |

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

-- Find duplicate content
SELECT content, COUNT(*) as count FROM lines GROUP BY content HAVING count > 1

-- Filter JSON logs by level
SELECT * FROM json_log_entries WHERE level = 'ERROR'
```

### Workflow Tips

1. **Always init workspace first**: Before using `sift_search`, run `sift_workspace(action: "init")`
2. **Use dry_run liberally**: Test edits with `dry_run: true` before applying
3. **Prefer replace_range for complex edits**: When find/replace fails due to special chars
4. **Chain operations with sift_batch**: For atomic multi-file changes
5. **Use head_limit for large results**: Prevent output overflow with `head_limit: 50`
