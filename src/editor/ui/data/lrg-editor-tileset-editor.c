/* lrg-editor-tileset-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTilesetEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-tileset-editor.h"

struct _LrgEditorTilesetEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorTilesetEditor, lrg_editor_tileset_editor, G_TYPE_OBJECT)

static void
lrg_editor_tileset_editor_class_init (LrgEditorTilesetEditorClass *klass)
{
}

static void
lrg_editor_tileset_editor_init (LrgEditorTilesetEditor *self)
{
}

LrgEditorTilesetEditor *
lrg_editor_tileset_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_TILESET_EDITOR, NULL);
}
