/* lrg-editor-locale-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorLocaleEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-locale-editor.h"

struct _LrgEditorLocaleEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorLocaleEditor, lrg_editor_locale_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_locale_editor_class_init (LrgEditorLocaleEditorClass *klass)
{
}

static void
lrg_editor_locale_editor_init (LrgEditorLocaleEditor *self)
{
}

LrgEditorLocaleEditor *
lrg_editor_locale_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_LOCALE_EDITOR, NULL);
}
