# edit Roadmap: 0.2.0 → 1.0.0

*Synthesized from architectural, correctness, performance, product, and UX reviews of the full codebase.*

---

## 0.2.0 — "Trustworthy"

*Theme: You can trust edit with real files.*

### Safety

- [x] **Atomic saves** — write-to-temp + `rename()` instead of truncate-then-write
- [x] **Signal handling** — `SIGTERM`/`SIGHUP` handlers via `sigaction()`, `SIGPIPE` ignored
- [x] **`terminal_die()` reentrancy guard** — prevents infinite recursion on OOM during emergency save
- [x] **`fsync()` before close** on save
- [x] **Fix `editor_save_accept()` memory leak** — free old filename before reassignment
- [x] **Filter negative key values** — blocks control chars and unrecognized escapes from insertion
- [x] **Line array capacity tracking** — doubling growth via `editor_lines_ensure_capacity()`

### Core Feature

- [x] **Undo/redo** — 6 operation types, 256-group bounded stack, 500ms time-based coalescing, Ctrl+U/Ctrl+R

### Quick UX Wins

- [x] **Help screen** (F1 or Alt+?) — state save/restore loads help text into buffer, ESC to return
- [x] **Ctrl+Z suspend** — restore terminal, SIGTSTP, re-enable on resume
- [x] **Ctrl key aliases** — Ctrl+S/Q/F/G alongside Alt bindings

---

## 0.3.0 — "Editable"

*Theme: Select, move, and manipulate text like a real editor.*

- [x] **Text selection** — Shift+Arrow/Home/End, cell flags rendering with inverted colors
- [x] **Mouse drag selection** — SGR button-event tracking (?1002h), click-and-drag
- [x] **Cut/copy/paste** — Alt+C/X/V, internal clipboard + OSC 52 system clipboard
- [x] **Cut line / duplicate line** — Alt+Shift+K cut line, Alt+D duplicate line
- [x] **Auto-indent** — copies leading whitespace (tabs/spaces) from current line on Enter
- [x] **Word movement** — Ctrl+Left/Right with Shift variants for word selection
- [x] **Scroll margin** — SCROLL_MARGIN=5, adapts to small terminals
- [x] **Virtual column preservation** — remembers render column across vertical movement

---

## 0.4.0 — "Fast"

*Theme: The architecture starts delivering on its promise.*

### Performance

- [x] **Lazy syntax highlighting** — deferred to line_ensure_warm(), syntax_stale flag, COLD lines stay COLD
- [x] **Dirty region tracking** — cursor-only movement skips full redraw, just repositions cursor
- [x] **Zero-copy search on COLD lines** — memmem() directly on mmap bytes, zero allocation
- [x] **`memchr()`-based newline scan** — glibc SIMD-accelerated memchr() for file open
- [x] **Pre-parsed theme colors** — cached RGB parse, sscanf skipped on same-pointer calls
- [x] **ASCII fast path for syntax** — direct char cast + identity mapping for ASCII-only lines
- [x] **Scratch buffers** — reusable buffers in editor_state, no per-line malloc/free
- [x] **`madvise()` hints** — MADV_SEQUENTIAL during scan, MADV_RANDOM after
- [x] **Pre-computed keyword lengths** — strlen() called once at syntax selection

### Architecture

- [x] **Temperature-aware save** — COLD lines copied from mmap, only WARM/HOT use line_to_bytes()

---

## 0.5.0 — "Productive"

*Theme: Daily-driver editing capabilities.*

- [ ] **Find and replace** — single + all occurrences, extending existing search infrastructure
- [ ] **Case-insensitive search toggle** (Alt+C during search)
- [ ] **All-match highlighting** — show all visible matches, active match in distinct color
- [ ] **Match count** — `"Match 3 of 15"` in prompt
- [ ] **Wrap indicator** — `[Wrapped]` when search passes EOF
- [ ] **8+ syntax languages** — Python, JavaScript, Go, Rust, Bash, JSON, YAML, Markdown
- [ ] **Regex search** — POSIX ERE via `<regex.h>`, no new dependency
- [ ] **Search history** — up arrow recalls previous searches
- [ ] **Read from stdin** — `cat file | edit` or `edit -`

---

## 0.6.0 — "Smart"

*Theme: The cell model pays dividends.*

- [ ] **Bracket matching** — highlight + jump-to-match using the reserved `context` field
- [ ] **Bracket pair colorization** — map pair IDs to colors during render
- [ ] **Git gutter markers** — added/modified/deleted indicators (mmap gives us original content for free)
- [ ] **Indent/dedent block** — Tab/Shift+Tab on selection
- [ ] **Comment toggle** — Alt+/ based on filetype
- [ ] **Trailing whitespace visualization** — using cell `flags`
- [ ] **Horizontal scroll indicator** — visual cue when content exists off-screen
- [ ] **Improved status bar** — filetype indicator, file percentage, proportional filename truncation, colorized dirty indicator

---

## 0.7.0 — "Yours"

*Theme: Make it your own.*

- [ ] **Config file** — `~/.config/edit/config`, simple `key = value` INI format
- [ ] **Configurable tab width** — also via `--tabstop=N` CLI flag
- [ ] **Configurable keybindings** in config
- [ ] **Persistent theme selection** across sessions
- [ ] **Colorblind-accessible theme** — blue/orange/yellow palette
- [ ] **Match background highlight** — add `match_background` to theme struct
- [ ] **True color detection** — check `COLORTERM=truecolor`, document requirement
- [ ] **Soft word wrap toggle** — rendering-only change, no data model modification
- [ ] **Column ruler** at configurable width (80, 120, etc.)

---

## 0.8.0 — "Scalable"

*Theme: Handle anything you throw at it.*

- [ ] **WARM→COLD cooling** — free cells for lines that scroll off-screen, keeping memory proportional to viewport
- [ ] **Predictive line warming** — pre-warm lines ahead of scroll direction after each frame
- [ ] **Gap buffer for lines array** — O(1) insert/delete at cursor instead of O(n) memmove
- [ ] **Streaming save** — write line-by-line via `writev()`, peak memory O(max_line_size) instead of O(file_size)
- [ ] **Cell struct optimization** — relocate unused fields to parallel arrays, shrink to 8 bytes
- [ ] **Binary file detection** — NUL byte scan + warning
- [ ] **Long line handling** — cache render-column-to-cell mappings
- [ ] **Pre-computed keyword lengths** in syntax database

---

## 0.9.0 — "Resilient"

*Theme: Defend your work.*

- [ ] **Swap file / periodic autosave** — `.edit.swp` with recovery on startup
- [ ] **File change detection** — `stat()` check at save time for external modifications
- [ ] **Remember cursor position** per file across sessions
- [ ] **Man page** (`edit.1`) — prerequisite for distro packaging

---

## 1.0.0 — "Complete"

*Theme: No asterisks, no apologies.*

- [ ] All features stable and regression-tested
- [ ] Comprehensive test suite (unit tests for cell/line ops, integration tests for workflows)
- [ ] Package-ready for Homebrew, apt, AUR
- [ ] Performance benchmarks published (mmap advantage quantified)
- [ ] Clean error handling for all edge cases
- [ ] Read-only file indicator
- [ ] File locking (`flock`)
- [ ] Documentation complete

---

## Design Philosophy

**edit is a single-file editor.** Modern terminals provide tabs, splits, and multiplexing — edit doesn't need to reinvent that. Let tmux, kitty, and WezTerm handle window management. edit focuses on being the best possible editor for the file in front of you.

**No multi-buffer, no tabs, no splits.** Open another terminal. This keeps the codebase small, the mental model simple, and the architecture focused.

## Architectural Principles

The cell model + temperature system + mmap lazy loading is the foundation. The roadmap sequences work to:

1. **Stop undermining it** (0.2.0: safety bugs; 0.4.0: fix lazy syntax defeating the design)
2. **Build on it** (0.3.0: selection via cell flags; 0.6.0: bracket matching via cell context)
3. **Optimize it** (0.8.0: cooling, gap buffer, struct slimming)

The architecture enables things other editors can't easily do — zero-copy search, instant large file open, per-character metadata without parallel buffers. Features should exploit these advantages rather than fighting the design.
