/* lrg-editor-blocks-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBlocksEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-blocks-editor.h"

struct _LrgEditorBlocksEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorBlocksEditor, lrg_editor_blocks_editor, G_TYPE_OBJECT)

static void
lrg_editor_blocks_editor_class_init (LrgEditorBlocksEditorClass *klass)
{
}

static void
lrg_editor_blocks_editor_init (LrgEditorBlocksEditor *self)
{
}

LrgEditorBlocksEditor *
lrg_editor_blocks_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_BLOCKS_EDITOR, NULL);
}
