# C Coding Standards

All code must follow these rules. Non-compliant code must be refactored.

## Naming

All identifiers use descriptive, full-word names. No abbreviations that sacrifice clarity.

| Category | Format | Examples |
|----------|--------|----------|
| Constants, macros | `SCREAMING_SNAKE_CASE` | `TAB_STOP_WIDTH`, `CURSOR_MOVE` |
| Functions | `module_verb_object` | `append_buffer_write_text`, `row_destroy` |
| Variables, structs, enums | `snake_case` | `cursor_x`, `line_index`, `editor_highlight` |

### Function Names: Module-Verb-Object

Functions follow the pattern `module_verb_object`:

| Wrong | Right | Why |
|-------|-------|-----|
| `append_buffer_append` | `append_buffer_write_text` | Redundant verb; "write" is clearer |
| `append_buffer_free` | `append_buffer_destroy_buffer` | "destroy" shows cleanup |
| `append_buffer_append_bg` | `append_buffer_write_background_color_code` | Full words, no abbreviations |

### No Typedefs for Structs or Enums

Always use explicit `struct name` and `enum name`. Typedefs hide what things are. This follows Linux kernel style.

```c
/* Correct */
struct buffer {
	struct line    *lines;
	uint32_t        num_lines;
};

enum buffer_state {
	BUFFER_EMPTY,
	BUFFER_MODIFIED,
	BUFFER_SAVED,
};

struct buffer *buffer_create(void);
enum buffer_state buffer_get_state(struct buffer *buffer);

/* Wrong */
typedef struct { ... } Buffer;
typedef enum { ... } BufferState;
```

Typedef exceptions: opaque/semantic types (`uint32_t`, `size_t`, `pid_t`), architecture-varying types, forward declarations for circular references.

### Prohibited Patterns

- `g_` prefixes, `_t` suffixes on custom types, Hungarian notation
- Single-letter variables (except loop counters `i`, `j`, `k`)
- Abbreviations: `buf`, `sz`, `num`, `tmp`, `len`, `ext`, `seq`, `msg`, `fp`, `ws`
- Cryptic names: `erow` → `struct editor_row`, `abuf` → `struct append_buffer`, `E` → `editor`
- Magic numbers — all numeric literals must be named constants (except -1, 0, 1, 2 in trivial arithmetic)
- Hardcoded strings — terminal escape sequences, format strings, etc. must be constants

```c
/* Correct */
#define STATUS_MESSAGE_TIMEOUT_SECONDS 5
#define FILE_PERMISSION_DEFAULT 0644
#define ESCAPE_CLEAR_SCREEN "\x1b[2J"
#define CURSOR_POSITION_BUFFER_SIZE 32

if (time(NULL) - timestamp < STATUS_MESSAGE_TIMEOUT_SECONDS)
char buffer[CURSOR_POSITION_BUFFER_SIZE];

/* Wrong */
if (time(NULL) - timestamp < 5)
char buffer[32];
```

## Formatting

### C Standard

All code must be C23 compliant (`-std=c23`). Prefer standard library functions over platform-specific alternatives.

### Indentation

Tabs only, displayed as 8 characters. Never spaces. If code is indented more than 3 levels, consider refactoring.

### Brace Style

Function opening braces go on a **new line**. Control structure braces go on the **same line**.

```c
int main(void)
{
	return 0;
}

void process_data(int count)
{
	for (int i = 0; i < count; i++) {
		if (condition) {
			// code here
		}
	}
}
```

### Pointer Declaration

Asterisk attaches to the variable name: `char *buffer`, not `char* buffer`.

### Function Signatures

Never wrap across multiple lines. If too long, simplify the name or parameters.

```c
/* Correct */
int row_char_index_to_render_index(struct editor_row *row, int char_index);

/* Wrong */
int row_char_index_to_render_index(struct editor_row *row,
                                   int char_index);
```

### Initializers

Struct arrays use compact single-line entries. Keyword arrays use flow-wrapped formatting.

```c
struct editor_theme editor_themes[] = {
	{ .name = "Dark",  .background = "000000", .foreground = "FFFFFF" },
	{ .name = "Light", .background = "FFFFFF", .foreground = "000000" },
};

char *c_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return",
	"else", "case", "struct", "union", "typedef", "enum", "class",
	"int|", "long|", "double|", "float|", "char|", "unsigned|",
	"signed|", "void|", NULL
};
```

### Macros

Single line unless multi-line continuation is truly necessary.

```c
/* Correct */
#define CURSOR_MOVE "\x1b[%d;%dH"

/* Wrong */
#define CURSOR_MOVE                                                   \
  "\x1b[%d;%dH"
```

### Section Banners

Preserve existing section banners exactly as written: `/*** Terminal ***/`, `/*** Row Operations ***/`, etc.

## Comments

Comments are mandatory and written for beginners. Assume the reader is learning C or is unfamiliar with the codebase.

### Placement

Always above the entity, never trailing. Required above: functions, structs, struct fields, macros, constants, enums, and non-obvious logic.

```c
/* Correct */
/* Maximum number of characters per line */
#define MAX_LINE_LENGTH 1024

/* Wrong */
#define MAX_LINE_LENGTH 1024  // Maximum number of characters per line
```

### Format

Use `/* */` block comments. Brief `//` comments are acceptable for inline notes.

### Tone

Conversational and friendly. Plain English. Vary your sentence structure — avoid starting every comment the same way. No formal labeled sections ("Purpose:", "Parameters:", "Returns:").

```c
/* Good - natural variety */
/* Checks whether the cursor is inside the buffer bounds. */
/* Converts a character index to its rendered column position. */
/* Number of lines in the buffer. */
/* When true, the file has unsaved changes. */
/* Path to the currently open file, or NULL if unsaved. */

/* Bad - robotic repetition */
/* This function checks whether the cursor is inside the buffer bounds. */
/* This function converts a character index to its rendered column position. */
/* This field tracks the total number of lines in this buffer. */
```

### What to Cover

Explain what the code does, why it exists, how it works (if non-obvious), what parameters mean, what it returns, and any side effects (state changes, allocation, I/O). Don't explain obvious C syntax or trivial assignments.

### Function Comments

Write naturally without formal sections:

```c
/* Converts a character index in the raw line to a render index by accounting
 * for tab expansion. Iterates through characters up to the given index,
 * expanding tabs into spaces and counting columns. The row parameter points to
 * the target line structure, and char_index is the character offset in the raw
 * line (0-based). Returns the rendered column index without modifying any state. */
int row_char_index_to_render_index(struct editor_row *row, int char_index);
```

### Struct Comments

Each field gets its own comment above it:

```c
/* Represents a single line of text in the editor, storing both the raw
 * characters and the rendered version with tabs expanded. */
struct editor_row {
	/* Original index (line number) of this row in the file (0-based). */
	int line_index;

	/* Number of actual characters (bytes) in this line. */
	int line_size;

	/* Number of columns this line takes up when rendered (tabs expanded). */
	int render_size;

	/* Pointer to the memory holding the actual characters of the line. */
	char *chars;

	/* Rendered version with tabs expanded to spaces. */
	char *render;

	/* Highlight type (enum editor_highlight) for each rendered character. */
	unsigned char *highlight;

	/* True if this line is part of an unclosed multi-line comment. */
	int open_comment;
};
```

## Review Checklist

- [ ] Tab indentation, 8-char display width
- [ ] K&R braces: new line for functions, same line for control structures
- [ ] Pointer asterisk on the variable name
- [ ] Function signatures on a single line
- [ ] Macros on a single line
- [ ] Section banners preserved exactly
- [ ] `SCREAMING_SNAKE_CASE` for constants/macros
- [ ] `snake_case` for everything else
- [ ] `module_verb_object` function names
- [ ] No abbreviations, no single-letter names (except `i`, `j`, `k`)
- [ ] No typedefs for structs or enums
- [ ] No magic numbers or hardcoded strings
- [ ] Every function, struct, field, macro, and enum has a comment above it
- [ ] Comments are conversational with varied sentence structure
- [ ] No trailing comments, no formal labeled sections
