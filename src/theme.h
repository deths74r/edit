/*
 * theme.h - Theme and color system for edit
 *
 * Provides theme loading, color parsing, contrast adjustment,
 * and style rendering functions.
 */

#ifndef EDIT_THEME_H
#define EDIT_THEME_H

#include "types.h"
/*****************************************************************************
 * Theme State (extern for rendering access)
 *****************************************************************************/
extern struct theme *loaded_themes;
extern int theme_count;
extern int current_theme_index;
extern struct theme active_theme;

/*****************************************************************************
 * Theme Management
 *****************************************************************************/

/*
 * Create the default built-in dark theme.
 */
struct theme theme_create_default(void);

/*
 * Create the built-in light theme.
 */
struct theme theme_create_mono_white(void);

/*
 * Free a theme's allocated memory.
 */
void theme_free(struct theme *t);

/*
 * Parse a theme from a .ini file.
 * Returns newly allocated theme, or NULL on error.
 * Caller must free with theme_free() then free().
 */
struct theme *theme_parse_file(const char *filepath);

/*
 * Load all themes from ~/.edit/themes/ directory.
 * Populates the global theme list with built-in themes first.
 */
void themes_load(void);

/*
 * Free all loaded themes.
 */
void themes_free(void);

/*
 * Apply a theme by index in the loaded themes list.
 * Computes contrast-adjusted colors for WCAG compliance.
 */
void theme_apply_by_index(int index);

/*
 * Get the currently active theme.
 */
struct theme *theme_get_active(void);

/*
 * Get the list of loaded themes and count.
 */
struct theme *theme_get_list(int *count);

/*
 * Get the index of the currently active theme.
 */
int theme_get_active_index(void);

/*
 * Find theme index by name.
 * Returns -1 if not found.
 */
int theme_find_by_name(const char *name);

/*****************************************************************************
 * Color Utilities
 *****************************************************************************/

/*
 * Parse a hex color string (e.g., "FF79C6" or "#ff79c6") into RGB.
 * Returns true on success.
 */
bool color_parse_hex(const char *hex, struct syntax_color *color);

/*
 * Ensure foreground color has sufficient contrast against background.
 * Uses WCAG 2.1 guidelines (4.5:1 ratio for normal text).
 */
struct syntax_color color_ensure_contrast(struct syntax_color fg,
                                          struct syntax_color bg);

/*
 * Compute relative luminance of a color (for contrast calculations).
 */
double color_luminance(struct syntax_color c);

/*
 * Compute contrast ratio between two colors.
 */
double color_contrast_ratio(struct syntax_color c1, struct syntax_color c2);

/*****************************************************************************
 * Style Rendering
 *****************************************************************************/

/*
 * Build escape sequence for text attributes only.
 * Returns the number of characters written to buf.
 */
int attr_to_escape(text_attr_t attr, char *buffer, size_t buffer_size);

/*
 * Build complete escape sequence for a style (fg, bg, and attributes).
 * Resets attributes first, then applies colors and attributes.
 * Returns the number of characters written to buf.
 */
int style_to_escape(const struct style *style, char *buffer, size_t buffer_size);

/*
 * Build escape sequence for a style with custom background override.
 * Useful for cursor line highlighting.
 * Returns the number of characters written to buf.
 */
int style_to_escape_with_bg(const struct style *style,
                            struct syntax_color bg_override,
                            char *buffer, size_t buffer_size);

/*****************************************************************************
 * Configuration Persistence
 *****************************************************************************/

/*
 * Load configuration from ~/.editrc
 */
void config_load(void);

/*
 * Save configuration to ~/.editrc
 */
void config_save(void);

#endif /* EDIT_THEME_H */
