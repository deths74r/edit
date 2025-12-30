# edit

A minimal terminal text editor written in C.

## Features

- **Full Unicode support** - UTF-8 encoding, grapheme cluster navigation, proper display widths for CJK and emoji
- **C syntax highlighting** - Keywords, types, strings, comments, preprocessor directives
- **Soft line wrapping** - Word and character wrap modes with customizable indicators
- **Find & Replace** - Incremental search with regex support and backreferences
- **Multiple cursors** - Ctrl+D to select word and add cursors at occurrences
- **Bracket matching** - Highlights matching delimiters, Ctrl+] to jump between pairs
- **Undo/Redo** - Full history with automatic grouping of rapid edits
- **Mouse support** - Click to position, drag to select, scroll wheel navigation
- **Visual aids** - Color column, trailing whitespace highlight, whitespace visualization

## Installation

```bash
make
make install    # Installs to ~/.local/bin
```

## Usage

```bash
edit <filename>
edit                # Opens empty buffer
```

## Key Bindings

### File Operations
| Key | Action |
|-----|--------|
| Ctrl+S | Save |
| F12 / Alt+Shift+S | Save As |
| Ctrl+Q | Quit |

### Navigation
| Key | Action |
|-----|--------|
| Ctrl+G | Go to line |
| Ctrl+F | Find |
| Ctrl+R | Find & Replace |
| Ctrl+] | Jump to matching bracket |
| Ctrl+Arrow | Word navigation |

### Editing
| Key | Action |
|-----|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+D | Select word / next occurrence |
| Ctrl+A | Select all |
| Alt+K | Delete line |
| Alt+D | Duplicate line |
| Alt+Up/Down | Move line up/down |
| Tab (with selection) | Indent |
| Shift+Tab | Outdent |
| Ctrl+/ or Alt+/ | Toggle comment |

### Search Mode
| Key | Action |
|-----|--------|
| Alt+C | Toggle case sensitivity |
| Alt+W | Toggle whole word |
| Alt+R | Toggle regex |
| Alt+N / Down | Next match |
| Alt+P / Up | Previous match |
| Tab | Switch to replace field |
| Enter | Replace current |
| Alt+A | Replace all |

### Display
| Key | Action |
|-----|--------|
| Alt+Z | Cycle wrap mode (off/word/char) |
| Alt+Shift+Z | Cycle wrap indicator style |
| F3 | Toggle whitespace visibility |
| F4 | Cycle color column (off/80/120) |
| Shift+F4 | Cycle color column style |

## Building from Source

Requires a C17 compiler and make.

```bash
git clone <repo>
cd edit
make
```

## Documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Comprehensive technical manual covering internals, data structures, subsystems, and extension guide
- **[CODING_STANDARDS.md](CODING_STANDARDS.md)** - Style guide and naming conventions
- **[TRANSACTION_LOG.md](TRANSACTION_LOG.md)** - Development history and phase tracking

## License

MIT
