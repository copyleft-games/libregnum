/* lrg-editor-assets-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAssetsPanel: the project's files (Wave 4: breadcrumb, list and thumbnail grid, git badges).
 *
 * Since 0.8.1 (Wave 3) this is a placeholder: a muted "Assets (Wave 4)"
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

#define LRG_TYPE_EDITOR_ASSETS_PANEL (lrg_editor_assets_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorAssetsPanel, lrg_editor_assets_panel, LRG, EDITOR_ASSETS_PANEL, LrgContainer)

/**
 * lrg_editor_assets_panel_new:
 * @app: the editor application it presents
 *
 * Returns: (transfer full): a new #LrgEditorAssetsPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorAssetsPanel * lrg_editor_assets_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_assets_panel_get_app:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_assets_panel_get_app (LrgEditorAssetsPanel *self);

G_END_DECLS
