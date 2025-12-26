# Checkbox Widget

Toggle checkbox with optional label and customizable appearance.

## Creation

```c
g_autoptr(LrgCheckbox) chk = lrg_checkbox_new("Enable Sound");
lrg_container_add_child(container, LRG_WIDGET(chk));
```

## State

```c
lrg_checkbox_set_checked(chk, TRUE);
gboolean checked = lrg_checkbox_get_checked(chk);

lrg_checkbox_toggle(chk);  /* Toggle state */
```

## Label

```c
lrg_checkbox_set_label(chk, "Enable feature");
const gchar *label = lrg_checkbox_get_label(chk);
```

## Appearance

```c
lrg_checkbox_set_box_size(chk, 16.0f);
lrg_checkbox_set_spacing(chk, 5.0f);  /* Between box and label */

lrg_checkbox_set_box_color(chk, &box_color);
lrg_checkbox_set_check_color(chk, &check_color);
lrg_checkbox_set_text_color(chk, &text_color);
lrg_checkbox_set_font_size(chk, 14.0f);
```

## API Reference

- `lrg_checkbox_new(const gchar *label)`
- `lrg_checkbox_get_checked(LrgCheckbox *self)`
- `lrg_checkbox_set_checked(LrgCheckbox *self, gboolean checked)`
- `lrg_checkbox_toggle(LrgCheckbox *self)`
- `lrg_checkbox_get_label(LrgCheckbox *self)`
- `lrg_checkbox_set_label(LrgCheckbox *self, const gchar *label)`
- `lrg_checkbox_get_box_size(LrgCheckbox *self)`
- `lrg_checkbox_set_box_size(LrgCheckbox *self, gfloat size)`
- `lrg_checkbox_get_spacing(LrgCheckbox *self)`
- `lrg_checkbox_set_spacing(LrgCheckbox *self, gfloat spacing)`

## Related

- [UI Widgets Overview](../index.md)
