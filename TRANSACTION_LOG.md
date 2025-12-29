# Transaction Log

This file tracks all interactions and actions for the `edit` project. If a session crashes or hangs, read this file to understand where we left off.

---

## Session: 2025-12-28

### Phase 5: Syntax Highlighting (COMPLETED)

**Action [User]**: Requested commit of Phase 5 changes
**Action [Claude]**: Committed syntax highlighting with bug fixes for block comment parsing and status bar color bleeding

**Action [User]**: Requested merge to main
**Action [Claude]**: Merged syntax-highlighting branch to main (fast-forward)

---

### Phase 6: Neighbor Layer and Pair Entanglement (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 6
- Neighbor layer (character classification + token position)
- Pair entanglement (linking matched delimiters)
- Ctrl+Arrow word movement
- Multiline block comment highlighting

**Action [Claude]**: Created branch `neighbor-pair-entanglement`

**Action [Claude]**: Updated header to Phase 6, version to 0.6.0

**Action [Claude]**: Added new enums:
- `character_class`: WHITESPACE, LETTER, DIGIT, UNDERSCORE, PUNCTUATION, BRACKET, QUOTE, OTHER
- `token_position`: SOLO, START, MIDDLE, END
- `pair_type`: NONE, COMMENT, PAREN, BRACKET, BRACE, DOUBLE_QUOTE, SINGLE_QUOTE
- `pair_role`: NONE, OPENER, CLOSER

**Action [Claude]**: Added bit field masks for neighbor (8-bit) and context (32-bit) encoding

**Action [Claude]**: Expanded `struct cell` from 8 to 12 bytes:
- `uint32_t codepoint`
- `uint16_t syntax`
- `uint8_t neighbor` (NEW: class + position)
- `uint8_t flags` (NEW: reserved)
- `uint32_t context` (NEW: pair_id + type + role)

**Action [Claude]**: Added `next_pair_id` to `struct buffer`

**Action [Claude]**: Added inline helper functions:
- `neighbor_get_class()`, `neighbor_get_position()`, `neighbor_encode()`
- `context_get_pair_id()`, `context_get_pair_type()`, `context_get_pair_role()`, `context_encode()`

**Action [Claude]**: Added `classify_codepoint()` - classifies Unicode codepoints into character classes

**Action [Claude]**: Added `classes_form_word()` - determines if two classes form a word together

**Action [Claude]**: Added `neighbor_compute_line()` - computes neighbor data for a line

**Action [Claude]**: Added word boundary functions:
- `cell_is_word_start()`
- `cell_is_word_end()` (unused, reserved for future double-click selection)
- `find_prev_word_start()`
- `find_next_word_start()`

**Action [Claude]**: Added pair functions:
- `buffer_allocate_pair_id()`
- `buffer_compute_pairs()` - scans entire buffer to match pairs
- `buffer_find_pair_partner()` - finds matching delimiter

**Action [Claude]**: Added `syntax_is_in_block_comment()` - checks if position is inside block comment

**Action [Claude]**: Updated `syntax_highlight_line()`:
- Added `row` parameter
- Added multiline comment support via `syntax_is_in_block_comment()`

**Action [Claude]**: Updated all call sites of `syntax_highlight_line` with row parameter

**Action [Claude]**: Added Ctrl+Arrow key codes: `KEY_CTRL_ARROW_LEFT`, `KEY_CTRL_ARROW_RIGHT`

**Action [Claude]**: Added escape sequence handling for `\x1b[1;5C` and `\x1b[1;5D`

**Action [Claude]**: Added word movement handlers in `editor_move_cursor()`

**Action [Claude]**: Updated edit operations to recompute neighbors:
- `buffer_insert_cell_at_column`
- `buffer_delete_grapheme_at_column`
- `buffer_insert_newline`

**Action [Claude]**: Updated `file_open` to call `buffer_compute_pairs` and syntax highlighting

**Bug Fix [Claude]**: Fixed C comments containing `/*` inside them breaking parsing

**Test [Claude]**: Verified multiline comment highlighting works correctly

**Test [Claude]**: Verified `/***` style comments highlight correctly

**Test [Claude]**: Verified build succeeds (1 warning: unused `cell_is_word_end`)

**Action [User]**: Requested merge to main

**Action [Claude]**: Committed Phase 6 (dc09108) - 771 insertions, 27 deletions

**Action [Claude]**: Merged `neighbor-pair-entanglement` to `main` (fast-forward)

**Action [User]**: Requested fix for unused function warning

**Action [Claude]**: Added `__attribute__((unused))` to `cell_is_word_end` function

**Action [User]**: Requested Ctrl+Arrow word movement to wrap across lines

**Action [Claude]**: Modified KEY_CTRL_ARROW_LEFT and KEY_CTRL_ARROW_RIGHT handlers:
- Ctrl+Left at column 0 now jumps to end of previous line
- Ctrl+Right at end of line now jumps to start of next line

**Action [User]**: Asked about Ctrl+Left/Right asymmetry, requested fix

**Action [Claude]**: Fixed `find_next_word_start` to use token boundaries like `find_prev_word_start`:
- Now both directions stop at punctuation, brackets, word starts
- Example: `hello, world` → stops at `hello`, `,`, `world` in both directions

**Status**: Phase 6 complete with symmetric word movement

---

### Phase 7: Selection (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 7
- Text selection with Shift+Arrow keys
- Mouse support (click, drag, scroll, double-click, triple-click)
- Selection highlighting
- Selection deletion/replacement

**Action [Claude]**: Created branch `selection`

**Action [Claude]**: Updated header to Phase 7, version to 0.7.0

**Action [Claude]**: Added selection key codes:
- `KEY_SHIFT_ARROW_UP/DOWN/LEFT/RIGHT`
- `KEY_SHIFT_HOME/END`
- `KEY_SHIFT_PAGE_UP/DOWN`
- `KEY_CTRL_SHIFT_ARROW_LEFT/RIGHT`
- `KEY_MOUSE_EVENT`

**Action [Claude]**: Added mouse event types:
- `MOUSE_LEFT_PRESS`, `MOUSE_LEFT_RELEASE`, `MOUSE_LEFT_DRAG`
- `MOUSE_SCROLL_UP`, `MOUSE_SCROLL_DOWN`

**Action [Claude]**: Added `struct mouse_input` for parsed mouse events

**Action [Claude]**: Added selection fields to `struct editor_state`:
- `selection_anchor_row`, `selection_anchor_column`
- `selection_active`

**Action [Claude]**: Added `THEME_SELECTION` color (Tokyo Night: #283b50)

**Action [Claude]**: Updated `input_read_key()`:
- Handle modifier codes (2=Shift, 5=Ctrl, 6=Ctrl+Shift)
- Parse SGR extended mouse sequences

**Action [Claude]**: Added terminal mouse functions:
- `terminal_enable_mouse()` - enables click, drag, SGR modes
- `terminal_disable_mouse()` - disables mouse tracking

**Action [Claude]**: Added selection functions:
- `selection_start()` - begin selection at current cursor
- `selection_clear()` - deactivate selection
- `selection_get_range()` - get ordered start/end positions
- `selection_contains()` - check if position is in selection
- `selection_is_empty()` - check if selection has zero extent

**Action [Claude]**: Updated `render_line_content()`:
- Added `file_row` parameter
- Render selection background when position in selection range

**Action [Claude]**: Updated `editor_move_cursor()`:
- Shift+Arrow keys extend selection
- Ctrl+Shift+Arrow for word selection
- Plain arrow keys clear selection

**Action [Claude]**: Added `editor_delete_selection()`:
- Handles multi-line selection deletion
- Joins partial lines at boundaries

**Action [Claude]**: Updated editing functions to delete selection first:
- `editor_insert_char()`
- `editor_insert_newline()`
- Backspace/Delete handlers

**Action [Claude]**: Added mouse handling functions:
- `screen_column_to_cell()` - convert screen X to cell column
- `editor_select_word()` - select word at position using neighbor layer
- `editor_select_line()` - select entire line
- `input_parse_sgr_mouse()` - parse SGR mouse sequences
- `editor_handle_mouse()` - process mouse events

**Action [Claude]**: Added Escape key to clear selection

**Bug Fix [Claude]**: Fixed duplicate `else if` blocks in input handling

**Bug Fix [Claude]**: Fixed unused `terminal_disable_mouse` warning

**Test [Claude]**: Verified build succeeds with no warnings

**Test [Claude]**: Verified editor renders correctly with Phase 7 changes

**Status**: Phase 7 complete, merged to main

---

### Phase 8: Adaptive Scroll Speed (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 8
- Velocity-based adaptive mouse scroll speed
- Slow scrolling for precision, fast scrolling for navigation

**Action [Claude]**: Created branch `adaptive-scroll`

**Action [Claude]**: Updated header to Phase 8, version to 0.8.0

**Action [Claude]**: Added adaptive scroll constants:
- `SCROLL_VELOCITY_DECAY` (0.7) - exponential smoothing factor
- `SCROLL_MIN_LINES` (1) - minimum lines to scroll
- `SCROLL_MAX_LINES` (20) - maximum lines to scroll
- `SCROLL_VELOCITY_SLOW` (4.0) - events/sec threshold for min scroll
- `SCROLL_VELOCITY_FAST` (18.0) - events/sec threshold for max scroll
- `SCROLL_VELOCITY_TIMEOUT` (0.4) - seconds before velocity resets

**Action [Claude]**: Added scroll velocity tracking state:
- `last_scroll_time` - timestamp of last scroll event
- `scroll_velocity` - smoothed velocity value
- `last_scroll_direction` - direction of last scroll (-1/0/1)

**Action [Claude]**: Added `calculate_adaptive_scroll()` function:
- Uses `clock_gettime(CLOCK_MONOTONIC)` for timing
- Exponential moving average for velocity smoothing
- Linear interpolation from velocity to scroll amount
- Resets on direction change, timeout, or first scroll

**Action [Claude]**: Updated `MOUSE_SCROLL_UP` handler:
- Uses adaptive scroll amount instead of fixed 3 lines
- Improved edge case handling for cursor position

**Action [Claude]**: Updated `MOUSE_SCROLL_DOWN` handler:
- Uses adaptive scroll amount instead of fixed 3 lines
- Calculates max_offset to prevent over-scrolling

**Test [Claude]**: Verified build succeeds with no warnings

**Status**: Phase 8 complete, merged to main

---

### Phase 9: Clipboard Integration (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 9
- Copy, cut, paste operations
- System clipboard integration with internal fallback

**Action [Claude]**: Created branch `clipboard-integration`

**Action [Claude]**: Updated header to Phase 9, version to 0.9.0

**Action [Claude]**: Added clipboard state:
- `internal_clipboard` buffer for fallback
- `enum clipboard_tool` (XCLIP, XSEL, WL, INTERNAL)
- `detected_clipboard_tool` cached detection result

**Action [Claude]**: Added `clipboard_detect_tool()`:
- Checks WAYLAND_DISPLAY for wl-copy
- Falls back to xclip, xsel, or internal buffer

**Action [Claude]**: Added `clipboard_copy()`:
- Pipes text to xclip/xsel/wl-copy
- Uses internal buffer as fallback

**Action [Claude]**: Added `clipboard_paste()`:
- Reads from xclip/xsel/wl-paste
- Dynamic buffer growth for large pastes
- Uses internal buffer as fallback

**Action [Claude]**: Added `selection_get_text()`:
- Extracts selection as UTF-8 string
- Handles multi-line selections with newlines

**Action [Claude]**: Added editor clipboard functions:
- `editor_copy()` - copy selection to clipboard
- `editor_cut()` - copy and delete selection
- `editor_paste()` - paste at cursor, replaces selection

**Action [Claude]**: Added key bindings:
- Ctrl+C - copy
- Ctrl+X - cut
- Ctrl+V - paste

**Action [Claude]**: Added internal clipboard cleanup on exit

**Test [Claude]**: Verified build succeeds with no warnings

**Status**: Phase 9 complete, ready for commit

---

### Phase 10: Undo/Redo (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 10
- Unlimited undo/redo with Ctrl+Z and Ctrl+Y
- Operation-based undo (not snapshot-based)
- Auto-grouping of rapid edits

**Action [Claude]**: Created branch `undo-redo`

**Action [Claude]**: Updated header to Phase 10, version to 0.10.0

**Action [Claude]**: Added undo constants:
- `UNDO_GROUP_TIMEOUT` (0.5) - seconds before new group
- `INITIAL_UNDO_CAPACITY` (64) - initial group array size
- `INITIAL_OPERATION_CAPACITY` (32) - initial ops per group

**Action [Claude]**: Added data structures:
- `enum edit_operation_type`: INSERT_CHAR, DELETE_CHAR, INSERT_NEWLINE, DELETE_NEWLINE, DELETE_TEXT
- `struct edit_operation`: stores operation details (row, col, codepoint, text)
- `struct undo_group`: collection of operations with cursor positions
- `struct undo_history`: array of groups with current index

**Action [Claude]**: Added `undo_history` field to `struct buffer`

**Action [Claude]**: Added undo history management functions:
- `undo_history_init()` - initialize empty history
- `undo_history_free()` - free all resources
- `undo_begin_group()` - start new or continue existing group
- `undo_end_group()` - finalize group with cursor position

**Action [Claude]**: Added operation recording functions:
- `undo_record_operation()` - add operation to current group
- `undo_record_insert_char()` - record char insertion
- `undo_record_delete_char()` - record char deletion
- `undo_record_insert_newline()` - record line split
- `undo_record_delete_newline()` - record line join
- `undo_record_delete_text()` - record selection delete

**Action [Claude]**: Added no-record buffer operation variants:
- `buffer_insert_cell_no_record()` - insert without undo recording
- `buffer_delete_cell_no_record()` - delete without undo recording
- `buffer_insert_newline_no_record()` - newline without undo recording
- `buffer_delete_newline_no_record()` - join without undo recording

**Action [Claude]**: Added undo application functions:
- `undo_reverse_operation()` - reverse a single operation
- `undo_apply_operation()` - apply a single operation (for redo)

**Action [Claude]**: Added editor undo/redo functions:
- `editor_undo()` - undo last group, restore cursor
- `editor_redo()` - redo next group, restore cursor

**Action [Claude]**: Integrated undo recording with edit operations:
- `editor_insert_character()` - records insert_char
- `editor_insert_newline()` - records insert_newline
- `editor_delete_character()` - records delete_char or delete_newline
- `editor_handle_backspace()` - records delete_char or delete_newline
- `editor_delete_selection()` - records delete_text with saved text
- `editor_paste()` - records all insertions in single group

**Action [Claude]**: Added key bindings:
- Ctrl+Z - undo
- Ctrl+Y - redo

**Action [Claude]**: Added initialization in `buffer_init()` and cleanup in `buffer_free()`

**Test [Claude]**: Verified build succeeds with no warnings

**Status**: Phase 10 complete, merged to main

---

### Phase 11: Incremental Search (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 11
- Ctrl+F to enter search mode
- Real-time highlighting of matches
- F3/Shift+F3 for find next/previous
- Case-insensitive search
- Wrap-around search

**Action [Claude]**: Created branch `incremental-search`

**Action [Claude]**: Updated header to Phase 11, version to 0.11.0

**Action [Claude]**: Added search theme colors:
- `THEME_SEARCH_MATCH` (0x3d, 0x59, 0xa1) - blue for other matches
- `THEME_SEARCH_CURRENT` (0xe0, 0xaf, 0x68) - gold for current match

**Action [Claude]**: Added key codes: `KEY_F3`, `KEY_SHIFT_F3`

**Action [Claude]**: Added search state structure:
- `active`, `query`, `query_length`
- `saved_cursor_row/column`, `saved_row/column_offset`
- `match_row`, `match_column`, `has_match`
- `direction` (1 = forward, -1 = backward)

**Action [Claude]**: Added search functions:
- `search_enter()` - enter search mode, save position
- `search_exit()` - exit search mode, optionally restore position
- `search_matches_at()` - case-insensitive match at position
- `search_query_cell_count()` - count cells in query
- `search_find_next()` - find next match with wrap
- `search_find_previous()` - find previous match with wrap
- `search_update()` - update after query changes
- `search_match_type()` - determine highlight type for cell

**Action [Claude]**: Updated F3/Shift+F3 detection in `input_read_key()`:
- `\x1b[13~` for F3
- `\x1b[13;2~` for Shift+F3
- `\x1bOR` for F3 (alternate)

**Action [Claude]**: Updated `render_line_content()`:
- Check `search_match_type()` for each cell
- Apply gold background for current match
- Apply blue background for other matches
- Priority: current match > other match > selection > normal

**Action [Claude]**: Updated `render_draw_message_bar()`:
- Show "Search: <query>" when search active
- Show "(no match)" suffix when no matches found

**Action [Claude]**: Added `search_handle_key()`:
- Escape to cancel (restore position)
- Enter to confirm (keep position)
- Backspace to delete from query
- F3/Ctrl+G for find next
- Shift+F3 for find previous
- Printable characters appended to query

**Action [Claude]**: Added key bindings:
- Ctrl+F - enter search mode
- F3 - find next (also works outside search mode)
- Shift+F3 - find previous (also works outside search mode)

**Test [Claude]**: Verified build succeeds with no warnings

**Action [Claude]**: Simplified search navigation keys:
- Replaced F3/Shift+F3 with Alt+N (next) and Alt+P (previous)
- Added Arrow keys and scroll wheel for navigation in search mode

**Action [Claude]**: Added `search_center_on_match()` - centers viewport on current match

**Status**: Phase 11 complete, merged to main

---

### WCAG 2.1 AA Color Contrast (COMPLETED)

**Action [User]**: Requested WCAG-compliant color contrast

**Action [Claude]**: Added color contrast functions:
- `color_linearize()` - sRGB to linear conversion
- `color_luminance()` - relative luminance per WCAG 2.1
- `color_contrast_ratio()` - contrast between two colors
- `color_ensure_contrast()` - auto-adjust for 4.5:1 minimum

**Action [Claude]**: Applied contrast adjustment to search highlights and selection

**Status**: Complete, merged to main

---

### Tritanopia-Friendly Theme (COMPLETED)

**Action [User]**: Requested color scheme optimized for Tritanopia color blindness

**Action [Claude]**: Replaced Tokyo Night theme with new design:
- Luminance as primary differentiator
- Red-cyan color axis (visible to Tritanopia)
- Avoids blue-yellow distinctions
- All combinations maintain WCAG AA compliance (≥4.5:1)

**Syntax colors**: magenta keywords, cyan types/functions, coral strings, gray comments

**Status**: Complete, merged to main

---

### Cursor Line Highlight (COMPLETED)

**Action [User]**: Requested subtle highlight for current line

**Action [Claude]**: Added `THEME_CURSOR_LINE` (#1E1E1E)
- Highlights entire line containing cursor
- Extends to line number gutter
- Lower priority than search/selection highlights

**Status**: Complete, merged to main

---

## Session: 2025-12-29

### Phase 12: Soft Line Wrapping (COMPLETED)

**Action [User]**: Requested soft wrap implementation

#### Phase 12A: Foundation & Wrap Point Calculation

**Action [Claude]**: Added wrap mode enums:
- `enum wrap_mode`: WRAP_NONE, WRAP_WORD, WRAP_CHAR
- `enum wrap_indicator`: NONE, CORNER(⎿), HOOK(↪), ARROW(→), DOT(·), FLOOR(⌊), BOTTOM(⌞), RETURN(↳), BOX(└)

**Action [Claude]**: Added wrap state to `struct editor_state`:
- `wrap_mode` (default: WRAP_WORD)
- `wrap_indicator` (default: WRAP_INDICATOR_RETURN)

**Action [Claude]**: Added key bindings:
- Alt+Z - cycle wrap mode (Off → Word → Char)
- Alt+Shift+Z - cycle wrap indicator

**Action [Claude]**: Added `line_find_wrap_point()` - finds optimal break point using neighbor layer

#### Phase 12B: Wrap Cache Infrastructure

**Action [Claude]**: Added wrap cache fields to `struct line`:
- `wrap_columns` - array of segment start columns
- `wrap_segment_count` - number of visual segments
- `wrap_cache_width` - cached text area width
- `wrap_cache_mode` - cached wrap mode

**Action [Claude]**: Added cache functions:
- `line_compute_wrap_points()` - compute wrap points for a line
- `line_invalidate_wrap_cache()` - invalidate single line cache
- `buffer_invalidate_all_wrap_caches()` - invalidate all caches

**Action [Claude]**: Hooked invalidation into edit operations and resize

#### Phase 12C: Wrapped Line Rendering

**Action [Claude]**: Added helper functions:
- `line_ensure_wrap_cache()` - lazy cache computation
- `line_get_segment_for_column()` - find segment containing column
- `line_get_segment_start/end()` - segment boundaries
- `editor_get_text_width()` - text area width calculation

**Action [Claude]**: Rewrote `render_draw_rows()`:
- Iterates through (file_row, segment) pairs
- Shows line number for segment 0, wrap indicator for continuations
- Cursor line highlight only on segment containing cursor

**Action [Claude]**: Updated `render_line_content()`:
- Segment mode (wrap enabled): render cells from start_cell to end_cell
- Scroll mode (wrap disabled): horizontal scrolling with column_offset

#### Phase 12D: Cursor Movement on Wrapped Lines

**Action [Claude]**: Added visual column helpers:
- `line_get_visual_column_in_segment()` - visual X within segment
- `line_find_column_at_visual()` - cell column at visual position

**Action [Claude]**: Updated Up/Down arrow handling:
- WRAP_NONE: move by logical line
- WRAP_WORD/CHAR: move by screen row (segment), preserve visual column

**Action [Claude]**: Updated Home/End handling:
- WRAP_NONE: go to line start/end
- WRAP_WORD/CHAR: go to segment start/end, press again at boundary to move to adjacent segment

#### Phase 12E: Mouse Click & Scroll

**Action [Claude]**: Added `screen_row_to_line_segment()` - maps screen coordinates to (line, segment)

**Action [Claude]**: Updated mouse handlers:
- MOUSE_LEFT_PRESS: segment-aware click positioning
- MOUSE_LEFT_DRAG: segment-aware drag selection
- Uses `line_find_column_at_visual()` for accurate column mapping

**Action [Claude]**: Added `calculate_max_row_offset()` - wrap-aware scroll limit

**Action [Claude]**: Updated scroll wheel to use wrap-aware max_offset

#### Bug Fixes

**Bug [User]**: Cursor and highlight bar became desynced when navigating with arrow keys

**Bug Fix [Claude]**: Fixed `render_refresh_screen()`:
- Was calculating cursor screen position as `(cursor_row - row_offset)` assuming 1 line = 1 row
- Now sums segment counts from row_offset to cursor_row
- Calculates visual column within cursor's segment

**Bug Fix [Claude]**: Fixed `editor_scroll()`:
- Same incorrect assumption for off-screen detection
- Now calculates actual screen row by summing segments
- Disabled horizontal scrolling in wrap mode (not needed)

**Status**: Phase 12 complete, merged to main

---

### Phase 13: Essential Editing Commands (COMPLETED)

**Action [User]**: Requested branch and implementation of Phase 13
- Go to Line (Ctrl+G)
- Select All (Ctrl+A)
- Delete Line (Alt+K)
- Duplicate Line (Alt+D)

**Action [Claude]**: Created branch `essential-editing-commands`

**Action [Claude]**: Updated header to Phase 13, version to 0.13.0

**Action [Claude]**: Added new key codes:
- `KEY_ALT_K` - for delete line
- `KEY_ALT_D` - for duplicate line

**Action [Claude]**: Added go-to-line mode state structure (`struct goto_state`)

**Action [Claude]**: Added Alt+K and Alt+D handling in `input_read_key()`

**Action [Claude]**: Added Go to Line functions:
- `goto_enter()` - enter go-to-line mode, save position
- `goto_exit()` - exit mode, optionally restore position
- `goto_execute()` - jump to line number and center on screen
- `goto_handle_key()` - handle keypresses in goto mode (digits, Enter, Escape, Backspace)
- Live preview: cursor jumps as digits are typed

**Action [Claude]**: Added `editor_select_all()` - selects entire buffer

**Action [Claude]**: Added `editor_delete_line()`:
- Deletes current line with undo support
- Records deleted text for undo
- Handles edge case of deleting last line

**Action [Claude]**: Added `editor_duplicate_line()`:
- Duplicates current line below
- Uses newline + character insertions for proper undo
- Cursor moves to duplicated line

**Action [Claude]**: Updated `render_draw_message_bar()` to show "Go to line:" prompt

**Action [Claude]**: Added key bindings:
- Ctrl+G - `goto_enter()`
- Ctrl+A - `editor_select_all()`
- Alt+K - `editor_delete_line()`
- Alt+D - `editor_duplicate_line()`

**Test [Claude]**: Build succeeds (1 expected warning: unused `buffer_get_total_screen_rows`)

**Action [Claude]**: Committed (624942e) and merged to main

**Status**: Phase 13 complete, merged to main

---

### Phase 14: Line Operations (IN PROGRESS)

**Action [User]**: Requested branch and implementation of Phase 14
- Move Line Up/Down (Alt+Up, Alt+Down)
- Indent/Outdent (Tab with selection, Shift+Tab)

**Action [Claude]**: Created branch `line-operations`

**Action [Claude]**: Updated header to Phase 14, version to 0.14.0

**Action [Claude]**: Added new key codes:
- `KEY_ALT_ARROW_UP`, `KEY_ALT_ARROW_DOWN` - for line movement
- `KEY_SHIFT_TAB` - for outdent

**Action [Claude]**: Added key detection in `input_read_key()`:
- Alt+Arrow via modifier code '3' in CSI sequences
- Shift+Tab via `ESC [ Z` sequence

**Action [Claude]**: Added line movement functions:
- `buffer_swap_lines()` - swaps two lines in buffer
- `editor_move_line_up()` - moves current line up
- `editor_move_line_down()` - moves current line down

**Action [Claude]**: Added indent/outdent functions:
- `editor_indent_lines()` - inserts tab at start of lines
- `editor_outdent_lines()` - removes leading tab/spaces

**Action [Claude]**: Added key bindings:
- Alt+Up - `editor_move_line_up()`
- Alt+Down - `editor_move_line_down()`
- Tab (with selection) - `editor_indent_lines()`
- Tab (no selection) - inserts tab character
- Shift+Tab - `editor_outdent_lines()`

**Test [Claude]**: Build succeeds (1 expected warning: unused `buffer_get_total_screen_rows`)

**Status**: Ready for commit

---

## Current State

- **Branch**: `line-operations`
- **Version**: 0.14.0 (line operations)
- **Build**: Clean (1 expected warning: unused `buffer_get_total_screen_rows`)
- **Source**: `src/edit.c` (~6900 lines)

---

## File Structure

```
/home/edward/repos/edit/
├── src/
│   └── edit.c          # Main editor source (~6900 lines)
├── third_party/
│   └── utflite/        # UTF-8 library
├── Makefile
├── CLAUDE.md           # AI assistant guidance
├── CODING_STANDARDS.md # Code style rules
├── TRANSACTION_LOG.md  # This file
└── (other files)
```

---

## Quick Reference

### Branches
- `main` - stable, contains all phases through Phase 13
- `line-operations` - Phase 14 in progress
- All feature branches merged to main

### Key Bindings (New in Phase 11-14)
- Ctrl+F - enter search mode
- Alt+N - find next match
- Alt+P - find previous match
- Alt+Z - cycle wrap mode
- Alt+Shift+Z - cycle wrap indicator
- Ctrl+G - go to line
- Ctrl+A - select all
- Alt+K - delete line
- Alt+D - duplicate line
- Alt+Up - move line up
- Alt+Down - move line down
- Tab (with selection) - indent lines
- Shift+Tab - outdent lines

### Build Commands
```bash
make          # Build editor
make clean    # Clean build artifacts
make test     # Run UTF-8 tests
```

### Test Commands
```bash
./edit <file>                    # Open file
timeout 0.3 ./edit <file>        # Quick visual test
```
