# Debug Module

The Debug module provides runtime debugging, profiling, and inspection tools for game development. It includes an interactive console, on-screen performance overlay, object inspector, and profiler.

## Overview

The Debug module consists of four main systems:

- **LrgDebugConsole**: Interactive command console for runtime debugging
- **LrgDebugOverlay**: On-screen HUD displaying performance metrics
- **LrgInspector**: Runtime inspection of game objects and components
- **LrgProfiler**: Performance profiling of code sections

## Features

### Debug Console

- Interactive command interface
- Command registration and execution
- Command history with up/down navigation
- Custom commands via callbacks
- Built-in commands (help, echo, clear, history)

### Debug Overlay

- On-screen HUD with configurable content
- Displays FPS, frame time, and memory usage
- Integrates profiler data
- Customizable position, font size, and padding
- Custom data lines for game-specific metrics

### Inspector

- Browse game objects and components at runtime
- Inspect GObject properties and values
- Property value modification
- Hierarchical view of world entities
- Formatted text output for console integration

### Profiler

- Section-based timing measurements
- Frame-level statistics
- Min/max/average timing data
- Sample-based history
- Enable/disable profiling without recompilation

## Quick Start

```c
#include <libregnum.h>

/* Get singleton instances */
LrgDebugConsole *console = lrg_debug_console_get_default ();
LrgDebugOverlay *overlay = lrg_debug_overlay_get_default ();
LrgProfiler *profiler = lrg_profiler_get_default ();
LrgInspector *inspector = lrg_inspector_get_default ();

/* Show overlay */
lrg_debug_overlay_set_visible (overlay, TRUE);
lrg_debug_overlay_set_flags (overlay, LRG_DEBUG_OVERLAY_FPS | LRG_DEBUG_OVERLAY_FRAME_TIME);

/* Start profiling */
lrg_profiler_set_enabled (profiler, TRUE);
lrg_profiler_begin_frame (profiler);

/* Time a code section */
lrg_profiler_begin_section (profiler, "render");
/* rendering code */
lrg_profiler_end_section (profiler, "render");

lrg_profiler_end_frame (profiler);

/* Check console */
if (lrg_debug_console_is_visible (console))
{
    GQueue *output = lrg_debug_console_get_output (console);
    g_print ("Console has %u entries\n", g_queue_get_length (output));
}
```

## Common Patterns

### Profiling a Game Loop

```c
LrgProfiler *profiler = lrg_profiler_get_default ();
lrg_profiler_set_enabled (profiler, TRUE);

for (gint frame = 0; frame < num_frames; frame++)
{
    lrg_profiler_begin_frame (profiler);

    /* Update phase */
    lrg_profiler_begin_section (profiler, "update");
    update_game ();
    lrg_profiler_end_section (profiler, "update");

    /* Render phase */
    lrg_profiler_begin_section (profiler, "render");
    render_game ();
    lrg_profiler_end_section (profiler, "render");

    lrg_profiler_end_frame (profiler);

    /* Check performance */
    gdouble fps = lrg_profiler_get_fps (profiler);
    gdouble frame_time = lrg_profiler_get_frame_time_ms (profiler);

    if (fps < 60.0)
    {
        g_print ("Warning: Low FPS (%.1f)\n", fps);
    }
}
```

### Adding Custom Console Commands

```c
static gchar *
cmd_status (LrgDebugConsole  *console,
            guint             argc,
            const gchar     **argv,
            gpointer          user_data)
{
    GameState *state = (GameState *)user_data;
    return g_strdup_printf ("Score: %d, Level: %d",
                            state->score,
                            state->level);
}

/* Register command */
LrgDebugConsole *console = lrg_debug_console_get_default ();
lrg_debug_console_register_command (console, "status",
                                     "Show game status",
                                     cmd_status, game_state, NULL);

/* Usage: type "status" in console */
```

### Displaying Custom Overlay Data

```c
LrgDebugOverlay *overlay = lrg_debug_overlay_get_default ();
lrg_debug_overlay_set_visible (overlay, TRUE);

/* Add custom lines each frame */
lrg_debug_overlay_set_custom_line (overlay, "score", "Score: %d", player->score);
lrg_debug_overlay_set_custom_line (overlay, "health", "Health: %.0f%%",
                                    player->health / player->max_health * 100.0);

/* Get rendered text for display */
g_autofree gchar *text = lrg_debug_overlay_get_text (overlay);
render_text (text, x, y);
```

### Inspecting Game Objects

```c
LrgInspector *inspector = lrg_inspector_get_default ();
lrg_inspector_set_world (inspector, world);

/* Get objects */
GList *objects = lrg_inspector_get_objects (inspector);
g_print ("World has %u objects\n", g_list_length (objects));

/* Select and inspect */
LrgGameObject *first = g_list_nth_data (objects, 0);
lrg_inspector_select_object (inspector, first);

g_autofree gchar *info = lrg_inspector_get_object_info (inspector);
g_print ("%s\n", info);

g_list_free (objects);
```

## Module Files

- `/var/home/zach/Source/Projects/libregnum/src/debug/lrg-debug-console.h` - Interactive console
- `/var/home/zach/Source/Projects/libregnum/src/debug/lrg-debug-overlay.h` - On-screen overlay
- `/var/home/zach/Source/Projects/libregnum/src/debug/lrg-inspector.h` - Object inspection
- `/var/home/zach/Source/Projects/libregnum/src/debug/lrg-profiler.h` - Performance profiling

## See Also

- [LrgDebugConsole Documentation](./debug-console.md)
- [LrgDebugOverlay Documentation](./debug-overlay.md)
- [LrgInspector Documentation](./inspector.md)
- [LrgProfiler Documentation](./profiler.md)
