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
#include "lrg-rule-tile.h"

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

/* ==========================================================================
 * .rtileset assets (0.9.0)
 *
 * An .rtileset file describes a tileset image and everything the tiles
 * carry: flags, collision polygons, custom data, animation frames and
 * rule tiles.  Tile ids in this API are 0-based tile indices (as above);
 * layer cells hold 1-based ids (see lrg_tile_get_id()).
 * ========================================================================== */

/**
 * LRG_TILESET_ERROR:
 *
 * Error domain of the .rtileset reader and writer.
 */
#define LRG_TILESET_ERROR (lrg_tileset_error_quark ())

/**
 * lrg_tileset_error_quark:
 *
 * Returns: the error quark of #LRG_TILESET_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_tileset_error_quark (void);

/**
 * LrgTilesetError:
 * @LRG_TILESET_ERROR_IO: the file or its image could not be read or written
 * @LRG_TILESET_ERROR_PARSE: the text is not valid YAML
 * @LRG_TILESET_ERROR_INVALID: the document breaks the .rtileset schema; the
 *   message names the key path (`file: /tiles/3/collision/0: ...`)
 *
 * Errors of the .rtileset reader and writer.
 */
typedef enum
{
    LRG_TILESET_ERROR_IO,
    LRG_TILESET_ERROR_PARSE,
    LRG_TILESET_ERROR_INVALID
} LrgTilesetError;

LRG_AVAILABLE_IN_ALL
GType lrg_tileset_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TILESET_ERROR (lrg_tileset_error_get_type ())

/**
 * lrg_tileset_new_empty:
 * @tile_width: width of each tile in pixels
 * @tile_height: height of each tile in pixels
 * @columns: tiles per row of the image
 * @tile_count: the number of tiles
 *
 * Creates a tileset without a texture: a description of the tile grid
 * that tools, servers and tests can use without a GL context.  Attach the
 * image later with lrg_tileset_set_texture() or lrg_tileset_load_texture().
 *
 * Returns: (transfer full): a new #LrgTileset
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tileset_new_empty (guint tile_width,
                                    guint tile_height,
                                    guint columns,
                                    guint tile_count);

/**
 * lrg_tileset_new_from_asset_file:
 * @path: path to an .rtileset file
 * @load_texture: also load the image (needs a GL context)
 * @error: return location for an error
 *
 * Loads an .rtileset.  The image path in the file is relative to the
 * file's directory.  Without @load_texture only the description is read
 * (editors, command-line tools, servers).
 *
 * Returns: (transfer full) (nullable): the tileset, or %NULL with @error
 *   set (%LRG_TILESET_ERROR)
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tileset_new_from_asset_file (const gchar  *path,
                                              gboolean      load_texture,
                                              GError      **error);

/**
 * lrg_tileset_new_from_asset_data:
 * @data: .rtileset text
 * @length: length of @data, or -1 if NUL-terminated
 * @base_dir: (nullable): directory the image path is relative to
 * @source: (nullable): name used in error messages (default "<data>")
 * @load_texture: also load the image (needs a GL context and @base_dir)
 * @error: return location for an error
 *
 * Parses .rtileset text; see lrg_tileset_new_from_asset_file().
 *
 * Returns: (transfer full) (nullable): the tileset, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tileset_new_from_asset_data (const gchar  *data,
                                              gssize        length,
                                              const gchar  *base_dir,
                                              const gchar  *source,
                                              gboolean      load_texture,
                                              GError      **error);

/**
 * lrg_tileset_save_to_file:
 * @self: an #LrgTileset
 * @path: where to write the .rtileset
 * @error: return location for an error
 *
 * Writes the tileset as an .rtileset.  Saving the same tileset twice gives
 * the same bytes; keys of a loaded file that this version does not know
 * are written back.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_save_to_file (LrgTileset   *self,
                                   const gchar  *path,
                                   GError      **error);

/**
 * lrg_tileset_save_to_data:
 * @self: an #LrgTileset
 * @length: (out) (optional): length of the text
 * @error: return location for an error
 *
 * Returns: (transfer full) (nullable): the .rtileset text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_tileset_save_to_data (LrgTileset  *self,
                                  gsize       *length,
                                  GError     **error);

/**
 * lrg_tileset_set_texture:
 * @self: an #LrgTileset
 * @texture: (nullable) (transfer none): the tileset image
 *
 * Attaches (or detaches) the image the tiles are drawn from.  The tile
 * grid (size, columns, count) is kept.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_texture (LrgTileset *self,
                              GrlTexture *texture);

/**
 * lrg_tileset_load_texture:
 * @self: an #LrgTileset
 * @error: return location for an error
 *
 * Loads #LrgTileset:image-path (relative to the directory the tileset was
 * loaded from) and attaches it.  Needs a GL context.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_load_texture (LrgTileset  *self,
                                   GError     **error);

/**
 * lrg_tileset_get_name:
 * @self: an #LrgTileset
 *
 * Returns: (transfer none) (nullable): the tileset's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tileset_get_name (LrgTileset *self);

/**
 * lrg_tileset_set_name:
 * @self: an #LrgTileset
 * @name: (nullable): the name
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_name (LrgTileset  *self,
                           const gchar *name);

/**
 * lrg_tileset_get_image_path:
 * @self: an #LrgTileset
 *
 * Returns: (transfer none) (nullable): the image path as written in the
 *   .rtileset (relative to its directory)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tileset_get_image_path (LrgTileset *self);

/**
 * lrg_tileset_set_image_path:
 * @self: an #LrgTileset
 * @path: (nullable): the image path, relative to the .rtileset
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_image_path (LrgTileset  *self,
                                 const gchar *path);

/**
 * lrg_tileset_get_margin:
 * @self: an #LrgTileset
 *
 * Returns: pixels between the image edge and the first tile
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_margin (LrgTileset *self);

/**
 * lrg_tileset_get_spacing:
 * @self: an #LrgTileset
 *
 * Returns: pixels between neighbouring tiles in the image
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_spacing (LrgTileset *self);

/**
 * lrg_tileset_set_margin_spacing:
 * @self: an #LrgTileset
 * @margin: pixels between the image edge and the first tile
 * @spacing: pixels between neighbouring tiles
 *
 * Sets the image layout used by lrg_tileset_get_tile_rect().
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_margin_spacing (LrgTileset *self,
                                     guint       margin,
                                     guint       spacing);

/**
 * lrg_tileset_get_draw_offset:
 * @self: an #LrgTileset
 * @x: (out) (optional): horizontal offset in pixels
 * @y: (out) (optional): vertical offset in pixels
 *
 * Gets the offset tiles are drawn at relative to their cell (Tiled's
 * tile offset): tall isometric tiles, art with a drop shadow.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_get_draw_offset (LrgTileset *self,
                                  gint       *x,
                                  gint       *y);

/**
 * lrg_tileset_set_draw_offset:
 * @self: an #LrgTileset
 * @x: horizontal offset in pixels
 * @y: vertical offset in pixels
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_draw_offset (LrgTileset *self,
                                  gint        x,
                                  gint        y);

/* --------------------------------------------------------------------------
 * Per-tile collision shapes
 * -------------------------------------------------------------------------- */

/**
 * lrg_tileset_add_tile_collision_polygon:
 * @self: an #LrgTileset
 * @tile_id: the tile index (0-based)
 * @coords: (array length=n_coords): x0, y0, x1, y1, ... in tile-local
 *   pixels (0,0 is the tile's top-left corner)
 * @n_coords: number of floats (twice the point count, at least 6)
 *
 * Adds a collision polygon to a tile.  A tile with polygons collides only
 * where they are; a %LRG_TILE_PROPERTY_SOLID tile without polygons
 * collides over its whole square.  Polygons follow the cell transform
 * (lrg_tile_transform_point()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_add_tile_collision_polygon (LrgTileset   *self,
                                             guint         tile_id,
                                             const gfloat *coords,
                                             guint         n_coords);

/**
 * lrg_tileset_get_tile_collision_polygon_count:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 *
 * Returns: the number of collision polygons of the tile
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_tile_collision_polygon_count (LrgTileset *self,
                                                    guint       tile_id);

/**
 * lrg_tileset_get_tile_collision_polygon:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @index: the polygon index
 * @n_coords: (out): number of floats returned
 *
 * Returns: (array length=n_coords) (transfer none) (nullable): the
 *   polygon's coordinates in tile-local pixels, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_tileset_get_tile_collision_polygon (LrgTileset *self,
                                                       guint       tile_id,
                                                       guint       index,
                                                       guint      *n_coords);

/**
 * lrg_tileset_clear_tile_collision:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 *
 * Removes every collision polygon of a tile.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_clear_tile_collision (LrgTileset *self,
                                       guint       tile_id);

/**
 * lrg_tileset_tile_has_collision:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 *
 * Returns: %TRUE if the tile is %LRG_TILE_PROPERTY_SOLID or has
 *   collision polygons
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_tile_has_collision (LrgTileset *self,
                                         guint       tile_id);

/* --------------------------------------------------------------------------
 * Per-tile custom data
 * -------------------------------------------------------------------------- */

/**
 * lrg_tileset_set_tile_data:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @key: the data key
 * @value: (nullable): the value as text; %NULL removes the key
 *
 * Sets a custom key on a tile (terrain kind, footstep sound, damage...).
 * Keys keep the order they were first set in.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_tile_data (LrgTileset  *self,
                                guint        tile_id,
                                const gchar *key,
                                const gchar *value);

/**
 * lrg_tileset_get_tile_data:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @key: the data key
 *
 * Returns: (transfer none) (nullable): the value, or %NULL if unset
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tileset_get_tile_data (LrgTileset  *self,
                                         guint        tile_id,
                                         const gchar *key);

/**
 * lrg_tileset_get_tile_data_double:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @key: the data key
 * @default_value: returned when the key is unset or not a number
 *
 * Returns: the value parsed as a number
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_tileset_get_tile_data_double (LrgTileset  *self,
                                          guint        tile_id,
                                          const gchar *key,
                                          gdouble      default_value);

/**
 * lrg_tileset_get_tile_data_boolean:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @key: the data key
 * @default_value: returned when the key is unset or not true/false
 *
 * Returns: the value parsed as a boolean
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_get_tile_data_boolean (LrgTileset  *self,
                                            guint        tile_id,
                                            const gchar *key,
                                            gboolean     default_value);

/**
 * lrg_tileset_get_tile_data_keys:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 *
 * Returns: (transfer full) (array zero-terminated=1): the tile's keys in
 *   order; free with g_strfreev()
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_tileset_get_tile_data_keys (LrgTileset *self,
                                         guint       tile_id);

/**
 * lrg_tileset_find_tiles_with_data:
 * @self: an #LrgTileset
 * @key: the data key
 * @value: (nullable): the value to match, or %NULL for any value
 * @n_tiles: (out): the number of tile ids returned
 *
 * Returns: (array length=n_tiles) (transfer full): the indices of the
 *   tiles that have @key (set to @value), ascending; free with g_free()
 */
LRG_AVAILABLE_IN_ALL
guint * lrg_tileset_find_tiles_with_data (LrgTileset  *self,
                                          const gchar *key,
                                          const gchar *value,
                                          guint       *n_tiles);

/* --------------------------------------------------------------------------
 * Per-tile animation
 * -------------------------------------------------------------------------- */

/**
 * lrg_tileset_set_tile_animation:
 * @self: an #LrgTileset
 * @tile_id: the tile index that is animated
 * @frames: (array length=n_frames) (nullable): the tile indices shown, in
 *   order
 * @durations_ms: (array length=n_frames) (nullable): how long each frame
 *   shows, in milliseconds (> 0)
 * @n_frames: the frame count; 0 removes the animation
 *
 * Animates a tile: every cell holding @tile_id shows the frames in turn
 * (see lrg_tileset_get_animated_tile()).  Sets or clears
 * %LRG_TILE_PROPERTY_ANIMATED.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_set_tile_animation (LrgTileset  *self,
                                     guint        tile_id,
                                     const guint *frames,
                                     const guint *durations_ms,
                                     guint        n_frames);

/**
 * lrg_tileset_get_tile_animation_length:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 *
 * Returns: the number of animation frames (0: not animated)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_tile_animation_length (LrgTileset *self,
                                             guint       tile_id);

/**
 * lrg_tileset_get_tile_animation_frame:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @index: the frame index
 * @out_tile: (out) (optional): the frame's tile index
 * @out_duration_ms: (out) (optional): the frame's duration
 *
 * Returns: %TRUE if the frame exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_get_tile_animation_frame (LrgTileset *self,
                                               guint       tile_id,
                                               guint       index,
                                               guint      *out_tile,
                                               guint      *out_duration_ms);

/**
 * lrg_tileset_get_animated_tile:
 * @self: an #LrgTileset
 * @tile_id: the tile index
 * @time_ms: the animation clock in milliseconds
 *
 * Gets the tile index an animated tile shows at a time (looping).
 *
 * Returns: the frame's tile index, or @tile_id if it is not animated
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_animated_tile (LrgTileset *self,
                                     guint       tile_id,
                                     guint64     time_ms);

/* --------------------------------------------------------------------------
 * Rule tiles
 * -------------------------------------------------------------------------- */

/**
 * lrg_tileset_add_rule_tile:
 * @self: an #LrgTileset
 * @rule_tile: (transfer none): the rule tile
 *
 * Adds a rule tile (saved in the .rtileset).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_add_rule_tile (LrgTileset  *self,
                                LrgRuleTile *rule_tile);

/**
 * lrg_tileset_remove_rule_tile:
 * @self: an #LrgTileset
 * @rule_tile: the rule tile
 *
 * Returns: %TRUE if it was part of the tileset
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tileset_remove_rule_tile (LrgTileset  *self,
                                       LrgRuleTile *rule_tile);

/**
 * lrg_tileset_get_n_rule_tiles:
 * @self: an #LrgTileset
 *
 * Returns: the number of rule tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_get_n_rule_tiles (LrgTileset *self);

/**
 * lrg_tileset_get_rule_tile:
 * @self: an #LrgTileset
 * @index: the rule tile index
 *
 * Returns: (transfer none) (nullable): the rule tile
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_tileset_get_rule_tile (LrgTileset *self,
                                         guint       index);

/**
 * lrg_tileset_get_rule_tile_by_name:
 * @self: an #LrgTileset
 * @name: the rule tile's name
 *
 * Returns: (transfer none) (nullable): the rule tile
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_tileset_get_rule_tile_by_name (LrgTileset  *self,
                                                 const gchar *name);

/**
 * lrg_tileset_find_rule_tile_for:
 * @self: an #LrgTileset
 * @tile: a cell value (1-based id, transform bits ignored)
 *
 * Returns: (transfer none) (nullable): the first rule tile that owns
 *   @tile (lrg_rule_tile_owns_tile()), or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgRuleTile * lrg_tileset_find_rule_tile_for (LrgTileset *self,
                                              guint       tile);

/**
 * lrg_tileset_apply_rule_tiles:
 * @self: an #LrgTileset
 * @tiles: (array length=n_tiles): a layer's cells, row-major, rewritten
 *   in place
 * @n_tiles: the number of cells (a multiple of @width)
 * @width: the layer width
 * @layout: the grid layout
 * @x: the rectangle's first column
 * @y: the rectangle's first row
 * @rect_width: the rectangle width (G_MAXUINT: to the edge)
 * @rect_height: the rectangle height
 *
 * Re-resolves every cell in the rectangle owned by any of the tileset's
 * rule tiles (a full refresh after loading or bulk edits).
 *
 * Returns: the number of cells whose value changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tileset_apply_rule_tiles (LrgTileset            *self,
                                    guint                 *tiles,
                                    gsize                  n_tiles,
                                    guint                  width,
                                    LrgTileNeighborLayout  layout,
                                    gint                   x,
                                    gint                   y,
                                    guint                  rect_width,
                                    guint                  rect_height);

/**
 * lrg_tileset_changed:
 * @self: an #LrgTileset
 *
 * Emits #LrgTileset::changed (the tileset's own setters do).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tileset_changed (LrgTileset *self);

G_END_DECLS
