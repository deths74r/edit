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
