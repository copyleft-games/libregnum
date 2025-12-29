/* lrg-trigger-rect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rectangular trigger zone implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRIGGER2D
#include "../lrg-log.h"

#include "lrg-trigger-rect.h"

/**
 * LrgTriggerRect:
 *
 * A rectangular trigger zone.
 *
 * #LrgTriggerRect is a concrete implementation of #LrgTrigger2D that
 * defines a rectangular area for collision detection.
 *
 * Since: 1.0
 */
struct _LrgTriggerRect
{
    LrgTrigger2D parent_instance;

    gfloat x;
    gfloat y;
    gfloat width;
    gfloat height;
};

G_DEFINE_FINAL_TYPE (LrgTriggerRect, lrg_trigger_rect, LRG_TYPE_TRIGGER2D)

enum
{
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Virtual method implementations */

static gboolean
lrg_trigger_rect_test_point_impl (LrgTrigger2D *trigger,
                                  gfloat        px,
                                  gfloat        py)
{
    LrgTriggerRect *self = LRG_TRIGGER_RECT (trigger);

    /*
     * Simple AABB point containment test.
     * A point is inside if it's within all four bounds.
     */
    return (px >= self->x &&
            px <= self->x + self->width &&
            py >= self->y &&
            py <= self->y + self->height);
}

static void
lrg_trigger_rect_get_bounds_impl (LrgTrigger2D *trigger,
                                  gfloat       *out_x,
                                  gfloat       *out_y,
                                  gfloat       *out_width,
                                  gfloat       *out_height)
{
    LrgTriggerRect *self = LRG_TRIGGER_RECT (trigger);

    /* For rectangles, the bounds ARE the rectangle */
    if (out_x != NULL)
        *out_x = self->x;
    if (out_y != NULL)
        *out_y = self->y;
    if (out_width != NULL)
        *out_width = self->width;
    if (out_height != NULL)
        *out_height = self->height;
}

static LrgTrigger2DShape
lrg_trigger_rect_get_shape_impl (LrgTrigger2D *trigger)
{
    (void)trigger;
    return LRG_TRIGGER2D_SHAPE_RECTANGLE;
}

/* GObject implementation */

static void
lrg_trigger_rect_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgTriggerRect *self = LRG_TRIGGER_RECT (object);

    switch (prop_id)
    {
    case PROP_X:
        g_value_set_float (value, self->x);
        break;
    case PROP_Y:
        g_value_set_float (value, self->y);
        break;
    case PROP_WIDTH:
        g_value_set_float (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_float (value, self->height);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_rect_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgTriggerRect *self = LRG_TRIGGER_RECT (object);

    switch (prop_id)
    {
    case PROP_X:
        lrg_trigger_rect_set_x (self, g_value_get_float (value));
        break;
    case PROP_Y:
        lrg_trigger_rect_set_y (self, g_value_get_float (value));
        break;
    case PROP_WIDTH:
        lrg_trigger_rect_set_width (self, g_value_get_float (value));
        break;
    case PROP_HEIGHT:
        lrg_trigger_rect_set_height (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_rect_class_init (LrgTriggerRectClass *klass)
{
    GObjectClass      *object_class  = G_OBJECT_CLASS (klass);
    LrgTrigger2DClass *trigger_class = LRG_TRIGGER2D_CLASS (klass);

    object_class->get_property = lrg_trigger_rect_get_property;
    object_class->set_property = lrg_trigger_rect_set_property;

    /* Override virtual methods */
    trigger_class->test_point = lrg_trigger_rect_test_point_impl;
    trigger_class->get_bounds = lrg_trigger_rect_get_bounds_impl;
    trigger_class->get_shape  = lrg_trigger_rect_get_shape_impl;

    /**
     * LrgTriggerRect:x:
     *
     * The X coordinate of the rectangle origin.
     *
     * Since: 1.0
     */
    properties[PROP_X] =
        g_param_spec_float ("x",
                            "X",
                            "X coordinate of the rectangle origin",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTriggerRect:y:
     *
     * The Y coordinate of the rectangle origin.
     *
     * Since: 1.0
     */
    properties[PROP_Y] =
        g_param_spec_float ("y",
                            "Y",
                            "Y coordinate of the rectangle origin",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTriggerRect:width:
     *
     * The width of the rectangle.
     *
     * Since: 1.0
     */
    properties[PROP_WIDTH] =
        g_param_spec_float ("width",
                            "Width",
                            "Width of the rectangle",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTriggerRect:height:
     *
     * The height of the rectangle.
     *
     * Since: 1.0
     */
    properties[PROP_HEIGHT] =
        g_param_spec_float ("height",
                            "Height",
                            "Height of the rectangle",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_trigger_rect_init (LrgTriggerRect *self)
{
    self->x      = 0.0f;
    self->y      = 0.0f;
    self->width  = 0.0f;
    self->height = 0.0f;
}

/* Public API */

/**
 * lrg_trigger_rect_new:
 * @x: X coordinate of the rectangle origin
 * @y: Y coordinate of the rectangle origin
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 *
 * Creates a new rectangular trigger zone.
 *
 * Returns: (transfer full): A new #LrgTriggerRect
 *
 * Since: 1.0
 */
LrgTriggerRect *
lrg_trigger_rect_new (gfloat x,
                      gfloat y,
                      gfloat width,
                      gfloat height)
{
    return g_object_new (LRG_TYPE_TRIGGER_RECT,
                         "x", x,
                         "y", y,
                         "width", width,
                         "height", height,
                         NULL);
}

/**
 * lrg_trigger_rect_new_with_id:
 * @id: Unique identifier for the trigger
 * @x: X coordinate of the rectangle origin
 * @y: Y coordinate of the rectangle origin
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 *
 * Creates a new rectangular trigger zone with an ID.
 *
 * Returns: (transfer full): A new #LrgTriggerRect
 *
 * Since: 1.0
 */
LrgTriggerRect *
lrg_trigger_rect_new_with_id (const gchar *id,
                              gfloat       x,
                              gfloat       y,
                              gfloat       width,
                              gfloat       height)
{
    return g_object_new (LRG_TYPE_TRIGGER_RECT,
                         "id", id,
                         "x", x,
                         "y", y,
                         "width", width,
                         "height", height,
                         NULL);
}

/**
 * lrg_trigger_rect_get_x:
 * @self: A #LrgTriggerRect
 *
 * Gets the X coordinate of the rectangle origin.
 *
 * Returns: The X coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_rect_get_x (LrgTriggerRect *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_RECT (self), 0.0f);
    return self->x;
}

/**
 * lrg_trigger_rect_set_x:
 * @self: A #LrgTriggerRect
 * @x: The X coordinate
 *
 * Sets the X coordinate of the rectangle origin.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_x (LrgTriggerRect *self,
                        gfloat          x)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    if (self->x != x)
    {
        self->x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
    }
}

/**
 * lrg_trigger_rect_get_y:
 * @self: A #LrgTriggerRect
 *
 * Gets the Y coordinate of the rectangle origin.
 *
 * Returns: The Y coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_rect_get_y (LrgTriggerRect *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_RECT (self), 0.0f);
    return self->y;
}

/**
 * lrg_trigger_rect_set_y:
 * @self: A #LrgTriggerRect
 * @y: The Y coordinate
 *
 * Sets the Y coordinate of the rectangle origin.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_y (LrgTriggerRect *self,
                        gfloat          y)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    if (self->y != y)
    {
        self->y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
    }
}

/**
 * lrg_trigger_rect_set_position:
 * @self: A #LrgTriggerRect
 * @x: The X coordinate
 * @y: The Y coordinate
 *
 * Sets the position of the rectangle origin.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_position (LrgTriggerRect *self,
                               gfloat          x,
                               gfloat          y)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_trigger_rect_set_x (self, x);
    lrg_trigger_rect_set_y (self, y);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_trigger_rect_get_width:
 * @self: A #LrgTriggerRect
 *
 * Gets the width of the rectangle.
 *
 * Returns: The width
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_rect_get_width (LrgTriggerRect *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_RECT (self), 0.0f);
    return self->width;
}

/**
 * lrg_trigger_rect_set_width:
 * @self: A #LrgTriggerRect
 * @width: The width
 *
 * Sets the width of the rectangle.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_width (LrgTriggerRect *self,
                            gfloat          width)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));
    g_return_if_fail (width >= 0.0f);

    if (self->width != width)
    {
        self->width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    }
}

/**
 * lrg_trigger_rect_get_height:
 * @self: A #LrgTriggerRect
 *
 * Gets the height of the rectangle.
 *
 * Returns: The height
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_rect_get_height (LrgTriggerRect *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_RECT (self), 0.0f);
    return self->height;
}

/**
 * lrg_trigger_rect_set_height:
 * @self: A #LrgTriggerRect
 * @height: The height
 *
 * Sets the height of the rectangle.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_height (LrgTriggerRect *self,
                             gfloat          height)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));
    g_return_if_fail (height >= 0.0f);

    if (self->height != height)
    {
        self->height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
    }
}

/**
 * lrg_trigger_rect_set_size:
 * @self: A #LrgTriggerRect
 * @width: The width
 * @height: The height
 *
 * Sets the size of the rectangle.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_size (LrgTriggerRect *self,
                           gfloat          width,
                           gfloat          height)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_trigger_rect_set_width (self, width);
    lrg_trigger_rect_set_height (self, height);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_trigger_rect_set_rect:
 * @self: A #LrgTriggerRect
 * @x: The X coordinate
 * @y: The Y coordinate
 * @width: The width
 * @height: The height
 *
 * Sets all rectangle properties at once.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_rect (LrgTriggerRect *self,
                           gfloat          x,
                           gfloat          y,
                           gfloat          width,
                           gfloat          height)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    g_object_freeze_notify (G_OBJECT (self));
    lrg_trigger_rect_set_x (self, x);
    lrg_trigger_rect_set_y (self, y);
    lrg_trigger_rect_set_width (self, width);
    lrg_trigger_rect_set_height (self, height);
    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_trigger_rect_get_center:
 * @self: A #LrgTriggerRect
 * @out_x: (out) (nullable): Return location for center X
 * @out_y: (out) (nullable): Return location for center Y
 *
 * Gets the center point of the rectangle.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_get_center (LrgTriggerRect *self,
                             gfloat         *out_x,
                             gfloat         *out_y)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    if (out_x != NULL)
        *out_x = self->x + self->width * 0.5f;
    if (out_y != NULL)
        *out_y = self->y + self->height * 0.5f;
}

/**
 * lrg_trigger_rect_set_center:
 * @self: A #LrgTriggerRect
 * @x: The center X coordinate
 * @y: The center Y coordinate
 *
 * Sets the position so that the center is at the given coordinates.
 *
 * Since: 1.0
 */
void
lrg_trigger_rect_set_center (LrgTriggerRect *self,
                             gfloat          x,
                             gfloat          y)
{
    g_return_if_fail (LRG_IS_TRIGGER_RECT (self));

    lrg_trigger_rect_set_position (self,
                                   x - self->width * 0.5f,
                                   y - self->height * 0.5f);
}
