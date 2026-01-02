# Edit

*A terminal text editor for people who know what they want to type*

---

Terminal text editors exist on a spectrum. At one end sits nano: friendly, obvious, and about as capable as a butter knife. At the other end sits vim: powerful, arcane, and requiring a years-long apprenticeship before you stop accidentally deleting your files. Between them lies a wasteland of editors that either aspire to vim's complexity without its coherence, or add features to nano until it collapses under its own weight.

Edit occupies a different position. It assumes you want to edit text, not learn a new religion. The keybindings are what you'd expect from any modern application—Ctrl+S saves, Ctrl+C copies, Ctrl+Z undoes—yet underneath runs machinery sophisticated enough to handle large files, Unicode edge cases, and the kind of structural navigation that usually requires a modal editor.

This is not a vim replacement. If you've invested years in vim, you've probably recouped that investment by now. This is for everyone else: the developer who SSH'd into a server and discovered nano can't handle their file, the new programmer who doesn't want to memorize a command vocabulary, the experienced developer who just wants to make a quick edit without launching an Electron application that consumes half a gigabyte of RAM.

## Quick Start

```bash
git clone https://github.com/edwardedmonds/edit.git
cd edit
make
./edit yourfile.txt
```

That's it. No configuration required. No plugins to install. No tutorial to complete before you can type characters into a file.

To install system-wide:

```bash
make install      # installs to ~/.local/bin
make uninstall    # removes it
make clean        # removes build artifacts
```

### Essential Keybindings

| Action | Key |
|--------|-----|
| Save | Ctrl+S |
| Open file | Ctrl+O |
| Quit | Ctrl+Q |
| Undo / Redo | Ctrl+Z / Ctrl+Y |
| Copy / Cut / Paste | Ctrl+C / Ctrl+X / Ctrl+V |
| Find | Ctrl+F |
| Find & Replace | Ctrl+R |
| Go to line | Ctrl+G |
| Select all | Ctrl+A |

If you've used any text editor in the last thirty years, you already know how to use edit.

## Features

### File Operations

**Ctrl+S** saves the current file. **Ctrl+O** opens a file browser dialog—arrow keys to navigate, Enter to select, Escape to cancel. **Ctrl+Q** quits; if you have unsaved changes, you'll be asked to confirm.

Edit can read from stdin:
```bash
cat somefile.txt | ./edit
```

If edit crashes (or your SSH connection drops, or your laptop dies), it maintains a swap file. On restart, you'll be offered the chance to recover your work.

If another process modifies the file while you're editing, edit detects the change and prompts you: `File changed on disk. [R]eload [K]eep:`. Reload discards your changes and loads the new version; Keep ignores the external change and continues with your version. This check runs every two seconds.

### Navigation

Arrow keys move the cursor. Unlike simpler editors, edit moves by *grapheme cluster*—the thing humans think of as "one character"—not by byte or codepoint. This means emoji, combining accents, and complex scripts work correctly.

**Ctrl+Arrow** jumps by word. The definition of "word" is context-aware: `hello_world` contains two words, `foo->bar` contains three, and punctuation sequences like `!=` are treated as single tokens.

**Ctrl+G** prompts for a line number and jumps there.

**Ctrl+]** jumps to the matching bracket. If you're on a `{`, it finds the corresponding `}`. This works across lines and handles nested brackets correctly—not by scanning the file, but by looking up precomputed metadata.

**Home/End** go to line boundaries. In wrap mode, they go to the wrap segment boundaries; press again to reach the actual line start/end.

**Page Up/Down** scroll by screenful, keeping the cursor visible.

### Selection

**Shift+Arrow** extends the selection character by character. **Ctrl+Shift+Arrow** extends by word.

**Ctrl+D** selects the word under the cursor. Press it again to select the next occurrence of that word—useful for renaming variables without opening find-and-replace.

**Ctrl+A** selects all text.

**Mouse** support includes click to position cursor, drag to select, double-click to select word, and triple-click to select line. The scroll wheel uses adaptive velocity: slow scrolling moves one line at a time, fast scrolling accelerates up to twenty lines per tick.

### Editing

**Ctrl+Z** undoes, **Ctrl+Y** redoes. Undo groups rapid keystrokes together—typing "hello" and pressing undo removes the whole word, not individual letters.

**Ctrl+C/X/V** use the system clipboard when available (via xclip, xsel, or wl-copy), falling back to an internal buffer when not.

**Tab** indents; with text selected, it indents all selected lines. **Shift+Tab** outdents.

**Ctrl+/** toggles comment on the current line (or all selected lines). Currently uses `//` style comments.

**Alt+K** deletes the entire current line. **Alt+D** duplicates it. **Alt+Up/Down** moves the line up or down.

**Enter** inserts a newline and preserves indentation from the previous line.

### Search & Replace

**Ctrl+F** enters search mode. Type your query; matches highlight in real-time as you type. **Alt+N** jumps to the next match, **Alt+P** to the previous.

**Alt+C** toggles case sensitivity. **Alt+W** toggles whole-word matching. **Alt+R** toggles regex mode (POSIX extended regular expressions).

**Ctrl+R** enters find-and-replace mode. **Tab** switches between the search and replace fields. **Enter** replaces the current match and jumps to the next. **Alt+A** replaces all matches.

For files larger than 5,000 lines, search runs in a background thread so the UI stays responsive.

**Escape** exits search mode.

### View Settings

**Alt+Z** cycles through wrap modes: no wrapping (horizontal scroll), word-boundary wrapping, and character wrapping.

**Alt+Shift+Z** cycles through wrap indicators—the symbol shown at line continuation points. Options include blank, arrows, corners, and various Unicode glyphs.

**F3** toggles whitespace visibility. Tabs appear as `→`, trailing spaces as `·`.

**F4** cycles the color column: off, 80 characters, 120 characters. A subtle vertical line helps you keep lines within bounds.

**F5** (or **Ctrl+T**) opens the theme picker.

## Themes

Edit ships with 49 built-in themes, ranging from dark themes with syntax colors to minimal monochrome schemes. All themes meet WCAG 2.1 AA contrast requirements—a 4.5:1 minimum ratio between text and background.

Themes include tritanopia-friendly options that use the red-cyan axis visible to those with blue-yellow color blindness.

The theme picker shows a live preview as you navigate. Press Enter to select, Escape to cancel.

### Custom Themes

Place `.ini` files in `~/.edit/themes/`. Edit scans this directory on startup.

#### Format

Theme files use a simple `key=value` format (no INI section headers needed):

```ini
# Comments start with #
name=My Custom Theme

# Colors are hex RGB
background=#1a1a1a
foreground=#d4d4d4

# Most elements support _fg, _bg, and _attr suffixes
syntax_keyword=#569cd6
syntax_keyword_attr=bold

# Combine attributes with +
syntax_comment_attr=italic+dim
```

**Color values**: `#RRGGBB` hex format

**Attributes**: `bold`, `dim`, `italic`, `underline`, `reverse`, `strike`, `curly`, `overline`

#### Theme Keys Reference

**Core UI**
| Key | Description |
|-----|-------------|
| `name` | Theme display name |
| `background` | Main background color |
| `foreground` | Default text color |
| `cursor_line` | Current line highlight |
| `selection` | Selected text background |
| `search_match` | Search match highlight |
| `search_current` | Current search match |
| `color_column` | Column guide background |
| `color_column_line` | Column guide line color |
| `trailing_ws` | Trailing whitespace highlight |

**Line Numbers** — each supports `_fg`, `_bg`, `_attr` suffixes
| Base Key | Description |
|----------|-------------|
| `line_number` | Inactive line numbers |
| `line_number_active` | Current line number |

**Gutter**
| Base Key | Description |
|----------|-------------|
| `gutter` | Gutter background |
| `gutter_active` | Active line gutter |

**Status Bar**
| Base Key | Description |
|----------|-------------|
| `status` | Status bar base |
| `status_filename` | Filename display |
| `status_modified` | Modified indicator |
| `status_position` | Line:column display |

**Message Bar**
| Base Key | Description |
|----------|-------------|
| `message` | Message/prompt area |

**Prompt Components**
| Base Key | Description |
|----------|-------------|
| `prompt_label` | "Search:", "Replace:" labels |
| `prompt_input` | User input field |
| `prompt_bracket` | Decorative brackets |
| `prompt_warning` | Warning messages |

**Search Feedback**
| Base Key | Description |
|----------|-------------|
| `search_options` | [C]ase [W]hole [R]egex indicators |
| `search_nomatch` | "No matches" message |
| `search_error` | Regex error message |

**Whitespace Indicators**
| Base Key | Description |
|----------|-------------|
| `whitespace` | General whitespace |
| `whitespace_tab` | Tab characters (→) |
| `whitespace_space` | Space characters (·) |

**Other UI Elements**
| Base Key | Description |
|----------|-------------|
| `wrap_indicator` | Line continuation marker |
| `empty_line` | Empty line marker (~) |
| `welcome` | Welcome screen text |
| `bracket_match` | Matching bracket highlight |
| `multicursor` | Additional cursors |

**Dialogs** (file browser, theme picker)
| Base Key | Description |
|----------|-------------|
| `dialog` | Dialog background |
| `dialog_header` | Dialog title bar |
| `dialog_footer` | Dialog footer/help |
| `dialog_highlight` | Selected item |
| `dialog_directory` | Directory entries |

**Syntax Highlighting**
| Base Key | Description |
|----------|-------------|
| `syntax_normal` | Default code |
| `syntax_keyword` | `if`, `for`, `return`, etc. |
| `syntax_type` | `int`, `char`, `void`, etc. |
| `syntax_string` | String literals |
| `syntax_number` | Numeric literals |
| `syntax_comment` | Comments |
| `syntax_preprocessor` | `#include`, `#define` |
| `syntax_function` | Function names |
| `syntax_operator` | `+`, `-`, `=`, etc. |
| `syntax_bracket` | `()`, `[]`, `{}` |
| `syntax_escape` | Escape sequences (`\n`) |

#### Minimal Example

A minimal dark theme:

```ini
name=Minimal Dark
background=#0d0d0d
foreground=#c0c0c0
cursor_line=#1a1a1a
selection=#264f78
syntax_keyword=#c586c0
syntax_string=#ce9178
syntax_comment=#6a9955
```

Unspecified keys inherit from the built-in default theme.

### Configuration

Edit stores your theme preference in `~/.editrc`:

```
# edit configuration
theme=Tritanopia Dark
```

Other settings (wrap mode, whitespace visibility, color column) are not persisted and reset to defaults each session.

---

## Architecture

The rest of this document explains how edit works internally. If you just want to edit text, you can stop reading here. If you're curious why edit handles large files efficiently, or how bracket matching can be instantaneous, read on.

### The Three-Temperature System

Most editors read files into memory. Open a 100MB file in nano and you'll wait several seconds while it allocates and copies. Vim is cleverer—it uses swap files and loads incrementally—but there's still measurable latency. VS Code loads everything into a rope data structure optimized for edits, which works well but consumes memory proportional to file size.

Edit uses memory-mapped I/O. When you open a file, edit calls `mmap()` to map it into virtual address space. No copying occurs. The operating system pages in data on demand, which means opening a 100MB file takes the same time as opening a 1KB file: essentially zero.

But memory mapping creates a problem: you can't edit a memory-mapped region directly (modifications would affect the file immediately). Edit solves this with a three-temperature system for lines:

**Cold** lines are backed entirely by mmap. They consume no heap memory. The line structure stores only an offset and length into the mapped file.

**Warm** lines have been decoded from UTF-8 into edit's internal cell format, but haven't been modified. The mmap content remains authoritative. Warming a line allocates memory for cells and computes syntax highlighting and structural metadata.

**Hot** lines have been edited. Their cell arrays are authoritative; the mmap content is stale. These lines must be written to disk on save.

When you scroll through a file, only the visible lines need to be warm. The background worker thread warms lines ahead of the viewport so scrolling stays smooth. When you edit a line, it becomes hot. On save, only hot lines are written.

**Trade-offs:**

| Aspect | Benefit | Cost |
|--------|---------|------|
| Instant load | Files open immediately regardless of size | Architecture complexity |
| Low memory | Memory ∝ viewport + edited lines | Atomic operations for thread safety |
| Deferred work | Syntax highlighting computed lazily | Cannot edit cold lines without warming |

### The Cell Structure

Each character in a warm or hot line is represented by a `struct cell`:

```c
struct cell {
    uint32_t codepoint;     // Unicode codepoint
    uint16_t syntax;        // Syntax token type
    uint8_t neighbor;       // Character class + position
    uint8_t flags;          // Reserved
    uint32_t context;       // Pair ID + type + role
};
```

Visually:

```
┌────────────────────────────────────────────────────────────┐
│                         12 bytes                           │
├──────────────┬─────────┬────────┬───────┬──────────────────┤
│   codepoint  │  syntax │neighbor│ flags │     context      │
│   4 bytes    │ 2 bytes │ 1 byte │1 byte │     4 bytes      │
└──────────────┴─────────┴────────┴───────┴──────────────────┘
```

Here's how the code `if (x)` is represented as cells:

```
 'i'       'f'       ' '       '('       'x'       ')'
  │         │         │         │         │         │
  ▼         ▼         ▼         ▼         ▼         ▼
┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
│0x69 │   │0x66 │   │0x20 │   │0x28 │   │0x78 │   │0x29 │
├─────┤   ├─────┤   ├─────┤   ├─────┤   ├─────┤   ├─────┤
│ KWD │   │ KWD │   │NORM │   │BRKT │   │NORM │   │BRKT │
├─────┤   ├─────┤   ├─────┤   ├─────┤   ├─────┤   ├─────┤
│START│   │ END │   │SOLO │   │SOLO │   │SOLO │   │SOLO │
├─────┤   ├─────┤   ├─────┤   ├─────┤   ├─────┤   ├─────┤
│  —  │   │  —  │   │  —  │   │ #1  │◄──┼──►──┼──►│ #1  │
│     │   │     │   │     │   │open │   │     │   │close│
└─────┘   └─────┘   └─────┘   └─────┘   └─────┘   └─────┘
```

The `(` and `)` share pair ID #1, enabling instant bracket matching. The `if` keyword spans two cells with START/END positions, enabling word-jump navigation.

Twelve bytes per character. This is larger than storing raw UTF-8 (1-4 bytes per codepoint), but the metadata enables features that would otherwise require expensive scans:

- **syntax** stores the highlighting category (keyword, string, comment, etc.) so rendering doesn't recompute it
- **neighbor** encodes character class and token position for word navigation
- **context** stores bracket matching information

A 10,000-line file averaging 80 characters per line requires about 9.6MB in cell form. For most editing sessions, this is acceptable. For truly enormous files, the three-temperature system ensures only visible and edited content incurs this cost.

### The Neighbor Layer

When you press Ctrl+Arrow to jump by word, what happens? Most editors scan backward or forward, classifying characters until they find a boundary. This is O(n) in the distance traveled.

Edit precomputes character classifications when warming a line. Each cell's `neighbor` byte encodes:

- **Character class** (3 bits): whitespace, letter, digit, underscore, punctuation, bracket, quote, or other
- **Token position** (2 bits): solo, start, middle, or end

For the string `hello_world`:

```
h    e    l    l    o    _    w    o    r    l    d
S    M    M    M    E    SOLO S    M    M    M    E
```

Ctrl+Arrow walks the cells and stops when the position field changes from END to START or vice versa. The character class handles transitions between different token types (letters to punctuation, etc.).

This makes word navigation effectively O(1) per character touched—the classification is already done.

### Pair Entanglement

Finding the matching bracket for a `{` traditionally requires scanning: count opening braces, decrement for closing braces, stop when the count reaches zero. This is O(n) in the worst case and must be done on demand.

Edit computes bracket pairing in a single pass when loading or editing a file. Each delimiter receives a unique 24-bit pair ID:

```
{  →  pair_id=1, role=opener
   (  →  pair_id=2, role=opener
   )  →  pair_id=2, role=closer
}  →  pair_id=1, role=closer
```

The `context` field in each cell stores this information. When you press Ctrl+] on a bracket, edit looks up the pair ID and searches for the cell with the same ID and opposite role. In practice, this search can use the pair ID as a key, making the lookup nearly instantaneous.

**Trade-off:** Pair matching must be recomputed after edits. For small files, this is imperceptible. For large files, edit batches recalculation and runs it in the background.

### UTF-8 and Grapheme Clusters

Edit uses the utflite library (embedded in `third_party/`) for Unicode handling. Three capabilities matter:

1. **UTF-8 encoding/decoding**: Converting between byte sequences and codepoints
2. **Grapheme cluster segmentation**: Determining what humans perceive as "one character"
3. **Character width calculation**: Knowing how many terminal columns a character occupies

The distinction between codepoints and graphemes is subtle but important. The letter "é" can be represented as a single codepoint (U+00E9, "Latin Small Letter E with Acute") or as two codepoints (U+0065 "Latin Small Letter E" followed by U+0301 "Combining Acute Accent"). Both render identically. A user pressing the right arrow should move past the entire grapheme, not land in the middle.

More dramatic examples include emoji: 🏴󠁧󠁢󠁳󠁣󠁴󠁿 (the Scottish flag) is 7 codepoints but one grapheme. Edit handles this correctly; many editors don't.

Cursor movement in edit operates on grapheme boundaries. The arrow key finds the next grapheme boundary, not the next byte or codepoint.

### Syntax Highlighting

Edit performs syntax highlighting in a single pass per line. The highlighter recognizes:

- Keywords and type names
- String and character literals
- Numeric constants
- Comments (line and block)
- Preprocessor directives
- Function names (identifiers followed by `(`)
- Operators and brackets

Currently only C/C++ syntax is implemented. The architecture supports additional languages—each is a separate highlighting function keyed by file extension.

**Trade-off vs tree-sitter:** Tree-sitter provides semantic understanding: it knows that `foo` is a function, not just an identifier before a parenthesis. This enables features like "go to definition" and intelligent refactoring. Edit's approach is simpler—about 1,000 lines of code versus tree-sitter's substantial complexity—but limited to syntactic coloring. For a minimal editor, this is sufficient. For an IDE, it's not.

### Undo System

Most editors implement undo in one of two ways:

1. **Snapshot-based**: Store the entire buffer state at each undo point. Simple but memory-expensive: typing 100 characters means storing 100 copies of the buffer.

2. **Operation-based**: Store the operations (insert, delete) that transform one state to another. Compact but requires careful replay logic.

Edit uses operation-based undo. Each edit records what changed, where, and the cursor position before and after. Operations are grouped by timing: keystrokes within one second of each other form a single undo group. Typing "hello" quickly and pressing undo removes all five characters; typing slowly creates five separate undo points.

Memory usage for 100 insertions: roughly 8KB (80 bytes per operation) versus potentially megabytes for snapshot-based approaches.

### Soft Line Wrapping

Edit supports three wrap modes:

- **None**: Long lines scroll horizontally
- **Word**: Lines wrap at word boundaries
- **Character**: Lines wrap at any character

The complexity lies in cursor movement. When a long line wraps to three screen rows, pressing Down should move to the next screen row, not the next file line. Home and End should go to wrap segment boundaries, with a second press reaching the actual line boundaries.

Edit maintains a wrap cache for each line: an array of column positions where wraps occur. This cache is invalidated when the line content changes, the terminal resizes, or the wrap mode changes.

### Background Worker

Some operations shouldn't block the UI:

- **Line warming**: Decoding UTF-8 and computing metadata for lines about to scroll into view
- **Large file search**: Finding matches in files over 5,000 lines
- **Autosave**: Writing to the swap file every 30 seconds

Edit runs a worker thread that processes a task queue. The main thread submits tasks and polls for results. Communication uses mutex-protected queues. The worker never directly modifies editor state visible to the main thread; instead, it produces results that the main thread applies.

---

## Comparisons

### Edit vs Vim

| Aspect | edit | vim |
|--------|------|-----|
| Learning curve | Minutes | Months to years |
| Editing model | Modeless | Modal |
| Startup time | <10ms | ~50ms |
| Plugin ecosystem | None | Thousands |
| Configuration | ~/.editrc (theme only) | Potentially hundreds of lines |
| Customization | Themes only | Infinitely scriptable |

**When to use vim:** You've already learned it, or you need macros, plugins, advanced text objects, or the ability to customize every behavior.

**When to use edit:** You want to edit a file without ceremony. You're on a new system. You're teaching someone who's never used a terminal. You need to make a quick change and move on.

### Edit vs Nano

| Aspect | edit | nano |
|--------|------|------|
| Unicode | Full grapheme support | Basic |
| Large files | Instant load via mmap | Loads entire file |
| Bracket matching | O(1), across lines | Line-local only |
| Multi-cursor | Ctrl+D for next occurrence | No |
| Soft wrapping | Word-aware | Character only |
| Syntax highlighting | Extensible | Built-in for many languages |

**When to use nano:** It's already installed. You're editing a format nano highlights well.

**When to use edit:** Large files, correct Unicode handling, bracket navigation, multi-occurrence selection.

### Edit vs VS Code

| Aspect | edit | VS Code |
|--------|------|---------|
| Memory | ~10MB | ~500MB |
| Startup | <10ms | 2-5 seconds |
| Language support | Syntax highlighting | Full LSP |
| Extensions | None | 50,000+ |
| Git integration | None | Built-in |
| Debugging | None | Integrated |

**When to use VS Code:** You need an IDE. Language server features, debugging, integrated terminals, extension ecosystem—these matter for serious development work.

**When to use edit:** SSH sessions where you can't run VS Code. Quick edits that don't justify launching an Electron app. Resource-constrained systems. When VS Code is overkill.

### Edit vs Micro

Both are modern terminal editors aiming for usability:

| Aspect | edit | micro |
|--------|------|-------|
| Language | C | Go |
| Plugins | No | Yes |
| Configuration | ~/.editrc | JSON files |
| Syntax themes | INI-based | JSON-based |

**Key difference:** Micro prioritizes extensibility with its plugin system. Edit prioritizes simplicity and speed. Neither is wrong—they serve different preferences.

---

## Building

```bash
make              # Build the editor
make test         # Run UTF-8 validation tests
make clean        # Remove build artifacts
make install      # Install to ~/.local/bin
make uninstall    # Remove installed binary
```

**Compiler requirements:** C17 with `-Wall -Wextra -pedantic`. Tested with GCC and Clang.

**Dependencies:** None beyond libc and POSIX APIs. The utflite Unicode library is embedded.

**Expected warnings:** One unused function (`cell_is_word_end`) is reserved for future double-click selection improvements.

## Source Organization

```
src/
├── edit.c          # Main logic, event loop
├── types.h         # Shared type definitions
├── buffer.c        # Line and buffer operations
├── syntax.c        # Highlighting and neighbor layer
├── theme.c         # Color system, WCAG compliance
├── undo.c          # Operation-based history
├── search.c        # Find and replace
├── worker.c        # Background thread
├── input.c         # Keyboard and mouse handling
├── render.c        # Screen drawing
├── dialog.c        # File browser, theme picker
├── clipboard.c     # System clipboard integration
├── autosave.c      # Swap file management
└── terminal.c      # Raw mode, window size

third_party/
└── utflite/        # Unicode library
```

## License

GPL-2.0-only. See LICENSE file.

---

*Edit is what happens when someone gets tired of explaining vim to new developers and tired of launching VS Code to change one line.*

