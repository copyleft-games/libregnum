/* lrg-editor-hud-layout-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorHudLayoutEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-hud-layout-editor.h"

struct _LrgEditorHudLayoutEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorHudLayoutEditor, lrg_editor_hud_layout_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_hud_layout_editor_class_init (LrgEditorHudLayoutEditorClass *klass)
{
}

static void
lrg_editor_hud_layout_editor_init (LrgEditorHudLayoutEditor *self)
{
}

LrgEditorHudLayoutEditor *
lrg_editor_hud_layout_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR, NULL);
}
