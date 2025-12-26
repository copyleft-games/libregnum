/* lrg-collider-component.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Collision bounds component.
 *
 * LrgColliderComponent defines collision bounds for a game object.
 * It supports collision layers and masks for filtering which objects
 * can collide with each other.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>

#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLLIDER_COMPONENT (lrg_collider_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColliderComponent, lrg_collider_component,
                      LRG, COLLIDER_COMPONENT, LrgComponent)

/*
 * Construction
 */

/**
 * lrg_collider_component_new:
 *
 * Creates a new collider component with default bounds (0, 0, 0, 0).
 *
 * Returns: (transfer full): A new #LrgColliderComponent
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent * lrg_collider_component_new           (void);

/**
 * lrg_collider_component_new_with_bounds:
 * @x: X offset from entity position
 * @y: Y offset from entity position
 * @width: collision width
 * @height: collision height
 *
 * Creates a new collider component with the specified bounds.
 *
 * Returns: (transfer full): A new #LrgColliderComponent
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent * lrg_collider_component_new_with_bounds (gfloat x,
                                                               gfloat y,
                                                               gfloat width,
                                                               gfloat height);

/*
 * Bounds (relative to entity position)
 */

/**
 * lrg_collider_component_set_bounds:
 * @self: an #LrgColliderComponent
 * @x: X offset from entity position
 * @y: Y offset from entity position
 * @width: collision width
 * @height: collision height
 *
 * Sets the collision bounds relative to the entity's position.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_collider_component_set_bounds      (LrgColliderComponent *self,
                                                      gfloat                x,
                                                      gfloat                y,
                                                      gfloat                width,
                                                      gfloat                height);

/**
 * lrg_collider_component_get_bounds:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision bounds relative to the entity's position.
 *
 * Returns: (transfer full): The bounds rectangle
 */
LRG_AVAILABLE_IN_ALL
GrlRectangle * lrg_collider_component_get_bounds     (LrgColliderComponent *self);

/**
 * lrg_collider_component_get_world_bounds:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision bounds in world coordinates.
 *
 * This combines the entity's position with the relative bounds
 * to produce the actual collision area in world space.
 *
 * Returns: (transfer full) (nullable): The world bounds, or %NULL if no owner
 */
LRG_AVAILABLE_IN_ALL
GrlRectangle * lrg_collider_component_get_world_bounds (LrgColliderComponent *self);

/*
 * Collision Enable/Disable
 */

/**
 * lrg_collider_component_set_collision_enabled:
 * @self: an #LrgColliderComponent
 * @enabled: whether collision checking is enabled
 *
 * Sets whether collision checking is enabled for this collider.
 *
 * Disabled colliders are ignored during collision detection.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_collider_component_set_collision_enabled (LrgColliderComponent *self,
                                                            gboolean              enabled);

/**
 * lrg_collider_component_get_collision_enabled:
 * @self: an #LrgColliderComponent
 *
 * Gets whether collision checking is enabled.
 *
 * Returns: %TRUE if collision checking is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_collider_component_get_collision_enabled (LrgColliderComponent *self);

/*
 * Collision Layers
 */

/**
 * lrg_collider_component_set_layer:
 * @self: an #LrgColliderComponent
 * @layer: the collision layer bitmask
 *
 * Sets the collision layer(s) this collider belongs to.
 *
 * An object collides with another if (a.layer & b.mask) != 0.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_collider_component_set_layer       (LrgColliderComponent *self,
                                                      guint32               layer);

/**
 * lrg_collider_component_get_layer:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision layer bitmask.
 *
 * Returns: The layer bitmask
 */
LRG_AVAILABLE_IN_ALL
guint32       lrg_collider_component_get_layer       (LrgColliderComponent *self);

/**
 * lrg_collider_component_set_mask:
 * @self: an #LrgColliderComponent
 * @mask: the collision mask bitmask
 *
 * Sets which layers this collider can collide with.
 *
 * An object collides with another if (a.layer & b.mask) != 0.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_collider_component_set_mask        (LrgColliderComponent *self,
                                                      guint32               mask);

/**
 * lrg_collider_component_get_mask:
 * @self: an #LrgColliderComponent
 *
 * Gets the collision mask bitmask.
 *
 * Returns: The mask bitmask
 */
LRG_AVAILABLE_IN_ALL
guint32       lrg_collider_component_get_mask        (LrgColliderComponent *self);

/*
 * Collision Testing
 */

/**
 * lrg_collider_component_intersects:
 * @self: an #LrgColliderComponent
 * @other: another #LrgColliderComponent
 *
 * Tests whether this collider intersects with another.
 *
 * This only checks bounds intersection, not layer/mask filtering.
 *
 * Returns: %TRUE if the colliders intersect
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_collider_component_intersects      (LrgColliderComponent *self,
                                                      LrgColliderComponent *other);

/**
 * lrg_collider_component_can_collide_with:
 * @self: an #LrgColliderComponent
 * @other: another #LrgColliderComponent
 *
 * Tests whether this collider can collide with another based on layers.
 *
 * Returns: %TRUE if collision is possible (layer/mask match)
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_collider_component_can_collide_with (LrgColliderComponent *self,
                                                       LrgColliderComponent *other);

G_END_DECLS
