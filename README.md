# edit

A fast terminal text editor. Single binary. Zero dependencies.

```bash
git clone https://github.com/edwardedmonds/edit.git && cd edit && make && ./edit
```

---

## Why?

Terminal editors sit on a spectrum. Nano is friendly but limited. Vim is powerful but requires years of practice. Edit occupies a different position: familiar keybindings (Ctrl+S saves, Ctrl+Z undoes) with sophisticated internals that handle large files, Unicode edge cases, and structural navigation.

This isn't a vim replacement—if you've invested years in vim, keep using it. This is for everyone else: developers who SSH into servers and need something better than nano, programmers who don't want to memorize command vocabularies, anyone who just wants to edit a file without launching Electron.

## Features

- **Instant file loading** via memory-mapped I/O—100MB files open as fast as 1KB files
- **Full Unicode 17.0 support** with proper grapheme cluster navigation (emoji, combining characters)
- **Syntax highlighting** for C and Markdown
- **Multiple cursors** via Ctrl+D (select next occurrence)
- **Bracket matching** with jump-to-match (O(1) lookup, not scanning)
- **Background search** keeps UI responsive on large files
- **Soft line wrapping** with word-boundary awareness
- **90+ themes** with WCAG 2.1 AA contrast compliance
- **Crash recovery** via automatic swap files
- **Pipe support** for use in shell pipelines

## Installation

```bash
make              # Build
make install      # Install to ~/.local/bin
make uninstall    # Remove
```

Requires: C17 compiler (GCC or Clang), POSIX system. No external dependencies.

## Keybindings

### Files
| Key | Action |
|-----|--------|
| Ctrl+O | Open |
| Ctrl+S | Save |
| Alt+Shift+S | Save As |
| Ctrl+N | New |
| Ctrl+Q | Quit |
| F1 | Help |

### Navigation
| Key | Action |
|-----|--------|
| Arrows | Move cursor |
| Ctrl+←/→ | Move by word |
| Home/End | Line start/end |
| Ctrl+Home/End | File start/end |
| PgUp/PgDn | Page up/down |
| Ctrl+G | Go to line |
| Alt+] | Jump to matching bracket |

### Selection
| Key | Action |
|-----|--------|
| Shift+Arrows | Extend selection |
| Ctrl+Shift+←/→ | Select by word |
| Ctrl+A | Select all |
| Ctrl+D | Select word / next occurrence |

### Editing
| Key | Action |
|-----|--------|
| Ctrl+C/X/V | Copy/Cut/Paste |
| Ctrl+Z/Y | Undo/Redo |
| Alt+K | Delete line |
| Alt+D | Duplicate line |
| Alt+↑/↓ | Move line up/down |
| Alt+/ | Toggle comment |
| Tab/Shift+Tab | Indent/Outdent |

### Search
| Key | Action |
|-----|--------|
| Ctrl+F | Find |
| Ctrl+H | Find & Replace |
| F3 / Shift+F3 | Next/Previous match |
| Alt+A | Replace all |
| Alt+C | Toggle case sensitivity |
| Alt+W | Toggle whole word |
| Alt+R | Toggle regex |

### View
| Key | Action |
|-----|--------|
| Ctrl+T | Theme picker |
| Alt+L | Toggle line numbers |
| Alt+Z | Cycle wrap mode |
| Alt+Shift+W | Toggle whitespace |
| Alt+Shift+C | Cycle color column |

## Themes

Edit includes 90+ themes. Press **Ctrl+T** to browse with live preview.

### Custom Themes

Create `.ini` files in `~/.edit/themes/`:

```ini
name=My Theme
background=#1a1a1a
foreground=#d4d4d4
cursor_line=#262626
selection=#264f78
syntax_keyword=#569cd6
syntax_string=#ce9178
syntax_comment=#6a9955
```

Colors use `#RRGGBB` format. Attributes (`bold`, `italic`, `dim`, `underline`) can be added with `_attr` suffix:

```ini
syntax_keyword_attr=bold
syntax_comment_attr=italic+dim
```

See the built-in themes in `themes/` for complete examples.

## Pipe Mode

Edit works in pipelines:

```bash
# Edit piped input
cat data.txt | ./edit | grep pattern

# Use as $EDITOR
export EDITOR="edit"
git commit  # Opens edit for commit message
```

When stdin or stdout is a pipe, edit uses `/dev/tty` for the terminal interface while preserving pipe data flow.

## Architecture

Edit uses a **three-temperature memory system**:

- **Cold lines**: Backed by mmap, zero heap allocation
- **Warm lines**: Decoded to cells, metadata computed, mmap still valid  
- **Hot lines**: Edited in memory, mmap stale

This means opening a 100MB file is instant—only visible lines consume memory.

Each character is a 12-byte **cell** containing:
- Unicode codepoint
- Syntax token type
- Word boundary classification (for Ctrl+Arrow navigation)
- Bracket pair ID (for O(1) matching)

The precomputed metadata makes operations like word-jump and bracket-match instantaneous rather than requiring scans.

**Background worker thread** handles:
- Line warming ahead of viewport
- Search on large files (>5000 lines)
- Autosave every 30 seconds

## Configuration

Edit stores preferences in `~/.editrc`:

```
theme=Solarized Dark
```

## Comparisons

| | edit | vim | nano | VS Code |
|--|------|-----|------|---------|
| Learning curve | Minutes | Years | Minutes | Hours |
| Memory | ~10MB | ~50MB | ~5MB | ~500MB |
| Startup | <10ms | ~50ms | <10ms | 2-5s |
| Large files | Instant | Good | Slow | Good |
| Unicode | Full | Full | Basic | Full |
| Plugins | No | Yes | No | Yes |

**Use edit when:** You need something better than nano without the vim learning curve. Quick edits over SSH. Resource-constrained systems.

**Use vim when:** You've already learned it. You need macros, plugins, or infinite customization.

**Use VS Code when:** You need an IDE with LSP, debugging, and extensions.

## Source Structure

```
src/
├── main.c          # Entry point, signal handling
├── edit.c          # Core editor logic
├── buffer.c        # Line and buffer management
├── syntax.c        # Highlighting (C, Markdown)
├── theme.c         # Theme loading and rendering
├── render.c        # Screen drawing
├── input.c         # Keyboard and mouse handling
├── search.c        # Find and replace
├── undo.c          # Operation-based history
├── worker.c        # Background thread
├── dialog.c        # File browser, theme picker
├── clipboard.c     # System clipboard (xclip/wl-copy)
├── autosave.c      # Swap file management
├── terminal.c      # Raw mode, window size
├── keybindings.c   # Key mapping
├── editor.c        # Editor state management
├── update.c        # Update checking
└── error.c         # Error handling

lib/
└── utflite/        # Embedded Unicode library
```

## License

MIT. See LICENSE file.

---

*Edit is what happens when someone gets tired of explaining vim to new developers and tired of launching VS Code to change one line.*
