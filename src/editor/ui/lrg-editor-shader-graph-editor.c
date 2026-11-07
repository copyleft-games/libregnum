/* lrg-editor-shader-graph-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorShaderGraphEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-shader-graph-editor.h"

struct _LrgEditorShaderGraphEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorShaderGraphEditor, lrg_editor_shader_graph_editor, G_TYPE_OBJECT)

static void
lrg_editor_shader_graph_editor_class_init (LrgEditorShaderGraphEditorClass *klass)
{
}

static void
lrg_editor_shader_graph_editor_init (LrgEditorShaderGraphEditor *self)
{
}

LrgEditorShaderGraphEditor *
lrg_editor_shader_graph_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SHADER_GRAPH_EDITOR, NULL);
}
