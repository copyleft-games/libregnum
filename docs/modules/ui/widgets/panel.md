# Panel Widget

Styled container with background and border support.

## Creation

```c
g_autoptr(LrgPanel) panel = lrg_panel_new();
lrg_widget_set_size(LRG_WIDGET(panel), 400, 300);
lrg_widget_set_position(LRG_WIDGET(panel), 100, 50);
lrg_container_add_child(container, LRG_WIDGET(panel));
```

## Background

```c
GrlColor bg = GRL_COLOR(50, 50, 50, 255);
lrg_panel_set_background_color(panel, &bg);

const GrlColor *current_bg = lrg_panel_get_background_color(panel);
```

## Border

```c
GrlColor border = GRL_COLOR(100, 100, 100, 255);
lrg_panel_set_border_color(panel, &border);
lrg_panel_set_border_width(panel, 2.0f);

/* NULL for no border */
lrg_panel_set_border_color(panel, NULL);
```

## Corner Radius

```c
lrg_panel_set_corner_radius(panel, 5.0f);  /* Rounded corners */
gfloat radius = lrg_panel_get_corner_radius(panel);
```

## Adding Children

```c
g_autoptr(LrgVBox) vbox = lrg_vbox_new();
lrg_container_set_padding(LRG_CONTAINER(vbox), 10);
lrg_container_add_child(LRG_CONTAINER(panel), LRG_WIDGET(vbox));

g_autoptr(LrgLabel) title = lrg_label_new("Settings");
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(title));
```

## API Reference

- `lrg_panel_new()`
- `lrg_panel_get_background_color/set_background_color`
- `lrg_panel_get_border_color/set_border_color`
- `lrg_panel_get_border_width/set_border_width`
- `lrg_panel_get_corner_radius/set_corner_radius`

## Related

- [Container](../container.md)
- [UI Widgets Overview](../index.md)
