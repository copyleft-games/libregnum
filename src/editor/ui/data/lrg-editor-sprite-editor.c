/* lrg-editor-sprite-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSpriteEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-sprite-editor.h"

struct _LrgEditorSpriteEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorSpriteEditor, lrg_editor_sprite_editor, G_TYPE_OBJECT)

static void
lrg_editor_sprite_editor_class_init (LrgEditorSpriteEditorClass *klass)
{
}

static void
lrg_editor_sprite_editor_init (LrgEditorSpriteEditor *self)
{
}

LrgEditorSpriteEditor *
lrg_editor_sprite_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SPRITE_EDITOR, NULL);
}
