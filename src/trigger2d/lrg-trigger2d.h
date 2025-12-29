/* lrg-trigger2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 2D triggers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER2D (lrg_trigger2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTrigger2D, lrg_trigger2d, LRG, TRIGGER2D, GObject)

/**
 * LrgTrigger2DClass:
 * @parent_class: Parent class
 * @test_point: Test if a point is inside the trigger
 * @get_bounds: Get the axis-aligned bounding box
 * @get_shape: Get the trigger shape type
 *
 * The class structure for #LrgTrigger2D.
 * Subclasses implement specific shape tests (rectangle, circle, polygon).
 */
struct _LrgTrigger2DClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgTrigger2DClass::test_point:
     * @self: A #LrgTrigger2D
     * @x: X coordinate to test
     * @y: Y coordinate to test
     *
     * Tests if a point is inside the trigger zone.
     *
     * Returns: %TRUE if the point is inside
     */
    gboolean         (*test_point)  (LrgTrigger2D *self,
                                     gfloat        x,
                                     gfloat        y);

    /**
     * LrgTrigger2DClass::get_bounds:
     * @self: A #LrgTrigger2D
     * @out_x: (out): Return location for X
     * @out_y: (out): Return location for Y
     * @out_width: (out): Return location for width
     * @out_height: (out): Return location for height
     *
     * Gets the axis-aligned bounding box of the trigger.
     */
    void             (*get_bounds)  (LrgTrigger2D *self,
                                     gfloat       *out_x,
                                     gfloat       *out_y,
                                     gfloat       *out_width,
                                     gfloat       *out_height);

    /**
     * LrgTrigger2DClass::get_shape:
     * @self: A #LrgTrigger2D
     *
     * Gets the shape type of the trigger.
     *
     * Returns: The #LrgTrigger2DShape
     */
    LrgTrigger2DShape (*get_shape)  (LrgTrigger2D *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Point testing */

/**
 * lrg_trigger2d_test_point:
 * @self: A #LrgTrigger2D
 * @x: X coordinate to test
 * @y: Y coordinate to test
 *
 * Tests if a point is inside the trigger zone.
 *
 * Returns: %TRUE if the point is inside
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger2d_test_point        (LrgTrigger2D *self,
                                                     gfloat        x,
                                                     gfloat        y);

/**
 * lrg_trigger2d_get_bounds:
 * @self: A #LrgTrigger2D
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 * @out_width: (out) (nullable): Return location for width
 * @out_height: (out) (nullable): Return location for height
 *
 * Gets the axis-aligned bounding box of the trigger.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_get_bounds        (LrgTrigger2D *self,
                                                     gfloat       *out_x,
                                                     gfloat       *out_y,
                                                     gfloat       *out_width,
                                                     gfloat       *out_height);

/**
 * lrg_trigger2d_get_shape:
 * @self: A #LrgTrigger2D
 *
 * Gets the shape type of the trigger.
 *
 * Returns: The #LrgTrigger2DShape
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTrigger2DShape   lrg_trigger2d_get_shape         (LrgTrigger2D *self);

/* Identification */

/**
 * lrg_trigger2d_get_id:
 * @self: A #LrgTrigger2D
 *
 * Gets the unique identifier of the trigger.
 *
 * Returns: (transfer none) (nullable): The trigger ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_trigger2d_get_id            (LrgTrigger2D *self);

/**
 * lrg_trigger2d_set_id:
 * @self: A #LrgTrigger2D
 * @id: (nullable): The trigger ID
 *
 * Sets the unique identifier of the trigger.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_set_id            (LrgTrigger2D *self,
                                                     const gchar  *id);

/* State */

/**
 * lrg_trigger2d_is_enabled:
 * @self: A #LrgTrigger2D
 *
 * Checks if the trigger is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger2d_is_enabled        (LrgTrigger2D *self);

/**
 * lrg_trigger2d_set_enabled:
 * @self: A #LrgTrigger2D
 * @enabled: Whether the trigger is enabled
 *
 * Sets whether the trigger is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_set_enabled       (LrgTrigger2D *self,
                                                     gboolean      enabled);

/**
 * lrg_trigger2d_is_one_shot:
 * @self: A #LrgTrigger2D
 *
 * Checks if the trigger fires only once.
 *
 * Returns: %TRUE if one-shot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger2d_is_one_shot       (LrgTrigger2D *self);

/**
 * lrg_trigger2d_set_one_shot:
 * @self: A #LrgTrigger2D
 * @one_shot: Whether the trigger fires only once
 *
 * Sets whether the trigger fires only once.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_set_one_shot      (LrgTrigger2D *self,
                                                     gboolean      one_shot);

/**
 * lrg_trigger2d_has_fired:
 * @self: A #LrgTrigger2D
 *
 * Checks if a one-shot trigger has already fired.
 *
 * Returns: %TRUE if has fired
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger2d_has_fired         (LrgTrigger2D *self);

/**
 * lrg_trigger2d_reset:
 * @self: A #LrgTrigger2D
 *
 * Resets a one-shot trigger so it can fire again.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_reset             (LrgTrigger2D *self);

/* Cooldown */

/**
 * lrg_trigger2d_get_cooldown:
 * @self: A #LrgTrigger2D
 *
 * Gets the cooldown period between trigger events.
 *
 * Returns: Cooldown in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger2d_get_cooldown      (LrgTrigger2D *self);

/**
 * lrg_trigger2d_set_cooldown:
 * @self: A #LrgTrigger2D
 * @cooldown: Cooldown in seconds
 *
 * Sets the cooldown period between trigger events.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_set_cooldown      (LrgTrigger2D *self,
                                                     gfloat        cooldown);

/**
 * lrg_trigger2d_is_on_cooldown:
 * @self: A #LrgTrigger2D
 *
 * Checks if the trigger is currently on cooldown.
 *
 * Returns: %TRUE if on cooldown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger2d_is_on_cooldown    (LrgTrigger2D *self);

/**
 * lrg_trigger2d_update_cooldown:
 * @self: A #LrgTrigger2D
 * @delta_time: Time elapsed since last update
 *
 * Updates the cooldown timer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_update_cooldown   (LrgTrigger2D *self,
                                                     gfloat        delta_time);

/* Collision layers */

/**
 * lrg_trigger2d_get_collision_layer:
 * @self: A #LrgTrigger2D
 *
 * Gets the collision layer this trigger belongs to.
 *
 * Returns: The collision layer bitmask
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint32             lrg_trigger2d_get_collision_layer (LrgTrigger2D *self);

/**
 * lrg_trigger2d_set_collision_layer:
 * @self: A #LrgTrigger2D
 * @layer: Collision layer bitmask
 *
 * Sets the collision layer this trigger belongs to.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_set_collision_layer (LrgTrigger2D *self,
                                                       guint32       layer);

/**
 * lrg_trigger2d_get_collision_mask:
 * @self: A #LrgTrigger2D
 *
 * Gets the collision mask for what this trigger can detect.
 *
 * Returns: The collision mask bitmask
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint32             lrg_trigger2d_get_collision_mask  (LrgTrigger2D *self);

/**
 * lrg_trigger2d_set_collision_mask:
 * @self: A #LrgTrigger2D
 * @mask: Collision mask bitmask
 *
 * Sets the collision mask for what this trigger can detect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger2d_set_collision_mask  (LrgTrigger2D *self,
                                                       guint32       mask);

/**
 * lrg_trigger2d_can_collide_with:
 * @self: A #LrgTrigger2D
 * @other_layer: The collision layer of the other entity
 *
 * Checks if this trigger can collide with an entity on the given layer.
 *
 * Returns: %TRUE if collision is possible
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger2d_can_collide_with    (LrgTrigger2D *self,
                                                       guint32       other_layer);

G_END_DECLS
