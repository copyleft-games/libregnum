/* lrg-decal-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecalComponent: a projected decal (LrgDecal) that follows its
 * entity and registers itself with its world's LrgDecalSet.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-decal.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECAL_COMPONENT (lrg_decal_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDecalComponent, lrg_decal_component, LRG, DECAL_COMPONENT, LrgComponent)

/**
 * LrgDecalTextureFunc:
 * @path: a texture path (or `guid:`) from the component
 * @user_data: (closure): data passed to lrg_decal_component_sync_world()
 *
 * Resolves a texture path for components without an asset manager (the
 * world renderer passes one built on lrg_world3d_renderer_get_texture()).
 *
 * Returns: (transfer none) (nullable): the texture, or %NULL
 */
typedef GrlTexture * (*LrgDecalTextureFunc) (const gchar *path,
                                             gpointer     user_data);

/**
 * lrg_decal_component_new:
 *
 * Creates a 1 x 1 x 1 decal component (see lrg_decal_new() for the
 * defaults of the decal it owns).
 *
 * Returns: (transfer full): a new #LrgDecalComponent
 */
LRG_AVAILABLE_IN_ALL
LrgDecalComponent * lrg_decal_component_new (void);

/**
 * lrg_decal_component_get_decal:
 * @self: an #LrgDecalComponent
 *
 * The decal the component drives.  Its colour, fades, sort order, layer
 * mask, blend, channels and material values are also the component's
 * properties of the same names; its transform is rewritten on every
 * sync from the entity (do not set it directly).
 *
 * Returns: (transfer none): the decal
 */
LRG_AVAILABLE_IN_ALL
LrgDecal * lrg_decal_component_get_decal (LrgDecalComponent *self);

/**
 * lrg_decal_component_set_size:
 * @self: an #LrgDecalComponent
 * @x: box width along the entity's local X (> 0)
 * @y: projection depth along local Y (> 0)
 * @z: box height along local Z (> 0)
 *
 * The projector box in the entity's local space (the entity's scale
 * multiplies it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_set_size (LrgDecalComponent *self,
                                   gfloat             x,
                                   gfloat             y,
                                   gfloat             z);

/**
 * lrg_decal_component_get_size:
 * @self: an #LrgDecalComponent
 * @out_size: (out caller-allocates) (array fixed-size=3): the box size
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_get_size (LrgDecalComponent *self,
                                   gfloat            *out_size);

/**
 * lrg_decal_component_set_albedo_path:
 * @self: an #LrgDecalComponent
 * @path: (nullable): albedo texture path or `guid:`
 *
 * Resolved lazily through the asset manager, or the resolver given to
 * lrg_decal_component_sync_world().
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_set_albedo_path (LrgDecalComponent *self,
                                          const gchar       *path);

/**
 * lrg_decal_component_get_albedo_path:
 * @self: an #LrgDecalComponent
 *
 * Returns: (nullable): the albedo texture path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_decal_component_get_albedo_path (LrgDecalComponent *self);

/**
 * lrg_decal_component_set_normal_path:
 * @self: an #LrgDecalComponent
 * @path: (nullable): normal map path or `guid:`
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_set_normal_path (LrgDecalComponent *self,
                                          const gchar       *path);

/**
 * lrg_decal_component_get_normal_path:
 * @self: an #LrgDecalComponent
 *
 * Returns: (nullable): the normal map path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_decal_component_get_normal_path (LrgDecalComponent *self);

/**
 * lrg_decal_component_set_orm_path:
 * @self: an #LrgDecalComponent
 * @path: (nullable): ORM map path or `guid:`
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_set_orm_path (LrgDecalComponent *self,
                                       const gchar       *path);

/**
 * lrg_decal_component_get_orm_path:
 * @self: an #LrgDecalComponent
 *
 * Returns: (nullable): the ORM map path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_decal_component_get_orm_path (LrgDecalComponent *self);

/**
 * lrg_decal_component_set_asset_manager:
 * @self: an #LrgDecalComponent
 * @manager: (nullable): the asset manager the texture paths load through
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_set_asset_manager (LrgDecalComponent *self,
                                            LrgAssetManager   *manager);

/**
 * lrg_decal_component_get_asset_manager:
 * @self: an #LrgDecalComponent
 *
 * Returns: (transfer none) (nullable): the asset manager
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_decal_component_get_asset_manager (LrgDecalComponent *self);

/**
 * lrg_decal_component_sync:
 * @self: an #LrgDecalComponent
 *
 * Copies the entity's world matrix times the size into the decal and
 * enables it only while the component is enabled and the entity is
 * active and visible in its hierarchy; loads texture paths through the
 * asset manager when one is set.  Runs in late_update automatically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_component_sync (LrgDecalComponent *self);

/**
 * lrg_decal_component_get_world_set:
 * @world: an #LrgWorld3D
 *
 * The decal set of @world, made on first use and owned by the world:
 * decal components add their decal to it when their entity enters the
 * world and remove it when it leaves.  The renderer binds it to the
 * light set (lrg_light_set_set_decals()) after
 * lrg_decal_component_sync_world() and lrg_decal_set_select().
 *
 * Returns: (transfer none): the world's decal set
 */
LRG_AVAILABLE_IN_ALL
LrgDecalSet * lrg_decal_component_get_world_set (LrgWorld3D *world);

/**
 * lrg_decal_component_sync_world:
 * @world: an #LrgWorld3D
 * @resolver: (nullable) (scope call): resolves texture paths of
 *   components without an asset manager
 * @user_data: data for @resolver
 *
 * lrg_decal_component_sync() on every decal component of @world
 * (including disabled ones and inactive entities, which it turns off),
 * resolving missing textures through @resolver.  Call once per frame
 * before lrg_decal_set_select().
 *
 * Returns: the number of decal components visited
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_component_sync_world (LrgWorld3D          *world,
                                      LrgDecalTextureFunc  resolver,
                                      gpointer             user_data);

G_END_DECLS
