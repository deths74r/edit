/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * syntax.c - Syntax highlighting and neighbor layer implementation
 */

#define _GNU_SOURCE

#include "edit.h"
#include "syntax.h"
#include "buffer.h"

extern struct editor_state editor;

/*****************************************************************************
 * Neighbor Layer - Character Classification
 *****************************************************************************/

/*
 * Classify a codepoint into a character class.
 */
enum character_class classify_codepoint(uint32_t cp)
{
	if (cp == ' ' || cp == '\t') {
		return CHAR_CLASS_WHITESPACE;
	}
	if (cp == '_') {
		return CHAR_CLASS_UNDERSCORE;
	}
	if (cp >= 'a' && cp <= 'z') {
		return CHAR_CLASS_LETTER;
	}
	if (cp >= 'A' && cp <= 'Z') {
		return CHAR_CLASS_LETTER;
	}
	if (cp >= '0' && cp <= '9') {
		return CHAR_CLASS_DIGIT;
	}
	if (cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
	    cp == '{' || cp == '}') {
		return CHAR_CLASS_BRACKET;
	}
	if (cp == '"' || cp == '\'' || cp == '`') {
		return CHAR_CLASS_QUOTE;
	}
	/* ASCII punctuation */
	if ((cp >= '!' && cp <= '/') || (cp >= ':' && cp <= '@') ||
	    (cp >= '[' && cp <= '`') || (cp >= '{' && cp <= '~')) {
		return CHAR_CLASS_PUNCTUATION;
	}
	/* Unicode letters (simplified - covers common ranges) */
	if (cp >= 0x00C0 && cp <= 0x024F) {
		return CHAR_CLASS_LETTER;  /* Latin Extended */
	}
	if (cp >= 0x0400 && cp <= 0x04FF) {
		return CHAR_CLASS_LETTER;  /* Cyrillic */
	}
	if (cp >= 0x4E00 && cp <= 0x9FFF) {
		return CHAR_CLASS_LETTER;  /* CJK */
	}

	return CHAR_CLASS_OTHER;
}

/*
 * Check if two character classes form a word together.
 */
bool classes_form_word(enum character_class a, enum character_class b)
{
	/* Letters, digits, and underscores form words together */
	bool a_is_word_char = (a == CHAR_CLASS_LETTER || a == CHAR_CLASS_DIGIT ||
	                       a == CHAR_CLASS_UNDERSCORE);
	bool b_is_word_char = (b == CHAR_CLASS_LETTER || b == CHAR_CLASS_DIGIT ||
	                       b == CHAR_CLASS_UNDERSCORE);
	return a_is_word_char && b_is_word_char;
}

/*
 * Compute neighbor data (character class and token position) for a line.
 */
void neighbor_compute_line(struct line *line)
{
	if (line->cell_count == 0) {
		return;
	}

	/* First pass: assign character classes */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		enum character_class class = classify_codepoint(line->cells[i].codepoint);
		line->cells[i].neighbor = neighbor_encode(class, TOKEN_POSITION_SOLO);
	}

	/* Second pass: compute token positions */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		enum character_class my_class = neighbor_get_class(line->cells[i].neighbor);

		bool has_prev = (i > 0);
		bool has_next = (i < line->cell_count - 1);

		enum character_class prev_class = has_prev ?
			neighbor_get_class(line->cells[i - 1].neighbor) : CHAR_CLASS_WHITESPACE;
		enum character_class next_class = has_next ?
			neighbor_get_class(line->cells[i + 1].neighbor) : CHAR_CLASS_WHITESPACE;

		bool joins_prev = has_prev && classes_form_word(prev_class, my_class);
		bool joins_next = has_next && classes_form_word(my_class, next_class);

		enum token_position position;
		if (!joins_prev && !joins_next) {
			position = TOKEN_POSITION_SOLO;
		} else if (!joins_prev && joins_next) {
			position = TOKEN_POSITION_START;
		} else if (joins_prev && joins_next) {
			position = TOKEN_POSITION_MIDDLE;
		} else {
			position = TOKEN_POSITION_END;
		}

		line->cells[i].neighbor = neighbor_encode(my_class, position);
	}
}

/*
 * Is this cell at the start of a word?
 */
bool cell_is_word_start(struct cell *cell)
{
	enum token_position pos = neighbor_get_position(cell->neighbor);
	return pos == TOKEN_POSITION_START || pos == TOKEN_POSITION_SOLO;
}

/*
 * Is this cell at the end of a word?
 */
bool cell_is_word_end(struct cell *cell)
{
	enum token_position pos = neighbor_get_position(cell->neighbor);
	return pos == TOKEN_POSITION_END || pos == TOKEN_POSITION_SOLO;
}

/*
 * Check if a cell is trailing whitespace.
 */
bool is_trailing_whitespace(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return false;
	}

	/* First check if current cell is whitespace. */
	enum character_class current_class =
		neighbor_get_class(line->cells[column].neighbor);
	if (current_class != CHAR_CLASS_WHITESPACE) {
		return false;
	}

	/* Check if there's any non-whitespace after this position. */
	for (uint32_t i = column + 1; i < line->cell_count; i++) {
		enum character_class cell_class =
			neighbor_get_class(line->cells[i].neighbor);
		if (cell_class != CHAR_CLASS_WHITESPACE) {
			return false;
		}
	}

	return true;
}

/*
 * Find start of previous word.
 */
uint32_t find_prev_word_start(struct line *line, uint32_t column)
{
	if (column == 0 || line->cell_count == 0) {
		return 0;
	}

	column--;

	/* Skip whitespace */
	while (column > 0 &&
	       neighbor_get_class(line->cells[column].neighbor) == CHAR_CLASS_WHITESPACE) {
		column--;
	}

	/* Find start of this word */
	while (column > 0 && !cell_is_word_start(&line->cells[column])) {
		column--;
	}

	return column;
}

/*
 * Find start of next word.
 */
uint32_t find_next_word_start(struct line *line, uint32_t column)
{
	if (column >= line->cell_count) {
		return line->cell_count;
	}

	/* Move past current position */
	column++;

	/* Skip until we find a non-whitespace word start */
	while (column < line->cell_count) {
		if (neighbor_get_class(line->cells[column].neighbor) != CHAR_CLASS_WHITESPACE &&
		    cell_is_word_start(&line->cells[column])) {
			break;
		}
		column++;
	}

	return column;
}

/*****************************************************************************
 * Pair Entanglement - Bracket and Comment Matching
 *****************************************************************************/

/*
 * Allocate a unique pair ID.
 */
uint32_t buffer_allocate_pair_id(struct buffer *buffer)
{
	return ++buffer->next_pair_id;
}

/*
 * Scan the entire buffer to match pairs.
 */
void buffer_compute_pairs(struct buffer *buffer)
{
	/* Reset all pair IDs and warm all lines first */
	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}
		for (uint32_t col = 0; col < line->cell_count; col++) {
			line->cells[col].context = 0;
		}
	}

	buffer->next_pair_id = 0;

	/* Stack for bracket matching */
	struct {
		uint32_t row;
		uint32_t col;
		uint32_t pair_id;
		enum pair_type type;
	} stack[256];
	int stack_top = 0;

	/* Are we inside a block comment? */
	bool in_block_comment = false;
	uint32_t comment_pair_id = 0;

	for (uint32_t row = 0; row < buffer->line_count; row++) {
		struct line *line = &buffer->lines[row];

		for (uint32_t col = 0; col < line->cell_count; col++) {
			uint32_t cp = line->cells[col].codepoint;

			/* Check for block comment start */
			if (!in_block_comment && cp == '/' && col + 1 < line->cell_count &&
			    line->cells[col + 1].codepoint == '*') {
				in_block_comment = true;
				comment_pair_id = buffer_allocate_pair_id(buffer);

				/* Mark the '/' and '*' as opener */
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_OPENER);
				col++;
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_OPENER);
				continue;
			}

			/* Check for block comment end */
			if (in_block_comment && cp == '*' && col + 1 < line->cell_count &&
			    line->cells[col + 1].codepoint == '/') {
				/* Mark the '*' and '/' as closer */
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_CLOSER);
				col++;
				line->cells[col].context = context_encode(comment_pair_id,
					PAIR_TYPE_COMMENT, PAIR_ROLE_CLOSER);
				in_block_comment = false;
				continue;
			}

			/* Skip other processing if inside block comment */
			if (in_block_comment) {
				continue;
			}

			/* Opening brackets */
			if (cp == '(' || cp == '[' || cp == '{') {
				enum pair_type type = (cp == '(') ? PAIR_TYPE_PAREN :
				                      (cp == '[') ? PAIR_TYPE_BRACKET :
				                      PAIR_TYPE_BRACE;
				uint32_t pair_id = buffer_allocate_pair_id(buffer);

				line->cells[col].context = context_encode(pair_id, type,
					PAIR_ROLE_OPENER);

				if (stack_top < BRACKET_STACK_SIZE) {
					stack[stack_top].row = row;
					stack[stack_top].col = col;
					stack[stack_top].pair_id = pair_id;
					stack[stack_top].type = type;
					stack_top++;
				}
				continue;
			}

			/* Closing brackets */
			if (cp == ')' || cp == ']' || cp == '}') {
				enum pair_type type = (cp == ')') ? PAIR_TYPE_PAREN :
				                      (cp == ']') ? PAIR_TYPE_BRACKET :
				                      PAIR_TYPE_BRACE;

				/* Find matching opener on stack */
				int match = -1;
				for (int i = stack_top - 1; i >= 0; i--) {
					if (stack[i].type == type) {
						match = i;
						break;
					}
				}

				if (match >= 0) {
					uint32_t pair_id = stack[match].pair_id;
					line->cells[col].context = context_encode(pair_id, type,
						PAIR_ROLE_CLOSER);

					/* Remove from stack (and any unmatched openers above it) */
					stack_top = match;
				} else {
					/* Unmatched closer */
					line->cells[col].context = 0;
				}
				continue;
			}
		}
	}
}

/*
 * Given a cell with a pair context, find its matching partner.
 */
bool buffer_find_pair_partner(struct buffer *buffer,
                              uint32_t row, uint32_t col,
                              uint32_t *out_row, uint32_t *out_col)
{
	if (row >= buffer->line_count) {
		return false;
	}

	struct line *line = &buffer->lines[row];
	if (col >= line->cell_count) {
		return false;
	}

	uint32_t context = line->cells[col].context;
	uint32_t pair_id = context_get_pair_id(context);
	enum pair_role role = context_get_pair_role(context);

	if (pair_id == 0 || role == PAIR_ROLE_NONE) {
		return false;
	}

	/* Search direction depends on role */
	bool search_forward = (role == PAIR_ROLE_OPENER);

	if (search_forward) {
		/* Search forward for closer */
		for (uint32_t r = row; r < buffer->line_count; r++) {
			struct line *search_line = &buffer->lines[r];
			if (line_get_temperature(search_line) == LINE_TEMPERATURE_COLD) {
				line_warm(search_line, buffer);
			}

			uint32_t start_col = (r == row) ? col + 1 : 0;
			for (uint32_t c = start_col; c < search_line->cell_count; c++) {
				if (context_get_pair_id(search_line->cells[c].context) == pair_id &&
				    context_get_pair_role(search_line->cells[c].context) == PAIR_ROLE_CLOSER) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	} else {
		/* Search backward for opener */
		for (int r = row; r >= 0; r--) {
			struct line *search_line = &buffer->lines[r];
			if (line_get_temperature(search_line) == LINE_TEMPERATURE_COLD) {
				line_warm(search_line, buffer);
			}

			int start_col = (r == (int)row) ? (int)col - 1 :
				(int)search_line->cell_count - 1;
			for (int c = start_col; c >= 0; c--) {
				if (context_get_pair_id(search_line->cells[c].context) == pair_id &&
				    context_get_pair_role(search_line->cells[c].context) == PAIR_ROLE_OPENER) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	}

	return false;
}

/*
 * Check if a position is inside a block comment.
 */
static bool syntax_is_in_block_comment(struct buffer *buffer, uint32_t row, uint32_t col)
{
	/* Scan backwards for an unclosed block comment opener */
	for (int r = row; r >= 0; r--) {
		struct line *line = &buffer->lines[r];
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}

		int end_col = (r == (int)row) ? (int)col - 1 : (int)line->cell_count - 1;

		for (int c = end_col; c >= 0; c--) {
			uint32_t context = line->cells[c].context;
			enum pair_type type = context_get_pair_type(context);
			enum pair_role role = context_get_pair_role(context);

			if (type == PAIR_TYPE_COMMENT) {
				if (role == PAIR_ROLE_CLOSER) {
					/* Found a closer before us, so we're not in that comment */
					return false;
				}
				if (role == PAIR_ROLE_OPENER) {
					/* Found an opener - check if it closes after our position */
					uint32_t partner_row, partner_col;
					if (buffer_find_pair_partner(buffer, r, c,
					                             &partner_row, &partner_col)) {
						/* Has a closer - are we before it? */
						if (partner_row > row ||
						    (partner_row == row && partner_col >= col)) {
							return true;  /* We're inside this comment */
						}
						/* Closer is before us, keep searching */
					} else {
						/* Unclosed comment - we're inside it */
						return true;
					}
				}
			}
		}
	}

	return false;
}

/*****************************************************************************
 * Syntax Highlighting
 *****************************************************************************/

/* C language keywords - control flow and declarations. */
static const char *C_KEYWORDS[] = {
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "goto", "sizeof", "typedef",
	"struct", "union", "enum", "static", "const", "volatile",
	"extern", "register", "inline", "restrict", "_Atomic", "_Noreturn",
	NULL
};

/* C language type names and common typedefs. */
static const char *C_TYPES[] = {
	"int", "char", "short", "long", "float", "double", "void",
	"signed", "unsigned", "bool", "true", "false", "NULL",
	"int8_t", "int16_t", "int32_t", "int64_t",
	"uint8_t", "uint16_t", "uint32_t", "uint64_t",
	"size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
	"FILE", "va_list",
	NULL
};

/* Returns true if codepoint is an ASCII letter. */
static bool syntax_is_alpha(uint32_t cp)
{
	return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

/* Returns true if codepoint is an ASCII digit. */
static bool syntax_is_digit(uint32_t cp)
{
	return cp >= '0' && cp <= '9';
}

/* Returns true if codepoint is alphanumeric or underscore. */
bool syntax_is_alnum(uint32_t cp)
{
	return syntax_is_alpha(cp) || syntax_is_digit(cp) || cp == '_';
}

/* Returns true if codepoint could be part of a number literal. */
static bool syntax_is_number_char(uint32_t cp)
{
	return syntax_is_digit(cp) || cp == '.' || cp == 'x' || cp == 'X' ||
	       (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F') ||
	       cp == 'u' || cp == 'U' || cp == 'l' || cp == 'L';
}

/* Returns true if codepoint is a C operator. */
static bool syntax_is_operator(uint32_t cp)
{
	return cp == '+' || cp == '-' || cp == '*' || cp == '/' ||
	       cp == '=' || cp == '<' || cp == '>' || cp == '!' ||
	       cp == '&' || cp == '|' || cp == '^' || cp == '~' ||
	       cp == '%' || cp == '?' || cp == ':' || cp == ';' ||
	       cp == ',' || cp == '.';
}

/* Returns true if codepoint is a bracket character. */
static bool syntax_is_bracket(uint32_t cp)
{
	return cp == '(' || cp == ')' || cp == '[' || cp == ']' ||
	       cp == '{' || cp == '}';
}

/* Returns true if position is at line start (only whitespace before). */
static bool syntax_is_line_start(struct line *line, uint32_t pos)
{
	for (uint32_t i = 0; i < pos; i++) {
		uint32_t cp = line->cells[i].codepoint;
		if (cp != ' ' && cp != '\t') {
			return false;
		}
	}
	return true;
}

/* Extracts a word from cells into a buffer. Only ASCII characters. */
static void syntax_extract_word(struct line *line, uint32_t start, uint32_t end,
                                char *buffer, size_t buffer_size)
{
	size_t len = 0;
	for (uint32_t i = start; i < end && len < buffer_size - 1; i++) {
		uint32_t cp = line->cells[i].codepoint;
		if (cp < 128) {
			buffer[len++] = (char)cp;
		}
	}
	buffer[len] = '\0';
}

/* Returns true if word is a C keyword. */
static bool syntax_is_keyword(const char *word)
{
	for (int i = 0; C_KEYWORDS[i] != NULL; i++) {
		if (strcmp(word, C_KEYWORDS[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns true if word is a C type name. */
static bool syntax_is_type(const char *word)
{
	for (int i = 0; C_TYPES[i] != NULL; i++) {
		if (strcmp(word, C_TYPES[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Returns true if filename has a C/C++ extension. */
bool syntax_is_c_file(const char *filename)
{
	if (filename == NULL) {
		return false;
	}

	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return false;
	}

	const char *ext = dot + 1;
	return strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0 ||
	       strcmp(ext, "cpp") == 0 || strcmp(ext, "hpp") == 0 ||
	       strcmp(ext, "cc") == 0 || strcmp(ext, "cxx") == 0;
}

/* Returns true if filename has a Markdown extension. */
bool syntax_is_markdown_file(const char *filename)
{
	if (filename == NULL) {
		return false;
	}

	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return false;
	}

	const char *ext = dot + 1;
	return strcmp(ext, "md") == 0 || strcmp(ext, "markdown") == 0 ||
	       strcmp(ext, "mkd") == 0 || strcmp(ext, "mdx") == 0;
}

/*
 * Apply syntax highlighting to a single line.
 */
void syntax_highlight_line(struct line *line, struct buffer *buffer,
                           uint32_t row)
{
	/* Dispatch to language-specific highlighter */
	if (syntax_is_markdown_file(buffer->filename)) {
		syntax_highlight_markdown_line(line, buffer, row);
		return;
	}

	/* Only highlight C files from here on */
	if (!syntax_is_c_file(buffer->filename)) {
		return;
	}

	/* Must be warm/hot to highlight */
	if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
		return;
	}

	/* Reset all cells to normal */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		line->cells[i].syntax = SYNTAX_NORMAL;
	}

	/* Mark line as hot since we're modifying syntax */
	line_set_temperature(line, LINE_TEMPERATURE_HOT);

	/* Check if we start inside a block comment */
	bool in_block_comment = syntax_is_in_block_comment(buffer, row, 0);
	bool in_string = false;
	bool in_char = false;

	for (uint32_t i = 0; i < line->cell_count; ) {
		uint32_t cp = line->cells[i].codepoint;

		/* Inside block comment - highlight until end */
		if (in_block_comment) {
			line->cells[i].syntax = SYNTAX_COMMENT;
			if (cp == '*' && i + 1 < line->cell_count &&
			    line->cells[i + 1].codepoint == '/') {
				line->cells[i + 1].syntax = SYNTAX_COMMENT;
				in_block_comment = false;
				i += 2;
				continue;
			}
			i++;
			continue;
		}

		/* Inside string literal */
		if (in_string) {
			line->cells[i].syntax = SYNTAX_STRING;
			if (cp == '\\' && i + 1 < line->cell_count) {
				/* Escape sequence */
				line->cells[i + 1].syntax = SYNTAX_STRING;
				i += 2;
				continue;
			}
			if (cp == '"') {
				in_string = false;
			}
			i++;
			continue;
		}

		/* Inside char literal */
		if (in_char) {
			line->cells[i].syntax = SYNTAX_STRING;
			if (cp == '\\' && i + 1 < line->cell_count) {
				line->cells[i + 1].syntax = SYNTAX_STRING;
				i += 2;
				continue;
			}
			if (cp == '\'') {
				in_char = false;
			}
			i++;
			continue;
		}

		/* Check for line comment */
		if (cp == '/' && i + 1 < line->cell_count &&
		    line->cells[i + 1].codepoint == '/') {
			/* Rest of line is comment */
			for (uint32_t j = i; j < line->cell_count; j++) {
				line->cells[j].syntax = SYNTAX_COMMENT;
			}
			break;
		}

		/* Check for block comment start */
		if (cp == '/' && i + 1 < line->cell_count &&
		    line->cells[i + 1].codepoint == '*') {
			line->cells[i].syntax = SYNTAX_COMMENT;
			line->cells[i + 1].syntax = SYNTAX_COMMENT;
			in_block_comment = true;
			i += 2;
			continue;
		}

		/* Check for string literal start */
		if (cp == '"') {
			line->cells[i].syntax = SYNTAX_STRING;
			in_string = true;
			i++;
			continue;
		}

		/* Check for char literal start */
		if (cp == '\'') {
			line->cells[i].syntax = SYNTAX_STRING;
			in_char = true;
			i++;
			continue;
		}

		/* Check for preprocessor directive */
		if (cp == '#' && syntax_is_line_start(line, i)) {
			for (uint32_t j = i; j < line->cell_count; j++) {
				line->cells[j].syntax = SYNTAX_PREPROCESSOR;
			}
			break;
		}

		/* Check for number */
		if (syntax_is_digit(cp) ||
		    (cp == '.' && i + 1 < line->cell_count &&
		     syntax_is_digit(line->cells[i + 1].codepoint))) {
			uint32_t start = i;
			while (i < line->cell_count &&
			       syntax_is_number_char(line->cells[i].codepoint)) {
				i++;
			}
			for (uint32_t j = start; j < i; j++) {
				line->cells[j].syntax = SYNTAX_NUMBER;
			}
			continue;
		}

		/* Check for identifier (keyword, type, or function) */
		if (syntax_is_alpha(cp) || cp == '_') {
			uint32_t start = i;
			while (i < line->cell_count &&
			       syntax_is_alnum(line->cells[i].codepoint)) {
				i++;
			}

			/* Extract word and classify */
			char word[64];
			syntax_extract_word(line, start, i, word, sizeof(word));

			enum syntax_token type = SYNTAX_NORMAL;
			if (syntax_is_keyword(word)) {
				type = SYNTAX_KEYWORD;
			} else if (syntax_is_type(word)) {
				type = SYNTAX_TYPE;
			} else {
				/* Check if followed by '(' - it's a function */
				uint32_t j = i;
				while (j < line->cell_count &&
				       (line->cells[j].codepoint == ' ' ||
				        line->cells[j].codepoint == '\t')) {
					j++;
				}
				if (j < line->cell_count &&
				    line->cells[j].codepoint == '(') {
					type = SYNTAX_FUNCTION;
				}
			}

			for (uint32_t j = start; j < i; j++) {
				line->cells[j].syntax = type;
			}
			continue;
		}

		/* Check for operator */
		if (syntax_is_operator(cp)) {
			line->cells[i].syntax = SYNTAX_OPERATOR;
			i++;
			continue;
		}

		/* Check for bracket */
		if (syntax_is_bracket(cp)) {
			line->cells[i].syntax = SYNTAX_BRACKET;
			i++;
			continue;
		}

		/* Default: skip */
		i++;
	}
}

/*****************************************************************************
 * Markdown Syntax Highlighting
 *****************************************************************************/

/*
 * Check if a row starts inside a fenced code block.
 * Scans backwards from the given row to find unclosed ``` or ~~~.
 */
bool syntax_is_in_code_block(struct buffer *buffer, uint32_t row)
{
	bool in_code_block = false;
	uint32_t fence_char = 0;
	uint32_t fence_length = 0;

	for (uint32_t r = 0; r < row; r++) {
		struct line *line = &buffer->lines[r];

		/* Ensure line is warm */
		if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
			line_warm(line, buffer);
		}

		if (line->cell_count == 0) {
			continue;
		}

		/* Skip up to 3 leading spaces */
		uint32_t col = 0;
		uint32_t indent = 0;
		while (col < line->cell_count && indent < 3 &&
		       line->cells[col].codepoint == ' ') {
			col++;
			indent++;
		}

		if (col >= line->cell_count) {
			continue;
		}

		uint32_t ch = line->cells[col].codepoint;

		/* Check for fence character */
		if (ch == '`' || ch == '~') {
			uint32_t count = 0;
			while (col < line->cell_count &&
			       line->cells[col].codepoint == ch) {
				count++;
				col++;
			}

			if (count >= 3) {
				if (!in_code_block) {
					/* Opening fence */
					in_code_block = true;
					fence_char = ch;
					fence_length = count;
				} else if (ch == fence_char && count >= fence_length) {
					/* Check if valid closing fence (only whitespace after) */
					bool valid_close = true;
					while (col < line->cell_count) {
						uint32_t c = line->cells[col].codepoint;
						if (c != ' ' && c != '\t') {
							valid_close = false;
							break;
						}
						col++;
					}
					if (valid_close) {
						in_code_block = false;
					}
				}
			}
		}
	}

	return in_code_block;
}

/*
 * Mark a range of cells with a syntax token.
 */
static void md_mark_range(struct line *line, uint32_t start, uint32_t end,
                          enum syntax_token syntax)
{
	for (uint32_t i = start; i < end && i < line->cell_count; i++) {
		line->cells[i].syntax = syntax;
	}
}

/*
 * Mark from position to end of line with a syntax token.
 */
static void md_mark_to_end(struct line *line, uint32_t start,
                           enum syntax_token syntax)
{
	md_mark_range(line, start, line->cell_count, syntax);
}

/*
 * Check if character is escapable in markdown.
 */
static bool md_is_escapable(uint32_t cp)
{
	return cp == '\\' || cp == '`' || cp == '*' || cp == '_' ||
	       cp == '{' || cp == '}' || cp == '[' || cp == ']' ||
	       cp == '(' || cp == ')' || cp == '#' || cp == '+' ||
	       cp == '-' || cp == '.' || cp == '!' || cp == '|';
}

/*
 * Check if line is a table separator (|---|:---:|---:|).
 * Returns true if line contains only |, -, :, and spaces with at least one -.
 */
bool md_is_table_separator(struct line *line)
{
	if (line == NULL || line->cell_count == 0) {
		return false;
	}

	/* Must start with | */
	if (line->cells[0].codepoint != '|') {
		return false;
	}

	bool has_dash = false;
	for (uint32_t i = 0; i < line->cell_count; i++) {
		uint32_t c = line->cells[i].codepoint;
		if (c == '-') {
			has_dash = true;
		} else if (c != '|' && c != ':' && c != ' ') {
			return false;
		}
	}
	return has_dash;
}

/*****************************************************************************
 * Table Auto-Formatting
 *****************************************************************************/

/*
 * Check if a line is part of a table (starts with |).
 */
static bool table_is_table_line(struct line *line)
{
	if (!line || line->cell_count == 0)
		return false;
	return line->cells[0].codepoint == '|';
}

/*
 * Detect table bounds starting from a given row.
 * Returns true if row is in a table, sets start_row, end_row, separator_row.
 */
bool table_detect_bounds(struct buffer *buffer, uint32_t row,
			 uint32_t *start_row, uint32_t *end_row,
			 uint32_t *separator_row)
{
	if (row >= buffer->line_count)
		return false;

	struct line *line = &buffer->lines[row];
	line_warm(line, buffer);

	if (!table_is_table_line(line))
		return false;

	/* Find start of table (walk up) */
	uint32_t start = row;
	while (start > 0) {
		struct line *prev = &buffer->lines[start - 1];
		line_warm(prev, buffer);
		if (!table_is_table_line(prev))
			break;
		start--;
	}

	/* Find end of table (walk down) */
	uint32_t end = row;
	while (end + 1 < buffer->line_count) {
		struct line *next = &buffer->lines[end + 1];
		line_warm(next, buffer);
		if (!table_is_table_line(next))
			break;
		end++;
	}

	/* Find separator row (should be second row typically) */
	uint32_t sep = UINT32_MAX;
	for (uint32_t r = start; r <= end; r++) {
		struct line *l = &buffer->lines[r];
		if (md_is_table_separator(l)) {
			sep = r;
			break;
		}
	}

	/* Must have a separator row to be a valid table */
	if (sep == UINT32_MAX)
		return false;

	*start_row = start;
	*end_row = end;
	*separator_row = sep;
	return true;
}

/*
 * Count pipe characters in a line (for column counting).
 */
static uint16_t table_count_pipes(struct line *line)
{
	uint16_t count = 0;
	for (uint32_t i = 0; i < line->cell_count; i++) {
		if (line->cells[i].codepoint == '|')
			count++;
	}
	return count;
}

/*
 * Parse alignment from separator row for a specific column.
 * column_index is 0-based.
 */
static enum table_alignment table_parse_alignment(struct line *line, uint16_t column_index)
{
	uint16_t current_col = 0;
	uint32_t cell_start = 0;
	bool in_cell = false;

	for (uint32_t i = 0; i < line->cell_count; i++) {
		if (line->cells[i].codepoint == '|') {
			if (in_cell && current_col == column_index) {
				/* Found the column, check alignment */
				bool left_colon = false;
				bool right_colon = false;

				/* Check first non-space char after cell_start */
				for (uint32_t j = cell_start; j < i; j++) {
					uint32_t c = line->cells[j].codepoint;
					if (c == ':') {
						left_colon = true;
						break;
					} else if (c != ' ') {
						break;
					}
				}

				/* Check last non-space char before i */
				for (uint32_t j = i; j > cell_start; j--) {
					uint32_t c = line->cells[j - 1].codepoint;
					if (c == ':') {
						right_colon = true;
						break;
					} else if (c != ' ') {
						break;
					}
				}

				if (left_colon && right_colon)
					return TABLE_ALIGN_CENTER;
				else if (right_colon)
					return TABLE_ALIGN_RIGHT;
				else if (left_colon)
					return TABLE_ALIGN_LEFT;
				else
					return TABLE_ALIGN_DEFAULT;
			}
			if (in_cell)
				current_col++;
			cell_start = i + 1;
			in_cell = true;
		}
	}

	return TABLE_ALIGN_DEFAULT;
}

/*
 * Parse table structure and allocate table_info.
 * Caller must free the returned struct and columns array.
 */
struct table_info *table_parse(struct buffer *buffer, uint32_t start_row,
			       uint32_t end_row, uint32_t separator_row)
{
	struct table_info *info = calloc(1, sizeof(struct table_info));
	if (!info)
		return NULL;

	info->start_row = start_row;
	info->end_row = end_row;
	info->separator_row = separator_row;

	/* Count columns from separator row */
	struct line *sep_line = &buffer->lines[separator_row];
	line_warm(sep_line, buffer);
	uint16_t pipes = table_count_pipes(sep_line);
	info->column_count = (pipes > 1) ? pipes - 1 : 0;

	if (info->column_count == 0) {
		free(info);
		return NULL;
	}

	/* Allocate columns */
	info->columns = calloc(info->column_count, sizeof(struct table_column));
	if (!info->columns) {
		free(info);
		return NULL;
	}

	/* Parse alignment for each column */
	for (uint16_t col = 0; col < info->column_count; col++) {
		info->columns[col].align = table_parse_alignment(sep_line, col);
		info->columns[col].width = 0;
	}

	return info;
}

/*
 * Free a table_info struct.
 */
void table_info_free(struct table_info *info)
{
	if (info) {
		free(info->columns);
		free(info);
	}
}

/*
 * Extract cell content from a line at given column index.
 * Returns allocated string (caller must free) and sets content_width.
 * content_width is the display width (grapheme-aware).
 */
static uint32_t *table_extract_cell(struct line *line, uint16_t column_index,
				    uint32_t *content_length, uint32_t *content_width)
{
	uint16_t current_col = 0;
	uint32_t cell_start = 0;
	uint32_t cell_end = 0;
	bool found = false;

	/* Find the cell boundaries */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		if (line->cells[i].codepoint == '|') {
			if (current_col == column_index + 1) {
				cell_end = i;
				found = true;
				break;
			}
			current_col++;
			cell_start = i + 1;
		}
	}

	if (!found) {
		*content_length = 0;
		*content_width = 0;
		return NULL;
	}

	/* Trim ALL leading/trailing spaces to get pure content */
	while (cell_start < cell_end && line->cells[cell_start].codepoint == ' ')
		cell_start++;
	while (cell_end > cell_start && line->cells[cell_end - 1].codepoint == ' ')
		cell_end--;

	uint32_t len = cell_end - cell_start;
	if (len == 0) {
		*content_length = 0;
		*content_width = 0;
		return NULL;
	}

	/* Allocate and copy codepoints */
	uint32_t *content = malloc(len * sizeof(uint32_t));
	if (!content) {
		*content_length = 0;
		*content_width = 0;
		return NULL;
	}

	uint32_t width = 0;
	for (uint32_t i = 0; i < len; i++) {
		content[i] = line->cells[cell_start + i].codepoint;
		/* Simple width calculation - ASCII is 1, wide chars are 2 */
		uint32_t cp = content[i];
		if (cp < 0x80) {
			width += 1;
		} else if ((cp >= 0x1100 && cp <= 0x115F) ||  /* Hangul Jamo */
			   (cp >= 0x2E80 && cp <= 0x9FFF) ||  /* CJK */
			   (cp >= 0xF900 && cp <= 0xFAFF) ||  /* CJK Compat */
			   (cp >= 0xFE10 && cp <= 0xFE1F) ||  /* Vertical forms */
			   (cp >= 0xFE30 && cp <= 0xFE6F) ||  /* CJK Compat Forms */
			   (cp >= 0xFF00 && cp <= 0xFF60) ||  /* Fullwidth */
			   (cp >= 0xFFE0 && cp <= 0xFFE6) ||  /* Fullwidth symbols */
			   (cp >= 0x20000 && cp <= 0x2FFFF)) { /* CJK Ext */
			width += 2;
		} else {
			width += 1;
		}
	}

	*content_length = len;
	*content_width = width;
	return content;
}

/*
 * Calculate maximum column widths across all rows in the table.
 */
void table_calculate_widths(struct buffer *buffer, struct table_info *info)
{
	/* Reset widths */
	for (uint16_t col = 0; col < info->column_count; col++) {
		info->columns[col].width = 0;
	}

	/* Scan all rows except separator */
	for (uint32_t row = info->start_row; row <= info->end_row; row++) {
		if (row == info->separator_row)
			continue;

		struct line *line = &buffer->lines[row];
		line_warm(line, buffer);

		for (uint16_t col = 0; col < info->column_count; col++) {
			uint32_t len, width;
			uint32_t *content = table_extract_cell(line, col, &len, &width);
			if (content) {
				if (width > info->columns[col].width)
					info->columns[col].width = width;
				free(content);
			}
		}
	}

	/* Minimum width of 1 for empty columns */
	for (uint16_t col = 0; col < info->column_count; col++) {
		if (info->columns[col].width == 0)
			info->columns[col].width = 1;
	}
}

/*
 * Format a content row with proper alignment and spacing.
 * Rebuilds the line's cells array.
 */
static void table_format_content_row(struct line *line, struct buffer *buffer,
				     uint32_t row, struct table_info *info)
{
	/* Extract all cell contents first */
	uint32_t **contents = calloc(info->column_count, sizeof(uint32_t *));
	uint32_t *lengths = calloc(info->column_count, sizeof(uint32_t));
	uint32_t *widths = calloc(info->column_count, sizeof(uint32_t));

	if (!contents || !lengths || !widths) {
		free(contents);
		free(lengths);
		free(widths);
		return;
	}

	for (uint16_t col = 0; col < info->column_count; col++) {
		contents[col] = table_extract_cell(line, col, &lengths[col], &widths[col]);
	}

	/* Calculate total cells needed: | + (space + content + padding + space) * cols + | */
	uint32_t total_cells = 1;  /* Leading | */
	for (uint16_t col = 0; col < info->column_count; col++) {
		total_cells += 1;  /* Leading space */
		total_cells += info->columns[col].width;  /* Content + padding */
		total_cells += 1;  /* Trailing space */
		total_cells += 1;  /* | */
	}

	/* Resize cells array */
	struct cell *new_cells = calloc(total_cells, sizeof(struct cell));
	if (!new_cells) {
		for (uint16_t col = 0; col < info->column_count; col++)
			free(contents[col]);
		free(contents);
		free(lengths);
		free(widths);
		return;
	}

	/* Build the new row */
	uint32_t pos = 0;

	/* Leading | */
	new_cells[pos++].codepoint = '|';

	for (uint16_t col = 0; col < info->column_count; col++) {
		uint32_t col_width = info->columns[col].width;
		uint32_t content_width = widths[col];
		uint32_t padding = col_width - content_width;

		/* Leading space */
		new_cells[pos++].codepoint = ' ';

		/* Calculate left/right padding based on alignment */
		uint32_t left_pad = 0, right_pad = 0;
		switch (info->columns[col].align) {
		case TABLE_ALIGN_DEFAULT:
		case TABLE_ALIGN_LEFT:
			right_pad = padding;
			break;
		case TABLE_ALIGN_RIGHT:
			left_pad = padding;
			break;
		case TABLE_ALIGN_CENTER:
			left_pad = padding / 2;
			right_pad = padding - left_pad;
			break;
		}

		/* Left padding */
		for (uint32_t i = 0; i < left_pad; i++)
			new_cells[pos++].codepoint = ' ';

		/* Content */
		for (uint32_t i = 0; i < lengths[col]; i++)
			new_cells[pos++].codepoint = contents[col][i];

		/* Right padding */
		for (uint32_t i = 0; i < right_pad; i++)
			new_cells[pos++].codepoint = ' ';

		/* Trailing space */
		new_cells[pos++].codepoint = ' ';

		/* | */
		new_cells[pos++].codepoint = '|';
	}

	/* Free old cells and install new ones */
	free(line->cells);
	line->cells = new_cells;
	line->cell_count = pos;
	line->cell_capacity = total_cells;

	/* Mark line as modified */
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);

	/* Cleanup */
	for (uint16_t col = 0; col < info->column_count; col++)
		free(contents[col]);
	free(contents);
	free(lengths);
	free(widths);
}

/*
 * Format the separator row with proper column widths.
 */
static void table_format_separator_row(struct line *line, struct buffer *buffer,
				       uint32_t row, struct table_info *info)
{
	/* Calculate total cells: | + (:-...-:) * cols + | */
	uint32_t total_cells = 1;  /* Leading | */
	for (uint16_t col = 0; col < info->column_count; col++) {
		/* Each column: optional :, dashes (width), optional :, | */
		total_cells += info->columns[col].width + 2;  /* dashes + 2 for possible colons */
		total_cells += 1;  /* | */
	}

	struct cell *new_cells = calloc(total_cells, sizeof(struct cell));
	if (!new_cells)
		return;

	uint32_t pos = 0;

	/* Leading | */
	new_cells[pos++].codepoint = '|';

	for (uint16_t col = 0; col < info->column_count; col++) {
		enum table_alignment align = info->columns[col].align;
		uint32_t width = info->columns[col].width;

		/* Left colon for center or left-explicit */
		if (align == TABLE_ALIGN_CENTER || align == TABLE_ALIGN_LEFT) {
			new_cells[pos++].codepoint = ':';
		}

		/* Dashes - adjust count based on colons */
		uint32_t dash_count = width;
		if (align == TABLE_ALIGN_CENTER)
			dash_count = width;  /* Both colons are extra */
		else if (align == TABLE_ALIGN_LEFT || align == TABLE_ALIGN_RIGHT)
			dash_count = width + 1;  /* One colon, add a dash */
		else  /* TABLE_ALIGN_DEFAULT */
			dash_count = width + 2;  /* No colons, add two dashes */

		for (uint32_t i = 0; i < dash_count; i++)
			new_cells[pos++].codepoint = '-';

		/* Right colon for center or right */
		if (align == TABLE_ALIGN_CENTER || align == TABLE_ALIGN_RIGHT) {
			new_cells[pos++].codepoint = ':';
		}

		/* | */
		new_cells[pos++].codepoint = '|';
	}

	/* Free old cells and install new ones */
	free(line->cells);
	line->cells = new_cells;
	line->cell_count = pos;
	line->cell_capacity = total_cells;

	/* Mark line as modified */
	line_set_temperature(line, LINE_TEMPERATURE_HOT);
	neighbor_compute_line(line);
	syntax_highlight_line(line, buffer, row);
	line_invalidate_wrap_cache(line);
}

/*
 * Reformat an entire table starting from any row within it.
 * Returns the cursor column adjustment needed.
 */
int32_t table_reformat(struct buffer *buffer, uint32_t row)
{
	uint32_t start_row, end_row, separator_row;

	if (!table_detect_bounds(buffer, row, &start_row, &end_row, &separator_row))
		return 0;

	struct table_info *info = table_parse(buffer, start_row, end_row, separator_row);
	if (!info)
		return 0;

	table_calculate_widths(buffer, info);

	/* Format all rows */
	for (uint32_t r = start_row; r <= end_row; r++) {
		struct line *line = &buffer->lines[r];
		line_warm(line, buffer);

		if (r == separator_row) {
			table_format_separator_row(line, buffer, r, info);
		} else {
			table_format_content_row(line, buffer, r, info);
		}
	}

	table_info_free(info);
	return 0;  /* TODO: Calculate cursor adjustment */
}

/*
 * Parse inline code span starting at pos. Returns end position.
 * Handles both single ` and multiple `` backticks.
 */
static uint32_t md_parse_code_span(struct line *line, uint32_t pos)
{
	if (pos >= line->cell_count || line->cells[pos].codepoint != '`') {
		return pos;
	}

	uint32_t start = pos;

	/* Count opening backticks */
	uint32_t open_count = 0;
	while (pos < line->cell_count && line->cells[pos].codepoint == '`') {
		open_count++;
		pos++;
	}

	/* Search for matching closing backticks */
	while (pos < line->cell_count) {
		if (line->cells[pos].codepoint == '`') {
			uint32_t close_count = 0;
			while (pos < line->cell_count &&
			       line->cells[pos].codepoint == '`') {
				close_count++;
				pos++;
			}

			if (close_count == open_count) {
				/* Found matching close */
				md_mark_range(line, start, pos, SYNTAX_MD_CODE_SPAN);
				return pos;
			}
			/* Wrong count, continue searching */
		} else {
			pos++;
		}
	}

	/* No closing found, just advance past first backtick */
	return start + 1;
}

/*
 * Parse emphasis starting at pos. Returns end position.
 * Handles *, **, ***, _, __, ___, ~~.
 */
static uint32_t md_parse_emphasis(struct line *line, uint32_t pos)
{
	if (pos >= line->cell_count) {
		return pos;
	}

	uint32_t delim = line->cells[pos].codepoint;
	if (delim != '*' && delim != '_' && delim != '~') {
		return pos;
	}

	uint32_t start = pos;

	/* Count delimiter run */
	uint32_t open_count = 0;
	while (pos < line->cell_count && line->cells[pos].codepoint == delim) {
		open_count++;
		pos++;
	}
	/* Strikethrough requires exactly 2 tildes */
	if (delim == '~' && open_count < 2) {
		return start + 1;
	}

	/* Must be followed by non-space (left-flanking check) */
	if (pos >= line->cell_count || line->cells[pos].codepoint == ' ') {
		return start + 1;
	}

	/* Search for closing delimiter run */
	while (pos < line->cell_count) {
		if (line->cells[pos].codepoint == delim) {
			uint32_t close_start = pos;
			uint32_t close_count = 0;
			while (pos < line->cell_count &&
			       line->cells[pos].codepoint == delim) {
				close_count++;
				pos++;
			}

			/* Check if preceded by non-space (right-flanking) */
			bool right_flanking = close_start > 0 &&
			                      line->cells[close_start - 1].codepoint != ' ';

			if (right_flanking && close_count >= open_count) {
				/* Determine emphasis type */
				enum syntax_token syntax;
				uint32_t match_count;

				if (delim == '~') {
					/* Strikethrough: ~~ */
					syntax = SYNTAX_MD_STRIKETHROUGH;
					match_count = 2;
				} else if (open_count >= 3 && close_count >= 3) {
					syntax = SYNTAX_MD_BOLD_ITALIC;
					match_count = 3;
				} else if (open_count >= 2 && close_count >= 2) {
					syntax = SYNTAX_MD_BOLD;
					match_count = 2;
				} else {
					syntax = SYNTAX_MD_ITALIC;
					match_count = 1;
				}

				uint32_t mark_end = close_start + match_count;
				md_mark_range(line, start, mark_end, syntax);
				return mark_end;
			}
		} else {
			pos++;
		}
	}

	/* No closing found */
	return start + 1;
}

/*
 * Parse link starting at pos. Returns end position.
 * Handles [text](url) format.
 */
static uint32_t md_parse_link(struct line *line, uint32_t pos, bool is_image)
{
	uint32_t start = pos;

	/* For images, skip the ! */
	if (is_image) {
		if (pos >= line->cell_count || line->cells[pos].codepoint != '!') {
			return pos;
		}
		pos++;
	}

	/* Must start with [ */
	if (pos >= line->cell_count || line->cells[pos].codepoint != '[') {
		return start;
	}
	uint32_t bracket_start = pos;
	pos++;

	/* Find closing ] */
	uint32_t depth = 1;
	while (pos < line->cell_count && depth > 0) {
		uint32_t cp = line->cells[pos].codepoint;
		if (cp == '[') depth++;
		else if (cp == ']') depth--;
		pos++;
	}

	if (depth != 0) {
		return start + 1;
	}

	uint32_t bracket_end = pos - 1;

	/* Must be followed by ( */
	if (pos >= line->cell_count || line->cells[pos].codepoint != '(') {
		return start + 1;
	}
	uint32_t url_start = pos;
	pos++;

	/* Find closing ) */
	depth = 1;
	while (pos < line->cell_count && depth > 0) {
		uint32_t cp = line->cells[pos].codepoint;
		if (cp == '(') depth++;
		else if (cp == ')') depth--;
		pos++;
	}

	if (depth != 0) {
		return start + 1;
	}

	uint32_t url_end = pos;

	/* Mark the image ! if present */
	if (is_image) {
		line->cells[start].syntax = SYNTAX_MD_IMAGE;
	}

	/* Mark link text portion [text] */
	md_mark_range(line, bracket_start, bracket_end + 1, SYNTAX_MD_LINK_TEXT);

	/* Mark URL portion (url) */
	md_mark_range(line, url_start, url_end, SYNTAX_MD_LINK_URL);

	return url_end;
}

/*
 * Parse inline elements in a line segment.
 */
static void md_parse_inline(struct line *line, uint32_t start, uint32_t end)
{
	uint32_t pos = start;

	while (pos < end && pos < line->cell_count) {
		/* Skip if already marked */
		if (line->cells[pos].syntax != SYNTAX_NORMAL) {
			pos++;
			continue;
		}

		uint32_t cp = line->cells[pos].codepoint;

		/* Escape sequence */
		if (cp == '\\' && pos + 1 < line->cell_count) {
			uint32_t next = line->cells[pos + 1].codepoint;
			if (md_is_escapable(next)) {
				line->cells[pos].syntax = SYNTAX_MD_ESCAPE;
				line->cells[pos + 1].syntax = SYNTAX_MD_ESCAPE;
				pos += 2;
				continue;
			}
		}

		/* Code span */
		if (cp == '`') {
			uint32_t new_pos = md_parse_code_span(line, pos);
			if (new_pos > pos) {
				pos = new_pos;
				continue;
			}
		}

		/* Image */
		if (cp == '!' && pos + 1 < line->cell_count &&
		    line->cells[pos + 1].codepoint == '[') {
			uint32_t new_pos = md_parse_link(line, pos, true);
			if (new_pos > pos + 1) {
				pos = new_pos;
				continue;
			}
		}

		/* Link */
		if (cp == '[') {
			uint32_t new_pos = md_parse_link(line, pos, false);
			if (new_pos > pos + 1) {
				pos = new_pos;
				continue;
			}
		}

		/* Emphasis and strikethrough */
		if (cp == '*' || cp == '_' || cp == '~') {
			uint32_t new_pos = md_parse_emphasis(line, pos);
			if (new_pos > pos + 1) {
				pos = new_pos;
				continue;
			}
		}

		pos++;
	}
}

/*
 * Highlight a Markdown line.
 */
void syntax_highlight_markdown_line(struct line *line, struct buffer *buffer,
                                    uint32_t row)
{
	/* Must be warm/hot to highlight */
	if (line_get_temperature(line) == LINE_TEMPERATURE_COLD) {
		return;
	}

	if (line->cell_count == 0) {
		return;
	}

	/* Reset all cells to normal */
	for (uint32_t i = 0; i < line->cell_count; i++) {
		line->cells[i].syntax = SYNTAX_NORMAL;
	}

	/* Mark line as hot */
	line_set_temperature(line, LINE_TEMPERATURE_HOT);

	/* Check if inside fenced code block */
	if (syntax_is_in_code_block(buffer, row)) {
		/* Check if this is a closing fence */
		uint32_t col = 0;
		uint32_t indent = 0;
		while (col < line->cell_count && indent < 3 &&
		       line->cells[col].codepoint == ' ') {
			col++;
			indent++;
		}

		if (col < line->cell_count) {
			uint32_t ch = line->cells[col].codepoint;
			if (ch == '`' || ch == '~') {
				uint32_t count = 0;
				while (col + count < line->cell_count &&
				       line->cells[col + count].codepoint == ch) {
					count++;
				}
				if (count >= 3) {
					/* This is a closing fence */
					md_mark_to_end(line, 0, SYNTAX_MD_CODE_FENCE_CLOSE);
					if (editor.hybrid_mode) {
						md_compute_elements(line);
						md_mark_hideable_cells(line);
					}
					return;
				}
			}
		}

		/* Regular code block content */
		md_mark_to_end(line, 0, SYNTAX_MD_CODE_BLOCK);
		return;
	}

	/* Block-level parsing */
	uint32_t pos = 0;
	uint32_t inline_start = 0;

	/* Skip leading whitespace (up to 3 spaces) */
	uint32_t indent = 0;
	while (pos < line->cell_count && indent < 3 &&
	       line->cells[pos].codepoint == ' ') {
		pos++;
		indent++;
	}

	/* 4+ spaces = indented code block */
	if (indent >= 4 || (pos < line->cell_count &&
	                    line->cells[pos].codepoint == '\t')) {
		md_mark_to_end(line, 0, SYNTAX_MD_CODE_BLOCK);
		return;
	}

	if (pos >= line->cell_count) {
		return;
	}

	uint32_t cp = line->cells[pos].codepoint;

	/* Fenced code block start (opening fence) */
	if (cp == '`' || cp == '~') {
		uint32_t count = 0;
		uint32_t start = pos;
		while (pos < line->cell_count && line->cells[pos].codepoint == cp) {
			count++;
			pos++;
		}
		if (count >= 3) {
			md_mark_to_end(line, 0, SYNTAX_MD_CODE_FENCE_OPEN);
			if (editor.hybrid_mode) {
				md_compute_elements(line);
				md_mark_hideable_cells(line);
			}
			return;
		}
		pos = start;
	}

	/* ATX Header - count level and assign appropriate token */
	if (cp == '#') {
		uint32_t level = 0;
		uint32_t start = pos;
		while (pos < line->cell_count && line->cells[pos].codepoint == '#' &&
		       level < 6) {
			level++;
			pos++;
		}
		/* Must be followed by space or end of line */
		if (level > 0 && (pos >= line->cell_count ||
		                  line->cells[pos].codepoint == ' ')) {
			enum syntax_token header_tokens[] = {
				SYNTAX_MD_HEADER_1, SYNTAX_MD_HEADER_2,
				SYNTAX_MD_HEADER_3, SYNTAX_MD_HEADER_4,
				SYNTAX_MD_HEADER_5, SYNTAX_MD_HEADER_6
			};
			md_mark_to_end(line, 0, header_tokens[level - 1]);
			if (editor.hybrid_mode) {
				md_compute_elements(line);
				md_mark_hideable_cells(line);
			}
			return;
		}
		pos = start;
	}

	/* Blockquote (supports nesting: > > > text) */
	if (cp == '>') {
		uint32_t start = pos;
		/* Parse all > markers with optional spaces between them */
		while (pos < line->cell_count) {
			if (line->cells[pos].codepoint == '>') {
				pos++;
				/* Optional space after > */
				if (pos < line->cell_count && line->cells[pos].codepoint == ' ') {
					pos++;
				}
			} else {
				break;
			}
		}
		md_mark_range(line, start, pos, SYNTAX_MD_BLOCKQUOTE);
		inline_start = pos;
		md_parse_inline(line, inline_start, line->cell_count);
		if (editor.hybrid_mode) {
			md_compute_elements(line);
			md_mark_hideable_cells(line);
		}
		return;
	}

	/* Horizontal rule: check for --- or *** or ___ */
	if (cp == '-' || cp == '*' || cp == '_') {
		uint32_t rule_char = cp;
		uint32_t count = 0;
		uint32_t check_pos = pos;
		bool is_rule = true;

		while (check_pos < line->cell_count) {
			uint32_t c = line->cells[check_pos].codepoint;
			if (c == rule_char) {
				count++;
			} else if (c != ' ') {
				is_rule = false;
				break;
			}
			check_pos++;
		}

		if (is_rule && count >= 3) {
			md_mark_to_end(line, 0, SYNTAX_MD_HORIZONTAL_RULE);
			if (editor.hybrid_mode) {
				md_compute_elements(line);
				md_mark_hideable_cells(line);
			}
			return;
		}
	}

	/* Unordered list marker: - * + followed by space */
	if ((cp == '-' || cp == '*' || cp == '+') &&
	    pos + 1 < line->cell_count &&
	    line->cells[pos + 1].codepoint == ' ') {
		uint32_t marker_end = pos + 2;
		md_mark_range(line, pos, marker_end, SYNTAX_MD_LIST_MARKER);

		/* Check for task marker [ ] or [x] */
		if (marker_end + 2 < line->cell_count &&
		    line->cells[marker_end].codepoint == '[') {
			uint32_t inner = line->cells[marker_end + 1].codepoint;
			if ((inner == ' ' || inner == 'x' || inner == 'X') &&
			    line->cells[marker_end + 2].codepoint == ']') {
				uint32_t task_end = marker_end + 3;
				/* Optional space after ] */
				if (task_end < line->cell_count &&
				    line->cells[task_end].codepoint == ' ') {
					task_end++;
				}
				md_mark_range(line, marker_end, task_end,
				              SYNTAX_MD_TASK_MARKER);
				inline_start = task_end;
				md_parse_inline(line, inline_start, line->cell_count);
				if (editor.hybrid_mode) {
					md_compute_elements(line);
					md_mark_hideable_cells(line);
				}
				return;
			}
		}

		inline_start = marker_end;
		md_parse_inline(line, inline_start, line->cell_count);
		if (editor.hybrid_mode) {
			md_compute_elements(line);
			md_mark_hideable_cells(line);
		}
		return;
	}

	/* Ordered list marker: digits followed by . or ) then space */
	if (cp >= '0' && cp <= '9') {
		uint32_t num_start = pos;
		while (pos < line->cell_count &&
		       line->cells[pos].codepoint >= '0' &&
		       line->cells[pos].codepoint <= '9') {
			pos++;
		}
		if (pos < line->cell_count &&
		    (line->cells[pos].codepoint == '.' ||
		     line->cells[pos].codepoint == ')')) {
			pos++;
			if (pos < line->cell_count &&
			    line->cells[pos].codepoint == ' ') {
				pos++;
				md_mark_range(line, num_start, pos, SYNTAX_MD_LIST_MARKER);
				inline_start = pos;
				md_parse_inline(line, inline_start, line->cell_count);
				if (editor.hybrid_mode) {
					md_compute_elements(line);
					md_mark_hideable_cells(line);
				}
				return;
			}
		}
		pos = num_start;
	}

	/* Table line: starts with | */
	if (cp == '|') {
		/* Check if this is a separator line (|---|:---:|---:|) */
		if (md_is_table_separator(line)) {
			md_mark_to_end(line, 0, SYNTAX_MD_TABLE_SEPARATOR);
			return;
		}

		/* Check if next line is separator (making this a header row) */
		bool is_header = false;
		if (row + 1 < buffer->line_count) {
			struct line *next_line = &buffer->lines[row + 1];
			/* Only check if next line is warm/hot (has cell data) */
			if (line_get_temperature(next_line) != LINE_TEMPERATURE_COLD) {
				is_header = md_is_table_separator(next_line);
			}
		}

		if (is_header) {
			/* Mark header row: pipes as table, content as header */
			for (uint32_t i = 0; i < line->cell_count; i++) {
				if (line->cells[i].codepoint == '|') {
					line->cells[i].syntax = SYNTAX_MD_TABLE;
				} else {
					line->cells[i].syntax = SYNTAX_MD_TABLE_HEADER;
				}
			}
		} else {
			/* Regular table row: mark all | characters */
			for (uint32_t i = 0; i < line->cell_count; i++) {
				if (line->cells[i].codepoint == '|') {
					line->cells[i].syntax = SYNTAX_MD_TABLE;
				}
			}
			/* Parse inline content between pipes */
			md_parse_inline(line, 0, line->cell_count);
		}
		if (editor.hybrid_mode) {
			md_compute_elements(line);
			md_mark_hideable_cells(line);
		}
		return;
	}

	/* Default: parse inline elements */
	md_parse_inline(line, 0, line->cell_count);

	/* Compute element cache for hybrid rendering mode */
	if (editor.hybrid_mode) {
		md_compute_elements(line);
		md_mark_hideable_cells(line);
	}
}

/*****************************************************************************
 * Hybrid Markdown Rendering - Element Detection
 *****************************************************************************/

/*
 * Free the markdown element cache for a line.
 */
void md_element_cache_free(struct line *line)
{
	if (line->md_elements) {
		free(line->md_elements->elements);
		free(line->md_elements);
		line->md_elements = NULL;
	}
}

/*
 * Invalidate the markdown element cache for a line.
 */
void md_element_cache_invalidate(struct line *line)
{
	if (line->md_elements) {
		line->md_elements->valid = false;
	}
}

/*
 * Add an element to the cache, growing if necessary.
 */
static void md_element_cache_add(struct md_element_cache *cache,
				 uint32_t start, uint32_t end, uint16_t syntax)
{
	if (cache->count >= cache->capacity) {
		uint16_t new_capacity = cache->capacity == 0 ? 8 : cache->capacity * 2;
		struct md_element *new_elements = realloc(cache->elements,
			new_capacity * sizeof(struct md_element));
		if (!new_elements)
			return;
		cache->elements = new_elements;
		cache->capacity = new_capacity;
	}
	cache->elements[cache->count].start_col = start;
	cache->elements[cache->count].end_col = end;
	cache->elements[cache->count].syntax_type = syntax;
	cache->count++;
}

/*
 * Check if a syntax token is a markdown element that should be tracked.
 */
static bool md_is_tracked_element(uint16_t syntax)
{
	switch (syntax) {
	case SYNTAX_MD_BOLD:
	case SYNTAX_MD_ITALIC:
	case SYNTAX_MD_BOLD_ITALIC:
	case SYNTAX_MD_STRIKETHROUGH:
	case SYNTAX_MD_CODE_SPAN:
	case SYNTAX_MD_CODE_FENCE_OPEN:
	case SYNTAX_MD_CODE_FENCE_CLOSE:
	case SYNTAX_MD_LINK_TEXT:
	case SYNTAX_MD_LINK_URL:
	case SYNTAX_MD_IMAGE:
	case SYNTAX_MD_HEADER_1:
	case SYNTAX_MD_HEADER_2:
	case SYNTAX_MD_HEADER_3:
	case SYNTAX_MD_HEADER_4:
	case SYNTAX_MD_HEADER_5:
	case SYNTAX_MD_HEADER_6:
	case SYNTAX_MD_HORIZONTAL_RULE:
		return true;
	default:
		return false;
	}
}

/*
 * Compute markdown element spans from syntax tokens.
 * This identifies contiguous runs of the same syntax type.
 */
void md_compute_elements(struct line *line)
{
	if (!line->md_elements) {
		line->md_elements = calloc(1, sizeof(struct md_element_cache));
		if (!line->md_elements)
			return;
	}

	/* Reset but keep allocation */
	line->md_elements->count = 0;
	line->md_elements->valid = true;

	if (line->cell_count == 0)
		return;

	uint32_t start = 0;
	uint16_t current_syntax = line->cells[0].syntax;
	bool tracking = md_is_tracked_element(current_syntax);

	for (uint32_t i = 1; i <= line->cell_count; i++) {
		uint16_t syntax = (i < line->cell_count) ? line->cells[i].syntax : SYNTAX_NORMAL;
		bool is_tracked = md_is_tracked_element(syntax);

		/* Transition: end current element if syntax changes */
		if (syntax != current_syntax || i == line->cell_count) {
			if (tracking) {
				md_element_cache_add(line->md_elements, start, i, current_syntax);
			}
			start = i;
			current_syntax = syntax;
			tracking = is_tracked;
		}
	}
}

/*
 * Check if a character is a delimiter that should be hidden.
 * This identifies the syntax characters like **, *, `, #, etc.
 */
static bool md_is_delimiter_char(uint32_t codepoint, uint16_t syntax)
{
	switch (syntax) {
	case SYNTAX_MD_BOLD:
	case SYNTAX_MD_ITALIC:
	case SYNTAX_MD_BOLD_ITALIC:
		return codepoint == '*' || codepoint == '_';

	case SYNTAX_MD_CODE_SPAN:
		return codepoint == '`';
	case SYNTAX_MD_STRIKETHROUGH:
		return codepoint == '~';
	case SYNTAX_MD_CODE_FENCE_OPEN:
		/* Hide backticks/tildes, language specifier rendered as label */
		return codepoint == '`' || codepoint == '~';
	case SYNTAX_MD_CODE_FENCE_CLOSE:
		/* Hide entire closing fence line */
		return true;

	case SYNTAX_MD_HEADER_1:
	case SYNTAX_MD_HEADER_2:
	case SYNTAX_MD_HEADER_3:
	case SYNTAX_MD_HEADER_4:
	case SYNTAX_MD_HEADER_5:
	case SYNTAX_MD_HEADER_6:
		return codepoint == '#' || codepoint == ' ';

	case SYNTAX_MD_LINK_TEXT:
		return codepoint == '[' || codepoint == ']';

	case SYNTAX_MD_LINK_URL:
		/* Hide entire URL portion including parens */
		return true;

	case SYNTAX_MD_IMAGE:
		return codepoint == '!' || codepoint == '[' || codepoint == ']' ||
		       codepoint == '(' || codepoint == ')';

	default:
		return false;
	}
}

/*
 * Mark cells that should be hidden in hybrid mode.
 * Sets CELL_FLAG_HIDEABLE on delimiter characters.
 */
void md_mark_hideable_cells(struct line *line)
{
	if (!line->md_elements || !line->md_elements->valid)
		return;

	for (uint16_t i = 0; i < line->md_elements->count; i++) {
		struct md_element *elem = &line->md_elements->elements[i];

		for (uint32_t col = elem->start_col; col < elem->end_col && col < line->cell_count; col++) {
			struct cell *cell = &line->cells[col];

			/* Clear flags first */
			cell->flags = 0;

			/* Mark element boundaries */
			if (col == elem->start_col)
				cell->flags |= CELL_FLAG_ELEMENT_START;
			if (col == elem->end_col - 1)
				cell->flags |= CELL_FLAG_ELEMENT_END;

			/* Mark hideable delimiters */
			if (md_is_delimiter_char(cell->codepoint, elem->syntax_type)) {
				cell->flags |= CELL_FLAG_HIDEABLE;
			}
		}
	}

	/* Special handling for headers: mark leading # and space as hideable */
	if (line->cell_count > 0) {
		uint16_t syntax = line->cells[0].syntax;
		if (syntax >= SYNTAX_MD_HEADER_1 && syntax <= SYNTAX_MD_HEADER_6) {
			for (uint32_t col = 0; col < line->cell_count; col++) {
				uint32_t cp = line->cells[col].codepoint;
				if (cp == '#' || (cp == ' ' && col > 0 && line->cells[col-1].codepoint == '#')) {
					line->cells[col].flags |= CELL_FLAG_HIDEABLE;
				} else if (cp != '#') {
					break;
				}
			}
		}
	}
}

/*
 * Find the element containing a given column position.
 * Returns NULL if not in any tracked element.
 */
struct md_element *md_find_element_at(struct line *line, uint32_t column)
{
	if (!line->md_elements || !line->md_elements->valid)
		return NULL;

	for (uint16_t i = 0; i < line->md_elements->count; i++) {
		struct md_element *elem = &line->md_elements->elements[i];
		if (column >= elem->start_col && column <= elem->end_col)
			return elem;
	}
	return NULL;
}

/*
 * Check if cursor is in an element that should be revealed.
 * If so, returns the reveal range in start/end.
 * For links, expands to include both LINK_TEXT and LINK_URL parts.
 */
bool md_should_reveal_element(struct line *line, uint32_t cursor_col,
			      uint32_t *reveal_start, uint32_t *reveal_end)
{
	struct md_element *elem = md_find_element_at(line, cursor_col);
	if (!elem)
		return false;

	*reveal_start = elem->start_col;
	*reveal_end = elem->end_col;

	/* For links, expand to include both text and URL parts */
	if (elem->syntax_type == SYNTAX_MD_LINK_TEXT ||
	    elem->syntax_type == SYNTAX_MD_LINK_URL) {
		/* Search for adjacent link elements to expand range */
		for (uint16_t i = 0; i < line->md_elements->count; i++) {
			struct md_element *other = &line->md_elements->elements[i];
			if (other->syntax_type == SYNTAX_MD_LINK_TEXT ||
			    other->syntax_type == SYNTAX_MD_LINK_URL) {
				/* Check if adjacent (end of one == start of other) */
				if (other->end_col == *reveal_start ||
				    other->start_col == *reveal_end) {
					if (other->start_col < *reveal_start)
						*reveal_start = other->start_col;
					if (other->end_col > *reveal_end)
						*reveal_end = other->end_col;
				}
			}
		}
	}

	return true;
}

/*
 * Extract URL from a link element.
 * If cursor is on a link, extracts the URL portion.
 * Returns true if URL was found and copied to buffer.
 */
bool md_extract_link_url(struct line *line, uint32_t cursor_col,
			 char *url_buffer, size_t buffer_size)
{
	if (!line || buffer_size == 0)
		return false;

	url_buffer[0] = '\0';

	/* Check if cursor is in a link element */
	uint16_t syntax = SYNTAX_NORMAL;
	if (cursor_col < line->cell_count)
		syntax = line->cells[cursor_col].syntax;

	if (syntax != SYNTAX_MD_LINK_TEXT && syntax != SYNTAX_MD_LINK_URL)
		return false;

	/* Find the URL portion (cells with SYNTAX_MD_LINK_URL) */
	uint32_t url_start = UINT32_MAX;
	uint32_t url_end = 0;

	for (uint32_t i = 0; i < line->cell_count; i++) {
		if (line->cells[i].syntax == SYNTAX_MD_LINK_URL) {
			if (url_start == UINT32_MAX)
				url_start = i;
			url_end = i + 1;
		} else if (url_start != UINT32_MAX && i > url_end) {
			/* Moved past URL region */
			break;
		}
	}

	if (url_start == UINT32_MAX)
		return false;

	/* Skip leading ( and trailing ) if present */
	if (url_start < line->cell_count && line->cells[url_start].codepoint == '(')
		url_start++;
	if (url_end > 0 && url_end <= line->cell_count &&
	    line->cells[url_end - 1].codepoint == ')')
		url_end--;

	/* Extract URL characters */
	size_t offset = 0;
	for (uint32_t i = url_start; i < url_end && i < line->cell_count; i++) {
		uint32_t cp = line->cells[i].codepoint;
		/* Simple ASCII for URLs - encode as UTF-8 */
		char utf8[4];
		int len = 0;
		if (cp < 0x80) {
			utf8[0] = (char)cp;
			len = 1;
		} else if (cp < 0x800) {
			utf8[0] = (char)(0xC0 | (cp >> 6));
			utf8[1] = (char)(0x80 | (cp & 0x3F));
			len = 2;
		} else if (cp < 0x10000) {
			utf8[0] = (char)(0xE0 | (cp >> 12));
			utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[2] = (char)(0x80 | (cp & 0x3F));
			len = 3;
		} else {
			utf8[0] = (char)(0xF0 | (cp >> 18));
			utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
			utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[3] = (char)(0x80 | (cp & 0x3F));
			len = 4;
		}

		if (offset + len < buffer_size - 1) {
			for (int j = 0; j < len; j++)
				url_buffer[offset++] = utf8[j];
		} else {
			break;
		}
	}
	url_buffer[offset] = '\0';

	return offset > 0;
}

/*
 * Update link preview state based on cursor position.
 * Called from cursor movement handlers.
 */
void md_update_link_preview(void)
{
	editor.link_preview_active = false;
	editor.link_url_preview[0] = '\0';

	if (!editor.hybrid_mode)
		return;

	if (!syntax_is_markdown_file(editor.buffer.filename))
		return;

	if (editor.cursor_row >= editor.buffer.line_count)
		return;

	struct line *line = &editor.buffer.lines[editor.cursor_row];
	line_warm(line, &editor.buffer);

	if (md_extract_link_url(line, editor.cursor_column,
	                        editor.link_url_preview,
	                        sizeof(editor.link_url_preview))) {
		editor.link_preview_active = true;
	}
}

/*
 * Check if a column is inside a task checkbox marker.
 * Returns true if the column is on [, ], space, x, or X of a task marker.
 * Sets checkbox_col to the column of the '[' character.
 */
bool md_is_task_checkbox(struct line *line, uint32_t column, uint32_t *checkbox_col)
{
	if (!line || column >= line->cell_count)
		return false;

	/* Check if cell has task marker syntax */
	if (line->cells[column].syntax != SYNTAX_MD_TASK_MARKER)
		return false;

	/* Scan backwards to find the '[' character */
	uint32_t bracket_col = column;
	while (bracket_col > 0 &&
	       line->cells[bracket_col].syntax == SYNTAX_MD_TASK_MARKER &&
	       line->cells[bracket_col].codepoint != '[') {
		bracket_col--;
	}

	/* Verify we found the opening bracket */
	if (line->cells[bracket_col].codepoint != '[' ||
	    line->cells[bracket_col].syntax != SYNTAX_MD_TASK_MARKER)
		return false;

	if (checkbox_col)
		*checkbox_col = bracket_col;

	return true;
}
