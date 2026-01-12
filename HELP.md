
# edit - Terminal Text Editor

A minimal, fast terminal text editor with syntax highlighting, multiple buffers, and modern editing features.

Press **F1** to close this help and return to your file.

---

## File Operations

| Key         | Action           |
|-------------|------------------|
| Ctrl+N      | New file         |
| Ctrl+S      | Save             |
| Alt+Shift+S | Save As          |
| Ctrl+O      | Open file        |
| Ctrl+Q      | Close tab / Quit |
| Ctrl+T      | Theme picker     |
| F1          | Toggle help      |

## Tabs (Multiple Buffers)

| Key       | Action               |
|-----------|----------------------|
| Alt+N     | New tab              |
| Alt+O     | Open file in new tab |
| Alt+Left  | Previous tab         |
| Alt+Right | Next tab             |
| Ctrl+Q    | Close current tab    |

*Click tabs with mouse to switch. Ctrl+Q quits when only one tab remains.*

## Navigation

| Key             | Action                   |
|-----------------|--------------------------|
| Arrow keys      | Move cursor              |
| Ctrl+Left/Right | Move by word             |
| Home / End      | Line start / end         |
| Page Up/Down    | Page navigation          |
| Ctrl+Home/End   | File start / end         |
| Ctrl+G          | Go to line               |
| Alt+] or Ctrl+] | Jump to matching bracket |

## Selection

| Key                   | Action                        |
|-----------------------|-------------------------------|
| Shift+Arrows          | Extend selection              |
| Shift+Home/End        | Select to line start / end    |
| Shift+Page Up/Down    | Select by page                |
| Ctrl+Shift+Left/Right | Select by word                |
| Ctrl+A                | Select all                    |
| Alt+W                 | Select word under cursor      |
| Ctrl+D                | Add cursor at next occurrence |

## Editing

| Key                | Action              |
|--------------------|---------------------|
| Ctrl+C / X / V     | Copy / Cut / Paste  |
| Ctrl+Z / Y         | Undo / Redo         |
| Backspace / Delete | Delete character    |
| Alt+K              | Delete line         |
| Alt+D              | Duplicate line      |
| Alt+Up/Down        | Move line up / down |
| Alt+/ or Ctrl+/    | Toggle comment      |

## Search & Replace

| Key              | Action                        |
|------------------|-------------------------------|
| Ctrl+F           | Find                          |
| Ctrl+H or Ctrl+R | Find & Replace                |
| F3               | Find next                     |
| Shift+F3         | Find previous                 |
| Alt+A            | Replace all (in replace mode) |
| Alt+C            | Toggle case sensitivity       |
| Alt+W            | Toggle whole word             |
| Alt+R            | Toggle regex                  |

## View

| Key         | Action                          |
|-------------|---------------------------------|
| Alt+L       | Toggle line numbers             |
| Alt+Shift+W | Toggle whitespace               |
| Alt+Shift+C | Cycle color column (off/80/120) |
| Alt+Z       | Cycle wrap mode (off/word/char) |
| Alt+Shift+Z | Cycle wrap indicator            |
| Alt+M       | Toggle hybrid markdown mode     |
| Alt+U       | Check for updates               |

## Markdown

Edit provides full syntax highlighting for Markdown files (.md):
- Headers (H1-H6) with level-specific styling
- **Bold**, *italic*, and ***bold+italic*** text
- `Inline code` and fenced code blocks
- Links, images, and blockquotes
- Tables with alignment support
- Task lists with checkboxes

| Key                  | Action                 |
|----------------------|------------------------|
| Alt+T                | Auto-format all tables |
| Tab (in table)       | Move to next cell      |
| Shift+Tab (in table) | Move to previous cell  |
| Space (on checkbox)  | Toggle task checkbox   |

## Tips

- **Multiple buffers**: Open files in tabs with Alt+O, switch with Alt+Left/Right
- **Multi-cursor**: Press Ctrl+D repeatedly to select multiple occurrences
- **Bracket matching**: Use Alt+] to jump between matching brackets `(){}[]`
- **Color column**: Shows a visual guide at column 80 or 120 (cycle with Alt+Shift+C)
- **Wrap modes**: No wrap, word-boundary wrap, or character wrap (Alt+Z)
- **Custom keybindings**: Edit `~/.edit/keybindings.ini` to customize shortcuts
- **Themes**: 90+ themes available via Ctrl+T, or add custom themes to `~/.edit/themes/`

---

For more information, visit the project repository.

