/* lrg-tileset.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas for tilemap rendering.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TILESET (lrg_tileset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTileset, lrg_tileset, LRG, TILESET, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_tileset_new:
 * @texture: (transfer none): the source texture atlas
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 *
 * Creates a new tileset from a texture atlas. The texture is divided
 * into a grid of tiles based on the specified tile dimensions.
 *
 * The number of columns is calculated from the texture width divided
 * by @tile_width. Tiles are numbered left-to-right, top-to-bottom,
 * starting from 0.
 *
 * Returns: (transfer full): A new #LrgTileset
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tileset_new (GrlTexture *texture,
                              guint       tile_width,
                              guint       tile_height);

/**
 * lrg_tileset_new_from_file:
 * @path: path to the image file
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 * @error: (nullable): return location for error, or %NULL
 *
 * Creates a new tileset by loading a texture from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTileset, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tileset_new_from_file (const gchar  *path,
                                        guint         tile_width,
                                        guint         tile_height,
                                        GError      **error);

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_tileset_get_texture:
 * @self: an #LrgTileset
 *
 * Gets the underlying texture atlas.
 *
 * Returns: (transfer none): The texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_tileset_get_texture (LrgTileset *self);

/**
 * lrg_tileset_get_tile_width:
 * @self: an #LrgTileset
 *
 * Gets the width of each tile in pixels.
 *
 * Returns: The tile width
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_tile_width (LrgTileset *self);

/**
 * lrg_tileset_get_tile_height:
 * @self: an #LrgTileset
 *
 * Gets the height of each tile in pixels.
 *
 * Returns: The tile height
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_tile_height (LrgTileset *self);

/**
 * lrg_tileset_get_columns:
 * @self: an #LrgTileset
 *
 * Gets the number of tile columns in the tileset.
 *
 * Returns: The column count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_columns (LrgTileset *self);

/**
 * lrg_tileset_get_rows:
 * @self: an #LrgTileset
 *
 * Gets the number of tile rows in the tileset.
 *
 * Returns: The row count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_rows (LrgTileset *self);

/**
 * lrg_tileset_get_tile_count:
 * @self: an #LrgTileset
 *
 * Gets the total number of tiles in the tileset.
 *
 * Returns: The tile count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_tile_count (LrgTileset *self);

/* ==========================================================================
 * Tile Rectangles
 * ========================================================================== */

/**
 * lrg_tileset_get_tile_rect:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 *
 * Gets the source rectangle for a specific tile within the texture.
 * This rectangle can be used with grl_draw_texture_rec() to render
 * the tile.
 *
 * Returns: (transfer full) (nullable): The tile source rectangle,
 *          or %NULL if @tile_id is out of bounds
 */
LRG_AVAILABLE_IN_ALL
GrlRectangle * lrg_tileset_get_tile_rect (LrgTileset *self,
                                          guint       tile_id);

/**
 * lrg_tileset_get_tile_rect_to:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @out_rect: (out caller-allocates): location to store the rectangle
 *
 * Gets the source rectangle for a specific tile, storing it in
 * a caller-provided rectangle. This avoids allocation.
 *
 * Returns: %TRUE if successful, %FALSE if @tile_id is out of bounds
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_get_tile_rect_to (LrgTileset   *self,
                                       guint         tile_id,
                                       GrlRectangle *out_rect);

/* ==========================================================================
 * Tile Properties
 * ========================================================================== */

/**
 * lrg_tileset_get_tile_properties:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 *
 * Gets the property flags for a specific tile.
 *
 * Returns: The tile property flags
 */
LRG_AVAILABLE_IN_ALL
LrgTileProperty lrg_tileset_get_tile_properties (LrgTileset *self,
                                                 guint       tile_id);

/**
 * lrg_tileset_set_tile_properties:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @properties: the property flags to set
 *
 * Sets the property flags for a specific tile.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_tile_properties (LrgTileset      *self,
                                      guint            tile_id,
                                      LrgTileProperty  properties);

/**
 * lrg_tileset_tile_has_property:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @property: the property to check
 *
 * Checks if a tile has a specific property flag set.
 *
 * Returns: %TRUE if the tile has the property
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_tile_has_property (LrgTileset      *self,
                                        guint            tile_id,
                                        LrgTileProperty  property);

G_END_DECLS
