#!/usr/bin/env python3
"""
Generate themes for edit with all color hooks and no duplicate colors.
Each theme uses 89+ unique colors with proper semantic assignments.
"""

import os
import colorsys

def hsl_to_hex(h, s, l):
    """Convert HSL (0-360, 0-100, 0-100) to hex color."""
    # Normalize inputs
    h = max(0, min(360, h)) / 360
    s = max(0, min(100, s)) / 100
    l = max(0, min(100, l)) / 100

    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return f"#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}"

class ThemeGenerator:
    def __init__(self, name, primary_hues, is_dark=True, saturation_range=(40, 80)):
        self.name = name
        self.is_dark = is_dark
        self.primary_hues = primary_hues
        self.sat_min, self.sat_max = saturation_range
        self.used_colors = set()
        self.color_counter = 0

    def _get_unique_color(self, hue, sat, light):
        """Generate a unique color, adjusting if needed."""
        hue = hue % 360
        sat = max(0, min(100, sat))
        light = max(5, min(95, light))

        color = hsl_to_hex(hue, sat, light)
        attempts = 0

        while color in self.used_colors and attempts < 100:
            self.color_counter += 1
            light = 5 + ((light + 3) % 90)
            sat = 10 + ((sat + 5) % 80)
            hue = (hue + 7) % 360
            color = hsl_to_hex(hue, sat, light)
            attempts += 1

        self.used_colors.add(color)
        return color

    def _bg_color(self, hue, variation=0):
        """Generate a background color."""
        if self.is_dark:
            light = 8 + (variation * 2) % 10
        else:
            light = 92 - (variation * 2) % 8
        sat = self.sat_min + (variation * 4) % 15
        return self._get_unique_color(hue, sat, light)

    def _fg_color(self, hue, variation=0):
        """Generate a foreground color."""
        if self.is_dark:
            light = 70 + (variation * 3) % 25
        else:
            light = 25 + (variation * 3) % 20
        sat = self.sat_min + 15 + (variation * 5) % 25
        return self._get_unique_color(hue, sat, light)

    def _accent_color(self, hue, intensity=0):
        """Generate an accent color."""
        if self.is_dark:
            light = 55 + (intensity * 4) % 20
        else:
            light = 45 + (intensity * 4) % 15
        sat = self.sat_max - (intensity * 3) % 15
        return self._get_unique_color(hue, sat, light)

    def _subtle_bg(self, hue, variation=0):
        """Generate a subtle background variation."""
        if self.is_dark:
            light = 14 + (variation * 2) % 8
        else:
            light = 86 - (variation * 2) % 8
        sat = self.sat_min + (variation * 3) % 12
        return self._get_unique_color(hue, sat, light)

    def _warning_color(self, variation=0):
        """Generate warning colors (oranges/yellows)."""
        hue = 35 + (variation * 8) % 25
        if self.is_dark:
            light = 60 + (variation * 4) % 15
        else:
            light = 45 + (variation * 4) % 12
        return self._get_unique_color(hue, 75, light)

    def _error_color(self, variation=0):
        """Generate error colors (reds)."""
        hue = 5 + (variation * 6) % 15
        if self.is_dark:
            light = 55 + (variation * 4) % 18
        else:
            light = 42 + (variation * 4) % 12
        return self._get_unique_color(hue, 78, light)

    def _success_color(self, variation=0):
        """Generate success colors (greens)."""
        hue = 125 + (variation * 8) % 25
        if self.is_dark:
            light = 50 + (variation * 4) % 18
        else:
            light = 38 + (variation * 4) % 12
        return self._get_unique_color(hue, 60, light)

    def generate(self, attrs=None):
        """Generate the complete theme."""
        if attrs is None:
            attrs = {}

        h1 = self.primary_hues[0]
        h2 = self.primary_hues[1] if len(self.primary_hues) > 1 else (h1 + 120) % 360
        h3 = self.primary_hues[2] if len(self.primary_hues) > 2 else (h1 + 240) % 360
        h4 = self.primary_hues[3] if len(self.primary_hues) > 3 else (h1 + 60) % 360

        theme = {
            # Core UI
            "background": self._bg_color(h1, 0),
            "foreground": self._fg_color(h1, 0),
            "selection": self._accent_color(h2, 0),
            "search_match": self._accent_color(h3, 1),
            "search_current": self._accent_color(h4, 2),
            "cursor_line": self._subtle_bg(h1, 1),
            "color_column": self._subtle_bg(h1, 2),
            "color_column_line": self._fg_color(h2, 1),
            "trailing_ws": self._error_color(0),

            # Line numbers
            "line_number_fg": self._fg_color(h1, 2),
            "line_number_bg": self._bg_color(h1, 1),
            "line_number_active_fg": self._accent_color(h2, 3),
            "line_number_active_bg": self._subtle_bg(h1, 3),

            # Gutter
            "gutter_fg": self._fg_color(h1, 3),
            "gutter_bg": self._bg_color(h1, 2),
            "gutter_active_fg": self._accent_color(h2, 4),
            "gutter_active_bg": self._subtle_bg(h1, 4),

            # Status bar
            "status_fg": self._fg_color(h2, 0),
            "status_bg": self._subtle_bg(h2, 0),
            "status_filename_fg": self._accent_color(h2, 5),
            "status_filename_bg": self._subtle_bg(h2, 1),
            "status_modified_fg": self._warning_color(0),
            "status_modified_bg": self._subtle_bg(h2, 2),
            "status_position_fg": self._fg_color(h2, 4),
            "status_position_bg": self._subtle_bg(h2, 3),

            # Message bar
            "message_fg": self._fg_color(h1, 4),
            "message_bg": self._bg_color(h1, 3),

            # Prompt
            "prompt_label_fg": self._accent_color(h3, 0),
            "prompt_label_bg": self._bg_color(h1, 4),
            "prompt_input_fg": self._fg_color(h1, 5),
            "prompt_input_bg": self._subtle_bg(h1, 5),
            "prompt_bracket_fg": self._accent_color(h4, 0),
            "prompt_bracket_bg": self._bg_color(h1, 5),
            "prompt_warning_fg": self._warning_color(1),
            "prompt_warning_bg": self._bg_color(h1, 6),

            # Search feedback
            "search_options_fg": self._success_color(0),
            "search_options_bg": self._bg_color(h1, 7),
            "search_nomatch_fg": self._error_color(1),
            "search_nomatch_bg": self._bg_color(h1, 8),
            "search_error_fg": self._error_color(2),
            "search_error_bg": self._bg_color(h1, 9),

            # Whitespace
            "whitespace_fg": self._fg_color(h1, 6),
            "whitespace_bg": self._bg_color(h1, 10),
            "whitespace_tab_fg": self._fg_color(h1, 7),
            "whitespace_tab_bg": self._bg_color(h1, 11),
            "whitespace_space_fg": self._fg_color(h1, 8),
            "whitespace_space_bg": self._bg_color(h1, 12),

            # Wrap indicator
            "wrap_indicator_fg": self._fg_color(h1, 9),
            "wrap_indicator_bg": self._bg_color(h1, 13),

            # Empty lines
            "empty_line_fg": self._fg_color(h1, 10),
            "empty_line_bg": self._bg_color(h1, 14),

            # Welcome
            "welcome_fg": self._accent_color(h2, 6),
            "welcome_bg": self._bg_color(h1, 15),

            # Bracket match
            "bracket_match_fg": self._accent_color(h4, 1),
            "bracket_match_bg": self._subtle_bg(h4, 0),

            # Multi-cursor
            "multicursor_fg": self._fg_color(h3, 0),
            "multicursor_bg": self._accent_color(h3, 2),

            # Dialog
            "dialog_fg": self._fg_color(h1, 11),
            "dialog_bg": self._subtle_bg(h1, 6),
            "dialog_header_fg": self._accent_color(h2, 7),
            "dialog_header_bg": self._subtle_bg(h2, 4),
            "dialog_footer_fg": self._fg_color(h1, 12),
            "dialog_footer_bg": self._subtle_bg(h1, 7),
            "dialog_highlight_fg": self._fg_color(h2, 5),
            "dialog_highlight_bg": self._accent_color(h2, 8),
            "dialog_directory_fg": self._accent_color(h3, 3),
            "dialog_directory_bg": self._subtle_bg(h1, 8),

            # Syntax highlighting
            "syntax_normal_fg": self._fg_color(h1, 13),
            "syntax_normal_bg": self._bg_color(h1, 16),
            "syntax_keyword_fg": self._accent_color(h2, 9),
            "syntax_keyword_bg": self._bg_color(h2, 0),
            "syntax_type_fg": self._accent_color(h3, 4),
            "syntax_type_bg": self._bg_color(h3, 0),
            "syntax_string_fg": self._success_color(1),
            "syntax_string_bg": self._bg_color(h1, 17),
            "syntax_number_fg": self._accent_color(h4, 2),
            "syntax_number_bg": self._bg_color(h4, 0),
            "syntax_comment_fg": self._fg_color(h1, 14),
            "syntax_comment_bg": self._bg_color(h1, 18),
            "syntax_preprocessor_fg": self._accent_color(h3, 5),
            "syntax_preprocessor_bg": self._bg_color(h3, 1),
            "syntax_function_fg": self._accent_color(h2, 10),
            "syntax_function_bg": self._bg_color(h2, 1),
            "syntax_operator_fg": self._fg_color(h1, 15),
            "syntax_operator_bg": self._bg_color(h1, 19),
            "syntax_bracket_fg": self._accent_color(h4, 3),
            "syntax_bracket_bg": self._bg_color(h4, 1),
            "syntax_escape_fg": self._warning_color(2),
            "syntax_escape_bg": self._bg_color(h1, 20),
        }

        return theme, attrs

def write_theme(filepath, name, theme_dict, attrs=None):
    """Write a theme file."""
    content = f"""# {name}
# Auto-generated theme with unique colors for all hooks
name={name}

# Core UI
background={theme_dict['background']}
foreground={theme_dict['foreground']}
selection={theme_dict['selection']}
search_match={theme_dict['search_match']}
search_current={theme_dict['search_current']}
cursor_line={theme_dict['cursor_line']}
color_column={theme_dict['color_column']}
color_column_line={theme_dict['color_column_line']}
trailing_ws={theme_dict['trailing_ws']}

# Line Numbers
line_number_fg={theme_dict['line_number_fg']}
line_number_bg={theme_dict['line_number_bg']}
line_number_active_fg={theme_dict['line_number_active_fg']}
line_number_active_bg={theme_dict['line_number_active_bg']}

# Gutter
gutter_fg={theme_dict['gutter_fg']}
gutter_bg={theme_dict['gutter_bg']}
gutter_active_fg={theme_dict['gutter_active_fg']}
gutter_active_bg={theme_dict['gutter_active_bg']}

# Status Bar
status_fg={theme_dict['status_fg']}
status_bg={theme_dict['status_bg']}
status_filename_fg={theme_dict['status_filename_fg']}
status_filename_bg={theme_dict['status_filename_bg']}
status_modified_fg={theme_dict['status_modified_fg']}
status_modified_bg={theme_dict['status_modified_bg']}
status_position_fg={theme_dict['status_position_fg']}
status_position_bg={theme_dict['status_position_bg']}

# Message Bar
message_fg={theme_dict['message_fg']}
message_bg={theme_dict['message_bg']}

# Prompt
prompt_label_fg={theme_dict['prompt_label_fg']}
prompt_label_bg={theme_dict['prompt_label_bg']}
prompt_input_fg={theme_dict['prompt_input_fg']}
prompt_input_bg={theme_dict['prompt_input_bg']}
prompt_bracket_fg={theme_dict['prompt_bracket_fg']}
prompt_bracket_bg={theme_dict['prompt_bracket_bg']}
prompt_warning_fg={theme_dict['prompt_warning_fg']}
prompt_warning_bg={theme_dict['prompt_warning_bg']}

# Search Feedback
search_options_fg={theme_dict['search_options_fg']}
search_options_bg={theme_dict['search_options_bg']}
search_nomatch_fg={theme_dict['search_nomatch_fg']}
search_nomatch_bg={theme_dict['search_nomatch_bg']}
search_error_fg={theme_dict['search_error_fg']}
search_error_bg={theme_dict['search_error_bg']}

# Whitespace
whitespace_fg={theme_dict['whitespace_fg']}
whitespace_bg={theme_dict['whitespace_bg']}
whitespace_tab_fg={theme_dict['whitespace_tab_fg']}
whitespace_tab_bg={theme_dict['whitespace_tab_bg']}
whitespace_space_fg={theme_dict['whitespace_space_fg']}
whitespace_space_bg={theme_dict['whitespace_space_bg']}

# Wrap Indicator
wrap_indicator_fg={theme_dict['wrap_indicator_fg']}
wrap_indicator_bg={theme_dict['wrap_indicator_bg']}

# Empty Lines
empty_line_fg={theme_dict['empty_line_fg']}
empty_line_bg={theme_dict['empty_line_bg']}

# Welcome Screen
welcome_fg={theme_dict['welcome_fg']}
welcome_bg={theme_dict['welcome_bg']}

# Bracket Matching
bracket_match_fg={theme_dict['bracket_match_fg']}
bracket_match_bg={theme_dict['bracket_match_bg']}

# Multi-cursor
multicursor_fg={theme_dict['multicursor_fg']}
multicursor_bg={theme_dict['multicursor_bg']}

# Dialog
dialog_fg={theme_dict['dialog_fg']}
dialog_bg={theme_dict['dialog_bg']}
dialog_header_fg={theme_dict['dialog_header_fg']}
dialog_header_bg={theme_dict['dialog_header_bg']}
dialog_footer_fg={theme_dict['dialog_footer_fg']}
dialog_footer_bg={theme_dict['dialog_footer_bg']}
dialog_highlight_fg={theme_dict['dialog_highlight_fg']}
dialog_highlight_bg={theme_dict['dialog_highlight_bg']}
dialog_directory_fg={theme_dict['dialog_directory_fg']}
dialog_directory_bg={theme_dict['dialog_directory_bg']}

# Syntax Highlighting
syntax_normal_fg={theme_dict['syntax_normal_fg']}
syntax_normal_bg={theme_dict['syntax_normal_bg']}
syntax_keyword_fg={theme_dict['syntax_keyword_fg']}
syntax_keyword_bg={theme_dict['syntax_keyword_bg']}
syntax_type_fg={theme_dict['syntax_type_fg']}
syntax_type_bg={theme_dict['syntax_type_bg']}
syntax_string_fg={theme_dict['syntax_string_fg']}
syntax_string_bg={theme_dict['syntax_string_bg']}
syntax_number_fg={theme_dict['syntax_number_fg']}
syntax_number_bg={theme_dict['syntax_number_bg']}
syntax_comment_fg={theme_dict['syntax_comment_fg']}
syntax_comment_bg={theme_dict['syntax_comment_bg']}
syntax_preprocessor_fg={theme_dict['syntax_preprocessor_fg']}
syntax_preprocessor_bg={theme_dict['syntax_preprocessor_bg']}
syntax_function_fg={theme_dict['syntax_function_fg']}
syntax_function_bg={theme_dict['syntax_function_bg']}
syntax_operator_fg={theme_dict['syntax_operator_fg']}
syntax_operator_bg={theme_dict['syntax_operator_bg']}
syntax_bracket_fg={theme_dict['syntax_bracket_fg']}
syntax_bracket_bg={theme_dict['syntax_bracket_bg']}
syntax_escape_fg={theme_dict['syntax_escape_fg']}
syntax_escape_bg={theme_dict['syntax_escape_bg']}
"""

    if attrs:
        content += "\n# Text Attributes\n"
        for key, value in attrs.items():
            content += f"{key}={value}\n"

    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, 'w') as f:
        f.write(content)

def create_theme_pair(base_name, primary_hues, saturation=(40, 80), attrs=None):
    """Create both dark and light versions."""
    if attrs is None:
        attrs = {}

    # Dark version
    dark_gen = ThemeGenerator(f"{base_name} Dark", primary_hues, is_dark=True, saturation_range=saturation)
    dark_theme, _ = dark_gen.generate(attrs)
    write_theme(f"themes/{base_name}-dark.ini", f"{base_name.replace('-', ' ').title()} Dark", dark_theme, attrs)

    # Light version
    light_gen = ThemeGenerator(f"{base_name} Light", primary_hues, is_dark=False, saturation_range=saturation)
    light_theme, _ = light_gen.generate(attrs)
    write_theme(f"themes/{base_name}-light.ini", f"{base_name.replace('-', ' ').title()} Light", light_theme, attrs)

# Theme configurations
THEMES = {
    # Colorblindness-friendly themes
    "protanopia": {"hues": [210, 50, 270], "sat": (40, 70), "attrs": {}},
    "deuteranopia": {"hues": [220, 45, 280], "sat": (40, 70), "attrs": {}},
    "tritanopia": {"hues": [0, 120, 30], "sat": (50, 75), "attrs": {}},
    "achromatopsia": {"hues": [0, 0, 0], "sat": (0, 8), "attrs": {}},

    # Holidays
    "christmas": {"hues": [0, 120, 45], "sat": (60, 85), "attrs": {"syntax_keyword_attr": "bold"}},
    "easter": {"hues": [300, 60, 180], "sat": (45, 65), "attrs": {"syntax_comment_attr": "italic"}},
    "halloween": {"hues": [25, 270, 0], "sat": (70, 90), "attrs": {"syntax_keyword_attr": "bold"}},
    "thanksgiving": {"hues": [25, 35, 15], "sat": (50, 75), "attrs": {}},
    "st-patricks": {"hues": [120, 90, 150], "sat": (55, 80), "attrs": {"syntax_function_attr": "bold"}},
    "valentines": {"hues": [340, 0, 330], "sat": (60, 85), "attrs": {"syntax_string_attr": "italic"}},
    "independence-day": {"hues": [0, 220, 45], "sat": (70, 90), "attrs": {"syntax_keyword_attr": "bold"}},
    "new-years": {"hues": [45, 270, 0], "sat": (65, 85), "attrs": {"syntax_number_attr": "bold"}},
    "hanukkah": {"hues": [220, 45, 210], "sat": (55, 75), "attrs": {}},
    "diwali": {"hues": [30, 45, 0], "sat": (70, 90), "attrs": {"syntax_keyword_attr": "bold"}},
    "chinese-new-year": {"hues": [0, 45, 30], "sat": (75, 90), "attrs": {"syntax_function_attr": "bold"}},
    "mardi-gras": {"hues": [270, 45, 120], "sat": (65, 85), "attrs": {}},

    # Decades
    "1820s-regency": {"hues": [30, 45, 180], "sat": (25, 45), "attrs": {}},
    "1830s-romantic": {"hues": [35, 280, 20], "sat": (25, 50), "attrs": {}},
    "1840s-victorian-early": {"hues": [280, 40, 30], "sat": (30, 50), "attrs": {}},
    "1850s-victorian": {"hues": [285, 45, 25], "sat": (35, 55), "attrs": {}},
    "1860s-civil-war": {"hues": [210, 30, 0], "sat": (30, 55), "attrs": {}},
    "1870s-gilded": {"hues": [45, 280, 35], "sat": (40, 60), "attrs": {}},
    "1880s-aesthetic": {"hues": [120, 280, 45], "sat": (35, 55), "attrs": {}},
    "1890s-art-nouveau": {"hues": [120, 45, 200], "sat": (40, 60), "attrs": {}},
    "1900s-edwardian": {"hues": [30, 200, 280], "sat": (40, 60), "attrs": {}},
    "1910s-belle-epoque": {"hues": [45, 300, 180], "sat": (40, 60), "attrs": {}},
    "1920s-art-deco": {"hues": [45, 0, 180], "sat": (55, 75), "attrs": {"syntax_keyword_attr": "bold"}},
    "1930s-depression": {"hues": [35, 180, 20], "sat": (30, 55), "attrs": {}},
    "1940s-wartime": {"hues": [120, 35, 200], "sat": (35, 55), "attrs": {}},
    "1950s-atomic": {"hues": [180, 330, 45], "sat": (55, 75), "attrs": {}},
    "1960s-psychedelic": {"hues": [30, 180, 300, 60], "sat": (75, 95), "attrs": {"syntax_keyword_attr": "bold", "syntax_comment_attr": "italic"}},
    "1970s-earth-tones": {"hues": [25, 90, 35], "sat": (45, 70), "attrs": {}},
    "1980s-neon": {"hues": [300, 180, 330], "sat": (80, 95), "attrs": {"syntax_keyword_attr": "bold"}},
    "1990s-grunge": {"hues": [180, 270, 30], "sat": (40, 65), "attrs": {}},
    "2000s-web": {"hues": [200, 30, 280], "sat": (50, 75), "attrs": {}},
    "2010s-flat": {"hues": [190, 340, 45], "sat": (55, 75), "attrs": {}},
    "2020s-modern": {"hues": [260, 180, 330], "sat": (50, 75), "attrs": {"syntax_comment_attr": "italic"}},

    # Cyberpunk
    "cyberpunk-neon": {"hues": [300, 180, 60], "sat": (85, 100), "attrs": {"syntax_keyword_attr": "bold", "syntax_comment_attr": "italic"}},
    "cyberpunk-matrix": {"hues": [120, 150, 90], "sat": (80, 95), "attrs": {"syntax_function_attr": "bold"}},
    "cyberpunk-blade-runner": {"hues": [20, 200, 280], "sat": (65, 85), "attrs": {"syntax_keyword_attr": "bold"}},
    "cyberpunk-akira": {"hues": [0, 220, 45], "sat": (75, 95), "attrs": {"syntax_keyword_attr": "bold"}},
    "cyberpunk-ghost": {"hues": [200, 280, 340], "sat": (60, 80), "attrs": {"syntax_comment_attr": "italic"}},

    # Synthwave
    "synthwave-retro": {"hues": [280, 320, 200], "sat": (80, 95), "attrs": {"syntax_keyword_attr": "bold", "syntax_string_attr": "italic"}},
    "synthwave-sunset": {"hues": [320, 280, 30], "sat": (85, 100), "attrs": {"syntax_function_attr": "bold"}},
    "synthwave-outrun": {"hues": [300, 180, 340], "sat": (85, 100), "attrs": {"syntax_keyword_attr": "bold"}},
    "synthwave-miami": {"hues": [180, 320, 45], "sat": (80, 95), "attrs": {}},
    "synthwave-arcade": {"hues": [270, 180, 60], "sat": (85, 100), "attrs": {"syntax_keyword_attr": "bold"}},

    # Darkwave
    "darkwave-gothic": {"hues": [270, 300, 0], "sat": (25, 50), "attrs": {"syntax_comment_attr": "italic"}},
    "darkwave-industrial": {"hues": [0, 30, 200], "sat": (20, 45), "attrs": {"syntax_keyword_attr": "bold"}},
    "darkwave-ethereal": {"hues": [240, 280, 200], "sat": (25, 50), "attrs": {"syntax_comment_attr": "italic", "syntax_string_attr": "italic"}},
    "darkwave-coldwave": {"hues": [200, 240, 270], "sat": (20, 45), "attrs": {}},
    "darkwave-deathrock": {"hues": [280, 0, 300], "sat": (30, 55), "attrs": {"syntax_keyword_attr": "bold"}},

    # High Contrast
    "high-contrast": {"hues": [60, 180, 300, 0], "sat": (90, 100), "attrs": {"syntax_keyword_attr": "bold", "syntax_function_attr": "bold"}},
    "high-contrast-warm": {"hues": [0, 30, 60], "sat": (90, 100), "attrs": {"syntax_keyword_attr": "bold"}},
    "high-contrast-cool": {"hues": [180, 220, 260], "sat": (90, 100), "attrs": {"syntax_keyword_attr": "bold"}},
    "high-contrast-mono": {"hues": [0], "sat": (0, 8), "attrs": {"syntax_keyword_attr": "bold", "syntax_comment_attr": "italic"}},
}

def main():
    import glob
    for f in glob.glob("themes/*.ini"):
        os.remove(f)

    print("Generating themes...")
    for name, config in THEMES.items():
        print(f"  Creating {name}...")
        create_theme_pair(name, config["hues"], config["sat"], config.get("attrs", {}))

    theme_count = len(os.listdir("themes"))
    print(f"\nGenerated {theme_count} themes!")

if __name__ == "__main__":
    main()
