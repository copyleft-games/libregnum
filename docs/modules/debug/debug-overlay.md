# LrgDebugOverlay

An on-screen HUD that displays performance metrics and custom game data.

## Overview

`LrgDebugOverlay` provides real-time visualization of FPS, frame time, memory usage, profiler data, and custom game metrics. It integrates with the profiler to display performance information.

## Singleton Access

```c
LrgDebugOverlay *overlay = lrg_debug_overlay_get_default ();

/* Or create new instance */
LrgDebugOverlay *overlay = lrg_debug_overlay_new ();
```

## Visibility and Display

### Visibility Control

```c
gboolean visible = lrg_debug_overlay_is_visible (overlay);

lrg_debug_overlay_set_visible (overlay, TRUE);
lrg_debug_overlay_set_visible (overlay, FALSE);
lrg_debug_overlay_toggle (overlay);
```

When hidden, the overlay returns empty text.

### Display Flags

Control what information is displayed:

```c
/* Get current flags */
LrgDebugOverlayFlags flags = lrg_debug_overlay_get_flags (overlay);

/* Set specific flags */
lrg_debug_overlay_set_flags (overlay, LRG_DEBUG_OVERLAY_FPS | LRG_DEBUG_OVERLAY_FRAME_TIME);

/* Add flags */
lrg_debug_overlay_add_flags (overlay, LRG_DEBUG_OVERLAY_MEMORY);

/* Remove flags */
lrg_debug_overlay_remove_flags (overlay, LRG_DEBUG_OVERLAY_FPS);

/* Check flag */
if (lrg_debug_overlay_has_flag (overlay, LRG_DEBUG_OVERLAY_PROFILER))
{
    g_print ("Profiler data is displayed\n");
}
```

### Available Flags

- `LRG_DEBUG_OVERLAY_FPS` - Frames per second
- `LRG_DEBUG_OVERLAY_FRAME_TIME` - Time per frame in ms
- `LRG_DEBUG_OVERLAY_MEMORY` - Memory usage statistics
- `LRG_DEBUG_OVERLAY_PROFILER` - Integrated profiler data
- `LRG_DEBUG_OVERLAY_CUSTOM` - Custom display lines

## Position and Style

### Position

```c
gint x, y;
lrg_debug_overlay_get_position (overlay, &x, &y);

/* Default is (10, 10) */
lrg_debug_overlay_set_position (overlay, 50, 100);
```

### Font Size

```c
gint size = lrg_debug_overlay_get_font_size (overlay);  /* default: 16 */

lrg_debug_overlay_set_font_size (overlay, 20);

/* Minimum is 8 pixels */
lrg_debug_overlay_set_font_size (overlay, 4);  /* becomes 8 */
```

### Padding

```c
gint padding = lrg_debug_overlay_get_padding (overlay);  /* default: 5 */

lrg_debug_overlay_set_padding (overlay, 10);

/* Minimum is 0 */
lrg_debug_overlay_set_padding (overlay, -5);  /* becomes 0 */
```

## Custom Lines

Add game-specific data to the overlay display:

```c
/* Add or update a custom line */
lrg_debug_overlay_set_custom_line (overlay, "player-score", "Score: %d", 1000);
lrg_debug_overlay_set_custom_line (overlay, "player-health", "Health: %.0f%%", 75.0);

/* Remove specific line */
lrg_debug_overlay_remove_custom_line (overlay, "player-score");

/* Clear all custom lines */
lrg_debug_overlay_clear_custom_lines (overlay);
```

The key parameter is used to identify and update lines:

```c
/* Updating existing line */
for (gint i = 0; i < 10; i++)
{
    /* Same key updates the same line */
    lrg_debug_overlay_set_custom_line (overlay, "counter", "Count: %d", i);
}
```

## Rendering

### Getting Text

```c
/* Get formatted text for rendering */
g_autofree gchar *text = lrg_debug_overlay_get_text (overlay);

if (!lrg_debug_overlay_is_visible (overlay))
{
    g_assert_cmpstr (text, ==, "");
}

/* Example output:
 * FPS: 60.2
 * Frame: 16.67ms
 * [render] avg: 3.2ms
 * Score: 1000
 * Health: 75%
 */
```

### Line Count

```c
guint lines = lrg_debug_overlay_get_line_count (overlay);

/* Useful for calculating display height */
gint height = lines * font_size + padding * 2;
```

### Rendering Implementation

```c
void render_debug_overlay (LrgDebugOverlay *overlay, RenderContext *ctx)
{
    if (!lrg_debug_overlay_is_visible (overlay))
        return;

    gint x, y;
    gint font_size;

    lrg_debug_overlay_get_position (overlay, &x, &y);
    font_size = lrg_debug_overlay_get_font_size (overlay);

    g_autofree gchar *text = lrg_debug_overlay_get_text (overlay);
    render_text (ctx, text, x, y, font_size);
}
```

## Integration with Profiler

The overlay automatically displays profiler data when enabled:

```c
LrgProfiler *profiler = lrg_profiler_get_default ();
LrgDebugOverlay *overlay = lrg_debug_overlay_get_default ();

lrg_profiler_set_enabled (profiler, TRUE);
lrg_debug_overlay_set_visible (overlay, TRUE);
lrg_debug_overlay_add_flags (overlay, LRG_DEBUG_OVERLAY_PROFILER);

/* Profile a section */
lrg_profiler_begin_section (profiler, "render");
/* rendering code */
lrg_profiler_end_section (profiler, "render");

/* Overlay automatically shows timing data */
g_autofree gchar *text = lrg_debug_overlay_get_text (overlay);
/* Contains: "[render] avg: 3.2ms" */
```

## Complete Example

```c
#include <libregnum.h>

typedef struct
{
    gint score;
    gfloat health;
    gint enemies;
} GameData;

int main (void)
{
    LrgDebugOverlay *overlay = lrg_debug_overlay_get_default ();
    LrgProfiler *profiler = lrg_profiler_get_default ();

    GameData data = { 0, 100.0f, 0 };

    /* Configure overlay */
    lrg_debug_overlay_set_visible (overlay, TRUE);
    lrg_debug_overlay_set_position (overlay, 10, 10);
    lrg_debug_overlay_set_font_size (overlay, 14);
    lrg_debug_overlay_set_padding (overlay, 5);

    /* Enable information display */
    lrg_debug_overlay_set_flags (overlay,
        LRG_DEBUG_OVERLAY_FPS |
        LRG_DEBUG_OVERLAY_FRAME_TIME |
        LRG_DEBUG_OVERLAY_MEMORY |
        LRG_DEBUG_OVERLAY_CUSTOM);

    /* Enable profiling */
    lrg_profiler_set_enabled (profiler, TRUE);

    /* Game loop */
    for (gint frame = 0; frame < 600; frame++)
    {
        lrg_profiler_begin_frame (profiler);

        /* Simulate game */
        lrg_profiler_begin_section (profiler, "update");
        data.score += 10;
        data.health -= 0.1f;
        data.enemies += (frame % 10 == 0) ? 1 : 0;
        lrg_profiler_end_section (profiler, "update");

        /* Update overlay */
        lrg_profiler_begin_section (profiler, "debug");
        lrg_debug_overlay_set_custom_line (overlay, "score",
                                            "Score: %d", data.score);
        lrg_debug_overlay_set_custom_line (overlay, "health",
                                            "Health: %.0f%%", data.health);
        lrg_debug_overlay_set_custom_line (overlay, "enemies",
                                            "Enemies: %d", data.enemies);
        lrg_profiler_end_section (profiler, "debug");

        lrg_profiler_end_frame (profiler);

        /* Render overlay */
        g_autofree gchar *text = lrg_debug_overlay_get_text (overlay);
        g_print ("%s\n", text);

        if (frame % 60 == 0)
            g_print ("---\n");
    }

    return 0;
}
```

## Performance Considerations

- The overlay is only rendered when visible
- Custom lines are stored efficiently
- Text formatting happens on-demand in `lrg_debug_overlay_get_text()`
- Profiler integration has minimal overhead when disabled
