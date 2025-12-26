# Progress Bar Widget

Visual progress indicator for displaying completion status.

## Creation

```c
g_autoptr(LrgProgressBar) pbar = lrg_progress_bar_new();
lrg_widget_set_size(LRG_WIDGET(pbar), 200, 20);
lrg_container_add_child(container, LRG_WIDGET(pbar));
```

## Value

```c
lrg_progress_bar_set_value(pbar, 50.0);
lrg_progress_bar_set_max(pbar, 100.0);

gdouble value = lrg_progress_bar_get_value(pbar);
gdouble max = lrg_progress_bar_get_max(pbar);
gdouble fraction = lrg_progress_bar_get_fraction(pbar);  /* 0.0-1.0 */
```

## Display

```c
lrg_progress_bar_set_show_text(pbar, TRUE);  /* Show percentage */
gboolean show = lrg_progress_bar_get_show_text(pbar);

lrg_progress_bar_set_orientation(pbar, LRG_ORIENTATION_VERTICAL);
```

## Appearance

```c
lrg_progress_bar_set_background_color(pbar, &bg_color);
lrg_progress_bar_set_fill_color(pbar, &fill_color);
lrg_progress_bar_set_text_color(pbar, &text_color);
lrg_progress_bar_set_corner_radius(pbar, 4.0f);
```

## API Reference

- `lrg_progress_bar_new()`
- `lrg_progress_bar_get_value/set_value`
- `lrg_progress_bar_get_max/set_max`
- `lrg_progress_bar_get_fraction`
- `lrg_progress_bar_get_show_text/set_show_text`
- `lrg_progress_bar_get_orientation/set_orientation`

## Related

- [UI Widgets Overview](../index.md)
