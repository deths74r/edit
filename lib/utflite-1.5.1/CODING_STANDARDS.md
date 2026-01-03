<!-- SPDX-License-Identifier: GPL-2.0-only -->

# C Coding Standards

This document defines the mandatory coding standards for all C projects. All code contributions must follow these rules for consistency, readability, and maintainability.

---

## Purpose

These standards ensure that the codebase is:
- **Readable:** Clear naming and formatting make code easy to understand
- **Maintainable:** Consistent style reduces cognitive load when navigating the codebase
- **Beginner-friendly:** Comments and naming help newcomers learn the architecture

When code violates these standards, it must be refactored to comply. Section banners (e.g., `/*** ... ***/`) must be preserved exactly as written.

---

## Naming Conventions

### General Rules

All identifiers must use descriptive, full-word names. Avoid cryptic abbreviations that sacrifice clarity.

### Constants and Macros
- **Format:** `SCREAMING_SNAKE_CASE`
- **Examples:** `TAB_STOP_WIDTH`, `MAX_BUFFER_SIZE`, `CURSOR_MOVE`
- **Rule:** All preprocessor macros and constants use uppercase with underscores

### Functions, Variables, and Structs
- **Format:** `snake_case`
- **Examples:** `cursor_x`, `line_index`, `editor_config`, `row_count`
- **Rule:** Use full, descriptive words. Never abbreviate for brevity
  - Correct: `index`, `cursor_x`, `buffer_size`
  - Wrong: `idx`, `cx`, `buf_sz`

### Structs and Enums: No Typedefs
- **Rule:** Never use `typedef` for structs or enums. Always use explicit `struct name` and `enum name` declarations.
- **Rationale:** Typedefs hide what things actually are. When you see `struct` or `enum`, you immediately know the type category. This follows Linux kernel coding style.

```c
/* Correct - explicit struct and enum keywords everywhere */
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
void buffer_destroy(struct buffer *buffer);
enum buffer_state buffer_get_state(struct buffer *buffer);

/* Wrong - typedef hides the struct/enum nature */
typedef struct {
	Line       *lines;
	uint32_t    num_lines;
} Buffer;

typedef enum {
	EMPTY,
	MODIFIED,
	SAVED,
} BufferState;

Buffer *buffer_create(void);
BufferState buffer_get_state(Buffer *buffer);
```

**Exceptions** (when typedefs ARE acceptable):
- Opaque/semantic types: `uint32_t`, `size_t`, `pid_t`
- Architecture-varying types
- Forward declarations when truly needed for circular references

### Function Naming Pattern: Subject-Verb-Object
Functions must follow the pattern `<module>_<action>_<target>` to clearly express what they do.

**Format:** `module_verb_object`
- `module`: The component or data structure being operated on
- `verb`: The action being performed (append, write, destroy, insert, etc.)
- `object`: What is being acted upon

**Example transformations:**

| Old (unclear) | New (follows pattern) | Explanation |
|-----------------|------------------------|-------------|
| `append_buffer_append` | `append_buffer_write_text` | "write" is clearer than redundant "append" |
| `append_buffer_free` | `append_buffer_destroy_buffer` | "destroy" explicitly shows cleanup action |
| `append_buffer_append_color` | `append_buffer_write_color_code` | Specific object "color_code" |
| `append_buffer_append_bg` | `append_buffer_write_background_color_code` | No abbreviations; full words |
| `append_buffer_append_bold` | `append_buffer_write_bold_format` | Clear action and object |
| `append_buffer_reset_format` | `append_buffer_write_reset_format` | "write" + "reset_format" follows pattern |

### Prohibited Naming Patterns

**Do NOT use:**
- Type-hint prefixes like `g_` for globals
- Type-hint suffixes like `_t` for custom types (exception: standard C/POSIX types like `size_t`, `ssize_t`)
- Typedefs for structs (use explicit `struct name` instead)
- Hungarian notation or other type-encoding schemes
- Single-letter variables except for loop counters (`i`, `j`, `k`) in for/while loops
- Abbreviations that sacrifice clarity (`buf`, `sz`, `num`, `tmp`, `len`, `ext`, `seq`, `msg`)
- Single-letter globals or struct instances (e.g., `E` for editor config)
- Cryptic type names (`erow`, `abuf` - use `struct editor_row`, `struct append_buffer`)
- **Magic numbers** - all numeric literals must be named constants (except -1, 0, 1, 2 in trivial arithmetic)
- **Hardcoded strings** - terminal escape sequences, format strings, etc. must be constants

**Common violations to watch for:**
- Global variables with abbreviated names (use full descriptive names)
- Parameter names like `c`, `s`, `p`, `n` (except in very limited context)
- Local variables like `buf`, `len`, `ext`, `seq`, `fp`, `ws`
- Struct names with abbreviated words (`erow` → `struct editor_row`)

**Acceptable single-letter uses ONLY:**
- Loop counters: `i`, `j`, `k` in `for` and `while` loops
- Standard library conventions: `va_list ap` (but prefer `args` when possible)

### Enums
- **Format:** `snake_case`
- **Example:** `editor_highlight`, `syntax_type`, `key_action`

### Constants and Magic Numbers

**All numeric literals and strings must be named constants.** Magic numbers hurt readability and maintainability.

**Required:**
- Terminal escape sequences as constants
- Buffer sizes as constants
- Timeout values as constants
- File permissions as constants
- ASCII values as constants
- Configuration limits as constants

**Examples:**

```c
/* Good - named constants */
#define STATUS_MESSAGE_TIMEOUT_SECONDS 5
#define FILE_PERMISSION_DEFAULT 0644
#define ESCAPE_CLEAR_SCREEN "\x1b[2J"
#define CURSOR_POSITION_BUFFER_SIZE 32

if (time(NULL) - timestamp < STATUS_MESSAGE_TIMEOUT_SECONDS)
int fd = open(filename, O_RDWR | O_CREAT, FILE_PERMISSION_DEFAULT);
write(STDOUT_FILENO, ESCAPE_CLEAR_SCREEN, 4);
char buffer[CURSOR_POSITION_BUFFER_SIZE];

/* Bad - magic numbers and hardcoded strings */
if (time(NULL) - timestamp < 5)
int fd = open(filename, O_RDWR | O_CREAT, 0644);
write(STDOUT_FILENO, "\x1b[2J", 4);
char buffer[32];
```

**Exceptions:** Trivial arithmetic values (-1, 0, 1, 2) are acceptable when their meaning is obvious from context.

---

## Formatting and Layout

### C Standard
- **Rule:** All code must be C17 compliant (`-std=c17`)
- Avoid compiler-specific extensions unless absolutely necessary
- Use standard library functions over platform-specific alternatives

### Indentation
- **Rule:** Use tabs for indentation, displayed as 8 characters
- **Never use spaces for indentation**
- This follows Linux kernel style: wide indentation discourages deep nesting
- If your code is indented more than 3 levels, consider refactoring

### Brace Style (K&R)
Opening braces go on the same line as the function signature or control statement.

```c
// Correct
int main(void) {
	return 0;
}

void process_data(int count) {
	for (int i = 0; i < count; i++) {
		// code here
	}
}

// Wrong
int main(void)
{
	return 0;
}
```

### Pointer Declaration
Attach the asterisk to the **variable name**, not the type.

```c
// Correct
char *buffer;
int *data_ptr;
void process_line(char *line, size_t *length);

// Wrong
char* buffer;
int* data_ptr;
void process_line(char* line, size_t* length);
```

**Rationale:** `char *a, *b;` is clearer than `char* a, *b;` where the second pointer is easy to miss.

### Function Signatures
**Never wrap function signatures across multiple lines.** If a signature is too long, consider whether the function name or parameters can be simplified.

```c
// Correct - explicit struct, single line
int row_char_index_to_render_index(struct editor_row *row, int char_index);
void buffer_insert_char(struct buffer *buffer, int position, char character);

// Wrong - never wrap signatures
int row_char_index_to_render_index(struct editor_row *row,
                                   int char_index);
void buffer_insert_char(struct buffer *buffer,
                        int position,
                        char character);
```

### Array and Struct Initializers
Use flow-wrapped formatting with proper indentation. One logical element per line when formatting long initializers.

```c
// Correct - compact when appropriate
struct editor_theme editor_themes[] = {
	{ .name = "Dark",  .background = "000000", .foreground = "FFFFFF" },
	{ .name = "Light", .background = "FFFFFF", .foreground = "000000" },
};

// Correct - flow-wrapped arrays
char *c_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return",
	"else", "case", "struct", "union", "typedef", "enum", "class",
	"int|", "long|", "double|", "float|", "char|", "unsigned|",
	"signed|", "void|", NULL
};
```

### Macros
Keep macros on a single line unless multi-line continuation is absolutely necessary.

```c
// Correct - single line
#define CURSOR_MOVE "\x1b[%d;%dH"
#define MAX_BUFFER 4096

// Wrong - unnecessary line continuation
#define CURSOR_MOVE                                                   \
  "\x1b[%d;%dH"
```

### Section Banners
Preserve existing section banner formatting exactly as written.

```c
/*** Terminal ***/
/*** Row Operations ***/
/*** File I/O ***/
```

---

## Comments

Comments are **mandatory** and must be written for beginners. Assume the reader is learning C or unfamiliar with the codebase.

### Writing Style

**Tone:**
- Conversational and friendly
- Plain English, no jargon unless necessary
- Pretend you're teaching someone what the code does

**Natural, varied language:**
Write comments like you're explaining code to a colleague. Vary your sentence structure and avoid robotic repetition. Use direct descriptions, passive voice, or occasionally first-person - whatever sounds most natural for that particular comment.

```c
// Good - natural variety (different sentence structures)
/* Checks whether the cursor is inside the buffer bounds. */
/* Converts a character index to its rendered column position. */
/* Expands tabs to spaces when rendering the line. */
/* Tracks the total number of lines in this buffer. */
/* True when the cursor has moved since the last render. */

// Bad - robotic repetition (every comment starts the same way)
/* I check whether the cursor is inside the buffer bounds. */
/* I convert a character index to its rendered column position. */
/* I expand tabs to spaces when rendering the line. */
/* I track the total number of lines in this buffer. */
/* I am true when the cursor has moved since the last render. */

// Also bad - overly formal (redundant "This function" prefix)
/* This function checks whether the cursor is inside the buffer bounds. */
/* This function converts a character index to its rendered column position. */
/* This field tracks the total number of lines in this buffer. */
```

**Mix up your patterns:**
Use different ways to start comments to keep them readable and natural.

```c
// Examples of natural variation:
/* Stores the current cursor position. */
/* Number of lines in the buffer. */
/* When true, the file has unsaved changes. */
/* Converts screen coordinates to buffer offsets. */
/* Allocates memory for a new row and initializes it. */
/* Path to the currently open file, or NULL if unsaved. */
```

### What to Document

Comments must explain:
1. **What** the code does (the purpose)
2. **Why** it matters (the reason for existence)
3. **How** it works (algorithm or approach, if non-obvious)
4. **Parameters** and what they represent
5. **Return values** and their meaning
6. **Side effects** (state changes, memory allocation, I/O, etc.)
7. **Dependencies** or related state when relevant

### Placement Rules

**Always comment above, never trailing:**

```c
// Correct - comment above
/* Maximum number of characters per line */
#define MAX_LINE_LENGTH 1024

// Wrong - trailing comment
#define MAX_LINE_LENGTH 1024  // Maximum number of characters per line
```

**Required comment locations:**
- Above all function declarations and definitions
- Above all struct definitions
- Above all struct fields (unless trivially obvious)
- Above all macros and constants
- Above all enum declarations
- Above complex or non-obvious logic blocks

### Comment Format

Use block comment style (`/* */`) for most comments. Single-line `//` comments are acceptable for brief inline notes.

**Struct field comments:**

```c
// Correct - each field explained naturally, no typedef
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

// Wrong - trailing comments and uses typedef
typedef struct editor_row {
	int line_index;      // The original index (0-based)
	int line_size;       // Number of bytes
	int render_size;     // Rendered columns
	char *chars;         // Actual characters
	char *render;        // Rendered version
	unsigned char *highlight;  // Highlight types
	int open_comment;    // Multi-line comment flag
} editor_row;
```

### Function Comment Template

Function comments should explain what the function does, how it works (if non-obvious), what the parameters mean, what it returns, and any side effects. Write naturally without formal sections:

```c
/* Converts a character index in the raw line to a render index by accounting
 * for tab expansion. Iterates through characters up to the given index,
 * expanding tabs into spaces and counting columns. The row parameter points to
 * the target line structure, and char_index is the character offset in the raw
 * line (0-based). Returns the rendered column index without modifying any state. */
int row_char_index_to_render_index(struct editor_row *row, int char_index);
```

### Level of Detail

- **Do** explain the purpose and behavior
- **Do** mention side effects, allocations, or state changes
- **Don't** explain obvious C syntax
- **Don't** comment trivial assignments or self-explanatory code
- **Don't** use formal labeled sections like "Purpose:", "Parameters:", "Returns:"

### Examples

**Good comments:**

```c
/* Allocates a new row structure and initializes all fields to safe defaults.
 * Memory is allocated for the row itself but not yet for its character buffers. */
struct editor_row *row_create(void);

/* Frees all memory associated with this row, including the chars, render, and
 * highlight arrays. After calling this, the row pointer is invalid. */
void row_destroy(struct editor_row *row);

/* Handles the Enter key by inserting a newline at the cursor position. If the
 * cursor is in the middle of a line, splits the line at that point. */
void editor_handle_enter_key(void);
```

**Bad comments (too formal or unclear):**

```c
// Too formal, uses labeled sections
/*
 * row_create
 * Purpose: Allocate a new row
 * Returns: Pointer to editor_row
 * Side effects: Allocates memory
 */
struct editor_row *row_create(void);

// States the obvious
/* This function frees memory */
void row_destroy(struct editor_row *row);

// Too vague
/* Handles key */
void editor_handle_enter_key(void);
```

---

## Complete Examples: Before and After

This section demonstrates real transformations from non-compliant to compliant code.

### Example 1: Macro Formatting

**Before (wrong - unnecessary line continuation):**

```c
#define CURSOR_MOVE                                                            \
  "\x1b[%d;%dH"
```

**After (correct - single line with comment):**

```c
/* ANSI escape sequence to move the cursor to a specific row and column. */
#define CURSOR_MOVE "\x1b[%d;%dH"
```

### Example 2: Struct/Array Initializers

**Before (wrong - poor formatting, cryptic names, trailing comments):**

```c
struct EditorTheme editorThemes[] = {{/* Dark - Original */
                                      .name = "Dark",
                                      .background = "000000",
                                      .foreground = "FFFFFF",
                                      .lineNumber = "777777",
                                      .statusBar = "181818",
                                      .statusBarText = "E0E0E0",
                                      .messageBar = "CCCCCC",
                                      .highlightBackground = "282828",
                                      .highlightForeground = "FFFFFF",
                                      .comment = "888888",
                                      .keyword1 = "FFFFFF",
                                      .keyword2 = "E0E0E0",
                                      .string = "BBBBBB",
                                      .number = "BBBBBB",
                                      .match = "000000"},
                                     {/* Light - Original */
                                      .name = "Light",
                                      .background = "FFFFFF",
                                      .foreground = "000000",
                                      .lineNumber = "808080",
                                      .statusBar = "E0E0E0",
                                      .statusBarText = "222222",
                                      .messageBar = "444444",
                                      .highlightBackground = "CCCCCC",
                                      .highlightForeground = "000000",
                                      .comment = "606060",
                                      .keyword1 = "333333",
                                      .keyword2 = "555555",
                                      .string = "444444",
                                      .number = "444444",
                                      .match = "000000"},}
```

**After (correct - snake_case, compact formatting, comments above):**

```c
/* Available color themes for the editor. Only essential fields shown for brevity. */
struct editor_theme editor_themes[] = {
	{ .name = "Dark",  .background = "000000", .foreground = "FFFFFF" },
	{ .name = "Light", .background = "FFFFFF", .foreground = "000000" },
};
```

### Example 3: Flow-Wrapped Arrays

**Correct formatting for keyword arrays:**

```c
/* C language keywords for syntax highlighting. Keywords ending with '|' are types. */
char *c_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return",
	"else", "case", "struct", "union", "typedef", "enum", "class",
	"int|", "long|", "double|", "float|", "char|", "unsigned|",
	"signed|", "void|", NULL
};
```

### Example 4: Function Signatures

**Before (wrong - wrapped signature, cryptic names):**

```c
int row_cx_to_rx(editor_row *row,
                 int cx);
```

**After (correct - single line, descriptive names, explicit struct):**

```c
/* Converts a character index to its rendered column position. */
int row_char_index_to_render_index(struct editor_row *row, int char_index);

/* Converts a rendered column position back to a character index. */
int row_render_index_to_char_index(struct editor_row *row, int render_index);
```

### Example 5: Struct Formatting and Comments

**Before (wrong - trailing comments, cryptic names, typedef, poor formatting):**

```c
typedef struct editor_row {
  int idx;   // The original index (line number) of this row in the file
             // (0-based).
  int size;  // The number of actual characters (bytes) in this line.
  int rsize; // The number of columns this line takes up when *rendered* (tabs
             // expanded).
  char *
      chars; // Pointer to the memory holding the actual characters of the line.
  char *render; // Pointer to the memory holding the *rendered* version (tabs
                // expanded to spaces).
  unsigned char *hl;   // Pointer to an array holding the highlight type (enum
                       // editorHighlight) for each *rendered* character.
  int hl_open_comment; // A flag: is this line part of an unclosed multi-line
                       // comment? (Used for highlighting subsequent lines).
} editor_row;
```

**After (correct - no typedef, comments above, descriptive names, tab indentation):**

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

---

## Comprehensive Scanning for Violations

When reviewing code for compliance, systematically scan for these specific violations:

### Global Variables
- [ ] Check ALL global variable names - no single letters or abbreviations
  - BAD: `E`, `G`, `cfg`, `buf`
  - GOOD: `editor`, `global_state`, `configuration`, `buffer`

### Type Names (struct, enum)
- [ ] Scan for cryptic struct names
  - BAD: `erow`, `abuf`, `cfg`
  - GOOD: `struct editor_row`, `struct append_buffer`, `struct configuration`
- [ ] Ensure no typedefs for structs
  - BAD: `typedef struct { ... } Buffer;`
  - GOOD: `struct buffer { ... };`

### Local Variables
- [ ] Find all abbreviated local variable names
  - BAD: `buf`, `len`, `ext`, `seq`, `msg`, `fp`, `ws`, `c`, `s`, `p`
  - GOOD: `buffer`, `length`, `extension`, `sequence`, `message`, `file_pointer`, `window_size`, `character`, `string`, `pointer`
  - EXCEPTION: Loop counters `i`, `j`, `k` are acceptable

### Function Parameters
- [ ] Review ALL function parameter names
  - BAD: `void func(char *s, int len, FILE *fp)`
  - GOOD: `void func(char *string, int length, FILE *file_pointer)`

### Search Patterns
Use these regex patterns to find violations:
- Single-letter globals: `^[A-Z]\s+[a-z];` or `^struct.*\s+[A-Z];`
- Abbreviated locals: `\s+(buf|len|ext|seq|msg|tmp|num|str|ptr|chr)\s*[=;]`
- Parameter abbreviations in function signatures

## Code Review Checklist

Use this checklist when reviewing code or refactoring existing code to ensure compliance.

### Mandatory Changes

When code violates these standards, it must be refactored:

1. **Rewrite non-compliant code** to conform to all rules
2. **Preserve behavior** — changes should be cosmetic (formatting, naming, comments only)
3. **Add missing function comments** — every function needs a beginner-friendly comment
4. **Move trailing comments** — convert to above-line block comments
5. **Rename identifiers** — use `snake_case` or `SCREAMING_SNAKE_CASE` as appropriate
6. **Simplify macros** — collapse single-line macros, remove unnecessary line continuations
7. **Reformat arrays/initializers** — use flow-wrapped, indented style
8. **Remove prohibited patterns** — no typedefs for structs, no `_t` suffixes, no `g_` prefixes, no cryptic abbreviations
9. **Preserve section banners** — keep `/*** ... ***/` style exactly as written

Use this checklist for every code contribution or refactoring:

#### Formatting
- [ ] Tab indentation (displayed as 8 characters)
- [ ] K&R brace style (opening brace on same line)
- [ ] Pointer asterisk attached to variable (`char *p`)
- [ ] Function signatures never wrapped across lines
- [ ] Macros on single line when possible
- [ ] Arrays/initializers use flow-wrapped formatting
- [ ] Section banners (`/*** ... ***/`) preserved exactly

#### Naming
- [ ] Constants/macros use `SCREAMING_SNAKE_CASE`
- [ ] Functions/variables/structs use `snake_case`
- [ ] No cryptic abbreviations (use full words)
- [ ] Names are descriptive and context-rich
- [ ] Function names follow `module_verb_object` pattern
- [ ] No `g_` prefix for globals
- [ ] No custom `_t` suffix for types
- [ ] No typedefs for structs (use explicit `struct name`)
- [ ] Enum names use lowercase `snake_case`

#### Comments
- [ ] All functions have descriptive comments
- [ ] All struct definitions have comments
- [ ] All struct fields have comments (unless trivial)
- [ ] All macros/constants have comments
- [ ] Comments are above the entity, not trailing
- [ ] Comments use conversational, beginner-friendly tone
- [ ] Comments use natural, varied sentence structures (avoid robotic repetition)
- [ ] Comments explain what, why, how, parameters, returns, and side effects
- [ ] No formal labeled sections ("Purpose:", "Returns:", etc.)

---

## Quick Reference: Complete Transformation

This example shows a complete before/after transformation applying all standards.

**Before (multiple violations):**

```c
#define CURSOR_MOVE                                                            \
  "\x1b[%d;%dH"

int row_cx_to_rx(editor_row *row,
                 int cx);  // convert char to render index

puts("Hello world");  // prints hello world
```

**After (fully compliant):**

```c
/* ANSI escape sequence to move the cursor to a specific row and column. */
#define CURSOR_MOVE "\x1b[%d;%dH"

/* Converts a character index in the raw line to a rendered column index by
 * accounting for tab expansion. Iterates through characters up to the given
 * index, expanding tabs into spaces and counting columns. The row parameter
 * points to the target line structure, and char_index is the character offset
 * in the raw line (0-based). Returns the rendered column index without
 * modifying any state. */
int row_char_index_to_render_index(struct editor_row *row, int char_index);

/* Prints a friendly greeting message. */
puts("Hello world");
```

---

## Summary

These standards exist to make the codebase:
- **Easy to read** for developers of all experience levels
- **Easy to maintain** through consistency and clarity
- **Easy to learn** with beginner-friendly comments

All code contributions must follow these rules. When in doubt, refer to the examples in this document.
