/* lrg-procgen-wfc.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: wave function collapse (simple tiled model).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-procgen-generator.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_WFC (lrg_procgen_wfc_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenWfc, lrg_procgen_wfc, LRG, PROCGEN_WFC, LrgProcgenGenerator)

/**
 * LRG_PROCGEN_WFC_MAX_TILES:
 *
 * The largest number of tiles a wave function collapse model holds.
 */
#define LRG_PROCGEN_WFC_MAX_TILES (64)

/**
 * lrg_procgen_wfc_new:
 * @width: grid width in cells
 * @height: grid height in cells
 *
 * Creates a wave function collapse generator (simple tiled model) with no
 * tiles. Add tiles and adjacency rules with lrg_procgen_wfc_add_tile() and
 * lrg_procgen_wfc_allow(), or learn both from a sample grid with
 * lrg_procgen_wfc_learn().
 *
 * Generation keeps, for every cell, the set of tiles still possible. It
 * repeatedly collapses the cell of lowest Shannon entropy (computed in
 * fixed point, ties broken by the random stream) to one tile picked by
 * weight, then propagates the adjacency rules. On a contradiction it
 * backtracks through up to #LrgProcgenWfc:backtrack-depth recent choices,
 * banning the choice that failed; when that is exhausted (or after
 * #LrgProcgenWfc:max-backtracks) it restarts, up to
 * #LrgProcgenWfc:max-attempts times, then fails with
 * %LRG_PROCGEN_ERROR_CONTRADICTION.
 *
 * Returns: (transfer full): a new #LrgProcgenWfc
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenWfc * lrg_procgen_wfc_new (guint width,
                                     guint height);

/**
 * lrg_procgen_wfc_add_tile:
 * @self: a generator
 * @name: unique tile name
 * @tag: the cell tag written for this tile
 * @weight: relative frequency (> 0)
 *
 * Adds a tile. It has no allowed neighbours until rules are added.
 *
 * Returns: the tile index, or -1 when the name exists or the model is full
 */
LRG_AVAILABLE_IN_ALL
gint lrg_procgen_wfc_add_tile (LrgProcgenWfc *self,
                               const gchar   *name,
                               guint8         tag,
                               gdouble        weight);

/**
 * lrg_procgen_wfc_get_tile_count:
 * @self: a generator
 *
 * Returns: the number of tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_wfc_get_tile_count (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_find_tile:
 * @self: a generator
 * @name: a tile name
 *
 * Returns: the tile's index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_procgen_wfc_find_tile (LrgProcgenWfc *self,
                                const gchar   *name);

/**
 * lrg_procgen_wfc_get_tile_name:
 * @self: a generator
 * @index: tile index
 *
 * Returns: (transfer none) (nullable): the tile's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_procgen_wfc_get_tile_name (LrgProcgenWfc *self,
                                             guint          index);

/**
 * lrg_procgen_wfc_get_tile_tag:
 * @self: a generator
 * @index: tile index
 *
 * Returns: the tile's cell tag
 */
LRG_AVAILABLE_IN_ALL
guint8 lrg_procgen_wfc_get_tile_tag (LrgProcgenWfc *self,
                                     guint          index);

/**
 * lrg_procgen_wfc_get_tile_weight:
 * @self: a generator
 * @index: tile index
 *
 * Returns: the tile's weight
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_procgen_wfc_get_tile_weight (LrgProcgenWfc *self,
                                         guint          index);

/**
 * lrg_procgen_wfc_allow:
 * @self: a generator
 * @a: a tile index
 * @b: a tile index
 * @direction: where @b lies relative to @a
 *
 * Allows @b next to @a in @direction (and so @a next to @b in the
 * opposite direction).
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_allow (LrgProcgenWfc       *self,
                            guint                a,
                            guint                b,
                            LrgProcgenDirection  direction);

/**
 * lrg_procgen_wfc_is_allowed:
 * @self: a generator
 * @a: a tile index
 * @b: a tile index
 * @direction: where @b lies relative to @a
 *
 * Returns: whether @b may lie in @direction of @a
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_wfc_is_allowed (LrgProcgenWfc       *self,
                                     guint                a,
                                     guint                b,
                                     LrgProcgenDirection  direction);

/**
 * lrg_procgen_wfc_clear:
 * @self: a generator
 *
 * Removes every tile and rule.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_clear (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_learn:
 * @self: a generator
 * @sample: a sample grid
 *
 * Replaces the model with one learned from @sample: one tile per distinct
 * tag (ascending, named by the #LrgProcgenCell nick or `tag-N`) weighted
 * by its count, and every adjacency the sample shows allowed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_learn (LrgProcgenWfc  *self,
                            LrgProcgenGrid *sample);

/**
 * lrg_procgen_wfc_check:
 * @self: a generator
 * @grid: a grid
 *
 * Checks every pair of orthogonal neighbours of @grid against the rules
 * (through the tiles' tags), and that every cell's tag belongs to a tile.
 *
 * Returns: whether @grid satisfies the model
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_wfc_check (LrgProcgenWfc  *self,
                                LrgProcgenGrid *grid);

/**
 * lrg_procgen_wfc_get_last_attempts:
 * @self: a generator
 *
 * Returns: how many attempts (restarts + 1) the last generation used
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_wfc_get_last_attempts (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_get_last_backtracks:
 * @self: a generator
 *
 * Returns: how many choices the last generation undid
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_wfc_get_last_backtracks (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_get_max_attempts:
 * @self: a generator
 *
 * Returns: the #LrgProcgenWfc:max-attempts property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_wfc_get_max_attempts (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_set_max_attempts:
 * @self: a generator
 * @value: attempts before giving up (at least 1)
 *
 * Sets the #LrgProcgenWfc:max-attempts property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_set_max_attempts (LrgProcgenWfc *self,
                                       guint          value);

/**
 * lrg_procgen_wfc_get_backtrack_depth:
 * @self: a generator
 *
 * Returns: the #LrgProcgenWfc:backtrack-depth property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_wfc_get_backtrack_depth (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_set_backtrack_depth:
 * @self: a generator
 * @value: recent choices kept for backtracking (0 = restart at once)
 *
 * Sets the #LrgProcgenWfc:backtrack-depth property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_set_backtrack_depth (LrgProcgenWfc *self,
                                          guint          value);

/**
 * lrg_procgen_wfc_get_max_backtracks:
 * @self: a generator
 *
 * Returns: the #LrgProcgenWfc:max-backtracks property
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_wfc_get_max_backtracks (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_set_max_backtracks:
 * @self: a generator
 * @value: undone choices per attempt before restarting
 *
 * Sets the #LrgProcgenWfc:max-backtracks property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_set_max_backtracks (LrgProcgenWfc *self,
                                         guint          value);

/**
 * lrg_procgen_wfc_get_border_tile:
 * @self: a generator
 *
 * Returns: (nullable): the #LrgProcgenWfc:border-tile property
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_procgen_wfc_get_border_tile (LrgProcgenWfc *self);

/**
 * lrg_procgen_wfc_set_border_tile:
 * @self: a generator
 * @name: (nullable): the tile every border cell must be, or %NULL
 *
 * Sets the #LrgProcgenWfc:border-tile property.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_wfc_set_border_tile (LrgProcgenWfc *self,
                                      const gchar   *name);

G_END_DECLS
