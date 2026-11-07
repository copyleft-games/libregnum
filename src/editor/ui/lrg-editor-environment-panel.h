/* lrg-editor-environment-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorEnvironmentPanel: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_ENVIRONMENT_PANEL (lrg_editor_environment_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorEnvironmentPanel, lrg_editor_environment_panel, LRG, EDITOR_ENVIRONMENT_PANEL, GObject)

/**
 * lrg_editor_environment_panel_new:
 *
 * Returns: (transfer full): a new #LrgEditorEnvironmentPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorEnvironmentPanel * lrg_editor_environment_panel_new (void);

G_END_DECLS
