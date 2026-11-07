/* lrg-editor-mixer-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerPanel: the docked "Mixer" panel.
 *
 * It shows the project's default mixer (`audio: { mixer: ... }` in
 * project.ryaml) as compact channel strips: name, peak/RMS meters with
 * gain reduction, fader, Mute and Solo. Meters run while play-in-editor
 * plays (the audio manager's game mixer, which the panel loads from the
 * project when play starts and the game has none) or while the mixer
 * editor previews. Edits from the strips go to the mixer's document
 * (opening its editor tab when it is not open), so they are undoable
 * and saved with it. The header has the mixer path, Open (the full
 * editor), Preview, Save and, without a project mixer, New (creates
 * audio/main.rmixer and makes it the default).
 *
 * The panel registers the "mixer.show" action and, through
 * lrg_editor_mixer_editor_install(), the mixer.* IPC methods.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "lrg-editor-app.h"
#include "data/lrg-editor-mixer-editor.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_MIXER_PANEL_ID:
 *
 * The dock panel id of the Mixer panel.
 */
#define LRG_EDITOR_MIXER_PANEL_ID "mixer"

#define LRG_TYPE_EDITOR_MIXER_PANEL (lrg_editor_mixer_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorMixerPanel, lrg_editor_mixer_panel, LRG, EDITOR_MIXER_PANEL, LrgContainer)

/**
 * lrg_editor_mixer_panel_new:
 * @app: the editor application
 *
 * Creates the Mixer panel's content, registers its action and installs
 * the mixer.* IPC methods. The window docks it.
 *
 * Returns: (transfer full): a new #LrgEditorMixerPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMixerPanel * lrg_editor_mixer_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_mixer_panel_find:
 * @app: the editor application
 *
 * Returns: (transfer none) (nullable): the app's Mixer panel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMixerPanel * lrg_editor_mixer_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_mixer_panel_get_path:
 * @self: an #LrgEditorMixerPanel
 *
 * Returns: (nullable) (type filename): the absolute path of the mixer
 *   shown (the project's default), %NULL for none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_mixer_panel_get_path (LrgEditorMixerPanel *self);

/**
 * lrg_editor_mixer_panel_get_asset:
 * @self: an #LrgEditorMixerPanel
 *
 * The layout the strips show: the open editor's (unsaved edits
 * included), else the file on disk.
 *
 * Returns: (transfer none) (nullable): the layout
 */
LRG_AVAILABLE_IN_ALL
LrgMixerAsset * lrg_editor_mixer_panel_get_asset (LrgEditorMixerPanel *self);

/**
 * lrg_editor_mixer_panel_get_strips:
 * @self: an #LrgEditorMixerPanel
 *
 * Returns: (transfer none): the compact strips widget (see
 *   lrg_editor_mixer_strips_get_part_rect())
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_mixer_panel_get_strips (LrgEditorMixerPanel *self);

/**
 * lrg_editor_mixer_panel_reload:
 * @self: an #LrgEditorMixerPanel
 *
 * Re-reads the project's default mixer path and the file (the panel also
 * does this when the project changes and once a second).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_mixer_panel_reload (LrgEditorMixerPanel *self);

G_END_DECLS
