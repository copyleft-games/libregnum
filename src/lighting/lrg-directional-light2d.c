/* lrg-directional-light2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Directional light implementation.
 */

#include "lrg-directional-light2d.h"
#include "../lrg-log.h"
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <graylib.h>

struct _LrgDirectionalLight2D
{
    LrgLight2D parent_instance;

    gfloat direction;     /* Direction in degrees */
    gfloat shadow_length; /* Length of cast shadows */
};

G_DEFINE_FINAL_TYPE (LrgDirectionalLight2D, lrg_directional_light2d, LRG_TYPE_LIGHT2D)

enum
{
    PROP_0,
    PROP_DIRECTION,
    PROP_SHADOW_LENGTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
lrg_directional_light2d_is_visible (LrgLight2D *light,
                                    gfloat      viewport_x,
                                    gfloat      viewport_y,
                                    gfloat      viewport_width,
                                    gfloat      viewport_height)
{
    (void)viewport_x;
    (void)viewport_y;
    (void)viewport_width;
    (void)viewport_height;

    /* Directional lights are always visible if enabled */
    return lrg_light2d_get_enabled (light);
}

static void
lrg_directional_light2d_render (LrgLight2D *light,
                                guint       target_id,
                                guint       width,
                                guint       height)
{
    g_autoptr(GrlRectangle) rect = NULL;
    g_autoptr(GrlColor) color = NULL;
    Matrix view;
    gfloat det;
    gfloat intensity;
    gfloat min_x, min_y, max_x, max_y;
    guint8 r, g, b;
    guint i;

    (void)target_id;

    if (!lrg_light2d_get_enabled (light) || width == 0 || height == 0)
        return;

    intensity = lrg_light2d_get_intensity (light);
    if (intensity <= 0.0f)
        return;

    /*
     * A directional light (sun, moon) lights everything in view evenly,
     * so it fills the whole target.  The light pass draws in world
     * units through a camera, so map the target's corners back to the
     * world with the inverse of the active modelview (the camera) and
     * fill their bounding box.  Direction and shadow length only
     * matter for shadow casting, not for the light's own contribution.
     *
     * A 2D camera's modelview is affine in X/Y:
     *   x' = m0 x + m4 y + m12,   y' = m1 x + m5 y + m13
     * so it is inverted by hand rather than through raymath (which
     * needs C99).
     */
    view = rlGetMatrixModelview ();
    det = view.m0 * view.m5 - view.m4 * view.m1;
    if (fabsf (det) < 1e-12f)
        return;
    min_x = min_y = G_MAXFLOAT;
    max_x = max_y = -G_MAXFLOAT;
    for (i = 0; i < 4; i++)
    {
        gfloat px, py;
        gfloat wx, wy;

        px = ((i & 1u) != 0 ? (gfloat) width : 0.0f) - view.m12;
        py = ((i & 2u) != 0 ? (gfloat) height : 0.0f) - view.m13;
        wx = ( view.m5 * px - view.m4 * py) / det;
        wy = (-view.m1 * px + view.m0 * py) / det;

        min_x = MIN (min_x, wx);
        min_y = MIN (min_y, wy);
        max_x = MAX (max_x, wx);
        max_y = MAX (max_y, wy);
    }

    lrg_light2d_get_color (light, &r, &g, &b);
    color = grl_color_new (r, g, b, (guint8) MIN (255.0f, 255.0f * intensity));
    rect = grl_rectangle_new (min_x, min_y, max_x - min_x, max_y - min_y);
    grl_draw_rectangle_rec (rect, color);
}

static void
lrg_directional_light2d_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgDirectionalLight2D *self = LRG_DIRECTIONAL_LIGHT2D (object);
    switch (prop_id)
    {
    case PROP_DIRECTION: g_value_set_float (value, self->direction); break;
    case PROP_SHADOW_LENGTH: g_value_set_float (value, self->shadow_length); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_directional_light2d_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgDirectionalLight2D *self = LRG_DIRECTIONAL_LIGHT2D (object);
    switch (prop_id)
    {
    case PROP_DIRECTION: self->direction = g_value_get_float (value); break;
    case PROP_SHADOW_LENGTH: self->shadow_length = g_value_get_float (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_directional_light2d_class_init (LrgDirectionalLight2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgLight2DClass *light_class = LRG_LIGHT2D_CLASS (klass);

    object_class->get_property = lrg_directional_light2d_get_property;
    object_class->set_property = lrg_directional_light2d_set_property;

    light_class->is_visible = lrg_directional_light2d_is_visible;
    light_class->render = lrg_directional_light2d_render;

    properties[PROP_DIRECTION] = g_param_spec_float ("direction", "Direction", "Light direction", 0, 360, 45, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SHADOW_LENGTH] = g_param_spec_float ("shadow-length", "Shadow Length", "Shadow length", 0, G_MAXFLOAT, 100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_directional_light2d_init (LrgDirectionalLight2D *self)
{
    self->direction = 45.0f;
    self->shadow_length = 100.0f;
}

/* Public API */

LrgDirectionalLight2D * lrg_directional_light2d_new (void) { return g_object_new (LRG_TYPE_DIRECTIONAL_LIGHT2D, NULL); }

gfloat lrg_directional_light2d_get_direction (LrgDirectionalLight2D *self) { g_return_val_if_fail (LRG_IS_DIRECTIONAL_LIGHT2D (self), 0); return self->direction; }
void lrg_directional_light2d_set_direction (LrgDirectionalLight2D *self, gfloat direction) { g_return_if_fail (LRG_IS_DIRECTIONAL_LIGHT2D (self)); self->direction = direction; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]); }

gfloat lrg_directional_light2d_get_shadow_length (LrgDirectionalLight2D *self) { g_return_val_if_fail (LRG_IS_DIRECTIONAL_LIGHT2D (self), 0); return self->shadow_length; }
void lrg_directional_light2d_set_shadow_length (LrgDirectionalLight2D *self, gfloat length) { g_return_if_fail (LRG_IS_DIRECTIONAL_LIGHT2D (self)); self->shadow_length = length; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_LENGTH]); }
