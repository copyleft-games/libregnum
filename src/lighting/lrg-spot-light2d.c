/* lrg-spot-light2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Spot light implementation.
 */

#include "lrg-spot-light2d.h"
#include "../lrg-log.h"
#include <math.h>
#include <graylib.h>

struct _LrgSpotLight2D
{
    LrgLight2D parent_instance;

    gfloat radius;
    gfloat angle;       /* Cone angle in degrees */
    gfloat inner_angle; /* Inner cone angle */
    gfloat direction;   /* Direction in degrees */
};

G_DEFINE_FINAL_TYPE (LrgSpotLight2D, lrg_spot_light2d, LRG_TYPE_LIGHT2D)

enum
{
    PROP_0,
    PROP_RADIUS,
    PROP_ANGLE,
    PROP_INNER_ANGLE,
    PROP_DIRECTION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_spot_light2d_is_visible (LrgLight2D *light,
                             gfloat      viewport_x,
                             gfloat      viewport_y,
                             gfloat      viewport_width,
                             gfloat      viewport_height)
{
    LrgSpotLight2D *self = LRG_SPOT_LIGHT2D (light);
    gfloat x, y;

    if (!lrg_light2d_get_enabled (light))
        return FALSE;

    lrg_light2d_get_position (light, &x, &y);

    /* Approximate visibility using bounding box of cone */
    return (x + self->radius >= viewport_x &&
            x - self->radius <= viewport_x + viewport_width &&
            y + self->radius >= viewport_y &&
            y - self->radius <= viewport_y + viewport_height);
}

static void
lrg_spot_light2d_render (LrgLight2D *light,
                         guint       target_id,
                         guint       width,
                         guint       height)
{
    LrgSpotLight2D *self = LRG_SPOT_LIGHT2D (light);
    g_autoptr(GrlVector2) center = NULL;
    g_autoptr(GrlColor) color = NULL;
    LrgLightFalloff falloff;
    gfloat x, y;
    gfloat intensity;
    gfloat half_outer;
    gfloat half_inner;
    guint8 r, g, b;
    guint8 alpha;
    guint layers;
    guint cones;
    guint c;
    guint i;

    (void)target_id;
    (void)width;
    (void)height;

    if (!lrg_light2d_get_enabled (light) || self->radius <= 0.0f || self->angle <= 0.0f)
        return;

    intensity = lrg_light2d_get_intensity (light);
    if (intensity <= 0.0f)
        return;

    lrg_light2d_get_position (light, &x, &y);
    lrg_light2d_get_color (light, &r, &g, &b);
    falloff = lrg_light2d_get_falloff (light);

    /*
     * Same stacked-layer scheme as the point light (see
     * lrg-point-light2d.c), with circle sectors instead of discs.  The
     * cone spans direction +- angle / 2 (degrees, raylib's convention:
     * 0 points along +X, positive turns clockwise on a Y-down screen).
     * A narrower inner_angle cone is the full-brightness core: the
     * light is split between the outer and the inner cone, so the
     * fringe between them gets half.
     */
    half_outer = self->angle * 0.5f;
    half_inner = CLAMP (self->inner_angle, 0.0f, self->angle) * 0.5f;
    cones = (half_inner > 0.0f && half_inner < half_outer) ? 2u : 1u;
    layers = falloff == LRG_LIGHT_FALLOFF_NONE ? 1u : 16u;
    alpha = (guint8) MIN (255.0f, 255.0f * intensity / (gfloat) (layers * cones));

    center = grl_vector2_new (x, y);
    color = grl_color_new (r, g, b, alpha);

    for (c = 0; c < cones; c++)
    {
        gfloat half = c == 0 ? half_outer : half_inner;

        for (i = 0; i < layers; i++)
        {
            gfloat s;
            gfloat reach;

            s = (gfloat) (i + 1) / (gfloat) layers;
            if (falloff == LRG_LIGHT_FALLOFF_QUADRATIC)
                reach = self->radius * (1.0f - sqrtf (1.0f - s));
            else
                reach = self->radius * s;

            if (reach > 0.0f)
                grl_draw_circle_sector (center, reach,
                                        self->direction - half,
                                        self->direction + half,
                                        0, color);
        }
    }
}

static void
lrg_spot_light2d_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgSpotLight2D *self = LRG_SPOT_LIGHT2D (object);
    switch (prop_id)
    {
    case PROP_RADIUS: g_value_set_float (value, self->radius); break;
    case PROP_ANGLE: g_value_set_float (value, self->angle); break;
    case PROP_INNER_ANGLE: g_value_set_float (value, self->inner_angle); break;
    case PROP_DIRECTION: g_value_set_float (value, self->direction); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_spot_light2d_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgSpotLight2D *self = LRG_SPOT_LIGHT2D (object);
    switch (prop_id)
    {
    case PROP_RADIUS: self->radius = g_value_get_float (value); break;
    case PROP_ANGLE: self->angle = g_value_get_float (value); break;
    case PROP_INNER_ANGLE: self->inner_angle = g_value_get_float (value); break;
    case PROP_DIRECTION: self->direction = g_value_get_float (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_spot_light2d_class_init (LrgSpotLight2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgLight2DClass *light_class = LRG_LIGHT2D_CLASS (klass);

    object_class->get_property = lrg_spot_light2d_get_property;
    object_class->set_property = lrg_spot_light2d_set_property;

    light_class->is_visible = lrg_spot_light2d_is_visible;
    light_class->render = lrg_spot_light2d_render;

    properties[PROP_RADIUS] = g_param_spec_float ("radius", "Radius", "Light radius", 0, G_MAXFLOAT, 300, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ANGLE] = g_param_spec_float ("angle", "Angle", "Cone angle in degrees", 0, 360, 45, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_INNER_ANGLE] = g_param_spec_float ("inner-angle", "Inner Angle", "Inner cone angle", 0, 360, 30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_DIRECTION] = g_param_spec_float ("direction", "Direction", "Direction in degrees", 0, 360, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_spot_light2d_init (LrgSpotLight2D *self)
{
    self->radius = 300.0f;
    self->angle = 45.0f;
    self->inner_angle = 30.0f;
    self->direction = 0.0f;
}

/* Public API */

LrgSpotLight2D * lrg_spot_light2d_new (void) { return g_object_new (LRG_TYPE_SPOT_LIGHT2D, NULL); }

gfloat lrg_spot_light2d_get_radius (LrgSpotLight2D *self) { g_return_val_if_fail (LRG_IS_SPOT_LIGHT2D (self), 0); return self->radius; }
void lrg_spot_light2d_set_radius (LrgSpotLight2D *self, gfloat radius) { g_return_if_fail (LRG_IS_SPOT_LIGHT2D (self)); self->radius = radius; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]); }

gfloat lrg_spot_light2d_get_angle (LrgSpotLight2D *self) { g_return_val_if_fail (LRG_IS_SPOT_LIGHT2D (self), 0); return self->angle; }
void lrg_spot_light2d_set_angle (LrgSpotLight2D *self, gfloat angle) { g_return_if_fail (LRG_IS_SPOT_LIGHT2D (self)); self->angle = angle; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANGLE]); }

gfloat lrg_spot_light2d_get_direction (LrgSpotLight2D *self) { g_return_val_if_fail (LRG_IS_SPOT_LIGHT2D (self), 0); return self->direction; }
void lrg_spot_light2d_set_direction (LrgSpotLight2D *self, gfloat direction) { g_return_if_fail (LRG_IS_SPOT_LIGHT2D (self)); self->direction = direction; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]); }

gfloat lrg_spot_light2d_get_inner_angle (LrgSpotLight2D *self) { g_return_val_if_fail (LRG_IS_SPOT_LIGHT2D (self), 0); return self->inner_angle; }
void lrg_spot_light2d_set_inner_angle (LrgSpotLight2D *self, gfloat angle) { g_return_if_fail (LRG_IS_SPOT_LIGHT2D (self)); self->inner_angle = angle; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INNER_ANGLE]); }
