# UI Module

The UI module provides a comprehensive widget system for building user interfaces in games. It features a hierarchical widget structure with flexible layout containers, event handling, and theming support.

## Overview

The UI system is built on these core concepts:

1. **Widgets** - Basic visual elements (buttons, labels, sliders, etc.)
2. **Containers** - Widgets that hold child widgets (boxes, panels, grids, canvas)
3. **Events** - User interaction events (mouse, keyboard, focus)
4. **Theme** - Centralized styling system for colors, fonts, spacing
5. **Canvas** - Root container that handles rendering and input dispatch

## Widget Hierarchy

All UI elements inherit from `LrgWidget`, creating a tree structure:

```
LrgCanvas (root container)
├── LrgPanel
│   ├── LrgButton
│   ├── LrgLabel
│   └── LrgTextInput
├── LrgVBox (vertical layout)
│   ├── LrgLabel ("Title")
│   └── LrgButton ("OK")
└── LrgGrid (2 columns)
    ├── LrgLabel ("Name:")
    ├── LrgTextInput
    ├── LrgLabel ("HP:")
    └── LrgSlider
```

## Quick Start

### Basic UI Setup

```c
#include <libregnum.h>

g_autoptr(LrgCanvas) canvas = lrg_canvas_new();

/* Create a panel */
g_autoptr(LrgPanel) panel = lrg_panel_new();
lrg_widget_set_size(LRG_WIDGET(panel), 400, 300);
lrg_widget_set_position(LRG_WIDGET(panel), 100, 50);
lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(panel));

/* Create a button */
g_autoptr(LrgButton) button = lrg_button_new("Click Me");
lrg_widget_set_size(LRG_WIDGET(button), 100, 40);
lrg_container_add_child(LRG_CONTAINER(panel), LRG_WIDGET(button));

/* Create a label */
g_autoptr(LrgLabel) label = lrg_label_new("Hello, UI!");
lrg_container_add_child(LRG_CONTAINER(panel), LRG_WIDGET(label));

/* In game loop - input and rendering */
void game_update(void)
{
    lrg_canvas_handle_input(canvas);  /* Process input events */
    lrg_canvas_render(canvas);         /* Draw everything */
}
```

## Core Types

### Base Classes

- **LrgWidget** - Abstract base for all UI elements
  - Position, size, visibility, enabled state
  - Event handling
  - Virtual methods: draw, measure, handle_event

- **LrgContainer** - Abstract base for widgets holding children
  - Child management
  - Spacing and padding
  - Layout system

### Layout Containers

- **LrgCanvas** - Root container, handles rendering and event dispatch
- **LrgVBox** - Vertical box layout
- **LrgHBox** - Horizontal box layout
- **LrgGrid** - Grid layout with configurable columns
- **LrgPanel** - Styled container with background and border

### Widgets

- **LrgButton** - Clickable button with three color states
- **LrgLabel** - Text display with alignment
- **LrgTextInput** - Single-line text input with focus support
- **LrgCheckbox** - Toggle checkbox with optional label
- **LrgSlider** - Value selection (horizontal/vertical)
- **LrgProgressBar** - Visual progress indicator
- **LrgImage** - Texture display with scaling modes

### Events and Styling

- **LrgUIEvent** - User interaction event (mouse, keyboard, focus)
- **LrgTheme** - Centralized styling system

## Working with Widgets

### Position and Size

```c
/* Relative to parent */
lrg_widget_set_x(button, 10);
lrg_widget_set_y(button, 10);
lrg_widget_set_width(button, 100);
lrg_widget_set_height(button, 40);

/* Or all at once */
lrg_widget_set_position(button, 10, 10);
lrg_widget_set_size(button, 100, 40);

/* Get world coordinates (accounting for parent positions) */
gfloat world_x = lrg_widget_get_world_x(button);
gfloat world_y = lrg_widget_get_world_y(button);
```

### Visibility and State

```c
/* Control visibility */
lrg_widget_set_visible(button, TRUE);

/* Disable input handling */
lrg_widget_set_enabled(button, FALSE);

/* Query state */
if (lrg_widget_get_visible(button))
{
    g_message("Button is visible");
}
```

### Hit Testing

```c
/* Check if point is inside widget */
if (lrg_widget_contains_point(button, mouse_x, mouse_y))
{
    g_message("Mouse over button");
}

/* Find widget at screen position */
LrgWidget *widget_under_mouse = lrg_canvas_widget_at_point(canvas,
                                                           mouse_x, mouse_y);
```

## Layout System

Containers implement layout algorithms:

### Vertical Box (VBox)

Stacks children vertically:

```c
g_autoptr(LrgVBox) vbox = lrg_vbox_new();
lrg_container_set_spacing(LRG_CONTAINER(vbox), 10);
lrg_container_set_padding(LRG_CONTAINER(vbox), 5);

g_autoptr(LrgLabel) title = lrg_label_new("Settings");
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(title));

g_autoptr(LrgSlider) volume = lrg_slider_new();
lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(volume));

/* Homogeneous: all children same height */
lrg_vbox_set_homogeneous(vbox, FALSE);
```

### Horizontal Box (HBox)

Stacks children horizontally:

```c
g_autoptr(LrgHBox) button_row = lrg_hbox_new();
lrg_container_set_spacing(LRG_CONTAINER(button_row), 10);

g_autoptr(LrgButton) ok = lrg_button_new("OK");
g_autoptr(LrgButton) cancel = lrg_button_new("Cancel");

lrg_container_add_child(LRG_CONTAINER(button_row), LRG_WIDGET(ok));
lrg_container_add_child(LRG_CONTAINER(button_row), LRG_WIDGET(cancel));
```

### Grid

Arrange children in a grid:

```c
g_autoptr(LrgGrid) grid = lrg_grid_new(3);  /* 3 columns */
lrg_grid_set_column_spacing(grid, 10);
lrg_grid_set_row_spacing(grid, 10);

/* Children fill left-to-right, top-to-bottom */
for (guint i = 0; i < 9; i++)
{
    g_autoptr(LrgButton) btn = lrg_button_new(g_strdup_printf("%u", i));
    lrg_container_add_child(LRG_CONTAINER(grid), LRG_WIDGET(btn));
}
```

### Canvas

Root container for the entire UI:

```c
g_autoptr(LrgCanvas) canvas = lrg_canvas_new();

/* Add top-level widgets */
g_autoptr(LrgPanel) main_panel = lrg_panel_new();
lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(main_panel));

/* Rendering */
void game_loop(void)
{
    lrg_canvas_handle_input(canvas);
    lrg_canvas_render(canvas);
}
```

## Event Handling

### Event Types

- Mouse movement
- Mouse button press/release
- Keyboard key press/release
- Scroll wheel
- Focus gain/loss

### Handling Events

Widgets override the `handle_event` virtual method:

```c
/* For widgets you create, handle events */
gboolean my_widget_handle_event(LrgWidget *self,
                                const LrgUIEvent *event)
{
    LrgUIEventType type = lrg_ui_event_get_event_type(event);

    if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
    {
        gint button = lrg_ui_event_get_button(event);
        g_message("Mouse button %d pressed", button);
        lrg_ui_event_consume(event);  /* Mark as handled */
        return TRUE;
    }

    if (type == LRG_UI_EVENT_KEY_DOWN)
    {
        GrlKey key = lrg_ui_event_get_key(event);
        g_message("Key pressed");
        return TRUE;
    }

    return FALSE;  /* Not handled, pass to parent */
}
```

### Focus Management

```c
/* Set which widget receives keyboard input */
lrg_canvas_set_focused_widget(canvas, LRG_WIDGET(text_input));

/* Get currently focused widget */
LrgWidget *focused = lrg_canvas_get_focused_widget(canvas);

/* Get widget under mouse */
LrgWidget *hovered = lrg_canvas_get_hovered_widget(canvas);
```

## Theming

### Using the Theme

```c
LrgTheme *theme = lrg_theme_get_default();

/* Set colors */
GrlColor primary = GRL_COLOR(0, 100, 255, 255);  /* Blue */
lrg_theme_set_primary_color(theme, &primary);

/* Set typography */
lrg_theme_set_font_size_normal(theme, 18.0f);
lrg_theme_set_font_size_large(theme, 24.0f);

/* Set spacing */
lrg_theme_set_padding_normal(theme, 10.0f);
lrg_theme_set_border_width(theme, 2.0f);

/* Query theme values */
const GrlColor *text_color = lrg_theme_get_text_color(theme);
gfloat padding = lrg_theme_get_padding_normal(theme);
```

### Color Palette

The theme provides a complete color palette:

- Primary, Secondary, Accent colors
- Background and Surface colors
- Text and secondary text colors
- Border color
- Error and Success colors

### Creating Custom Themes

```c
/* Create a custom theme */
g_autoptr(LrgTheme) custom_theme = lrg_theme_new();

GrlColor dark_bg = GRL_COLOR(20, 20, 30, 255);
GrlColor text = GRL_COLOR(200, 200, 200, 255);
GrlColor accent = GRL_COLOR(100, 200, 255, 255);

lrg_theme_set_background_color(custom_theme, &dark_bg);
lrg_theme_set_text_color(custom_theme, &text);
lrg_theme_set_accent_color(custom_theme, &accent);

/* Widgets automatically use the default theme */
/* Custom themes can be passed to widgets if needed */
```

## Game Integration

### Main Loop Integration

```c
typedef struct
{
    LrgCanvas *ui_canvas;
    LrgInputMap *input_map;
} Game;

void game_init(Game *game)
{
    game->ui_canvas = lrg_canvas_new();
    game->input_map = lrg_input_map_new();

    /* Setup UI */
    g_autoptr(LrgButton) start_btn = lrg_button_new("Start Game");
    lrg_container_add_child(LRG_CONTAINER(game->ui_canvas),
                           LRG_WIDGET(start_btn));
}

void game_update(Game *game)
{
    /* Process input to UI */
    lrg_canvas_handle_input(game->ui_canvas);

    /* Update game logic */
    // ... player update, etc.

    /* Render UI */
    lrg_canvas_render(game->ui_canvas);
}
```

## Widget Hierarchy Functions

### Adding/Removing Children

```c
LrgContainer *container = LRG_CONTAINER(panel);
LrgWidget *child = LRG_WIDGET(button);

lrg_container_add_child(container, child);
lrg_container_remove_child(container, child);
lrg_container_remove_all(container);
```

### Querying Children

```c
guint child_count = lrg_container_get_child_count(LRG_CONTAINER(panel));

LrgWidget *first_child = lrg_container_get_child(LRG_CONTAINER(panel), 0);

GList *children = lrg_container_get_children(LRG_CONTAINER(panel));
for (GList *iter = children; iter; iter = iter->next)
{
    LrgWidget *child = LRG_WIDGET(iter->data);
    // process child
}
```

## Complete Example

```c
g_autoptr(LrgCanvas) create_main_menu(void)
{
    g_autoptr(LrgCanvas) canvas = lrg_canvas_new();

    /* Main panel */
    g_autoptr(LrgPanel) main_panel = lrg_panel_new();
    lrg_widget_set_position(LRG_WIDGET(main_panel), 150, 100);
    lrg_widget_set_size(LRG_WIDGET(main_panel), 500, 400);
    lrg_container_add_child(LRG_CONTAINER(canvas), LRG_WIDGET(main_panel));

    /* VBox for layout */
    g_autoptr(LrgVBox) vbox = lrg_vbox_new();
    lrg_container_set_spacing(LRG_CONTAINER(vbox), 20);
    lrg_container_set_padding(LRG_CONTAINER(vbox), 20);
    lrg_container_add_child(LRG_CONTAINER(main_panel), LRG_WIDGET(vbox));

    /* Title */
    g_autoptr(LrgLabel) title = lrg_label_new("Main Menu");
    lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(title));

    /* Buttons */
    g_autoptr(LrgButton) play = lrg_button_new("Play");
    g_autoptr(LrgButton) settings = lrg_button_new("Settings");
    g_autoptr(LrgButton) quit = lrg_button_new("Quit");

    lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(play));
    lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(settings));
    lrg_container_add_child(LRG_CONTAINER(vbox), LRG_WIDGET(quit));

    return g_steal_pointer(&canvas);
}
```

## Module Structure

### Core Types

- `lrg-widget.h` - Base widget class
- `lrg-container.h` - Base container class
- `lrg-ui-event.h` - Event system
- `lrg-theme.h` - Theming system

### Containers

- `lrg-canvas.h` - Root UI container
- `lrg-panel.h` - Styled container
- `lrg-vbox.h` - Vertical layout
- `lrg-hbox.h` - Horizontal layout
- `lrg-grid.h` - Grid layout

### Widgets

- `lrg-button.h` - Clickable button
- `lrg-label.h` - Text display
- `lrg-text-input.h` - Text input field
- `lrg-checkbox.h` - Toggle checkbox
- `lrg-slider.h` - Value slider
- `lrg-progress-bar.h` - Progress indicator
- `lrg-image.h` - Texture display

## Next Steps

- See [Widget Documentation](widget.md) for widget basics
- See [Container Documentation](container.md) for layout
- See [UI Events](ui-event.md) for event handling
- See [Theming](theme.md) for styling
- See [UI Basics Example](../examples/ui-basics.md) for complete sample
