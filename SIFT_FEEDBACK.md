# Sift Tool Feedback

This document tracks feedback on using the Sift MCP tools during the edit text editor modularization project.

## Session: Phase 25B - Terminal Module (2026-01-01)

### What Worked Well

1. **`sift_workspace init`** - Quick indexing of the codebase (5 files, 4086 lines) with persistent database for fast subsequent searches.

2. **`sift_search`** - FTS5 boolean search (`terminal_resized OR terminal_get_size`) was excellent for finding all references across files quickly.

3. **`sift_read`** - The line number output format is very useful for planning edits. The `start_line`/`end_line` parameters are helpful for reading specific sections.

4. **`sift_edit` with find/replace** - Simple string replacements work smoothly:
   ```
   find: "SRC = src/edit.c src/error.c"
   replace: "SRC = src/edit.c src/error.c src/terminal.c"
   ```
   The diff output showing the change is helpful for verification.

### Areas for Improvement

1. **Large file handling** - For files like edit.c (15000+ lines), would benefit from better chunked reading strategies or automatic context detection.

2. **Multi-line find/replace** - The Edit tool was still more convenient for large multi-line block removals. Sift_edit's SQL mode could potentially handle this but wasn't intuitive for the use case.

3. **Workspace refresh after edits** - After creating new files (terminal.c, terminal.h), need to run `sift_workspace refresh` to update the index. Would be nice if this happened automatically.

### Usage Pattern Notes

- Used `sift_search` for discovery, `sift_read` for targeted line reading
- Used `sift_edit` for simple find/replace operations
- Fell back to standard Edit tool for complex multi-line block operations
- The combination works well - Sift handles search/read efficiently, Edit handles complex changes

### Suggestions

1. Add a "remove lines N-M" convenience mode to sift_edit
2. Consider auto-refresh of workspace index when files change
3. Add pattern-based multi-file search results with context (like grep -C)
## Session: Phase 25C - Theme Module (2026-01-01)
### What Worked Well
1. **`sift_search`** - Fast pattern searches like `config_save OR "static void config_save"` quickly located function definitions and usages across files.
2. **`sift_read`** - Targeted reading with line ranges (`start_line`/`end_line`) was efficient for examining specific sections without loading entire large files.
3. **`sift_edit` with `insert_after`** - Adding new lines at specific positions worked smoothly:
   ```
   insert_after: 11
   content: "#define _GNU_SOURCE"
   ```
4. **Single-line find/replace** - Removing `static` keywords from variable declarations was quick and reliable with diff confirmation.
### Areas for Improvement
1. **Multi-line find/replace still problematic** - Patterns spanning multiple lines with exact whitespace don't match reliably. The workarounds:
   - Use `insert_after` for additions
   - Use `sed` via Bash for large deletions (e.g., `sed -i '26,1740d' file.c`)
   - Use the standard Edit tool for complex multi-line operations
2. **SQL mode for deletions unclear** - Tried `sift_edit` with SQL to delete a range of lines but the expected syntax wasn't obvious. A simpler `delete_lines: "26-1740"` parameter would be helpful.
### Usage Pattern Notes
- Used `sift_search` to find theme-related code locations
- Used `sift_read` for examining code boundaries before removal
- Used `sift_edit` for single-line operations (adding includes, removing static)
- Used `sed` via Bash for removing large code blocks (~1715 lines)
- Combination of tools is effective, but large block operations still require workarounds
### Additional Suggestions
4. Add explicit `delete_range: { start: N, end: M }` parameter to sift_edit
5. Document multi-line matching syntax (or improve it if currently limited)
