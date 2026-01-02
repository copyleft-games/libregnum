# Label Widget

Simple text display widget with customizable font, size, and alignment. Labels automatically use the theme's default font when no font is explicitly set.

## Creation

```c
g_autoptr(LrgLabel) lbl = lrg_label_new ("Hello World");
lrg_container_add_child (container, LRG_WIDGET (lbl));
```

## Text

```c
lrg_label_set_text (lbl, "New Text");
const gchar *text = lrg_label_get_text (lbl);
```

## Font

Labels use a three-level font fallback chain:

1. **Widget font** - Set via `lrg_label_set_font()`
2. **Theme default font** - From `lrg_theme_get_default_font()`
3. **raylib default** - Built-in bitmap font (last resort)

### Using Theme's Default Font (Recommended)

The simplest approach - just create the label and it uses the theme font:

```c
/* No font set - automatically uses theme's system font */
g_autoptr(LrgLabel) lbl = lrg_label_new ("Hello World");
lrg_label_set_font_size (lbl, 24.0f);
lrg_widget_draw (LRG_WIDGET (lbl));
```

### Using a Specific Font

```c
/* Load and use a custom font */
LrgFontManager *fonts = lrg_font_manager_get_default ();
lrg_font_manager_load_font (fonts, "custom", "assets/fonts/MyFont.ttf", 18, NULL);

GrlFont *font = lrg_font_manager_get_font (fonts, "custom");
lrg_label_set_font (lbl, font);
```

### Font Size

```c
lrg_label_set_font_size (lbl, 20.0f);
gfloat size = lrg_label_get_font_size (lbl);
```

## Color and Alignment

```c
g_autoptr(GrlColor) text_color = grl_color_new (200, 200, 200, 255);
lrg_label_set_color (lbl, text_color);

lrg_label_set_alignment (lbl, LRG_TEXT_ALIGN_CENTER);
```

### Alignment Values

| Value | Description |
|-------|-------------|
| `LRG_TEXT_ALIGN_LEFT` | Left-aligned (default) |
| `LRG_TEXT_ALIGN_CENTER` | Centered within widget bounds |
| `LRG_TEXT_ALIGN_RIGHT` | Right-aligned |

## Text Measurement

Labels can measure their preferred size based on text content:

```c
gfloat width, height;
lrg_widget_measure (LRG_WIDGET (lbl), &width, &height);

/* Use measured size for layout */
lrg_widget_set_size (LRG_WIDGET (lbl), width, height);
```

## Drawing

Labels implement the `draw` virtual method:

```c
/* Position the label */
lrg_widget_set_position (LRG_WIDGET (lbl), 100.0f, 50.0f);

/* Draw it */
lrg_widget_draw (LRG_WIDGET (lbl));
```

## Example: Dynamic Text Updates

```c
/* Create a score label */
g_autoptr(LrgLabel) score_label = lrg_label_new ("Score: 0");
lrg_label_set_font_size (score_label, 24.0f);

g_autoptr(GrlColor) gold = grl_color_new (255, 215, 0, 255);
lrg_label_set_color (score_label, gold);

/* Update each frame */
void update_score (gint score)
{
    g_autofree gchar *text = g_strdup_printf ("Score: %d", score);
    lrg_label_set_text (score_label, text);
}
```

## API Reference

- `lrg_label_new(const gchar *text)`
- `lrg_label_get_text(LrgLabel *self)`
- `lrg_label_set_text(LrgLabel *self, const gchar *text)`
- `lrg_label_get_font(LrgLabel *self)`
- `lrg_label_set_font(LrgLabel *self, GrlFont *font)`
- `lrg_label_get_font_size(LrgLabel *self)`
- `lrg_label_set_font_size(LrgLabel *self, gfloat size)`
- `lrg_label_get_color(LrgLabel *self)`
- `lrg_label_set_color(LrgLabel *self, const GrlColor *color)`
- `lrg_label_get_alignment(LrgLabel *self)`
- `lrg_label_set_alignment(LrgLabel *self, LrgTextAlignment alignment)`

## Related

- [UI Widgets Overview](../index.md)
- [Theme](../theme.md) - Theming system and default font
- [Font Manager](../../text/font-manager.md) - Font loading and system font detection
