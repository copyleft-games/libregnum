/* lrg-tilemap.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Multi-layer tilemap with rendering support.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-tileset.h"
#include "lrg-tilemap-layer.h"

G_BEGIN_DECLS

#define LRG_TYPE_TILEMAP (lrg_tilemap_get_type ())

/* ==========================================================================
 * Grid orientation (0.9.0)
 * ========================================================================== */

/**
 * LrgTilemapOrientation:
 * @LRG_TILEMAP_ORIENTATION_ORTHOGONAL: square cells in rows and columns
 * @LRG_TILEMAP_ORIENTATION_ISOMETRIC: diamond cells; x runs down-right and
 *   y down-left on screen (Tiled's "isometric")
 * @LRG_TILEMAP_ORIENTATION_STAGGERED: diamond cells laid out in rows (or
 *   columns) with every other row shifted half a cell (Tiled's "staggered")
 * @LRG_TILEMAP_ORIENTATION_HEXAGONAL: hexagonal cells in staggered rows
 *   (pointy-top, stagger axis y) or columns (flat-top, stagger axis x)
 *
 * How grid cells are laid out on screen.  The conventions (origin, stagger
 * axis and index, hex side length) match Tiled's, so maps can be
 * exchanged with it.
 */
typedef enum
{
    LRG_TILEMAP_ORIENTATION_ORTHOGONAL,
    LRG_TILEMAP_ORIENTATION_ISOMETRIC,
    LRG_TILEMAP_ORIENTATION_STAGGERED,
    LRG_TILEMAP_ORIENTATION_HEXAGONAL
} LrgTilemapOrientation;

LRG_AVAILABLE_IN_ALL
GType lrg_tilemap_orientation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILEMAP_ORIENTATION (lrg_tilemap_orientation_get_type ())

/**
 * LrgTilemapStaggerAxis:
 * @LRG_TILEMAP_STAGGER_AXIS_X: columns are staggered (flat-top hexagons)
 * @LRG_TILEMAP_STAGGER_AXIS_Y: rows are staggered (pointy-top hexagons)
 *
 * Which axis staggered and hexagonal maps shift.
 */
typedef enum
{
    LRG_TILEMAP_STAGGER_AXIS_X,
    LRG_TILEMAP_STAGGER_AXIS_Y
} LrgTilemapStaggerAxis;

LRG_AVAILABLE_IN_ALL
GType lrg_tilemap_stagger_axis_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILEMAP_STAGGER_AXIS (lrg_tilemap_stagger_axis_get_type ())

/**
 * LrgTilemapStaggerIndex:
 * @LRG_TILEMAP_STAGGER_INDEX_ODD: odd rows (columns) are shifted
 * @LRG_TILEMAP_STAGGER_INDEX_EVEN: even rows (columns) are shifted
 *
 * Which rows (or columns) of a staggered or hexagonal map are shifted
 * by half a cell (right for rows, down for columns).
 */
typedef enum
{
    LRG_TILEMAP_STAGGER_INDEX_ODD,
    LRG_TILEMAP_STAGGER_INDEX_EVEN
} LrgTilemapStaggerIndex;

LRG_AVAILABLE_IN_ALL
GType lrg_tilemap_stagger_index_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILEMAP_STAGGER_INDEX (lrg_tilemap_stagger_index_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTilemap, lrg_tilemap, LRG, TILEMAP, GObject)

/**
 * LrgTilemapClass:
 * @parent_class: the parent class
 *
 * The class structure for #LrgTilemap.
 */
struct _LrgTilemapClass
{
    GObjectClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Signals
 * ========================================================================== */

/**
 * LrgTilemap::tile-changed:
 * @self: the tilemap
 * @layer_index: the layer where the tile changed
 * @x: the tile X coordinate
 * @y: the tile Y coordinate
 * @old_tile: the previous tile ID
 * @new_tile: the new tile ID
 *
 * Emitted when a tile value changes in any layer.
 */

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tilemap_new:
 * @tileset: (transfer none): the tileset to use for rendering
 *
 * Creates a new tilemap with the specified tileset.
 * The tilemap starts with no layers.
 *
 * Returns: (transfer full): A new #LrgTilemap
 */
LRG_AVAILABLE_IN_ALL
LrgTilemap * lrg_tilemap_new (LrgTileset *tileset);

/* ==========================================================================
 * Tileset
 * ========================================================================== */

/**
 * lrg_tilemap_get_tileset:
 * @self: an #LrgTilemap
 *
 * Gets the tileset used for rendering.
 *
 * Returns: (transfer none): The tileset
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tilemap_get_tileset (LrgTilemap *self);

/**
 * lrg_tilemap_set_tileset:
 * @self: an #LrgTilemap
 * @tileset: (transfer none): the tileset
 *
 * Sets the tileset used for rendering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_tileset (LrgTilemap *self,
                              LrgTileset *tileset);

/* ==========================================================================
 * Layer Management
 * ========================================================================== */

/**
 * lrg_tilemap_add_layer:
 * @self: an #LrgTilemap
 * @layer: (transfer none): the layer to add
 *
 * Adds a layer to the tilemap. Layers are rendered in the order
 * they are added (first added = rendered first = behind).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_add_layer (LrgTilemap      *self,
                            LrgTilemapLayer *layer);

/**
 * lrg_tilemap_insert_layer:
 * @self: an #LrgTilemap
 * @layer: (transfer none): the layer to insert
 * @index: the position to insert at
 *
 * Inserts a layer at a specific position. Layers with lower indices
 * are rendered first (behind layers with higher indices).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_insert_layer (LrgTilemap      *self,
                               LrgTilemapLayer *layer,
                               guint            index);

/**
 * lrg_tilemap_remove_layer:
 * @self: an #LrgTilemap
 * @layer: the layer to remove
 *
 * Removes a layer from the tilemap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_remove_layer (LrgTilemap      *self,
                               LrgTilemapLayer *layer);

/**
 * lrg_tilemap_remove_layer_at:
 * @self: an #LrgTilemap
 * @index: the layer index to remove
 *
 * Removes the layer at the specified index.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_remove_layer_at (LrgTilemap *self,
                                  guint       index);

/**
 * lrg_tilemap_get_layer:
 * @self: an #LrgTilemap
 * @index: the layer index
 *
 * Gets a layer by index.
 *
 * Returns: (transfer none) (nullable): The layer, or %NULL if out of bounds
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapLayer * lrg_tilemap_get_layer (LrgTilemap *self,
                                         guint       index);

/**
 * lrg_tilemap_get_layer_by_name:
 * @self: an #LrgTilemap
 * @name: the layer name
 *
 * Finds a layer by name.
 *
 * Returns: (transfer none) (nullable): The layer, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapLayer * lrg_tilemap_get_layer_by_name (LrgTilemap  *self,
                                                 const gchar *name);

/**
 * lrg_tilemap_get_layer_count:
 * @self: an #LrgTilemap
 *
 * Gets the number of layers in the tilemap.
 *
 * Returns: The layer count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_layer_count (LrgTilemap *self);

/**
 * lrg_tilemap_get_layers:
 * @self: an #LrgTilemap
 *
 * Gets all layers in the tilemap.
 *
 * Returns: (element-type LrgTilemapLayer) (transfer none): The layers list
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_tilemap_get_layers (LrgTilemap *self);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_tilemap_draw:
 * @self: an #LrgTilemap
 *
 * Draws all visible layers of the tilemap at position (0, 0).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_draw (LrgTilemap *self);

/**
 * lrg_tilemap_draw_at:
 * @self: an #LrgTilemap
 * @x: the X position to draw at
 * @y: the Y position to draw at
 *
 * Draws all visible layers at the specified position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_draw_at (LrgTilemap *self,
                          gfloat      x,
                          gfloat      y);

/**
 * lrg_tilemap_draw_with_camera:
 * @self: an #LrgTilemap
 * @camera: the camera for view transformation and parallax
 *
 * Draws all visible layers using a camera for view transformation.
 * Parallax scrolling is applied based on each layer's parallax settings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_draw_with_camera (LrgTilemap  *self,
                                   GrlCamera2D *camera);

/**
 * lrg_tilemap_draw_layer:
 * @self: an #LrgTilemap
 * @layer_index: the layer to draw
 * @x: the X position
 * @y: the Y position
 *
 * Draws a specific layer at the given position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_draw_layer (LrgTilemap *self,
                             guint       layer_index,
                             gfloat      x,
                             gfloat      y);

/* ==========================================================================
 * Collision Queries
 * ========================================================================== */

/**
 * lrg_tilemap_is_solid:
 * @self: an #LrgTilemap
 * @tile_x: the tile X coordinate
 * @tile_y: the tile Y coordinate
 *
 * Checks if any collision-enabled layer has a solid tile at the position.
 *
 * Returns: %TRUE if there is a solid tile
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_is_solid (LrgTilemap *self,
                               guint       tile_x,
                               guint       tile_y);

/**
 * lrg_tilemap_is_solid_at:
 * @self: an #LrgTilemap
 * @world_x: the world X coordinate in pixels
 * @world_y: the world Y coordinate in pixels
 *
 * Checks if there is a solid tile at the world position.
 *
 * Returns: %TRUE if there is a solid tile
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_is_solid_at (LrgTilemap *self,
                                  gfloat      world_x,
                                  gfloat      world_y);

/**
 * lrg_tilemap_get_tile_at:
 * @self: an #LrgTilemap
 * @layer_index: the layer to query
 * @world_x: the world X coordinate in pixels
 * @world_y: the world Y coordinate in pixels
 *
 * Gets the tile ID at a world position for a specific layer.
 *
 * Returns: The tile ID, or %LRG_TILEMAP_EMPTY_TILE if out of bounds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_tile_at (LrgTilemap *self,
                               guint       layer_index,
                               gfloat      world_x,
                               gfloat      world_y);

/* ==========================================================================
 * World Bounds
 * ========================================================================== */

/**
 * lrg_tilemap_get_world_bounds:
 * @self: an #LrgTilemap
 *
 * Gets the world bounds of the tilemap in pixels.
 * Uses the dimensions of the first layer or returns empty if no layers.
 *
 * Returns: (transfer full): The bounds rectangle
 */
LRG_AVAILABLE_IN_ALL
GrlRectangle * lrg_tilemap_get_world_bounds (LrgTilemap *self);

/**
 * lrg_tilemap_get_width:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap width in tiles (from first layer).
 *
 * Returns: The width in tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_width (LrgTilemap *self);

/**
 * lrg_tilemap_get_height:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap height in tiles (from first layer).
 *
 * Returns: The height in tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_height (LrgTilemap *self);

/**
 * lrg_tilemap_get_pixel_width:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap width in pixels.
 *
 * Returns: The width in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_pixel_width (LrgTilemap *self);

/**
 * lrg_tilemap_get_pixel_height:
 * @self: an #LrgTilemap
 *
 * Gets the tilemap height in pixels.
 *
 * Returns: The height in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_pixel_height (LrgTilemap *self);

/* ==========================================================================
 * Coordinate Conversion
 * ========================================================================== */

/**
 * lrg_tilemap_world_to_tile:
 * @self: an #LrgTilemap
 * @world_x: the world X coordinate
 * @world_y: the world Y coordinate
 * @out_tile_x: (out): return location for tile X
 * @out_tile_y: (out): return location for tile Y
 *
 * Converts world coordinates to tile coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_world_to_tile (LrgTilemap *self,
                                gfloat      world_x,
                                gfloat      world_y,
                                guint      *out_tile_x,
                                guint      *out_tile_y);

/**
 * lrg_tilemap_tile_to_world:
 * @self: an #LrgTilemap
 * @tile_x: the tile X coordinate
 * @tile_y: the tile Y coordinate
 * @out_world_x: (out): return location for world X
 * @out_world_y: (out): return location for world Y
 *
 * Converts tile coordinates to world coordinates (top-left corner of tile).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_tile_to_world (LrgTilemap *self,
                                guint       tile_x,
                                guint       tile_y,
                                gfloat     *out_world_x,
                                gfloat     *out_world_y);

/* ==========================================================================
 * Orientation, cells and picking (0.9.0)
 *
 * World coordinates are pixels relative to the map's draw position.  A
 * cell's bounding box is cell-width x cell-height; tiles are drawn with
 * their bottom-left corner on the bottom-left of the box (plus the
 * tileset's draw offset), so taller tiles stick out upwards.
 * ========================================================================== */

/**
 * lrg_tilemap_get_orientation:
 * @self: an #LrgTilemap
 *
 * Returns: the grid orientation
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapOrientation lrg_tilemap_get_orientation (LrgTilemap *self);

/**
 * lrg_tilemap_set_orientation:
 * @self: an #LrgTilemap
 * @orientation: the grid orientation
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_orientation (LrgTilemap            *self,
                                  LrgTilemapOrientation  orientation);

/**
 * lrg_tilemap_get_stagger_axis:
 * @self: an #LrgTilemap
 *
 * Returns: the stagger axis of staggered and hexagonal maps
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapStaggerAxis lrg_tilemap_get_stagger_axis (LrgTilemap *self);

/**
 * lrg_tilemap_set_stagger_axis:
 * @self: an #LrgTilemap
 * @axis: the stagger axis
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_stagger_axis (LrgTilemap            *self,
                                   LrgTilemapStaggerAxis  axis);

/**
 * lrg_tilemap_get_stagger_index:
 * @self: an #LrgTilemap
 *
 * Returns: which rows or columns are shifted
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapStaggerIndex lrg_tilemap_get_stagger_index (LrgTilemap *self);

/**
 * lrg_tilemap_set_stagger_index:
 * @self: an #LrgTilemap
 * @index: which rows or columns are shifted
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_stagger_index (LrgTilemap             *self,
                                    LrgTilemapStaggerIndex  index);

/**
 * lrg_tilemap_get_hex_side_length:
 * @self: an #LrgTilemap
 *
 * Gets the length in pixels of the flat sides of hexagonal cells along
 * the stagger axis (Tiled's hexsidelength).  0 means half the cell
 * height (stagger axis y) or width (x).  Staggered maps always use 0
 * side length (diamonds).
 *
 * Returns: the configured side length (0: automatic)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_hex_side_length (LrgTilemap *self);

/**
 * lrg_tilemap_set_hex_side_length:
 * @self: an #LrgTilemap
 * @length: the side length in pixels, 0 for automatic
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_hex_side_length (LrgTilemap *self,
                                      guint       length);

/**
 * lrg_tilemap_get_cell_width:
 * @self: an #LrgTilemap
 *
 * Returns: the cell width in pixels (the tileset's tile width unless set
 *   with lrg_tilemap_set_cell_size())
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_cell_width (LrgTilemap *self);

/**
 * lrg_tilemap_get_cell_height:
 * @self: an #LrgTilemap
 *
 * Returns: the cell height in pixels (the tileset's tile height unless
 *   set with lrg_tilemap_set_cell_size())
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_cell_height (LrgTilemap *self);

/**
 * lrg_tilemap_set_cell_size:
 * @self: an #LrgTilemap
 * @width: the cell width in pixels, 0 to follow the tileset
 * @height: the cell height in pixels, 0 to follow the tileset
 *
 * Sets the grid cell size independently of the tile images, e.g. a
 * 64x32 isometric grid drawn with 64x64 tiles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_cell_size (LrgTilemap *self,
                                guint       width,
                                guint       height);

/**
 * lrg_tilemap_get_neighbor_layout:
 * @self: an #LrgTilemap
 *
 * Returns: how neighbours are found on this map (for rule tiles)
 */
LRG_AVAILABLE_IN_ALL
LrgTileNeighborLayout lrg_tilemap_get_neighbor_layout (LrgTilemap *self);

/**
 * lrg_tilemap_get_cell_bounds:
 * @self: an #LrgTilemap
 * @x: the cell column (may lie outside the map)
 * @y: the cell row
 * @out_rect: (out caller-allocates): the cell's bounding box in world pixels
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_get_cell_bounds (LrgTilemap   *self,
                                  gint          x,
                                  gint          y,
                                  GrlRectangle *out_rect);

/**
 * lrg_tilemap_cell_to_world:
 * @self: an #LrgTilemap
 * @x: the cell column (may lie outside the map)
 * @y: the cell row
 * @out_x: (out): the cell centre's world x
 * @out_y: (out): the cell centre's world y
 *
 * Gets the centre of a cell.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_cell_to_world (LrgTilemap *self,
                                gint        x,
                                gint        y,
                                gfloat     *out_x,
                                gfloat     *out_y);

/**
 * lrg_tilemap_world_to_cell:
 * @self: an #LrgTilemap
 * @world_x: world x in pixels
 * @world_y: world y in pixels
 * @out_x: (out): the column of the cell under the point
 * @out_y: (out): the row of the cell under the point
 *
 * Picks the cell under a point, exactly, for every orientation (diamond
 * and hexagon outlines, not bounding boxes).  The cell is reported even
 * when it lies outside the map.
 *
 * Returns: %TRUE if the cell is inside the map
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_world_to_cell (LrgTilemap *self,
                                    gfloat      world_x,
                                    gfloat      world_y,
                                    gint       *out_x,
                                    gint       *out_y);

/**
 * lrg_tilemap_get_cell_polygon:
 * @self: an #LrgTilemap
 * @x: the cell column
 * @y: the cell row
 * @out_coords: (out caller-allocates) (array fixed-size=12): x, y pairs of
 *   the outline, clockwise from the top
 *
 * Gets a cell's outline in world pixels: a rectangle, a diamond or a
 * hexagon.  Editors draw grids and hover outlines with it.
 *
 * Returns: the number of points written (4 or 6)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_get_cell_polygon (LrgTilemap *self,
                                    gint        x,
                                    gint        y,
                                    gfloat     *out_coords);

/**
 * lrg_tilemap_get_cell_depth:
 * @self: an #LrgTilemap
 * @x: the cell column
 * @y: the cell row
 *
 * Gets a cell's sort key for interleaving sprites with tiles (y-sorting):
 * the world y of the cell centre.  Draw a sprite after the cells whose
 * depth is not greater than the sprite's feet.
 *
 * Returns: the cell's depth
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tilemap_get_cell_depth (LrgTilemap *self,
                                   gint        x,
                                   gint        y);

/**
 * lrg_tilemap_get_draw_order:
 * @self: an #LrgTilemap
 * @x: the first column of the region
 * @y: the first row of the region
 * @width: region width in cells
 * @height: region height in cells
 *
 * Lists the region's cells in the order they are drawn (back to front),
 * so overlapping tiles stack correctly: rows top to bottom for
 * orthogonal, isometric and row-staggered maps; for column-staggered maps
 * each row draws its raised columns before its lowered ones.
 *
 * Returns: (transfer full) (element-type gint): x, y pairs
 */
LRG_AVAILABLE_IN_ALL
GArray * lrg_tilemap_get_draw_order (LrgTilemap *self,
                                     gint        x,
                                     gint        y,
                                     guint       width,
                                     guint       height);

/* ==========================================================================
 * Animated tiles and rule tiles (0.9.0)
 * ========================================================================== */

/**
 * lrg_tilemap_update:
 * @self: an #LrgTilemap
 * @delta: seconds since the last update
 *
 * Advances the animation clock of animated tiles
 * (lrg_tileset_set_tile_animation()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_update (LrgTilemap *self,
                         gfloat      delta);

/**
 * lrg_tilemap_get_animation_time:
 * @self: an #LrgTilemap
 *
 * Returns: the animation clock in milliseconds
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_tilemap_get_animation_time (LrgTilemap *self);

/**
 * lrg_tilemap_set_animation_time:
 * @self: an #LrgTilemap
 * @time_ms: the animation clock in milliseconds
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_set_animation_time (LrgTilemap *self,
                                     guint64     time_ms);

/**
 * lrg_tilemap_paint_rule_tile:
 * @self: an #LrgTilemap
 * @layer_index: the layer to paint
 * @rule_tile: the rule tile brush
 * @x: the cell column
 * @y: the cell row
 *
 * Paints a cell with a rule tile and updates its neighbours, using the
 * map's neighbour layout (lrg_rule_tile_paint()).
 *
 * Returns: the number of cells that changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_paint_rule_tile (LrgTilemap  *self,
                                   guint        layer_index,
                                   LrgRuleTile *rule_tile,
                                   gint         x,
                                   gint         y);

/**
 * lrg_tilemap_erase_rule_tile:
 * @self: an #LrgTilemap
 * @layer_index: the layer
 * @x: the cell column
 * @y: the cell row
 *
 * Empties a cell and updates the rule-tiled cells around it (every rule
 * tile of the tileset).
 *
 * Returns: the number of cells that changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_erase_rule_tile (LrgTilemap *self,
                                   guint       layer_index,
                                   gint        x,
                                   gint        y);

/**
 * lrg_tilemap_refresh_rule_tiles:
 * @self: an #LrgTilemap
 * @layer_index: the layer
 *
 * Re-resolves every rule-tiled cell of a layer (after loading a map whose
 * cells were painted with a different rule set, or after editing rules).
 *
 * Returns: the number of cells that changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_refresh_rule_tiles (LrgTilemap *self,
                                      guint       layer_index);

G_END_DECLS
