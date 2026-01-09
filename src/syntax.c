/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2024-2025 Edward Edmonds
 */

/*
 * syntax.c - Syntax highlighting and neighbor layer implementation
 */

#define _GNU_SOURCE

#include "edit.h"
#include "syntax.h"
#include "buffer.h"

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
static bool md_is_table_separator(struct line *line)
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
 * Handles *, **, ***, _, __, ___.
 */
static uint32_t md_parse_emphasis(struct line *line, uint32_t pos)
{
	if (pos >= line->cell_count) {
		return pos;
	}

	uint32_t delim = line->cells[pos].codepoint;
	if (delim != '*' && delim != '_') {
		return pos;
	}

	uint32_t start = pos;

	/* Count delimiter run */
	uint32_t open_count = 0;
	while (pos < line->cell_count && line->cells[pos].codepoint == delim) {
		open_count++;
		pos++;
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

				if (open_count >= 3 && close_count >= 3) {
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

		/* Emphasis */
		if (cp == '*' || cp == '_') {
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
					/* This is a fence line */
					md_mark_to_end(line, 0, SYNTAX_MD_CODE_BLOCK);
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

	/* Fenced code block start */
	if (cp == '`' || cp == '~') {
		uint32_t count = 0;
		uint32_t start = pos;
		while (pos < line->cell_count && line->cells[pos].codepoint == cp) {
			count++;
			pos++;
		}
		if (count >= 3) {
			md_mark_to_end(line, 0, SYNTAX_MD_CODE_BLOCK);
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
			return;
		}
		pos = start;
	}

	/* Blockquote */
	if (cp == '>') {
		uint32_t start = pos;
		pos++;
		/* Optional space after > */
		if (pos < line->cell_count && line->cells[pos].codepoint == ' ') {
			pos++;
		}
		md_mark_range(line, start, pos, SYNTAX_MD_BLOCKQUOTE);
		inline_start = pos;
		md_parse_inline(line, inline_start, line->cell_count);
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
				return;
			}
		}

		inline_start = marker_end;
		md_parse_inline(line, inline_start, line->cell_count);
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
		return;
	}

	/* Default: parse inline elements */
	md_parse_inline(line, 0, line->cell_count);
}
