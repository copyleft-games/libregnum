/* lrg-editor-data-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDataEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-data-editor.h"

struct _LrgEditorDataEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorDataEditor, lrg_editor_data_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_data_editor_class_init (LrgEditorDataEditorClass *klass)
{
}

static void
lrg_editor_data_editor_init (LrgEditorDataEditor *self)
{
}

LrgEditorDataEditor *
lrg_editor_data_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_DATA_EDITOR, NULL);
}
