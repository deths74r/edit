# edit

A terminal text editor in C.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ edit                                                               src/edit.c│
├────┬─────────────────────────────────────────────────────────────────────────┤
│  1 │ /*                                                                      │
│  2 │  * edit.c - Core editing operations                                     │
│  3 │  */                                                                     │
│  4 │                                                                         │
│  5 │ #include "edit.h"                                                       │
│  6 │                                                                         │
│  7 │ struct editor_state editor;                                             │
│  8 │                                                                         │
├────┴─────────────────────────────────────────────────────────────────────────┤
│ HELP: Ctrl-S = save | Ctrl-Q = quit | F2 = toggle line numbers              │
└──────────────────────────────────────────────────────────────────────────────┘
```

## What It Does

Opens files. Edits text. Saves changes. Handles Unicode properly—emoji, CJK, combining marks. Highlights C syntax. Wraps long lines. Multiple cursors. Find and replace with regex. Undo that groups keystrokes. Mouse support. Two built-in themes. Crash recovery.

No plugins. No configuration language. No LSP. No async runtime. Just a text editor.

## Install

```bash
git clone https://github.com/yourusername/edit.git
cd edit
make && make install
```

Installs to `~/.local/bin`. Uninstall with `make uninstall`.

## Keys

**Files:** `Ctrl+S` save, `Ctrl+O` open, `Ctrl+Q` quit

**Move:** arrows, `Ctrl+Arrow` by word, `Home`/`End`, `Page Up`/`Down`, `Ctrl+G` goto line, `Ctrl+]` jump to bracket

**Select:** `Shift+Arrow`, `Ctrl+A` all, `Ctrl+D` word/next occurrence, double-click word, triple-click line

**Edit:** `Ctrl+Z` undo, `Ctrl+Y` redo, `Ctrl+C`/`X`/`V` copy/cut/paste, `Alt+K` delete line, `Alt+D` duplicate, `Alt+Up`/`Down` move line, `Tab`/`Shift+Tab` indent, `Ctrl+/` comment

**Search:** `Ctrl+F` find, `Ctrl+R` replace, `Alt+C` case, `Alt+W` whole word, `Alt+R` regex, `Alt+A` replace all

**View:** `F2` line numbers, `F3` whitespace, `F4` column marker, `F5` themes, `Alt+Z` wrap mode

## Source

19,000 lines of C across 16 modules:

```
src/
├── main.c          Entry point, main loop
├── edit.c          Core editing, keypress handling
├── editor.c        State management, dialogs
├── buffer.c        Lines and text storage
├── syntax.c        Highlighting, word boundaries
├── undo.c          Operation-based undo/redo
├── input.c         Keyboard and mouse parsing
├── render.c        Screen drawing
├── terminal.c      Raw mode, resize
├── theme.c         Colors, contrast enforcement
├── search.c        Find/replace with async
├── worker.c        Background thread
├── autosave.c      Crash recovery
├── dialog.c        Theme picker, file browser
├── clipboard.c     System clipboard
├── error.c         Linux kernel-style errors
└── types.h         All shared types (1,145 lines)
```

## How It Works

**Memory:** Files are memory-mapped. Lines start cold (just a pointer). Scroll to a line and it warms up (decoded to cells). Edit it and it's hot (mmap stale). Opening a 100K line file allocates ~2.4MB of line metadata. Lines you never view stay cold.

**Characters:** Each character is 12 bytes—codepoint, syntax token, word boundary info, bracket pair ID. Pay once when editing, then everything is O(1): scrolling doesn't re-highlight, Ctrl+Arrow doesn't scan, jump-to-bracket doesn't search.

**Brackets:** Single pass matches all delimiters and assigns pair IDs. Jump-to-bracket looks up the ID and finds its partner. Block comments spanning 1000 lines? Same cost as adjacent parens.

**Undo:** Records operations, not snapshots. Keystrokes within one second group together. Memory scales with edits, not file size.

**Search:** Runs on background thread. Results stream in as found. UI stays responsive.

**Crash recovery:** Swap file written every 30 seconds. Fatal signals trigger emergency save. Your work survives crashes.

## Themes

Two built-in: Dark and Light. Add custom themes to `~/.edit/themes/`:

```ini
[theme]
name = Dracula
background = 282a36
foreground = f8f8f2
keyword = ff79c6
string = f1fa8c
comment = 6272a4
```

All colors auto-adjust for WCAG 2.1 AA contrast (4.5:1 ratio).

## Extending

Add a key binding: define in `types.h`, detect in `input.c`, handle in `edit.c`.

Add syntax token: enum in `types.h`, detection in `syntax.c`, color in themes.

Add background task: type in `worker.h`, handler in worker thread, result processing in main loop.

Code style: Linux kernel. Tabs, `snake_case`, explicit `struct`, `module_verb_object()` naming.

## Requirements

C17 compiler, make, pthreads. Linux, macOS, BSD. No other dependencies—UTF-8 handling is bundled (utflite).

## License

MIT
