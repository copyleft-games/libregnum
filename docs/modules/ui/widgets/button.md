# Button Widget

A clickable button with visual feedback for three states: normal, hover, and pressed.

## Creation

```c
g_autoptr(LrgButton) btn = lrg_button_new("Click Me");
lrg_widget_set_size(LRG_WIDGET(btn), 100, 40);
lrg_container_add_child(container, LRG_WIDGET(btn));
```

## Text

```c
lrg_button_set_text(btn, "New Text");
const gchar *text = lrg_button_get_text(btn);
```

## Colors

```c
GrlColor normal = GRL_COLOR(100, 100, 100, 255);
GrlColor hover = GRL_COLOR(120, 120, 120, 255);
GrlColor pressed = GRL_COLOR(80, 80, 80, 255);
GrlColor text = GRL_COLOR(255, 255, 255, 255);

lrg_button_set_normal_color(btn, &normal);
lrg_button_set_hover_color(btn, &hover);
lrg_button_set_pressed_color(btn, &pressed);
lrg_button_set_text_color(btn, &text);
```

## Appearance

```c
lrg_button_set_corner_radius(btn, 5.0f);
gfloat radius = lrg_button_get_corner_radius(btn);
```

## State

```c
if (lrg_button_get_is_hovered(btn))
    g_message("Button is hovered");

if (lrg_button_get_is_pressed(btn))
    g_message("Button is pressed");
```

## API Reference

- `lrg_button_new(const gchar *text)`
- `lrg_button_get_text(LrgButton *self)`
- `lrg_button_set_text(LrgButton *self, const gchar *text)`
- `lrg_button_get_normal_color(LrgButton *self)`
- `lrg_button_set_normal_color(LrgButton *self, const GrlColor *color)`
- `lrg_button_get_hover_color(LrgButton *self)`
- `lrg_button_set_hover_color(LrgButton *self, const GrlColor *color)`
- `lrg_button_get_pressed_color(LrgButton *self)`
- `lrg_button_set_pressed_color(LrgButton *self, const GrlColor *color)`
- `lrg_button_get_text_color(LrgButton *self)`
- `lrg_button_set_text_color(LrgButton *self, const GrlColor *color)`
- `lrg_button_get_corner_radius(LrgButton *self)`
- `lrg_button_set_corner_radius(LrgButton *self, gfloat radius)`
- `lrg_button_get_is_hovered(LrgButton *self)`
- `lrg_button_get_is_pressed(LrgButton *self)`

## Related

- [UI Widgets Overview](../index.md)
