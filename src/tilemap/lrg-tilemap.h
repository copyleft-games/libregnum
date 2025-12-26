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

G_END_DECLS
