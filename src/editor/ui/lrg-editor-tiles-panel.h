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
 * eraser, rectangle, fill, eyedropper), the brush size and current tile,
 * the grid size and tile slicing, the tileset (drop an image from the
 * Assets panel, payload "application/x-lrg-asset"), the layers (add,
 * remove, rename, show / hide, pick the current one) and the palette:
 * the tileset image sliced into its tiles, or numbered colour swatches
 * without an image. Painting itself happens in the viewport; the panel
 * and the viewport share the view state's tile-tool / tile-mode and the
 * app's #LrgEditorTilePainter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_TILES_ASSET_TYPE:
 *
 * The drag content type the tileset drop zone takes: a string payload
 * holding an image path (absolute or relative to the project).
 */
#define LRG_EDITOR_TILES_ASSET_TYPE "application/x-lrg-asset"

#define LRG_TYPE_EDITOR_TILES_PANEL (lrg_editor_tiles_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTilesPanel, lrg_editor_tiles_panel, LRG, EDITOR_TILES_PANEL, LrgContainer)

/**
 * lrg_editor_tiles_panel_new:
 * @app: the editor application it presents
 *
 * Creates the Tiles panel. It registers the "tilemap.rename-layer"
 * action (rename the current layer to the text in the layer name
 * field).
 *
 * Returns: (transfer full): a new #LrgEditorTilesPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesPanel * lrg_editor_tiles_panel_new (LrgEditorApp *app);

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
 * @path: an image path, absolute or relative to the project
 *
 * Sets the target's tileset as one undo step (what dropping an image
 * on the tileset zone does). Paths inside the project are stored
 * relative to it.
 *
 * Returns: %TRUE when there was a target to set it on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tiles_panel_set_tileset (LrgEditorTilesPanel *self,
                                             const gchar         *path);

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
 * The palette grid: the tileset image's size divided by the grid's tile
 * size, or 8 x 4 numbered swatches when there is no image.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_tiles_panel_get_palette_size (LrgEditorTilesPanel *self,
                                              guint               *columns,
                                              guint               *rows);

G_END_DECLS
