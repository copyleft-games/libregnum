/* lrg-editor-sound-bank-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSoundBankEditor: the data editor of sound bank manifests
 * (LrgSoundBank YAML: name, base_path, sounds). A list of the sounds with
 * a found / missing mark, a property grid for the bank and the selected
 * sound, audio assets dropped from the assets panel become sounds, and
 * Play previews the selected one.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SOUND_BANK_EDITOR (lrg_editor_sound_bank_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSoundBankEditor, lrg_editor_sound_bank_editor, LRG, EDITOR_SOUND_BANK_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_sound_bank_editor_new:
 * @app: (nullable): the editor application
 * @document: a sound bank document
 *
 * Returns: (transfer full): a new #LrgEditorSoundBankEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSoundBankEditor * lrg_editor_sound_bank_editor_new (LrgEditorApp      *app,
                                                             LrgEditorDocument *document);

/**
 * lrg_editor_sound_bank_editor_select:
 * @self: an #LrgEditorSoundBankEditor
 * @name: (nullable): a sound name, %NULL for the bank itself
 *
 * Returns: %FALSE when there is no sound @name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sound_bank_editor_select (LrgEditorSoundBankEditor *self,
                                              const gchar              *name);

/**
 * lrg_editor_sound_bank_editor_get_selected:
 * @self: an #LrgEditorSoundBankEditor
 *
 * Returns: (nullable): the selected sound
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sound_bank_editor_get_selected (LrgEditorSoundBankEditor *self);

/**
 * lrg_editor_sound_bank_editor_add_sound:
 * @self: an #LrgEditorSoundBankEditor
 * @name: (nullable): the sound name, %NULL to derive it from @file
 * @file: the file, relative to the bank's folder (base_path) or absolute
 * @error: return location for a #GError
 *
 * Adds a sound (a free name is chosen when @name is taken) and selects it.
 *
 * Returns: (transfer none) (nullable): the name used
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sound_bank_editor_add_sound (LrgEditorSoundBankEditor  *self,
                                                      const gchar               *name,
                                                      const gchar               *file,
                                                      GError                   **error);

/**
 * lrg_editor_sound_bank_editor_add_asset:
 * @self: an #LrgEditorSoundBankEditor
 * @asset: an audio file, project-relative (as the assets panel drags it)
 *   or absolute
 * @error: return location for a #GError
 *
 * What dropping an asset on the sound list does: adds it as a sound named
 * after the file, with its path made relative to the bank's sound folder.
 *
 * Returns: (transfer none) (nullable): the name used
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sound_bank_editor_add_asset (LrgEditorSoundBankEditor  *self,
                                                      const gchar               *asset,
                                                      GError                   **error);

/**
 * lrg_editor_sound_bank_editor_remove_selected:
 * @self: an #LrgEditorSoundBankEditor
 *
 * Returns: %TRUE when a sound was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sound_bank_editor_remove_selected (LrgEditorSoundBankEditor *self);

/**
 * lrg_editor_sound_bank_editor_play:
 * @self: an #LrgEditorSoundBankEditor
 * @name: (nullable): a sound, %NULL for the selected one
 * @error: return location for a #GError
 *
 * Previews a sound through the default audio device (opened on first
 * use). Fails without touching audio when the file does not exist.
 *
 * Returns: %TRUE when the sound started playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sound_bank_editor_play (LrgEditorSoundBankEditor  *self,
                                            const gchar               *name,
                                            GError                   **error);

G_END_DECLS
