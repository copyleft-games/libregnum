/* lrg-editor-skeleton2d-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSkeleton2DEditor: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SKELETON2D_EDITOR (lrg_editor_skeleton2d_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSkeleton2DEditor, lrg_editor_skeleton2d_editor, LRG, EDITOR_SKELETON2D_EDITOR, GObject)

/**
 * lrg_editor_skeleton2d_editor_new:
 *
 * Returns: (transfer full): a new #LrgEditorSkeleton2DEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSkeleton2DEditor * lrg_editor_skeleton2d_editor_new (void);

G_END_DECLS
