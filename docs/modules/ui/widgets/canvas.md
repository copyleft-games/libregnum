# Canvas Widget

Root container for the UI system. Handles rendering and input dispatch.

## Creation

```c
g_autoptr(LrgCanvas) canvas = lrg_canvas_new();
lrg_widget_set_size(LRG_WIDGET(canvas), 1280, 720);
```

## Rendering

```c
void game_update(void)
{
    /* Process input and dispatch to widgets */
    lrg_canvas_handle_input(canvas);

    /* Draw entire widget tree */
    lrg_canvas_render(canvas);
}
```

## Adding Widgets

```c
g_autoptr(LrgPanel) main_panel = lrg_panel_new();
lrg_widget_set_position(LRG_WIDGET(main_panel), 150, 100);
lrg_widget_set_size(LRG_WIDGET(main_panel), 500, 400);
lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(main_panel));
```

## Hit Testing

```c
/* Find widget at screen position */
LrgWidget *widget = lrg_canvas_widget_at_point(canvas, mouse_x, mouse_y);
if (widget != NULL)
{
    g_message("Clicked on widget");
}
```

## Focus Management

```c
lrg_canvas_set_focused_widget(canvas, LRG_WIDGET(text_input));
LrgWidget *focused = lrg_canvas_get_focused_widget(canvas);

LrgWidget *hovered = lrg_canvas_get_hovered_widget(canvas);
```

## API Reference

- `lrg_canvas_new()`
- `lrg_canvas_render(LrgCanvas *self)`
- `lrg_canvas_handle_input(LrgCanvas *self)`
- `lrg_canvas_widget_at_point(LrgCanvas *self, gfloat x, gfloat y)`
- `lrg_canvas_get_focused_widget/set_focused_widget`
- `lrg_canvas_get_hovered_widget(LrgCanvas *self)`

## Related

- [Container](../container.md)
- [UI Module Overview](../index.md)
