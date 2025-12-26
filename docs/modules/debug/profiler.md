# LrgProfiler

Performance profiling system that measures execution time of code sections and provides statistics.

## Overview

`LrgProfiler` tracks timing of named code sections and provides statistics (min/max/average) for performance analysis. It stores samples per section and supports frame-level aggregation.

## Singleton Access

```c
LrgProfiler *profiler = lrg_profiler_get_default ();

/* Or create new instance */
LrgProfiler *profiler = lrg_profiler_new ();
```

## Enabling/Disabling

```c
/* Check if enabled */
gboolean enabled = lrg_profiler_is_enabled (profiler);

/* Enable profiling */
lrg_profiler_set_enabled (profiler, TRUE);

/* Disable profiling (no overhead when disabled) */
lrg_profiler_set_enabled (profiler, FALSE);
```

## Section Timing

### Basic Usage

```c
lrg_profiler_set_enabled (profiler, TRUE);

/* Time a code section */
lrg_profiler_begin_section (profiler, "my-operation");

/* Code to time */
do_expensive_work ();

lrg_profiler_end_section (profiler, "my-operation");
```

### Nested Sections

```c
lrg_profiler_begin_section (profiler, "update");
{
    lrg_profiler_begin_section (profiler, "input");
    process_input ();
    lrg_profiler_end_section (profiler, "input");

    lrg_profiler_begin_section (profiler, "physics");
    update_physics ();
    lrg_profiler_end_section (profiler, "physics");
}
lrg_profiler_end_section (profiler, "update");
```

## Frame Management

### Frame Boundaries

```c
for (gint frame = 0; frame < num_frames; frame++)
{
    lrg_profiler_begin_frame (profiler);

    /* All sections timed in this frame */
    lrg_profiler_begin_section (profiler, "update");
    update ();
    lrg_profiler_end_section (profiler, "update");

    lrg_profiler_end_frame (profiler);
}
```

Calling `begin_frame()` resets per-frame statistics, and `end_frame()` finalizes frame timing.

## Statistics

### Sample Count

```c
guint count = lrg_profiler_get_sample_count (profiler, "my-operation");
g_print ("Section 'my-operation' was called %u times\n", count);
```

### Timing Statistics

```c
gdouble avg_ms = lrg_profiler_get_average_ms (profiler, "render");
gdouble min_ms = lrg_profiler_get_min_ms (profiler, "render");
gdouble max_ms = lrg_profiler_get_max_ms (profiler, "render");

g_print ("Render timing: avg=%.2fms, min=%.2fms, max=%.2fms\n",
         avg_ms, min_ms, max_ms);
```

### Frame Timing

```c
gdouble frame_time = lrg_profiler_get_frame_time_ms (profiler);
gdouble fps = lrg_profiler_get_fps (profiler);

g_print ("FPS: %.1f (%.2fms per frame)\n", fps, frame_time);
```

### Section Names

```c
GList *sections = lrg_profiler_get_section_names (profiler);

for (GList *l = sections; l != NULL; l = l->next)
{
    const gchar *name = (const gchar *)l->data;
    gdouble avg = lrg_profiler_get_average_ms (profiler, name);
    g_print ("%s: %.2fms\n", name, avg);
}

g_list_free (sections);
```

## Sample Access

### Getting Last Sample

```c
LrgProfilerSample *sample = lrg_profiler_get_last_sample (profiler, "render");

if (sample != NULL)
{
    const gchar *name = lrg_profiler_sample_get_name (sample);
    gint64 us = lrg_profiler_sample_get_duration_us (sample);
    gdouble ms = lrg_profiler_sample_get_duration_ms (sample);

    g_print ("Last %s: %ldus (%.2fms)\n", name, us, ms);

    lrg_profiler_sample_free (sample);
}
```

## Configuration

### Max Samples Per Section

```c
/* Default is 60 (one second at 60 FPS) */
guint max = lrg_profiler_get_max_samples (profiler);

/* Keep more history */
lrg_profiler_set_max_samples (profiler, 300);  /* 5 seconds at 60 FPS */

/* Minimum is 1 */
```

## Clearing Data

### Clear Everything

```c
lrg_profiler_clear (profiler);

/* All sections and frame data cleared */
```

### Clear Specific Section

```c
lrg_profiler_clear_section (profiler, "old-section");

/* Only "old-section" data is cleared */
```

## Complete Example

```c
#include <libregnum.h>

typedef struct
{
    LrgPhysicsWorld *physics;
    LrgWorld        *world;
    LrgProfiler     *profiler;
} Game;

void update_game (Game *game, gfloat delta_time)
{
    LrgProfiler *p = game->profiler;

    lrg_profiler_begin_frame (p);

    /* Update phase */
    lrg_profiler_begin_section (p, "update");
    {
        lrg_profiler_begin_section (p, "input");
        process_input ();
        lrg_profiler_end_section (p, "input");

        lrg_profiler_begin_section (p, "physics");
        lrg_physics_world_step (game->physics, delta_time);
        lrg_profiler_end_section (p, "physics");

        lrg_profiler_begin_section (p, "logic");
        update_game_logic ();
        lrg_profiler_end_section (p, "logic");
    }
    lrg_profiler_end_section (p, "update");

    lrg_profiler_end_frame (p);
}

void print_stats (Game *game)
{
    LrgProfiler *p = game->profiler;

    g_print ("=== Performance Stats ===\n");
    g_print ("FPS: %.1f (%.2fms)\n",
             lrg_profiler_get_fps (p),
             lrg_profiler_get_frame_time_ms (p));

    GList *sections = lrg_profiler_get_section_names (p);
    for (GList *l = sections; l != NULL; l = l->next)
    {
        const gchar *name = (const gchar *)l->data;
        gdouble avg = lrg_profiler_get_average_ms (p, name);
        gdouble min_v = lrg_profiler_get_min_ms (p, name);
        gdouble max_v = lrg_profiler_get_max_ms (p, name);
        guint count = lrg_profiler_get_sample_count (p, name);

        g_print ("[%s] %u samples: %.2f (min: %.2f, max: %.2f) ms\n",
                 name, count, avg, min_v, max_v);
    }
    g_list_free (sections);
}

int main (void)
{
    Game game = {
        .physics = lrg_physics_world_new (),
        .world = lrg_world_new (),
        .profiler = lrg_profiler_get_default ()
    };

    lrg_profiler_set_enabled (game.profiler, TRUE);
    lrg_profiler_set_max_samples (game.profiler, 120);

    /* Run game loop */
    for (gint frame = 0; frame < 600; frame++)  /* 10 seconds at 60 FPS */
    {
        update_game (&game, 1.0f / 60.0f);

        if (frame % 60 == 0)  /* Every second */
        {
            print_stats (&game);
            g_print ("\n");
        }
    }

    g_object_unref (game.profiler);
    g_object_unref (game.physics);
    g_object_unref (game.world);

    return 0;
}
```

## Performance Notes

- Profiling has minimal overhead when disabled
- Section timing is precise (microsecond resolution)
- Sample storage uses circular buffer per section
- Frame aggregation is automatic
- Safe to call from any thread (internally synchronized)
