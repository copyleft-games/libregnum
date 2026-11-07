/* lrg-editor-bt-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBtEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-bt-editor.h"

struct _LrgEditorBtEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorBtEditor, lrg_editor_bt_editor, G_TYPE_OBJECT)

static void
lrg_editor_bt_editor_class_init (LrgEditorBtEditorClass *klass)
{
}

static void
lrg_editor_bt_editor_init (LrgEditorBtEditor *self)
{
}

LrgEditorBtEditor *
lrg_editor_bt_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_BT_EDITOR, NULL);
}
