/* lrg-destructible-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDestructibleComponent: makes an entity breakable.  The entity's
 * shape (a cube #LrgMeshRendererComponent, or an #LrgFracture given
 * explicitly) is pre-fractured into Voronoi chunks (cached in a
 * `.rfracture` file); a contact impulse above the threshold on the
 * entity's #LrgRigidBodyComponent, or a call, swaps the intact object
 * for one dynamic body per chunk in the world's #LrgDynamicsWorld and
 * draws the chunks where their bodies are.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"
#include "../../physics/lrg-fracture.h"

G_BEGIN_DECLS

#define LRG_TYPE_DESTRUCTIBLE_COMPONENT (lrg_destructible_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDestructibleComponent, lrg_destructible_component, LRG, DESTRUCTIBLE_COMPONENT, LrgComponent)

/**
 * lrg_destructible_component_new:
 *
 * Creates a destructible with 12 random chunks, a 50 N·s threshold and
 * a density of 1000 kg/m³.
 *
 * Returns: (transfer full): a new #LrgDestructibleComponent
 */
LRG_AVAILABLE_IN_ALL
LrgDestructibleComponent * lrg_destructible_component_new (void);

/**
 * lrg_destructible_component_get_fracture:
 * @self: an #LrgDestructibleComponent
 *
 * Returns: (transfer none) (nullable): the fracture in use (set, or made
 *   by lrg_destructible_component_prepare())
 */
LRG_AVAILABLE_IN_ALL
LrgFracture * lrg_destructible_component_get_fracture (LrgDestructibleComponent *self);

/**
 * lrg_destructible_component_set_fracture:
 * @self: an #LrgDestructibleComponent
 * @fracture: (nullable): a built or loaded fracture in the entity's local
 *   space (scaled to the entity's scale), or %NULL to derive one
 *
 * Uses @fracture instead of deriving one from the entity's cube.  Its
 * parameters stay its own.
 */
LRG_AVAILABLE_IN_ALL
void lrg_destructible_component_set_fracture (LrgDestructibleComponent *self,
                                              LrgFracture              *fracture);

/**
 * lrg_destructible_component_prepare:
 * @self: an #LrgDestructibleComponent attached to an entity
 * @error: (nullable): return location for an error
 *
 * Makes sure chunks exist.  Without an explicit fracture the source is
 * the box of the entity's cube #LrgMeshRendererComponent (its size times
 * the entity's world scale) and the chunks come from
 * lrg_fracture_load_or_build() with #LrgDestructibleComponent:fracture-path
 * as the cache.  Called by the first break; call it at load time to keep
 * the build out of gameplay.
 *
 * Returns: %TRUE when chunks are available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_destructible_component_prepare (LrgDestructibleComponent  *self,
                                             GError                   **error);

/**
 * lrg_destructible_component_fracture:
 * @self: an #LrgDestructibleComponent in a world with physics
 * @impact_point: (array fixed-size=3) (nullable): world impact point
 * @impulse: (array fixed-size=3) (nullable): world impulse on the object
 * @error: (nullable): return location for an error
 *
 * Breaks the object now: lrg_fracture_swap_in() into the world's
 * physics, disables the entity's mesh renderer and rigid body
 * components (the intact body leaves on the next sync) and emits
 * #LrgDestructibleComponent::fractured.  Breaking a broken object does
 * nothing and returns %TRUE.
 *
 * Returns: %TRUE when the object is broken
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_destructible_component_fracture (LrgDestructibleComponent  *self,
                                              const gfloat              *impact_point,
                                              const gfloat              *impulse,
                                              GError                   **error);

/**
 * lrg_destructible_component_report_impact:
 * @self: an #LrgDestructibleComponent
 * @point: (array fixed-size=3): world contact point
 * @impulse: (array fixed-size=3): world impulse on the object (N·s)
 *
 * Feeds a hit.  At or above #LrgDestructibleComponent:impulse-threshold
 * the break is queued and happens at the next fixed update (so it is
 * safe from a physics signal handler).  With
 * #LrgDestructibleComponent:auto-break the component reports the
 * contacts of its rigid body itself.
 *
 * Returns: %TRUE when the hit will break the object
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_destructible_component_report_impact (LrgDestructibleComponent *self,
                                                   const gfloat             *point,
                                                   const gfloat             *impulse);

/**
 * lrg_destructible_component_restore:
 * @self: an #LrgDestructibleComponent
 *
 * Undoes a break: removes the chunk bodies and re-enables the components
 * the break disabled (play-in-editor reset, respawning props).
 */
LRG_AVAILABLE_IN_ALL
void lrg_destructible_component_restore (LrgDestructibleComponent *self);

/**
 * lrg_destructible_component_is_broken:
 * @self: an #LrgDestructibleComponent
 *
 * Returns: %TRUE once the chunks were swapped in
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_destructible_component_is_broken (LrgDestructibleComponent *self);

/**
 * lrg_destructible_component_get_chunk_count:
 * @self: an #LrgDestructibleComponent
 *
 * Returns: the number of chunk bodies (0 before the break)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_destructible_component_get_chunk_count (LrgDestructibleComponent *self);

/**
 * lrg_destructible_component_get_chunk_body:
 * @self: an #LrgDestructibleComponent
 * @index: chunk index
 *
 * Returns: the #LrgDynamicsWorld body id of chunk @index, or
 *   %G_MAXUINT out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_destructible_component_get_chunk_body (LrgDestructibleComponent *self,
                                                 guint                     index);

G_END_DECLS
