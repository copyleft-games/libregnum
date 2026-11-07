/* lrg-editor-tileset-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTilesetEditor: the `.rtileset` data editor of lrgeditor.
 *
 * One tab edits one tileset asset (see docs/modules/tilemap/
 * tileset-asset.org): the tile sheet (the image cut by the tileset's
 * tile size, columns, margin and spacing; click a tile to select it),
 * the layout (name, image, tile size, columns, count, margin, spacing,
 * draw offset), the selected tile's properties (solid / animated /
 * hazard flags, collision polygons drawn and dragged over the magnified
 * tile, custom key/value data, animation frames with durations) and the
 * rule tiles (autotiling brushes: a list of rules, each a 3x3 neighbour
 * mask clicked cell by cell, a transform and weighted output tiles; the
 * 16- and 47-tile presets; seed, default tile and border; a live preview
 * grid painted with the rule tile).
 *
 * The document is the truth: after every change its text is parsed with
 * lrg_tileset_new_from_asset_data() (a parse error is an issue and
 * empties the views). Edits parse a fresh copy, change it through the
 * LrgTileset / LrgRuleTile API and write it back with
 * lrg_tileset_save_to_data() as one undo step of the document (drags
 * merge into one step per gesture).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../tilemap/lrg-tileset.h"
#include "../../../tilemap/lrg-rule-tile.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_TILESET_EDITOR (lrg_editor_tileset_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTilesetEditor, lrg_editor_tileset_editor, LRG, EDITOR_TILESET_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_tileset_editor_new:
 * @app: (nullable): the editor application
 * @document: an `.rtileset` document
 *
 * Creates the editor of @document (lrg_editor_data_editor_open() does
 * this for `.rtileset` files and docks it).
 *
 * Returns: (transfer full): a new #LrgEditorTilesetEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesetEditor * lrg_editor_tileset_editor_new (LrgEditorApp      *app,
                                                        LrgEditorDocument *document);

/**
 * lrg_editor_tileset_editor_open:
 * @app: the editor application
 * @path: an `.rtileset` file, absolute or relative to the project
 * @error: return location for a #GError
 *
 * Opens (or presents) the tab of @path.
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error
 *   set (unreadable file, not a tileset)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesetEditor * lrg_editor_tileset_editor_open (LrgEditorApp  *app,
                                                         const gchar   *path,
                                                         GError       **error);

/**
 * lrg_editor_tileset_editor_create:
 * @app: the editor application
 * @path: the new `.rtileset` file, absolute or relative to the project
 * @image: (nullable): the tileset image, absolute or relative to the
 *   project (stored relative to the new file)
 * @tile_width: tile width in pixels (0: 16)
 * @tile_height: tile height in pixels (0: 16)
 * @name: (nullable): the tileset name (default: the file's base name)
 * @error: return location for a #GError
 *
 * Writes a new tileset asset: columns and tile count come from the
 * image's size (read from the file) or are 8 x 8 without an image.
 * Refuses to overwrite an existing file.
 *
 * Returns: %TRUE when the file was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_create (LrgEditorApp  *app,
                                           const gchar   *path,
                                           const gchar   *image,
                                           guint          tile_width,
                                           guint          tile_height,
                                           const gchar   *name,
                                           GError       **error);

/**
 * lrg_editor_tileset_editor_install:
 * @app: the editor application
 *
 * Registers the tileset.* IPC methods on @app's server (once). The Tiles
 * panel calls it when it is created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tileset_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_tileset_editor_get_tileset:
 * @self: an #LrgEditorTilesetEditor
 *
 * Returns: (transfer none) (nullable): the tileset parsed from the
 *   document, or %NULL while the text does not load
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_editor_tileset_editor_get_tileset (LrgEditorTilesetEditor *self);

/**
 * lrg_editor_tileset_editor_get_parse_error:
 * @self: an #LrgEditorTilesetEditor
 *
 * Returns: (nullable): the loader's message when the document does not
 *   load, else %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_tileset_editor_get_parse_error (LrgEditorTilesetEditor *self);

/**
 * lrg_editor_tileset_editor_get_selected_tile:
 * @self: an #LrgEditorTilesetEditor
 *
 * Returns: the selected tile index (0-based), or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_tileset_editor_get_selected_tile (LrgEditorTilesetEditor *self);

/**
 * lrg_editor_tileset_editor_set_selected_tile:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index, or -1
 *
 * Selects a tile of the sheet (its properties show on the right).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tileset_editor_set_selected_tile (LrgEditorTilesetEditor *self,
                                                  gint                    index);

/**
 * lrg_editor_tileset_editor_tile_at:
 * @self: an #LrgEditorTilesetEditor
 * @x: canvas x
 * @y: canvas y
 *
 * Returns: the tile index of the sheet under a canvas point, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_tileset_editor_tile_at (LrgEditorTilesetEditor *self,
                                        gfloat                  x,
                                        gfloat                  y);

/**
 * lrg_editor_tileset_editor_set_layout:
 * @self: an #LrgEditorTilesetEditor
 * @name: (nullable): the new name, or %NULL to keep it
 * @image: (nullable): the image path as written in the file, or %NULL to
 *   keep it
 * @tile_width: tile width, or 0 to keep it
 * @tile_height: tile height, or 0 to keep it
 * @columns: tiles per image row, or 0 to keep it
 * @tile_count: number of tiles, or -1 to keep it
 * @error: return location for a #GError
 *
 * Changes the tileset's layout as one undo step. Changing the tile size,
 * columns or count rebuilds the grid; per-tile properties, rule tiles
 * and unknown keys survive (tiles past the new count are dropped).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_layout (LrgEditorTilesetEditor  *self,
                                               const gchar             *name,
                                               const gchar             *image,
                                               guint                    tile_width,
                                               guint                    tile_height,
                                               guint                    columns,
                                               gint                     tile_count,
                                               GError                 **error);

/**
 * lrg_editor_tileset_editor_set_spacing:
 * @self: an #LrgEditorTilesetEditor
 * @margin: pixels before the first tile
 * @spacing: pixels between tiles
 * @offset_x: draw offset x of every tile
 * @offset_y: draw offset y of every tile
 * @error: return location for a #GError
 *
 * Sets margin, spacing and draw offset as one undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_spacing (LrgEditorTilesetEditor  *self,
                                                guint                    margin,
                                                guint                    spacing,
                                                gint                     offset_x,
                                                gint                     offset_y,
                                                GError                 **error);

/* --- per-tile properties --- */

/**
 * lrg_editor_tileset_editor_set_tile_flags:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @flags: the tile's #LrgTileProperty flags
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_tile_flags (LrgEditorTilesetEditor  *self,
                                                   guint                    index,
                                                   LrgTileProperty          flags,
                                                   GError                 **error);

/**
 * lrg_editor_tileset_editor_set_tile_data:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @key: the data key
 * @value: (nullable): the value, or %NULL to remove the key
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_tile_data (LrgEditorTilesetEditor  *self,
                                                  guint                    index,
                                                  const gchar             *key,
                                                  const gchar             *value,
                                                  GError                 **error);

/**
 * lrg_editor_tileset_editor_set_tile_animation:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @frames: (array length=n_frames) (nullable): tile indices of the frames
 * @durations_ms: (array length=n_frames) (nullable): frame durations
 * @n_frames: the number of frames (0 removes the animation)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_tile_animation (LrgEditorTilesetEditor  *self,
                                                       guint                    index,
                                                       const guint             *frames,
                                                       const guint             *durations_ms,
                                                       guint                    n_frames,
                                                       GError                 **error);

/**
 * lrg_editor_tileset_editor_add_collision:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @coords: (array length=n_coords): x, y pairs in tile pixels
 * @n_coords: number of floats (even, at least 6)
 * @error: return location for a #GError
 *
 * Adds a collision polygon to a tile.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_add_collision (LrgEditorTilesetEditor  *self,
                                                  guint                    index,
                                                  const gfloat            *coords,
                                                  guint                    n_coords,
                                                  GError                 **error);

/**
 * lrg_editor_tileset_editor_remove_collision:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @polygon: a polygon index, or -1 to remove every polygon
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_remove_collision (LrgEditorTilesetEditor  *self,
                                                     guint                    index,
                                                     gint                     polygon,
                                                     GError                 **error);

/**
 * lrg_editor_tileset_editor_move_collision_point:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @polygon: a polygon index
 * @point: a point index of the polygon
 * @x: the new x in tile pixels
 * @y: the new y in tile pixels
 * @gesture: 0, or a gesture id from lrg_editor_data_editor_new_gesture():
 *   moves with the same id merge into one undo step (a drag)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_move_collision_point (LrgEditorTilesetEditor  *self,
                                                         guint                    index,
                                                         guint                    polygon,
                                                         guint                    point,
                                                         gfloat                   x,
                                                         gfloat                   y,
                                                         guint                    gesture,
                                                         GError                 **error);

/**
 * lrg_editor_tileset_editor_insert_collision_point:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @polygon: a polygon index
 * @point: the point index the new point takes (the rest move up)
 * @x: x in tile pixels
 * @y: y in tile pixels
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_insert_collision_point (LrgEditorTilesetEditor  *self,
                                                           guint                    index,
                                                           guint                    polygon,
                                                           guint                    point,
                                                           gfloat                   x,
                                                           gfloat                   y,
                                                           GError                 **error);

/**
 * lrg_editor_tileset_editor_remove_collision_point:
 * @self: an #LrgEditorTilesetEditor
 * @index: a tile index
 * @polygon: a polygon index
 * @point: a point index; a triangle loses the whole polygon
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_remove_collision_point (LrgEditorTilesetEditor  *self,
                                                           guint                    index,
                                                           guint                    polygon,
                                                           guint                    point,
                                                           GError                 **error);

/* --- rule tiles --- */

/**
 * lrg_editor_tileset_editor_get_selected_rule_tile:
 * @self: an #LrgEditorTilesetEditor
 *
 * Returns: (nullable): the name of the rule tile shown in the rules
 *   section, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_tileset_editor_get_selected_rule_tile (LrgEditorTilesetEditor *self);

/**
 * lrg_editor_tileset_editor_select_rule_tile:
 * @self: an #LrgEditorTilesetEditor
 * @name: (nullable): a rule tile name, or %NULL
 *
 * Returns: %FALSE when there is no such rule tile
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_select_rule_tile (LrgEditorTilesetEditor *self,
                                                     const gchar            *name);

/**
 * lrg_editor_tileset_editor_get_selected_rule:
 * @self: an #LrgEditorTilesetEditor
 *
 * Returns: the selected rule's index in the selected rule tile, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_tileset_editor_get_selected_rule (LrgEditorTilesetEditor *self);

/**
 * lrg_editor_tileset_editor_select_rule:
 * @self: an #LrgEditorTilesetEditor
 * @index: a rule index, or -1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tileset_editor_select_rule (LrgEditorTilesetEditor *self,
                                            gint                    index);

/**
 * lrg_editor_tileset_editor_add_rule_tile:
 * @self: an #LrgEditorTilesetEditor
 * @name: a new, unique rule tile name
 * @preset: (nullable): "edges16", "blob47" or %NULL for an empty rule tile
 * @first_index: the tile index of the preset's first tile
 * @error: return location for a #GError
 *
 * Adds a rule tile (lrg_rule_tile_new_edges16(),
 * lrg_rule_tile_new_blob47() or lrg_rule_tile_new()) and selects it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_add_rule_tile (LrgEditorTilesetEditor  *self,
                                                  const gchar             *name,
                                                  const gchar             *preset,
                                                  guint                    first_index,
                                                  GError                 **error);

/**
 * lrg_editor_tileset_editor_remove_rule_tile:
 * @self: an #LrgEditorTilesetEditor
 * @name: a rule tile name
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_remove_rule_tile (LrgEditorTilesetEditor  *self,
                                                     const gchar             *name,
                                                     GError                 **error);

/**
 * lrg_editor_tileset_editor_set_rule_tile:
 * @self: an #LrgEditorTilesetEditor
 * @name: a rule tile name
 * @new_name: (nullable): a new name, or %NULL to keep it
 * @default_tile: the default cell value (1-based id with transform bits,
 *   0 for "first output"), or -1 to keep it
 * @seed: the variant seed, or -1 to keep it
 * @border_is_this: 1 or 0, or -1 to keep it
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_rule_tile (LrgEditorTilesetEditor  *self,
                                                  const gchar             *name,
                                                  const gchar             *new_name,
                                                  gint64                   default_tile,
                                                  gint64                   seed,
                                                  gint                     border_is_this,
                                                  GError                 **error);

/**
 * lrg_editor_tileset_editor_add_rule:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: the position, or -1 to append
 * @output: the first output cell value (1-based id with transform bits),
 *   or 0 for the selected sheet tile
 * @error: return location for a #GError
 *
 * Adds a rule matching anything with one output, and selects it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_add_rule (LrgEditorTilesetEditor  *self,
                                             const gchar             *rule_tile,
                                             gint                     index,
                                             guint                    output,
                                             GError                 **error);

/**
 * lrg_editor_tileset_editor_remove_rule:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_remove_rule (LrgEditorTilesetEditor  *self,
                                                const gchar             *rule_tile,
                                                guint                    index,
                                                GError                 **error);

/**
 * lrg_editor_tileset_editor_move_rule:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @from: the rule's index
 * @to: its new index
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_move_rule (LrgEditorTilesetEditor  *self,
                                              const gchar             *rule_tile,
                                              guint                    from,
                                              guint                    to,
                                              GError                 **error);

/**
 * lrg_editor_tileset_editor_set_rule_neighbor:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @direction: one or more #LrgTileNeighbor bits
 * @match: what those neighbours must be
 * @error: return location for a #GError
 *
 * Sets cells of a rule's 3x3 pattern (what clicking the mask does).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_rule_neighbor (LrgEditorTilesetEditor  *self,
                                                      const gchar             *rule_tile,
                                                      guint                    index,
                                                      LrgTileNeighbor          direction,
                                                      LrgTileRuleMatch         match,
                                                      GError                 **error);

/**
 * lrg_editor_tileset_editor_cycle_rule_neighbor:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @direction: one #LrgTileNeighbor bit
 * @backwards: cycle the other way (right click)
 * @error: return location for a #GError
 *
 * Cycles one pattern cell: any -> this -> not this -> any.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_cycle_rule_neighbor (LrgEditorTilesetEditor  *self,
                                                        const gchar             *rule_tile,
                                                        guint                    index,
                                                        LrgTileNeighbor          direction,
                                                        gboolean                 backwards,
                                                        GError                 **error);

/**
 * lrg_editor_tileset_editor_set_rule_transform:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @transform: the variants the rule also matches
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_rule_transform (LrgEditorTilesetEditor  *self,
                                                       const gchar             *rule_tile,
                                                       guint                    index,
                                                       LrgTileRuleTransform     transform,
                                                       GError                 **error);

/**
 * lrg_editor_tileset_editor_add_rule_output:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @value: a cell value (1-based id with transform bits)
 * @weight: its weight (> 0)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_add_rule_output (LrgEditorTilesetEditor  *self,
                                                    const gchar             *rule_tile,
                                                    guint                    index,
                                                    guint                    value,
                                                    gfloat                   weight,
                                                    GError                 **error);

/**
 * lrg_editor_tileset_editor_remove_rule_output:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @output: an output index (a rule keeps at least one output)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_remove_rule_output (LrgEditorTilesetEditor  *self,
                                                       const gchar             *rule_tile,
                                                       guint                    index,
                                                       guint                    output,
                                                       GError                 **error);

/**
 * lrg_editor_tileset_editor_set_rule_output_weight:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @index: a rule index
 * @output: an output index
 * @weight: the new weight (> 0)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tileset_editor_set_rule_output_weight (LrgEditorTilesetEditor  *self,
                                                           const gchar             *rule_tile,
                                                           guint                    index,
                                                           guint                    output,
                                                           gfloat                   weight,
                                                           GError                 **error);

/**
 * lrg_editor_tileset_editor_preview:
 * @self: an #LrgEditorTilesetEditor
 * @rule_tile: a rule tile name
 * @cells: (array length=n_values): x, y pairs painted with the rule tile
 * @n_values: number of integers in @cells
 * @width: preview width in cells
 * @height: preview height in cells
 *
 * Resolves a scratch map of @width x @height with the rule tile painted
 * at @cells (square neighbours), without touching the document; the
 * preview grid of the rules section shows the same.
 *
 * Returns: (transfer full) (array zero-terminated=0) (nullable): the
 *   @width x @height cell values, row-major; %NULL when the rule tile
 *   does not exist
 */
LRG_AVAILABLE_IN_ALL
guint * lrg_editor_tileset_editor_preview (LrgEditorTilesetEditor *self,
                                           const gchar            *rule_tile,
                                           const gint             *cells,
                                           gsize                   n_values,
                                           guint                   width,
                                           guint                   height);

G_END_DECLS
