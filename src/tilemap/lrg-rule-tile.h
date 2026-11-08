/* lrg-rule-tile.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRuleTile: autotiling.  A rule tile is a brush whose cells pick
 * their tile from their neighbours: an ordered list of rules, each a
 * 3x3 neighbour pattern (this / not this / don't care) with one or more
 * weighted output tiles.  Presets build the classic 16-tile (edges) and
 * 47-tile (blob) sets.  Rule tiles live in an .rtileset (LrgTileset)
 * and are applied to tile arrays, so the same code serves the runtime
 * (LrgTilemap), the level data and the editor's tile painter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

/* ==========================================================================
 * Neighbours and grid layouts
 * ========================================================================== */

/**
 * LrgTileNeighbor:
 * @LRG_TILE_NEIGHBOR_NONE: no neighbour
 * @LRG_TILE_NEIGHBOR_N: the cell above (y - 1)
 * @LRG_TILE_NEIGHBOR_NE: above right
 * @LRG_TILE_NEIGHBOR_E: the cell to the right (x + 1)
 * @LRG_TILE_NEIGHBOR_SE: below right
 * @LRG_TILE_NEIGHBOR_S: the cell below (y + 1)
 * @LRG_TILE_NEIGHBOR_SW: below left
 * @LRG_TILE_NEIGHBOR_W: the cell to the left (x - 1)
 * @LRG_TILE_NEIGHBOR_NW: above left
 * @LRG_TILE_NEIGHBOR_ALL: all eight
 *
 * The eight neighbours of a cell, as bits of an 8-bit neighbour mask.
 * Bit i is the i-th direction clockwise from north, so rotating a mask
 * by a quarter turn clockwise is an 8-bit rotate left by two.
 *
 * Directions are logical grid directions: on an isometric map north is
 * the cell at y - 1 (up and to the right on screen); on hexagonal maps
 * two of the eight do not exist (see #LrgTileNeighborLayout).
 */
typedef enum /*< flags >*/
{
	LRG_TILE_NEIGHBOR_NONE = 0,
	LRG_TILE_NEIGHBOR_N    = 1 << 0,
	LRG_TILE_NEIGHBOR_NE   = 1 << 1,
	LRG_TILE_NEIGHBOR_E    = 1 << 2,
	LRG_TILE_NEIGHBOR_SE   = 1 << 3,
	LRG_TILE_NEIGHBOR_S    = 1 << 4,
	LRG_TILE_NEIGHBOR_SW   = 1 << 5,
	LRG_TILE_NEIGHBOR_W    = 1 << 6,
	LRG_TILE_NEIGHBOR_NW   = 1 << 7,
	LRG_TILE_NEIGHBOR_ALL  = 0xFF
} LrgTileNeighbor;

LRG_AVAILABLE_IN_ALL
GType lrg_tile_neighbor_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILE_NEIGHBOR (lrg_tile_neighbor_get_type ())

/**
 * LrgTileNeighborLayout:
 * @LRG_TILE_NEIGHBOR_LAYOUT_SQUARE: orthogonal and diamond isometric maps:
 *   the eight cells around (x, y)
 * @LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD: staggered isometric map with odd
 *   rows shifted right; directions are mapped onto the diamond directions
 *   so rules written for diamond maps work unchanged
 * @LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_EVEN: staggered isometric map with even
 *   rows shifted right
 * @LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD: pointy-top hexagons in rows, odd
 *   rows shifted right; neighbours E, W, NE, NW, SE, SW (no N or S)
 * @LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN: pointy-top rows, even rows shifted
 * @LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD: flat-top hexagons in columns,
 *   odd columns shifted down; neighbours N, S, NE, NW, SE, SW (no E or W)
 * @LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN: flat-top columns, even columns
 *   shifted down
 *
 * How grid coordinates map to neighbours.  lrg_tilemap_get_neighbor_layout()
 * gives the layout of a tilemap's orientation.
 */
typedef enum
{
	LRG_TILE_NEIGHBOR_LAYOUT_SQUARE,
	LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD,
	LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_EVEN,
	LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD,
	LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN,
	LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_ODD,
	LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN
} LrgTileNeighborLayout;

LRG_AVAILABLE_IN_ALL
GType lrg_tile_neighbor_layout_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILE_NEIGHBOR_LAYOUT (lrg_tile_neighbor_layout_get_type ())

/**
 * lrg_tile_neighbor_offset:
 * @layout: the grid layout
 * @x: the cell column
 * @y: the cell row
 * @direction: one direction (a single bit of #LrgTileNeighbor)
 * @out_x: (out): the neighbour's column
 * @out_y: (out): the neighbour's row
 *
 * Finds the neighbour of a cell in one direction.  The result may lie
 * outside the map; bounds are the caller's business.
 *
 * Returns: %FALSE when the layout has no neighbour in that direction
 *   (N and S of a pointy-top hexagon, E and W of a flat-top one)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tile_neighbor_offset (LrgTileNeighborLayout  layout,
                                   gint                   x,
                                   gint                   y,
                                   LrgTileNeighbor        direction,
                                   gint                  *out_x,
                                   gint                  *out_y);

/**
 * lrg_tile_neighbor_layout_get_mask:
 * @layout: the grid layout
 *
 * Gets the directions that exist in a layout.
 *
 * Returns: the #LrgTileNeighbor bits a cell has in @layout
 */
LRG_AVAILABLE_IN_ALL
LrgTileNeighbor lrg_tile_neighbor_layout_get_mask (LrgTileNeighborLayout layout);

/* ==========================================================================
 * LrgTileRule
 * ========================================================================== */

/**
 * LrgTileRuleMatch:
 * @LRG_TILE_RULE_MATCH_ANY: the neighbour does not matter
 * @LRG_TILE_RULE_MATCH_THIS: the neighbour must belong to the same rule tile
 * @LRG_TILE_RULE_MATCH_NOT_THIS: the neighbour must not belong to it
 *
 * What a rule requires of one neighbour.
 */
typedef enum
{
	LRG_TILE_RULE_MATCH_ANY,
	LRG_TILE_RULE_MATCH_THIS,
	LRG_TILE_RULE_MATCH_NOT_THIS
} LrgTileRuleMatch;

LRG_AVAILABLE_IN_ALL
GType lrg_tile_rule_match_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILE_RULE_MATCH (lrg_tile_rule_match_get_type ())

/**
 * LrgTileRuleTransform:
 * @LRG_TILE_RULE_TRANSFORM_FIXED: the pattern is matched as written
 * @LRG_TILE_RULE_TRANSFORM_ROTATED: the pattern also matches turned by 90,
 *   180 and 270 degrees; the output tile is turned the same way
 * @LRG_TILE_RULE_TRANSFORM_MIRROR_X: the pattern also matches mirrored left
 *   to right; the output tile is flipped horizontally
 * @LRG_TILE_RULE_TRANSFORM_MIRROR_Y: the pattern also matches mirrored top to
 *   bottom; the output tile is flipped vertically
 * @LRG_TILE_RULE_TRANSFORM_MIRROR_XY: the pattern also matches mirrored on
 *   either axis or both
 *
 * How a rule's pattern may be transformed when matching, so one rule and
 * one tile image can cover every orientation of a corner or an edge.
 * Variants are tried in the order listed (unchanged first).
 */
typedef enum
{
	LRG_TILE_RULE_TRANSFORM_FIXED,
	LRG_TILE_RULE_TRANSFORM_ROTATED,
	LRG_TILE_RULE_TRANSFORM_MIRROR_X,
	LRG_TILE_RULE_TRANSFORM_MIRROR_Y,
	LRG_TILE_RULE_TRANSFORM_MIRROR_XY
} LrgTileRuleTransform;

LRG_AVAILABLE_IN_ALL
GType lrg_tile_rule_transform_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILE_RULE_TRANSFORM (lrg_tile_rule_transform_get_type ())

/**
 * LrgTileRule:
 *
 * One rule of an #LrgRuleTile: a neighbour pattern, the transforms it
 * may be matched under and the weighted output tiles it picks from.
 * Reference counted; a rule may be shared by several rule tiles.
 */
typedef struct _LrgTileRule LrgTileRule;

LRG_AVAILABLE_IN_ALL
GType lrg_tile_rule_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILE_RULE (lrg_tile_rule_get_type ())

/**
 * lrg_tile_rule_new:
 *
 * Creates a rule that matches anything (every neighbour
 * %LRG_TILE_RULE_MATCH_ANY) and has no outputs.
 *
 * Returns: (transfer full): a new #LrgTileRule
 */
LRG_AVAILABLE_IN_ALL
LrgTileRule * lrg_tile_rule_new (void);

/**
 * lrg_tile_rule_ref:
 * @self: an #LrgTileRule
 *
 * Returns: (transfer full): @self with one more reference
 */
LRG_AVAILABLE_IN_ALL
LrgTileRule * lrg_tile_rule_ref (LrgTileRule *self);

/**
 * lrg_tile_rule_unref:
 * @self: (transfer full): an #LrgTileRule
 *
 * Drops a reference; the rule is freed with the last one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_rule_unref (LrgTileRule *self);

/**
 * lrg_tile_rule_copy:
 * @self: an #LrgTileRule
 *
 * Returns: (transfer full): an independent copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgTileRule * lrg_tile_rule_copy (const LrgTileRule *self);

/**
 * lrg_tile_rule_set_neighbor:
 * @self: an #LrgTileRule
 * @direction: one or more #LrgTileNeighbor bits
 * @match: what those neighbours must be
 *
 * Sets the requirement for one or more neighbours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_rule_set_neighbor (LrgTileRule      *self,
                                 LrgTileNeighbor   direction,
                                 LrgTileRuleMatch  match);

/**
 * lrg_tile_rule_get_neighbor:
 * @self: an #LrgTileRule
 * @direction: a single #LrgTileNeighbor bit
 *
 * Returns: the requirement for that neighbour
 */
LRG_AVAILABLE_IN_ALL
LrgTileRuleMatch lrg_tile_rule_get_neighbor (const LrgTileRule *self,
                                             LrgTileNeighbor    direction);

/**
 * lrg_tile_rule_get_this_mask:
 * @self: an #LrgTileRule
 *
 * Returns: the neighbours that must belong to the rule tile
 */
LRG_AVAILABLE_IN_ALL
LrgTileNeighbor lrg_tile_rule_get_this_mask (const LrgTileRule *self);

/**
 * lrg_tile_rule_get_not_this_mask:
 * @self: an #LrgTileRule
 *
 * Returns: the neighbours that must not belong to the rule tile
 */
LRG_AVAILABLE_IN_ALL
LrgTileNeighbor lrg_tile_rule_get_not_this_mask (const LrgTileRule *self);

/**
 * lrg_tile_rule_set_transform:
 * @self: an #LrgTileRule
 * @transform: how the pattern may be transformed
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_rule_set_transform (LrgTileRule          *self,
                                  LrgTileRuleTransform  transform);

/**
 * lrg_tile_rule_get_transform:
 * @self: an #LrgTileRule
 *
 * Returns: how the pattern may be transformed
 */
LRG_AVAILABLE_IN_ALL
LrgTileRuleTransform lrg_tile_rule_get_transform (const LrgTileRule *self);

/**
 * lrg_tile_rule_add_output:
 * @self: an #LrgTileRule
 * @tile: the cell value to write: a 1-based tile id with optional
 *   transform bits (see lrg_tile_make())
 * @weight: the relative chance of this output among the rule's outputs
 *   (> 0)
 *
 * Adds an output tile.  With several outputs the rule picks one per cell
 * at random, weighted, from a hash of the cell coordinates and the rule
 * tile's seed: the same cell always gets the same variant.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_rule_add_output (LrgTileRule *self,
                               guint        tile,
                               gfloat       weight);

/**
 * lrg_tile_rule_get_n_outputs:
 * @self: an #LrgTileRule
 *
 * Returns: the number of output tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_rule_get_n_outputs (const LrgTileRule *self);

/**
 * lrg_tile_rule_get_output:
 * @self: an #LrgTileRule
 * @index: the output index
 * @out_weight: (out) (optional): the output's weight
 *
 * Returns: the output's cell value, or 0 if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_rule_get_output (const LrgTileRule *self,
                                guint              index,
                                gfloat            *out_weight);

/**
 * lrg_tile_rule_remove_output:
 * @self: an #LrgTileRule
 * @index: the output index
 *
 * Returns: %TRUE if an output was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tile_rule_remove_output (LrgTileRule *self,
                                      guint        index);

/**
 * lrg_tile_rule_clear_outputs:
 * @self: an #LrgTileRule
 *
 * Removes every output.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_rule_clear_outputs (LrgTileRule *self);

/**
 * lrg_tile_rule_match:
 * @self: an #LrgTileRule
 * @mask: the cell's neighbour mask: the bits of the neighbours that belong
 *   to the rule tile
 * @valid: the directions that exist in the grid layout (see
 *   lrg_tile_neighbor_layout_get_mask()); requirements on other
 *   directions are ignored
 * @out_rotation: (out) (optional): quarter turns clockwise to apply to the
 *   output
 * @out_mirrored: (out) (optional): whether to mirror the output left to
 *   right before rotating
 *
 * Tests the pattern (and its transformed variants) against a mask.
 *
 * Returns: %TRUE if the rule matches
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tile_rule_match (const LrgTileRule *self,
                              LrgTileNeighbor    mask,
                              LrgTileNeighbor    valid,
                              guint             *out_rotation,
                              gboolean          *out_mirrored);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTileRule, lrg_tile_rule_unref)

/* ==========================================================================
 * LrgRuleTile
 * ========================================================================== */

#define LRG_TYPE_RULE_TILE (lrg_rule_tile_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRuleTile, lrg_rule_tile, LRG, RULE_TILE, GObject)

/**
 * LRG_RULE_TILE_BLOB47_COUNT:
 *
 * The number of tiles in a 47-tile (blob) set.
 */
#define LRG_RULE_TILE_BLOB47_COUNT (47)

/**
 * LRG_RULE_TILE_EDGES16_COUNT:
 *
 * The number of tiles in a 16-tile (edges only) set.
 */
#define LRG_RULE_TILE_EDGES16_COUNT (16)

/**
 * lrg_rule_tile_new:
 * @name: the rule tile's name (unique within its tileset)
 *
 * Creates an empty rule tile.
 *
 * Returns: (transfer full): a new #LrgRuleTile
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_rule_tile_new (const gchar *name);

/**
 * lrg_rule_tile_new_edges16:
 * @name: the rule tile's name
 * @first_tile: the 1-based tile id of the first of 16 consecutive tiles
 *
 * Creates a 16-tile rule tile: only the four edge neighbours matter.
 * Tile `first_tile + m` is used for edge mask m = N*1 + E*2 + S*4 + W*8
 * (1 when that neighbour belongs to the rule tile), so the set is laid
 * out in the tileset in mask order: isolated, N, E, N+E, S, ...
 *
 * Returns: (transfer full): a new #LrgRuleTile with 16 rules
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_rule_tile_new_edges16 (const gchar *name,
                                         guint        first_tile);

/**
 * lrg_rule_tile_new_blob47:
 * @name: the rule tile's name
 * @first_tile: the 1-based tile id of the first of 47 consecutive tiles
 *
 * Creates a 47-tile ("blob") rule tile: edges and corners matter, a
 * corner only when both edges next to it belong to the rule tile.  The
 * 47 distinct masks are sorted ascending (see lrg_rule_tile_blob47_mask())
 * and tile `first_tile + rank` is used for the mask of that rank.
 *
 * Returns: (transfer full): a new #LrgRuleTile with 47 rules
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_rule_tile_new_blob47 (const gchar *name,
                                        guint        first_tile);

/**
 * lrg_rule_tile_blob47_mask:
 * @rank: 0 to 46
 *
 * Gets the neighbour mask the blob tile of a rank stands for, so tools
 * can draw a template of the set.
 *
 * Returns: the mask, or 0 if @rank is out of range
 */
LRG_AVAILABLE_IN_ALL
LrgTileNeighbor lrg_rule_tile_blob47_mask (guint rank);

/**
 * lrg_rule_tile_blob47_rank:
 * @mask: any neighbour mask
 *
 * Reduces a mask to blob form (a corner counts only with both of its
 * edges) and finds its rank in the 47-tile set.
 *
 * Returns: 0 to 46
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_blob47_rank (LrgTileNeighbor mask);

/**
 * lrg_rule_tile_get_name:
 * @self: an #LrgRuleTile
 *
 * Returns: (transfer none): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_rule_tile_get_name (LrgRuleTile *self);

/**
 * lrg_rule_tile_set_name:
 * @self: an #LrgRuleTile
 * @name: the new name
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_set_name (LrgRuleTile *self,
                             const gchar *name);

/**
 * lrg_rule_tile_get_default_tile:
 * @self: an #LrgRuleTile
 *
 * Returns: the cell value used when no rule matches (0: the first
 *   output of the first rule)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_get_default_tile (LrgRuleTile *self);

/**
 * lrg_rule_tile_set_default_tile:
 * @self: an #LrgRuleTile
 * @tile: a cell value, or 0
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_set_default_tile (LrgRuleTile *self,
                                     guint        tile);

/**
 * lrg_rule_tile_get_seed:
 * @self: an #LrgRuleTile
 *
 * Returns: the seed of the per-cell variant choice
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_get_seed (LrgRuleTile *self);

/**
 * lrg_rule_tile_set_seed:
 * @self: an #LrgRuleTile
 * @seed: the seed of the per-cell variant choice
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_set_seed (LrgRuleTile *self,
                             guint        seed);

/**
 * lrg_rule_tile_get_border_is_this:
 * @self: an #LrgRuleTile
 *
 * Returns: whether cells outside the map count as belonging to the rule
 *   tile
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rule_tile_get_border_is_this (LrgRuleTile *self);

/**
 * lrg_rule_tile_set_border_is_this:
 * @self: an #LrgRuleTile
 * @border_is_this: %TRUE to treat cells outside the map as belonging to
 *   the rule tile (terrain that continues past the map edge)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_set_border_is_this (LrgRuleTile *self,
                                       gboolean     border_is_this);

/**
 * lrg_rule_tile_add_match_tile:
 * @self: an #LrgRuleTile
 * @tile_id: a 1-based tile id
 *
 * Makes cells holding @tile_id count as "this" for the rule tile's
 * patterns without being repainted by it (another terrain the rule tile
 * should join up with).
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_add_match_tile (LrgRuleTile *self,
                                   guint        tile_id);

/**
 * lrg_rule_tile_get_match_tiles:
 * @self: an #LrgRuleTile
 * @n_tiles: (out): the number of tile ids
 *
 * Returns: (array length=n_tiles) (transfer none): the extra tile ids
 *   that count as "this", in the order they were added
 */
LRG_AVAILABLE_IN_ALL
const guint * lrg_rule_tile_get_match_tiles (LrgRuleTile *self,
                                             guint       *n_tiles);

/**
 * lrg_rule_tile_clear_match_tiles:
 * @self: an #LrgRuleTile
 *
 * Removes every extra matching tile id.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_clear_match_tiles (LrgRuleTile *self);

/**
 * lrg_rule_tile_add_rule:
 * @self: an #LrgRuleTile
 * @rule: (transfer none): the rule to append
 *
 * Appends a rule.  Rules are tried in order and the first match wins,
 * so put specific rules before general ones.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_add_rule (LrgRuleTile *self,
                             LrgTileRule *rule);

/**
 * lrg_rule_tile_insert_rule:
 * @self: an #LrgRuleTile
 * @index: the position (clamped to the rule count)
 * @rule: (transfer none): the rule to insert
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_insert_rule (LrgRuleTile *self,
                                guint        index,
                                LrgTileRule *rule);

/**
 * lrg_rule_tile_remove_rule:
 * @self: an #LrgRuleTile
 * @index: the rule index
 *
 * Returns: %TRUE if a rule was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rule_tile_remove_rule (LrgRuleTile *self,
                                    guint        index);

/**
 * lrg_rule_tile_move_rule:
 * @self: an #LrgRuleTile
 * @from: the rule's current index
 * @to: its new index
 *
 * Returns: %TRUE if both indices were valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rule_tile_move_rule (LrgRuleTile *self,
                                  guint        from,
                                  guint        to);

/**
 * lrg_rule_tile_get_rule:
 * @self: an #LrgRuleTile
 * @index: the rule index
 *
 * The rule is shared, not copied: change it in place, then call
 * lrg_rule_tile_changed() so views refresh.
 *
 * Returns: (transfer none) (nullable): the rule, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgTileRule * lrg_rule_tile_get_rule (LrgRuleTile *self,
                                      guint        index);

/**
 * lrg_rule_tile_get_n_rules:
 * @self: an #LrgRuleTile
 *
 * Returns: the number of rules
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_get_n_rules (LrgRuleTile *self);

/**
 * lrg_rule_tile_clear_rules:
 * @self: an #LrgRuleTile
 *
 * Removes every rule.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_clear_rules (LrgRuleTile *self);

/**
 * lrg_rule_tile_changed:
 * @self: an #LrgRuleTile
 *
 * Emits #LrgRuleTile::changed.  The rule list functions emit it
 * themselves; call this after editing a rule obtained with
 * lrg_rule_tile_get_rule().
 */
LRG_AVAILABLE_IN_ALL
void lrg_rule_tile_changed (LrgRuleTile *self);

/**
 * lrg_rule_tile_owns_tile:
 * @self: an #LrgRuleTile
 * @tile: a cell value (transform bits are ignored)
 *
 * Tests whether a cell was painted with this rule tile: its tile is the
 * default tile or an output of one of the rules.  Owned cells are the
 * ones lrg_rule_tile_apply() repaints.
 *
 * Returns: %TRUE if @tile belongs to the rule tile
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rule_tile_owns_tile (LrgRuleTile *self,
                                  guint        tile);

/**
 * lrg_rule_tile_matches_tile:
 * @self: an #LrgRuleTile
 * @tile: a cell value (transform bits are ignored)
 *
 * Tests whether a cell counts as "this" in the rule tile's patterns: an
 * owned tile or one of the match tiles.
 *
 * Returns: %TRUE if @tile counts as this rule tile
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rule_tile_matches_tile (LrgRuleTile *self,
                                     guint        tile);

/**
 * lrg_rule_tile_resolve:
 * @self: an #LrgRuleTile
 * @mask: the cell's neighbour mask
 * @layout: the grid layout (decides which directions exist)
 * @x: the cell column (for the variant choice)
 * @y: the cell row (for the variant choice)
 * @out_rule: (out) (optional): the index of the matching rule, or -1
 *   when the default tile was used
 *
 * Picks the cell value for a cell with the given neighbours: the first
 * matching rule's output (a weighted variant, transformed when the rule
 * matched rotated or mirrored), or the default tile.
 *
 * Returns: the cell value, or 0 when the rule tile has no tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_resolve (LrgRuleTile           *self,
                             LrgTileNeighbor        mask,
                             LrgTileNeighborLayout  layout,
                             gint                   x,
                             gint                   y,
                             gint                  *out_rule);

/**
 * lrg_rule_tile_compute_mask:
 * @self: an #LrgRuleTile
 * @tiles: (array length=n_tiles): the layer's cells, row-major
 * @n_tiles: the number of cells (a multiple of @width)
 * @width: the layer width in cells
 * @layout: the grid layout
 * @x: the cell column
 * @y: the cell row
 *
 * Builds a cell's neighbour mask: the directions whose neighbour counts
 * as this rule tile (lrg_rule_tile_matches_tile()), with cells outside
 * the layer following #LrgRuleTile:border-is-this.
 *
 * Returns: the neighbour mask
 */
LRG_AVAILABLE_IN_ALL
LrgTileNeighbor lrg_rule_tile_compute_mask (LrgRuleTile           *self,
                                            const guint           *tiles,
                                            gsize                  n_tiles,
                                            guint                  width,
                                            LrgTileNeighborLayout  layout,
                                            gint                   x,
                                            gint                   y);

/**
 * lrg_rule_tile_apply:
 * @self: an #LrgRuleTile
 * @tiles: (array length=n_tiles): the layer's cells, row-major; owned
 *   cells in the rectangle are rewritten in place
 * @n_tiles: the number of cells (a multiple of @width)
 * @width: the layer width in cells
 * @layout: the grid layout
 * @x: the rectangle's first column (clamped to the layer)
 * @y: the rectangle's first row
 * @rect_width: the rectangle width in cells (G_MAXUINT: to the edge)
 * @rect_height: the rectangle height in cells
 *
 * Re-resolves every cell in the rectangle that the rule tile owns.
 * Masks are computed from the cells as they were before the call, so
 * the result does not depend on the scan order.
 *
 * Returns: the number of cells whose value changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_apply (LrgRuleTile           *self,
                           guint                 *tiles,
                           gsize                  n_tiles,
                           guint                  width,
                           LrgTileNeighborLayout  layout,
                           gint                   x,
                           gint                   y,
                           guint                  rect_width,
                           guint                  rect_height);

/**
 * lrg_rule_tile_paint:
 * @self: an #LrgRuleTile
 * @tiles: (array length=n_tiles): the layer's cells, row-major
 * @n_tiles: the number of cells (a multiple of @width)
 * @width: the layer width in cells
 * @layout: the grid layout
 * @x: the painted cell's column
 * @y: the painted cell's row
 *
 * Paints one cell with the rule tile and re-resolves it and its owned
 * neighbours (two cells around it, which covers staggered layouts).
 * This is what a tile painter calls per brush cell.
 *
 * Returns: the number of cells whose value changed, 0 if (@x, @y) is
 *   outside the layer
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_paint (LrgRuleTile           *self,
                           guint                 *tiles,
                           gsize                  n_tiles,
                           guint                  width,
                           LrgTileNeighborLayout  layout,
                           gint                   x,
                           gint                   y);

/**
 * lrg_rule_tile_erase:
 * @self: an #LrgRuleTile
 * @tiles: (array length=n_tiles): the layer's cells, row-major
 * @n_tiles: the number of cells (a multiple of @width)
 * @width: the layer width in cells
 * @layout: the grid layout
 * @x: the erased cell's column
 * @y: the erased cell's row
 *
 * Empties one cell and re-resolves the owned cells around it.
 *
 * Returns: the number of cells whose value changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rule_tile_erase (LrgRuleTile           *self,
                           guint                 *tiles,
                           gsize                  n_tiles,
                           guint                  width,
                           LrgTileNeighborLayout  layout,
                           gint                   x,
                           gint                   y);

#ifndef __GI_SCANNER__
#ifdef LIBREGNUM_COMPILATION
#include <yaml-glib.h>

/*
 * In-tree: the .rtileset reader and writer (lrg-tileset.c) load and save
 * rule tiles through these.  @path is the key path used in messages.
 */
G_GNUC_INTERNAL
LrgRuleTile * lrg_rule_tile_from_yaml (YamlNode     *node,
                                       const gchar  *source,
                                       const gchar  *path,
                                       guint         tile_count,
                                       GError      **error);
G_GNUC_INTERNAL
YamlNode * lrg_rule_tile_to_yaml (LrgRuleTile *self);
#endif /* LIBREGNUM_COMPILATION */
#endif /* __GI_SCANNER__ */

G_END_DECLS
