/* lrg-tiled-world.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTiledWorld: an LrgWorldAdapter over a Tiled map.  Places are the
 * map's named objects, walls come from collision layers, solid tiles
 * and obstacle objects, actors are LrgActorMovers walking A* routes on
 * the map's grid, and the world draws itself (tiles, then actors as
 * coloured discs) through a 2D camera that frames the whole map or
 * follows an actor.  Simple on purpose: it is the reference
 * implementation of the interface and a usable 2D office or town.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-world-adapter.h"
#include "../tilemap/lrg-tiled-map.h"
#include "../pathfinding/lrg-nav-grid.h"
#include "../pathfinding/lrg-actor-mover.h"
#include "../graphics/lrg-camera2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_TILED_WORLD (lrg_tiled_world_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTiledWorld, lrg_tiled_world, LRG, TILED_WORLD, GObject)

/**
 * lrg_tiled_world_new:
 *
 * Creates an empty world; load a map with lrg_world_adapter_load().
 * Actors walk 96 map pixels a second.
 *
 * Returns: (transfer full): a new #LrgTiledWorld
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledWorld * lrg_tiled_world_new (void);

/**
 * lrg_tiled_world_get_map:
 * @self: an #LrgTiledWorld
 *
 * Returns: (transfer none) (nullable): the loaded map
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgTiledMap * lrg_tiled_world_get_map (LrgTiledWorld *self);

/**
 * lrg_tiled_world_get_nav_grid:
 * @self: an #LrgTiledWorld
 *
 * The walkable grid, one cell per map tile.  A cell is blocked when
 *
 * - a tile layer holds a tile there and the layer has a true
 *   "collision" or "solid" property, or is named "collision" or
 *   "walls" (the last part of its group path);
 * - any tile layer holds a tile there whose tile properties have a true
 *   "solid" or "collision";
 * - its centre is inside an area object whose type is "obstacle" or
 *   "solid", or which has a true "solid" property.
 *
 * Change it at run time (a door closing) and actors re-path.
 *
 * Returns: (transfer none) (nullable): the grid
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavGrid * lrg_tiled_world_get_nav_grid (LrgTiledWorld *self);

/**
 * lrg_tiled_world_get_camera:
 * @self: an #LrgTiledWorld
 *
 * Returns: (transfer none): the 2D camera the world draws with (move it
 *   yourself in %LRG_WORLD_CAMERA_FREE)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgCamera2D * lrg_tiled_world_get_camera (LrgTiledWorld *self);

/**
 * lrg_tiled_world_set_viewport:
 * @self: an #LrgTiledWorld
 * @width: width of the view in pixels (> 0)
 * @height: height of the view in pixels (> 0)
 *
 * Tells the camera how big the view is (the frame size when drawing
 * into an #LrgFrameExport), so the overview fits and the follow camera
 * centres.  Default 1280x720.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_world_set_viewport (LrgTiledWorld *self,
                                   gint           width,
                                   gint           height);

/**
 * lrg_tiled_world_set_actor_speed:
 * @self: an #LrgTiledWorld
 * @speed: map pixels per second for actors spawned from now on (> 0)
 *
 * Sets the walking speed of new actors.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_tiled_world_set_actor_speed (LrgTiledWorld *self,
                                      gfloat         speed);

/**
 * lrg_tiled_world_get_actor_mover:
 * @self: an #LrgTiledWorld
 * @handle: an actor handle
 *
 * The mover behind an actor, for its state, facing and animation name.
 *
 * Returns: (transfer none) (nullable): the mover
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgActorMover * lrg_tiled_world_get_actor_mover (LrgTiledWorld *self,
                                                 guint          handle);

/**
 * lrg_tiled_world_find_actor:
 * @self: an #LrgTiledWorld
 * @id: the id the actor was spawned with
 *
 * Returns: its handle, 0 when there is none
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_world_find_actor (LrgTiledWorld *self,
                                  const gchar   *id);

/**
 * lrg_tiled_world_get_n_actors:
 * @self: an #LrgTiledWorld
 *
 * Returns: the number of actors
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tiled_world_get_n_actors (LrgTiledWorld *self);

G_END_DECLS
