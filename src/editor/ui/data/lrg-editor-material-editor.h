/* lrg-editor-material-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMaterialEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_MATERIAL_EDITOR (lrg_editor_material_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorMaterialEditor, lrg_editor_material_editor, LRG, EDITOR_MATERIAL_EDITOR, GObject)

/**
 * lrg_editor_material_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorMaterialEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMaterialEditor * lrg_editor_material_editor_new (void);

G_END_DECLS
