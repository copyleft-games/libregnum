/* lrg-text-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-text-effect.h"
#include <math.h>

/**
 * SECTION:lrg-text-effect
 * @Title: LrgTextEffect
 * @Short_description: Animated text effects
 *
 * #LrgTextEffect applies animated visual effects to text characters.
 * Effects can modify character position (offset) and color.
 *
 * Available effect types:
 * - Shake: Random vibration
 * - Wave: Sinusoidal vertical movement
 * - Rainbow: Cycling hue shift
 * - Typewriter: Progressive character reveal
 * - Fade In: Gradual alpha increase
 * - Pulse: Pulsing scale/alpha
 */

typedef struct
{
    LrgTextEffectType effect_type;
    gfloat            speed;
    gfloat            intensity;
    gfloat            time;
    guint             char_count;
    gboolean          complete;

    /* Random state for shake effect */
    guint32           rand_state;
} LrgTextEffectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgTextEffect, lrg_text_effect, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_EFFECT_TYPE,
    PROP_SPEED,
    PROP_INTENSITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Simple pseudo-random number generator for shake effect
 */
static guint32
xorshift32 (guint32 *state)
{
    guint32 x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return x;
}

static gfloat
random_float (guint32 *state)
{
    return (gfloat) (xorshift32 (state) & 0xFFFF) / 65535.0f;
}

/*
 * Effect implementations
 */

static void
apply_shake (LrgTextEffectPrivate *priv,
             guint                 char_index,
             gfloat               *offset_x,
             gfloat               *offset_y)
{
    guint32 state;
    gfloat shake_amount;

    /* Seed based on time and character index for variation */
    state = priv->rand_state + char_index * 12345 + (guint32) (priv->time * 100.0f);
    shake_amount = priv->intensity * 4.0f;

    *offset_x += (random_float (&state) - 0.5f) * shake_amount;
    *offset_y += (random_float (&state) - 0.5f) * shake_amount;
}

static void
apply_wave (LrgTextEffectPrivate *priv,
            guint                 char_index,
            gfloat               *offset_y)
{
    gfloat phase;
    gfloat wave_height;

    phase = priv->time * priv->speed * 4.0f + (gfloat) char_index * 0.3f;
    wave_height = priv->intensity * 6.0f;

    *offset_y += sinf (phase) * wave_height;
}

static void
apply_rainbow (LrgTextEffectPrivate *priv,
               guint                 char_index,
               guint8               *r,
               guint8               *g,
               guint8               *b)
{
    gfloat hue;
    gfloat kr, kg, kb;

    /* Calculate hue based on time and character offset */
    hue = fmodf (priv->time * priv->speed + (gfloat) char_index * 0.1f, 1.0f);

    /*
     * Convert hue to RGB using simplified HSV->RGB.
     * Assuming saturation=1, value=1
     */
    kr = fabsf (hue * 6.0f - 3.0f) - 1.0f;
    kg = 2.0f - fabsf (hue * 6.0f - 2.0f);
    kb = 2.0f - fabsf (hue * 6.0f - 4.0f);

    kr = CLAMP (kr, 0.0f, 1.0f);
    kg = CLAMP (kg, 0.0f, 1.0f);
    kb = CLAMP (kb, 0.0f, 1.0f);

    /* Blend with original color based on intensity */
    *r = (guint8) ((1.0f - priv->intensity) * (*r) + priv->intensity * kr * 255.0f);
    *g = (guint8) ((1.0f - priv->intensity) * (*g) + priv->intensity * kg * 255.0f);
    *b = (guint8) ((1.0f - priv->intensity) * (*b) + priv->intensity * kb * 255.0f);
}

static void
apply_typewriter (LrgTextEffectPrivate *priv,
                  guint                 char_index,
                  guint8               *a)
{
    gfloat chars_revealed;
    guint revealed_count;

    /* Characters per second */
    chars_revealed = priv->time * priv->speed * 20.0f;
    revealed_count = (guint) chars_revealed;

    if (char_index >= revealed_count)
        *a = 0;

    if (revealed_count >= priv->char_count)
        priv->complete = TRUE;
}

static void
apply_fade_in (LrgTextEffectPrivate *priv,
               guint                 char_index,
               guint8               *a)
{
    gfloat char_delay;
    gfloat char_time;
    gfloat fade_progress;

    /* Stagger fade based on character index */
    char_delay = (gfloat) char_index * 0.05f / priv->speed;
    char_time = priv->time - char_delay;

    if (char_time < 0.0f)
    {
        *a = 0;
        return;
    }

    /* Fade over 0.5 seconds adjusted by speed */
    fade_progress = char_time * priv->speed * 2.0f;
    fade_progress = CLAMP (fade_progress, 0.0f, 1.0f);

    *a = (guint8) (fade_progress * priv->intensity * (*a));

    if (priv->char_count > 0)
    {
        gfloat total_time;

        total_time = (gfloat) priv->char_count * 0.05f / priv->speed + 0.5f / priv->speed;
        if (priv->time >= total_time)
            priv->complete = TRUE;
    }
}

static void
apply_pulse (LrgTextEffectPrivate *priv,
             guint                 char_index,
             guint8               *a)
{
    gfloat phase;
    gfloat pulse;

    (void) char_index;

    phase = priv->time * priv->speed * 4.0f;
    pulse = (sinf (phase) + 1.0f) * 0.5f;

    /* Pulse between intensity and 1.0 */
    pulse = (1.0f - priv->intensity) + priv->intensity * pulse;

    *a = (guint8) (pulse * (*a));
}

/*
 * Virtual method implementations
 */

static void
lrg_text_effect_real_update (LrgTextEffect *effect,
                             gfloat         delta_time)
{
    LrgTextEffectPrivate *priv;

    priv = lrg_text_effect_get_instance_private (effect);
    priv->time += delta_time;

    /* Update random state each frame for shake */
    priv->rand_state = (guint32) (priv->time * 1000.0f);

    /* Check completion for time-based effects */
    if (!priv->complete && priv->char_count > 0)
    {
        switch (priv->effect_type)
        {
        case LRG_TEXT_EFFECT_TYPEWRITER:
            {
                gfloat chars_revealed;

                chars_revealed = priv->time * priv->speed * 20.0f;
                if ((guint) chars_revealed >= priv->char_count)
                    priv->complete = TRUE;
            }
            break;

        case LRG_TEXT_EFFECT_FADE_IN:
            {
                gfloat total_time;

                total_time = (gfloat) priv->char_count * 0.05f / priv->speed
                           + 0.5f / priv->speed;
                if (priv->time >= total_time)
                    priv->complete = TRUE;
            }
            break;

        default:
            break;
        }
    }
}

static void
lrg_text_effect_real_apply (LrgTextEffect *effect,
                            guint          char_index,
                            gfloat        *offset_x,
                            gfloat        *offset_y,
                            guint8        *r,
                            guint8        *g,
                            guint8        *b,
                            guint8        *a)
{
    LrgTextEffectPrivate *priv;

    priv = lrg_text_effect_get_instance_private (effect);

    switch (priv->effect_type)
    {
    case LRG_TEXT_EFFECT_NONE:
        break;

    case LRG_TEXT_EFFECT_SHAKE:
        apply_shake (priv, char_index, offset_x, offset_y);
        break;

    case LRG_TEXT_EFFECT_WAVE:
        apply_wave (priv, char_index, offset_y);
        break;

    case LRG_TEXT_EFFECT_RAINBOW:
        apply_rainbow (priv, char_index, r, g, b);
        break;

    case LRG_TEXT_EFFECT_TYPEWRITER:
        apply_typewriter (priv, char_index, a);
        break;

    case LRG_TEXT_EFFECT_FADE_IN:
        apply_fade_in (priv, char_index, a);
        break;

    case LRG_TEXT_EFFECT_PULSE:
        apply_pulse (priv, char_index, a);
        break;

    case LRG_TEXT_EFFECT_CUSTOM:
        /* Custom effects should override the apply virtual method */
        break;
    }
}

static void
lrg_text_effect_real_reset (LrgTextEffect *effect)
{
    LrgTextEffectPrivate *priv;

    priv = lrg_text_effect_get_instance_private (effect);
    priv->time = 0.0f;
    priv->complete = FALSE;
    priv->rand_state = 42;
}

static void
lrg_text_effect_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgTextEffect *self = LRG_TEXT_EFFECT (object);
    LrgTextEffectPrivate *priv;

    priv = lrg_text_effect_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_EFFECT_TYPE:
        g_value_set_enum (value, priv->effect_type);
        break;
    case PROP_SPEED:
        g_value_set_float (value, priv->speed);
        break;
    case PROP_INTENSITY:
        g_value_set_float (value, priv->intensity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_text_effect_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgTextEffect *self = LRG_TEXT_EFFECT (object);
    LrgTextEffectPrivate *priv;

    priv = lrg_text_effect_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_EFFECT_TYPE:
        priv->effect_type = g_value_get_enum (value);
        break;
    case PROP_SPEED:
        priv->speed = g_value_get_float (value);
        break;
    case PROP_INTENSITY:
        priv->intensity = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_text_effect_class_init (LrgTextEffectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_text_effect_get_property;
    object_class->set_property = lrg_text_effect_set_property;

    klass->update = lrg_text_effect_real_update;
    klass->apply = lrg_text_effect_real_apply;
    klass->reset = lrg_text_effect_real_reset;

    properties[PROP_EFFECT_TYPE] =
        g_param_spec_enum ("effect-type", "Effect Type", "The effect type",
                           LRG_TYPE_TEXT_EFFECT_TYPE, LRG_TEXT_EFFECT_NONE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    properties[PROP_SPEED] =
        g_param_spec_float ("speed", "Speed", "Animation speed",
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity", "Intensity", "Effect intensity",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_text_effect_init (LrgTextEffect *self)
{
    LrgTextEffectPrivate *priv;

    priv = lrg_text_effect_get_instance_private (self);
    priv->effect_type = LRG_TEXT_EFFECT_NONE;
    priv->speed = 1.0f;
    priv->intensity = 1.0f;
    priv->time = 0.0f;
    priv->char_count = 0;
    priv->complete = FALSE;
    priv->rand_state = 42;
}

/*
 * Public API
 */

LrgTextEffect *
lrg_text_effect_new (LrgTextEffectType effect_type)
{
    return g_object_new (LRG_TYPE_TEXT_EFFECT,
                         "effect-type", effect_type,
                         NULL);
}

LrgTextEffectType
lrg_text_effect_get_effect_type (LrgTextEffect *effect)
{
    LrgTextEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_TEXT_EFFECT (effect), LRG_TEXT_EFFECT_NONE);

    priv = lrg_text_effect_get_instance_private (effect);
    return priv->effect_type;
}

gfloat
lrg_text_effect_get_speed (LrgTextEffect *effect)
{
    LrgTextEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_TEXT_EFFECT (effect), 1.0f);

    priv = lrg_text_effect_get_instance_private (effect);
    return priv->speed;
}

void
lrg_text_effect_set_speed (LrgTextEffect *effect,
                           gfloat         speed)
{
    LrgTextEffectPrivate *priv;

    g_return_if_fail (LRG_IS_TEXT_EFFECT (effect));

    priv = lrg_text_effect_get_instance_private (effect);

    if (priv->speed != speed)
    {
        priv->speed = speed;
        g_object_notify_by_pspec (G_OBJECT (effect), properties[PROP_SPEED]);
    }
}

gfloat
lrg_text_effect_get_intensity (LrgTextEffect *effect)
{
    LrgTextEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_TEXT_EFFECT (effect), 1.0f);

    priv = lrg_text_effect_get_instance_private (effect);
    return priv->intensity;
}

void
lrg_text_effect_set_intensity (LrgTextEffect *effect,
                               gfloat         intensity)
{
    LrgTextEffectPrivate *priv;

    g_return_if_fail (LRG_IS_TEXT_EFFECT (effect));

    priv = lrg_text_effect_get_instance_private (effect);
    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (priv->intensity != intensity)
    {
        priv->intensity = intensity;
        g_object_notify_by_pspec (G_OBJECT (effect), properties[PROP_INTENSITY]);
    }
}

void
lrg_text_effect_update (LrgTextEffect *effect,
                        gfloat         delta_time)
{
    LrgTextEffectClass *klass;

    g_return_if_fail (LRG_IS_TEXT_EFFECT (effect));

    klass = LRG_TEXT_EFFECT_GET_CLASS (effect);
    if (klass->update != NULL)
        klass->update (effect, delta_time);
}

void
lrg_text_effect_apply (LrgTextEffect *effect,
                       guint          char_index,
                       gfloat        *offset_x,
                       gfloat        *offset_y,
                       guint8        *r,
                       guint8        *g,
                       guint8        *b,
                       guint8        *a)
{
    LrgTextEffectClass *klass;

    g_return_if_fail (LRG_IS_TEXT_EFFECT (effect));

    klass = LRG_TEXT_EFFECT_GET_CLASS (effect);
    if (klass->apply != NULL)
        klass->apply (effect, char_index, offset_x, offset_y, r, g, b, a);
}

void
lrg_text_effect_reset (LrgTextEffect *effect)
{
    LrgTextEffectClass *klass;

    g_return_if_fail (LRG_IS_TEXT_EFFECT (effect));

    klass = LRG_TEXT_EFFECT_GET_CLASS (effect);
    if (klass->reset != NULL)
        klass->reset (effect);
}

gfloat
lrg_text_effect_get_time (LrgTextEffect *effect)
{
    LrgTextEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_TEXT_EFFECT (effect), 0.0f);

    priv = lrg_text_effect_get_instance_private (effect);
    return priv->time;
}

gboolean
lrg_text_effect_is_complete (LrgTextEffect *effect)
{
    LrgTextEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_TEXT_EFFECT (effect), TRUE);

    priv = lrg_text_effect_get_instance_private (effect);
    return priv->complete;
}

void
lrg_text_effect_set_char_count (LrgTextEffect *effect,
                                guint          count)
{
    LrgTextEffectPrivate *priv;

    g_return_if_fail (LRG_IS_TEXT_EFFECT (effect));

    priv = lrg_text_effect_get_instance_private (effect);
    priv->char_count = count;
}
