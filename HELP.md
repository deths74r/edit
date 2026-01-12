# edit - Terminal Text Editor

A minimal, fast terminal text editor with syntax highlighting and modern editing features.

Press **F1** to close this help and return to your file.

---

## File Operations

| Key | Action |
|-----|--------|
| Ctrl+N | New file |
| Ctrl+S | Save |
| Alt+Shift+S | Save As |
| Ctrl+O | Open file |
| Ctrl+Q | Quit |
| Ctrl+T | Theme picker |
| F1 | Toggle help |

## Navigation

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor |
| Ctrl+Left/Right | Move by word |
| Home / End | Line start / end |
| Page Up/Down | Page navigation |
| Ctrl+Home/End | File start / end |
| Ctrl+G | Go to line |
| Alt+] | Jump to matching bracket |

## Selection

| Key | Action |
|-----|--------|
| Shift+Arrows | Extend selection |
| Shift+Home/End | Select to line start / end |
| Shift+Page Up/Down | Select by page |
| Ctrl+Shift+Left/Right | Select by word |
| Ctrl+A | Select all |
| Ctrl+D | Add cursor at next occurrence |

## Editing

| Key | Action |
|-----|--------|
| Ctrl+C / X / V | Copy / Cut / Paste |
| Ctrl+Z / Y | Undo / Redo |
| Backspace / Delete | Delete character |
| Alt+K | Delete line |
| Alt+D | Duplicate line |
| Alt+Up/Down | Move line up / down |
| Alt+/ | Toggle comment |

## Search

| Key | Action |
|-----|--------|
| Ctrl+F | Find |
| Ctrl+H | Find & Replace |
| F3 / Alt+N | Find next |
| Shift+F3 / Alt+P | Find previous |
| Alt+A | Replace all |
| Alt+C | Toggle case sensitivity |
| Alt+W | Toggle whole word |
| Alt+R | Toggle regex |

## View

| Key | Action |
|-----|--------|
| Alt+L | Toggle line numbers |
| Alt+Shift+W | Toggle whitespace |
| Alt+Shift+C | Cycle color column |
| Alt+Z | Cycle wrap mode |
| Alt+Shift+Z | Cycle wrap indicator |
| Alt+M | Toggle hybrid markdown mode |
| Alt+U | Check for updates |

## Markdown

| Key | Action |
|-----|--------|
| Alt+T | Format all tables |
| Tab (in table) | Next cell |
| Shift+Tab (in table) | Previous cell |
| Space (on checkbox) | Toggle task checkbox |

## Tips

- **Multi-cursor**: Press Ctrl+D to select the next occurrence of the current word
- **Bracket matching**: Use Alt+] to jump between matching brackets
- **Color column**: Shows a visual guide at column 80 (cycle styles with Alt+Shift+C)
- **Wrap modes**: No wrap, soft wrap, or wrap at word boundaries (Alt+Z)
- **Custom keybindings**: Edit `~/.edit/keybindings.ini` to customize shortcuts

---

For more information, visit the project repository.
