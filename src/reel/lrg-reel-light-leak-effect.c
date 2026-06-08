/* lrg-reel-light-leak-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-light-leak-effect.h"
#include "lrg-reel-context.h"
#include <math.h>
#include <raylib.h>

/* Period of the leak center drift in frames. */
#define LEAK_DRIFT_PERIOD 120.0f

struct _LrgReelLightLeakEffect
{
    LrgReelEffect parent_instance;

    GrlColor leak_color;
    gdouble  intensity;
};

G_DEFINE_FINAL_TYPE (LrgReelLightLeakEffect, lrg_reel_light_leak_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_COLOR,
    PROP_INTENSITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_light_leak_effect_apply (LrgReelEffect  *base,
                                   GrlImage       *image,
                                   LrgReelContext *ctx)
{
    LrgReelLightLeakEffect *self;
    Image                  *img;
    guint8                 *data;
    gint                    width;
    gint                    height;
    gfloat                  frame_t;
    gfloat                  cx;
    gfloat                  cy;
    gfloat                  half_diag;
    gfloat                  intensity_f;
    gfloat                  cr;
    gfloat                  cg;
    gfloat                  cb;
    gint                    y;

    self = LRG_REEL_LIGHT_LEAK_EFFECT (base);

    if (self->intensity <= 0.0)
        return;

    if (grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        return;

    img    = (Image *) grl_image_get_handle (image);
    data   = (guint8 *) img->data;
    width  = img->width;
    height = img->height;

    /*
     * Animate the leak center by drifting it across the image using a
     * slow sinusoidal path derived from the frame number.  The center
     * moves in a figure-eight pattern bounded within [0.2, 0.8] of the
     * image dimensions so the effect is always partially visible.
     */
    frame_t = (gfloat) lrg_reel_context_get_frame (ctx) / LEAK_DRIFT_PERIOD;
    cx      = ((sinf (frame_t * 2.0f * G_PI)       * 0.3f) + 0.5f) * (gfloat) width;
    cy      = ((sinf (frame_t * 2.0f * G_PI * 0.7f) * 0.3f) + 0.5f) * (gfloat) height;

    /* half_diag: normalises the radial distance to [0, 1] at the corners. */
    half_diag   = sqrtf ((gfloat) width  * (gfloat) width +
                          (gfloat) height * (gfloat) height) * 0.5f;
    intensity_f = (gfloat) self->intensity;
    cr          = self->leak_color.r / 255.0f;
    cg          = self->leak_color.g / 255.0f;
    cb          = self->leak_color.b / 255.0f;

    for (y = 0; y < height; y++)
    {
        gint x;

        for (x = 0; x < width; x++)
        {
            gfloat dx;
            gfloat dy;
            gfloat d;
            gfloat falloff;
            gfloat add_r;
            gfloat add_g;
            gfloat add_b;
            gfloat out_r;
            gfloat out_g;
            gfloat out_b;
            gint   idx;

            idx = (y * width + x) * 4;

            dx      = (gfloat) x - cx;
            dy      = (gfloat) y - cy;
            d       = sqrtf (dx * dx + dy * dy) / half_diag;
            /* Gaussian-like falloff: bright at center, fade to zero. */
            falloff = expf (-d * d * 3.0f) * intensity_f;

            add_r = cr * falloff * 255.0f;
            add_g = cg * falloff * 255.0f;
            add_b = cb * falloff * 255.0f;

            out_r = (gfloat) data[idx + 0] + add_r;
            out_g = (gfloat) data[idx + 1] + add_g;
            out_b = (gfloat) data[idx + 2] + add_b;

            if (out_r > 255.0f) out_r = 255.0f;
            if (out_g > 255.0f) out_g = 255.0f;
            if (out_b > 255.0f) out_b = 255.0f;

            data[idx + 0] = (guint8) out_r;
            data[idx + 1] = (guint8) out_g;
            data[idx + 2] = (guint8) out_b;
            /* alpha unchanged */
        }
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_light_leak_effect_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
    LrgReelLightLeakEffect *self = LRG_REEL_LIGHT_LEAK_EFFECT (object);
    GrlColor                tmp;

    switch (prop_id)
    {
    case PROP_COLOR:
        tmp = self->leak_color;
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_INTENSITY:
        g_value_set_double (value, self->intensity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_light_leak_effect_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
    LrgReelLightLeakEffect *self = LRG_REEL_LIGHT_LEAK_EFFECT (object);

    switch (prop_id)
    {
    case PROP_COLOR:
        lrg_reel_light_leak_effect_set_color (
            self, (const GrlColor *) g_value_get_boxed (value));
        break;
    case PROP_INTENSITY:
        lrg_reel_light_leak_effect_set_intensity (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_light_leak_effect_class_init (LrgReelLightLeakEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_light_leak_effect_get_property;
    object_class->set_property = lrg_reel_light_leak_effect_set_property;

    effect_class->apply = lrg_reel_light_leak_effect_apply;

    /**
     * LrgReelLightLeakEffect:color:
     *
     * The tint color of the light-leak gradient.
     *
     * Since: 1.0
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color",
                            "Color",
                            "Light-leak tint color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelLightLeakEffect:intensity:
     *
     * Additive blend strength of the light-leak gradient [0, 1].
     *
     * Since: 1.0
     */
    properties[PROP_INTENSITY] =
        g_param_spec_double ("intensity",
                             "Intensity",
                             "Additive blend intensity [0, 1]",
                             0.0, 1.0, 0.25,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_light_leak_effect_init (LrgReelLightLeakEffect *self)
{
    /* Default: warm orange. */
    self->leak_color.r = 255;
    self->leak_color.g = 160;
    self->leak_color.b = 64;
    self->leak_color.a = 255;
    self->intensity    = 0.25;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_light_leak_effect_new:
 *
 * Creates a new #LrgReelLightLeakEffect.
 *
 * Returns: (transfer full): a new #LrgReelLightLeakEffect
 *
 * Since: 1.0
 */
LrgReelLightLeakEffect *
lrg_reel_light_leak_effect_new (void)
{
    return g_object_new (LRG_TYPE_REEL_LIGHT_LEAK_EFFECT, NULL);
}

/**
 * lrg_reel_light_leak_effect_get_color:
 * @self: a #LrgReelLightLeakEffect
 * @out_color: (out caller-allocates): return location for the leak color
 *
 * Since: 1.0
 */
void
lrg_reel_light_leak_effect_get_color (LrgReelLightLeakEffect *self,
                                       GrlColor               *out_color)
{
    g_return_if_fail (LRG_IS_REEL_LIGHT_LEAK_EFFECT (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->leak_color;
}

/**
 * lrg_reel_light_leak_effect_set_color:
 * @self: a #LrgReelLightLeakEffect
 * @color: (not nullable): new leak tint color
 *
 * Since: 1.0
 */
void
lrg_reel_light_leak_effect_set_color (LrgReelLightLeakEffect *self,
                                       const GrlColor         *color)
{
    g_return_if_fail (LRG_IS_REEL_LIGHT_LEAK_EFFECT (self));
    g_return_if_fail (color != NULL);

    self->leak_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

/**
 * lrg_reel_light_leak_effect_get_intensity:
 * @self: a #LrgReelLightLeakEffect
 *
 * Returns: the additive blend intensity
 *
 * Since: 1.0
 */
gdouble
lrg_reel_light_leak_effect_get_intensity (LrgReelLightLeakEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_LIGHT_LEAK_EFFECT (self), 0.0);

    return self->intensity;
}

/**
 * lrg_reel_light_leak_effect_set_intensity:
 * @self: a #LrgReelLightLeakEffect
 * @intensity: new intensity [0, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_light_leak_effect_set_intensity (LrgReelLightLeakEffect *self,
                                           gdouble                 intensity)
{
    g_return_if_fail (LRG_IS_REEL_LIGHT_LEAK_EFFECT (self));

    if (self->intensity == intensity)
        return;

    self->intensity = intensity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}
