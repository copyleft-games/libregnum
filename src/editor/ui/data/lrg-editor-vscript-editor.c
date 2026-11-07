/* lrg-editor-vscript-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorVscriptEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-vscript-editor.h"

struct _LrgEditorVscriptEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorVscriptEditor, lrg_editor_vscript_editor, G_TYPE_OBJECT)

static void
lrg_editor_vscript_editor_class_init (LrgEditorVscriptEditorClass *klass)
{
}

static void
lrg_editor_vscript_editor_init (LrgEditorVscriptEditor *self)
{
}

LrgEditorVscriptEditor *
lrg_editor_vscript_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_VSCRIPT_EDITOR, NULL);
}
