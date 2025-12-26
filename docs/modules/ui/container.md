# Container Base Class

`LrgContainer` is the abstract base class for widgets that hold child widgets. It manages layout, child management, and spacing/padding properties.

## Overview

A container provides:

- **Child Management** - Add, remove, query children
- **Layout** - Virtual `layout_children()` method for positioning
- **Spacing and Padding** - Properties for widget arrangement

## Child Management

### Adding Children

```c
LrgContainer *panel = LRG_CONTAINER(lrg_panel_new());
LrgWidget *button = LRG_WIDGET(lrg_button_new("Click"));

lrg_container_add_child(panel, button);
```

The container takes a reference to the child.

### Removing Children

```c
/* Remove specific child */
lrg_container_remove_child(panel, button);

/* Remove all children */
lrg_container_remove_all(panel);
```

### Querying Children

```c
/* Get total count */
guint count = lrg_container_get_child_count(panel);
g_message("Panel has %u children", count);

/* Get specific child by index */
LrgWidget *first = lrg_container_get_child(panel, 0);
LrgWidget *second = lrg_container_get_child(panel, 1);

/* Get all children as a list */
GList *children = lrg_container_get_children(panel);
for (GList *iter = children; iter; iter = iter->next)
{
    LrgWidget *child = LRG_WIDGET(iter->data);
    /* Process child */
}
/* Don't free the list data, just the list itself */
g_list_free(children);
```

## Layout Properties

### Spacing

Space between children:

```c
/* Set spacing between widgets */
lrg_container_set_spacing(container, 10);

/* Query spacing */
gfloat spacing = lrg_container_get_spacing(container);
```

### Padding

Space around the container's content:

```c
/* Set padding inside container */
lrg_container_set_padding(container, 15);

/* Query padding */
gfloat padding = lrg_container_get_padding(container);
```

## Layout Algorithm

Each container subclass implements its own layout:

### Manual Layout Triggering

```c
/* Trigger layout after changing spacing/padding */
lrg_container_layout_children(container);
```

Automatically called when:

- Children are added/removed
- Container size changes
- Container position changes

## Container Types

### VBox - Vertical Layout

```c
g_autoptr(LrgVBox) vbox = lrg_vbox_new();

/* Add children - they stack vertically */
g_autoptr(LrgLabel) title = lrg_label_new("Settings");
g_autoptr(LrgSlider) volume = lrg_slider_new();
g_autoptr(LrgSlider) brightness = lrg_slider_new();

lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(title));
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(volume));
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(brightness));

/* Optional: equal height for all children */
lrg_vbox_set_homogeneous(vbox, TRUE);

/* Set spacing between items */
lrg_container_set_spacing(LRG_CONTAINER(vbox), 10);
```

### HBox - Horizontal Layout

```c
g_autoptr(LrgHBox) hbox = lrg_hbox_new();

/* Add children - they stack horizontally */
g_autoptr(LrgButton) ok = lrg_button_new("OK");
g_autoptr(LrgButton) cancel = lrg_button_new("Cancel");

lrg_container_add_child(LRG_CONTAINER(hbox), LRG_WIDGET(ok));
lrg_container_add_child(LRG_CONTAINER(hbox), LRG_WIDGET(cancel));

/* Optional: equal width for all children */
lrg_hbox_set_homogeneous(hbox, FALSE);
```

### Grid - Grid Layout

```c
g_autoptr(LrgGrid) grid = lrg_grid_new(3);  /* 3 columns */

/* Children arrange left-to-right, top-to-bottom */
for (guint i = 0; i < 9; i++)
{
    g_autoptr(LrgButton) btn = lrg_button_new(g_strdup_printf("%u", i));
    lrg_container_add_child(LRG_CONTAINER(grid), LRG_WIDGET(btn));
}

/* Set spacing */
lrg_grid_set_column_spacing(grid, 5);
lrg_grid_set_row_spacing(grid, 5);
```

### Panel - Styled Container

```c
g_autoptr(LrgPanel) panel = lrg_panel_new();

/* Set appearance */
GrlColor bg = GRL_COLOR(50, 50, 50, 255);
lrg_panel_set_background_color(panel, &bg);

GrlColor border = GRL_COLOR(100, 100, 100, 255);
lrg_panel_set_border_color(panel, &border);
lrg_panel_set_border_width(panel, 2);
lrg_panel_set_corner_radius(panel, 5);

/* Add children */
g_autoptr(LrgVBox) contents = lrg_vbox_new();
lrg_container_add_child(LRG_CONTAINER(panel), LRG_WIDGET(contents));
```

### Canvas - Root Container

```c
g_autoptr(LrgCanvas) canvas = lrg_canvas_new();

/* Top-level widgets */
g_autoptr(LrgPanel) main_panel = lrg_panel_new();
lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(main_panel));

/* Handle input and rendering */
lrg_canvas_handle_input(canvas);
lrg_canvas_render(canvas);

/* Focus management */
lrg_canvas_set_focused_widget(canvas, LRG_WIDGET(text_input));
```

## Common Patterns

### Nested Layout

```c
g_autoptr(LrgCanvas) canvas = lrg_canvas_new();

/* Main panel */
g_autoptr(LrgPanel) main = lrg_panel_new();
lrg_widget_set_size(LRG_WIDGET(main), 600, 400);
lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(main));

/* Vertical layout inside panel */
g_autoptr(LrgVBox) vbox = lrg_vbox_new();
lrg_container_set_padding(LRG_CONTAINER(vbox), 10);
lrg_container_set_spacing(LRG_CONTAINER(vbox), 10);
lrg_container_add_child(LRG_CONTAINER(main), LRG_WIDGET(vbox));

/* Title and horizontal button row */
g_autoptr(LrgLabel) title = lrg_label_new("Dialog");
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(title));

g_autoptr(LrgHBox) buttons = lrg_hbox_new();
lrg_container_set_spacing(LRG_CONTAINER(buttons), 10);

g_autoptr(LrgButton) ok = lrg_button_new("OK");
g_autoptr(LrgButton) cancel = lrg_button_new("Cancel");

lrg_container_add_child(LRG_CONTAINER(buttons), LRG_WIDGET(ok));
lrg_container_add_child(LRG_CONTAINER(buttons), LRG_WIDGET(cancel));

lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(buttons));
```

### Dynamic Content

```c
void populate_list(LrgContainer *list_container, const gchar **items, guint count)
{
    /* Clear existing items */
    lrg_container_remove_all(list_container);

    /* Add new items */
    for (guint i = 0; i < count; i++)
    {
        g_autoptr(LrgLabel) item = lrg_label_new(items[i]);
        lrg_container_add_child(list_container, LRG_WIDGET(item));
    }

    /* Trigger layout */
    lrg_container_layout_children(list_container);
}
```

### Form Layout

```c
g_autoptr(LrgGrid) form = lrg_grid_new(2);  /* 2 columns: label, input */
lrg_grid_set_column_spacing(form, 10);
lrg_grid_set_row_spacing(form, 10);

/* Row 1: Name field */
g_autoptr(LrgLabel) name_label = lrg_label_new("Name:");
g_autoptr(LrgTextInput) name_input = lrg_text_input_new();
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(name_label));
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(name_input));

/* Row 2: Email field */
g_autoptr(LrgLabel) email_label = lrg_label_new("Email:");
g_autoptr(LrgTextInput) email_input = lrg_text_input_new();
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(email_label));
lrg_container_add_child(LRG_CONTAINER(form), LRG_WIDGET(email_input));
```

### Scrollable Container

```c
/* Create a container that will be scrolled */
g_autoptr(LrgVBox) content = lrg_vbox_new();

/* Add many children */
for (guint i = 0; i < 100; i++)
{
    g_autoptr(LrgLabel) item = lrg_label_new(
        g_strdup_printf("Item %u", i)
    );
    lrg_container_add_child(LRG_CONTAINER(content), LRG_WIDGET(item));
}

/* In a real scrollable container, you'd implement clipping
   and respond to scroll events */
```

## Layout Strategy

When implementing layout for a custom container:

1. Get all children with `lrg_container_get_children()`
2. Measure each child with `lrg_widget_measure()`
3. Calculate positions based on layout algorithm
4. Set position and size on each child with `lrg_widget_set_position()` and `lrg_widget_set_size()`

Example (simplified VBox):

```c
void my_container_layout_children(LrgContainer *self)
{
    GList *children = lrg_container_get_children(self);
    if (children == NULL)
        return;

    gfloat spacing = lrg_container_get_spacing(self);
    gfloat padding = lrg_container_get_padding(self);
    gfloat y = padding;

    for (GList *iter = children; iter; iter = iter->next)
    {
        LrgWidget *child = LRG_WIDGET(iter->data);

        /* Measure child */
        gfloat child_w = 0, child_h = 0;
        lrg_widget_measure(child, &child_w, &child_h);

        /* Position child */
        lrg_widget_set_position(child, padding, y);
        lrg_widget_set_size(child, child_w, child_h);

        y += child_h + spacing;
    }
}
```

## API Reference

### Child Management

- `lrg_container_add_child(LrgContainer *self, LrgWidget *child)`
- `lrg_container_remove_child(LrgContainer *self, LrgWidget *child)`
- `lrg_container_remove_all(LrgContainer *self)`
- `lrg_container_get_child_count(LrgContainer *self)`
- `lrg_container_get_child(LrgContainer *self, guint index)`
- `lrg_container_get_children(LrgContainer *self)`

### Layout Properties

- `lrg_container_get_spacing(LrgContainer *self)`
- `lrg_container_set_spacing(LrgContainer *self, gfloat spacing)`
- `lrg_container_get_padding(LrgContainer *self)`
- `lrg_container_set_padding(LrgContainer *self, gfloat padding)`

### Layout

- `lrg_container_layout_children(LrgContainer *self)`

## Notes

- Containers are reference-counted GObjects
- Children are referenced when added
- Layout is automatic when size/position changes
- `get_children()` returns a list that must be freed
- Spacing is between children, padding is around content

## Related

- [Widget](widget.md) - Base widget class
- [UI Module Overview](index.md) - Complete UI documentation
