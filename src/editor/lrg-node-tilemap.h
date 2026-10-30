/* lrg-node-tilemap.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editable tile grid carried by a tilemap node's visual.
 *
 * An #LrgNodeTilemap is the authoring-side data of a
 * %LRG_NODE_VISUAL_TILEMAP node: a fixed-size grid with one or more named
 * layers of tile ids. It holds no GPU state (the tileset image is the
 * visual's asset), so it can be created, edited, copied and serialized
 * headless. Tile ids follow the runtime #LrgTilemapLayer convention:
 * %LRG_TILEMAP_EMPTY_TILE (0) is empty and id N draws tileset tile N - 1.
 *
 * Geometry (node-local space): cell (x, y) covers
 * [x * tile_size, (x + 1) * tile_size] horizontally and
 * [-(y + 1) * tile_size, -y * tile_size] vertically, so row 0 is the top
 * row and the grid grows right and down from the node origin. The world
 * size of one cell is the visual param "tile_size" (default 1.0).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_NODE_TILEMAP (lrg_node_tilemap_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNodeTilemap, lrg_node_tilemap, LRG, NODE_TILEMAP, GObject)

/**
 * LRG_NODE_TILEMAP_MAX_SIZE:
 *
 * Largest accepted grid width or height, in cells.
 */
#define LRG_NODE_TILEMAP_MAX_SIZE (4096)

/**
 * lrg_node_tilemap_new:
 * @width: grid width in cells (1 .. %LRG_NODE_TILEMAP_MAX_SIZE)
 * @height: grid height in cells (1 .. %LRG_NODE_TILEMAP_MAX_SIZE)
 *
 * Creates an empty grid with a single layer named "base" and a 16x16
 * pixel tileset slicing.
 *
 * Returns: (transfer full): a new #LrgNodeTilemap
 */
LRG_AVAILABLE_IN_ALL
LrgNodeTilemap * lrg_node_tilemap_new (guint width,
                                       guint height);

/**
 * lrg_node_tilemap_get_width:
 * @self: an #LrgNodeTilemap
 *
 * Returns: the grid width in cells
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_get_width (LrgNodeTilemap *self);

/**
 * lrg_node_tilemap_get_height:
 * @self: an #LrgNodeTilemap
 *
 * Returns: the grid height in cells
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_get_height (LrgNodeTilemap *self);

/**
 * lrg_node_tilemap_resize:
 * @self: an #LrgNodeTilemap
 * @width: new width in cells
 * @height: new height in cells
 *
 * Resizes every layer, keeping the cells that stay in range (anchored at
 * the top-left cell) and clearing new ones.
 *
 * Returns: %FALSE if a size is 0 or above %LRG_NODE_TILEMAP_MAX_SIZE
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_resize (LrgNodeTilemap *self,
                                  guint           width,
                                  guint           height);

/**
 * lrg_node_tilemap_get_tile_width:
 * @self: an #LrgNodeTilemap
 *
 * Returns: the width of one tileset tile in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_get_tile_width (LrgNodeTilemap *self);

/**
 * lrg_node_tilemap_get_tile_height:
 * @self: an #LrgNodeTilemap
 *
 * Returns: the height of one tileset tile in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_get_tile_height (LrgNodeTilemap *self);

/**
 * lrg_node_tilemap_set_tile_size:
 * @self: an #LrgNodeTilemap
 * @tile_width: tileset tile width in pixels (> 0)
 * @tile_height: tileset tile height in pixels (> 0)
 *
 * Sets how the tileset image is sliced into tiles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_tilemap_set_tile_size (LrgNodeTilemap *self,
                                     guint           tile_width,
                                     guint           tile_height);

/**
 * lrg_node_tilemap_get_n_layers:
 * @self: an #LrgNodeTilemap
 *
 * Returns: the number of layers (bottom first)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_get_n_layers (LrgNodeTilemap *self);

/**
 * lrg_node_tilemap_add_layer:
 * @self: an #LrgNodeTilemap
 * @name: (nullable): the layer name; %NULL picks "layer N"
 *
 * Appends an empty, visible layer on top.
 *
 * Returns: the index of the new layer
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_add_layer (LrgNodeTilemap *self,
                                  const gchar    *name);

/**
 * lrg_node_tilemap_remove_layer:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 *
 * Removes a layer. The last remaining layer cannot be removed.
 *
 * Returns: %TRUE if the layer was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_remove_layer (LrgNodeTilemap *self,
                                        guint           layer);

/**
 * lrg_node_tilemap_find_layer:
 * @self: an #LrgNodeTilemap
 * @name: a layer name
 *
 * Returns: the index of the first layer called @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_node_tilemap_find_layer (LrgNodeTilemap *self,
                                  const gchar    *name);

/**
 * lrg_node_tilemap_get_layer_name:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 *
 * Returns: (nullable): the layer name, or %NULL for a bad index
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_tilemap_get_layer_name (LrgNodeTilemap *self,
                                               guint           layer);

/**
 * lrg_node_tilemap_set_layer_name:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @name: the new name
 *
 * Renames a layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_tilemap_set_layer_name (LrgNodeTilemap *self,
                                      guint           layer,
                                      const gchar    *name);

/**
 * lrg_node_tilemap_get_layer_visible:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 *
 * Returns: whether the layer is drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_get_layer_visible (LrgNodeTilemap *self,
                                             guint           layer);

/**
 * lrg_node_tilemap_set_layer_visible:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @visible: whether the layer is drawn
 *
 * Shows or hides a layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_tilemap_set_layer_visible (LrgNodeTilemap *self,
                                         guint           layer,
                                         gboolean        visible);

/**
 * lrg_node_tilemap_in_bounds:
 * @self: an #LrgNodeTilemap
 * @x: cell column
 * @y: cell row
 *
 * Returns: whether (@x, @y) is inside the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_in_bounds (LrgNodeTilemap *self,
                                     gint            x,
                                     gint            y);

/**
 * lrg_node_tilemap_get_tile:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @x: cell column
 * @y: cell row
 *
 * Returns: the tile id, or 0 (empty) outside the grid or for a bad layer
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_get_tile (LrgNodeTilemap *self,
                                 guint           layer,
                                 gint            x,
                                 gint            y);

/**
 * lrg_node_tilemap_set_tile:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @x: cell column
 * @y: cell row
 * @tile: the tile id (0 erases)
 *
 * Sets one cell. Out-of-range cells are ignored.
 *
 * Returns: %TRUE if the cell exists (whether or not it changed)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_set_tile (LrgNodeTilemap *self,
                                    guint           layer,
                                    gint            x,
                                    gint            y,
                                    guint           tile);

/**
 * lrg_node_tilemap_get_layer_tiles:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @n_tiles: (out): the number of cells (width * height)
 *
 * Gets a layer's cells in row-major order (row 0 first).
 *
 * Returns: (array length=n_tiles) (transfer none) (nullable): the cells,
 *   or %NULL for a bad layer
 */
LRG_AVAILABLE_IN_ALL
const guint * lrg_node_tilemap_get_layer_tiles (LrgNodeTilemap *self,
                                                guint           layer,
                                                gsize          *n_tiles);

/**
 * lrg_node_tilemap_set_layer_tiles:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @tiles: (array length=n_tiles): cells in row-major order
 * @n_tiles: must equal width * height
 *
 * Replaces all cells of a layer.
 *
 * Returns: %FALSE on a bad layer or length
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_set_layer_tiles (LrgNodeTilemap *self,
                                           guint           layer,
                                           const guint    *tiles,
                                           gsize           n_tiles);

/**
 * lrg_node_tilemap_flood_region:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 * @x: seed column
 * @y: seed row
 *
 * Collects the 4-connected region of cells holding the same tile id as
 * the seed cell (the flood-fill area). Cells are returned as row-major
 * indices (y * width + x) in breadth-first order.
 *
 * Returns: (transfer full) (element-type guint): the region; empty when
 *   the seed is out of range
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_node_tilemap_flood_region (LrgNodeTilemap *self,
                                        guint           layer,
                                        gint            x,
                                        gint            y);

/**
 * lrg_node_tilemap_count_tiles:
 * @self: an #LrgNodeTilemap
 * @layer: a layer index
 *
 * Returns: the number of non-empty cells in @layer
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_tilemap_count_tiles (LrgNodeTilemap *self,
                                    guint           layer);

/**
 * lrg_node_tilemap_local_to_cell:
 * @self: an #LrgNodeTilemap
 * @tile_size: world size of one cell (> 0)
 * @local_x: node-local x
 * @local_y: node-local y
 * @out_x: (out): cell column (may be outside the grid)
 * @out_y: (out): cell row (may be outside the grid)
 *
 * Maps a node-local point to the cell under it (see the file description
 * for the geometry).
 *
 * Returns: whether the cell is inside the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_local_to_cell (LrgNodeTilemap *self,
                                         gfloat          tile_size,
                                         gfloat          local_x,
                                         gfloat          local_y,
                                         gint           *out_x,
                                         gint           *out_y);

/**
 * lrg_node_tilemap_copy:
 * @self: an #LrgNodeTilemap
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgNodeTilemap * lrg_node_tilemap_copy (LrgNodeTilemap *self);

/**
 * lrg_node_tilemap_equal:
 * @a: (nullable): an #LrgNodeTilemap
 * @b: (nullable): an #LrgNodeTilemap
 *
 * Returns: whether both are %NULL or hold the same size, slicing and layers
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_tilemap_equal (LrgNodeTilemap *a,
                                 LrgNodeTilemap *b);

/**
 * lrg_node_tilemap_to_runtime:
 * @self: an #LrgNodeTilemap
 * @tileset: the tileset to draw with
 *
 * Builds a runtime #LrgTilemap with the same layers (names, visibility
 * and cells), e.g. for a game that loads tilemaps from a level.
 *
 * Returns: (transfer full): a new #LrgTilemap
 */
LRG_AVAILABLE_IN_ALL
LrgTilemap * lrg_node_tilemap_to_runtime (LrgNodeTilemap *self,
                                          LrgTileset     *tileset);

G_END_DECLS
