/* lrg-screen-shake.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-screen-shake.h"
#include <math.h>

/**
 * SECTION:lrg-screen-shake
 * @Title: LrgScreenShake
 * @Short_description: Trauma-based screen shake effect
 *
 * #LrgScreenShake implements a trauma-based camera shake system.
 * Trauma accumulates from events (explosions, impacts) and decays
 * over time. The actual shake intensity is trauma squared, creating
 * a smooth falloff.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgScreenShake *shake = lrg_screen_shake_new ();
 * lrg_screen_shake_set_max_offset (shake, 20.0f, 15.0f);
 * lrg_screen_shake_set_decay (shake, 1.5f);
 *
 * // On explosion:
 * lrg_screen_shake_add_trauma (shake, 0.5f);
 *
 * // In update loop:
 * lrg_screen_shake_update (shake, delta_time);
 * ]|
 */

struct _LrgScreenShake
{
    LrgPostEffect  parent_instance;

    /* Trauma system */
    gfloat         trauma;
    gfloat         decay;

    /* Shake parameters */
    gfloat         max_offset_x;
    gfloat         max_offset_y;
    gfloat         max_rotation;
    gfloat         frequency;

    /* Current state */
    gfloat         time;
    gfloat         current_offset_x;
    gfloat         current_offset_y;
    gfloat         current_rotation;

    /* Perlin noise seeds */
    gfloat         seed_x;
    gfloat         seed_y;
    gfloat         seed_rot;
};

G_DEFINE_TYPE (LrgScreenShake, lrg_screen_shake, LRG_TYPE_POST_EFFECT)

enum
{
    PROP_0,
    PROP_TRAUMA,
    PROP_DECAY,
    PROP_FREQUENCY,
    PROP_MAX_ROTATION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Simple noise function for shake */
static gfloat
noise (gfloat x,
       gfloat seed)
{
    gfloat val;

    val = sinf (x * 12.9898f + seed * 78.233f);
    val = val * 43758.5453f;
    val = val - floorf (val);

    return val * 2.0f - 1.0f;
}

static gboolean
lrg_screen_shake_real_initialize (LrgPostEffect *effect,
                                  guint          width,
                                  guint          height,
                                  GError       **error)
{
    return TRUE;
}

static void
lrg_screen_shake_real_shutdown (LrgPostEffect *effect)
{
    (void)effect;
}

static void
lrg_screen_shake_real_apply (LrgPostEffect *effect,
                             guint          source_texture_id,
                             guint          target_texture_id,
                             guint          width,
                             guint          height,
                             gfloat         delta_time)
{
    /*
     * Apply screen shake as UV offset in shader:
     *
     * uniform vec2 shakeOffset;
     * uniform float shakeRotation;
     * uniform vec2 resolution;
     *
     * void main() {
     *     vec2 uv = gl_FragCoord.xy / resolution;
     *     vec2 center = vec2(0.5, 0.5);
     *
     *     // Apply rotation around center
     *     float s = sin(shakeRotation);
     *     float c = cos(shakeRotation);
     *     uv -= center;
     *     uv = vec2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
     *     uv += center;
     *
     *     // Apply offset
     *     uv += shakeOffset / resolution;
     *
     *     gl_FragColor = texture(texture0, uv);
     * }
     */
    (void)effect;
    (void)source_texture_id;
    (void)target_texture_id;
    (void)width;
    (void)height;
    (void)delta_time;
}

static const gchar *
lrg_screen_shake_real_get_name (LrgPostEffect *effect)
{
    (void)effect;
    return "screen-shake";
}

static void
lrg_screen_shake_real_resize (LrgPostEffect *effect,
                              guint          width,
                              guint          height)
{
    (void)effect;
    (void)width;
    (void)height;
}

static void
lrg_screen_shake_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgScreenShake *self = LRG_SCREEN_SHAKE (object);

    switch (prop_id)
    {
    case PROP_TRAUMA:
        g_value_set_float (value, self->trauma);
        break;
    case PROP_DECAY:
        g_value_set_float (value, self->decay);
        break;
    case PROP_FREQUENCY:
        g_value_set_float (value, self->frequency);
        break;
    case PROP_MAX_ROTATION:
        g_value_set_float (value, self->max_rotation);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_screen_shake_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgScreenShake *self = LRG_SCREEN_SHAKE (object);

    switch (prop_id)
    {
    case PROP_TRAUMA:
        lrg_screen_shake_set_trauma (self, g_value_get_float (value));
        break;
    case PROP_DECAY:
        lrg_screen_shake_set_decay (self, g_value_get_float (value));
        break;
    case PROP_FREQUENCY:
        lrg_screen_shake_set_frequency (self, g_value_get_float (value));
        break;
    case PROP_MAX_ROTATION:
        lrg_screen_shake_set_max_rotation (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_screen_shake_class_init (LrgScreenShakeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgPostEffectClass *effect_class = LRG_POST_EFFECT_CLASS (klass);

    object_class->get_property = lrg_screen_shake_get_property;
    object_class->set_property = lrg_screen_shake_set_property;

    effect_class->initialize = lrg_screen_shake_real_initialize;
    effect_class->shutdown = lrg_screen_shake_real_shutdown;
    effect_class->apply = lrg_screen_shake_real_apply;
    effect_class->resize = lrg_screen_shake_real_resize;
    effect_class->get_name = lrg_screen_shake_real_get_name;

    properties[PROP_TRAUMA] =
        g_param_spec_float ("trauma",
                            "Trauma",
                            "Current trauma level",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_DECAY] =
        g_param_spec_float ("decay",
                            "Decay",
                            "Trauma decay rate per second",
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_FREQUENCY] =
        g_param_spec_float ("frequency",
                            "Frequency",
                            "Shake frequency in Hz",
                            0.1f, 50.0f, 15.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_MAX_ROTATION] =
        g_param_spec_float ("max-rotation",
                            "Max Rotation",
                            "Maximum rotation in degrees",
                            0.0f, 45.0f, 5.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_screen_shake_init (LrgScreenShake *self)
{
    self->trauma = 0.0f;
    self->decay = 1.0f;
    self->max_offset_x = 20.0f;
    self->max_offset_y = 15.0f;
    self->max_rotation = 5.0f;
    self->frequency = 15.0f;
    self->time = 0.0f;
    self->current_offset_x = 0.0f;
    self->current_offset_y = 0.0f;
    self->current_rotation = 0.0f;

    /* Random seeds for different noise patterns */
    self->seed_x = (gfloat)g_random_double () * 1000.0f;
    self->seed_y = (gfloat)g_random_double () * 1000.0f;
    self->seed_rot = (gfloat)g_random_double () * 1000.0f;
}

LrgScreenShake *
lrg_screen_shake_new (void)
{
    return g_object_new (LRG_TYPE_SCREEN_SHAKE, NULL);
}

void
lrg_screen_shake_add_trauma (LrgScreenShake *self,
                             gfloat          amount)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    self->trauma = CLAMP (self->trauma + amount, 0.0f, 1.0f);
}

gfloat
lrg_screen_shake_get_trauma (LrgScreenShake *self)
{
    g_return_val_if_fail (LRG_IS_SCREEN_SHAKE (self), 0.0f);
    return self->trauma;
}

void
lrg_screen_shake_set_trauma (LrgScreenShake *self,
                             gfloat          trauma)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    trauma = CLAMP (trauma, 0.0f, 1.0f);
    if (self->trauma == trauma)
        return;

    self->trauma = trauma;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRAUMA]);
}

gfloat
lrg_screen_shake_get_decay (LrgScreenShake *self)
{
    g_return_val_if_fail (LRG_IS_SCREEN_SHAKE (self), 1.0f);
    return self->decay;
}

void
lrg_screen_shake_set_decay (LrgScreenShake *self,
                            gfloat          decay)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    decay = CLAMP (decay, 0.0f, 10.0f);
    if (self->decay == decay)
        return;

    self->decay = decay;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DECAY]);
}

void
lrg_screen_shake_get_max_offset (LrgScreenShake *self,
                                 gfloat         *x,
                                 gfloat         *y)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    if (x != NULL)
        *x = self->max_offset_x;
    if (y != NULL)
        *y = self->max_offset_y;
}

void
lrg_screen_shake_set_max_offset (LrgScreenShake *self,
                                 gfloat          x,
                                 gfloat          y)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    self->max_offset_x = MAX (0.0f, x);
    self->max_offset_y = MAX (0.0f, y);
}

gfloat
lrg_screen_shake_get_max_rotation (LrgScreenShake *self)
{
    g_return_val_if_fail (LRG_IS_SCREEN_SHAKE (self), 5.0f);
    return self->max_rotation;
}

void
lrg_screen_shake_set_max_rotation (LrgScreenShake *self,
                                   gfloat          degrees)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    degrees = CLAMP (degrees, 0.0f, 45.0f);
    if (self->max_rotation == degrees)
        return;

    self->max_rotation = degrees;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ROTATION]);
}

gfloat
lrg_screen_shake_get_frequency (LrgScreenShake *self)
{
    g_return_val_if_fail (LRG_IS_SCREEN_SHAKE (self), 15.0f);
    return self->frequency;
}

void
lrg_screen_shake_set_frequency (LrgScreenShake *self,
                                gfloat          frequency)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    frequency = CLAMP (frequency, 0.1f, 50.0f);
    if (self->frequency == frequency)
        return;

    self->frequency = frequency;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREQUENCY]);
}

void
lrg_screen_shake_update (LrgScreenShake *self,
                         gfloat          delta_time)
{
    gfloat shake;
    gfloat t;

    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    /* Decay trauma */
    self->trauma = MAX (0.0f, self->trauma - self->decay * delta_time);

    /* Update time */
    self->time += delta_time;
    t = self->time * self->frequency;

    /* Shake intensity is trauma squared for smooth falloff */
    shake = self->trauma * self->trauma;

    /* Calculate current shake values using noise */
    self->current_offset_x = shake * self->max_offset_x * noise (t, self->seed_x);
    self->current_offset_y = shake * self->max_offset_y * noise (t, self->seed_y);
    self->current_rotation = shake * self->max_rotation * noise (t, self->seed_rot);
}

void
lrg_screen_shake_get_current_offset (LrgScreenShake *self,
                                     gfloat         *x,
                                     gfloat         *y)
{
    g_return_if_fail (LRG_IS_SCREEN_SHAKE (self));

    if (x != NULL)
        *x = self->current_offset_x;
    if (y != NULL)
        *y = self->current_offset_y;
}

gfloat
lrg_screen_shake_get_current_rotation (LrgScreenShake *self)
{
    g_return_val_if_fail (LRG_IS_SCREEN_SHAKE (self), 0.0f);
    return self->current_rotation;
}
