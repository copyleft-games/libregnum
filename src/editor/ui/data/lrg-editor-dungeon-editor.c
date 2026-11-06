/* lrg-editor-dungeon-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDungeonEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-dungeon-editor.h"

struct _LrgEditorDungeonEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorDungeonEditor, lrg_editor_dungeon_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_dungeon_editor_class_init (LrgEditorDungeonEditorClass *klass)
{
}

static void
lrg_editor_dungeon_editor_init (LrgEditorDungeonEditor *self)
{
}

LrgEditorDungeonEditor *
lrg_editor_dungeon_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_DUNGEON_EDITOR, NULL);
}
