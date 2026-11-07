/* lrg-tilemap-layer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Single layer of tile data for a tilemap.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_TILEMAP_LAYER (lrg_tilemap_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTilemapLayer, lrg_tilemap_layer, LRG, TILEMAP_LAYER, GObject)

/**
 * LRG_TILEMAP_EMPTY_TILE:
 *
 * Special tile ID value indicating an empty/transparent tile.
 * This value (0) is reserved and should not be used for actual tiles.
 */
#define LRG_TILEMAP_EMPTY_TILE (0)

/* ==========================================================================
 * Per-tile transform bits (0.9.0)
 *
 * A layer cell holds a 32-bit value: the low 29 bits are the 1-based tile
 * id (0 = empty), the top three bits flip and rotate the tile when it is
 * drawn.  The encoding matches Tiled's GID flags, so maps exchanged with
 * Tiled keep their transforms.
 * ========================================================================== */

/**
 * LRG_TILE_FLIP_HORIZONTAL:
 *
 * Cell flag: the tile is mirrored left to right (after the diagonal flip).
 */
#define LRG_TILE_FLIP_HORIZONTAL ((guint) 0x80000000)

/**
 * LRG_TILE_FLIP_VERTICAL:
 *
 * Cell flag: the tile is mirrored top to bottom (after the diagonal flip).
 */
#define LRG_TILE_FLIP_VERTICAL ((guint) 0x40000000)

/**
 * LRG_TILE_FLIP_DIAGONAL:
 *
 * Cell flag: the tile is mirrored across its top-left to bottom-right
 * diagonal (x and y swapped).  Combined with the other two flags it
 * expresses the four rotations; see lrg_tile_make().
 */
#define LRG_TILE_FLIP_DIAGONAL ((guint) 0x20000000)

/**
 * LRG_TILE_FLAGS_MASK:
 *
 * The three transform bits of a cell value.
 */
#define LRG_TILE_FLAGS_MASK ((guint) 0xE0000000)

/**
 * LRG_TILE_ID_MASK:
 *
 * The tile id bits of a cell value.
 */
#define LRG_TILE_ID_MASK ((guint) 0x1FFFFFFF)

/**
 * lrg_tile_get_id:
 * @tile: a cell value
 *
 * Strips the transform bits from a cell value.
 *
 * Returns: the 1-based tile id (0 for an empty cell)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_get_id (guint tile);

/**
 * lrg_tile_make:
 * @tile_id: the 1-based tile id (0 gives an empty cell)
 * @mirrored: mirror the tile left to right first
 * @rotation: quarter turns clockwise applied after the mirror (taken modulo 4)
 *
 * Builds a cell value from a tile id and a transform.  Every one of the
 * eight transforms of a square has exactly one (@mirrored, @rotation)
 * pair and one combination of the three flag bits:
 *
 * | mirrored | rotation | flags |
 * |----------|----------|-------|
 * | no  | 0 | none |
 * | no  | 1 | diagonal + horizontal |
 * | no  | 2 | horizontal + vertical |
 * | no  | 3 | diagonal + vertical |
 * | yes | 0 | horizontal |
 * | yes | 1 | all three |
 * | yes | 2 | vertical |
 * | yes | 3 | diagonal |
 *
 * Returns: the cell value
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_make (guint    tile_id,
                     gboolean mirrored,
                     guint    rotation);

/**
 * lrg_tile_get_rotation:
 * @tile: a cell value
 *
 * Gets the clockwise rotation of a cell, in the (mirror, then rotate)
 * form of lrg_tile_make().
 *
 * Returns: quarter turns clockwise, 0 to 3
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_get_rotation (guint tile);

/**
 * lrg_tile_get_mirrored:
 * @tile: a cell value
 *
 * Gets whether a cell is mirrored, in the (mirror, then rotate) form of
 * lrg_tile_make().
 *
 * Returns: %TRUE if the tile is mirrored left to right before rotating
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tile_get_mirrored (guint tile);

/**
 * lrg_tile_rotate:
 * @tile: a cell value
 * @quarter_turns: quarter turns to add, clockwise (negative turns
 *   counter-clockwise)
 *
 * Rotates a cell's tile on top of its current transform.
 *
 * Returns: the rotated cell value (empty cells stay empty)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_rotate (guint tile,
                       gint  quarter_turns);

/**
 * lrg_tile_flip_horizontally:
 * @tile: a cell value
 *
 * Mirrors a cell's tile left to right on top of its current transform.
 *
 * Returns: the flipped cell value (empty cells stay empty)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_flip_horizontally (guint tile);

/**
 * lrg_tile_flip_vertically:
 * @tile: a cell value
 *
 * Mirrors a cell's tile top to bottom on top of its current transform.
 *
 * Returns: the flipped cell value (empty cells stay empty)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_flip_vertically (guint tile);

/**
 * lrg_tile_transform_point:
 * @tile: a cell value (only the transform bits are used)
 * @width: the tile width
 * @height: the tile height
 * @x: (inout): a point in tile-local pixels (0,0 is the top-left corner)
 * @y: (inout): the point's y
 *
 * Maps a point of the untransformed tile image to where it lands once
 * the cell's transform is applied about the tile centre.  Used to
 * transform per-tile collision polygons with their tile.  Rotations by
 * a quarter turn swap the extents of non-square tiles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_transform_point (guint   tile,
                               gfloat  width,
                               gfloat  height,
                               gfloat *x,
                               gfloat *y);

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tilemap_layer_new:
 * @width: the layer width in tiles
 * @height: the layer height in tiles
 *
 * Creates a new tilemap layer with all tiles initialized to
 * %LRG_TILEMAP_EMPTY_TILE (0).
 *
 * Returns: (transfer full): A new #LrgTilemapLayer
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapLayer * lrg_tilemap_layer_new (guint width,
                                         guint height);

/* ==========================================================================
 * Dimensions
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_width:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer width in tiles.
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_layer_get_width (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_get_height:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer height in tiles.
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_layer_get_height (LrgTilemapLayer *self);

/* ==========================================================================
 * Tile Access
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_tile:
 * @self: an #LrgTilemapLayer
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 *
 * Gets the tile ID at the specified position.
 *
 * Returns: The tile ID, or %LRG_TILEMAP_EMPTY_TILE if out of bounds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_layer_get_tile (LrgTilemapLayer *self,
                                  guint            x,
                                  guint            y);

/**
 * lrg_tilemap_layer_set_tile:
 * @self: an #LrgTilemapLayer
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 * @tile_id: the tile ID to set
 *
 * Sets the tile ID at the specified position.
 * Does nothing if coordinates are out of bounds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_tile (LrgTilemapLayer *self,
                                 guint            x,
                                 guint            y,
                                 guint            tile_id);

/**
 * lrg_tilemap_layer_fill:
 * @self: an #LrgTilemapLayer
 * @tile_id: the tile ID to fill with
 *
 * Fills the entire layer with the specified tile ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_fill (LrgTilemapLayer *self,
                             guint            tile_id);

/**
 * lrg_tilemap_layer_fill_rect:
 * @self: an #LrgTilemapLayer
 * @x: the starting X coordinate
 * @y: the starting Y coordinate
 * @width: the rectangle width in tiles
 * @height: the rectangle height in tiles
 * @tile_id: the tile ID to fill with
 *
 * Fills a rectangular region with the specified tile ID.
 * Coordinates are clamped to layer bounds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_fill_rect (LrgTilemapLayer *self,
                                  guint            x,
                                  guint            y,
                                  guint            width,
                                  guint            height,
                                  guint            tile_id);

/**
 * lrg_tilemap_layer_clear:
 * @self: an #LrgTilemapLayer
 *
 * Clears the layer by setting all tiles to %LRG_TILEMAP_EMPTY_TILE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_clear (LrgTilemapLayer *self);

/* ==========================================================================
 * Layer Properties
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_visible:
 * @self: an #LrgTilemapLayer
 *
 * Gets whether the layer is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_layer_get_visible (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_set_visible:
 * @self: an #LrgTilemapLayer
 * @visible: whether the layer is visible
 *
 * Sets whether the layer is visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_visible (LrgTilemapLayer *self,
                                    gboolean         visible);

/**
 * lrg_tilemap_layer_get_collision_enabled:
 * @self: an #LrgTilemapLayer
 *
 * Gets whether collision detection is enabled for this layer.
 *
 * Returns: %TRUE if collision is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_layer_get_collision_enabled (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_set_collision_enabled:
 * @self: an #LrgTilemapLayer
 * @enabled: whether collision is enabled
 *
 * Sets whether collision detection is enabled for this layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_collision_enabled (LrgTilemapLayer *self,
                                              gboolean         enabled);

/**
 * lrg_tilemap_layer_get_parallax_x:
 * @self: an #LrgTilemapLayer
 *
 * Gets the horizontal parallax factor.
 * 1.0 = normal scrolling, 0.5 = half speed, 0.0 = stationary.
 *
 * Returns: The parallax factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tilemap_layer_get_parallax_x (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_set_parallax_x:
 * @self: an #LrgTilemapLayer
 * @parallax: the parallax factor
 *
 * Sets the horizontal parallax factor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_parallax_x (LrgTilemapLayer *self,
                                       gfloat           parallax);

/**
 * lrg_tilemap_layer_get_parallax_y:
 * @self: an #LrgTilemapLayer
 *
 * Gets the vertical parallax factor.
 *
 * Returns: The parallax factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tilemap_layer_get_parallax_y (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_set_parallax_y:
 * @self: an #LrgTilemapLayer
 * @parallax: the parallax factor
 *
 * Sets the vertical parallax factor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_parallax_y (LrgTilemapLayer *self,
                                       gfloat           parallax);

/**
 * lrg_tilemap_layer_get_opacity:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer opacity (0.0 = transparent, 1.0 = opaque).
 *
 * Returns: The opacity value
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tilemap_layer_get_opacity (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_set_opacity:
 * @self: an #LrgTilemapLayer
 * @opacity: the opacity value (0.0 to 1.0)
 *
 * Sets the layer opacity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_opacity (LrgTilemapLayer *self,
                                    gfloat           opacity);

/**
 * lrg_tilemap_layer_get_name:
 * @self: an #LrgTilemapLayer
 *
 * Gets the layer name.
 *
 * Returns: (transfer none) (nullable): The layer name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tilemap_layer_get_name (LrgTilemapLayer *self);

/**
 * lrg_tilemap_layer_set_name:
 * @self: an #LrgTilemapLayer
 * @name: (nullable): the layer name
 *
 * Sets the layer name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_name (LrgTilemapLayer *self,
                                 const gchar     *name);

/**
 * lrg_tilemap_layer_get_tile_id:
 * @self: an #LrgTilemapLayer
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 *
 * Gets the tile id at a cell without its transform bits (see
 * lrg_tile_get_id()); lrg_tilemap_layer_get_tile() returns the full cell
 * value.
 *
 * Returns: the 1-based tile id, or %LRG_TILEMAP_EMPTY_TILE if empty or
 *   out of bounds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_layer_get_tile_id (LrgTilemapLayer *self,
                                     guint            x,
                                     guint            y);

/**
 * lrg_tilemap_layer_set_tile_transform:
 * @self: an #LrgTilemapLayer
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 * @mirrored: mirror the tile left to right first
 * @rotation: quarter turns clockwise after the mirror
 *
 * Replaces the transform of the tile at a cell, keeping its id (see
 * lrg_tile_make()).  Empty and out-of-bounds cells are left alone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_layer_set_tile_transform (LrgTilemapLayer *self,
                                           guint            x,
                                           guint            y,
                                           gboolean         mirrored,
                                           guint            rotation);

/* ==========================================================================
 * Tile Data Access
 * ========================================================================== */

/**
 * lrg_tilemap_layer_get_tiles:
 * @self: an #LrgTilemapLayer
 * @out_len: (out) (optional): return location for tile count
 *
 * Gets direct access to the tile data array.
 * The array is stored in row-major order (y * width + x).
 *
 * Returns: (array length=out_len) (transfer none): The tile data
 */
LRG_AVAILABLE_IN_ALL
const guint * lrg_tilemap_layer_get_tiles (LrgTilemapLayer *self,
                                           gsize           *out_len);

/**
 * lrg_tilemap_layer_set_tiles:
 * @self: an #LrgTilemapLayer
 * @tiles: (array length=len): the tile data to copy
 * @len: the number of tiles (must match width * height)
 *
 * Sets all tile data from an array. The array must contain
 * exactly width * height elements in row-major order.
 *
 * Returns: %TRUE if successful, %FALSE if @len is incorrect
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_layer_set_tiles (LrgTilemapLayer *self,
                                      const guint     *tiles,
                                      gsize            len);

G_END_DECLS
