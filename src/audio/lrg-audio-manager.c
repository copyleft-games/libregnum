/* lrg-audio-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"
#include "lrg-audio-manager.h"
#include "../lrg-log.h"

/* Private structure */
struct _LrgAudioManager
{
    GObject parent_instance;

    /* Sound banks */
    GHashTable     *banks;          /* gchar* -> LrgSoundBank* */

    /* Music */
    LrgMusicTrack  *current_music;
    LrgMusicTrack  *next_music;     /* For crossfading */

    /* Volume channels */
    gfloat          volume_master;
    gfloat          volume_sfx;
    gfloat          volume_music;
    gfloat          volume_voice;
    gboolean        muted;

    /* Crossfade state */
    gboolean        crossfading;
    gfloat          crossfade_timer;
    gfloat          crossfade_duration;
};

G_DEFINE_FINAL_TYPE (LrgAudioManager, lrg_audio_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MASTER_VOLUME,
    PROP_SFX_VOLUME,
    PROP_MUSIC_VOLUME,
    PROP_VOICE_VOLUME,
    PROP_MUTED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_MUSIC_CHANGED,
    SIGNAL_SOUND_PLAYED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Singleton instance */
static LrgAudioManager *default_manager = NULL;

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_audio_manager_finalize (GObject *object)
{
    LrgAudioManager *self = LRG_AUDIO_MANAGER (object);

    g_clear_pointer (&self->banks, g_hash_table_unref);
    g_clear_object (&self->current_music);
    g_clear_object (&self->next_music);

    if (default_manager == self)
        default_manager = NULL;

    G_OBJECT_CLASS (lrg_audio_manager_parent_class)->finalize (object);
}

static void
lrg_audio_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgAudioManager *self = LRG_AUDIO_MANAGER (object);

    switch (prop_id)
    {
    case PROP_MASTER_VOLUME:
        g_value_set_float (value, self->volume_master);
        break;
    case PROP_SFX_VOLUME:
        g_value_set_float (value, self->volume_sfx);
        break;
    case PROP_MUSIC_VOLUME:
        g_value_set_float (value, self->volume_music);
        break;
    case PROP_VOICE_VOLUME:
        g_value_set_float (value, self->volume_voice);
        break;
    case PROP_MUTED:
        g_value_set_boolean (value, self->muted);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_audio_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgAudioManager *self = LRG_AUDIO_MANAGER (object);

    switch (prop_id)
    {
    case PROP_MASTER_VOLUME:
        lrg_audio_manager_set_master_volume (self, g_value_get_float (value));
        break;
    case PROP_SFX_VOLUME:
        lrg_audio_manager_set_sfx_volume (self, g_value_get_float (value));
        break;
    case PROP_MUSIC_VOLUME:
        lrg_audio_manager_set_music_volume (self, g_value_get_float (value));
        break;
    case PROP_VOICE_VOLUME:
        lrg_audio_manager_set_voice_volume (self, g_value_get_float (value));
        break;
    case PROP_MUTED:
        lrg_audio_manager_set_muted (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_audio_manager_class_init (LrgAudioManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_audio_manager_finalize;
    object_class->get_property = lrg_audio_manager_get_property;
    object_class->set_property = lrg_audio_manager_set_property;

    /**
     * LrgAudioManager:master-volume:
     *
     * The master volume for all audio.
     */
    properties[PROP_MASTER_VOLUME] =
        g_param_spec_float ("master-volume",
                            "Master Volume",
                            "The master volume for all audio",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAudioManager:sfx-volume:
     *
     * The volume for sound effects.
     */
    properties[PROP_SFX_VOLUME] =
        g_param_spec_float ("sfx-volume",
                            "SFX Volume",
                            "The volume for sound effects",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAudioManager:music-volume:
     *
     * The volume for music.
     */
    properties[PROP_MUSIC_VOLUME] =
        g_param_spec_float ("music-volume",
                            "Music Volume",
                            "The volume for music",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAudioManager:voice-volume:
     *
     * The volume for voice/dialog.
     */
    properties[PROP_VOICE_VOLUME] =
        g_param_spec_float ("voice-volume",
                            "Voice Volume",
                            "The volume for voice/dialog",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAudioManager:muted:
     *
     * Whether audio is muted.
     */
    properties[PROP_MUTED] =
        g_param_spec_boolean ("muted",
                              "Muted",
                              "Whether audio is muted",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAudioManager::music-changed:
     * @self: the audio manager
     * @track: (nullable): the new music track
     *
     * Emitted when the current music changes.
     */
    signals[SIGNAL_MUSIC_CHANGED] =
        g_signal_new ("music-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_MUSIC_TRACK);

    /**
     * LrgAudioManager::sound-played:
     * @self: the audio manager
     * @bank: the bank name
     * @sound: the sound name
     *
     * Emitted when a sound is played.
     */
    signals[SIGNAL_SOUND_PLAYED] =
        g_signal_new ("sound-played",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_audio_manager_init (LrgAudioManager *self)
{
    self->banks = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          g_object_unref);

    self->volume_master = 1.0f;
    self->volume_sfx = 1.0f;
    self->volume_music = 1.0f;
    self->volume_voice = 1.0f;
    self->muted = FALSE;

    self->crossfading = FALSE;
    self->crossfade_timer = 0.0f;
    self->crossfade_duration = 0.0f;
}

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_audio_manager_get_default:
 *
 * Gets the default audio manager singleton.
 *
 * Returns: (transfer none): The default #LrgAudioManager
 */
LrgAudioManager *
lrg_audio_manager_get_default (void)
{
    if (default_manager == NULL)
    {
        default_manager = g_object_new (LRG_TYPE_AUDIO_MANAGER, NULL);
        lrg_log_info ("Audio manager initialized");
    }

    return default_manager;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gfloat
calculate_effective_sfx_volume (LrgAudioManager *self)
{
    if (self->muted)
        return 0.0f;

    return self->volume_master * self->volume_sfx;
}

static gfloat
calculate_effective_music_volume (LrgAudioManager *self)
{
    if (self->muted)
        return 0.0f;

    return self->volume_master * self->volume_music;
}

static void
update_bank_volume (gpointer key     G_GNUC_UNUSED,
                    gpointer value,
                    gpointer user_data)
{
    LrgSoundBank *bank = LRG_SOUND_BANK (value);
    gfloat volume = *(gfloat *)user_data;
    lrg_sound_bank_set_volume (bank, volume);
}

static void
update_all_bank_volumes (LrgAudioManager *self)
{
    gfloat volume = calculate_effective_sfx_volume (self);
    g_hash_table_foreach (self->banks, update_bank_volume, &volume);
}

static void
update_music_volume (LrgAudioManager *self)
{
    gfloat volume;

    if (self->current_music == NULL)
        return;

    /* Don't override volume if crossfading */
    if (self->crossfading)
        return;

    volume = calculate_effective_music_volume (self);
    lrg_music_track_set_volume (self->current_music, volume);
}

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_audio_manager_update:
 * @self: the audio manager
 *
 * Updates all audio streams. Must be called every frame.
 */
void
lrg_audio_manager_update (LrgAudioManager *self)
{
    gfloat dt;
    gfloat progress;
    gfloat old_volume;
    gfloat new_volume;
    gfloat effective_volume;

    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    /* Update current music */
    if (self->current_music != NULL)
        lrg_music_track_update (self->current_music);

    /* Update next music (for crossfading) */
    if (self->next_music != NULL)
        lrg_music_track_update (self->next_music);

    /* Handle crossfading */
    if (self->crossfading && self->current_music != NULL && self->next_music != NULL)
    {
        /* Estimate dt as 1/60th of a second */
        dt = 1.0f / 60.0f;
        self->crossfade_timer += dt;

        if (self->crossfade_timer >= self->crossfade_duration)
        {
            /* Crossfade complete */
            lrg_music_track_stop (self->current_music);
            g_clear_object (&self->current_music);
            self->current_music = g_steal_pointer (&self->next_music);

            effective_volume = calculate_effective_music_volume (self);
            lrg_music_track_set_volume (self->current_music, effective_volume);

            self->crossfading = FALSE;

            lrg_log_debug ("Crossfade complete");
            g_signal_emit (self, signals[SIGNAL_MUSIC_CHANGED], 0, self->current_music);
        }
        else
        {
            /* Interpolate volumes */
            progress = self->crossfade_timer / self->crossfade_duration;
            effective_volume = calculate_effective_music_volume (self);

            old_volume = (1.0f - progress) * effective_volume;
            new_volume = progress * effective_volume;

            lrg_music_track_set_volume (self->current_music, old_volume);
            lrg_music_track_set_volume (self->next_music, new_volume);
        }
    }
}

/* ==========================================================================
 * Sound Bank Management
 * ========================================================================== */

/**
 * lrg_audio_manager_add_bank:
 * @self: the audio manager
 * @bank: the sound bank to add
 *
 * Adds a sound bank to the manager.
 */
void
lrg_audio_manager_add_bank (LrgAudioManager *self,
                             LrgSoundBank    *bank)
{
    const gchar *name;
    gfloat volume;

    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));
    g_return_if_fail (LRG_IS_SOUND_BANK (bank));

    name = lrg_sound_bank_get_name (bank);
    g_return_if_fail (name != NULL);

    /* Apply current volume */
    volume = calculate_effective_sfx_volume (self);
    lrg_sound_bank_set_volume (bank, volume);

    g_hash_table_insert (self->banks,
                         g_strdup (name),
                         g_object_ref (bank));

    lrg_log_debug ("Added sound bank '%s'", name);
}

/**
 * lrg_audio_manager_load_bank:
 * @self: the audio manager
 * @manifest_path: path to the bank manifest YAML file
 * @error: (optional): return location for a #GError
 *
 * Loads a sound bank from a manifest file and adds it.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_audio_manager_load_bank (LrgAudioManager  *self,
                              const gchar      *manifest_path,
                              GError          **error)
{
    g_autoptr(LrgSoundBank) bank = NULL;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (manifest_path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    bank = lrg_sound_bank_new_from_file (manifest_path, error);
    if (bank == NULL)
        return FALSE;

    lrg_audio_manager_add_bank (self, bank);
    return TRUE;
}

/**
 * lrg_audio_manager_remove_bank:
 * @self: the audio manager
 * @name: the bank name to remove
 *
 * Removes a sound bank from the manager.
 *
 * Returns: %TRUE if the bank was found and removed
 */
gboolean
lrg_audio_manager_remove_bank (LrgAudioManager *self,
                                const gchar     *name)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_remove (self->banks, name);
}

/**
 * lrg_audio_manager_get_bank:
 * @self: the audio manager
 * @name: the bank name
 *
 * Gets a sound bank by name.
 *
 * Returns: (transfer none) (nullable): the sound bank, or %NULL if not found
 */
LrgSoundBank *
lrg_audio_manager_get_bank (LrgAudioManager *self,
                             const gchar     *name)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->banks, name);
}

/**
 * lrg_audio_manager_get_bank_names:
 * @self: the audio manager
 *
 * Gets a list of all bank names.
 *
 * Returns: (transfer container) (element-type utf8): list of bank names
 */
GList *
lrg_audio_manager_get_bank_names (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), NULL);

    return g_hash_table_get_keys (self->banks);
}

/* ==========================================================================
 * Sound Playback
 * ========================================================================== */

/**
 * lrg_audio_manager_play_sound:
 * @self: the audio manager
 * @bank: the bank name
 * @sound: the sound name
 *
 * Plays a sound from a bank.
 *
 * Returns: %TRUE if the sound was found and played
 */
gboolean
lrg_audio_manager_play_sound (LrgAudioManager *self,
                               const gchar     *bank,
                               const gchar     *sound)
{
    LrgSoundBank *sound_bank;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (bank != NULL, FALSE);
    g_return_val_if_fail (sound != NULL, FALSE);

    sound_bank = lrg_audio_manager_get_bank (self, bank);
    if (sound_bank == NULL)
    {
        lrg_log_warning ("Sound bank '%s' not found", bank);
        return FALSE;
    }

    if (lrg_sound_bank_play (sound_bank, sound))
    {
        g_signal_emit (self, signals[SIGNAL_SOUND_PLAYED], 0, bank, sound);
        return TRUE;
    }

    return FALSE;
}

/**
 * lrg_audio_manager_play_sound_multi:
 * @self: the audio manager
 * @bank: the bank name
 * @sound: the sound name
 *
 * Plays a sound allowing multiple overlapping instances.
 *
 * Returns: %TRUE if the sound was found and played
 */
gboolean
lrg_audio_manager_play_sound_multi (LrgAudioManager *self,
                                     const gchar     *bank,
                                     const gchar     *sound)
{
    LrgSoundBank *sound_bank;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (bank != NULL, FALSE);
    g_return_val_if_fail (sound != NULL, FALSE);

    sound_bank = lrg_audio_manager_get_bank (self, bank);
    if (sound_bank == NULL)
    {
        lrg_log_warning ("Sound bank '%s' not found", bank);
        return FALSE;
    }

    if (lrg_sound_bank_play_multi (sound_bank, sound))
    {
        g_signal_emit (self, signals[SIGNAL_SOUND_PLAYED], 0, bank, sound);
        return TRUE;
    }

    return FALSE;
}

/**
 * lrg_audio_manager_stop_sound:
 * @self: the audio manager
 * @bank: the bank name
 * @sound: the sound name
 *
 * Stops a playing sound.
 *
 * Returns: %TRUE if the sound was found and stopped
 */
gboolean
lrg_audio_manager_stop_sound (LrgAudioManager *self,
                               const gchar     *bank,
                               const gchar     *sound)
{
    LrgSoundBank *sound_bank;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (bank != NULL, FALSE);
    g_return_val_if_fail (sound != NULL, FALSE);

    sound_bank = lrg_audio_manager_get_bank (self, bank);
    if (sound_bank == NULL)
        return FALSE;

    return lrg_sound_bank_stop (sound_bank, sound);
}

static void
stop_bank_sounds (gpointer key     G_GNUC_UNUSED,
                  gpointer value,
                  gpointer user_data G_GNUC_UNUSED)
{
    LrgSoundBank *bank = LRG_SOUND_BANK (value);
    lrg_sound_bank_stop_all (bank);
}

/**
 * lrg_audio_manager_stop_all_sounds:
 * @self: the audio manager
 *
 * Stops all playing sounds in all banks.
 */
void
lrg_audio_manager_stop_all_sounds (LrgAudioManager *self)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    g_hash_table_foreach (self->banks, stop_bank_sounds, NULL);
}

/* ==========================================================================
 * Music Playback
 * ========================================================================== */

/**
 * lrg_audio_manager_play_music:
 * @self: the audio manager
 * @track: the music track to play
 *
 * Plays a music track, replacing any currently playing music.
 */
void
lrg_audio_manager_play_music (LrgAudioManager *self,
                               LrgMusicTrack   *track)
{
    gfloat volume;

    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));
    g_return_if_fail (track == NULL || LRG_IS_MUSIC_TRACK (track));

    /* Stop any crossfade in progress */
    if (self->crossfading)
    {
        self->crossfading = FALSE;
        if (self->next_music != NULL)
        {
            lrg_music_track_stop (self->next_music);
            g_clear_object (&self->next_music);
        }
    }

    /* Stop current music */
    if (self->current_music != NULL)
    {
        lrg_music_track_stop (self->current_music);
        g_clear_object (&self->current_music);
    }

    /* Set new music */
    if (track != NULL)
    {
        self->current_music = g_object_ref (track);
        volume = calculate_effective_music_volume (self);
        lrg_music_track_set_volume (track, volume);
        lrg_music_track_play (track);

        lrg_log_debug ("Playing music: %s",
                      lrg_music_track_get_name (track) ?
                      lrg_music_track_get_name (track) : "(unnamed)");
    }

    g_signal_emit (self, signals[SIGNAL_MUSIC_CHANGED], 0, track);
}

/**
 * lrg_audio_manager_play_music_from_file:
 * @self: the audio manager
 * @path: path to the music file
 * @error: (optional): return location for a #GError
 *
 * Loads and plays a music file.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_audio_manager_play_music_from_file (LrgAudioManager  *self,
                                         const gchar      *path,
                                         GError          **error)
{
    g_autoptr(LrgMusicTrack) track = NULL;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    track = lrg_music_track_new_from_file (path, error);
    if (track == NULL)
        return FALSE;

    lrg_audio_manager_play_music (self, track);
    return TRUE;
}

/**
 * lrg_audio_manager_stop_music:
 * @self: the audio manager
 *
 * Stops the currently playing music.
 */
void
lrg_audio_manager_stop_music (LrgAudioManager *self)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    /* Cancel any crossfade */
    self->crossfading = FALSE;
    if (self->next_music != NULL)
    {
        lrg_music_track_stop (self->next_music);
        g_clear_object (&self->next_music);
    }

    if (self->current_music != NULL)
    {
        lrg_music_track_stop (self->current_music);
        g_clear_object (&self->current_music);

        g_signal_emit (self, signals[SIGNAL_MUSIC_CHANGED], 0, NULL);
    }
}

/**
 * lrg_audio_manager_pause_music:
 * @self: the audio manager
 *
 * Pauses the currently playing music.
 */
void
lrg_audio_manager_pause_music (LrgAudioManager *self)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    if (self->current_music != NULL)
        lrg_music_track_pause (self->current_music);
}

/**
 * lrg_audio_manager_resume_music:
 * @self: the audio manager
 *
 * Resumes paused music.
 */
void
lrg_audio_manager_resume_music (LrgAudioManager *self)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    if (self->current_music != NULL)
        lrg_music_track_resume (self->current_music);
}

/**
 * lrg_audio_manager_get_current_music:
 * @self: the audio manager
 *
 * Gets the currently playing music track.
 *
 * Returns: (transfer none) (nullable): the current track, or %NULL if none
 */
LrgMusicTrack *
lrg_audio_manager_get_current_music (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), NULL);

    return self->current_music;
}

/**
 * lrg_audio_manager_is_music_playing:
 * @self: the audio manager
 *
 * Checks if music is currently playing.
 *
 * Returns: %TRUE if music is playing
 */
gboolean
lrg_audio_manager_is_music_playing (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);

    if (self->current_music == NULL)
        return FALSE;

    return lrg_music_track_is_playing (self->current_music);
}

/* ==========================================================================
 * Crossfade
 * ========================================================================== */

/**
 * lrg_audio_manager_crossfade_to:
 * @self: the audio manager
 * @track: the new music track
 * @duration: crossfade duration in seconds
 *
 * Crossfades from the current music to a new track.
 */
void
lrg_audio_manager_crossfade_to (LrgAudioManager *self,
                                 LrgMusicTrack   *track,
                                 gfloat           duration)
{
    gfloat effective_volume;

    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));
    g_return_if_fail (LRG_IS_MUSIC_TRACK (track));
    g_return_if_fail (duration > 0.0f);

    /* If no current music, just play directly */
    if (self->current_music == NULL || !lrg_music_track_is_playing (self->current_music))
    {
        lrg_audio_manager_play_music (self, track);
        return;
    }

    /* Cancel existing crossfade */
    if (self->crossfading && self->next_music != NULL)
    {
        lrg_music_track_stop (self->next_music);
        g_clear_object (&self->next_music);
    }

    /* Start crossfade */
    self->next_music = g_object_ref (track);
    self->crossfading = TRUE;
    self->crossfade_timer = 0.0f;
    self->crossfade_duration = duration;

    /* Start new track at zero volume */
    lrg_music_track_set_volume (track, 0.0f);
    lrg_music_track_play (track);

    /* Set current track to full effective volume */
    effective_volume = calculate_effective_music_volume (self);
    lrg_music_track_set_volume (self->current_music, effective_volume);

    lrg_log_debug ("Starting crossfade to '%s' over %.2fs",
                  lrg_music_track_get_name (track) ?
                  lrg_music_track_get_name (track) : "(unnamed)",
                  duration);
}

/**
 * lrg_audio_manager_is_crossfading:
 * @self: the audio manager
 *
 * Checks if a crossfade is in progress.
 *
 * Returns: %TRUE if crossfading
 */
gboolean
lrg_audio_manager_is_crossfading (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);

    return self->crossfading;
}

/* ==========================================================================
 * Volume Control
 * ========================================================================== */

/**
 * lrg_audio_manager_set_master_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the master volume for all audio.
 */
void
lrg_audio_manager_set_master_volume (LrgAudioManager *self,
                                      gfloat           volume)
{
    GrlAudioDevice *device;

    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (self->volume_master != volume)
    {
        self->volume_master = volume;

        /* Update graylib master volume */
        device = grl_audio_device_get_default ();
        grl_audio_device_set_master_volume (device, self->muted ? 0.0f : volume);

        /* Update all banks */
        update_all_bank_volumes (self);

        /* Update music */
        update_music_volume (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MASTER_VOLUME]);
    }
}

/**
 * lrg_audio_manager_get_master_volume:
 * @self: the audio manager
 *
 * Gets the master volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
gfloat
lrg_audio_manager_get_master_volume (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), 1.0f);

    return self->volume_master;
}

/**
 * lrg_audio_manager_set_sfx_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the sound effects volume.
 */
void
lrg_audio_manager_set_sfx_volume (LrgAudioManager *self,
                                   gfloat           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (self->volume_sfx != volume)
    {
        self->volume_sfx = volume;
        update_all_bank_volumes (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SFX_VOLUME]);
    }
}

/**
 * lrg_audio_manager_get_sfx_volume:
 * @self: the audio manager
 *
 * Gets the sound effects volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
gfloat
lrg_audio_manager_get_sfx_volume (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), 1.0f);

    return self->volume_sfx;
}

/**
 * lrg_audio_manager_set_music_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the music volume.
 */
void
lrg_audio_manager_set_music_volume (LrgAudioManager *self,
                                     gfloat           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (self->volume_music != volume)
    {
        self->volume_music = volume;
        update_music_volume (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MUSIC_VOLUME]);
    }
}

/**
 * lrg_audio_manager_get_music_volume:
 * @self: the audio manager
 *
 * Gets the music volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
gfloat
lrg_audio_manager_get_music_volume (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), 1.0f);

    return self->volume_music;
}

/**
 * lrg_audio_manager_set_voice_volume:
 * @self: the audio manager
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the voice/dialog volume.
 */
void
lrg_audio_manager_set_voice_volume (LrgAudioManager *self,
                                     gfloat           volume)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (self->volume_voice != volume)
    {
        self->volume_voice = volume;
        /* Voice would be applied to a voice bank if we had one */
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOICE_VOLUME]);
    }
}

/**
 * lrg_audio_manager_get_voice_volume:
 * @self: the audio manager
 *
 * Gets the voice/dialog volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
gfloat
lrg_audio_manager_get_voice_volume (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), 1.0f);

    return self->volume_voice;
}

/**
 * lrg_audio_manager_set_muted:
 * @self: the audio manager
 * @muted: whether to mute
 *
 * Mutes or unmutes all audio.
 */
void
lrg_audio_manager_set_muted (LrgAudioManager *self,
                              gboolean         muted)
{
    GrlAudioDevice *device;

    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    if (self->muted != muted)
    {
        self->muted = muted;

        /* Update graylib master volume */
        device = grl_audio_device_get_default ();
        grl_audio_device_set_master_volume (device, muted ? 0.0f : self->volume_master);

        /* Update all volumes */
        update_all_bank_volumes (self);
        update_music_volume (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MUTED]);

        lrg_log_debug ("Audio %s", muted ? "muted" : "unmuted");
    }
}

/**
 * lrg_audio_manager_get_muted:
 * @self: the audio manager
 *
 * Gets whether audio is muted.
 *
 * Returns: %TRUE if muted
 */
gboolean
lrg_audio_manager_get_muted (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);

    return self->muted;
}
