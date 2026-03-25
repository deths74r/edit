# edit

A terminal text editor written in C23. Single file. Zero dependencies beyond libc.

**v0.9.0** | C23 | MIT License | ~11,800 lines

## What is edit?

`edit` is a terminal text editor that sits between nano and micro in complexity. It has everything you need for comfortable daily editing -- syntax highlighting, undo/redo, search and replace, mouse support, themes -- without the learning curve of vim or the plugin ecosystem of micro. You open a file, edit it, save it, and move on.

The biggest feature is the **command prompt**. Press ESC or Ctrl+Space to open it, then type what you want: `save`, `theme tokyo night`, `sort`, `copy word`, `goto 42`. Over 50 commands cover navigation, selection, clipboard, text transforms, macros, and bookmarks. If what you type does not match a command, it becomes a search -- so the prompt doubles as a quick-find.

Under the hood, `edit` is built on two ideas. First, a **cell-based architecture** where every codepoint carries its own inline syntax and rendering metadata, so there is no separate highlight buffer and no synchronization to worry about. Second, **mmap lazy loading** with a three-level temperature system (COLD/WARM/HOT) that makes files open instantly regardless of size -- only lines that scroll into view pay the decode cost.

The entire editor is a single C file (`edit.c`) plus a header-only Unicode library ([gstr](https://github.com/deths74r/gstr)). No build system magic, no generated code, no third-party dependencies. It compiles in under a second.

## Features

### Editing

- Undo/redo with 256 undo groups and time-based coalescing
- Auto-indent (Enter copies leading whitespace)
- Text selection via keyboard (Shift+Arrow) and mouse drag
- Word selection (Shift+Ctrl+Arrow) and select to start/end of line
- Internal clipboard with copy, cut, paste, and duplicate line
- OSC 52 system clipboard integration (works in xterm, kitty, alacritty, WezTerm, foot, iTerm2, Windows Terminal)
- Word movement (Ctrl+Left/Right) with three-class boundary detection
- Block indent/dedent with Tab/Shift+Tab on selections
- Toggle line comment (filetype-aware)
- Bracket jump to matching pair
- Text transforms: uppercase, lowercase, title case, sort, reverse, trim, collapse, unique

### Search

- Incremental search with results updating on each keypress
- All-match highlighting across visible lines with match counter
- Regex search mode (POSIX ERE), toggled with Alt+X during search
- Case-insensitive toggle (Alt+C during search)
- Find and replace with single and replace-all modes
- Search history (Up/Down arrows during search, 50 entries)
- Arrow keys navigate between matches with wrap-around

### Syntax Highlighting (9 languages)

| Language   | Extensions                |
|------------|---------------------------|
| C/C++      | `.c`, `.h`, `.cpp`        |
| Python     | `.py`                     |
| JavaScript | `.js`, `.jsx`, `.mjs`     |
| Go         | `.go`                     |
| Rust       | `.rs`                     |
| Bash       | `.sh`, `.bash`            |
| JSON       | `.json`                   |
| YAML       | `.yml`, `.yaml`           |
| Markdown   | `.md`, `.markdown`        |

### Display

- 7 color themes (24-bit RGB), cycled with Tab in the command prompt or the `theme` command
- Bracket pair colorization with 4 cycling depth colors
- Git-style gutter markers (added/modified lines since last save)
- Trailing whitespace visualization
- Horizontal scroll indicators (`<` / `>` at screen edges)
- Column ruler (configurable position)
- Soft word wrap
- Line numbers (toggleable)

### Navigation

- 5-line scroll margin keeps context visible
- Virtual column preservation across vertical movement through short lines
- Bracket jump (Ctrl+]) to matching `()`, `{}`, `[]`
- Go to line number (Ctrl+G)
- Mouse click to position, scroll wheel with acceleration
- Page Up/Down scrolls by one screenful

### Files

- Atomic saves (write to temp file, fsync, rename)
- Swap file recovery (`.edit.swp`, written every 30 seconds)
- File change detection (warns before overwriting external modifications)
- Cursor position history across sessions (`~/.local/share/edit/cursor_history`)
- Stdin pipe support (`command | edit` or `edit -`)
- File locking via `flock()` to prevent concurrent editing
- Read-only file detection (status bar warning)
- Binary file detection (warns on NUL bytes in first 8 KB)
- Emergency save on SIGTERM/SIGHUP

### Configuration

- Config file at `~/.config/edit/config` (XDG-compliant)
- Configurable tab width, theme, line numbers, column ruler
- CLI flags (`--tabstop=N`, `--ruler=N`)
- Theme persisted automatically when changed

## Installation

Requires a C23-capable compiler (GCC 14+ or Clang 18+) and a POSIX system (Linux, macOS, *BSD).

```
git clone --recurse-submodules https://github.com/deths74r/edit.git
cd edit
make release
sudo make install
```

This installs `edit` to `/usr/local/bin` and the man page to `/usr/local/share/man/man1`. Override with `PREFIX`:

```
make install PREFIX=$HOME/.local
```

If you already cloned without `--recurse-submodules`:

```
git submodule update --init
```

### Build targets

| Target | Description |
|--------|-------------|
| `make` | Debug build (`-g -O0`) |
| `make release` | Optimized build (`-O2`) |
| `make install` | Build release and install binary + man page |
| `make clean` | Remove build artifacts |
| `make lint` | Check for stray control characters in source |
| `make test` | Build and run unit tests |

## Quick Start

```
edit myfile.c
```

Five things to know:

1. **Type** to insert text. Arrow keys to move. Mouse works too.
2. **Ctrl+S** to save, **Ctrl+Q** to quit.
3. **Ctrl+F** to search, **Ctrl+H** to find and replace.
4. **Ctrl+Z** to undo, **Ctrl+Y** to redo.
5. **ESC** to open the command prompt -- type any command or search term.

## Command Prompt

Press **ESC** to open the command prompt (if a selection is active, the first ESC clears it; press ESC again to open the prompt). **Ctrl+Space** always opens the prompt regardless of selection state.

At the prompt, type a command name and press Enter. Commands do not use a leading slash -- just type `save`, not `/save`. Tab completes and cycles through matching commands. If your input does not match any command, it is treated as a search query.

Commands can take arguments: `goto 42`, `theme Tokyo Night`, `set tabstop 4`. Many commands accept a repeat prefix: `3 dup line` duplicates the current line 3 times.

### File

| Command | Description |
|---------|-------------|
| `save` | Save the current file |
| `save [file]` | Save to a specific file |
| `save as <file>` | Save with a new filename (always prompts) |
| `quit` | Quit (prompts if unsaved changes) |
| `quit!` | Quit immediately without saving |

### Navigation

| Command | Description |
|---------|-------------|
| `goto <N>` | Jump to line N |
| `goto top` | Jump to first line |
| `goto bottom` | Jump to last line |
| `goto match` | Jump to matching bracket |

### Selection

| Command | Description |
|---------|-------------|
| `select` | Select current line |
| `select all` | Select entire file |
| `select line` | Select current or given line(s) |
| `select word` | Select word under cursor |
| `select block` | Select to matching bracket |

### Clipboard

| Command | Description |
|---------|-------------|
| `copy` | Copy selection or range |
| `copy line` | Copy line(s) |
| `copy word` | Copy word under cursor |
| `copy all` | Copy entire file |
| `copy path` | Copy filename to clipboard |
| `cut` | Cut selection or range |
| `cut line` | Cut line(s) |
| `cut word` | Cut word under cursor |
| `cut trailing` | Remove trailing whitespace |
| `paste` | Paste at cursor |
| `paste above` | Paste above cursor line |
| `paste below` | Paste below cursor line |
| `dup` | Duplicate selection or line |
| `dup line` | Duplicate line N times |

### Text Transforms

| Command | Description |
|---------|-------------|
| `upper` | Convert to uppercase |
| `lower` | Convert to lowercase |
| `title` | Convert to title case |
| `sort` | Sort lines ascending |
| `sort reverse` | Sort lines descending |
| `trim` | Strip trailing whitespace |
| `reverse` | Reverse line order |
| `uniq` | Remove duplicate lines |
| `collapse` | Collapse blank lines |
| `indent` | Indent lines |
| `outdent` | Outdent lines |
| `comment` | Toggle line comments |
| `number` | Number lines |

### Search

| Command | Description |
|---------|-------------|
| `find` | Open incremental search |
| `find [text]` | Search with pre-filled query |
| `replace` | Open find and replace |

### Display

| Command | Description |
|---------|-------------|
| `theme` | Cycle to next theme |
| `theme <name>` | Set theme by name |
| `wrap` | Toggle word wrap |
| `numbers` | Toggle line numbers |
| `help` | Show help screen |

### Settings

| Command | Description |
|---------|-------------|
| `set tabstop <N>` | Set tab display width (1-32) |
| `set ruler <N>` | Set column ruler position (0 to disable) |
| `set wrap` | Toggle word wrap |
| `set numbers` | Toggle line numbers |

### Session

| Command | Description |
|---------|-------------|
| `suspend` | Suspend to shell (`fg` to resume) |
| `record start` | Start recording a macro |
| `record stop` | Stop recording |
| `record play` | Play back the recorded macro |
| `mark` | Set a bookmark at cursor |
| `jump` | Jump to a bookmark |
| `marks` | List all bookmarks |
| `mark clear` | Clear all bookmarks |

### Info

| Command | Description |
|---------|-------------|
| `stats` | Show file statistics |
| `count <text>` | Count occurrences of text |
| `pos` | Show cursor position info |
| `undo` | Undo the last change |
| `redo` | Redo the last undone change |

## Keyboard Shortcuts

### File

| Key | Action |
|-----|--------|
| Ctrl+S | Save |
| Ctrl+Q | Quit (prompts if unsaved) |

### Navigation

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor (grapheme-aware) |
| Home / End | Start / end of line |
| Ctrl+A / Ctrl+E | Start / end of line |
| Ctrl+Left / Ctrl+Right | Jump by word |
| Page Up / Page Down | Scroll by one screenful |
| Ctrl+G | Go to line number |
| Ctrl+] | Jump to matching bracket |
| Mouse click | Position cursor |
| Scroll wheel | Scroll with acceleration |

### Selection

| Key | Action |
|-----|--------|
| Shift+Arrow | Select by character / line |
| Shift+Ctrl+Left / Right | Select by word |
| Shift+Home / Shift+End | Select to start / end of line |
| Alt+A | Select to line start |
| Alt+E | Select to line end |
| Mouse drag | Select with mouse |
| ESC | Clear selection |

### Clipboard

| Key | Action |
|-----|--------|
| Ctrl+C | Copy (whole line if no selection) |
| Ctrl+X | Cut (whole line if no selection) |
| Ctrl+V | Paste |
| Ctrl+D | Duplicate line |

### Editing

| Key | Action |
|-----|--------|
| Enter | Insert newline with auto-indent |
| Backspace | Delete character before cursor |
| Delete | Delete character at cursor |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+/ | Toggle line comment (filetype-aware) |
| Tab | Insert tab, or indent selection |
| Shift+Tab | Dedent selection |

### Search

| Key | Action |
|-----|--------|
| Ctrl+F | Incremental search |
| Ctrl+H | Find and replace |

During search:

| Key | Action |
|-----|--------|
| Left / Right | Navigate between matches |
| Up / Down | Browse search history |
| Alt+C | Toggle case sensitivity |
| Alt+X | Toggle regex mode (POSIX ERE) |
| Enter | Accept current match |
| ESC | Cancel and restore cursor position |

### Command Prompt

| Key | Action |
|-----|--------|
| ESC | Open command prompt (clears selection first if active) |
| Ctrl+Space | Open command prompt (always) |
| F1 / F11 | Help screen |

### Legacy Shortcuts

These Alt shortcuts still work but will be removed in a future release in favor of commands:

| Key | Action | Command equivalent |
|-----|--------|--------------------|
| Alt+T | Cycle theme | `theme` |
| Alt+N | Toggle line numbers | `numbers` |
| Alt+W | Toggle word wrap | `wrap` |
| Alt+S | Save | `save` |
| Alt+Q | Quit | `quit` |
| Alt+Shift+S | Save as | `save as` |
| Alt+F | Find | `find` |
| Alt+G | Go to line | `goto` |
| Alt+Shift+K | Cut line | `cut line` |

## Configuration

`edit` reads `$XDG_CONFIG_HOME/edit/config` (defaults to `~/.config/edit/config`). Lines starting with `#` are comments. Format is `key = value`.

### Example config

```
# Tab width (1-32, default: 8)
tabstop = 4

# Color theme
theme = Tokyo Night

# Show line numbers (true/false, default: true)
line_numbers = true

# Column ruler position (0 to disable, default: 0)
ruler = 80
```

### Configuration keys

| Key | Values | Default | Description |
|-----|--------|---------|-------------|
| `tabstop` | 1--32 | 8 | Tab display width in columns |
| `theme` | Theme name | Cyberpunk | Color theme (see Themes below) |
| `line_numbers` | true / false | true | Show line number gutter |
| `ruler` | 0--999 | 0 (disabled) | Column ruler position |

### CLI flags

CLI flags override the config file.

| Flag | Description |
|------|-------------|
| `--tabstop=N` | Set tab display width |
| `--ruler=N` | Set column ruler position (0 to disable) |

## Themes

All 7 themes use 24-bit RGB color. Cycle at runtime with the `theme` command or Alt+T. Set a specific theme with `theme <name>`. The selected theme is saved to the config file automatically.

| Theme | Description |
|-------|-------------|
| **Cyberpunk** (default) | Dark background with neon magenta, cyan, and green accents |
| **Nightwatch** | Monochrome dark palette using shades of gray and white |
| **Daywatch** | Monochrome light palette -- dark text on near-white background |
| **Tokyo Night** | Deep indigo base with soft purple, blue, and green tones |
| **Akira** | Neo-Tokyo aesthetic with red and cyan on a warm dark base |
| **Tokyo Cyberpunk** | Tokyo Night's indigo base with neon magenta and cyan accents |
| **Clarity** | Colorblind-accessible blue/orange/yellow palette |

## Architecture

The full design is documented in [CODING_STANDARDS.md](CODING_STANDARDS.md). Here is a brief overview.

### Cell model

Every character is a `struct cell` carrying its Unicode codepoint and inline syntax metadata. This avoids a separate highlight buffer and means syntax state travels with the character through insertions, deletions, and undo operations.

### Line temperature

Lines have three states, transitioning one-way: COLD -> WARM -> HOT.

- **COLD** -- Only the mmap byte offset and length are stored. No cells allocated.
- **WARM** -- Cells decoded from mmap bytes on demand (when scrolled into view).
- **HOT** -- Line has been edited. Cells are the source of truth; mmap data is stale.

Files open instantly regardless of size. Lines that scroll out of the viewport are gradually cooled back to COLD to reclaim memory.

### Undo system

Six operation types (insert char, delete char, insert line, delete line, split line, join line) recorded in a bounded stack of 256 groups. Edits within 500ms are coalesced into a single undo group. Structural operations and cursor line changes force new group boundaries.

### Rendering pipeline

Each frame is assembled in a heap-allocated append buffer and flushed with a single `write()` call. The cursor is hidden during drawing to avoid flicker. Partial redraws skip the full paint when only the cursor has moved.

## Syntax Highlighting

Highlighting activates automatically based on file extension. All languages get comments, strings, and keyword highlighting. Numbers are highlighted for all languages except Markdown. Each language has two keyword tiers: control-flow keywords and type keywords, displayed in distinct colors.

| Language | Comments | Strings | Keywords | Numbers | Extra |
|----------|----------|---------|----------|---------|-------|
| C/C++ | `//`, `/* */` | `"`, `'` | Yes (control + types) | Yes | |
| Python | `#`, `""" """` | `"`, `'` | Yes (control + types) | Yes | |
| JavaScript | `//`, `/* */` | `"`, `'`, `` ` `` | Yes (control + types) | Yes | Template literals |
| Go | `//`, `/* */` | `"`, `'`, `` ` `` | Yes (control + types) | Yes | Raw strings |
| Rust | `//`, `/* */` | `"`, `'` | Yes (control + types) | Yes | |
| Bash | `#` | `"`, `'` | Yes (control + builtins) | Yes | |
| JSON | -- | `"`, `'` | `true`, `false`, `null` | Yes | |
| YAML | `#` | `"`, `'` | Boolean/null literals | Yes | |
| Markdown | `#` (headers) | `` ` `` | -- | -- | |

## Unicode Support

`edit` uses [gstr](https://github.com/deths74r/gstr) for grapheme cluster boundary detection following Unicode UAX #29. Cursor movement, insertion, deletion, and rendering all operate on grapheme cluster boundaries.

What works:

- **Emoji** -- single emoji, skin-tone modifiers, ZWJ sequences (family, flags, etc.)
- **Regional indicators** -- flag emoji display and navigate correctly
- **CJK characters** -- wide characters take two columns, cursor accounts for display width
- **Combining marks** -- diacritics attach to their base character
- **Bidirectional text** -- stored and rendered correctly at the codepoint level (no bidi reordering)

Encoding is UTF-8 only. Non-UTF-8 bytes produce Unicode replacement characters.

## Limitations

- **No LSP integration** -- no code completion, diagnostics, or go-to-definition.
- **No plugin system** -- behavior is compiled in. Customization is limited to the config file and commands.
- **No multi-buffer / tabs / splits** -- single file, single view. Use terminal tabs or tmux panes for multiple files.
- **No soft wrapping at word boundaries** -- word wrap breaks at the screen edge, not between words.
- **UTF-8 only** -- files in other encodings are not transcoded.
- **No bidi reordering** -- RTL text is stored but not visually reordered.

## License

MIT -- see [LICENSE](LICENSE).
