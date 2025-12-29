# LrgPostEffect

`LrgPostEffect` is the base class for all post-processing effects. It defines the interface for initializing, applying, and managing effects.

## Type

```c
#define LRG_TYPE_POST_EFFECT (lrg_post_effect_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgPostEffect, lrg_post_effect, LRG, POST_EFFECT, GObject)
```

This is a derivable type for creating custom effects.

## Virtual Methods

Subclasses can override these methods:

```c
struct _LrgPostEffectClass
{
    GObjectClass parent_class;

    gboolean (*initialize) (LrgPostEffect *self, guint width, guint height, GError **error);
    void     (*shutdown)   (LrgPostEffect *self);
    void     (*apply)      (LrgPostEffect *self, guint source, guint target,
                            guint width, guint height, gfloat delta_time);
    void     (*resize)     (LrgPostEffect *self, guint width, guint height);
    const gchar * (*get_name) (LrgPostEffect *self);
};
```

## Functions

### Lifecycle

```c
gboolean lrg_post_effect_initialize (LrgPostEffect *self, guint width, guint height, GError **error);
void lrg_post_effect_shutdown (LrgPostEffect *self);
gboolean lrg_post_effect_is_initialized (LrgPostEffect *self);
```

### Application

```c
void lrg_post_effect_apply (LrgPostEffect *self, guint source_texture_id,
                            guint target_texture_id, guint width, guint height,
                            gfloat delta_time);
void lrg_post_effect_resize (LrgPostEffect *self, guint width, guint height);
```

### Properties

```c
const gchar *lrg_post_effect_get_name (LrgPostEffect *self);

gboolean lrg_post_effect_is_enabled (LrgPostEffect *self);
void lrg_post_effect_set_enabled (LrgPostEffect *self, gboolean enabled);

gfloat lrg_post_effect_get_intensity (LrgPostEffect *self);
void lrg_post_effect_set_intensity (LrgPostEffect *self, gfloat intensity);

gint lrg_post_effect_get_priority (LrgPostEffect *self);
void lrg_post_effect_set_priority (LrgPostEffect *self, gint priority);
```

## Priority

Effects are applied in priority order (lower numbers first):

| Typical Priority | Effect Type |
|------------------|-------------|
| 0-100 | Scene modifications (color grade) |
| 100-200 | Artistic effects (bloom, vignette) |
| 200-300 | Post effects (film grain) |
| 900+ | Final pass (anti-aliasing) |

## Creating Custom Effects

```c
struct _MyCustomEffect
{
    LrgPostEffect parent;
    guint shader_program;
    /* Custom fields */
};

G_DEFINE_TYPE (MyCustomEffect, my_custom_effect, LRG_TYPE_POST_EFFECT)

static gboolean
my_custom_effect_initialize (LrgPostEffect *effect,
                             guint width,
                             guint height,
                             GError **error)
{
    MyCustomEffect *self = MY_CUSTOM_EFFECT (effect);

    /* Load shader */
    self->shader_program = /* compile shader */;

    return TRUE;
}

static void
my_custom_effect_apply (LrgPostEffect *effect,
                        guint source,
                        guint target,
                        guint width,
                        guint height,
                        gfloat delta_time)
{
    MyCustomEffect *self = MY_CUSTOM_EFFECT (effect);

    /* Bind target framebuffer */
    /* Bind source texture */
    /* Use shader program */
    /* Draw fullscreen quad */
}

static void
my_custom_effect_class_init (MyCustomEffectClass *klass)
{
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);
    effect_class->initialize = my_custom_effect_initialize;
    effect_class->apply = my_custom_effect_apply;
}
```

## Dynamic Enable/Disable

Toggle effects at runtime:

```c
/* Disable bloom during menus */
lrg_post_effect_set_enabled (bloom, FALSE);

/* Fade effect with intensity */
lrg_post_effect_set_intensity (vignette, 0.5f);  /* Half strength */
```
