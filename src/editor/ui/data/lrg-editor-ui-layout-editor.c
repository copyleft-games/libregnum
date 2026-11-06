/* lrg-editor-ui-layout-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorUiLayoutEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-ui-layout-editor.h"

struct _LrgEditorUiLayoutEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorUiLayoutEditor, lrg_editor_ui_layout_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_ui_layout_editor_class_init (LrgEditorUiLayoutEditorClass *klass)
{
}

static void
lrg_editor_ui_layout_editor_init (LrgEditorUiLayoutEditor *self)
{
}

LrgEditorUiLayoutEditor *
lrg_editor_ui_layout_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_UI_LAYOUT_EDITOR, NULL);
}
