/* lrg-editor-tiles-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTilesPanel: tilemap painting settings for the selected
 * tilemap node.
 *
 * The panel shows the paint-mode toggle (B), the five tools (brush,
 * eraser, rectangle, fill, eyedropper), the brush size and current tile
 * with its flip / rotation (X, Shift+X, Z while painting), the grid (size, tile
 * slicing and, since 0.9.0, the orientation: orthogonal, isometric,
 * staggered or hexagonal with its cell size), the tileset (drop an image
 * or an `.rtileset` from the Assets panel, payload
 * "application/x-lrg-asset"), the layers (add, remove, rename, show /
 * hide, pick the current one), the rule tiles of an `.rtileset` (pick one
 * to paint with autotiling, "Refresh all" to re-resolve the map), the
 * palette (the tileset image sliced into its tiles, or numbered colour
 * swatches without an image) and the properties of the tile under the
 * pointer in the palette (flags, collision, custom data, animation).
 * Painting itself happens in the viewport; the panel and the viewport
 * share the view state's tile-tool / tile-mode and the app's
 * #LrgEditorTilePainter.
 *
 * The panel also owns the iso/hex-aware tilemap helpers the viewport, the
 * scene renderer and the gizmo painter call (cell picking, cell outlines,
 * drawing a tilemap node with transform bits and `.rtileset` images, the
 * rule tile gesture around a painter gesture).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-theme.h"
#include "../../paint/lrg-paint-list.h"
#include "../../tilemap/lrg-tileset.h"
#include "../../tilemap/lrg-tilemap.h"
#include "../../world3d/lrg-world3d-renderer.h"
#include "../lrg-editor-math.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_TILES_ASSET_TYPE:
 *
 * The drag content type the tileset drop zone takes: a string payload
 * holding an image or `.rtileset` path (absolute or relative to the
 * project).
 */
#define LRG_EDITOR_TILES_ASSET_TYPE "application/x-lrg-asset"

#define LRG_TYPE_EDITOR_TILES_PANEL (lrg_editor_tiles_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTilesPanel, lrg_editor_tiles_panel, LRG, EDITOR_TILES_PANEL, LrgContainer)

/**
 * lrg_editor_tiles_panel_new:
 * @app: the editor application it presents
 *
 * Creates the Tiles panel. It registers the "tilemap.rename-layer",
 * "tilemap.flip-x" (X), "tilemap.flip-y" (Shift+X), "tilemap.rotate" (Z),
 * "tilemap.rotate-back" (Shift+Z), "tilemap.reset-transform" and
 * "tilemap.refresh-rules" actions, the tiles.* IPC methods, and installs
 * the tileset.* and sprite.* methods of the `.rtileset` and `.rsprite`
 * editors.
 *
 * Returns: (transfer full): a new #LrgEditorTilesPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesPanel * lrg_editor_tiles_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_tiles_panel_find:
 * @app: the editor application
 *
 * Returns: (transfer none) (nullable): the app's Tiles panel, or %NULL
 *   when it was not created
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesPanel * lrg_editor_tiles_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_tiles_panel_get_target:
 * @self: an #LrgEditorTilesPanel
 *
 * Returns: (transfer none) (nullable): the tilemap node the panel edits
 *   (the primary selection when it is a tilemap), or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_tiles_panel_get_target (LrgEditorTilesPanel *self);

/**
 * lrg_editor_tiles_panel_set_tileset:
 * @self: an #LrgEditorTilesPanel
 * @path: an image or `.rtileset` path, absolute or relative to the project
 *
 * Sets the target's tileset as one undo step (what dropping a file on
 * the tileset zone does). Paths inside the project are stored relative
 * to it. An `.rtileset` also sets the grid's tile size to the tileset's.
 *
 * Returns: %TRUE when there was a target to set it on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_set_tileset (LrgEditorTilesPanel *self,
                                             const gchar         *path);

/**
 * lrg_editor_tiles_panel_get_tileset:
 * @self: an #LrgEditorTilesPanel
 *
 * The target's tileset asset when its tileset is an `.rtileset` file
 * (loaded without a texture, reloaded when the file changes).
 *
 * Returns: (transfer none) (nullable): the tileset, or %NULL for an image
 *   tileset or no target
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_editor_tiles_panel_get_tileset (LrgEditorTilesPanel *self);

/**
 * lrg_editor_tiles_panel_rename_layer:
 * @self: an #LrgEditorTilesPanel
 * @layer: a layer index of the target's grid
 * @name: the new name
 *
 * Renames a layer as one undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_rename_layer (LrgEditorTilesPanel *self,
                                              guint                layer,
                                              const gchar         *name);

/**
 * lrg_editor_tiles_panel_set_layer_visible:
 * @self: an #LrgEditorTilesPanel
 * @layer: a layer index of the target's grid
 * @visible: show or hide it
 *
 * Shows or hides a layer as one undo step (the eye toggle).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_set_layer_visible (LrgEditorTilesPanel *self,
                                                   guint                layer,
                                                   gboolean             visible);

/**
 * lrg_editor_tiles_panel_get_palette_size:
 * @self: an #LrgEditorTilesPanel
 * @columns: (out) (optional): palette columns
 * @rows: (out) (optional): palette rows
 *
 * The palette grid: the `.rtileset`'s columns and rows, the tileset
 * image's size divided by the grid's tile size, or 8 x 4 numbered
 * swatches when there is no image.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_get_palette_size (LrgEditorTilesPanel *self,
                                              guint               *columns,
                                              guint               *rows);

/**
 * lrg_editor_tiles_panel_set_grid:
 * @self: an #LrgEditorTilesPanel
 * @orientation: the grid orientation
 * @stagger_axis: stagger axis of staggered and hexagonal grids
 * @stagger_index: which rows / columns are shifted
 * @hex_side_length: hexagon side length in pixels (0: automatic)
 * @cell_width: cell width in pixels (0: the tile width)
 * @cell_height: cell height in pixels (0: the tile height)
 *
 * Sets the target grid's layout (lrg_node_tilemap_set_grid()) as one
 * undo step. Picking, the grid overlay, rule tile neighbours and the
 * drawing in the viewport follow it.
 *
 * Returns: %TRUE when there was a grid to change
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_set_grid (LrgEditorTilesPanel    *self,
                                          LrgTilemapOrientation   orientation,
                                          LrgTilemapStaggerAxis   stagger_axis,
                                          LrgTilemapStaggerIndex  stagger_index,
                                          guint                   hex_side_length,
                                          guint                   cell_width,
                                          guint                   cell_height);

/* --- the brush: tile, transform, rule tile --- */

/**
 * lrg_editor_tiles_panel_get_brush_value:
 * @self: an #LrgEditorTilesPanel
 *
 * Returns: the cell value the brush paints: the painter's tile id with
 *   the flip / rotation bits (see lrg_tile_make())
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tiles_panel_get_brush_value (LrgEditorTilesPanel *self);

/**
 * lrg_editor_tiles_panel_set_brush_tile:
 * @self: an #LrgEditorTilesPanel
 * @tile_id: a 1-based tile id (0 erases)
 *
 * Makes @tile_id the painted tile, keeping the brush's flip / rotation
 * and leaving rule tile painting (what clicking a palette tile does).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_set_brush_tile (LrgEditorTilesPanel *self,
                                            guint                tile_id);

/**
 * lrg_editor_tiles_panel_set_brush_transform:
 * @self: an #LrgEditorTilesPanel
 * @mirrored: mirror the tile left to right (before rotating)
 * @rotation: clockwise quarter turns, 0 to 3
 *
 * Sets the brush's transform bits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_set_brush_transform (LrgEditorTilesPanel *self,
                                                 gboolean             mirrored,
                                                 guint                rotation);

/**
 * lrg_editor_tiles_panel_flip_brush:
 * @self: an #LrgEditorTilesPanel
 * @vertical: %FALSE mirrors left to right (X), %TRUE top to bottom
 *   (Shift+X)
 *
 * Mirrors the brush on top of its current transform.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_flip_brush (LrgEditorTilesPanel *self,
                                        gboolean             vertical);

/**
 * lrg_editor_tiles_panel_rotate_brush:
 * @self: an #LrgEditorTilesPanel
 * @quarter_turns: clockwise quarter turns (negative: counter-clockwise)
 *
 * Rotates the brush on top of its current transform (Z, Shift+Z).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_rotate_brush (LrgEditorTilesPanel *self,
                                          gint                 quarter_turns);

/**
 * lrg_editor_tiles_panel_get_rule_tile:
 * @self: an #LrgEditorTilesPanel
 *
 * Returns: (nullable): the name of the rule tile the brush paints with,
 *   or %NULL when it paints plain tiles
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_tiles_panel_get_rule_tile (LrgEditorTilesPanel *self);

/**
 * lrg_editor_tiles_panel_set_rule_tile:
 * @self: an #LrgEditorTilesPanel
 * @name: (nullable): a rule tile of the target's `.rtileset`, or %NULL
 *   to paint plain tiles again
 *
 * Makes the brush paint with a rule tile: every gesture (stroke,
 * rectangle, fill, erase) re-resolves the painted cells and their
 * neighbours (autotiling), in the same undo step.
 *
 * Returns: %FALSE when @name is not a rule tile of the target's tileset
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_set_rule_tile (LrgEditorTilesPanel *self,
                                               const gchar         *name);

/**
 * lrg_editor_tiles_panel_paint_cells:
 * @self: an #LrgEditorTilesPanel
 * @cells: (array length=n_values): x, y pairs
 * @n_values: number of integers in @cells (twice the cell count)
 * @erase: %TRUE to erase instead of painting
 *
 * Paints the cells on the painter's layer with the brush (its rule tile
 * with autotiling, else its value with the transform bits) as one undo
 * step.
 *
 * Returns: the number of cells that changed (neighbours included)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tiles_panel_paint_cells (LrgEditorTilesPanel *self,
                                          const gint          *cells,
                                          gsize                n_values,
                                          gboolean             erase);

/**
 * lrg_editor_tiles_panel_refresh_rule_tiles:
 * @self: an #LrgEditorTilesPanel
 * @layer: a layer index, or -1 for every layer
 *
 * "Refresh all": re-resolves every rule-tiled cell of the target with
 * every rule tile of its `.rtileset` (after editing rules or painting
 * plain tiles of a rule tile's range), as one undo step.
 *
 * Returns: the number of cells that changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tiles_panel_refresh_rule_tiles (LrgEditorTilesPanel *self,
                                                 gint                 layer);

/**
 * lrg_editor_tiles_panel_get_info_tile:
 * @self: an #LrgEditorTilesPanel
 *
 * Returns: the 1-based tile id whose properties the panel shows (the
 *   palette tile under the pointer, else the brush tile), 0 for none
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tiles_panel_get_info_tile (LrgEditorTilesPanel *self);

/* --- iso/hex-aware helpers for the viewport, renderer and gizmos --- */

/**
 * lrg_editor_tiles_panel_get_geometry:
 * @node: a tilemap node with a grid
 * @scale: (out) (optional): world units per grid pixel
 *
 * The node grid's layout as an #LrgTilemap (one empty layer of the
 * grid's size, cached on the grid and rebuilt when its size or layout
 * changes): cell centres, outlines, picking and draw order in grid
 * pixels. A point (px, py) of it lies at (px * @scale, -py * @scale) in
 * the node's local XY plane; @scale is the "tile_size" visual parameter
 * over the cell width, so an orthogonal grid keeps one world tile per
 * cell.
 *
 * Returns: (transfer none) (nullable): the layout, or %NULL when @node
 *   has no grid
 */
LRG_AVAILABLE_IN_ALL
LrgTilemap * lrg_editor_tiles_panel_get_geometry (LrgNode *node,
                                                  gfloat  *scale);

/**
 * lrg_editor_tiles_panel_local_to_cell:
 * @node: a tilemap node with a grid
 * @local_x: x in the node's local plane
 * @local_y: y in the node's local plane
 * @out_x: (out): cell column (may be outside the grid)
 * @out_y: (out): cell row (may be outside the grid)
 *
 * Picks the cell under a local point exactly for every orientation
 * (diamonds and hexagons, not their boxes).
 *
 * Returns: whether the cell is inside the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_local_to_cell (LrgNode *node,
                                               gfloat   local_x,
                                               gfloat   local_y,
                                               gint    *out_x,
                                               gint    *out_y);

/**
 * lrg_editor_tiles_panel_cell_at:
 * @node: a tilemap node with a grid
 * @world_point: (array fixed-size=3): a world-space point on the node's
 *   plane
 * @out_x: (out): cell column (may be outside the grid)
 * @out_y: (out): cell row (may be outside the grid)
 *
 * lrg_editor_tile_painter_cell_at() for every grid orientation: maps a
 * world point (where a viewport ray meets the node's plane) through the
 * node's transform to a cell.
 *
 * Returns: whether the cell is inside the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_cell_at (LrgNode      *node,
                                         const gfloat *world_point,
                                         gint         *out_x,
                                         gint         *out_y);

/**
 * lrg_editor_tiles_panel_get_cell_polygon:
 * @node: a tilemap node with a grid
 * @x: cell column
 * @y: cell row
 * @out_local: (out caller-allocates) (array fixed-size=12): x, y pairs of
 *   the cell outline in the node's local plane
 *
 * Returns: the number of points written: 4 (rectangle, diamond) or 6
 *   (hexagon), 0 when @node has no grid
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tiles_panel_get_cell_polygon (LrgNode *node,
                                               gint     x,
                                               gint     y,
                                               gfloat  *out_local);

/**
 * lrg_editor_tiles_panel_draw_tilemap:
 * @renderer: the runtime renderer that resolves and caches textures
 * @node: a tilemap node with a grid
 * @world: (array fixed-size=16): the node's world matrix
 * @show_grid: draw the cell outlines (the node is being painted)
 *
 * Draws a tilemap node in its local XY plane with raw rlgl calls, as the
 * editor's scene renderer does: every orientation in the engine's draw
 * order, tiles placed like #LrgTilemap places them (bottom-left on the
 * cell box plus the draw offset), flip / rotation bits honoured, an
 * `.rtileset` tileset's image, margin, spacing and animated tiles, flat
 * swatches without an image; the bounds always and the cell outlines
 * with @show_grid. Needs a GL context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_draw_tilemap (LrgWorld3DRenderer *renderer,
                                          LrgNode            *node,
                                          const gfloat       *world,
                                          gboolean            show_grid);

/**
 * lrg_editor_tiles_panel_paint_hover:
 * @list: the paint list of the viewport overlay
 * @theme: (nullable): the theme for the accent / danger colours
 * @view: the viewport's camera
 * @node: the tilemap node being painted
 * @cell_x: the hovered column
 * @cell_y: the hovered row
 * @erase: draw the eraser colour
 * @origin_x: canvas x of the viewport's top-left corner
 * @origin_y: canvas y of the viewport's top-left corner
 * @scale: canvas pixels per viewport pixel
 *
 * Paints the hovered cell's outline for isometric, staggered and
 * hexagonal grids (lrg_editor_gizmo_paint_tile_hover() draws the
 * orthogonal square).
 *
 * Returns: %TRUE when it painted (a non-orthogonal grid), %FALSE to let
 *   the caller paint the square
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_paint_hover (LrgPaintList        *list,
                                             LrgTheme            *theme,
                                             const LrgEditorView *view,
                                             LrgNode             *node,
                                             gint                 cell_x,
                                             gint                 cell_y,
                                             gboolean             erase,
                                             gfloat               origin_x,
                                             gfloat               origin_y,
                                             gfloat               scale);

/**
 * lrg_editor_tiles_panel_begin_gesture:
 * @app: the editor application
 * @node: the tilemap node a viewport tile tool starts on
 *
 * Called by the viewport before a tile tool runs (press). With a rule
 * tile brush it opens an undo group around the painter's edits and
 * points the painter at the rule tile's default tile; without one it
 * does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_begin_gesture (LrgEditorApp *app,
                                           LrgNode      *node);

/**
 * lrg_editor_tiles_panel_update_gesture:
 * @app: the editor application
 *
 * Called by the viewport after each painter step of a gesture (a stroke
 * sample): autotiles the cells the painter changed since the last call
 * and their neighbours, inside the gesture's undo group.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_update_gesture (LrgEditorApp *app);

/**
 * lrg_editor_tiles_panel_end_gesture:
 * @app: the editor application
 *
 * Called by the viewport when a tile tool finished (release, or right
 * after an instant tool): a last lrg_editor_tiles_panel_update_gesture()
 * and the end of the undo group. Safe to call without a gesture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_end_gesture (LrgEditorApp *app);

G_END_DECLS
