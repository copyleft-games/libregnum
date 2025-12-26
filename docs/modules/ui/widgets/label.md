# Label Widget

Simple text display widget with customizable font, size, and alignment.

## Creation

```c
g_autoptr(LrgLabel) lbl = lrg_label_new("Hello World");
lrg_container_add_child(container, LRG_WIDGET(lbl));
```

## Text

```c
lrg_label_set_text(lbl, "New Text");
const gchar *text = lrg_label_get_text(lbl);
```

## Font

```c
GrlFont *font = grl_font_load("assets/fonts/Arial.ttf");
lrg_label_set_font(lbl, font);

lrg_label_set_font_size(lbl, 20.0f);
gfloat size = lrg_label_get_font_size(lbl);
```

## Color and Alignment

```c
GrlColor text_color = GRL_COLOR(200, 200, 200, 255);
lrg_label_set_color(lbl, &text_color);

lrg_label_set_alignment(lbl, LRG_TEXT_ALIGNMENT_CENTER);
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
