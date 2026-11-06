/* lrg-editor-tiles-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTilesPanel: the tile palette and layers of the selected tilemap (Wave 4).
 *
 * Since 0.8.1 (Wave 3) this is a placeholder: a muted "Tiles (Wave 4)"
 * label. The constructor signature is final.
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

#define LRG_TYPE_EDITOR_TILES_PANEL (lrg_editor_tiles_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTilesPanel, lrg_editor_tiles_panel, LRG, EDITOR_TILES_PANEL, LrgContainer)

/**
 * lrg_editor_tiles_panel_new:
 * @app: the editor application it presents
 *
 * Returns: (transfer full): a new #LrgEditorTilesPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTilesPanel * lrg_editor_tiles_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_tiles_panel_get_app:
 * @self: an #LrgEditorTilesPanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_tiles_panel_get_app (LrgEditorTilesPanel *self);

G_END_DECLS
