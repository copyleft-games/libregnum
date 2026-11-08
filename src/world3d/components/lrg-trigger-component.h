/* lrg-trigger-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTriggerComponent: a box or sphere volume around its entity.  Every
 * fixed update it tests the world positions of candidate entities (those
 * with a tag, those with a component type, or everything near it) and
 * emits #LrgTriggerComponent::entered and #LrgTriggerComponent::exited.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LrgTriggerShape:
 * @LRG_TRIGGER_SHAPE_BOX: an oriented box of
 *   #LrgTriggerComponent:size-x x size-y x size-z, centred on the entity
 *   and scaled and rotated with it
 * @LRG_TRIGGER_SHAPE_SPHERE: a sphere of #LrgTriggerComponent:radius
 *   (times the entity's largest world scale) at the entity
 *
 * The volume of an #LrgTriggerComponent.
 */
typedef enum
{
	LRG_TRIGGER_SHAPE_BOX,
	LRG_TRIGGER_SHAPE_SPHERE
} LrgTriggerShape;

LRG_AVAILABLE_IN_ALL
GType lrg_trigger_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRIGGER_SHAPE (lrg_trigger_shape_get_type ())

#define LRG_TYPE_TRIGGER_COMPONENT (lrg_trigger_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTriggerComponent, lrg_trigger_component, LRG, TRIGGER_COMPONENT, LrgComponent)

/**
 * lrg_trigger_component_new:
 *
 * Creates a 1 x 1 x 1 box trigger without filters (any entity in the
 * world counts, except the trigger's own).
 *
 * Returns: (transfer full): a new #LrgTriggerComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerComponent * lrg_trigger_component_new (void);

/**
 * lrg_trigger_component_new_box:
 * @size_x: box width
 * @size_y: box height
 * @size_z: box depth
 *
 * Returns: (transfer full): a new box #LrgTriggerComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerComponent * lrg_trigger_component_new_box (gfloat size_x,
                                                     gfloat size_y,
                                                     gfloat size_z);

/**
 * lrg_trigger_component_new_sphere:
 * @radius: sphere radius
 *
 * Returns: (transfer full): a new sphere #LrgTriggerComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerComponent * lrg_trigger_component_new_sphere (gfloat radius);

/**
 * lrg_trigger_component_get_shape:
 * @self: an #LrgTriggerComponent
 *
 * Returns: the volume shape
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerShape lrg_trigger_component_get_shape (LrgTriggerComponent *self);

/**
 * lrg_trigger_component_set_shape:
 * @self: an #LrgTriggerComponent
 * @shape: box or sphere
 *
 * Sets the volume shape.
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_set_shape (LrgTriggerComponent *self,
                                      LrgTriggerShape      shape);

/**
 * lrg_trigger_component_set_size:
 * @self: an #LrgTriggerComponent
 * @x: box width
 * @y: box height
 * @z: box depth
 *
 * Sets the box size (local units, before the entity's scale).
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_set_size (LrgTriggerComponent *self,
                                     gfloat               x,
                                     gfloat               y,
                                     gfloat               z);

/**
 * lrg_trigger_component_set_radius:
 * @self: an #LrgTriggerComponent
 * @radius: sphere radius (local units)
 *
 * Sets the sphere radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_set_radius (LrgTriggerComponent *self,
                                       gfloat               radius);

/**
 * lrg_trigger_component_get_filter_tag:
 * @self: an #LrgTriggerComponent
 *
 * Returns: (transfer none) (nullable): the tag candidates must carry
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_trigger_component_get_filter_tag (LrgTriggerComponent *self);

/**
 * lrg_trigger_component_set_filter_tag:
 * @self: an #LrgTriggerComponent
 * @tag: (nullable): an #LrgEntity3D tag, %NULL for no tag filter
 *
 * Only entities with @tag can trigger.
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_set_filter_tag (LrgTriggerComponent *self,
                                           const gchar         *tag);

/**
 * lrg_trigger_component_get_filter_component:
 * @self: an #LrgTriggerComponent
 *
 * Returns: (transfer none) (nullable): the component type name candidates
 *   must carry
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_trigger_component_get_filter_component (LrgTriggerComponent *self);

/**
 * lrg_trigger_component_set_filter_component:
 * @self: an #LrgTriggerComponent
 * @type_name: (nullable): a registered #LrgComponent type name (for
 *   example "LrgAudioListenerComponent"), %NULL for no component filter
 *
 * Only entities with a component of that type can trigger.  A name that
 * is not a registered #LrgComponent type matches nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_set_filter_component (LrgTriggerComponent *self,
                                                 const gchar         *type_name);

/**
 * lrg_trigger_component_contains_point:
 * @self: an #LrgTriggerComponent
 * @world_point: (array fixed-size=3): a world-space point
 *
 * Tests @world_point against the volume at the entity's current world
 * transform.
 *
 * Returns: %TRUE if the point is inside (always %FALSE without an entity)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_trigger_component_contains_point (LrgTriggerComponent *self,
                                               const gfloat        *world_point);

/**
 * lrg_trigger_component_is_inside:
 * @self: an #LrgTriggerComponent
 * @entity: an entity
 *
 * Returns: %TRUE if @entity entered and has not exited yet
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_trigger_component_is_inside (LrgTriggerComponent *self,
                                          LrgEntity3D         *entity);

/**
 * lrg_trigger_component_get_n_inside:
 * @self: an #LrgTriggerComponent
 *
 * Returns: how many entities are inside
 */
LRG_AVAILABLE_IN_ALL
guint lrg_trigger_component_get_n_inside (LrgTriggerComponent *self);

/**
 * lrg_trigger_component_evaluate:
 * @self: an #LrgTriggerComponent
 *
 * Runs the overlap test now and emits the enter/exit signals
 * (fixed_update() calls it every fixed step).  Does nothing outside a
 * world, while disabled, or after a one-shot trigger fired.
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_evaluate (LrgTriggerComponent *self);

/**
 * lrg_trigger_component_reset:
 * @self: an #LrgTriggerComponent
 *
 * Forgets every entity inside (without exit signals) and re-arms a fired
 * one-shot trigger.
 */
LRG_AVAILABLE_IN_ALL
void lrg_trigger_component_reset (LrgTriggerComponent *self);

G_END_DECLS
