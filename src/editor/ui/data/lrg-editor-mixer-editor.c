/* lrg-editor-mixer-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-mixer-editor.h"

struct _LrgEditorMixerEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorMixerEditor, lrg_editor_mixer_editor, G_TYPE_OBJECT)

static void
lrg_editor_mixer_editor_class_init (LrgEditorMixerEditorClass *klass)
{
}

static void
lrg_editor_mixer_editor_init (LrgEditorMixerEditor *self)
{
}

LrgEditorMixerEditor *
lrg_editor_mixer_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_MIXER_EDITOR, NULL);
}
