/* lrg-editor-hud-layout-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorHudLayoutEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR (lrg_editor_hud_layout_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorHudLayoutEditor, lrg_editor_hud_layout_editor, LRG, EDITOR_HUD_LAYOUT_EDITOR, LrgContainer)

/**
 * lrg_editor_hud_layout_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorHudLayoutEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorHudLayoutEditor * lrg_editor_hud_layout_editor_new (void);

G_END_DECLS
