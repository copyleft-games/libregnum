/* lrg-procgen-tile-rules.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rule-based mapping from procgen cell tags to tilemap tile ids, with
 * 16-variant (cardinal) and 47-variant (blob) autotiling.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../tilemap/lrg-tilemap.h"
#include "lrg-procgen-grid.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROCGEN_TILE_RULES (lrg_procgen_tile_rules_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenTileRules, lrg_procgen_tile_rules, LRG, PROCGEN_TILE_RULES, GObject)

/**
 * LRG_PROCGEN_BLOB_VARIANTS:
 *
 * The number of tile variants of an %LRG_PROCGEN_AUTOTILE_BLOB rule.
 */
#define LRG_PROCGEN_BLOB_VARIANTS (47)

/**
 * lrg_procgen_tile_rules_new:
 *
 * Creates an empty rule set: every tag maps to the default tile
 * (%LRG_TILEMAP_EMPTY_TILE) until a rule is set.
 *
 * Returns: (transfer full): a new #LrgProcgenTileRules
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenTileRules * lrg_procgen_tile_rules_new (void);

/**
 * lrg_procgen_tile_rules_set_default_tile:
 * @self: tile rules
 * @tile_id: tile id for tags without a rule
 *
 * Sets the tile of tags without a rule.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_tile_rules_set_default_tile (LrgProcgenTileRules *self,
                                              guint                tile_id);

/**
 * lrg_procgen_tile_rules_get_default_tile:
 * @self: tile rules
 *
 * Returns: the tile of tags without a rule
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_tile_rules_get_default_tile (LrgProcgenTileRules *self);

/**
 * lrg_procgen_tile_rules_set_rule:
 * @self: tile rules
 * @tag: the cell tag
 * @tile_id: the tile id (tilemap layer ids are 1-based, 0 is empty), the
 *   first variant for an autotile rule
 * @autotile: how variants are picked
 *
 * Maps a tag to a tile. With autotiling the tile is @tile_id plus the
 * variant index of the cell's neighbour mask (see #LrgProcgenAutotile),
 * unless lrg_procgen_tile_rules_set_variant() overrides that mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_tile_rules_set_rule (LrgProcgenTileRules *self,
                                      guint8               tag,
                                      guint                tile_id,
                                      LrgProcgenAutotile   autotile);

/**
 * lrg_procgen_tile_rules_remove_rule:
 * @self: tile rules
 * @tag: the cell tag
 *
 * Removes a tag's rule, its variants and its connections.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_tile_rules_remove_rule (LrgProcgenTileRules *self,
                                         guint8               tag);

/**
 * lrg_procgen_tile_rules_get_rule:
 * @self: tile rules
 * @tag: the cell tag
 * @out_tile_id: (out) (optional): the rule's tile id
 * @out_autotile: (out) (optional): the rule's autotile mode
 *
 * Returns: whether @tag has a rule
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_tile_rules_get_rule (LrgProcgenTileRules *self,
                                          guint8               tag,
                                          guint               *out_tile_id,
                                          LrgProcgenAutotile  *out_autotile);

/**
 * lrg_procgen_tile_rules_set_variant:
 * @self: tile rules
 * @tag: the cell tag
 * @mask: the neighbour mask (4-bit for cardinal, reduced 8-bit for blob)
 * @tile_id: the tile for that mask
 *
 * Overrides the tile an autotile rule picks for one neighbour mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_tile_rules_set_variant (LrgProcgenTileRules *self,
                                         guint8               tag,
                                         guint                mask,
                                         guint                tile_id);

/**
 * lrg_procgen_tile_rules_get_variant:
 * @self: tile rules
 * @tag: the cell tag
 * @mask: the neighbour mask
 * @out_tile_id: (out) (optional): the override's tile
 *
 * Returns: whether lrg_procgen_tile_rules_set_variant() set an override
 *   for this tag and mask
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_tile_rules_get_variant (LrgProcgenTileRules *self,
                                             guint8               tag,
                                             guint                mask,
                                             guint               *out_tile_id);

/**
 * lrg_procgen_tile_rules_set_connects:
 * @self: tile rules
 * @tag: the cell tag being autotiled
 * @other: another tag
 * @connects: whether @other counts as a matching neighbour of @tag
 *
 * A neighbour matches when it has the same tag or a tag connected to it
 * (e.g. doors that join walls).
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_tile_rules_set_connects (LrgProcgenTileRules *self,
                                          guint8               tag,
                                          guint8               other,
                                          gboolean             connects);

/**
 * lrg_procgen_tile_rules_get_connects:
 * @self: tile rules
 * @tag: the cell tag being autotiled
 * @other: another tag
 *
 * Returns: whether @other was set as a matching neighbour of @tag (a tag
 *   always matches itself regardless)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_tile_rules_get_connects (LrgProcgenTileRules *self,
                                              guint8               tag,
                                              guint8               other);

/**
 * lrg_procgen_tile_rules_set_edge_connects:
 * @self: tile rules
 * @connects: whether cells outside the grid count as matching
 *
 * Sets whether the outside of the grid matches every tag (default %TRUE,
 * so border walls tile seamlessly).
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_tile_rules_set_edge_connects (LrgProcgenTileRules *self,
                                               gboolean             connects);

/**
 * lrg_procgen_tile_rules_get_edge_connects:
 * @self: tile rules
 *
 * Returns: whether cells outside the grid count as matching
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_tile_rules_get_edge_connects (LrgProcgenTileRules *self);

/**
 * lrg_procgen_tile_rules_neighbour_mask:
 * @self: tile rules
 * @grid: a grid
 * @x: column
 * @y: row
 * @autotile: which mask to compute
 *
 * Computes a cell's neighbour mask: 0 for %LRG_PROCGEN_AUTOTILE_NONE, the
 * 4-bit mask for %LRG_PROCGEN_AUTOTILE_CARDINAL, the reduced 8-bit mask
 * (corners only with both edges) for %LRG_PROCGEN_AUTOTILE_BLOB.
 *
 * Returns: the mask
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_tile_rules_neighbour_mask (LrgProcgenTileRules *self,
                                             LrgProcgenGrid      *grid,
                                             gint                 x,
                                             gint                 y,
                                             LrgProcgenAutotile   autotile);

/**
 * lrg_procgen_tile_rules_blob_index:
 * @mask: an 8-bit neighbour mask
 *
 * Reduces @mask (drops corners without both edges) and returns its index
 * among the 47 reduced masks in ascending order.
 *
 * Returns: the variant index, 0..46
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_tile_rules_blob_index (guint mask);

/**
 * lrg_procgen_tile_rules_resolve:
 * @self: tile rules
 * @grid: a grid
 * @x: column
 * @y: row
 *
 * Returns: the tile id for the cell
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_tile_rules_resolve (LrgProcgenTileRules *self,
                                      LrgProcgenGrid      *grid,
                                      gint                 x,
                                      gint                 y);

/**
 * lrg_procgen_tile_rules_build_layer:
 * @self: tile rules
 * @grid: a grid
 *
 * Builds a tilemap layer of the grid's size, named "procgen", with
 * collision enabled, resolving every cell.
 *
 * Returns: (transfer full): a new #LrgTilemapLayer
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapLayer * lrg_procgen_tile_rules_build_layer (LrgProcgenTileRules *self,
                                                      LrgProcgenGrid      *grid);

/**
 * lrg_procgen_tile_rules_build_tilemap:
 * @self: tile rules
 * @grid: a grid
 * @tileset: the tileset to draw with
 *
 * Builds a tilemap with one layer (lrg_procgen_tile_rules_build_layer())
 * and marks %LRG_TILE_PROPERTY_SOLID on every tile of @tileset that a
 * non-walkable cell resolved to, so lrg_tilemap_is_solid() matches the
 * grid.
 *
 * Returns: (transfer full): a new #LrgTilemap
 */
LRG_AVAILABLE_IN_ALL
LrgTilemap * lrg_procgen_tile_rules_build_tilemap (LrgProcgenTileRules *self,
                                                   LrgProcgenGrid      *grid,
                                                   LrgTileset          *tileset);

G_END_DECLS
