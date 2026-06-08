/* lrg-reel-chroma-key-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-chroma-key-effect.h"
#include "lrg-reel-context.h"
#include <math.h>
#include <raylib.h>

struct _LrgReelChromaKeyEffect
{
    LrgReelEffect parent_instance;

    GrlColor key_color;
    gdouble  threshold;
    gdouble  smoothness;
};

G_DEFINE_FINAL_TYPE (LrgReelChromaKeyEffect, lrg_reel_chroma_key_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_KEY_COLOR,
    PROP_THRESHOLD,
    PROP_SMOOTHNESS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_chroma_key_effect_apply (LrgReelEffect  *base,
                                   GrlImage       *image,
                                   LrgReelContext *ctx)
{
    LrgReelChromaKeyEffect *self;
    Image                  *img;
    guint8                 *data;
    gint                    width;
    gint                    height;
    gint                    n_pixels;
    gfloat                  kr;
    gfloat                  kg;
    gfloat                  kb;
    gfloat                  threshold_f;
    gfloat                  smooth_end;
    gint                    i;

    (void) ctx;

    self = LRG_REEL_CHROMA_KEY_EFFECT (base);

    if (grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        return;

    img      = (Image *) grl_image_get_handle (image);
    data     = (guint8 *) img->data;
    width    = img->width;
    height   = img->height;
    n_pixels = width * height;

    kr          = self->key_color.r / 255.0f;
    kg          = self->key_color.g / 255.0f;
    kb          = self->key_color.b / 255.0f;
    threshold_f = (gfloat) self->threshold;
    smooth_end  = threshold_f + (gfloat) self->smoothness;

    for (i = 0; i < n_pixels; i++)
    {
        gfloat r;
        gfloat g;
        gfloat b;
        gfloat dr;
        gfloat dg;
        gfloat db;
        gfloat dist;
        gfloat alpha_scale;
        gfloat t;

        r = data[i * 4 + 0] / 255.0f;
        g = data[i * 4 + 1] / 255.0f;
        b = data[i * 4 + 2] / 255.0f;

        /* Euclidean distance in normalized RGB space. */
        dr   = r - kr;
        dg   = g - kg;
        db   = b - kb;
        dist = sqrtf (dr * dr + dg * dg + db * db) / 1.732051f; /* normalize by max diag */

        if (dist < threshold_f)
        {
            /* Below threshold: fully transparent. */
            data[i * 4 + 3] = 0;
        }
        else if (dist < smooth_end && self->smoothness > 0.0)
        {
            /* Ramp: partial alpha. */
            t = (dist - threshold_f) / (gfloat) self->smoothness;
            alpha_scale = t * t * (3.0f - 2.0f * t); /* smoothstep */
            data[i * 4 + 3] = (guint8) (data[i * 4 + 3] * alpha_scale + 0.5f);
        }
        /* else: fully opaque, leave alpha unchanged. */
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_chroma_key_effect_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
    LrgReelChromaKeyEffect *self = LRG_REEL_CHROMA_KEY_EFFECT (object);
    GrlColor                tmp;

    switch (prop_id)
    {
    case PROP_KEY_COLOR:
        tmp = self->key_color;
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_THRESHOLD:
        g_value_set_double (value, self->threshold);
        break;
    case PROP_SMOOTHNESS:
        g_value_set_double (value, self->smoothness);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_chroma_key_effect_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
    LrgReelChromaKeyEffect *self = LRG_REEL_CHROMA_KEY_EFFECT (object);

    switch (prop_id)
    {
    case PROP_KEY_COLOR:
        lrg_reel_chroma_key_effect_set_key_color (
            self, (const GrlColor *) g_value_get_boxed (value));
        break;
    case PROP_THRESHOLD:
        lrg_reel_chroma_key_effect_set_threshold (self, g_value_get_double (value));
        break;
    case PROP_SMOOTHNESS:
        lrg_reel_chroma_key_effect_set_smoothness (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_chroma_key_effect_class_init (LrgReelChromaKeyEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_chroma_key_effect_get_property;
    object_class->set_property = lrg_reel_chroma_key_effect_set_property;

    effect_class->apply = lrg_reel_chroma_key_effect_apply;

    /**
     * LrgReelChromaKeyEffect:key-color:
     *
     * The color to key out (make transparent).
     *
     * Since: 1.0
     */
    properties[PROP_KEY_COLOR] =
        g_param_spec_boxed ("key-color",
                            "Key Color",
                            "The color to remove",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelChromaKeyEffect:threshold:
     *
     * Normalized RGB color-distance threshold [0, 1]. Pixels closer than
     * this to the key color are fully cut.
     *
     * Since: 1.0
     */
    properties[PROP_THRESHOLD] =
        g_param_spec_double ("threshold",
                             "Threshold",
                             "Color-distance cut threshold [0, 1]",
                             0.0, 1.0, 0.3,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelChromaKeyEffect:smoothness:
     *
     * Soft edge range added to the threshold [0, 1]. Pixels within
     * [threshold, threshold+smoothness] are ramped to transparent.
     *
     * Since: 1.0
     */
    properties[PROP_SMOOTHNESS] =
        g_param_spec_double ("smoothness",
                             "Smoothness",
                             "Edge-softening range beyond threshold [0, 1]",
                             0.0, 1.0, 0.1,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_chroma_key_effect_init (LrgReelChromaKeyEffect *self)
{
    /* Default key: pure green */
    self->key_color.r = 0;
    self->key_color.g = 255;
    self->key_color.b = 0;
    self->key_color.a = 255;
    self->threshold   = 0.3;
    self->smoothness  = 0.1;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_chroma_key_effect_new:
 * @key: (not nullable): the key color to remove
 *
 * Creates a new #LrgReelChromaKeyEffect.
 *
 * Returns: (transfer full): a new #LrgReelChromaKeyEffect
 *
 * Since: 1.0
 */
LrgReelChromaKeyEffect *
lrg_reel_chroma_key_effect_new (const GrlColor *key)
{
    g_return_val_if_fail (key != NULL, NULL);

    return g_object_new (LRG_TYPE_REEL_CHROMA_KEY_EFFECT,
                         "key-color", key,
                         NULL);
}

/**
 * lrg_reel_chroma_key_effect_get_key_color:
 * @self: a #LrgReelChromaKeyEffect
 * @out_color: (out caller-allocates): return location for the key color
 *
 * Since: 1.0
 */
void
lrg_reel_chroma_key_effect_get_key_color (LrgReelChromaKeyEffect *self,
                                           GrlColor               *out_color)
{
    g_return_if_fail (LRG_IS_REEL_CHROMA_KEY_EFFECT (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->key_color;
}

/**
 * lrg_reel_chroma_key_effect_set_key_color:
 * @self: a #LrgReelChromaKeyEffect
 * @key: (not nullable): new key color
 *
 * Since: 1.0
 */
void
lrg_reel_chroma_key_effect_set_key_color (LrgReelChromaKeyEffect *self,
                                           const GrlColor         *key)
{
    g_return_if_fail (LRG_IS_REEL_CHROMA_KEY_EFFECT (self));
    g_return_if_fail (key != NULL);

    self->key_color = *key;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEY_COLOR]);
}

/**
 * lrg_reel_chroma_key_effect_get_threshold:
 * @self: a #LrgReelChromaKeyEffect
 *
 * Returns: the color-distance threshold
 *
 * Since: 1.0
 */
gdouble
lrg_reel_chroma_key_effect_get_threshold (LrgReelChromaKeyEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CHROMA_KEY_EFFECT (self), 0.0);

    return self->threshold;
}

/**
 * lrg_reel_chroma_key_effect_set_threshold:
 * @self: a #LrgReelChromaKeyEffect
 * @threshold: new threshold [0, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_chroma_key_effect_set_threshold (LrgReelChromaKeyEffect *self,
                                           gdouble                 threshold)
{
    g_return_if_fail (LRG_IS_REEL_CHROMA_KEY_EFFECT (self));

    if (self->threshold == threshold)
        return;

    self->threshold = threshold;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THRESHOLD]);
}

/**
 * lrg_reel_chroma_key_effect_get_smoothness:
 * @self: a #LrgReelChromaKeyEffect
 *
 * Returns: the edge-softening range
 *
 * Since: 1.0
 */
gdouble
lrg_reel_chroma_key_effect_get_smoothness (LrgReelChromaKeyEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CHROMA_KEY_EFFECT (self), 0.0);

    return self->smoothness;
}

/**
 * lrg_reel_chroma_key_effect_set_smoothness:
 * @self: a #LrgReelChromaKeyEffect
 * @smoothness: new smoothness range [0, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_chroma_key_effect_set_smoothness (LrgReelChromaKeyEffect *self,
                                            gdouble                 smoothness)
{
    g_return_if_fail (LRG_IS_REEL_CHROMA_KEY_EFFECT (self));

    if (self->smoothness == smoothness)
        return;

    self->smoothness = smoothness;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTHNESS]);
}
