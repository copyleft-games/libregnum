/* lrg-editor-ui-layout-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorUiLayoutEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR (lrg_editor_ui_layout_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorUiLayoutEditor, lrg_editor_ui_layout_editor, LRG, EDITOR_UI_LAYOUT_EDITOR, LrgContainer)

/**
 * lrg_editor_ui_layout_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorUiLayoutEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorUiLayoutEditor * lrg_editor_ui_layout_editor_new (void);

G_END_DECLS
