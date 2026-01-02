/*
 * syntax.h - Syntax highlighting for edit
 *
 * Provides language detection, syntax highlighting,
 * and the neighbor layer for bracket matching.
 */

#ifndef EDIT_SYNTAX_H
#define EDIT_SYNTAX_H

#include "types.h"

/*****************************************************************************
 * Neighbor Layer - Character Classification and Token Boundaries
 *****************************************************************************/

/*
 * Extract character class from neighbor field.
 */
static inline enum character_class neighbor_get_class(uint8_t neighbor)
{
	return (neighbor & NEIGHBOR_CLASS_MASK) >> NEIGHBOR_CLASS_SHIFT;
}

/*
 * Extract token position from neighbor field.
 */
static inline enum token_position neighbor_get_position(uint8_t neighbor)
{
	return (neighbor & NEIGHBOR_POSITION_MASK) >> NEIGHBOR_POSITION_SHIFT;
}

/*
 * Encode character class and token position into neighbor field.
 */
static inline uint8_t neighbor_encode(enum character_class class, enum token_position position)
{
	return (class << NEIGHBOR_CLASS_SHIFT) | (position << NEIGHBOR_POSITION_SHIFT);
}

/*
 * Compute neighbor layer for a line.
 * Sets character class and token position data.
 */
void neighbor_compute_line(struct line *line);

/*
 * Classify a codepoint into a character class.
 */
enum character_class classify_codepoint(uint32_t cp);

/*
 * Check if two character classes form a word together.
 */
bool classes_form_word(enum character_class a, enum character_class b);

/*
 * Check if cell is at word start.
 */
bool cell_is_word_start(struct cell *cell);

/*
 * Check if cell is at word end.
 */
bool cell_is_word_end(struct cell *cell);

/*
 * Check if position is trailing whitespace.
 */
bool is_trailing_whitespace(struct line *line, uint32_t column);

/*
 * Find start of previous word.
 */
uint32_t find_prev_word_start(struct line *line, uint32_t column);

/*
 * Find start of next word.
 */
uint32_t find_next_word_start(struct line *line, uint32_t column);

/*****************************************************************************
 * Pair Entanglement - Bracket and Comment Matching
 *****************************************************************************/

/*
 * Extract pair ID from context field.
 */
static inline uint32_t context_get_pair_id(uint32_t context)
{
	return context & CONTEXT_PAIR_ID_MASK;
}

/*
 * Extract pair type from context field.
 */
static inline enum pair_type context_get_pair_type(uint32_t context)
{
	return (context & CONTEXT_PAIR_TYPE_MASK) >> CONTEXT_PAIR_TYPE_SHIFT;
}

/*
 * Extract pair role from context field.
 */
static inline enum pair_role context_get_pair_role(uint32_t context)
{
	return (context & CONTEXT_PAIR_ROLE_MASK) >> CONTEXT_PAIR_ROLE_SHIFT;
}

/*
 * Encode pair ID, type, and role into context field.
 */
static inline uint32_t context_encode(uint32_t pair_id, enum pair_type type, enum pair_role role)
{
	return (pair_id & CONTEXT_PAIR_ID_MASK) |
	       ((uint32_t)type << CONTEXT_PAIR_TYPE_SHIFT) |
	       ((uint32_t)role << CONTEXT_PAIR_ROLE_SHIFT);
}

/*
 * Allocate a unique pair ID from buffer.
 */
uint32_t buffer_allocate_pair_id(struct buffer *buffer);

/*
 * Compute pair matching for entire buffer.
 */
void buffer_compute_pairs(struct buffer *buffer);

/*
 * Find matching bracket/pair partner.
 * Returns true if match found.
 */
bool buffer_find_pair_partner(struct buffer *buffer,
                              uint32_t row, uint32_t col,
                              uint32_t *match_row, uint32_t *match_col);

/*****************************************************************************
 * Syntax Highlighting
 *****************************************************************************/

/*
 * Check if filename is a C/C++ file.
 */
bool syntax_is_c_file(const char *filename);

/*
 * Check if codepoint is alphanumeric or underscore.
 */
bool syntax_is_alnum(uint32_t cp);

/*
 * Highlight a single line.
 * Sets the syntax field of each cell.
 */
void syntax_highlight_line(struct line *line, struct buffer *buffer, uint32_t row);

#endif /* EDIT_SYNTAX_H */
