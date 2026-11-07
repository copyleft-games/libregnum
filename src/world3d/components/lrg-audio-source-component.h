/* lrg-audio-source-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioSourceComponent: a positional sound emitter.  Plays a sound
 * bank entry (or a sound file) as an #LrgAudioManager voice at the
 * entity's world position and moves the voice with the entity every late
 * update.  Without an audio device nothing plays and nothing fails.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../audio/lrg-audio-manager.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LRG_AUDIO_SOURCE_FILE_BANK:
 *
 * Name of the sound bank #LrgAudioSourceComponent:sound-path files are
 * loaded into (one bank per audio manager, the file path is the sound
 * name).
 */
#define LRG_AUDIO_SOURCE_FILE_BANK "lrg-audio-source-files"

#define LRG_TYPE_AUDIO_SOURCE_COMPONENT (lrg_audio_source_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioSourceComponent, lrg_audio_source_component,
                      LRG, AUDIO_SOURCE_COMPONENT, LrgComponent)

/**
 * lrg_audio_source_component_new:
 *
 * Creates an emitter with no sound, volume 1, the default attenuation
 * (%LRG_AUDIO_DEFAULT_MIN_DISTANCE .. %LRG_AUDIO_DEFAULT_MAX_DISTANCE,
 * %LRG_AUDIO_DEFAULT_ROLLOFF), fully positional, not looping and not
 * autoplaying.
 *
 * Returns: (transfer full): a new #LrgAudioSourceComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAudioSourceComponent * lrg_audio_source_component_new (void);

/**
 * lrg_audio_source_component_new_with_sound:
 * @bank: the sound bank name
 * @sound: the sound name in @bank
 *
 * Creates an emitter for a sound bank entry.
 *
 * Returns: (transfer full): a new #LrgAudioSourceComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAudioSourceComponent * lrg_audio_source_component_new_with_sound (const gchar *bank,
                                                                     const gchar *sound);

/**
 * lrg_audio_source_component_set_sound:
 * @self: an #LrgAudioSourceComponent
 * @bank: (nullable): the sound bank name
 * @sound: (nullable): the sound name in @bank
 *
 * Selects a sound bank entry (#LrgAudioSourceComponent:bank and
 * #LrgAudioSourceComponent:sound).  Takes effect on the next play.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_sound (LrgAudioSourceComponent *self,
                                           const gchar             *bank,
                                           const gchar             *sound);

/**
 * lrg_audio_source_component_get_bank:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the sound bank name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_bank (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_sound:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the sound name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_sound (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_sound_path:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the sound file played when no bank
 *   is set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_source_component_get_sound_path (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_sound_path:
 * @self: an #LrgAudioSourceComponent
 * @path: (nullable): a sound file (the editor's AUDIO_EMITTER asset)
 *
 * Plays a file instead of a bank entry: on play it is loaded once into
 * the %LRG_AUDIO_SOURCE_FILE_BANK bank of the audio manager.  A bank set
 * with lrg_audio_source_component_set_sound() wins.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_sound_path (LrgAudioSourceComponent *self,
                                                const gchar             *path);

/**
 * lrg_audio_source_component_get_attenuation:
 * @self: an #LrgAudioSourceComponent
 * @out: (out caller-allocates): the attenuation voices are played with
 *
 * Fills @out from #LrgAudioSourceComponent:min-distance, max-distance,
 * rolloff and spatial-blend.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_get_attenuation (LrgAudioSourceComponent *self,
                                                 LrgAudioAttenuation     *out);

/**
 * lrg_audio_source_component_set_attenuation:
 * @self: an #LrgAudioSourceComponent
 * @attenuation: the curve to copy
 *
 * Sets min-distance, max-distance, rolloff and spatial-blend at once.
 * Takes effect on the next play.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_attenuation (LrgAudioSourceComponent   *self,
                                                 const LrgAudioAttenuation *attenuation);

/**
 * lrg_audio_source_component_play:
 * @self: an #LrgAudioSourceComponent
 *
 * Stops the current voice and starts a new one at the entity's world
 * position (flat when the component has no entity).
 *
 * Returns: the voice id, or 0 when nothing played (no sound, unknown
 *   bank or sound, no audio device)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_source_component_play (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_stop:
 * @self: an #LrgAudioSourceComponent
 *
 * Stops the current voice, if any.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_stop (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_is_playing:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: %TRUE while the component's voice is audible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_source_component_is_playing (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_voice:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: the current voice id, 0 when none
 */
LRG_AVAILABLE_IN_ALL
guint lrg_audio_source_component_get_voice (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_get_audio_manager:
 * @self: an #LrgAudioSourceComponent
 *
 * Returns: (transfer none) (nullable): the explicit audio manager, %NULL
 *   for the default one
 */
LRG_AVAILABLE_IN_ALL
LrgAudioManager * lrg_audio_source_component_get_audio_manager (LrgAudioSourceComponent *self);

/**
 * lrg_audio_source_component_set_audio_manager:
 * @self: an #LrgAudioSourceComponent
 * @manager: (nullable): an audio manager, %NULL for the default one
 *
 * Sets the audio manager voices play through.  Stops the current voice.
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_source_component_set_audio_manager (LrgAudioSourceComponent *self,
                                                   LrgAudioManager         *manager);

G_END_DECLS
