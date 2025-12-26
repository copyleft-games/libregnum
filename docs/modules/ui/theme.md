# Theming System

`LrgTheme` is the centralized styling system for the UI. It provides a singleton default theme with colors, fonts, and spacing values.

## Overview

The theme system provides:

- **Color Palette** - Consistent colors across the UI
- **Typography** - Font sizes for different scales
- **Spacing** - Standard padding and border widths

## Getting the Default Theme

```c
LrgTheme *theme = lrg_theme_get_default();
```

The default theme is a singleton instance used by all widgets.

## Creating Custom Themes

```c
g_autoptr(LrgTheme) custom_theme = lrg_theme_new();

/* Configure as needed */
GrlColor custom_primary = GRL_COLOR(255, 0, 0, 255);
lrg_theme_set_primary_color(custom_theme, &custom_primary);
```

## Color Palette

### Primary Colors

```c
LrgTheme *theme = lrg_theme_get_default();

GrlColor primary = GRL_COLOR(0, 100, 255, 255);
lrg_theme_set_primary_color(theme, &primary);

GrlColor secondary = GRL_COLOR(150, 150, 150, 255);
lrg_theme_set_secondary_color(theme, &secondary);

GrlColor accent = GRL_COLOR(255, 165, 0, 255);
lrg_theme_set_accent_color(theme, &accent);
```

### Background Colors

```c
GrlColor background = GRL_COLOR(20, 20, 30, 255);
lrg_theme_set_background_color(theme, &background);

GrlColor surface = GRL_COLOR(40, 40, 50, 255);
lrg_theme_set_surface_color(theme, &surface);
```

### Text Colors

```c
GrlColor text = GRL_COLOR(255, 255, 255, 255);
lrg_theme_set_text_color(theme, &text);

GrlColor text_secondary = GRL_COLOR(180, 180, 180, 255);
lrg_theme_set_text_secondary_color(theme, &text_secondary);
```

### Border Colors

```c
GrlColor border = GRL_COLOR(100, 100, 100, 255);
lrg_theme_set_border_color(theme, &border);
```

### Status Colors

```c
GrlColor error = GRL_COLOR(255, 50, 50, 255);
lrg_theme_set_error_color(theme, &error);

GrlColor success = GRL_COLOR(50, 255, 50, 255);
lrg_theme_set_success_color(theme, &success);
```

## Typography

### Font

```c
GrlFont *font = grl_font_load("assets/fonts/Roboto.ttf");
lrg_theme_set_default_font(theme, font);

/* Query font */
GrlFont *current = lrg_theme_get_default_font(theme);
```

### Font Sizes

```c
lrg_theme_set_font_size_small(theme, 12.0f);
lrg_theme_set_font_size_normal(theme, 16.0f);
lrg_theme_set_font_size_large(theme, 24.0f);

/* Query sizes */
gfloat small = lrg_theme_get_font_size_small(theme);
gfloat normal = lrg_theme_get_font_size_normal(theme);
gfloat large = lrg_theme_get_font_size_large(theme);
```

## Spacing

### Padding

```c
lrg_theme_set_padding_small(theme, 4.0f);
lrg_theme_set_padding_normal(theme, 8.0f);
lrg_theme_set_padding_large(theme, 16.0f);
```

### Border

```c
lrg_theme_set_border_width(theme, 2.0f);
```

### Corner Radius

```c
lrg_theme_set_corner_radius(theme, 4.0f);
```

## Using Theme in Widgets

Widgets typically use the default theme:

```c
/* Button uses theme colors */
g_autoptr(LrgButton) button = lrg_button_new("Click Me");
GrlColor primary = grl_color_dup(lrg_theme_get_primary_color(theme));
lrg_button_set_normal_color(button, &primary);
```

## Complete Theme Setup

```c
void setup_theme(void)
{
    LrgTheme *theme = lrg_theme_get_default();

    /* Dark theme palette */
    GrlColor bg = GRL_COLOR(20, 20, 30, 255);
    GrlColor surface = GRL_COLOR(40, 40, 50, 255);
    GrlColor primary = GRL_COLOR(100, 150, 255, 255);
    GrlColor text = GRL_COLOR(220, 220, 220, 255);
    GrlColor border = GRL_COLOR(80, 80, 100, 255);

    lrg_theme_set_background_color(theme, &bg);
    lrg_theme_set_surface_color(theme, &surface);
    lrg_theme_set_primary_color(theme, &primary);
    lrg_theme_set_text_color(theme, &text);
    lrg_theme_set_border_color(theme, &border);

    /* Typography */
    lrg_theme_set_font_size_small(theme, 12.0f);
    lrg_theme_set_font_size_normal(theme, 16.0f);
    lrg_theme_set_font_size_large(theme, 24.0f);

    /* Spacing */
    lrg_theme_set_padding_small(theme, 4.0f);
    lrg_theme_set_padding_normal(theme, 8.0f);
    lrg_theme_set_padding_large(theme, 16.0f);
    lrg_theme_set_border_width(theme, 1.0f);
    lrg_theme_set_corner_radius(theme, 4.0f);
}
```

## Common Themes

### Light Theme

```c
void apply_light_theme(void)
{
    LrgTheme *theme = lrg_theme_get_default();

    GrlColor bg = GRL_COLOR(245, 245, 245, 255);
    GrlColor surface = GRL_COLOR(255, 255, 255, 255);
    GrlColor text = GRL_COLOR(30, 30, 30, 255);
    GrlColor primary = GRL_COLOR(25, 118, 210, 255);
    GrlColor border = GRL_COLOR(200, 200, 200, 255);

    lrg_theme_set_background_color(theme, &bg);
    lrg_theme_set_surface_color(theme, &surface);
    lrg_theme_set_text_color(theme, &text);
    lrg_theme_set_primary_color(theme, &primary);
    lrg_theme_set_border_color(theme, &border);
}
```

### Dark Theme

```c
void apply_dark_theme(void)
{
    LrgTheme *theme = lrg_theme_get_default();

    GrlColor bg = GRL_COLOR(33, 33, 33, 255);
    GrlColor surface = GRL_COLOR(48, 48, 48, 255);
    GrlColor text = GRL_COLOR(229, 229, 229, 255);
    GrlColor primary = GRL_COLOR(33, 150, 243, 255);
    GrlColor border = GRL_COLOR(76, 76, 76, 255);

    lrg_theme_set_background_color(theme, &bg);
    lrg_theme_set_surface_color(theme, &surface);
    lrg_theme_set_text_color(theme, &text);
    lrg_theme_set_primary_color(theme, &primary);
    lrg_theme_set_border_color(theme, &border);
}
```

### Retro/Pixel Art Theme

```c
void apply_retro_theme(void)
{
    LrgTheme *theme = lrg_theme_get_default();

    /* 16-color palette */
    GrlColor bg = GRL_COLOR(0, 0, 0, 255);
    GrlColor surface = GRL_COLOR(85, 85, 85, 255);
    GrlColor text = GRL_COLOR(255, 255, 255, 255);
    GrlColor primary = GRL_COLOR(0, 255, 0, 255);  /* Bright green */
    GrlColor border = GRL_COLOR(170, 170, 170, 255);

    lrg_theme_set_background_color(theme, &bg);
    lrg_theme_set_surface_color(theme, &surface);
    lrg_theme_set_text_color(theme, &text);
    lrg_theme_set_primary_color(theme, &primary);
    lrg_theme_set_border_color(theme, &border);

    /* Large fonts for pixelated look */
    lrg_theme_set_font_size_small(theme, 12.0f);
    lrg_theme_set_font_size_normal(theme, 16.0f);
    lrg_theme_set_font_size_large(theme, 24.0f);

    /* No rounded corners for retro look */
    lrg_theme_set_corner_radius(theme, 0.0f);
}
```

## API Reference

### Singleton Access

- `lrg_theme_get_default()` - Get default theme
- `lrg_theme_new()` - Create custom theme

### Primary Colors

- `lrg_theme_get_primary_color(LrgTheme *self)`
- `lrg_theme_set_primary_color(LrgTheme *self, const GrlColor *color)`
- `lrg_theme_get_secondary_color(LrgTheme *self)`
- `lrg_theme_set_secondary_color(LrgTheme *self, const GrlColor *color)`
- `lrg_theme_get_accent_color(LrgTheme *self)`
- `lrg_theme_set_accent_color(LrgTheme *self, const GrlColor *color)`

### Background Colors

- `lrg_theme_get_background_color(LrgTheme *self)`
- `lrg_theme_set_background_color(LrgTheme *self, const GrlColor *color)`
- `lrg_theme_get_surface_color(LrgTheme *self)`
- `lrg_theme_set_surface_color(LrgTheme *self, const GrlColor *color)`

### Text Colors

- `lrg_theme_get_text_color(LrgTheme *self)`
- `lrg_theme_set_text_color(LrgTheme *self, const GrlColor *color)`
- `lrg_theme_get_text_secondary_color(LrgTheme *self)`
- `lrg_theme_set_text_secondary_color(LrgTheme *self, const GrlColor *color)`

### Border/Status

- `lrg_theme_get_border_color(LrgTheme *self)`
- `lrg_theme_set_border_color(LrgTheme *self, const GrlColor *color)`
- `lrg_theme_get_error_color(LrgTheme *self)`
- `lrg_theme_set_error_color(LrgTheme *self, const GrlColor *color)`
- `lrg_theme_get_success_color(LrgTheme *self)`
- `lrg_theme_set_success_color(LrgTheme *self, const GrlColor *color)`

### Typography

- `lrg_theme_get_default_font(LrgTheme *self)`
- `lrg_theme_set_default_font(LrgTheme *self, GrlFont *font)`
- `lrg_theme_get_font_size_small(LrgTheme *self)`
- `lrg_theme_set_font_size_small(LrgTheme *self, gfloat size)`
- `lrg_theme_get_font_size_normal(LrgTheme *self)`
- `lrg_theme_set_font_size_normal(LrgTheme *self, gfloat size)`
- `lrg_theme_get_font_size_large(LrgTheme *self)`
- `lrg_theme_set_font_size_large(LrgTheme *self, gfloat size)`

### Spacing

- `lrg_theme_get_padding_small(LrgTheme *self)`
- `lrg_theme_set_padding_small(LrgTheme *self, gfloat padding)`
- `lrg_theme_get_padding_normal(LrgTheme *self)`
- `lrg_theme_set_padding_normal(LrgTheme *self, gfloat padding)`
- `lrg_theme_get_padding_large(LrgTheme *self)`
- `lrg_theme_set_padding_large(LrgTheme *self, gfloat padding)`
- `lrg_theme_get_border_width(LrgTheme *self)`
- `lrg_theme_set_border_width(LrgTheme *self, gfloat width)`
- `lrg_theme_get_corner_radius(LrgTheme *self)`
- `lrg_theme_set_corner_radius(LrgTheme *self, gfloat radius)`

## Notes

- The default theme is a singleton
- All widgets use the default theme unless overridden
- Custom themes can be created for testing or multiple themes
- Colors are mutable - changes affect all widgets using them
- Spacing values are in pixels

## Related

- [Widget](widget.md) - Widget system
- [UI Module Overview](index.md) - Complete UI documentation
