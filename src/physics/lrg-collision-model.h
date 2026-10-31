/* lrg-collision-model.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCollisionModel places one immutable LrgSolidGeometry in a world
 * transform. LrgSolidWorld registers models with generation-tagged
 * handles. This is collision only: no mass, joint or timestep.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-solid-geometry.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLLISION_MODEL (lrg_collision_model_get_type ())
#define LRG_TYPE_SOLID_WORLD (lrg_solid_world_get_type ())

/**
 * LRG_SOLID_MAX_MODELS:
 *
 * Most live or retired slots one #LrgSolidWorld retains.
 */
#define LRG_SOLID_MAX_MODELS (4096u)

/**
 * LrgSolidHandle:
 * @index: slot in the world
 * @generation: generation of that slot
 *
 * A zeroed handle is never issued. Removal keeps the generation so a
 * second remove is idempotent. The next add bumps the generation, and
 * the old handle is then rejected.
 */
typedef struct
{
    guint index;
    guint generation;
} LrgSolidHandle;

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCollisionModel, lrg_collision_model, LRG, COLLISION_MODEL, GObject)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSolidWorld, lrg_solid_world, LRG, SOLID_WORLD, GObject)

/**
 * lrg_collision_model_new:
 * @geometry: (transfer none): retained solid. The model holds a reference
 * @transform: (array fixed-size=16) (nullable): world transform, or
 *   %NULL for identity. Uniform positive scale, rotation and translation
 * @group: collision group bits
 * @mask: bits of other groups this model collides with
 * @caller_id: caller identity, stored and not interpreted
 * @error: (nullable): return location for a #GError
 *
 * Two models overlap only when each group's bits meet the other's mask.
 * Group 1 and mask 0xffffffff meet every default model.
 *
 * Returns: (transfer full): a new #LrgCollisionModel
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionModel *
lrg_collision_model_new (LrgSolidGeometry *geometry,
                         const gfloat     *transform,
                         guint32           group,
                         guint32           mask,
                         guint64           caller_id,
                         GError          **error);

/**
 * lrg_collision_model_set_transform:
 * @self: an #LrgCollisionModel
 * @transform: (array fixed-size=16): replacement world transform
 * @error: (nullable): return location for a #GError
 *
 * Validates @transform before replacing the stored one. A rejected
 * update leaves the previous transform and every query result unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_collision_model_set_transform (LrgCollisionModel *self,
                                   const gfloat      *transform,
                                   GError           **error);

/**
 * lrg_collision_model_get_transform:
 * @self: an #LrgCollisionModel
 * @transform: (out caller-allocates) (array fixed-size=16): copy of the
 *   world transform
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collision_model_get_transform (LrgCollisionModel *self,
                                   gfloat            *transform);

/**
 * lrg_collision_model_get_geometry:
 * @self: an #LrgCollisionModel
 *
 * Returns: (transfer none): the retained solid
 */
LRG_AVAILABLE_IN_ALL
LrgSolidGeometry *
lrg_collision_model_get_geometry (LrgCollisionModel *self);

/**
 * lrg_collision_model_get_filter:
 * @self: an #LrgCollisionModel
 * @group: (out) (nullable): collision group bits
 * @mask: (out) (nullable): bits of other groups this model meets
 *
 * Either output may be %NULL.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_collision_model_get_filter (LrgCollisionModel *self,
                                guint32           *group,
                                guint32           *mask);

/**
 * lrg_collision_model_get_caller_id:
 * @self: an #LrgCollisionModel
 *
 * Returns the identity stored at construction. The engine does not
 * interpret it.
 *
 * Returns: caller identity
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_collision_model_get_caller_id (LrgCollisionModel *self);

/**
 * lrg_solid_world_new:
 *
 * Creates an empty world. Models are not registered until
 * lrg_solid_world_add().
 *
 * Returns: (transfer full): a new #LrgSolidWorld
 */
LRG_AVAILABLE_IN_ALL
LrgSolidWorld *lrg_solid_world_new (void);

/**
 * lrg_solid_world_add:
 * @self: an #LrgSolidWorld
 * @model: (transfer none): model to register. The world holds a reference
 * @handle: (out caller-allocates): generation-tagged slot
 * @error: (nullable): return location for a #GError
 *
 * Reuses an empty slot when one exists. A full world or a %NULL model
 * fails before any slot changes.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_world_add (LrgSolidWorld     *self,
                     LrgCollisionModel *model,
                     LrgSolidHandle    *handle,
                     GError           **error);

/**
 * lrg_solid_world_remove:
 * @self: an #LrgSolidWorld
 * @handle: a handle from lrg_solid_world_add()
 * @error: (nullable): return location for a #GError
 *
 * Drops the world's reference. A second call with the same handle
 * succeeds and does nothing. A handle whose slot was reused, or a
 * handle that was never issued, fails with %G_IO_ERROR_NOT_FOUND.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_world_remove (LrgSolidWorld        *self,
                        LrgSolidHandle        handle,
                        GError              **error);

/**
 * lrg_solid_world_get_model:
 * @self: an #LrgSolidWorld
 * @handle: a live handle
 * @error: (nullable): return location for a #GError
 *
 * Returns: (transfer none) (nullable): the registered model, or %NULL
 *   on a stale or empty handle
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionModel *
lrg_solid_world_get_model (LrgSolidWorld *self,
                           LrgSolidHandle handle,
                           GError       **error);

/**
 * lrg_solid_world_classify_point:
 * @self: an #LrgSolidWorld
 * @handle: a live handle
 * @point: (array fixed-size=3): point in world space
 * @result: (out): classification
 * @error: (nullable): return location for a #GError
 *
 * Classifies @point against the model's baked solid.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_world_classify_point (LrgSolidWorld *self,
                                LrgSolidHandle handle,
                                const gfloat  *point,
                                LrgSolidClass *result,
                                GError       **error);

/**
 * lrg_solid_world_overlap:
 * @self: an #LrgSolidWorld
 * @a: first live handle
 * @b: second live handle
 * @contact: (out caller-allocates) (nullable): written only when overlapping
 * @overlapping: (out): set only on success
 * @error: (nullable): return location for a #GError
 *
 * Filters first. A mask miss is a successful separation, not an error.
 * A stale handle fails before either output is written. The pair rules
 * are lrg_solid_geometry_overlap(), evaluated in world space.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_world_overlap (LrgSolidWorld  *self,
                         LrgSolidHandle  a,
                         LrgSolidHandle  b,
                         LrgSolidContact *contact,
                         gboolean       *overlapping,
                         GError        **error);

/**
 * lrg_solid_world_contains:
 * @self: an #LrgSolidWorld
 * @outer: live outer handle
 * @inner: live inner handle
 * @contained: (out): set only on success
 * @error: (nullable): return location for a #GError
 *
 * World-space form of lrg_solid_geometry_contains().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_world_contains (LrgSolidWorld *self,
                          LrgSolidHandle outer,
                          LrgSolidHandle inner,
                          gboolean      *contained,
                          GError       **error);

/**
 * lrg_solid_world_sweep_sphere:
 * @self: an #LrgSolidWorld
 * @moving: live model that bakes to exactly one sphere
 * @delta: (array fixed-size=3): world-space motion of the sphere center
 * @against: live static model
 * @hit: (out caller-allocates) (nullable): earliest hit
 * @did_hit: (out): set only on success
 * @error: (nullable): return location for a #GError
 *
 * The moving model's scale is already in its baked radius. @delta is
 * not scaled again. A filter miss is a successful miss.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_solid_world_sweep_sphere (LrgSolidWorld *self,
                              LrgSolidHandle moving,
                              const gfloat  *delta,
                              LrgSolidHandle against,
                              LrgSolidHit   *hit,
                              gboolean      *did_hit,
                              GError       **error);

G_END_DECLS
