/* lrg-editor-post-stack-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPostStackEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-post-stack-editor.h"

struct _LrgEditorPostStackEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorPostStackEditor, lrg_editor_post_stack_editor, G_TYPE_OBJECT)

static void
lrg_editor_post_stack_editor_class_init (LrgEditorPostStackEditorClass *klass)
{
}

static void
lrg_editor_post_stack_editor_init (LrgEditorPostStackEditor *self)
{
}

LrgEditorPostStackEditor *
lrg_editor_post_stack_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_POST_STACK_EDITOR, NULL);
}
