/* lrg-editor-nav-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNavPanel: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_NAV_PANEL (lrg_editor_nav_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorNavPanel, lrg_editor_nav_panel, LRG, EDITOR_NAV_PANEL, GObject)

/**
 * lrg_editor_nav_panel_new:
 *
 * Returns: (transfer full): a new #LrgEditorNavPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavPanel * lrg_editor_nav_panel_new (void);

G_END_DECLS
