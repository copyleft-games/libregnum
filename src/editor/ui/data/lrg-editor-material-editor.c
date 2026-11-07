/* lrg-editor-material-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMaterialEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-material-editor.h"

struct _LrgEditorMaterialEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorMaterialEditor, lrg_editor_material_editor, G_TYPE_OBJECT)

static void
lrg_editor_material_editor_class_init (LrgEditorMaterialEditorClass *klass)
{
}

static void
lrg_editor_material_editor_init (LrgEditorMaterialEditor *self)
{
}

LrgEditorMaterialEditor *
lrg_editor_material_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_MATERIAL_EDITOR, NULL);
}
