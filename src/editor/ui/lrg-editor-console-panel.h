/* lrg-editor-console-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorConsolePanel: the editor log and command line (Wave 4: filters, history, problems).
 *
 * Since 0.8.1 (Wave 3) this is a placeholder: a muted "Console (Wave 4)"
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

#define LRG_TYPE_EDITOR_CONSOLE_PANEL (lrg_editor_console_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorConsolePanel, lrg_editor_console_panel, LRG, EDITOR_CONSOLE_PANEL, LrgContainer)

/**
 * lrg_editor_console_panel_new:
 * @app: the editor application it presents
 *
 * Returns: (transfer full): a new #LrgEditorConsolePanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorConsolePanel * lrg_editor_console_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_console_panel_get_app:
 * @self: an #LrgEditorConsolePanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_console_panel_get_app (LrgEditorConsolePanel *self);

G_END_DECLS
