
# edit - Terminal Text Editor

A minimal, fast terminal text editor with syntax highlighting and modern editing features.

Press **F1** to close this help and return to your file.

---

## File Operations

| Key         | Action      |
|-------------|-------------|
| Alt+S       | Save        |
| Alt+Shift+S | Save As     |
| Alt+Q       | Quit        |
| Alt+T       | Cycle theme |
| F1          | Toggle help |

## Navigation

| Key             | Action                   |
|-----------------|--------------------------|
| Arrow keys      | Move cursor              |
| Ctrl+Left/Right | Move by word             |
| Home / End      | Line start / end         |
| Page Up/Down    | Page navigation          |
| Ctrl+Home/End   | File start / end         |
| Alt+G           | Go to line               |
| Alt+]           | Jump to matching bracket |

## Selection

| Key                   | Action                     |
|-----------------------|----------------------------|
| Shift+Arrows          | Extend selection           |
| Shift+Home/End        | Select to line start / end |
| Shift+Page Up/Down    | Select by page             |
| Ctrl+Shift+Left/Right | Select by word             |
| Alt+A                 | Select all                 |
| Alt+W                 | Select word                |

## Editing

| Key                | Action              |
|--------------------|---------------------|
| Alt+C / X / V      | Copy / Cut / Paste  |
| Alt+Z              | Undo                |
| Alt+Shift+Z        | Redo                |
| Backspace / Delete | Delete character    |
| Alt+K              | Delete line         |
| Alt+D              | Duplicate line      |
| Alt+Up/Down        | Move line up / down |
| Alt+/              | Toggle comment      |

## Search

| Key   | Action        |
|-------|---------------|
| Alt+F | Find          |
| Alt+N | Find next     |
| Alt+P | Find previous |

## View

| Key         | Action                      |
|-------------|-----------------------------|
| Alt+L       | Toggle line numbers         |
| Alt+Shift+W | Toggle whitespace           |
| Alt+Shift+C | Cycle color column          |
| Alt+M       | Toggle hybrid markdown mode |
| Alt+U       | Check for updates           |

## Markdown

| Key                  | Action               |
|----------------------|----------------------|
| Tab (in table)       | Next cell            |
| Shift+Tab (in table) | Previous cell        |
| Space (on checkbox)  | Toggle task checkbox |

## Tips

- **Multi-cursor**: Select a word and use Ctrl+D to select the next occurrence
- **Bracket matching**: Use Alt+] to jump between matching brackets
- **Color column**: Shows a visual guide at column 80 (cycle styles with Alt+Shift+C)
- **Custom keybindings**: Edit `~/.edit/keybindings.ini` to customize shortcuts

---

For more information, visit the project repository.

