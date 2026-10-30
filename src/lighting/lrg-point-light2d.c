/* lrg-point-light2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Point light implementation.
 */

#include "lrg-point-light2d.h"
#include "../lrg-log.h"
#include <math.h>
#include <graylib.h>
#include <stdlib.h>

struct _LrgPointLight2D
{
    LrgLight2D parent_instance;

    gfloat   radius;
    gfloat   inner_radius;
    gboolean flicker_enabled;
    gfloat   flicker_speed;
    gfloat   flicker_amount;
    gfloat   flicker_time;
    gfloat   current_flicker;
};

G_DEFINE_FINAL_TYPE (LrgPointLight2D, lrg_point_light2d, LRG_TYPE_LIGHT2D)

enum
{
    PROP_0,
    PROP_RADIUS,
    PROP_INNER_RADIUS,
    PROP_FLICKER_ENABLED,
    PROP_FLICKER_SPEED,
    PROP_FLICKER_AMOUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_point_light2d_is_visible (LrgLight2D *light,
                              gfloat      viewport_x,
                              gfloat      viewport_y,
                              gfloat      viewport_width,
                              gfloat      viewport_height)
{
    LrgPointLight2D *self = LRG_POINT_LIGHT2D (light);
    gfloat x, y;

    if (!lrg_light2d_get_enabled (light))
        return FALSE;

    lrg_light2d_get_position (light, &x, &y);

    /* Check if light circle intersects viewport */
    return (x + self->radius >= viewport_x &&
            x - self->radius <= viewport_x + viewport_width &&
            y + self->radius >= viewport_y &&
            y - self->radius <= viewport_y + viewport_height);
}

static void
lrg_point_light2d_update (LrgLight2D *light,
                          gfloat      delta_time)
{
    LrgPointLight2D *self = LRG_POINT_LIGHT2D (light);

    if (self->flicker_enabled)
    {
        self->flicker_time += delta_time * self->flicker_speed;

        /* Use noise-like function for natural flickering */
        self->current_flicker = 1.0f - self->flicker_amount * 0.5f +
                                self->flicker_amount * 0.5f *
                                sinf (self->flicker_time * 5.0f) *
                                cosf (self->flicker_time * 3.7f);
    }
    else
    {
        self->current_flicker = 1.0f;
    }
}

static void
lrg_point_light2d_render (LrgLight2D *light,
                          guint       target_id,
                          guint       width,
                          guint       height)
{
    LrgPointLight2D *self = LRG_POINT_LIGHT2D (light);
    g_autoptr(GrlVector2) center = NULL;
    g_autoptr(GrlColor) color = NULL;
    LrgLightFalloff falloff;
    gfloat x, y;
    gfloat intensity;
    gfloat inner;
    gfloat span;
    guint8 r, g, b;
    guint layers;
    guint8 alpha;
    guint i;

    (void)target_id;
    (void)width;
    (void)height;

    if (!lrg_light2d_get_enabled (light) || self->radius <= 0.0f)
        return;

    lrg_light2d_get_position (light, &x, &y);
    intensity = lrg_light2d_get_intensity (light) * self->current_flicker;
    lrg_light2d_get_color (light, &r, &g, &b);
    falloff = lrg_light2d_get_falloff (light);
    if (intensity <= 0.0f)
        return;

    /*
     * Drawn in world units inside the manager's light pass, which has
     * additive blending (src * alpha + dst) on.  The falloff is built
     * from stacked discs: every disc adds 1/layers of the light, so a
     * point at distance d receives the share of discs reaching past d.
     * Disc radii are spaced so that share follows the falloff curve:
     *   linear     r_j = inner + span * s_j
     *   quadratic  r_j = inner + span * (1 - sqrt (1 - s_j))
     * with s_j = (j + 1) / layers.  Everything inside inner_radius gets
     * every disc, i.e. full brightness.
     */
    inner = CLAMP (self->inner_radius, 0.0f, self->radius);
    span = self->radius - inner;
    layers = (falloff == LRG_LIGHT_FALLOFF_NONE || span <= 0.0f) ? 1u : 16u;
    alpha = (guint8) MIN (255.0f, 255.0f * intensity / (gfloat) layers);

    center = grl_vector2_new (x, y);
    color = grl_color_new (r, g, b, alpha);

    for (i = 0; i < layers; i++)
    {
        gfloat s;
        gfloat disc;

        s = (gfloat) (i + 1) / (gfloat) layers;
        if (layers == 1)
            disc = self->radius;
        else if (falloff == LRG_LIGHT_FALLOFF_QUADRATIC)
            disc = inner + span * (1.0f - sqrtf (1.0f - s));
        else
            disc = inner + span * s;

        if (disc > 0.0f)
            grl_draw_circle_v (center, disc, color);
    }
}

static void
lrg_point_light2d_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgPointLight2D *self = LRG_POINT_LIGHT2D (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        g_value_set_float (value, self->radius);
        break;
    case PROP_INNER_RADIUS:
        g_value_set_float (value, self->inner_radius);
        break;
    case PROP_FLICKER_ENABLED:
        g_value_set_boolean (value, self->flicker_enabled);
        break;
    case PROP_FLICKER_SPEED:
        g_value_set_float (value, self->flicker_speed);
        break;
    case PROP_FLICKER_AMOUNT:
        g_value_set_float (value, self->flicker_amount);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_point_light2d_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgPointLight2D *self = LRG_POINT_LIGHT2D (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        self->radius = g_value_get_float (value);
        break;
    case PROP_INNER_RADIUS:
        self->inner_radius = g_value_get_float (value);
        break;
    case PROP_FLICKER_ENABLED:
        self->flicker_enabled = g_value_get_boolean (value);
        break;
    case PROP_FLICKER_SPEED:
        self->flicker_speed = g_value_get_float (value);
        break;
    case PROP_FLICKER_AMOUNT:
        self->flicker_amount = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_point_light2d_class_init (LrgPointLight2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgLight2DClass *light_class = LRG_LIGHT2D_CLASS (klass);

    object_class->get_property = lrg_point_light2d_get_property;
    object_class->set_property = lrg_point_light2d_set_property;

    light_class->is_visible = lrg_point_light2d_is_visible;
    light_class->update = lrg_point_light2d_update;
    light_class->render = lrg_point_light2d_render;

    properties[PROP_RADIUS] =
        g_param_spec_float ("radius", "Radius", "Light radius",
                            0.0f, G_MAXFLOAT, 200.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INNER_RADIUS] =
        g_param_spec_float ("inner-radius", "Inner Radius", "Inner radius",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FLICKER_ENABLED] =
        g_param_spec_boolean ("flicker-enabled", "Flicker Enabled", "Enable flicker",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FLICKER_SPEED] =
        g_param_spec_float ("flicker-speed", "Flicker Speed", "Flicker speed",
                            0.0f, 100.0f, 5.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FLICKER_AMOUNT] =
        g_param_spec_float ("flicker-amount", "Flicker Amount", "Flicker intensity",
                            0.0f, 1.0f, 0.2f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_point_light2d_init (LrgPointLight2D *self)
{
    self->radius = 200.0f;
    self->inner_radius = 0.0f;
    self->flicker_enabled = FALSE;
    self->flicker_speed = 5.0f;
    self->flicker_amount = 0.2f;
    self->flicker_time = 0.0f;
    self->current_flicker = 1.0f;
}

/* Public API */

LrgPointLight2D *
lrg_point_light2d_new (void)
{
    return g_object_new (LRG_TYPE_POINT_LIGHT2D, NULL);
}

gfloat lrg_point_light2d_get_radius (LrgPointLight2D *self) { g_return_val_if_fail (LRG_IS_POINT_LIGHT2D (self), 0); return self->radius; }
void lrg_point_light2d_set_radius (LrgPointLight2D *self, gfloat radius) { g_return_if_fail (LRG_IS_POINT_LIGHT2D (self)); self->radius = radius; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]); }

gfloat lrg_point_light2d_get_inner_radius (LrgPointLight2D *self) { g_return_val_if_fail (LRG_IS_POINT_LIGHT2D (self), 0); return self->inner_radius; }
void lrg_point_light2d_set_inner_radius (LrgPointLight2D *self, gfloat radius) { g_return_if_fail (LRG_IS_POINT_LIGHT2D (self)); self->inner_radius = radius; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INNER_RADIUS]); }

gboolean lrg_point_light2d_get_flicker_enabled (LrgPointLight2D *self) { g_return_val_if_fail (LRG_IS_POINT_LIGHT2D (self), FALSE); return self->flicker_enabled; }
void lrg_point_light2d_set_flicker_enabled (LrgPointLight2D *self, gboolean enabled) { g_return_if_fail (LRG_IS_POINT_LIGHT2D (self)); self->flicker_enabled = enabled; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLICKER_ENABLED]); }

gfloat lrg_point_light2d_get_flicker_speed (LrgPointLight2D *self) { g_return_val_if_fail (LRG_IS_POINT_LIGHT2D (self), 0); return self->flicker_speed; }
void lrg_point_light2d_set_flicker_speed (LrgPointLight2D *self, gfloat speed) { g_return_if_fail (LRG_IS_POINT_LIGHT2D (self)); self->flicker_speed = speed; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLICKER_SPEED]); }

gfloat lrg_point_light2d_get_flicker_amount (LrgPointLight2D *self) { g_return_val_if_fail (LRG_IS_POINT_LIGHT2D (self), 0); return self->flicker_amount; }
void lrg_point_light2d_set_flicker_amount (LrgPointLight2D *self, gfloat amount) { g_return_if_fail (LRG_IS_POINT_LIGHT2D (self)); self->flicker_amount = CLAMP (amount, 0.0f, 1.0f); g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLICKER_AMOUNT]); }
