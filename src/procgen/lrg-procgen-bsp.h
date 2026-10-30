/* lrg-procgen-bsp.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: binary space partition rooms and corridors.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-procgen-generator.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_BSP (lrg_procgen_bsp_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenBsp, lrg_procgen_bsp, LRG, PROCGEN_BSP, LrgProcgenGenerator)

/**
 * lrg_procgen_bsp_new:
 * @width: grid width in cells
 * @height: grid height in cells
 *
 * Creates a generator of binary space partition rooms and corridors. Rooms in the leaves of a binary space partition, joined by L-shaped
 * corridors along the partition tree (every room is reachable), with doors
 * where a corridor meets a room wall.
 *
 * Returns: (transfer full): a new #LrgProcgenBsp
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenBsp * lrg_procgen_bsp_new (guint width,
                                     guint height);

/**
 * lrg_procgen_bsp_get_min_room_size:
 * @self: a generator
 *
 * Returns: the #LrgProcgenBsp:min-room-size property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_bsp_get_min_room_size (LrgProcgenBsp *self);

/**
 * lrg_procgen_bsp_set_min_room_size:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenBsp:min-room-size property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_bsp_set_min_room_size (LrgProcgenBsp *self,
                                        guint value);

/**
 * lrg_procgen_bsp_get_max_room_size:
 * @self: a generator
 *
 * Returns: the #LrgProcgenBsp:max-room-size property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_bsp_get_max_room_size (LrgProcgenBsp *self);

/**
 * lrg_procgen_bsp_set_max_room_size:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenBsp:max-room-size property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_bsp_set_max_room_size (LrgProcgenBsp *self,
                                        guint value);

/**
 * lrg_procgen_bsp_get_min_leaf_size:
 * @self: a generator
 *
 * Returns: the #LrgProcgenBsp:min-leaf-size property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_bsp_get_min_leaf_size (LrgProcgenBsp *self);

/**
 * lrg_procgen_bsp_set_min_leaf_size:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenBsp:min-leaf-size property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_bsp_set_min_leaf_size (LrgProcgenBsp *self,
                                        guint value);

/**
 * lrg_procgen_bsp_get_max_leaf_size:
 * @self: a generator
 *
 * Returns: the #LrgProcgenBsp:max-leaf-size property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_bsp_get_max_leaf_size (LrgProcgenBsp *self);

/**
 * lrg_procgen_bsp_set_max_leaf_size:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenBsp:max-leaf-size property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_bsp_set_max_leaf_size (LrgProcgenBsp *self,
                                        guint value);

/**
 * lrg_procgen_bsp_get_doors:
 * @self: a generator
 *
 * Returns: the #LrgProcgenBsp:doors property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_bsp_get_doors (LrgProcgenBsp *self);

/**
 * lrg_procgen_bsp_set_doors:
 * @self: a generator
 * @value: the new value
 *
 * Sets the #LrgProcgenBsp:doors property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_bsp_set_doors (LrgProcgenBsp *self,
                                gboolean value);

G_END_DECLS
