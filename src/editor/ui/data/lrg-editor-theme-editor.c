/* lrg-editor-theme-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorThemeEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-theme-editor.h"

struct _LrgEditorThemeEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorThemeEditor, lrg_editor_theme_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_theme_editor_class_init (LrgEditorThemeEditorClass *klass)
{
}

static void
lrg_editor_theme_editor_init (LrgEditorThemeEditor *self)
{
}

LrgEditorThemeEditor *
lrg_editor_theme_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_THEME_EDITOR, NULL);
}
