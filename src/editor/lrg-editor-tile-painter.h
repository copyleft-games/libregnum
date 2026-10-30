/* lrg-editor-tile-painter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tile painting tools for tilemap nodes.
 *
 * #LrgEditorTilePainter holds the painting state (current tile, layer,
 * brush size) and turns tool gestures into undoable editor commands:
 *
 *  - strokes (brush / eraser): begin_stroke(), stroke_to() per pointer
 *    sample, end_stroke(); the whole stroke is one undo group and samples
 *    are joined with a line so fast drags leave no gaps
 *  - rectangles: paint_rect()
 *  - flood fill: fill() (4-connected, same id as the seed)
 *  - eyedropper: pick()
 *
 * The viewport, the IPC methods and the tests all drive the same object,
 * so the logic is identical whether a human or a script paints.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_TILE_PAINTER (lrg_editor_tile_painter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTilePainter, lrg_editor_tile_painter, LRG, EDITOR_TILE_PAINTER, GObject)

/**
 * LRG_EDITOR_TILEMAP_DEFAULT_SIZE:
 *
 * Width and height (cells) of the grid created for a tilemap node that
 * is painted before it has one.
 */
#define LRG_EDITOR_TILEMAP_DEFAULT_SIZE (16)

/**
 * lrg_editor_tile_painter_new:
 * @editor: the editor whose history records the edits
 *
 * Returns: (transfer full): a new painter (tile 1, layer 0, brush 1)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilePainter * lrg_editor_tile_painter_new (LrgEditor *editor);

/**
 * lrg_editor_tile_painter_get_tile:
 * @self: an #LrgEditorTilePainter
 *
 * Returns: the tile id painted by the brush, rectangle and fill tools
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_get_tile (LrgEditorTilePainter *self);

/**
 * lrg_editor_tile_painter_set_tile:
 * @self: an #LrgEditorTilePainter
 * @tile: a tile id (0 paints empty, i.e. erases)
 *
 * Sets the current tile.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tile_painter_set_tile (LrgEditorTilePainter *self,
                                       guint                 tile);

/**
 * lrg_editor_tile_painter_get_layer:
 * @self: an #LrgEditorTilePainter
 *
 * Returns: the layer edits go to (clamped to the node's layers on use)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_get_layer (LrgEditorTilePainter *self);

/**
 * lrg_editor_tile_painter_set_layer:
 * @self: an #LrgEditorTilePainter
 * @layer: a layer index
 *
 * Sets the layer edits go to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tile_painter_set_layer (LrgEditorTilePainter *self,
                                        guint                 layer);

/**
 * lrg_editor_tile_painter_get_brush_size:
 * @self: an #LrgEditorTilePainter
 *
 * Returns: the square brush size in cells
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_get_brush_size (LrgEditorTilePainter *self);

/**
 * lrg_editor_tile_painter_set_brush_size:
 * @self: an #LrgEditorTilePainter
 * @size: brush size in cells (1 .. 64)
 *
 * Sets the square brush size; even sizes extend right/down of the cell.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tile_painter_set_brush_size (LrgEditorTilePainter *self,
                                             guint                 size);

/**
 * lrg_editor_tile_painter_ensure_tilemap:
 * @self: an #LrgEditorTilePainter
 * @node: a node with a %LRG_NODE_VISUAL_TILEMAP visual
 *
 * Returns the node's tile grid, first giving it an empty
 * %LRG_EDITOR_TILEMAP_DEFAULT_SIZE grid (one undoable step) if it has
 * none.
 *
 * Returns: (transfer none) (nullable): the grid, or %NULL if @node is not
 *   a tilemap node
 */
LRG_AVAILABLE_IN_ALL
LrgNodeTilemap * lrg_editor_tile_painter_ensure_tilemap (LrgEditorTilePainter *self,
                                                         LrgNode              *node);

/**
 * lrg_editor_tile_painter_begin_stroke:
 * @self: an #LrgEditorTilePainter
 * @node: the tilemap node to paint
 * @erase: %TRUE to erase (paint empty) instead of the current tile
 *
 * Starts a stroke; everything until lrg_editor_tile_painter_end_stroke()
 * is one undo step.
 *
 * Returns: %FALSE if @node is not a tilemap node or a stroke is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tile_painter_begin_stroke (LrgEditorTilePainter *self,
                                               LrgNode              *node,
                                               gboolean              erase);

/**
 * lrg_editor_tile_painter_stroke_to:
 * @self: an #LrgEditorTilePainter
 * @x: cell column under the pointer
 * @y: cell row under the pointer
 *
 * Stamps the brush along the line from the previous sample (or just at
 * (@x, @y) for the first sample). Cells outside the grid are skipped.
 *
 * Returns: the number of cells changed by this sample
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_stroke_to (LrgEditorTilePainter *self,
                                         gint                  x,
                                         gint                  y);

/**
 * lrg_editor_tile_painter_end_stroke:
 * @self: an #LrgEditorTilePainter
 *
 * Ends the stroke and records it.
 *
 * Returns: the number of cells the stroke changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_end_stroke (LrgEditorTilePainter *self);

/**
 * lrg_editor_tile_painter_is_stroking:
 * @self: an #LrgEditorTilePainter
 *
 * Returns: whether a stroke is in progress
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tile_painter_is_stroking (LrgEditorTilePainter *self);

/**
 * lrg_editor_tile_painter_paint_cells:
 * @self: an #LrgEditorTilePainter
 * @node: the tilemap node
 * @cells: (array length=n_values): x, y pairs
 * @n_values: number of integers in @cells (twice the cell count)
 * @erase: %TRUE to erase instead of the current tile
 *
 * Sets a list of cells as one undo step (no brush footprint).
 *
 * Returns: the number of cells changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_paint_cells (LrgEditorTilePainter *self,
                                           LrgNode              *node,
                                           const gint           *cells,
                                           gsize                 n_values,
                                           gboolean              erase);

/**
 * lrg_editor_tile_painter_paint_rect:
 * @self: an #LrgEditorTilePainter
 * @node: the tilemap node
 * @x0: one corner column
 * @y0: one corner row
 * @x1: the opposite corner column (inclusive)
 * @y1: the opposite corner row (inclusive)
 * @erase: %TRUE to erase instead of the current tile
 *
 * Fills a rectangle (clipped to the grid) as one undo step.
 *
 * Returns: the number of cells changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_paint_rect (LrgEditorTilePainter *self,
                                          LrgNode              *node,
                                          gint                  x0,
                                          gint                  y0,
                                          gint                  x1,
                                          gint                  y1,
                                          gboolean              erase);

/**
 * lrg_editor_tile_painter_fill:
 * @self: an #LrgEditorTilePainter
 * @node: the tilemap node
 * @x: seed column
 * @y: seed row
 * @erase: %TRUE to erase instead of the current tile
 *
 * Flood-fills the 4-connected region holding the seed's tile id as one
 * undo step.
 *
 * Returns: the number of cells changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_fill (LrgEditorTilePainter *self,
                                    LrgNode              *node,
                                    gint                  x,
                                    gint                  y,
                                    gboolean              erase);

/**
 * lrg_editor_tile_painter_pick:
 * @self: an #LrgEditorTilePainter
 * @node: the tilemap node
 * @x: cell column
 * @y: cell row
 *
 * Eyedropper: makes the tile at (@x, @y) on the current layer the current
 * tile (an empty cell leaves it unchanged).
 *
 * Returns: the tile id at the cell (0 if empty or outside)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_tile_painter_pick (LrgEditorTilePainter *self,
                                    LrgNode              *node,
                                    gint                  x,
                                    gint                  y);

/**
 * lrg_editor_tile_painter_cell_at:
 * @node: a tilemap node with a grid
 * @world_point: (array fixed-size=3): a world-space point
 * @out_x: (out): cell column (may be outside the grid)
 * @out_y: (out): cell row (may be outside the grid)
 *
 * Maps a world point (e.g. where a viewport ray meets the node's plane)
 * to a cell, honouring the node's world transform and "tile_size" param.
 *
 * Returns: whether the cell is inside the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tile_painter_cell_at (LrgNode      *node,
                                          const gfloat *world_point,
                                          gint         *out_x,
                                          gint         *out_y);

/**
 * lrg_editor_tile_painter_get_tile_size:
 * @node: a tilemap node
 *
 * Returns: the world size of one cell (the visual's "tile_size" param,
 *   1.0 by default)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_tile_painter_get_tile_size (LrgNode *node);

G_END_DECLS
