# Tab View Widget

`LrgTabView` is a container widget that displays content in switchable tabs. Users can click on tab headers to switch between different content panels.

## Overview

The tab view provides:

- **Tab Bar** - Horizontal row of clickable tab headers
- **Content Area** - Displays the active tab's content widget
- **Tab Position** - Headers can be at top or bottom
- **Hover/Active States** - Visual feedback for interaction

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Tab 1  │  Tab 2  │  Tab 3  │                   │ ← Tab bar (top position)
├─────────────────────────────────────────────────┤
│                                                 │
│                                                 │
│              Content Area                       │
│         (Active tab's widget)                   │
│                                                 │
│                                                 │
└─────────────────────────────────────────────────┘
```

## Basic Usage

### Creating a Tab View

```c
#include <libregnum.h>

/* Create tab view */
LrgTabView *tabs = lrg_tab_view_new ();

/* Create content widgets */
LrgLabel *page1 = lrg_label_new ("Page 1 Content");
LrgLabel *page2 = lrg_label_new ("Page 2 Content");
LrgLabel *page3 = lrg_label_new ("Page 3 Content");

/* Add tabs */
lrg_tab_view_add_tab (tabs, "Settings", LRG_WIDGET (page1));
lrg_tab_view_add_tab (tabs, "Graphics", LRG_WIDGET (page2));
lrg_tab_view_add_tab (tabs, "Audio", LRG_WIDGET (page3));

/* Set size and position */
lrg_widget_set_position (LRG_WIDGET (tabs), 100, 100);
lrg_widget_set_size (LRG_WIDGET (tabs), 400, 300);
```

### Switching Tabs Programmatically

```c
/* Get current tab */
guint current = lrg_tab_view_get_active_tab (tabs);

/* Switch to second tab (0-indexed) */
lrg_tab_view_set_active_tab (tabs, 1);

/* Check tab count */
guint count = lrg_tab_view_get_tab_count (tabs);
```

### Tab Position

```c
/* Place tabs at bottom */
lrg_tab_view_set_tab_position (tabs, LRG_TAB_POSITION_BOTTOM);

/* Query position */
LrgTabPosition pos = lrg_tab_view_get_tab_position (tabs);
if (pos == LRG_TAB_POSITION_TOP)
{
    g_print ("Tabs are at top\n");
}
```

### Tab Bar Height

```c
/* Set custom tab bar height */
lrg_tab_view_set_tab_height (tabs, 40.0f);

/* Get current height */
gfloat height = lrg_tab_view_get_tab_height (tabs);
```

## Responding to Tab Changes

Connect to the `tab-changed` signal:

```c
static void
on_tab_changed (LrgTabView *tabs,
                guint       index,
                gpointer    user_data)
{
    const gchar *label = lrg_tab_view_get_tab_label (tabs, index);
    g_print ("Switched to tab: %s\n", label);
}

g_signal_connect (tabs, "tab-changed",
                  G_CALLBACK (on_tab_changed), NULL);
```

## Managing Tabs

### Adding Tabs

```c
/* Add tab with label and content widget */
lrg_tab_view_add_tab (tabs, "New Tab", LRG_WIDGET (content));
```

### Removing Tabs

```c
/* Remove tab by index */
lrg_tab_view_remove_tab (tabs, 1);  /* Removes second tab */
```

### Accessing Tab Content

```c
/* Get content widget for tab index */
LrgWidget *content = lrg_tab_view_get_tab_content (tabs, 0);

/* Get tab label */
const gchar *label = lrg_tab_view_get_tab_label (tabs, 0);

/* Update tab label */
lrg_tab_view_set_tab_label (tabs, 0, "New Label");
```

## Complex Content

Tabs can contain any widget, including containers:

```c
/* Create a settings page with multiple widgets */
LrgVBox *settings_page = lrg_vbox_new ();
lrg_vbox_set_spacing (settings_page, 10);

LrgLabel *volume_label = lrg_label_new ("Volume");
LrgButton *mute_btn = lrg_button_new_with_label ("Mute");

lrg_container_add_child (LRG_CONTAINER (settings_page), LRG_WIDGET (volume_label));
lrg_container_add_child (LRG_CONTAINER (settings_page), LRG_WIDGET (mute_btn));

/* Add complex page as tab */
lrg_tab_view_add_tab (tabs, "Audio", LRG_WIDGET (settings_page));
```

## Input Handling

The tab view handles mouse interaction automatically:

1. **Hover** - Tab headers highlight on mouse hover
2. **Click** - Clicking a tab header switches to that tab
3. **Content Events** - Events within the content area are forwarded to the active tab's content widget

```c
/* Events are handled automatically, but you can also
   process them manually if needed */
g_autoptr(LrgUIEvent) event = lrg_ui_event_new_mouse_button (
    LRG_UI_EVENT_MOUSE_BUTTON_DOWN,
    0,  /* left button */
    mouse_x, mouse_y
);

lrg_widget_handle_event (LRG_WIDGET (tabs), event);
```

## Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `active-tab` | guint | 0 | Index of currently active tab |
| `tab-position` | LrgTabPosition | TOP | Position of tab bar |
| `tab-height` | gfloat | 32.0 | Height of tab bar in pixels |

### Using Properties

```c
/* Via GObject property system */
guint active;
g_object_get (tabs, "active-tab", &active, NULL);

g_object_set (tabs,
              "tab-position", LRG_TAB_POSITION_BOTTOM,
              "tab-height", 40.0f,
              NULL);
```

## Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `tab-changed` | `(guint index)` | Emitted when active tab changes |

## Styling

The tab view uses built-in colors that can be customized:

| Element | Default Color |
|---------|---------------|
| Normal tab | Gray (80, 80, 80) |
| Hovered tab | Light gray (100, 100, 100) |
| Active tab | Blue (60, 120, 200) |
| Tab text | White (255, 255, 255) |
| Content background | Dark (40, 40, 40) |

For custom styling, subclass `LrgTabView` and override the `draw` virtual method.

## Complete Example

```c
#include <libregnum.h>

static void
on_tab_changed (LrgTabView *tabs,
                guint       index,
                gpointer    user_data)
{
    const gchar *label = lrg_tab_view_get_tab_label (tabs, index);
    g_print ("Tab changed to: %s (index %u)\n", label, index);
}

int
main (void)
{
    /* Initialize engine and window... */

    /* Create tab view */
    LrgTabView *tabs = lrg_tab_view_new ();
    lrg_widget_set_position (LRG_WIDGET (tabs), 50, 50);
    lrg_widget_set_size (LRG_WIDGET (tabs), 500, 400);

    /* Create tab content */
    LrgVBox *general = lrg_vbox_new ();
    lrg_container_add_child (LRG_CONTAINER (general),
                             LRG_WIDGET (lrg_label_new ("General Settings")));

    LrgVBox *graphics = lrg_vbox_new ();
    lrg_container_add_child (LRG_CONTAINER (graphics),
                             LRG_WIDGET (lrg_label_new ("Resolution: 1920x1080")));
    lrg_container_add_child (LRG_CONTAINER (graphics),
                             LRG_WIDGET (lrg_button_new_with_label ("Apply")));

    LrgVBox *audio = lrg_vbox_new ();
    lrg_container_add_child (LRG_CONTAINER (audio),
                             LRG_WIDGET (lrg_label_new ("Master Volume: 80%")));

    /* Add tabs */
    lrg_tab_view_add_tab (tabs, "General", LRG_WIDGET (general));
    lrg_tab_view_add_tab (tabs, "Graphics", LRG_WIDGET (graphics));
    lrg_tab_view_add_tab (tabs, "Audio", LRG_WIDGET (audio));

    /* Connect signal */
    g_signal_connect (tabs, "tab-changed",
                      G_CALLBACK (on_tab_changed), NULL);

    /* Set initial tab */
    lrg_tab_view_set_active_tab (tabs, 1);  /* Start on Graphics */

    /* In game loop */
    while (running)
    {
        /* Handle input */
        lrg_widget_handle_event (LRG_WIDGET (tabs), event);

        /* Draw */
        lrg_widget_draw (LRG_WIDGET (tabs));
    }

    g_object_unref (tabs);
    return 0;
}
```

## API Reference

### Construction

| Function | Description |
|----------|-------------|
| `lrg_tab_view_new()` | Create new tab view |

### Tab Management

| Function | Description |
|----------|-------------|
| `lrg_tab_view_add_tab(self, label, content)` | Add a new tab |
| `lrg_tab_view_remove_tab(self, index)` | Remove tab by index |
| `lrg_tab_view_get_tab_count(self)` | Get number of tabs |

### Active Tab

| Function | Description |
|----------|-------------|
| `lrg_tab_view_get_active_tab(self)` | Get active tab index |
| `lrg_tab_view_set_active_tab(self, index)` | Set active tab |

### Tab Position

| Function | Description |
|----------|-------------|
| `lrg_tab_view_get_tab_position(self)` | Get tab bar position |
| `lrg_tab_view_set_tab_position(self, position)` | Set tab bar position |

### Tab Height

| Function | Description |
|----------|-------------|
| `lrg_tab_view_get_tab_height(self)` | Get tab bar height |
| `lrg_tab_view_set_tab_height(self, height)` | Set tab bar height |

### Tab Access

| Function | Description |
|----------|-------------|
| `lrg_tab_view_get_tab_content(self, index)` | Get content widget |
| `lrg_tab_view_get_tab_label(self, index)` | Get tab label string |
| `lrg_tab_view_set_tab_label(self, index, label)` | Update tab label |

## Enumerations

### LrgTabPosition

| Value | Description |
|-------|-------------|
| `LRG_TAB_POSITION_TOP` | Tab bar at top (default) |
| `LRG_TAB_POSITION_BOTTOM` | Tab bar at bottom |

## Related

- [Container](container.md) - Parent class for containers
- [Widget](widget.md) - Base widget class
- [UI Events](ui-event.md) - Event handling
- [UI Module Overview](index.md) - Complete UI documentation
