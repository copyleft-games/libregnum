/* lrg-editor-bt-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBtEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_BT_EDITOR (lrg_editor_bt_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorBtEditor, lrg_editor_bt_editor, LRG, EDITOR_BT_EDITOR, GObject)

/**
 * lrg_editor_bt_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorBtEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBtEditor * lrg_editor_bt_editor_new (void);

G_END_DECLS
