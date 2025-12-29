# LrgPostProcessor

`LrgPostProcessor` manages the post-processing pipeline, handling render targets, effect chains, and the capture/render workflow.

## Type

```c
#define LRG_TYPE_POST_PROCESSOR (lrg_post_processor_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgPostProcessor, lrg_post_processor, LRG, POST_PROCESSOR, GObject)
```

## Functions

### Creation

```c
LrgPostProcessor *lrg_post_processor_new (guint width, guint height);
```

Creates a processor with render targets of the specified size.

### Effect Management

```c
void lrg_post_processor_add_effect (LrgPostProcessor *self, LrgPostEffect *effect);
void lrg_post_processor_remove_effect (LrgPostProcessor *self, LrgPostEffect *effect);
LrgPostEffect *lrg_post_processor_get_effect (LrgPostProcessor *self, const gchar *name);
GList *lrg_post_processor_get_effects (LrgPostProcessor *self);
guint lrg_post_processor_get_effect_count (LrgPostProcessor *self);
void lrg_post_processor_clear_effects (LrgPostProcessor *self);
void lrg_post_processor_sort_effects (LrgPostProcessor *self);
```

### Render Pipeline

```c
void lrg_post_processor_begin_capture (LrgPostProcessor *self);
void lrg_post_processor_end_capture (LrgPostProcessor *self);
void lrg_post_processor_render (LrgPostProcessor *self, gfloat delta_time);
```

### Properties

```c
guint lrg_post_processor_get_width (LrgPostProcessor *self);
guint lrg_post_processor_get_height (LrgPostProcessor *self);
void lrg_post_processor_resize (LrgPostProcessor *self, guint width, guint height);

gboolean lrg_post_processor_is_enabled (LrgPostProcessor *self);
void lrg_post_processor_set_enabled (LrgPostProcessor *self, gboolean enabled);

gboolean lrg_post_processor_is_capturing (LrgPostProcessor *self);
```

## Render Workflow

The typical usage pattern in a game loop:

```c
void
game_render (gfloat delta_time)
{
    /* 1. Start capture */
    lrg_post_processor_begin_capture (processor);

    /* 2. Render scene normally */
    render_background ();
    render_entities ();
    render_particles ();
    render_ui ();

    /* 3. End capture */
    lrg_post_processor_end_capture (processor);

    /* 4. Apply effects and output to screen */
    lrg_post_processor_render (processor, delta_time);
}
```

## Handling Window Resize

```c
static void
on_window_resize (guint new_width, guint new_height)
{
    /* Resize render targets */
    lrg_post_processor_resize (processor, new_width, new_height);
}
```

## Effect Priority and Ordering

Effects are applied in priority order. After changing priorities, re-sort:

```c
lrg_post_effect_set_priority (bloom, 50);
lrg_post_effect_set_priority (color_grade, 10);
lrg_post_effect_set_priority (fxaa, 1000);

/* Re-sort after priority changes */
lrg_post_processor_sort_effects (processor);
```

## Finding Effects by Name

```c
LrgPostEffect *bloom = lrg_post_processor_get_effect (processor, "Bloom");
if (bloom != NULL)
{
    lrg_post_effect_set_intensity (bloom, 0.8f);
}
```

## Bypassing Post-Processing

Disable the entire chain for debugging or performance:

```c
/* Disable all post-processing */
lrg_post_processor_set_enabled (processor, FALSE);

/* Scene renders directly to screen */
```

## Complete Example

```c
static LrgPostProcessor *processor = NULL;
static LrgBloom *bloom = NULL;
static LrgVignette *vignette = NULL;
static LrgScreenShake *shake = NULL;

void
init_post_processing (guint width, guint height)
{
    processor = lrg_post_processor_new (width, height);

    /* Add effects in desired order */
    bloom = lrg_bloom_new ();
    lrg_bloom_set_threshold (bloom, 0.8f);
    lrg_bloom_set_intensity (bloom, 1.2f);
    lrg_post_effect_set_priority (LRG_POST_EFFECT (bloom), 100);
    lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (bloom));

    vignette = lrg_vignette_new ();
    lrg_vignette_set_intensity (vignette, 0.3f);
    lrg_post_effect_set_priority (LRG_POST_EFFECT (vignette), 200);
    lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (vignette));

    shake = lrg_screen_shake_new ();
    lrg_screen_shake_set_max_offset (shake, 10.0f, 10.0f);
    lrg_post_effect_set_priority (LRG_POST_EFFECT (shake), 300);
    lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (shake));
}

void
trigger_explosion (void)
{
    /* Add trauma for screen shake */
    lrg_screen_shake_add_trauma (shake, 0.5f);
}

void
cleanup_post_processing (void)
{
    g_clear_object (&processor);
    /* Effects are owned by processor, no need to free individually */
}
```
