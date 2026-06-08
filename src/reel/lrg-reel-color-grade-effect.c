/* lrg-reel-color-grade-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-color-grade-effect.h"
#include "lrg-reel-context.h"
#include <raylib.h>

struct _LrgReelColorGradeEffect
{
    LrgReelEffect parent_instance;

    gdouble brightness;
    gdouble contrast;
    gdouble saturation;
};

G_DEFINE_FINAL_TYPE (LrgReelColorGradeEffect, lrg_reel_color_grade_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,
    PROP_SATURATION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static gfloat
clampf (gfloat v,
        gfloat lo,
        gfloat hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_color_grade_effect_apply (LrgReelEffect  *base,
                                    GrlImage       *image,
                                    LrgReelContext *ctx)
{
    LrgReelColorGradeEffect *self;
    Image                   *img;
    guint8                  *data;
    gint                     width;
    gint                     height;
    gint                     n_pixels;
    gint                     i;
    gfloat                   brightness_f;
    gfloat                   contrast_f;
    gfloat                   saturation_f;

    (void) ctx;

    self = LRG_REEL_COLOR_GRADE_EFFECT (base);

    /* Require RGBA8 for raw pixel access. */
    if (grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        return;

    /* Skip identity. */
    if (self->brightness == 0.0 && self->contrast == 1.0 && self->saturation == 1.0)
        return;

    img  = (Image *) grl_image_get_handle (image);
    data = (guint8 *) img->data;

    width    = img->width;
    height   = img->height;
    n_pixels = width * height;

    brightness_f = (gfloat) self->brightness;
    contrast_f   = (gfloat) self->contrast;
    saturation_f = (gfloat) self->saturation;

    for (i = 0; i < n_pixels; i++)
    {
        gfloat r;
        gfloat g;
        gfloat b;
        gfloat luma;

        r = data[i * 4 + 0] / 255.0f;
        g = data[i * 4 + 1] / 255.0f;
        b = data[i * 4 + 2] / 255.0f;
        /* alpha (data[i*4+3]) is left unchanged. */

        /* Contrast: pivot at 0.5 */
        r = (r - 0.5f) * contrast_f + 0.5f;
        g = (g - 0.5f) * contrast_f + 0.5f;
        b = (b - 0.5f) * contrast_f + 0.5f;

        /* Brightness: additive */
        r += brightness_f;
        g += brightness_f;
        b += brightness_f;

        /* Saturation: luma mix. Rec.601 coefficients. */
        luma = 0.299f * r + 0.587f * g + 0.114f * b;
        r = luma + (r - luma) * saturation_f;
        g = luma + (g - luma) * saturation_f;
        b = luma + (b - luma) * saturation_f;

        /* Clamp and write back. */
        data[i * 4 + 0] = (guint8) (clampf (r, 0.0f, 1.0f) * 255.0f + 0.5f);
        data[i * 4 + 1] = (guint8) (clampf (g, 0.0f, 1.0f) * 255.0f + 0.5f);
        data[i * 4 + 2] = (guint8) (clampf (b, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_color_grade_effect_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    LrgReelColorGradeEffect *self = LRG_REEL_COLOR_GRADE_EFFECT (object);

    switch (prop_id)
    {
    case PROP_BRIGHTNESS:
        g_value_set_double (value, self->brightness);
        break;
    case PROP_CONTRAST:
        g_value_set_double (value, self->contrast);
        break;
    case PROP_SATURATION:
        g_value_set_double (value, self->saturation);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_color_grade_effect_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    LrgReelColorGradeEffect *self = LRG_REEL_COLOR_GRADE_EFFECT (object);

    switch (prop_id)
    {
    case PROP_BRIGHTNESS:
        lrg_reel_color_grade_effect_set_brightness (self, g_value_get_double (value));
        break;
    case PROP_CONTRAST:
        lrg_reel_color_grade_effect_set_contrast (self, g_value_get_double (value));
        break;
    case PROP_SATURATION:
        lrg_reel_color_grade_effect_set_saturation (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_color_grade_effect_class_init (LrgReelColorGradeEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_color_grade_effect_get_property;
    object_class->set_property = lrg_reel_color_grade_effect_set_property;

    effect_class->apply = lrg_reel_color_grade_effect_apply;

    /**
     * LrgReelColorGradeEffect:brightness:
     *
     * Additive brightness offset applied to each channel after contrast.
     * Range [-1, 1]; 0 = no change.
     *
     * Since: 1.0
     */
    properties[PROP_BRIGHTNESS] =
        g_param_spec_double ("brightness",
                             "Brightness",
                             "Additive brightness offset [-1, 1]",
                             -1.0, 1.0, 0.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelColorGradeEffect:contrast:
     *
     * Contrast multiplier applied around the 0.5 midpoint.
     * Range [0.5, 2]; 1 = no change.
     *
     * Since: 1.0
     */
    properties[PROP_CONTRAST] =
        g_param_spec_double ("contrast",
                             "Contrast",
                             "Contrast multiplier around 0.5 [0.5, 2]",
                             0.5, 2.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelColorGradeEffect:saturation:
     *
     * Saturation multiplier applied via luma mixing.
     * Range [0, 2]; 1 = no change; 0 = grayscale.
     *
     * Since: 1.0
     */
    properties[PROP_SATURATION] =
        g_param_spec_double ("saturation",
                             "Saturation",
                             "Saturation multiplier [0, 2]",
                             0.0, 2.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_color_grade_effect_init (LrgReelColorGradeEffect *self)
{
    self->brightness = 0.0;
    self->contrast   = 1.0;
    self->saturation = 1.0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_color_grade_effect_new:
 *
 * Creates a new #LrgReelColorGradeEffect with identity settings.
 *
 * Returns: (transfer full): a new #LrgReelColorGradeEffect
 *
 * Since: 1.0
 */
LrgReelColorGradeEffect *
lrg_reel_color_grade_effect_new (void)
{
    return g_object_new (LRG_TYPE_REEL_COLOR_GRADE_EFFECT, NULL);
}

/**
 * lrg_reel_color_grade_effect_get_brightness:
 * @self: a #LrgReelColorGradeEffect
 *
 * Returns: the brightness offset
 *
 * Since: 1.0
 */
gdouble
lrg_reel_color_grade_effect_get_brightness (LrgReelColorGradeEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_COLOR_GRADE_EFFECT (self), 0.0);

    return self->brightness;
}

/**
 * lrg_reel_color_grade_effect_set_brightness:
 * @self: a #LrgReelColorGradeEffect
 * @brightness: new brightness offset [-1, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_color_grade_effect_set_brightness (LrgReelColorGradeEffect *self,
                                             gdouble                  brightness)
{
    g_return_if_fail (LRG_IS_REEL_COLOR_GRADE_EFFECT (self));

    if (self->brightness == brightness)
        return;

    self->brightness = brightness;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BRIGHTNESS]);
}

/**
 * lrg_reel_color_grade_effect_get_contrast:
 * @self: a #LrgReelColorGradeEffect
 *
 * Returns: the contrast multiplier
 *
 * Since: 1.0
 */
gdouble
lrg_reel_color_grade_effect_get_contrast (LrgReelColorGradeEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_COLOR_GRADE_EFFECT (self), 1.0);

    return self->contrast;
}

/**
 * lrg_reel_color_grade_effect_set_contrast:
 * @self: a #LrgReelColorGradeEffect
 * @contrast: new contrast multiplier [0.5, 2]
 *
 * Since: 1.0
 */
void
lrg_reel_color_grade_effect_set_contrast (LrgReelColorGradeEffect *self,
                                           gdouble                  contrast)
{
    g_return_if_fail (LRG_IS_REEL_COLOR_GRADE_EFFECT (self));

    if (self->contrast == contrast)
        return;

    self->contrast = contrast;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTRAST]);
}

/**
 * lrg_reel_color_grade_effect_get_saturation:
 * @self: a #LrgReelColorGradeEffect
 *
 * Returns: the saturation multiplier
 *
 * Since: 1.0
 */
gdouble
lrg_reel_color_grade_effect_get_saturation (LrgReelColorGradeEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_COLOR_GRADE_EFFECT (self), 1.0);

    return self->saturation;
}

/**
 * lrg_reel_color_grade_effect_set_saturation:
 * @self: a #LrgReelColorGradeEffect
 * @saturation: new saturation multiplier [0, 2]
 *
 * Since: 1.0
 */
void
lrg_reel_color_grade_effect_set_saturation (LrgReelColorGradeEffect *self,
                                             gdouble                  saturation)
{
    g_return_if_fail (LRG_IS_REEL_COLOR_GRADE_EFFECT (self));

    if (self->saturation == saturation)
        return;

    self->saturation = saturation;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SATURATION]);
}
