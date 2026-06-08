/* lrg-reel-vignette-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-vignette-effect.h"
#include "lrg-reel-context.h"
#include <math.h>
#include <raylib.h>

struct _LrgReelVignetteEffect
{
    LrgReelEffect parent_instance;

    gdouble intensity;
    gdouble radius;
};

G_DEFINE_FINAL_TYPE (LrgReelVignetteEffect, lrg_reel_vignette_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_INTENSITY,
    PROP_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/*
 * smoothstep(edge0, edge1, x): Hermite interpolation between 0 and 1.
 * Returns 0 when x <= edge0, 1 when x >= edge1.
 */
static gfloat
smoothstep (gfloat edge0,
            gfloat edge1,
            gfloat x)
{
    gfloat t;

    if (x <= edge0) return 0.0f;
    if (x >= edge1) return 1.0f;

    t = (x - edge0) / (edge1 - edge0);
    return t * t * (3.0f - 2.0f * t);
}

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
lrg_reel_vignette_effect_apply (LrgReelEffect  *base,
                                  GrlImage       *image,
                                  LrgReelContext *ctx)
{
    LrgReelVignetteEffect *self;
    Image                 *img;
    guint8                *data;
    gint                   width;
    gint                   height;
    gfloat                 cx;
    gfloat                 cy;
    gfloat                 half_diag;
    gfloat                 intensity_f;
    gfloat                 radius_f;
    gint                   y;

    (void) ctx;

    self = LRG_REEL_VIGNETTE_EFFECT (base);

    if (self->intensity <= 0.0)
        return;

    if (grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        return;

    img    = (Image *) grl_image_get_handle (image);
    data   = (guint8 *) img->data;
    width  = img->width;
    height = img->height;

    cx          = width  * 0.5f;
    cy          = height * 0.5f;
    half_diag   = sqrtf (cx * cx + cy * cy);
    intensity_f = (gfloat) self->intensity;
    radius_f    = (gfloat) self->radius;

    for (y = 0; y < height; y++)
    {
        gint x;

        for (x = 0; x < width; x++)
        {
            gfloat  dx;
            gfloat  dy;
            gfloat  d;
            gfloat  factor;
            gfloat  r;
            gfloat  g;
            gfloat  b;
            gint    idx;

            idx = (y * width + x) * 4;

            dx = (x - cx) / half_diag;
            dy = (y - cy) / half_diag;
            d  = sqrtf (dx * dx + dy * dy);

            /* factor = 1 - intensity * smoothstep(radius, 1, d) */
            factor = 1.0f - intensity_f * smoothstep (radius_f, 1.0f, d);

            r = data[idx + 0] * factor;
            g = data[idx + 1] * factor;
            b = data[idx + 2] * factor;

            data[idx + 0] = (guint8) clampf (r, 0.0f, 255.0f);
            data[idx + 1] = (guint8) clampf (g, 0.0f, 255.0f);
            data[idx + 2] = (guint8) clampf (b, 0.0f, 255.0f);
            /* alpha unchanged */
        }
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_vignette_effect_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgReelVignetteEffect *self = LRG_REEL_VIGNETTE_EFFECT (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        g_value_set_double (value, self->intensity);
        break;
    case PROP_RADIUS:
        g_value_set_double (value, self->radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_vignette_effect_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgReelVignetteEffect *self = LRG_REEL_VIGNETTE_EFFECT (object);

    switch (prop_id)
    {
    case PROP_INTENSITY:
        lrg_reel_vignette_effect_set_intensity (self, g_value_get_double (value));
        break;
    case PROP_RADIUS:
        lrg_reel_vignette_effect_set_radius (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_vignette_effect_class_init (LrgReelVignetteEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_vignette_effect_get_property;
    object_class->set_property = lrg_reel_vignette_effect_set_property;

    effect_class->apply = lrg_reel_vignette_effect_apply;

    /**
     * LrgReelVignetteEffect:intensity:
     *
     * Maximum darkening strength at the image corners [0, 1].
     *
     * Since: 1.0
     */
    properties[PROP_INTENSITY] =
        g_param_spec_double ("intensity",
                             "Intensity",
                             "Darkening strength at corners [0, 1]",
                             0.0, 1.0, 0.5,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelVignetteEffect:radius:
     *
     * Normalized inner radius (fraction of the half-diagonal) where
     * darkening begins [0, 1]. Default 0.6.
     *
     * Since: 1.0
     */
    properties[PROP_RADIUS] =
        g_param_spec_double ("radius",
                             "Radius",
                             "Inner radius fraction where darkening starts [0, 1]",
                             0.0, 1.0, 0.6,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_vignette_effect_init (LrgReelVignetteEffect *self)
{
    self->intensity = 0.5;
    self->radius    = 0.6;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_vignette_effect_new:
 *
 * Creates a new #LrgReelVignetteEffect.
 *
 * Returns: (transfer full): a new #LrgReelVignetteEffect
 *
 * Since: 1.0
 */
LrgReelVignetteEffect *
lrg_reel_vignette_effect_new (void)
{
    return g_object_new (LRG_TYPE_REEL_VIGNETTE_EFFECT, NULL);
}

/**
 * lrg_reel_vignette_effect_get_intensity:
 * @self: a #LrgReelVignetteEffect
 *
 * Returns: the vignette intensity
 *
 * Since: 1.0
 */
gdouble
lrg_reel_vignette_effect_get_intensity (LrgReelVignetteEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIGNETTE_EFFECT (self), 0.0);

    return self->intensity;
}

/**
 * lrg_reel_vignette_effect_set_intensity:
 * @self: a #LrgReelVignetteEffect
 * @intensity: darkening strength [0, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_vignette_effect_set_intensity (LrgReelVignetteEffect *self,
                                         gdouble                intensity)
{
    g_return_if_fail (LRG_IS_REEL_VIGNETTE_EFFECT (self));

    if (self->intensity == intensity)
        return;

    self->intensity = intensity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

/**
 * lrg_reel_vignette_effect_get_radius:
 * @self: a #LrgReelVignetteEffect
 *
 * Returns: the inner radius
 *
 * Since: 1.0
 */
gdouble
lrg_reel_vignette_effect_get_radius (LrgReelVignetteEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIGNETTE_EFFECT (self), 0.0);

    return self->radius;
}

/**
 * lrg_reel_vignette_effect_set_radius:
 * @self: a #LrgReelVignetteEffect
 * @radius: inner radius fraction [0, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_vignette_effect_set_radius (LrgReelVignetteEffect *self,
                                      gdouble                radius)
{
    g_return_if_fail (LRG_IS_REEL_VIGNETTE_EFFECT (self));

    if (self->radius == radius)
        return;

    self->radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}
