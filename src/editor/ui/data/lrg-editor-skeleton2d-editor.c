/* lrg-editor-skeleton2d-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSkeleton2DEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-skeleton2d-editor.h"

struct _LrgEditorSkeleton2DEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorSkeleton2DEditor, lrg_editor_skeleton2d_editor, G_TYPE_OBJECT)

static void
lrg_editor_skeleton2d_editor_class_init (LrgEditorSkeleton2DEditorClass *klass)
{
}

static void
lrg_editor_skeleton2d_editor_init (LrgEditorSkeleton2DEditor *self)
{
}

LrgEditorSkeleton2DEditor *
lrg_editor_skeleton2d_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SKELETON2D_EDITOR, NULL);
}
