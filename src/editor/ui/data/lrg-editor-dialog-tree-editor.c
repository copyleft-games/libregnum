/* lrg-editor-dialog-tree-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDialogTreeEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-dialog-tree-editor.h"

struct _LrgEditorDialogTreeEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorDialogTreeEditor, lrg_editor_dialog_tree_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_dialog_tree_editor_class_init (LrgEditorDialogTreeEditorClass *klass)
{
}

static void
lrg_editor_dialog_tree_editor_init (LrgEditorDialogTreeEditor *self)
{
}

LrgEditorDialogTreeEditor *
lrg_editor_dialog_tree_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR, NULL);
}
