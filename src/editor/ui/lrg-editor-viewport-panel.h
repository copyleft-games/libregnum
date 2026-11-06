/* lrg-editor-viewport-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportPanel: the editor's scene viewport.
 *
 * A container holding an #LrgViewport in %LRG_VIEWPORT_MODE_RENDER whose
 * render function draws the level with #LrgEditorSceneRenderer. Pointer
 * and key input go to the app's #LrgEditorViewportNav first (fly, orbit,
 * pan, zoom, the orientation gizmo), then to the transform gizmo, tile
 * painting and picking: a click selects, a drag on empty space box
 * selects, a right click opens the context menu.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-viewport.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_VIEWPORT_PANEL (lrg_editor_viewport_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorViewportPanel, lrg_editor_viewport_panel, LRG, EDITOR_VIEWPORT_PANEL, LrgContainer)

/**
 * lrg_editor_viewport_panel_new:
 * @app: the application whose level, view state and navigation it shows
 *
 * Returns: (transfer full): a new #LrgEditorViewportPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportPanel * lrg_editor_viewport_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_viewport_panel_get_viewport:
 * @self: an #LrgEditorViewportPanel
 *
 * Returns: (transfer none): the viewport widget showing viewport 0
 */
LRG_AVAILABLE_IN_ALL
LrgViewport * lrg_editor_viewport_panel_get_viewport (LrgEditorViewportPanel *self);

G_END_DECLS
