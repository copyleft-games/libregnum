# Widget Base Class

`LrgWidget` is the abstract base class for all UI elements. It provides position, size, visibility, event handling, and the virtual method system.

## Overview

Every UI element (button, label, container, etc.) inherits from `LrgWidget`. Widgets provide:

- **Position and Size** - Relative to parent, world coordinates
- **Visibility** - Show/hide widgets
- **State** - Enabled/disabled for input
- **Events** - Mouse, keyboard, focus
- **Virtual Methods** - Draw, measure, handle events

## Position and Size

### Setting Position

```c
/* Relative to parent container */
lrg_widget_set_x(button, 50);
lrg_widget_set_y(button, 100);

/* Or both at once */
lrg_widget_set_position(button, 50, 100);

/* Query position */
gfloat x = lrg_widget_get_x(button);
gfloat y = lrg_widget_get_y(button);
```

### Setting Size

```c
lrg_widget_set_width(button, 100);
lrg_widget_set_height(button, 40);

/* Or both at once */
lrg_widget_set_size(button, 100, 40);

/* Query size */
gfloat width = lrg_widget_get_width(button);
gfloat height = lrg_widget_get_height(button);
```

### World Coordinates

Get absolute position accounting for parent positions:

```c
/* Widget position in parent: (50, 100) */
/* Parent position in its parent: (200, 200) */
/* Grandparent position in its parent: (0, 0) */

gfloat world_x = lrg_widget_get_world_x(button);  /* 250 */
gfloat world_y = lrg_widget_get_world_y(button);  /* 300 */
```

## Visibility

```c
/* Hide a widget */
lrg_widget_set_visible(button, FALSE);

/* Show a widget */
lrg_widget_set_visible(button, TRUE);

/* Query visibility */
if (lrg_widget_get_visible(button))
{
    g_message("Widget is visible");
}
```

Invisible widgets are not drawn and don't receive input events.

## Enabled/Disabled State

```c
/* Disable input processing */
lrg_widget_set_enabled(button, FALSE);

/* Re-enable */
lrg_widget_set_enabled(button, TRUE);

/* Query state */
if (!lrg_widget_get_enabled(button))
{
    g_message("Widget is disabled");
}
```

Disabled widgets don't respond to events (mouse, keyboard, etc.).

## Hierarchy

### Getting Parent

```c
LrgContainer *parent = lrg_widget_get_parent(button);
if (parent != NULL)
{
    g_message("Widget has a parent container");
}
```

Only set when the widget is added to a container.

## Hit Testing

```c
/* Check if point is within widget bounds (world coordinates) */
if (lrg_widget_contains_point(button, mouse_x, mouse_y))
{
    g_message("Point is inside button");
}
```

Used for determining which widget receives mouse events.

## Virtual Methods

Subclasses override these methods:

### Draw Method

Called each frame to render the widget:

```c
/* In your widget subclass */
gboolean my_widget_draw(LrgWidget *self)
{
    gfloat x = lrg_widget_get_world_x(self);
    gfloat y = lrg_widget_get_world_y(self);
    gfloat w = lrg_widget_get_width(self);
    gfloat h = lrg_widget_get_height(self);

    /* Draw using graylib functions */
    GrlColor bg = GRL_COLOR(100, 100, 100, 255);
    grl_draw_rectangle(x, y, w, h, &bg, FALSE);

    return TRUE;
}

/* Call the virtual method */
lrg_widget_draw(my_widget);
```

### Measure Method

Calculate preferred size based on content:

```c
gboolean my_widget_measure(LrgWidget *self,
                           gfloat *preferred_width,
                           gfloat *preferred_height)
{
    /* Calculate size based on content */
    *preferred_width = 100;
    *preferred_height = 40;
    return TRUE;
}

/* Call the virtual method */
gfloat pref_w, pref_h;
lrg_widget_measure(my_widget, &pref_w, &pref_h);
```

### Handle Event Method

Process input events:

```c
gboolean my_widget_handle_event(LrgWidget *self,
                                const LrgUIEvent *event)
{
    LrgUIEventType type = lrg_ui_event_get_event_type(event);

    if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
    {
        gint button = lrg_ui_event_get_button(event);
        g_message("Mouse button %d clicked", button);
        lrg_ui_event_consume(event);
        return TRUE;  /* Consumed the event */
    }

    return FALSE;  /* Not handled, pass to parent */
}

/* Call the virtual method */
g_autoptr(LrgUIEvent) event = lrg_ui_event_new_mouse_button(
    LRG_UI_EVENT_MOUSE_BUTTON_DOWN,
    0,  /* left button */
    100, 50
);
gboolean consumed = lrg_widget_handle_event(my_widget, event);
```

## Wrapper Methods

These call the virtual methods:

### Drawing

```c
/* Only draws if visible */
lrg_widget_draw(button);
```

### Measurement

```c
gfloat preferred_width = 0, preferred_height = 0;
lrg_widget_measure(button, &preferred_width, &preferred_height);
```

### Event Handling

```c
g_autoptr(LrgUIEvent) event = lrg_ui_event_new_mouse_move(150, 200);
gboolean consumed = lrg_widget_handle_event(button, event);
```

Only handles events if visible and enabled.

## Common Patterns

### Widget State Management

```c
typedef struct
{
    LrgWidget *widget;
    gboolean is_hovered;
    gboolean is_focused;
    gfloat opacity;
} WidgetState;

void update_widget_state(WidgetState *state)
{
    if (state->is_hovered)
        state->opacity = 1.0f;
    else
        state->opacity = 0.8f;

    if (!state->is_hovered && !state->is_focused)
        lrg_widget_set_enabled(state->widget, FALSE);
}
```

### Hit Test and Interaction

```c
void handle_mouse_click(LrgCanvas *canvas, gfloat x, gfloat y)
{
    /* Find widget at click position */
    LrgWidget *widget = lrg_canvas_widget_at_point(canvas, x, y);

    if (widget == NULL)
    {
        g_message("Clicked on empty space");
        return;
    }

    if (!lrg_widget_get_enabled(widget))
    {
        g_message("Widget is disabled");
        return;
    }

    /* Create mouse event and dispatch */
    g_autoptr(LrgUIEvent) event = lrg_ui_event_new_mouse_button(
        LRG_UI_EVENT_MOUSE_BUTTON_DOWN,
        0,  /* left button */
        x, y
    );

    lrg_widget_handle_event(widget, event);
}
```

### Widget Visibility Toggle

```c
void toggle_widget_visibility(LrgWidget *widget)
{
    gboolean currently_visible = lrg_widget_get_visible(widget);
    lrg_widget_set_visible(widget, !currently_visible);
}

void hide_all_in_container(LrgContainer *container)
{
    guint count = lrg_container_get_child_count(container);
    for (guint i = 0; i < count; i++)
    {
        LrgWidget *child = lrg_container_get_child(container, i);
        lrg_widget_set_visible(child, FALSE);
    }
}
```

### Measuring Widget Tree

```c
void measure_widget_tree(LrgContainer *container)
{
    gfloat pref_w = 0, pref_h = 0;
    lrg_widget_measure(LRG_WIDGET(container), &pref_w, &pref_h);

    g_message("Container preferred size: %fx%f", pref_w, pref_h);

    guint count = lrg_container_get_child_count(container);
    for (guint i = 0; i < count; i++)
    {
        LrgWidget *child = lrg_container_get_child(container, i);
        gfloat child_w = 0, child_h = 0;
        lrg_widget_measure(child, &child_w, &child_h);
        g_message("  Child %u: %fx%f", i, child_w, child_h);
    }
}
```

## API Reference

### Position

- `lrg_widget_get_x(LrgWidget *self)` - Get X position (relative)
- `lrg_widget_set_x(LrgWidget *self, gfloat x)`
- `lrg_widget_get_y(LrgWidget *self)` - Get Y position (relative)
- `lrg_widget_set_y(LrgWidget *self, gfloat y)`
- `lrg_widget_set_position(LrgWidget *self, gfloat x, gfloat y)`

### Size

- `lrg_widget_get_width(LrgWidget *self)`
- `lrg_widget_set_width(LrgWidget *self, gfloat width)`
- `lrg_widget_get_height(LrgWidget *self)`
- `lrg_widget_set_height(LrgWidget *self, gfloat height)`
- `lrg_widget_set_size(LrgWidget *self, gfloat width, gfloat height)`

### World Coordinates

- `lrg_widget_get_world_x(LrgWidget *self)`
- `lrg_widget_get_world_y(LrgWidget *self)`

### State

- `lrg_widget_get_visible(LrgWidget *self)`
- `lrg_widget_set_visible(LrgWidget *self, gboolean visible)`
- `lrg_widget_get_enabled(LrgWidget *self)`
- `lrg_widget_set_enabled(LrgWidget *self, gboolean enabled)`

### Hierarchy

- `lrg_widget_get_parent(LrgWidget *self)` - Get parent container

### Hit Testing

- `lrg_widget_contains_point(LrgWidget *self, gfloat x, gfloat y)`

### Virtual Methods

- `lrg_widget_draw(LrgWidget *self)`
- `lrg_widget_measure(LrgWidget *self, gfloat *preferred_width, gfloat *preferred_height)`
- `lrg_widget_handle_event(LrgWidget *self, const LrgUIEvent *event)`

## Notes

- All widgets are reference-counted GObjects
- Position is relative to parent (except world functions)
- Invisible or disabled widgets don't render or process events
- Virtual methods are overridden by subclasses
- World coordinates account for entire parent hierarchy

## Related

- [Container](container.md) - Container widgets
- [UI Events](ui-event.md) - Event system
- [UI Module Overview](index.md) - Complete UI documentation
