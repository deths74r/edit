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

- [x] **Find and replace** — Alt+R, two-prompt flow, y/n/a/ESC confirmation, replace-all with count
- [x] **Case-insensitive search toggle** — Alt+C during search, editor_memmem with tolower
- [x] **All-match highlighting** — all visible matches shown with HL_MATCH during search
- [x] **Match count** — "Match N of M" in status bar during search
- [x] **Wrap indicator** — [Wrapped] shown when search crosses file boundaries
- [x] **8+ syntax languages** — Python, JavaScript, Go, Rust, Bash, JSON, YAML, Markdown
- [x] **Regex search** — Alt+X toggle, POSIX ERE via regcomp/regexec
- [x] **Search history** — Up/Down arrows browse previous searches, 50-entry buffer
- [x] **Read from stdin** — `cat file | edit` or `edit -`, reopens /dev/tty

---

## 0.6.0 — "Smart"

*Theme: The cell model pays dividends.*

- [x] **Bracket matching** — Alt+] jump, highlight match, depth scan capped at 5000 lines
- [x] **Bracket pair colorization** — cycling colors by nesting depth (keyword1/2/string/number)
- [x] **Git gutter markers** — + (added) and ~ (modified) in line number gutter, reset on save
- [x] **Indent/dedent block** — Tab/Shift+Tab on selection, skips empty lines
- [x] **Comment toggle** — Alt+/ using filetype comment prefix (// or #)
- [x] **Trailing whitespace visualization** — muted background on trailing spaces/tabs
- [x] **Horizontal scroll indicator** — < and > at viewport edges
- [x] **Improved status bar** — filetype, percentage, proportional truncation, colored [+]

---

## 0.7.0 — "Yours"

*Theme: Make it your own.*

- [x] **Config file** — ~/.config/edit/config, key=value INI, XDG discovery
- [x] **Configurable tab width** — config + --tabstop=N CLI flag
- [ ] **Configurable keybindings** in config (deferred — config infrastructure ready)
- [x] **Persistent theme selection** — Alt+T saves to config automatically
- [x] **Colorblind-accessible theme** — "Clarity" blue/orange/yellow palette
- [x] **Match background highlight** — match_background field on all themes
- [x] **True color detection** — COLORTERM + TERM env var check
- [x] **Soft word wrap toggle** — Alt+W, rendering-only, no data model change
- [x] **Column ruler** — config ruler=N, subtle vertical line

---

## 0.8.0 — "Scalable"

*Theme: Handle anything you throw at it.*

- [x] **WARM→COLD cooling** — frees cells for distant WARM lines, 100 lines/frame gradual scan
- [x] **Predictive line warming** — pre-warms screen_rows/2 lines ahead of scroll direction
- [ ] **Gap buffer for lines array** — deferred (current O(n) acceptable for typical file sizes)
- [x] **Streaming save** — lines written directly to fd, peak memory O(max_line_size)
- [ ] **Cell struct optimization** — deferred (flags/context fields in active use)
- [x] **Binary file detection** — NUL scan in first 8KB, warning in status bar
- [x] **Long line handling** — cached_render_width on struct line, invalidated on mutation
- [x] **Pre-computed keyword lengths** — done in 0.4.0

---

## 0.9.0 — "Resilient"

*Theme: Defend your work.*

- [x] **Swap file / periodic autosave** — .edit.swp every 30s, PID-aware recovery on open
- [x] **File change detection** — stat() mtime/dev/ino check at save time, overwrite prompt
- [x] **Remember cursor position** — ~/.local/share/edit/cursor_history, 200-entry LRU
- [x] **Man page** — edit.1 with all sections, ready for packaging

---

## 1.0.0 — "Complete"

*Theme: No asterisks, no apologies.*

- [x] **All features stable and regression-tested** — 237 assertions passing
- [x] **Comprehensive test suite** — 83 tests across 7 categories with MC/DC coverage
- [ ] Package-ready for Homebrew, apt, AUR (skipped per decision)
- [ ] Performance benchmarks published (skipped per decision)
- [x] **Clean error handling** — line_to_bytes NULL checks at all 13 call sites
- [x] **Read-only file indicator** — [RO] in status bar, access(W_OK) check
- [x] **File locking** — flock(LOCK_EX|LOCK_NB), read-only fallback on EWOULDBLOCK
- [x] **Documentation** — man page, updated README, help screen
- [x] **Makefile install/uninstall** — PREFIX support, release build dependency

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
