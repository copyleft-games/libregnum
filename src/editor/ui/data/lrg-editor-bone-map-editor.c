/* lrg-editor-bone-map-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBoneMapEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-bone-map-editor.h"

struct _LrgEditorBoneMapEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorBoneMapEditor, lrg_editor_bone_map_editor, G_TYPE_OBJECT)

static void
lrg_editor_bone_map_editor_class_init (LrgEditorBoneMapEditorClass *klass)
{
}

static void
lrg_editor_bone_map_editor_init (LrgEditorBoneMapEditor *self)
{
}

LrgEditorBoneMapEditor *
lrg_editor_bone_map_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_BONE_MAP_EDITOR, NULL);
}
