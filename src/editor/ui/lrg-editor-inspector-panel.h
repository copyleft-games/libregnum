/* lrg-editor-inspector-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInspectorPanel: the properties of the selected nodes (Wave 4: property grids per section, prefab bar).
 *
 * Since 0.8.1 (Wave 3) this is a placeholder: a muted "Inspector (Wave 4)"
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

#define LRG_TYPE_EDITOR_INSPECTOR_PANEL (lrg_editor_inspector_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorInspectorPanel, lrg_editor_inspector_panel, LRG, EDITOR_INSPECTOR_PANEL, LrgContainer)

/**
 * lrg_editor_inspector_panel_new:
 * @app: the editor application it presents
 *
 * Returns: (transfer full): a new #LrgEditorInspectorPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorInspectorPanel * lrg_editor_inspector_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_inspector_panel_get_app:
 * @self: an #LrgEditorInspectorPanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_inspector_panel_get_app (LrgEditorInspectorPanel *self);

G_END_DECLS
