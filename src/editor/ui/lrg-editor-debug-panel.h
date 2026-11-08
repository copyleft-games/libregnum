/* lrg-editor-debug-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDebugPanel: the docked Debug panel (call stack, variables,
 * watches, breakpoints, evaluate). Registers the debug.* actions and
 * IPC methods. The window docks it; this file does not.
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

typedef struct _LrgDapServer LrgDapServer;

#define LRG_TYPE_EDITOR_DEBUG_PANEL (lrg_editor_debug_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDebugPanel, lrg_editor_debug_panel, LRG, EDITOR_DEBUG_PANEL, LrgContainer)

/**
 * lrg_editor_debug_panel_new:
 * @app: the editor application the panel registers its actions and IPC
 *   methods on
 *
 * Creates the Debug panel and registers `debug.*` actions and IPC
 * methods, or returns another reference to the one already created
 * for @app. Does not dock itself: the application places it.
 *
 * Returns: (transfer full): the panel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDebugPanel * lrg_editor_debug_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_debug_panel_get_app:
 * @self: an #LrgEditorDebugPanel
 *
 * Returns: (transfer none) (nullable): the application, or %NULL after
 *   it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_debug_panel_get_app (LrgEditorDebugPanel *self);

/**
 * lrg_editor_debug_panel_get_dap:
 * @self: an #LrgEditorDebugPanel
 *
 * The panel's DAP server. It listens only after `debug.dap_start`.
 *
 * Returns: (transfer none) (nullable): the #LrgDapServer
 */
LRG_AVAILABLE_IN_ALL
LrgDapServer * lrg_editor_debug_panel_get_dap (LrgEditorDebugPanel *self);

G_END_DECLS
