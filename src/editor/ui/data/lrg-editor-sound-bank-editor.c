/* lrg-editor-sound-bank-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSoundBankEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-sound-bank-editor.h"

struct _LrgEditorSoundBankEditor
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorSoundBankEditor, lrg_editor_sound_bank_editor, LRG_TYPE_CONTAINER)

static void
lrg_editor_sound_bank_editor_class_init (LrgEditorSoundBankEditorClass *klass)
{
}

static void
lrg_editor_sound_bank_editor_init (LrgEditorSoundBankEditor *self)
{
}

LrgEditorSoundBankEditor *
lrg_editor_sound_bank_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SOUND_BANK_EDITOR, NULL);
}
