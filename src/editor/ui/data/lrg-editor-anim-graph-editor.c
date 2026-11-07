/* lrg-editor-anim-graph-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAnimGraphEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-anim-graph-editor.h"

struct _LrgEditorAnimGraphEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorAnimGraphEditor, lrg_editor_anim_graph_editor, G_TYPE_OBJECT)

static void
lrg_editor_anim_graph_editor_class_init (LrgEditorAnimGraphEditorClass *klass)
{
}

static void
lrg_editor_anim_graph_editor_init (LrgEditorAnimGraphEditor *self)
{
}

LrgEditorAnimGraphEditor *
lrg_editor_anim_graph_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_ANIM_GRAPH_EDITOR, NULL);
}
