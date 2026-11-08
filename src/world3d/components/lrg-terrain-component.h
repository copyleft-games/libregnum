/* lrg-terrain-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainComponent: draws an #LrgTerrainAsset on an entity and gives
 * it a static heightfield body.
 *
 * Every chunk of the asset becomes one mesh render item per frame at the
 * LOD its distance to the eye selects, textured with the chunk's baked
 * splat albedo (re-baked and re-meshed only for the chunks an edit
 * touched), so terrain is lit, shadowed, culled and layered like any
 * other mesh.  The asset's foliage is drawn too (instanced, see
 * #LrgFoliage).  In a world with physics the component owns one static
 * heightfield body (rebuilt after height or hole edits).  The level node
 * kind `terrain` instantiates it with the `.rterrain` as its asset.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-terrain-asset.h"

G_BEGIN_DECLS

#define LRG_TYPE_TERRAIN_COMPONENT (lrg_terrain_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainComponent, lrg_terrain_component, LRG, TERRAIN_COMPONENT, LrgComponent)

/**
 * lrg_terrain_component_new:
 *
 * Creates a component without an asset (shadows on, physics on, foliage
 * on).
 *
 * Returns: (transfer full): a new #LrgTerrainComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainComponent * lrg_terrain_component_new (void);

/**
 * lrg_terrain_component_new_with_asset:
 * @asset: the terrain to draw
 *
 * Returns: (transfer full): a new #LrgTerrainComponent drawing @asset
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainComponent * lrg_terrain_component_new_with_asset (LrgTerrainAsset *asset);

/**
 * lrg_terrain_component_get_asset:
 * @self: an #LrgTerrainComponent
 *
 * The asset set explicitly or loaded from #LrgTerrainComponent:asset-path
 * (loaded on first use).
 *
 * Returns: (transfer none) (nullable): the terrain asset
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_terrain_component_get_asset (LrgTerrainComponent *self);

/**
 * lrg_terrain_component_set_asset:
 * @self: an #LrgTerrainComponent
 * @asset: (nullable): the terrain asset
 *
 * Replaces the asset (dropping meshes, textures and the body).
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_component_set_asset (LrgTerrainComponent *self,
                                      LrgTerrainAsset     *asset);

/**
 * lrg_terrain_component_get_asset_path:
 * @self: an #LrgTerrainComponent
 *
 * Returns: (nullable): the `.rterrain` path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_terrain_component_get_asset_path (LrgTerrainComponent *self);

/**
 * lrg_terrain_component_set_asset_path:
 * @self: an #LrgTerrainComponent
 * @path: (nullable): a `.rterrain` file; loaded the first time the asset
 *   is needed (a failure is logged once)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_component_set_asset_path (LrgTerrainComponent *self,
                                           const gchar         *path);

/**
 * lrg_terrain_component_get_cast_shadows:
 * @self: an #LrgTerrainComponent
 *
 * Returns: whether the chunks are shadow casters
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_component_get_cast_shadows (LrgTerrainComponent *self);

/**
 * lrg_terrain_component_set_cast_shadows:
 * @self: an #LrgTerrainComponent
 * @cast: whether the chunks cast shadows
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_component_set_cast_shadows (LrgTerrainComponent *self,
                                             gboolean             cast);

/**
 * lrg_terrain_component_get_physics:
 * @self: an #LrgTerrainComponent
 *
 * Returns: whether the component builds a heightfield body
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_component_get_physics (LrgTerrainComponent *self);

/**
 * lrg_terrain_component_set_physics:
 * @self: an #LrgTerrainComponent
 * @physics: whether to build a static heightfield body in the world's
 *   #LrgDynamicsWorld
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_component_set_physics (LrgTerrainComponent *self,
                                        gboolean             physics);

/**
 * lrg_terrain_component_get_foliage:
 * @self: an #LrgTerrainComponent
 *
 * Returns: whether the asset's foliage is drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_component_get_foliage (LrgTerrainComponent *self);

/**
 * lrg_terrain_component_set_foliage:
 * @self: an #LrgTerrainComponent
 * @foliage: whether to draw the asset's foliage
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_component_set_foliage (LrgTerrainComponent *self,
                                        gboolean             foliage);

/**
 * lrg_terrain_component_get_body_id:
 * @self: an #LrgTerrainComponent
 *
 * Returns: the heightfield body, or %LRG_PHYSICS_NO_BODY
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_component_get_body_id (LrgTerrainComponent *self);

/**
 * lrg_terrain_component_get_chunk_lod:
 * @self: an #LrgTerrainComponent
 * @cx: chunk column
 * @cz: chunk row
 *
 * Returns: the LOD the chunk was last drawn at, or -1 when it was not
 *   collected (culled, or no asset)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_terrain_component_get_chunk_lod (LrgTerrainComponent *self,
                                          guint                cx,
                                          guint                cz);

/**
 * lrg_terrain_component_height_at:
 * @self: an #LrgTerrainComponent
 * @x: world X
 * @z: world Z
 * @out_height: (out) (optional): the world height of the ground there
 *
 * Samples the terrain under a world position through the entity's world
 * transform (translation, rotation and scale).
 *
 * Returns: %TRUE over the terrain (and not over a hole)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_component_height_at (LrgTerrainComponent *self,
                                          gfloat               x,
                                          gfloat               z,
                                          gfloat              *out_height);

/**
 * lrg_terrain_component_raycast:
 * @self: an #LrgTerrainComponent
 * @origin: (array fixed-size=3): world ray origin
 * @direction: (array fixed-size=3): world ray direction
 * @max_distance: longest hit distance in world units
 * @out_world: (out caller-allocates) (array fixed-size=3) (optional): the
 *   world hit point
 * @out_local: (out caller-allocates) (array fixed-size=3) (optional): the
 *   hit in the asset's local space (what brushes take)
 *
 * The brush cursor pick: lrg_terrain_asset_raycast() through the entity
 * transform.
 *
 * Returns: %TRUE on a hit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_component_raycast (LrgTerrainComponent *self,
                                        const gfloat        *origin,
                                        const gfloat        *direction,
                                        gfloat               max_distance,
                                        gfloat              *out_world,
                                        gfloat              *out_local);

/**
 * lrg_terrain_component_release_gpu:
 * @self: an #LrgTerrainComponent
 *
 * Frees chunk meshes, baked textures and foliage GPU data while the GL
 * context lives (they are made again on the next draw).
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_component_release_gpu (LrgTerrainComponent *self);

G_END_DECLS
