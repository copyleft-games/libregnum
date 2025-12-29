/* lrg-audio-settings.h - Audio settings group
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_AUDIO_SETTINGS_H
#define LRG_AUDIO_SETTINGS_H

#include "lrg-settings-group.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_AUDIO_SETTINGS (lrg_audio_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAudioSettings, lrg_audio_settings, LRG, AUDIO_SETTINGS, LrgSettingsGroup)

/**
 * lrg_audio_settings_new:
 *
 * Creates a new #LrgAudioSettings with default values.
 *
 * Returns: (transfer full): A new #LrgAudioSettings
 */
LRG_AVAILABLE_IN_ALL
LrgAudioSettings *
lrg_audio_settings_new (void);

/* Master Volume (0.0 - 1.0) */

/**
 * lrg_audio_settings_get_master_volume:
 * @self: an #LrgAudioSettings
 *
 * Gets the master volume level.
 *
 * Returns: The master volume (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_settings_get_master_volume (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_master_volume:
 * @self: an #LrgAudioSettings
 * @volume: the volume level (0.0 to 1.0)
 *
 * Sets the master volume level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_master_volume (LrgAudioSettings *self,
                                      gdouble           volume);

/* Music Volume (0.0 - 1.0) */

/**
 * lrg_audio_settings_get_music_volume:
 * @self: an #LrgAudioSettings
 *
 * Gets the music volume level.
 *
 * Returns: The music volume (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_settings_get_music_volume (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_music_volume:
 * @self: an #LrgAudioSettings
 * @volume: the volume level (0.0 to 1.0)
 *
 * Sets the music volume level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_music_volume (LrgAudioSettings *self,
                                     gdouble           volume);

/* SFX Volume (0.0 - 1.0) */

/**
 * lrg_audio_settings_get_sfx_volume:
 * @self: an #LrgAudioSettings
 *
 * Gets the sound effects volume level.
 *
 * Returns: The SFX volume (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_settings_get_sfx_volume (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_sfx_volume:
 * @self: an #LrgAudioSettings
 * @volume: the volume level (0.0 to 1.0)
 *
 * Sets the sound effects volume level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_sfx_volume (LrgAudioSettings *self,
                                   gdouble           volume);

/* Voice Volume (0.0 - 1.0) */

/**
 * lrg_audio_settings_get_voice_volume:
 * @self: an #LrgAudioSettings
 *
 * Gets the voice/dialogue volume level.
 *
 * Returns: The voice volume (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_audio_settings_get_voice_volume (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_voice_volume:
 * @self: an #LrgAudioSettings
 * @volume: the volume level (0.0 to 1.0)
 *
 * Sets the voice/dialogue volume level.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_voice_volume (LrgAudioSettings *self,
                                     gdouble           volume);

/* Mute */

/**
 * lrg_audio_settings_get_muted:
 * @self: an #LrgAudioSettings
 *
 * Gets whether all audio is muted.
 *
 * Returns: %TRUE if audio is muted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_settings_get_muted (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_muted:
 * @self: an #LrgAudioSettings
 * @muted: whether to mute audio
 *
 * Sets whether all audio is muted.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_muted (LrgAudioSettings *self,
                              gboolean          muted);

/* Mono Audio (accessibility) */

/**
 * lrg_audio_settings_get_mono_audio:
 * @self: an #LrgAudioSettings
 *
 * Gets whether mono audio is enabled (accessibility feature).
 *
 * Returns: %TRUE if mono audio is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_settings_get_mono_audio (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_mono_audio:
 * @self: an #LrgAudioSettings
 * @mono: whether to enable mono audio
 *
 * Sets whether mono audio is enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_mono_audio (LrgAudioSettings *self,
                                   gboolean          mono);

/* Subtitles */

/**
 * lrg_audio_settings_get_subtitles_enabled:
 * @self: an #LrgAudioSettings
 *
 * Gets whether subtitles are enabled.
 *
 * Returns: %TRUE if subtitles are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_audio_settings_get_subtitles_enabled (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_subtitles_enabled:
 * @self: an #LrgAudioSettings
 * @enabled: whether to enable subtitles
 *
 * Sets whether subtitles are enabled.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_subtitles_enabled (LrgAudioSettings *self,
                                          gboolean          enabled);

/* Audio Device */

/**
 * lrg_audio_settings_get_audio_device:
 * @self: an #LrgAudioSettings
 *
 * Gets the selected audio output device name.
 *
 * Returns: (transfer none) (nullable): The device name, or %NULL for default
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_audio_settings_get_audio_device (LrgAudioSettings *self);

/**
 * lrg_audio_settings_set_audio_device:
 * @self: an #LrgAudioSettings
 * @device: (nullable): the device name, or %NULL for default
 *
 * Sets the audio output device.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_audio_settings_set_audio_device (LrgAudioSettings *self,
                                     const gchar      *device);

G_END_DECLS

#endif /* LRG_AUDIO_SETTINGS_H */
