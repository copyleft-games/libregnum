/* lrg-editor-fsm-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFsmEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-fsm-editor.h"

struct _LrgEditorFsmEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorFsmEditor, lrg_editor_fsm_editor, G_TYPE_OBJECT)

static void
lrg_editor_fsm_editor_class_init (LrgEditorFsmEditorClass *klass)
{
}

static void
lrg_editor_fsm_editor_init (LrgEditorFsmEditor *self)
{
}

LrgEditorFsmEditor *
lrg_editor_fsm_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_FSM_EDITOR, NULL);
}
