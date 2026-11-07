/* lrg-editor-bone-map-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBoneMapEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_BONE_MAP_EDITOR (lrg_editor_bone_map_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorBoneMapEditor, lrg_editor_bone_map_editor, LRG, EDITOR_BONE_MAP_EDITOR, GObject)

/**
 * lrg_editor_bone_map_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorBoneMapEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBoneMapEditor * lrg_editor_bone_map_editor_new (void);

G_END_DECLS
