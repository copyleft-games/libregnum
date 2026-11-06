/* lrg-editor-input-map-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInputMapEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-input-map-editor.h"

struct _LrgEditorInputMapEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorInputMapEditor, lrg_editor_input_map_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_input_map_editor_class_init (LrgEditorInputMapEditorClass *klass)
{
}

static void
lrg_editor_input_map_editor_init (LrgEditorInputMapEditor *self)
{
}

LrgEditorInputMapEditor *
lrg_editor_input_map_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_INPUT_MAP_EDITOR, NULL);
}
