# Text Input Widget

Single-line text input field with placeholder text, max length, and password mode support.

## Creation

```c
g_autoptr(LrgTextInput) input = lrg_text_input_new();
g_autoptr(LrgTextInput) input_with_placeholder = lrg_text_input_new_with_placeholder("Enter name");
lrg_widget_set_size(LRG_WIDGET(input), 200, 30);
lrg_container_add_child(container, LRG_WIDGET(input));
```

## Text Management

```c
lrg_text_input_set_text(input, "Hello");
const gchar *text = lrg_text_input_get_text(input);

lrg_text_input_set_placeholder(input, "Enter text");
const gchar *placeholder = lrg_text_input_get_placeholder(input);
```

## Input Constraints

```c
lrg_text_input_set_max_length(input, 20);
guint max = lrg_text_input_get_max_length(input);

lrg_text_input_set_password_mode(input, TRUE);
gboolean is_password = lrg_text_input_get_password_mode(input);
```

## Cursor and Focus

```c
lrg_text_input_set_cursor_position(input, 5);
gint pos = lrg_text_input_get_cursor_position(input);

lrg_text_input_set_focused(input, TRUE);
gboolean focused = lrg_text_input_get_focused(input);
```

## Appearance

```c
lrg_text_input_set_font_size(input, 16.0f);
lrg_text_input_set_text_color(input, &text_color);
lrg_text_input_set_background_color(input, &bg_color);
lrg_text_input_set_border_color(input, &border_color);
lrg_text_input_set_placeholder_color(input, &placeholder_color);
lrg_text_input_set_corner_radius(input, 4.0f);
lrg_text_input_set_padding(input, 5.0f);
```

## API Reference

- `lrg_text_input_new()`
- `lrg_text_input_new_with_placeholder(const gchar *placeholder)`
- `lrg_text_input_get_text(LrgTextInput *self)`
- `lrg_text_input_set_text(LrgTextInput *self, const gchar *text)`
- `lrg_text_input_get_placeholder(LrgTextInput *self)`
- `lrg_text_input_set_placeholder(LrgTextInput *self, const gchar *placeholder)`
- `lrg_text_input_get_max_length(LrgTextInput *self)`
- `lrg_text_input_set_max_length(LrgTextInput *self, guint max_length)`
- `lrg_text_input_get_password_mode(LrgTextInput *self)`
- `lrg_text_input_set_password_mode(LrgTextInput *self, gboolean password_mode)`
- `lrg_text_input_get_cursor_position(LrgTextInput *self)`
- `lrg_text_input_set_cursor_position(LrgTextInput *self, gint position)`
- `lrg_text_input_get_focused(LrgTextInput *self)`
- `lrg_text_input_set_focused(LrgTextInput *self, gboolean focused)`

## Related

- [UI Widgets Overview](../index.md)
