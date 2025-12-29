/* lrg-trigger-circle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Circular trigger zone implementation.
 */

#include "config.h"

#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRIGGER2D
#include "../lrg-log.h"

#include "lrg-trigger-circle.h"

/**
 * LrgTriggerCircle:
 *
 * A circular trigger zone.
 *
 * #LrgTriggerCircle is a concrete implementation of #LrgTrigger2D that
 * defines a circular area for collision detection.
 *
 * Since: 1.0
 */
struct _LrgTriggerCircle
{
    LrgTrigger2D parent_instance;

    gfloat center_x;
    gfloat center_y;
    gfloat radius;
};

G_DEFINE_FINAL_TYPE (LrgTriggerCircle, lrg_trigger_circle, LRG_TYPE_TRIGGER2D)

enum
{
    PROP_0,
    PROP_CENTER_X,
    PROP_CENTER_Y,
    PROP_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Virtual method implementations */

static gboolean
lrg_trigger_circle_test_point_impl (LrgTrigger2D *trigger,
                                    gfloat        px,
                                    gfloat        py)
{
    LrgTriggerCircle *self = LRG_TRIGGER_CIRCLE (trigger);
    gfloat dx;
    gfloat dy;
    gfloat dist_sq;

    /*
     * Point-in-circle test using squared distance.
     * Avoids expensive sqrt() call.
     */
    dx = px - self->center_x;
    dy = py - self->center_y;
    dist_sq = dx * dx + dy * dy;

    return dist_sq <= (self->radius * self->radius);
}

static void
lrg_trigger_circle_get_bounds_impl (LrgTrigger2D *trigger,
                                    gfloat       *out_x,
                                    gfloat       *out_y,
                                    gfloat       *out_width,
                                    gfloat       *out_height)
{
    LrgTriggerCircle *self = LRG_TRIGGER_CIRCLE (trigger);
    gfloat diameter;

    /* Bounding box is a square centered on the circle */
    diameter = self->radius * 2.0f;

    if (out_x != NULL)
        *out_x = self->center_x - self->radius;
    if (out_y != NULL)
        *out_y = self->center_y - self->radius;
    if (out_width != NULL)
        *out_width = diameter;
    if (out_height != NULL)
        *out_height = diameter;
}

static LrgTrigger2DShape
lrg_trigger_circle_get_shape_impl (LrgTrigger2D *trigger)
{
    (void)trigger;
    return LRG_TRIGGER2D_SHAPE_CIRCLE;
}

/* GObject implementation */

static void
lrg_trigger_circle_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgTriggerCircle *self = LRG_TRIGGER_CIRCLE (object);

    switch (prop_id)
    {
    case PROP_CENTER_X:
        g_value_set_float (value, self->center_x);
        break;
    case PROP_CENTER_Y:
        g_value_set_float (value, self->center_y);
        break;
    case PROP_RADIUS:
        g_value_set_float (value, self->radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_circle_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgTriggerCircle *self = LRG_TRIGGER_CIRCLE (object);

    switch (prop_id)
    {
    case PROP_CENTER_X:
        lrg_trigger_circle_set_center_x (self, g_value_get_float (value));
        break;
    case PROP_CENTER_Y:
        lrg_trigger_circle_set_center_y (self, g_value_get_float (value));
        break;
    case PROP_RADIUS:
        lrg_trigger_circle_set_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_circle_class_init (LrgTriggerCircleClass *klass)
{
    GObjectClass      *object_class  = G_OBJECT_CLASS (klass);
    LrgTrigger2DClass *trigger_class = LRG_TRIGGER2D_CLASS (klass);

    object_class->get_property = lrg_trigger_circle_get_property;
    object_class->set_property = lrg_trigger_circle_set_property;

    /* Override virtual methods */
    trigger_class->test_point = lrg_trigger_circle_test_point_impl;
    trigger_class->get_bounds = lrg_trigger_circle_get_bounds_impl;
    trigger_class->get_shape  = lrg_trigger_circle_get_shape_impl;

    /**
     * LrgTriggerCircle:center-x:
     *
     * The X coordinate of the circle center.
     *
     * Since: 1.0
     */
    properties[PROP_CENTER_X] =
        g_param_spec_float ("center-x",
                            "Center X",
                            "X coordinate of the circle center",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTriggerCircle:center-y:
     *
     * The Y coordinate of the circle center.
     *
     * Since: 1.0
     */
    properties[PROP_CENTER_Y] =
        g_param_spec_float ("center-y",
                            "Center Y",
                            "Y coordinate of the circle center",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTriggerCircle:radius:
     *
     * The radius of the circle.
     *
     * Since: 1.0
     */
    properties[PROP_RADIUS] =
        g_param_spec_float ("radius",
                            "Radius",
                            "Radius of the circle",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_trigger_circle_init (LrgTriggerCircle *self)
{
    self->center_x = 0.0f;
    self->center_y = 0.0f;
    self->radius   = 0.0f;
}

/* Public API */

/**
 * lrg_trigger_circle_new:
 * @center_x: X coordinate of the circle center
 * @center_y: Y coordinate of the circle center
 * @radius: Radius of the circle
 *
 * Creates a new circular trigger zone.
 *
 * Returns: (transfer full): A new #LrgTriggerCircle
 *
 * Since: 1.0
 */
LrgTriggerCircle *
lrg_trigger_circle_new (gfloat center_x,
                        gfloat center_y,
                        gfloat radius)
{
    return g_object_new (LRG_TYPE_TRIGGER_CIRCLE,
                         "center-x", center_x,
                         "center-y", center_y,
                         "radius", radius,
                         NULL);
}

/**
 * lrg_trigger_circle_new_with_id:
 * @id: Unique identifier for the trigger
 * @center_x: X coordinate of the circle center
 * @center_y: Y coordinate of the circle center
 * @radius: Radius of the circle
 *
 * Creates a new circular trigger zone with an ID.
 *
 * Returns: (transfer full): A new #LrgTriggerCircle
 *
 * Since: 1.0
 */
LrgTriggerCircle *
lrg_trigger_circle_new_with_id (const gchar *id,
                                gfloat       center_x,
                                gfloat       center_y,
                                gfloat       radius)
{
    return g_object_new (LRG_TYPE_TRIGGER_CIRCLE,
                         "id", id,
                         "center-x", center_x,
                         "center-y", center_y,
                         "radius", radius,
                         NULL);
}

/**
 * lrg_trigger_circle_get_center_x:
 * @self: A #LrgTriggerCircle
 *
 * Gets the X coordinate of the circle center.
 *
 * Returns: The center X coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_circle_get_center_x (LrgTriggerCircle *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_CIRCLE (self), 0.0f);
    return self->center_x;
}

/**
 * lrg_trigger_circle_set_center_x:
 * @self: A #LrgTriggerCircle
 * @x: The center X coordinate
 *
 * Sets the X coordinate of the circle center.
 *
 * Since: 1.0
 */
void
lrg_trigger_circle_set_center_x (LrgTriggerCircle *self,
                                 gfloat            x)
{
    g_return_if_fail (LRG_IS_TRIGGER_CIRCLE (self));

    if (self->center_x != x)
    {
        self->center_x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CENTER_X]);
    }
}

/**
 * lrg_trigger_circle_get_center_y:
 * @self: A #LrgTriggerCircle
 *
 * Gets the Y coordinate of the circle center.
 *
 * Returns: The center Y coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_circle_get_center_y (LrgTriggerCircle *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_CIRCLE (self), 0.0f);
    return self->center_y;
}

/**
 * lrg_trigger_circle_set_center_y:
 * @self: A #LrgTriggerCircle
 * @y: The center Y coordinate
 *
 * Sets the Y coordinate of the circle center.
 *
 * Since: 1.0
 */
void
lrg_trigger_circle_set_center_y (LrgTriggerCircle *self,
                                 gfloat            y)
{
    g_return_if_fail (LRG_IS_TRIGGER_CIRCLE (self));

    if (self->center_y != y)
    {
        self->center_y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CENTER_Y]);
    }
}

/**
 * lrg_trigger_circle_get_center:
 * @self: A #LrgTriggerCircle
 * @out_x: (out) (nullable): Return location for center X
 * @out_y: (out) (nullable): Return location for center Y
 *
 * Gets the center point of the circle.
 *
 * Since: 1.0
 */
void
lrg_trigger_circle_get_center (LrgTriggerCircle *self,
                               gfloat           *out_x,
                               gfloat           *out_y)
{
    g_return_if_fail (LRG_IS_TRIGGER_CIRCLE (self));

    if (out_x != NULL)
        *out_x = self->center_x;
    if (out_y != NULL)
        *out_y = self->center_y;
}

/**
 * lrg_trigger_circle_set_center:
 * @self: A #LrgTriggerCircle
 * @x: The center X coordinate
 * @y: The center Y coordinate
 *
 * Sets the center point of the circle.
 *
 * Since: 1.0
 */
void
lrg_trigger_circle_set_center (LrgTriggerCircle *self,
                               gfloat            x,
                               gfloat            y)
{
    g_return_if_fail (LRG_IS_TRIGGER_CIRCLE (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_trigger_circle_set_center_x (self, x);
    lrg_trigger_circle_set_center_y (self, y);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_trigger_circle_get_radius:
 * @self: A #LrgTriggerCircle
 *
 * Gets the radius of the circle.
 *
 * Returns: The radius
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_circle_get_radius (LrgTriggerCircle *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_CIRCLE (self), 0.0f);
    return self->radius;
}

/**
 * lrg_trigger_circle_set_radius:
 * @self: A #LrgTriggerCircle
 * @radius: The radius
 *
 * Sets the radius of the circle.
 *
 * Since: 1.0
 */
void
lrg_trigger_circle_set_radius (LrgTriggerCircle *self,
                               gfloat            radius)
{
    g_return_if_fail (LRG_IS_TRIGGER_CIRCLE (self));
    g_return_if_fail (radius >= 0.0f);

    if (self->radius != radius)
    {
        self->radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
    }
}

/**
 * lrg_trigger_circle_get_diameter:
 * @self: A #LrgTriggerCircle
 *
 * Gets the diameter of the circle (2 * radius).
 *
 * Returns: The diameter
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_circle_get_diameter (LrgTriggerCircle *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_CIRCLE (self), 0.0f);
    return self->radius * 2.0f;
}

/**
 * lrg_trigger_circle_distance_to_point:
 * @self: A #LrgTriggerCircle
 * @x: X coordinate of the point
 * @y: Y coordinate of the point
 *
 * Gets the distance from the circle edge to a point.
 * Negative values indicate the point is inside the circle.
 *
 * Returns: The signed distance
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_circle_distance_to_point (LrgTriggerCircle *self,
                                      gfloat            x,
                                      gfloat            y)
{
    gfloat dx;
    gfloat dy;
    gfloat dist;

    g_return_val_if_fail (LRG_IS_TRIGGER_CIRCLE (self), 0.0f);

    dx = x - self->center_x;
    dy = y - self->center_y;
    dist = sqrtf (dx * dx + dy * dy);

    return dist - self->radius;
}
