/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * theme.c - Theme and color system implementation
 */
#define _GNU_SOURCE

#include "edit.h"
#include "theme.h"

extern struct editor_state editor;

/*****************************************************************************
 * Global State
 *****************************************************************************/

/* Array of loaded themes */
struct theme *loaded_themes = NULL;
int theme_count = 0;
int current_theme_index = 0;

/* Active theme - this is what rendering uses */
struct theme active_theme;

/*****************************************************************************
 * WCAG Color Contrast Utilities
 *****************************************************************************/

/*
 * Linearize an sRGB component (0-255) for luminance calculation.
 * Applies inverse gamma correction per sRGB specification.
 */
static double color_linearize(uint8_t value)
{
	double srgb = value / 255.0;
	if (srgb <= 0.03928) {
		return srgb / 12.92;
	}
	return pow((srgb + 0.055) / 1.055, 2.4);
}

/*
 * Calculate relative luminance of an RGB color per WCAG 2.1.
 * Returns a value between 0.0 (black) and 1.0 (white).
 */
double color_luminance(struct syntax_color color)
{
	double r = color_linearize(color.red);
	double g = color_linearize(color.green);
	double b = color_linearize(color.blue);
	return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/*
 * Calculate contrast ratio between two colors per WCAG 2.1.
 * Returns a value >= 1.0, where 1.0 means identical colors
 * and 21.0 is the maximum (black on white).
 */
double color_contrast_ratio(struct syntax_color fg, struct syntax_color bg)
{
	double lum_fg = color_luminance(fg);
	double lum_bg = color_luminance(bg);

	double lighter = lum_fg > lum_bg ? lum_fg : lum_bg;
	double darker = lum_fg > lum_bg ? lum_bg : lum_fg;

	return (lighter + 0.05) / (darker + 0.05);
}

/*
 * Adjust a single color channel toward a target to improve contrast.
 * Returns the new channel value.
 */
static uint8_t color_adjust_channel(uint8_t value, bool make_lighter)
{
	if (make_lighter) {
		/* Move toward 255 */
		int new_val = value + (255 - value) / 2;
		if (new_val > 255) new_val = 255;
		if (new_val == value && value < 255) new_val = value + 1;
		return (uint8_t)new_val;
	} else {
		/* Move toward 0 */
		int new_val = value / 2;
		if (new_val == value && value > 0) new_val = value - 1;
		return (uint8_t)new_val;
	}
}

/*
 * Get a WCAG-compliant foreground color for the given background.
 * If the original foreground has sufficient contrast, returns it unchanged.
 * Otherwise, adjusts the foreground (lighter or darker) to meet WCAG AA.
 */
struct syntax_color color_ensure_contrast(struct syntax_color fg,
                                          struct syntax_color bg)
{
	double ratio = color_contrast_ratio(fg, bg);

	/* Already compliant */
	if (ratio >= WCAG_MIN_CONTRAST) {
		return fg;
	}

	/* Determine whether to lighten or darken the foreground.
	 * Choose the direction that increases contrast with the background. */
	double bg_lum = color_luminance(bg);
	/* Dark backgrounds need lighter text, light backgrounds need darker text */
	bool make_lighter = bg_lum < 0.5;

	struct syntax_color adjusted = fg;
	int iterations = 0;
	/* Limit iterations to prevent infinite loops */
	const int max_iterations = MAX_CONTRAST_ITERATIONS;

	while (ratio < WCAG_MIN_CONTRAST && iterations < max_iterations) {
		adjusted.red = color_adjust_channel(adjusted.red, make_lighter);
		adjusted.green = color_adjust_channel(adjusted.green, make_lighter);
		adjusted.blue = color_adjust_channel(adjusted.blue, make_lighter);

		ratio = color_contrast_ratio(adjusted, bg);
		iterations++;
	}

	/* If we couldn't achieve compliance going one direction, try the other */
	if (ratio < WCAG_MIN_CONTRAST) {
		adjusted = fg;
		make_lighter = !make_lighter;
		iterations = 0;

		while (ratio < WCAG_MIN_CONTRAST && iterations < max_iterations) {
			adjusted.red = color_adjust_channel(adjusted.red, make_lighter);
			adjusted.green = color_adjust_channel(adjusted.green, make_lighter);
			adjusted.blue = color_adjust_channel(adjusted.blue, make_lighter);

			ratio = color_contrast_ratio(adjusted, bg);
			iterations++;
		}
	}

	/* Last resort: use pure black or white */
	if (ratio < WCAG_MIN_CONTRAST) {
		if (bg_lum < 0.5) {
			/* White */
			adjusted = (struct syntax_color){0xff, 0xff, 0xff};
		} else {
			/* Black */
			adjusted = (struct syntax_color){0x00, 0x00, 0x00};
		}
	}

	return adjusted;
}

/*
 * Parse a hex color string (e.g., "FF79C6" or "#ff79c6") into RGB.
 * Returns true on success, false on invalid input.
 */
bool color_parse_hex(const char *hex, struct syntax_color *out)
{
	if (hex == NULL || out == NULL) {
		return false;
	}

	/* Skip optional # prefix */
	if (hex[0] == '#') {
		hex++;
	}

	/* Must be exactly 6 hex digits */
	if (strlen(hex) != HEX_COLOR_LENGTH) {
		return false;
	}

	/* Validate all characters are hex */
	for (int i = 0; i < HEX_COLOR_LENGTH; i++) {
		if (!isxdigit((unsigned char)hex[i])) {
			return false;
		}
	}

	/* Parse RGB components */
	unsigned int r, g, b;
	if (sscanf(hex, "%2x%2x%2x", &r, &g, &b) != 3) {
		return false;
	}

	out->red = (uint8_t)r;
	out->green = (uint8_t)g;
	out->blue = (uint8_t)b;

	return true;
}

/*****************************************************************************
 * Attribute Parsing and Rendering
 *****************************************************************************/

/*
 * Parse attribute string like "bold+italic+underline" into flags.
 * Attributes are separated by '+'. Whitespace is trimmed.
 * Returns ATTR_NONE if string is "none", empty, or NULL.
 *
 * Valid attribute names:
 *   bold, dim, italic, underline, reverse, strike, curly, overline
 */
static text_attr attr_parse(const char *str)
{
	text_attr attr = ATTR_NONE;
	char buf[64];
	char *token, *saveptr;

	if (str == NULL || *str == '\0') {
		return ATTR_NONE;
	}

	/* Copy to mutable buffer for strtok_r */
	strncpy(buf, str, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	token = strtok_r(buf, "+", &saveptr);
	while (token != NULL) {
		/* Trim leading whitespace */
		while (*token == ' ' || *token == '\t') token++;

		/* Trim trailing whitespace */
		char *end = token + strlen(token) - 1;
		while (end > token && (*end == ' ' || *end == '\t')) {
			*end-- = '\0';
		}

		/* Match attribute names */
		if (strcmp(token, "none") == 0) {
			return ATTR_NONE;
		} else if (strcmp(token, "bold") == 0) {
			attr |= ATTR_BOLD;
		} else if (strcmp(token, "dim") == 0) {
			attr |= ATTR_DIM;
		} else if (strcmp(token, "italic") == 0) {
			attr |= ATTR_ITALIC;
		} else if (strcmp(token, "underline") == 0) {
			attr |= ATTR_UNDERLINE;
		} else if (strcmp(token, "reverse") == 0) {
			attr |= ATTR_REVERSE;
		} else if (strcmp(token, "strike") == 0) {
			attr |= ATTR_STRIKE;
		} else if (strcmp(token, "curly") == 0) {
			attr |= ATTR_CURLY;
		} else if (strcmp(token, "overline") == 0) {
			attr |= ATTR_OVERLINE;
		}
		/* Unknown attributes are silently ignored */

		token = strtok_r(NULL, "+", &saveptr);
	}

	return attr;
}

/*
 * Build escape sequence for text attributes.
 * Returns the number of characters written to buf.
 */
int attr_to_escape(text_attr attr, char *buffer, size_t buffer_size)
{
	if (attr == ATTR_NONE) {
		return 0;
	}

	int length = 0;

	if (attr & ATTR_BOLD) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[1m");
	}
	if (attr & ATTR_DIM) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[2m");
	}
	if (attr & ATTR_ITALIC) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[3m");
	}
	if (attr & ATTR_UNDERLINE) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[4m");
	}
	if (attr & ATTR_REVERSE) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[7m");
	}
	if (attr & ATTR_STRIKE) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[9m");
	}
	if (attr & ATTR_CURLY) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[4:3m");
	}
	if (attr & ATTR_OVERLINE) {
		length += snprintf(buffer + length, buffer_size - length, "\x1b[53m");
	}

	return length;
}

/*
 * Build complete escape sequence for a style (fg, bg, and attributes).
 * Resets attributes first, then applies colors and attributes.
 * Returns the number of characters written to buffer.
 */
int style_to_escape(const struct style *style, char *buffer, size_t buffer_size)
{
	int length = 0;

	/* Reset attributes and set colors */
	length = snprintf(buffer, buffer_size, "\x1b[0;38;2;%d;%d;%d;48;2;%d;%d;%dm",
	               style->fg.red, style->fg.green, style->fg.blue,
	               style->bg.red, style->bg.green, style->bg.blue);

	/* Append text attributes */
	length += attr_to_escape(style->attr, buffer + length, buffer_size - length);

	return length;
}

/*
 * Build escape sequence for a style with custom background override.
 * Useful for cursor line highlighting.
 * Returns the number of characters written to buf.
 */
int style_to_escape_with_bg(const struct style *style,
                            struct syntax_color bg_override,
                            char *buffer, size_t buffer_size)
{
	int length = 0;

	/* Reset attributes and set colors with overridden background */
	length = snprintf(buffer, buffer_size, "\x1b[0;38;2;%d;%d;%d;48;2;%d;%d;%dm",
	               style->fg.red, style->fg.green, style->fg.blue,
	               bg_override.red, bg_override.green, bg_override.blue);

	/* Append text attributes */
	length += attr_to_escape(style->attr, buffer + length, buffer_size - length);

	return length;
}

/*****************************************************************************
 * Theme Creation
 *****************************************************************************/

/*
 * Initialize a theme struct with the default dark theme.
 * This is the built-in fallback when no theme files exist.
 */
struct theme theme_create_default(void)
{
	struct theme t = {0};
	t.name = strdup("Mono Black");

	/* Color-only fields */
	t.background = (struct syntax_color){0x0A, 0x0A, 0x0A};
	t.foreground = (struct syntax_color){0xD0, 0xD0, 0xD0};
	t.selection = (struct syntax_color){0x40, 0x40, 0x40};
	t.search_match = (struct syntax_color){0x60, 0x60, 0x60};
	t.search_current = (struct syntax_color){0x90, 0x90, 0x90};
	t.cursor_line = (struct syntax_color){0x1A, 0x1A, 0x1A};
	t.color_column = (struct syntax_color){0x1A, 0x1A, 0x1A};
	t.color_column_line = (struct syntax_color){0x38, 0x38, 0x38};
	t.trailing_ws = (struct syntax_color){0x4A, 0x30, 0x30};

	/* Line numbers */
	t.line_number = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.line_number_active = (struct style){
		.fg = {0x80, 0x80, 0x80},
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_BOLD
	};

	/* Gutter */
	t.gutter = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.gutter_active = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_NONE
	};

	/* Status bar */
	t.status = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x2A, 0x2A, 0x2A},
		.attr = ATTR_NONE
	};
	t.status_filename = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x2A, 0x2A, 0x2A},
		.attr = ATTR_BOLD
	};
	t.status_modified = (struct style){
		.fg = {0xE0, 0xA0, 0x00},
		.bg = {0x2A, 0x2A, 0x2A},
		.attr = ATTR_BOLD
	};
	t.status_position = (struct style){
		.fg = {0xA0, 0xA0, 0xA0},
		.bg = {0x2A, 0x2A, 0x2A},
		.attr = ATTR_NONE
	};

	/* Message bar */
	t.message = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};

	/* Prompt components */
	t.prompt_label = (struct style){
		.fg = {0xA0, 0xA0, 0xD0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.prompt_input = (struct style){
		.fg = {0xFF, 0xFF, 0xFF},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.prompt_bracket = (struct style){
		.fg = {0x80, 0x80, 0xFF},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.prompt_warning = (struct style){
		.fg = {0xFF, 0xA0, 0x00},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};

	/* Search feedback */
	t.search_options = (struct style){
		.fg = {0x80, 0xC0, 0x80},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.search_nomatch = (struct style){
		.fg = {0xFF, 0x60, 0x60},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_ITALIC
	};
	t.search_error = (struct style){
		.fg = {0xFF, 0x40, 0x40},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};

	/* Whitespace */
	t.whitespace = (struct style){
		.fg = {0x38, 0x38, 0x38},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.whitespace_tab = (struct style){
		.fg = {0x38, 0x38, 0x38},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.whitespace_space = (struct style){
		.fg = {0x38, 0x38, 0x38},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};

	/* Wrap and special lines */
	t.wrap_indicator = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.empty_line = (struct style){
		.fg = {0x38, 0x38, 0x38},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.welcome = (struct style){
		.fg = {0x58, 0x58, 0x58},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM | ATTR_ITALIC
	};

	/* Bracket matching */
	t.bracket_match = (struct style){
		.fg = {0xFF, 0xFF, 0x00},
		.bg = {0x50, 0x50, 0x00},
		.attr = ATTR_BOLD
	};

	/* Multi-cursor */
	t.multicursor = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x40, 0x60, 0x80},
		.attr = ATTR_NONE
	};

	/* Dialog */
	t.dialog = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_NONE
	};
	t.dialog_header = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_BOLD
	};
	t.dialog_footer = (struct style){
		.fg = {0x80, 0x80, 0x80},
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_DIM
	};
	t.dialog_highlight = (struct style){
		.fg = {0xFF, 0xFF, 0xFF},
		.bg = {0x40, 0x40, 0x40},
		.attr = ATTR_BOLD
	};
	t.dialog_directory = (struct style){
		.fg = {0x80, 0xB0, 0xFF},
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_BOLD
	};

	/* Tab bar */
	t.tab_bar = (struct style){
		.fg = {0x80, 0x80, 0x80},
		.bg = {0x16, 0x16, 0x16},
		.attr = ATTR_NONE
	};
	t.tab_active = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x28, 0x28, 0x28},
		.attr = ATTR_NONE
	};
	t.tab_inactive = (struct style){
		.fg = {0x70, 0x70, 0x70},
		.bg = {0x16, 0x16, 0x16},
		.attr = ATTR_NONE
	};
	t.tab_modified = (struct style){
		.fg = {0xFF, 0xA0, 0x50},
		.bg = {0x00, 0x00, 0x00},
		.attr = ATTR_NONE
	};

	/* Syntax highlighting - grayscale with varying intensity */
	t.syntax[SYNTAX_NORMAL] = (struct style){
		.fg = {0xD0, 0xD0, 0xD0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_KEYWORD] = (struct style){
		.fg = {0xFF, 0xFF, 0xFF},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_TYPE] = (struct style){
		.fg = {0xE0, 0xE0, 0xE0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_STRING] = (struct style){
		.fg = {0xA0, 0xA0, 0xA0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_NUMBER] = (struct style){
		.fg = {0xC0, 0xC0, 0xC0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_COMMENT] = (struct style){
		.fg = {0x60, 0x60, 0x60},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_ITALIC
	};
	t.syntax[SYNTAX_PREPROCESSOR] = (struct style){
		.fg = {0x90, 0x90, 0x90},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_FUNCTION] = (struct style){
		.fg = {0xF0, 0xF0, 0xF0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_OPERATOR] = (struct style){
		.fg = {0xB0, 0xB0, 0xB0},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_BRACKET] = (struct style){
		.fg = {0xC8, 0xC8, 0xC8},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_ESCAPE] = (struct style){
		.fg = {0xCC, 0xCC, 0xCC},
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};

	/* Markdown syntax highlighting - color blind friendly palette
	 * Based on Wong's colorblind-safe palette with high luminance contrast.
	 * Uses blue/orange axis (safe for deuteranopia/protanopia) and
	 * varied brightness (safe for tritanopia). Text attributes provide
	 * additional non-color visual cues. */
	t.syntax[SYNTAX_MD_HEADER_1] = (struct style){
		.fg = {0x56, 0xB4, 0xE9},  /* Sky blue - most prominent */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD | ATTR_UNDERLINE
	};
	t.syntax[SYNTAX_MD_HEADER_2] = (struct style){
		.fg = {0x56, 0xB4, 0xE9},  /* Sky blue */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HEADER_3] = (struct style){
		.fg = {0xE6, 0x9F, 0x00},  /* Orange */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HEADER_4] = (struct style){
		.fg = {0xE6, 0x9F, 0x00},  /* Orange - no bold */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_HEADER_5] = (struct style){
		.fg = {0x90, 0x90, 0x90},  /* Gray */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HEADER_6] = (struct style){
		.fg = {0x80, 0x80, 0x80},  /* Darker gray */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_BOLD] = (struct style){
		.fg = {0xE6, 0x9F, 0x00},  /* Orange - contrasts with blue */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_ITALIC] = (struct style){
		.fg = {0xCC, 0x79, 0xA7},  /* Reddish purple - distinct hue */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_ITALIC
	};
	t.syntax[SYNTAX_MD_BOLD_ITALIC] = (struct style){
		.fg = {0xF0, 0xE4, 0x42},  /* Yellow - high luminance */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD | ATTR_ITALIC
	};
	t.syntax[SYNTAX_MD_STRIKETHROUGH] = (struct style){
		.fg = {0x80, 0x80, 0x80},  /* Gray - dimmed appearance */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.syntax[SYNTAX_MD_CODE_SPAN] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green - accessible green */
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_CODE_BLOCK] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green */
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_CODE_FENCE_OPEN] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Same as code block */
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_CODE_FENCE_CLOSE] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Same as code block */
		.bg = {0x1A, 0x1A, 0x1A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_LINK_TEXT] = (struct style){
		.fg = {0x00, 0x72, 0xB2},  /* Blue - standard link color */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD | ATTR_UNDERLINE
	};
	t.syntax[SYNTAX_MD_LINK_URL] = (struct style){
		.fg = {0x56, 0xB4, 0xE9},  /* Sky blue - lighter than link */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.syntax[SYNTAX_MD_IMAGE] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD | ATTR_UNDERLINE
	};
	t.syntax[SYNTAX_MD_BLOCKQUOTE] = (struct style){
		.fg = {0xA0, 0xA0, 0xA0},  /* Gray - neutral */
		.bg = {0x15, 0x15, 0x18},
		.attr = ATTR_ITALIC
	};
	t.syntax[SYNTAX_MD_LIST_MARKER] = (struct style){
		.fg = {0xD5, 0x5E, 0x00},  /* Vermillion - distinct from blue */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HORIZONTAL_RULE] = (struct style){
		.fg = {0xF0, 0xE4, 0x42},  /* Yellow */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_ESCAPE] = (struct style){
		.fg = {0xA0, 0xA0, 0xA0},  /* Gray */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.syntax[SYNTAX_MD_TABLE] = (struct style){
		.fg = {0x56, 0xB4, 0xE9},  /* Sky blue */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_TABLE_SEPARATOR] = (struct style){
		.fg = {0x60, 0x60, 0x60},  /* Dark gray - subtle */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_DIM
	};
	t.syntax[SYNTAX_MD_TABLE_HEADER] = (struct style){
		.fg = {0xE6, 0x9F, 0x00},  /* Orange - matches bold */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_TASK_MARKER] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green */
		.bg = {0x0A, 0x0A, 0x0A},
		.attr = ATTR_BOLD
	};

	/* Syntax backgrounds not explicitly set */
	for (int i = 0; i < SYNTAX_TOKEN_COUNT; i++) {
		t.syntax_bg_set[i] = false;
	}

	return t;
}

/*
 * Initialize a theme struct with the Mono White light theme.
 * Built-in light theme counterpart to Mono Black.
 */
struct theme theme_create_mono_white(void)
{
	struct theme t = {0};
	t.name = strdup("Mono White");

	/* Color-only fields */
	t.background = (struct syntax_color){0xF8, 0xF8, 0xF8};
	t.foreground = (struct syntax_color){0x20, 0x20, 0x20};
	t.selection = (struct syntax_color){0xC8, 0xC8, 0xC8};
	t.search_match = (struct syntax_color){0xA8, 0xA8, 0xA8};
	t.search_current = (struct syntax_color){0x80, 0x80, 0x80};
	t.cursor_line = (struct syntax_color){0xEC, 0xEC, 0xEC};
	t.color_column = (struct syntax_color){0xEC, 0xEC, 0xEC};
	t.color_column_line = (struct syntax_color){0xC0, 0xC0, 0xC0};
	t.trailing_ws = (struct syntax_color){0xD8, 0xC0, 0xC0};

	/* Line numbers */
	t.line_number = (struct style){
		.fg = {0x90, 0x90, 0x90},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.line_number_active = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0xEC, 0xEC, 0xEC},
		.attr = ATTR_BOLD
	};

	/* Gutter */
	t.gutter = (struct style){
		.fg = {0x90, 0x90, 0x90},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.gutter_active = (struct style){
		.fg = {0x90, 0x90, 0x90},
		.bg = {0xEC, 0xEC, 0xEC},
		.attr = ATTR_NONE
	};

	/* Status bar */
	t.status = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xD8, 0xD8, 0xD8},
		.attr = ATTR_NONE
	};
	t.status_filename = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xD8, 0xD8, 0xD8},
		.attr = ATTR_BOLD
	};
	t.status_modified = (struct style){
		.fg = {0xA0, 0x60, 0x00},
		.bg = {0xD8, 0xD8, 0xD8},
		.attr = ATTR_BOLD
	};
	t.status_position = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0xD8, 0xD8, 0xD8},
		.attr = ATTR_NONE
	};

	/* Message bar */
	t.message = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};

	/* Prompt components */
	t.prompt_label = (struct style){
		.fg = {0x40, 0x40, 0x80},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.prompt_input = (struct style){
		.fg = {0x00, 0x00, 0x00},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.prompt_bracket = (struct style){
		.fg = {0x40, 0x40, 0xA0},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.prompt_warning = (struct style){
		.fg = {0xC0, 0x60, 0x00},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};

	/* Search feedback */
	t.search_options = (struct style){
		.fg = {0x40, 0x80, 0x40},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_DIM
	};
	t.search_nomatch = (struct style){
		.fg = {0xC0, 0x30, 0x30},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_ITALIC
	};
	t.search_error = (struct style){
		.fg = {0xC0, 0x20, 0x20},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};

	/* Whitespace */
	t.whitespace = (struct style){
		.fg = {0xC0, 0xC0, 0xC0},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.whitespace_tab = (struct style){
		.fg = {0xC0, 0xC0, 0xC0},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.whitespace_space = (struct style){
		.fg = {0xC0, 0xC0, 0xC0},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};

	/* Wrap and special lines */
	t.wrap_indicator = (struct style){
		.fg = {0x90, 0x90, 0x90},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_DIM
	};
	t.empty_line = (struct style){
		.fg = {0xC0, 0xC0, 0xC0},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_DIM
	};
	t.welcome = (struct style){
		.fg = {0xA0, 0xA0, 0xA0},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_DIM | ATTR_ITALIC
	};

	/* Bracket matching */
	t.bracket_match = (struct style){
		.fg = {0x00, 0x00, 0x00},
		.bg = {0xE0, 0xE0, 0x80},
		.attr = ATTR_BOLD
	};

	/* Multi-cursor */
	t.multicursor = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xA0, 0xC0, 0xE0},
		.attr = ATTR_NONE
	};

	/* Dialog */
	t.dialog = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xEC, 0xEC, 0xEC},
		.attr = ATTR_NONE
	};
	t.dialog_header = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xEC, 0xEC, 0xEC},
		.attr = ATTR_BOLD
	};
	t.dialog_footer = (struct style){
		.fg = {0x60, 0x60, 0x60},
		.bg = {0xEC, 0xEC, 0xEC},
		.attr = ATTR_DIM
	};
	t.dialog_highlight = (struct style){
		.fg = {0x00, 0x00, 0x00},
		.bg = {0xC8, 0xC8, 0xC8},
		.attr = ATTR_BOLD
	};
	t.dialog_directory = (struct style){
		.fg = {0x30, 0x60, 0xA0},
		.bg = {0xEC, 0xEC, 0xEC},
		.attr = ATTR_BOLD
	};

	/* Tab bar */
	t.tab_bar = (struct style){
		.fg = {0x60, 0x60, 0x60},
		.bg = {0xE0, 0xE0, 0xE0},
		.attr = ATTR_NONE
	};
	t.tab_active = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.tab_inactive = (struct style){
		.fg = {0x70, 0x70, 0x70},
		.bg = {0xE0, 0xE0, 0xE0},
		.attr = ATTR_NONE
	};
	t.tab_modified = (struct style){
		.fg = {0xC0, 0x60, 0x00},
		.bg = {0x00, 0x00, 0x00},
		.attr = ATTR_NONE
	};

	/* Syntax highlighting - grayscale with varying intensity */
	t.syntax[SYNTAX_NORMAL] = (struct style){
		.fg = {0x20, 0x20, 0x20},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_KEYWORD] = (struct style){
		.fg = {0x00, 0x00, 0x00},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_TYPE] = (struct style){
		.fg = {0x18, 0x18, 0x18},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_STRING] = (struct style){
		.fg = {0x50, 0x50, 0x50},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_NUMBER] = (struct style){
		.fg = {0x38, 0x38, 0x38},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_COMMENT] = (struct style){
		.fg = {0x78, 0x78, 0x78},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_ITALIC
	};
	t.syntax[SYNTAX_PREPROCESSOR] = (struct style){
		.fg = {0x60, 0x60, 0x60},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_FUNCTION] = (struct style){
		.fg = {0x10, 0x10, 0x10},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_OPERATOR] = (struct style){
		.fg = {0x40, 0x40, 0x40},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_BRACKET] = (struct style){
		.fg = {0x28, 0x28, 0x28},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_ESCAPE] = (struct style){
		.fg = {0x30, 0x30, 0x30},
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};

	/* Markdown syntax highlighting (light theme) - color blind friendly palette
	 * Based on Wong's colorblind-safe palette, darkened for light backgrounds. */
	t.syntax[SYNTAX_MD_HEADER_1] = (struct style){
		.fg = {0x00, 0x72, 0xB2},  /* Dark blue - most prominent */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD | ATTR_UNDERLINE
	};
	t.syntax[SYNTAX_MD_HEADER_2] = (struct style){
		.fg = {0x00, 0x72, 0xB2},  /* Dark blue */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HEADER_3] = (struct style){
		.fg = {0xD5, 0x5E, 0x00},  /* Vermilion */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HEADER_4] = (struct style){
		.fg = {0xD5, 0x5E, 0x00},  /* Vermilion - no bold */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_HEADER_5] = (struct style){
		.fg = {0x60, 0x60, 0x60},  /* Gray */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HEADER_6] = (struct style){
		.fg = {0x70, 0x70, 0x70},  /* Lighter gray */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_BOLD] = (struct style){
		.fg = {0xD5, 0x5E, 0x00},  /* Vermilion - contrasts with blue */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_ITALIC] = (struct style){
		.fg = {0x88, 0x56, 0x78},  /* Muted reddish purple */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_ITALIC
	};
	t.syntax[SYNTAX_MD_BOLD_ITALIC] = (struct style){
		.fg = {0x94, 0x40, 0x60},  /* Darker reddish purple */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD | ATTR_ITALIC
	};
	t.syntax[SYNTAX_MD_STRIKETHROUGH] = (struct style){
		.fg = {0x70, 0x70, 0x70},  /* Gray - dimmed appearance */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_DIM
	};
	t.syntax[SYNTAX_MD_CODE_SPAN] = (struct style){
		.fg = {0x40, 0x40, 0x40},  /* Dark gray */
		.bg = {0xE8, 0xE8, 0xE8},  /* Light gray background */
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_CODE_BLOCK] = (struct style){
		.fg = {0x40, 0x40, 0x40},  /* Dark gray */
		.bg = {0xE8, 0xE8, 0xE8},  /* Light gray background */
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_CODE_FENCE_OPEN] = (struct style){
		.fg = {0x40, 0x40, 0x40},  /* Same as code block */
		.bg = {0xE8, 0xE8, 0xE8},  /* Light gray background */
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_CODE_FENCE_CLOSE] = (struct style){
		.fg = {0x40, 0x40, 0x40},  /* Same as code block */
		.bg = {0xE8, 0xE8, 0xE8},  /* Light gray background */
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_LINK_TEXT] = (struct style){
		.fg = {0x00, 0x72, 0xB2},  /* Dark blue - same as headers */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_UNDERLINE
	};
	t.syntax[SYNTAX_MD_LINK_URL] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green - distinct from link text */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_IMAGE] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_BLOCKQUOTE] = (struct style){
		.fg = {0x60, 0x60, 0x70},  /* Dark gray with slight blue */
		.bg = {0xF0, 0xF0, 0xF4},  /* Very light gray-blue */
		.attr = ATTR_ITALIC
	};
	t.syntax[SYNTAX_MD_LIST_MARKER] = (struct style){
		.fg = {0xD5, 0x5E, 0x00},  /* Vermilion - same as bold */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_HORIZONTAL_RULE] = (struct style){
		.fg = {0x80, 0x80, 0x80},  /* Medium gray */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_ESCAPE] = (struct style){
		.fg = {0x50, 0x50, 0x50},  /* Dark gray */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_TABLE] = (struct style){
		.fg = {0x00, 0x72, 0xB2},  /* Dark blue */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_NONE
	};
	t.syntax[SYNTAX_MD_TABLE_SEPARATOR] = (struct style){
		.fg = {0x90, 0x90, 0x90},  /* Light gray - subtle */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_DIM
	};
	t.syntax[SYNTAX_MD_TABLE_HEADER] = (struct style){
		.fg = {0xD5, 0x5E, 0x00},  /* Vermilion - matches bold */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};
	t.syntax[SYNTAX_MD_TASK_MARKER] = (struct style){
		.fg = {0x00, 0x9E, 0x73},  /* Bluish green */
		.bg = {0xF8, 0xF8, 0xF8},
		.attr = ATTR_BOLD
	};

	/* Syntax backgrounds not explicitly set */
	for (int i = 0; i < SYNTAX_TOKEN_COUNT; i++) {
		t.syntax_bg_set[i] = false;
	}

	return t;
}

/*
 * Free a theme's allocated memory.
 */
void theme_free(struct theme *t)
{
	if (t) {
		free(t->name);
		t->name = NULL;
	}
}

/*****************************************************************************
 * Theme File Parsing
 *****************************************************************************/

/*
 * Parse a theme file in INI format.
 * Format: key=value (one per line), # for comments, blank lines ignored.
 * Returns a newly allocated theme struct, or NULL on error.
 *
 * Supports both legacy color-only keys and new style keys:
 * - Legacy: syntax_comment = #606090 (sets fg only)
 * - New: syntax_comment_fg, syntax_comment_bg, syntax_comment_attr
 */
struct theme *theme_parse_file(const char *filepath)
{
	FILE *file = fopen(filepath, "r");
	if (file == NULL) {
		return NULL;
	}

	struct theme *t = calloc(1, sizeof(struct theme));
	if (t == NULL) {
		fclose(file);
		return NULL;
	}

	/* Start with defaults so missing properties are sensible */
	*t = theme_create_default();
	free(t->name);  /* Will be replaced */
	t->name = NULL;

	char line[256];
	while (fgets(line, sizeof(line), file)) {
		/* Skip comments and empty lines */
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
			continue;
		}

		/* Find = separator */
		char *eq = strchr(line, '=');
		if (eq == NULL) {
			continue;
		}

		/* Split into key and value */
		*eq = '\0';
		char *key = line;
		char *value = eq + 1;

		/* Trim trailing whitespace from key */
		char *key_end = key + strlen(key) - 1;
		while (key_end > key && isspace((unsigned char)*key_end)) {
			*key_end-- = '\0';
		}

		/* Trim leading whitespace from value */
		while (isspace((unsigned char)*value)) {
			value++;
		}

		/* Trim trailing whitespace/newline from value */
		size_t value_len = strlen(value);
		while (value_len > 0 && (value[value_len - 1] == '\n' ||
		       value[value_len - 1] == '\r' ||
		       isspace((unsigned char)value[value_len - 1]))) {
			value[--value_len] = '\0';
		}

		/* Parse the key-value pair */
		struct syntax_color color;

		if (strcmp(key, "name") == 0) {
			free(t->name);
			t->name = strdup(value);
		}
		/* Color-only fields */
		else if (strcmp(key, "background") == 0 && color_parse_hex(value, &color)) {
			t->background = color;
		}
		else if (strcmp(key, "foreground") == 0 && color_parse_hex(value, &color)) {
			t->foreground = color;
		}
		else if (strcmp(key, "selection") == 0 && color_parse_hex(value, &color)) {
			t->selection = color;
		}
		else if (strcmp(key, "search_match") == 0 && color_parse_hex(value, &color)) {
			t->search_match = color;
		}
		else if (strcmp(key, "search_current") == 0 && color_parse_hex(value, &color)) {
			t->search_current = color;
		}
		else if (strcmp(key, "cursor_line") == 0 && color_parse_hex(value, &color)) {
			t->cursor_line = color;
		}
		else if (strcmp(key, "color_column") == 0 && color_parse_hex(value, &color)) {
			t->color_column = color;
		}
		else if (strcmp(key, "color_column_line") == 0 && color_parse_hex(value, &color)) {
			t->color_column_line = color;
		}
		else if (strcmp(key, "trailing_ws") == 0 && color_parse_hex(value, &color)) {
			t->trailing_ws = color;
		}

		/* Line numbers - style fields */
		else if (strcmp(key, "line_number") == 0 && color_parse_hex(value, &color)) {
			t->line_number.fg = color;  /* Legacy: set fg only */
		}
		else if (strcmp(key, "line_number_fg") == 0 && color_parse_hex(value, &color)) {
			t->line_number.fg = color;
		}
		else if (strcmp(key, "line_number_bg") == 0 && color_parse_hex(value, &color)) {
			t->line_number.bg = color;
		}
		else if (strcmp(key, "line_number_attr") == 0) {
			t->line_number.attr = attr_parse(value);
		}
		else if (strcmp(key, "line_number_active") == 0 && color_parse_hex(value, &color)) {
			t->line_number_active.fg = color;  /* Legacy */
		}
		else if (strcmp(key, "line_number_active_fg") == 0 && color_parse_hex(value, &color)) {
			t->line_number_active.fg = color;
		}
		else if (strcmp(key, "line_number_active_bg") == 0 && color_parse_hex(value, &color)) {
			t->line_number_active.bg = color;
		}
		else if (strcmp(key, "line_number_active_attr") == 0) {
			t->line_number_active.attr = attr_parse(value);
		}

		/* Gutter */
		else if (strcmp(key, "gutter_fg") == 0 && color_parse_hex(value, &color)) {
			t->gutter.fg = color;
		}
		else if (strcmp(key, "gutter_bg") == 0 && color_parse_hex(value, &color)) {
			t->gutter.bg = color;
		}
		else if (strcmp(key, "gutter_attr") == 0) {
			t->gutter.attr = attr_parse(value);
		}
		else if (strcmp(key, "gutter_active_fg") == 0 && color_parse_hex(value, &color)) {
			t->gutter_active.fg = color;
		}
		else if (strcmp(key, "gutter_active_bg") == 0 && color_parse_hex(value, &color)) {
			t->gutter_active.bg = color;
		}
		else if (strcmp(key, "gutter_active_attr") == 0) {
			t->gutter_active.attr = attr_parse(value);
		}

		/* Status bar - legacy and new */
		else if (strcmp(key, "status_bg") == 0 && color_parse_hex(value, &color)) {
			t->status.bg = color;
		}
		else if (strcmp(key, "status_fg") == 0 && color_parse_hex(value, &color)) {
			t->status.fg = color;
		}
		else if (strcmp(key, "status_attr") == 0) {
			t->status.attr = attr_parse(value);
		}
		else if (strcmp(key, "status_filename_fg") == 0 && color_parse_hex(value, &color)) {
			t->status_filename.fg = color;
		}
		else if (strcmp(key, "status_filename_bg") == 0 && color_parse_hex(value, &color)) {
			t->status_filename.bg = color;
		}
		else if (strcmp(key, "status_filename_attr") == 0) {
			t->status_filename.attr = attr_parse(value);
		}
		else if (strcmp(key, "status_modified_fg") == 0 && color_parse_hex(value, &color)) {
			t->status_modified.fg = color;
		}
		else if (strcmp(key, "status_modified_bg") == 0 && color_parse_hex(value, &color)) {
			t->status_modified.bg = color;
		}
		else if (strcmp(key, "status_modified_attr") == 0) {
			t->status_modified.attr = attr_parse(value);
		}
		else if (strcmp(key, "status_position_fg") == 0 && color_parse_hex(value, &color)) {
			t->status_position.fg = color;
		}
		else if (strcmp(key, "status_position_bg") == 0 && color_parse_hex(value, &color)) {
			t->status_position.bg = color;
		}
		else if (strcmp(key, "status_position_attr") == 0) {
			t->status_position.attr = attr_parse(value);
		}

		/* Message bar */
		else if (strcmp(key, "message_bg") == 0 && color_parse_hex(value, &color)) {
			t->message.bg = color;
		}
		else if (strcmp(key, "message_fg") == 0 && color_parse_hex(value, &color)) {
			t->message.fg = color;
		}
		else if (strcmp(key, "message") == 0 && color_parse_hex(value, &color)) {
			t->message.fg = color;  /* Legacy */
		}
		else if (strcmp(key, "message_attr") == 0) {
			t->message.attr = attr_parse(value);
		}

		/* Prompt components */
		else if (strcmp(key, "prompt_label_fg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_label.fg = color;
		}
		else if (strcmp(key, "prompt_label_bg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_label.bg = color;
		}
		else if (strcmp(key, "prompt_label_attr") == 0) {
			t->prompt_label.attr = attr_parse(value);
		}
		else if (strcmp(key, "prompt_input_fg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_input.fg = color;
		}
		else if (strcmp(key, "prompt_input_bg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_input.bg = color;
		}
		else if (strcmp(key, "prompt_input_attr") == 0) {
			t->prompt_input.attr = attr_parse(value);
		}
		else if (strcmp(key, "prompt_bracket_fg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_bracket.fg = color;
		}
		else if (strcmp(key, "prompt_bracket_bg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_bracket.bg = color;
		}
		else if (strcmp(key, "prompt_bracket_attr") == 0) {
			t->prompt_bracket.attr = attr_parse(value);
		}
		else if (strcmp(key, "prompt_warning_fg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_warning.fg = color;
		}
		else if (strcmp(key, "prompt_warning_bg") == 0 && color_parse_hex(value, &color)) {
			t->prompt_warning.bg = color;
		}
		else if (strcmp(key, "prompt_warning_attr") == 0) {
			t->prompt_warning.attr = attr_parse(value);
		}

		/* Search feedback */
		else if (strcmp(key, "search_options_fg") == 0 && color_parse_hex(value, &color)) {
			t->search_options.fg = color;
		}
		else if (strcmp(key, "search_options_bg") == 0 && color_parse_hex(value, &color)) {
			t->search_options.bg = color;
		}
		else if (strcmp(key, "search_options_attr") == 0) {
			t->search_options.attr = attr_parse(value);
		}
		else if (strcmp(key, "search_nomatch_fg") == 0 && color_parse_hex(value, &color)) {
			t->search_nomatch.fg = color;
		}
		else if (strcmp(key, "search_nomatch_bg") == 0 && color_parse_hex(value, &color)) {
			t->search_nomatch.bg = color;
		}
		else if (strcmp(key, "search_nomatch_attr") == 0) {
			t->search_nomatch.attr = attr_parse(value);
		}
		else if (strcmp(key, "search_error_fg") == 0 && color_parse_hex(value, &color)) {
			t->search_error.fg = color;
		}
		else if (strcmp(key, "search_error_bg") == 0 && color_parse_hex(value, &color)) {
			t->search_error.bg = color;
		}
		else if (strcmp(key, "search_error_attr") == 0) {
			t->search_error.attr = attr_parse(value);
		}

		/* Whitespace */
		else if (strcmp(key, "whitespace") == 0 && color_parse_hex(value, &color)) {
			t->whitespace.fg = color;  /* Legacy */
		}
		else if (strcmp(key, "whitespace_fg") == 0 && color_parse_hex(value, &color)) {
			t->whitespace.fg = color;
		}
		else if (strcmp(key, "whitespace_bg") == 0 && color_parse_hex(value, &color)) {
			t->whitespace.bg = color;
		}
		else if (strcmp(key, "whitespace_attr") == 0) {
			t->whitespace.attr = attr_parse(value);
		}
		else if (strcmp(key, "whitespace_tab_fg") == 0 && color_parse_hex(value, &color)) {
			t->whitespace_tab.fg = color;
		}
		else if (strcmp(key, "whitespace_tab_bg") == 0 && color_parse_hex(value, &color)) {
			t->whitespace_tab.bg = color;
		}
		else if (strcmp(key, "whitespace_tab_attr") == 0) {
			t->whitespace_tab.attr = attr_parse(value);
		}
		else if (strcmp(key, "whitespace_space_fg") == 0 && color_parse_hex(value, &color)) {
			t->whitespace_space.fg = color;
		}
		else if (strcmp(key, "whitespace_space_bg") == 0 && color_parse_hex(value, &color)) {
			t->whitespace_space.bg = color;
		}
		else if (strcmp(key, "whitespace_space_attr") == 0) {
			t->whitespace_space.attr = attr_parse(value);
		}

		/* Wrap indicator */
		else if (strcmp(key, "wrap_indicator_fg") == 0 && color_parse_hex(value, &color)) {
			t->wrap_indicator.fg = color;
		}
		else if (strcmp(key, "wrap_indicator_bg") == 0 && color_parse_hex(value, &color)) {
			t->wrap_indicator.bg = color;
		}
		else if (strcmp(key, "wrap_indicator_attr") == 0) {
			t->wrap_indicator.attr = attr_parse(value);
		}

		/* Empty line */
		else if (strcmp(key, "empty_line_fg") == 0 && color_parse_hex(value, &color)) {
			t->empty_line.fg = color;
		}
		else if (strcmp(key, "empty_line_bg") == 0 && color_parse_hex(value, &color)) {
			t->empty_line.bg = color;
		}
		else if (strcmp(key, "empty_line_attr") == 0) {
			t->empty_line.attr = attr_parse(value);
		}

		/* Welcome */
		else if (strcmp(key, "welcome_fg") == 0 && color_parse_hex(value, &color)) {
			t->welcome.fg = color;
		}
		else if (strcmp(key, "welcome_bg") == 0 && color_parse_hex(value, &color)) {
			t->welcome.bg = color;
		}
		else if (strcmp(key, "welcome_attr") == 0) {
			t->welcome.attr = attr_parse(value);
		}

		/* Bracket match */
		else if (strcmp(key, "bracket_match_fg") == 0 && color_parse_hex(value, &color)) {
			t->bracket_match.fg = color;
		}
		else if (strcmp(key, "bracket_match_bg") == 0 && color_parse_hex(value, &color)) {
			t->bracket_match.bg = color;
		}
		else if (strcmp(key, "bracket_match_attr") == 0) {
			t->bracket_match.attr = attr_parse(value);
		}

		/* Multi-cursor */
		else if (strcmp(key, "multicursor_fg") == 0 && color_parse_hex(value, &color)) {
			t->multicursor.fg = color;
		}
		else if (strcmp(key, "multicursor_bg") == 0 && color_parse_hex(value, &color)) {
			t->multicursor.bg = color;
		}
		else if (strcmp(key, "multicursor_attr") == 0) {
			t->multicursor.attr = attr_parse(value);
		}

		/* Dialog */
		else if (strcmp(key, "dialog_fg") == 0 && color_parse_hex(value, &color)) {
			t->dialog.fg = color;
		}
		else if (strcmp(key, "dialog_bg") == 0 && color_parse_hex(value, &color)) {
			t->dialog.bg = color;
		}
		else if (strcmp(key, "dialog_attr") == 0) {
			t->dialog.attr = attr_parse(value);
		}
		else if (strcmp(key, "dialog_header_fg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_header.fg = color;
		}
		else if (strcmp(key, "dialog_header_bg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_header.bg = color;
		}
		else if (strcmp(key, "dialog_header_attr") == 0) {
			t->dialog_header.attr = attr_parse(value);
		}
		else if (strcmp(key, "dialog_footer_fg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_footer.fg = color;
		}
		else if (strcmp(key, "dialog_footer_bg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_footer.bg = color;
		}
		else if (strcmp(key, "dialog_footer_attr") == 0) {
			t->dialog_footer.attr = attr_parse(value);
		}
		else if (strcmp(key, "dialog_highlight_fg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_highlight.fg = color;
		}
		else if (strcmp(key, "dialog_highlight_bg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_highlight.bg = color;
		}
		else if (strcmp(key, "dialog_highlight_attr") == 0) {
			t->dialog_highlight.attr = attr_parse(value);
		}
		else if (strcmp(key, "dialog_directory_fg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_directory.fg = color;
		}
		else if (strcmp(key, "dialog_directory_bg") == 0 && color_parse_hex(value, &color)) {
			t->dialog_directory.bg = color;
		}
		else if (strcmp(key, "dialog_directory_attr") == 0) {
			t->dialog_directory.attr = attr_parse(value);
		}

		/* Tab bar */
		else if (strcmp(key, "tab_bar_fg") == 0 && color_parse_hex(value, &color)) {
			t->tab_bar.fg = color;
		}
		else if (strcmp(key, "tab_bar_bg") == 0 && color_parse_hex(value, &color)) {
			t->tab_bar.bg = color;
		}
		else if (strcmp(key, "tab_bar_attr") == 0) {
			t->tab_bar.attr = attr_parse(value);
		}
		else if (strcmp(key, "tab_active_fg") == 0 && color_parse_hex(value, &color)) {
			t->tab_active.fg = color;
		}
		else if (strcmp(key, "tab_active_bg") == 0 && color_parse_hex(value, &color)) {
			t->tab_active.bg = color;
		}
		else if (strcmp(key, "tab_active_attr") == 0) {
			t->tab_active.attr = attr_parse(value);
		}
		else if (strcmp(key, "tab_inactive_fg") == 0 && color_parse_hex(value, &color)) {
			t->tab_inactive.fg = color;
		}
		else if (strcmp(key, "tab_inactive_bg") == 0 && color_parse_hex(value, &color)) {
			t->tab_inactive.bg = color;
		}
		else if (strcmp(key, "tab_inactive_attr") == 0) {
			t->tab_inactive.attr = attr_parse(value);
		}
		else if (strcmp(key, "tab_modified_fg") == 0 && color_parse_hex(value, &color)) {
			t->tab_modified.fg = color;
		}
		else if (strcmp(key, "tab_modified_bg") == 0 && color_parse_hex(value, &color)) {
			t->tab_modified.bg = color;
		}
		else if (strcmp(key, "tab_modified_attr") == 0) {
			t->tab_modified.attr = attr_parse(value);
		}

		/* Syntax highlighting - legacy (color only sets fg) */
		else if (strcmp(key, "syntax_normal") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_NORMAL].fg = color;
		}
		else if (strcmp(key, "syntax_keyword") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_KEYWORD].fg = color;
		}
		else if (strcmp(key, "syntax_type") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_TYPE].fg = color;
		}
		else if (strcmp(key, "syntax_string") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_STRING].fg = color;
		}
		else if (strcmp(key, "syntax_number") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_NUMBER].fg = color;
		}
		else if (strcmp(key, "syntax_comment") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_COMMENT].fg = color;
		}
		else if (strcmp(key, "syntax_preprocessor") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_PREPROCESSOR].fg = color;
		}
		else if (strcmp(key, "syntax_function") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_FUNCTION].fg = color;
		}
		else if (strcmp(key, "syntax_operator") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_OPERATOR].fg = color;
		}
		else if (strcmp(key, "syntax_bracket") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_BRACKET].fg = color;
		}
		else if (strcmp(key, "syntax_escape") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_ESCAPE].fg = color;
		}

		/* Syntax highlighting - new style fg/bg/attr */
		else if (strcmp(key, "syntax_normal_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_NORMAL].fg = color;
		}
		else if (strcmp(key, "syntax_normal_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_NORMAL].bg = color;
			t->syntax_bg_set[SYNTAX_NORMAL] = true;
		}
		else if (strcmp(key, "syntax_normal_attr") == 0) {
			t->syntax[SYNTAX_NORMAL].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_keyword_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_KEYWORD].fg = color;
		}
		else if (strcmp(key, "syntax_keyword_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_KEYWORD].bg = color;
			t->syntax_bg_set[SYNTAX_KEYWORD] = true;
		}
		else if (strcmp(key, "syntax_keyword_attr") == 0) {
			t->syntax[SYNTAX_KEYWORD].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_type_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_TYPE].fg = color;
		}
		else if (strcmp(key, "syntax_type_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_TYPE].bg = color;
			t->syntax_bg_set[SYNTAX_TYPE] = true;
		}
		else if (strcmp(key, "syntax_type_attr") == 0) {
			t->syntax[SYNTAX_TYPE].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_string_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_STRING].fg = color;
		}
		else if (strcmp(key, "syntax_string_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_STRING].bg = color;
			t->syntax_bg_set[SYNTAX_STRING] = true;
		}
		else if (strcmp(key, "syntax_string_attr") == 0) {
			t->syntax[SYNTAX_STRING].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_number_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_NUMBER].fg = color;
		}
		else if (strcmp(key, "syntax_number_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_NUMBER].bg = color;
			t->syntax_bg_set[SYNTAX_NUMBER] = true;
		}
		else if (strcmp(key, "syntax_number_attr") == 0) {
			t->syntax[SYNTAX_NUMBER].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_comment_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_COMMENT].fg = color;
		}
		else if (strcmp(key, "syntax_comment_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_COMMENT].bg = color;
			t->syntax_bg_set[SYNTAX_COMMENT] = true;
		}
		else if (strcmp(key, "syntax_comment_attr") == 0) {
			t->syntax[SYNTAX_COMMENT].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_preprocessor_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_PREPROCESSOR].fg = color;
		}
		else if (strcmp(key, "syntax_preprocessor_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_PREPROCESSOR].bg = color;
			t->syntax_bg_set[SYNTAX_PREPROCESSOR] = true;
		}
		else if (strcmp(key, "syntax_preprocessor_attr") == 0) {
			t->syntax[SYNTAX_PREPROCESSOR].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_function_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_FUNCTION].fg = color;
		}
		else if (strcmp(key, "syntax_function_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_FUNCTION].bg = color;
			t->syntax_bg_set[SYNTAX_FUNCTION] = true;
		}
		else if (strcmp(key, "syntax_function_attr") == 0) {
			t->syntax[SYNTAX_FUNCTION].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_operator_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_OPERATOR].fg = color;
		}
		else if (strcmp(key, "syntax_operator_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_OPERATOR].bg = color;
			t->syntax_bg_set[SYNTAX_OPERATOR] = true;
		}
		else if (strcmp(key, "syntax_operator_attr") == 0) {
			t->syntax[SYNTAX_OPERATOR].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_bracket_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_BRACKET].fg = color;
		}
		else if (strcmp(key, "syntax_bracket_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_BRACKET].bg = color;
			t->syntax_bg_set[SYNTAX_BRACKET] = true;
		}
		else if (strcmp(key, "syntax_bracket_attr") == 0) {
			t->syntax[SYNTAX_BRACKET].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_escape_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_ESCAPE].fg = color;
		}
		else if (strcmp(key, "syntax_escape_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_ESCAPE].bg = color;
			t->syntax_bg_set[SYNTAX_ESCAPE] = true;
		}
		else if (strcmp(key, "syntax_escape_attr") == 0) {
			t->syntax[SYNTAX_ESCAPE].attr = attr_parse(value);
		}

		/* Markdown syntax - Headers */
		else if (strcmp(key, "syntax_md_header_1") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_1].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_1_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_1].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_1_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_1].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HEADER_1] = true;
		}
		else if (strcmp(key, "syntax_md_header_1_attr") == 0) {
			t->syntax[SYNTAX_MD_HEADER_1].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_header_2") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_2].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_2_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_2].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_2_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_2].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HEADER_2] = true;
		}
		else if (strcmp(key, "syntax_md_header_2_attr") == 0) {
			t->syntax[SYNTAX_MD_HEADER_2].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_header_3") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_3].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_3_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_3].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_3_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_3].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HEADER_3] = true;
		}
		else if (strcmp(key, "syntax_md_header_3_attr") == 0) {
			t->syntax[SYNTAX_MD_HEADER_3].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_header_4") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_4].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_4_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_4].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_4_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_4].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HEADER_4] = true;
		}
		else if (strcmp(key, "syntax_md_header_4_attr") == 0) {
			t->syntax[SYNTAX_MD_HEADER_4].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_header_5") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_5].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_5_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_5].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_5_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_5].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HEADER_5] = true;
		}
		else if (strcmp(key, "syntax_md_header_5_attr") == 0) {
			t->syntax[SYNTAX_MD_HEADER_5].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_header_6") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_6].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_6_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_6].fg = color;
		}
		else if (strcmp(key, "syntax_md_header_6_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HEADER_6].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HEADER_6] = true;
		}
		else if (strcmp(key, "syntax_md_header_6_attr") == 0) {
			t->syntax[SYNTAX_MD_HEADER_6].attr = attr_parse(value);
		}

		/* Markdown syntax - Text formatting */
		else if (strcmp(key, "syntax_md_bold") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BOLD].fg = color;
		}
		else if (strcmp(key, "syntax_md_bold_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BOLD].fg = color;
		}
		else if (strcmp(key, "syntax_md_bold_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BOLD].bg = color;
			t->syntax_bg_set[SYNTAX_MD_BOLD] = true;
		}
		else if (strcmp(key, "syntax_md_bold_attr") == 0) {
			t->syntax[SYNTAX_MD_BOLD].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_italic") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_ITALIC].fg = color;
		}
		else if (strcmp(key, "syntax_md_italic_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_ITALIC].fg = color;
		}
		else if (strcmp(key, "syntax_md_italic_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_ITALIC].bg = color;
			t->syntax_bg_set[SYNTAX_MD_ITALIC] = true;
		}
		else if (strcmp(key, "syntax_md_italic_attr") == 0) {
			t->syntax[SYNTAX_MD_ITALIC].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_bold_italic") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BOLD_ITALIC].fg = color;
		}
		else if (strcmp(key, "syntax_md_bold_italic_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BOLD_ITALIC].fg = color;
		}
		else if (strcmp(key, "syntax_md_bold_italic_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BOLD_ITALIC].bg = color;
			t->syntax_bg_set[SYNTAX_MD_BOLD_ITALIC] = true;
		}
		else if (strcmp(key, "syntax_md_bold_italic_attr") == 0) {
			t->syntax[SYNTAX_MD_BOLD_ITALIC].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_strikethrough") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_STRIKETHROUGH].fg = color;
		}
		else if (strcmp(key, "syntax_md_strikethrough_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_STRIKETHROUGH].fg = color;
		}
		else if (strcmp(key, "syntax_md_strikethrough_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_STRIKETHROUGH].bg = color;
			t->syntax_bg_set[SYNTAX_MD_STRIKETHROUGH] = true;
		}
		else if (strcmp(key, "syntax_md_strikethrough_attr") == 0) {
			t->syntax[SYNTAX_MD_STRIKETHROUGH].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_escape") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_ESCAPE].fg = color;
		}
		else if (strcmp(key, "syntax_md_escape_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_ESCAPE].fg = color;
		}
		else if (strcmp(key, "syntax_md_escape_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_ESCAPE].bg = color;
			t->syntax_bg_set[SYNTAX_MD_ESCAPE] = true;
		}
		else if (strcmp(key, "syntax_md_escape_attr") == 0) {
			t->syntax[SYNTAX_MD_ESCAPE].attr = attr_parse(value);
		}

		/* Markdown syntax - Code */
		else if (strcmp(key, "syntax_md_code_span") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_SPAN].fg = color;
		}
		else if (strcmp(key, "syntax_md_code_span_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_SPAN].fg = color;
		}
		else if (strcmp(key, "syntax_md_code_span_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_SPAN].bg = color;
			t->syntax_bg_set[SYNTAX_MD_CODE_SPAN] = true;
		}
		else if (strcmp(key, "syntax_md_code_span_attr") == 0) {
			t->syntax[SYNTAX_MD_CODE_SPAN].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_code_block") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_BLOCK].fg = color;
		}
		else if (strcmp(key, "syntax_md_code_block_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_BLOCK].fg = color;
		}
		else if (strcmp(key, "syntax_md_code_block_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_BLOCK].bg = color;
			t->syntax_bg_set[SYNTAX_MD_CODE_BLOCK] = true;
		}
		else if (strcmp(key, "syntax_md_code_block_attr") == 0) {
			t->syntax[SYNTAX_MD_CODE_BLOCK].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_code_fence_open_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_FENCE_OPEN].fg = color;
		}
		else if (strcmp(key, "syntax_md_code_fence_open_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_FENCE_OPEN].bg = color;
			t->syntax_bg_set[SYNTAX_MD_CODE_FENCE_OPEN] = true;
		}
		else if (strcmp(key, "syntax_md_code_fence_close_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_FENCE_CLOSE].fg = color;
		}
		else if (strcmp(key, "syntax_md_code_fence_close_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_CODE_FENCE_CLOSE].bg = color;
			t->syntax_bg_set[SYNTAX_MD_CODE_FENCE_CLOSE] = true;
		}

		/* Markdown syntax - Links and images */
		else if (strcmp(key, "syntax_md_link_text") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LINK_TEXT].fg = color;
		}
		else if (strcmp(key, "syntax_md_link_text_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LINK_TEXT].fg = color;
		}
		else if (strcmp(key, "syntax_md_link_text_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LINK_TEXT].bg = color;
			t->syntax_bg_set[SYNTAX_MD_LINK_TEXT] = true;
		}
		else if (strcmp(key, "syntax_md_link_text_attr") == 0) {
			t->syntax[SYNTAX_MD_LINK_TEXT].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_link_url") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LINK_URL].fg = color;
		}
		else if (strcmp(key, "syntax_md_link_url_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LINK_URL].fg = color;
		}
		else if (strcmp(key, "syntax_md_link_url_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LINK_URL].bg = color;
			t->syntax_bg_set[SYNTAX_MD_LINK_URL] = true;
		}
		else if (strcmp(key, "syntax_md_link_url_attr") == 0) {
			t->syntax[SYNTAX_MD_LINK_URL].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_image") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_IMAGE].fg = color;
		}
		else if (strcmp(key, "syntax_md_image_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_IMAGE].fg = color;
		}
		else if (strcmp(key, "syntax_md_image_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_IMAGE].bg = color;
			t->syntax_bg_set[SYNTAX_MD_IMAGE] = true;
		}
		else if (strcmp(key, "syntax_md_image_attr") == 0) {
			t->syntax[SYNTAX_MD_IMAGE].attr = attr_parse(value);
		}

		/* Markdown syntax - Block elements */
		else if (strcmp(key, "syntax_md_blockquote") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BLOCKQUOTE].fg = color;
		}
		else if (strcmp(key, "syntax_md_blockquote_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BLOCKQUOTE].fg = color;
		}
		else if (strcmp(key, "syntax_md_blockquote_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_BLOCKQUOTE].bg = color;
			t->syntax_bg_set[SYNTAX_MD_BLOCKQUOTE] = true;
		}
		else if (strcmp(key, "syntax_md_blockquote_attr") == 0) {
			t->syntax[SYNTAX_MD_BLOCKQUOTE].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_list_marker") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LIST_MARKER].fg = color;
		}
		else if (strcmp(key, "syntax_md_list_marker_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LIST_MARKER].fg = color;
		}
		else if (strcmp(key, "syntax_md_list_marker_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_LIST_MARKER].bg = color;
			t->syntax_bg_set[SYNTAX_MD_LIST_MARKER] = true;
		}
		else if (strcmp(key, "syntax_md_list_marker_attr") == 0) {
			t->syntax[SYNTAX_MD_LIST_MARKER].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_horizontal_rule") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HORIZONTAL_RULE].fg = color;
		}
		else if (strcmp(key, "syntax_md_horizontal_rule_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HORIZONTAL_RULE].fg = color;
		}
		else if (strcmp(key, "syntax_md_horizontal_rule_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_HORIZONTAL_RULE].bg = color;
			t->syntax_bg_set[SYNTAX_MD_HORIZONTAL_RULE] = true;
		}
		else if (strcmp(key, "syntax_md_horizontal_rule_attr") == 0) {
			t->syntax[SYNTAX_MD_HORIZONTAL_RULE].attr = attr_parse(value);
		}

		/* Markdown syntax - Tables */
		else if (strcmp(key, "syntax_md_table") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE].fg = color;
		}
		else if (strcmp(key, "syntax_md_table_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE].fg = color;
		}
		else if (strcmp(key, "syntax_md_table_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE].bg = color;
			t->syntax_bg_set[SYNTAX_MD_TABLE] = true;
		}
		else if (strcmp(key, "syntax_md_table_attr") == 0) {
			t->syntax[SYNTAX_MD_TABLE].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_table_separator") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE_SEPARATOR].fg = color;
		}
		else if (strcmp(key, "syntax_md_table_separator_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE_SEPARATOR].fg = color;
		}
		else if (strcmp(key, "syntax_md_table_separator_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE_SEPARATOR].bg = color;
			t->syntax_bg_set[SYNTAX_MD_TABLE_SEPARATOR] = true;
		}
		else if (strcmp(key, "syntax_md_table_separator_attr") == 0) {
			t->syntax[SYNTAX_MD_TABLE_SEPARATOR].attr = attr_parse(value);
		}
		else if (strcmp(key, "syntax_md_table_header") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE_HEADER].fg = color;
		}
		else if (strcmp(key, "syntax_md_table_header_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE_HEADER].fg = color;
		}
		else if (strcmp(key, "syntax_md_table_header_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TABLE_HEADER].bg = color;
			t->syntax_bg_set[SYNTAX_MD_TABLE_HEADER] = true;
		}
		else if (strcmp(key, "syntax_md_table_header_attr") == 0) {
			t->syntax[SYNTAX_MD_TABLE_HEADER].attr = attr_parse(value);
		}

		/* Markdown syntax - Task lists */
		else if (strcmp(key, "syntax_md_task_marker") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TASK_MARKER].fg = color;
		}
		else if (strcmp(key, "syntax_md_task_marker_fg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TASK_MARKER].fg = color;
		}
		else if (strcmp(key, "syntax_md_task_marker_bg") == 0 && color_parse_hex(value, &color)) {
			t->syntax[SYNTAX_MD_TASK_MARKER].bg = color;
			t->syntax_bg_set[SYNTAX_MD_TASK_MARKER] = true;
		}
		else if (strcmp(key, "syntax_md_task_marker_attr") == 0) {
			t->syntax[SYNTAX_MD_TASK_MARKER].attr = attr_parse(value);
		}
	}

	fclose(file);

	/* If no name was specified, extract from filename */
	if (t->name == NULL) {
		const char *slash = strrchr(filepath, '/');
		const char *basename = slash ? slash + 1 : filepath;
		char *dot = strrchr(basename, '.');
		if (dot) {
			t->name = strndup(basename, dot - basename);
		} else {
			t->name = strdup(basename);
		}
	}

	return t;
}

/*****************************************************************************
 * Theme Loading and Management
 *****************************************************************************/

/*
 * Compare themes by name for sorting.
 */
static int theme_compare(const void *a, const void *b)
{
	const struct theme *ta = a;
	const struct theme *tb = b;
	return strcmp(ta->name, tb->name);
}

/*
 * Load all themes from ~/.edit/themes/ directory.
 * Always includes the built-in default theme first.
 */
void themes_load(void)
{
	/* Free any previously loaded themes */
	if (loaded_themes != NULL) {
		for (int i = 0; i < theme_count; i++) {
			theme_free(&loaded_themes[i]);
		}
		free(loaded_themes);
		loaded_themes = NULL;
		theme_count = 0;
	}

	/* Start with built-in themes */
	theme_count = 2;
	loaded_themes = malloc(2 * sizeof(struct theme));
	if (loaded_themes == NULL) {
		return;
	}
	loaded_themes[0] = theme_create_default();
	loaded_themes[1] = theme_create_mono_white();

	/* Get home directory (validated) */
	const char *home = safe_get_home();
	if (home == NULL) {
		return;
	}

	/* Build theme directory path */
	char theme_dir[PATH_MAX];
	snprintf(theme_dir, sizeof(theme_dir), "%s%s", home, THEME_DIR);

	/* Open theme directory */
	DIR *dir = opendir(theme_dir);
	if (dir == NULL) {
		return;  /* No theme directory - use default only */
	}

	/* Scan for .ini files */
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		/* Check for .ini extension */
		size_t name_len = strlen(entry->d_name);
		if (name_len < 5) {
			continue;  /* Too short for "x.ini" */
		}

		const char *ext = entry->d_name + name_len - 4;
		if (strcmp(ext, ".ini") != 0) {
			continue;
		}

		/* Build full path, skip if it would be truncated */
		char filepath[PATH_MAX];
		int path_len = snprintf(filepath, sizeof(filepath), "%s%s", theme_dir, entry->d_name);
		if (path_len < 0 || (size_t)path_len >= sizeof(filepath)) {
			continue;
		}

		/* Parse the theme file */
		struct theme *parsed = theme_parse_file(filepath);
		if (parsed == NULL) {
			continue;
		}

		/* Add to loaded themes array */
		struct theme *new_array = realloc(loaded_themes,
		                                  (theme_count + 1) * sizeof(struct theme));
		if (new_array == NULL) {
			theme_free(parsed);
			free(parsed);
			continue;
		}

		loaded_themes = new_array;
		loaded_themes[theme_count] = *parsed;
		free(parsed);  /* Struct was copied, free wrapper only */
		theme_count++;
	}

	closedir(dir);

	/* Sort themes alphabetically, keeping built-ins first */
	if (theme_count > 3) {
		qsort(&loaded_themes[2], theme_count - 2,
		      sizeof(struct theme), theme_compare);
	}
}

/*
 * Free all loaded themes.
 */
void themes_free(void)
{
	if (loaded_themes != NULL) {
		for (int i = 0; i < theme_count; i++) {
			theme_free(&loaded_themes[i]);
		}
		free(loaded_themes);
		loaded_themes = NULL;
		theme_count = 0;
	}
}

/*
 * Find theme index by name.
 * Returns -1 if not found.
 */
int theme_find_by_name(const char *name)
{
	if (name == NULL) {
		return -1;
	}

	for (int i = 0; i < theme_count; i++) {
		if (loaded_themes[i].name != NULL &&
		    strcmp(loaded_themes[i].name, name) == 0) {
			return i;
		}
	}

	return -1;
}

/*
 * Helper to ensure style foreground has sufficient contrast against its background.
 */
static void style_ensure_contrast(struct style *style)
{
	style->fg = color_ensure_contrast(style->fg, style->bg);
}

/*
 * Apply a theme, making it the active theme.
 * Pre-computes WCAG-adjusted foreground colors for readability.
 */
static void theme_apply(struct theme *t)
{
	if (t == NULL) {
		return;
	}

	/* Free previous active theme name */
	free(active_theme.name);

	/* Copy base theme */
	active_theme = *t;
	active_theme.name = t->name ? strdup(t->name) : NULL;

	/*
	 * For syntax styles where background wasn't explicitly set,
	 * use the main theme background. This ensures light themes
	 * don't inherit dark syntax backgrounds from defaults.
	 */
	for (int i = 0; i < SYNTAX_TOKEN_COUNT; i++) {
		if (!t->syntax_bg_set[i]) {
			active_theme.syntax[i].bg = t->background;
		}
	}

	/* Apply WCAG contrast adjustments for foreground colors against backgrounds */

	/* Main foreground against background */
	active_theme.foreground = color_ensure_contrast(t->foreground, active_theme.background);

	/* Color column line against background */
	active_theme.color_column_line = color_ensure_contrast(t->color_column_line, active_theme.background);

	/* Style components - adjust fg against their own bg */
	style_ensure_contrast(&active_theme.line_number);
	style_ensure_contrast(&active_theme.line_number_active);
	style_ensure_contrast(&active_theme.gutter);
	style_ensure_contrast(&active_theme.gutter_active);
	style_ensure_contrast(&active_theme.status);
	style_ensure_contrast(&active_theme.status_filename);
	style_ensure_contrast(&active_theme.status_modified);
	style_ensure_contrast(&active_theme.status_position);
	style_ensure_contrast(&active_theme.message);
	style_ensure_contrast(&active_theme.prompt_label);
	style_ensure_contrast(&active_theme.prompt_input);
	style_ensure_contrast(&active_theme.prompt_bracket);
	style_ensure_contrast(&active_theme.prompt_warning);
	style_ensure_contrast(&active_theme.search_options);
	style_ensure_contrast(&active_theme.search_nomatch);
	style_ensure_contrast(&active_theme.search_error);
	style_ensure_contrast(&active_theme.whitespace);
	style_ensure_contrast(&active_theme.whitespace_tab);
	style_ensure_contrast(&active_theme.whitespace_space);
	style_ensure_contrast(&active_theme.wrap_indicator);
	style_ensure_contrast(&active_theme.empty_line);
	style_ensure_contrast(&active_theme.welcome);
	style_ensure_contrast(&active_theme.bracket_match);
	style_ensure_contrast(&active_theme.multicursor);
	style_ensure_contrast(&active_theme.dialog);
	style_ensure_contrast(&active_theme.dialog_header);
	style_ensure_contrast(&active_theme.dialog_footer);
	style_ensure_contrast(&active_theme.dialog_highlight);
	style_ensure_contrast(&active_theme.dialog_directory);
	style_ensure_contrast(&active_theme.tab_bar);
	style_ensure_contrast(&active_theme.tab_active);
	style_ensure_contrast(&active_theme.tab_inactive);
	style_ensure_contrast(&active_theme.tab_modified);

	/* Syntax styles - adjust fg against their own bg */
	for (int i = 0; i < SYNTAX_TOKEN_COUNT; i++) {
		style_ensure_contrast(&active_theme.syntax[i]);
	}
}

/*
 * Apply theme by index.
 */
void theme_apply_by_index(int index)
{
	if (index < 0 || index >= theme_count) {
		return;
	}

	current_theme_index = index;
	theme_apply(&loaded_themes[index]);
}

/*****************************************************************************
 * Theme Accessors
 *****************************************************************************/

/*
 * Get the currently active theme.
 */
struct theme *theme_get_active(void)
{
	return &active_theme;
}

/*
 * Get the list of loaded themes and count.
 */
struct theme *theme_get_list(int *count)
{
	if (count) {
		*count = theme_count;
	}
	return loaded_themes;
}

/*
 * Get the index of the currently active theme.
 */
int theme_get_active_index(void)
{
	return current_theme_index;
}

/*****************************************************************************
 * Configuration Persistence
 *****************************************************************************/

/*
 * Get the path to the config file (~/.editrc).
 * Returns a newly allocated string, caller must free.
 * Returns NULL if HOME is not set.
 */
static char *config_get_path(void)
{
	const char *home = safe_get_home();
	if (home == NULL) {
		return NULL;
	}

	size_t len = strlen(home) + strlen(CONFIG_FILE) + 1;
	char *path = malloc(len);
	if (path) {
		snprintf(path, len, "%s%s", home, CONFIG_FILE);
	}
	return path;
}

/*
 * Load configuration from ~/.editrc.
 * Supports:
 *   theme=<theme_name>
 *   fuzzy_max_depth=<int>
 *   fuzzy_max_files=<int>
 *   fuzzy_case_sensitive=<true|false>
 *   show_file_icons=<true|false>
 *   show_hidden_files=<true|false>
 */
void config_load(void)
{
	char *path = config_get_path();
	if (path == NULL) {
		return;
	}

	FILE *file = fopen(path, "r");
	free(path);

	if (file == NULL) {
		return;  /* No config file - use defaults */
	}

	char line[256];
	while (fgets(line, sizeof(line), file)) {
		/* Skip comments and empty lines */
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
			continue;
		}

		/* Trim trailing whitespace/newline */
		size_t line_len = strlen(line);
		while (line_len > 0 && (line[line_len - 1] == '\n' ||
		       line[line_len - 1] == '\r' ||
		       isspace((unsigned char)line[line_len - 1]))) {
			line[--line_len] = '\0';
		}

		/* Parse theme=<name> */
		if (strncmp(line, "theme=", 6) == 0) {
			char *name = line + 6;
			int index = theme_find_by_name(name);
			if (index >= 0) {
				current_theme_index = index;
			}
		}
		/* Parse fuzzy_max_depth=<int> */
		else if (strncmp(line, "fuzzy_max_depth=", 16) == 0) {
			int value = atoi(line + 16);
			if (value > 0) {
				editor.fuzzy_max_depth = value;
			}
		}
		/* Parse fuzzy_max_files=<int> */
		else if (strncmp(line, "fuzzy_max_files=", 16) == 0) {
			int value = atoi(line + 16);
			if (value > 0) {
				editor.fuzzy_max_files = value;
			}
		}
		/* Parse fuzzy_case_sensitive=<true|false> */
		else if (strncmp(line, "fuzzy_case_sensitive=", 21) == 0) {
			char *value = line + 21;
			if (strcmp(value, "true") == 0) {
				editor.fuzzy_case_sensitive = true;
			} else if (strcmp(value, "false") == 0) {
				editor.fuzzy_case_sensitive = false;
			}
		}
		/* Parse show_file_icons=<true|false> */
		else if (strncmp(line, "show_file_icons=", 16) == 0) {
			char *value = line + 16;
			editor.show_file_icons = (strcmp(value, "true") == 0);
		}
		/* Parse show_hidden_files=<true|false> */
		else if (strncmp(line, "show_hidden_files=", 18) == 0) {
			char *value = line + 18;
			editor.show_hidden_files = (strcmp(value, "true") == 0);
		}
		/* Parse tab_width=<int> */
		else if (strncmp(line, "tab_width=", 10) == 0) {
			int value = atoi(line + 10);
			if (value >= 1 && value <= 16) {
				editor.tab_width = value;
			}
		}
		/* Parse bar_at_top=<true|false> */
		else if (strncmp(line, "bar_at_top=", 11) == 0) {
			char *value = line + 11;
			editor.bar_at_top = (strcmp(value, "true") == 0);
		}
	}

	fclose(file);
}

/*
 * Save configuration to ~/.editrc.
 */
void config_save(void)
{
	char *path = config_get_path();
	if (path == NULL) {
		return;
	}

	FILE *file = fopen(path, "w");
	free(path);

	if (file == NULL) {
		return;
	}

	fprintf(file, "# edit configuration\n");
	if (active_theme.name != NULL) {
		fprintf(file, "theme=%s\n", active_theme.name);
	}

	/* Fuzzy finder settings */
	fprintf(file, "fuzzy_max_depth=%d\n", editor.fuzzy_max_depth);
	fprintf(file, "fuzzy_max_files=%d\n", editor.fuzzy_max_files);
	fprintf(file, "fuzzy_case_sensitive=%s\n",
	        editor.fuzzy_case_sensitive ? "true" : "false");

	/* File dialog settings */
	fprintf(file, "show_file_icons=%s\n",
	        editor.show_file_icons ? "true" : "false");
	fprintf(file, "show_hidden_files=%s\n",
	        editor.show_hidden_files ? "true" : "false");

	/* Editor settings */
	fprintf(file, "tab_width=%d\n", editor.tab_width);

	/* Bar position */
	fprintf(file, "bar_at_top=%s\n", editor.bar_at_top ? "true" : "false");

	fclose(file);
}
