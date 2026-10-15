/* lrg-audio-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"
#include <math.h>
#include <string.h>
#include "lrg-audio-manager.h"
#include "../lrg-log.h"

/*
 * LrgAudioVoice:
 * One playing instance of a bank sound started by
 * lrg_audio_manager_play_sound_at().  The alias shares the samples of
 * @source, which the voice keeps alive (raylib aliases do not own their
 * data), so a bank may drop the sound while voices still play.
 */
typedef struct
{
    GrlSound            *source;        /* owned ref: the bank's sound */
    GrlSound            *alias;         /* owned: this voice's sound */
    gfloat               volume;        /* requested 0..1 */
    gboolean             positional;
    gfloat               position[3];
    LrgAudioAttenuation  attenuation;
    gfloat               gain;          /* last applied attenuation gain */
    gfloat               pan;           /* last applied pan */
} LrgAudioVoice;

static void apply_voice_foreach (gpointer key,
                                 gpointer value,
                                 gpointer user_data);

static void
lrg_audio_voice_free (gpointer data)
{
    LrgAudioVoice *voice = data;

    if (voice == NULL)
        return;

    /* the alias goes before the sound whose samples it borrows */
    if (voice->alias != NULL)
        grl_sound_stop (voice->alias);
    g_clear_object (&voice->alias);
    g_clear_object (&voice->source);
    g_free (voice);
}

/* Private structure */
struct _LrgAudioManager
{
    GObject parent_instance;

    /* Sound banks */
    GHashTable     *banks;          /* gchar* -> LrgSoundBank* */

    /* Procedural audio sources */
    GHashTable     *procedural;     /* gchar* -> LrgProceduralAudio* */

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

    /* Positional voices and the listener */
    GHashTable     *voices;         /* GUINT_TO_POINTER(id) -> LrgAudioVoice* */
    guint           next_voice;
    gfloat          listener_position[3];
    gfloat          listener_forward[3];
    gfloat          listener_up[3];
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

    /* voices hold aliases of bank sounds: release them first */
    g_clear_pointer (&self->voices, g_hash_table_unref);
    g_clear_pointer (&self->banks, g_hash_table_unref);
    g_clear_pointer (&self->procedural, g_hash_table_unref);
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

    self->procedural = g_hash_table_new_full (g_str_hash,
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

    self->voices = g_hash_table_new_full (g_direct_hash,
                                          g_direct_equal,
                                          NULL,
                                          lrg_audio_voice_free);
    self->next_voice = 1;
    self->listener_forward[2] = 1.0f;
    self->listener_up[1] = 1.0f;
}

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/*
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

    /* voices carry their own volume: re-apply with the new channels */
    if (self->voices != NULL)
        g_hash_table_foreach (self->voices, apply_voice_foreach, self);
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
 * Positional voices: helpers
 * ========================================================================== */

static gfloat
vec3_length (const gfloat *v)
{
    return sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/*
 * attenuation_gain:
 * The distance curve of lrg_audio_compute_spatial() before the spatial
 * blend: 1 within min, 0 beyond max, linear (rolloff <= 0) or inverse
 * distance rescaled to reach 0 at max (rolloff > 0) in between.
 */
static gfloat
attenuation_gain (const LrgAudioAttenuation *att,
                  gfloat                     distance)
{
    gfloat min_d;
    gfloat max_d;
    gfloat inv_d;
    gfloat inv_max;

    min_d = MAX (att->min_distance, 0.0001f);
    max_d = MAX (att->max_distance, min_d);

    if (distance <= min_d)
        return 1.0f;
    if (distance >= max_d)
        return 0.0f;

    if (att->rolloff <= 0.0f)
        return (max_d - distance) / (max_d - min_d);

    inv_d = min_d / (min_d + att->rolloff * (distance - min_d));
    inv_max = min_d / (min_d + att->rolloff * (max_d - min_d));
    if (inv_max >= 1.0f)
        return 1.0f;

    return CLAMP ((inv_d - inv_max) / (1.0f - inv_max), 0.0f, 1.0f);
}

/* effective volume of one voice: request x attenuation x channels */
static void
apply_voice (LrgAudioManager *self,
             LrgAudioVoice   *voice)
{
    gfloat gain;
    gfloat pan;

    gain = 1.0f;
    pan = 0.0f;
    if (voice->positional)
        lrg_audio_compute_spatial (self->listener_position,
                                   self->listener_forward,
                                   self->listener_up,
                                   voice->position,
                                   &voice->attenuation,
                                   &gain, &pan);

    voice->gain = gain;
    voice->pan = pan;

    if (voice->alias != NULL)
    {
        grl_sound_set_volume (voice->alias,
                              CLAMP (voice->volume * gain *
                                     calculate_effective_sfx_volume (self),
                                     0.0f, 1.0f));
        grl_sound_set_pan (voice->alias, pan);
    }
}

static void
apply_voice_foreach (gpointer key       G_GNUC_UNUSED,
                     gpointer value,
                     gpointer user_data)
{
    apply_voice (LRG_AUDIO_MANAGER (user_data), value);
}

/* GHRFunc: drop finished voices, re-spatialise the others */
static gboolean
update_voice_foreach (gpointer key       G_GNUC_UNUSED,
                      gpointer value,
                      gpointer user_data)
{
    LrgAudioVoice *voice = value;

    if (voice->alias == NULL || !grl_sound_is_playing (voice->alias))
        return TRUE;

    apply_voice (LRG_AUDIO_MANAGER (user_data), voice);
    return FALSE;
}

static void
update_voices (LrgAudioManager *self)
{
    g_hash_table_foreach_remove (self->voices, update_voice_foreach, self);
}

static LrgAudioVoice *
lookup_voice (LrgAudioManager *self,
              guint            voice)
{
    if (voice == 0)
        return NULL;
    return g_hash_table_lookup (self->voices, GUINT_TO_POINTER (voice));
}

/* ==========================================================================
 * Update
 * ========================================================================== */

/* Helper to update all procedural audio sources */
static void
update_procedural_foreach (gpointer key     G_GNUC_UNUSED,
                           gpointer value,
                           gpointer user_data G_GNUC_UNUSED)
{
    LrgProceduralAudio *audio = LRG_PROCEDURAL_AUDIO (value);
    lrg_procedural_audio_update (audio);
}

/*
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

    /* Update all procedural audio sources */
    g_hash_table_foreach (self->procedural, update_procedural_foreach, NULL);

    /* Release finished voices, re-spatialise the others */
    update_voices (self);

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

/*
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

/*
 * lrg_audio_manager_load_bank:
 * @self: the audio manager
 * @manifest_path: path to the bank manifest YAML file
 * @error: return location for a #GError
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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
    lrg_audio_manager_stop_all_voices (self);
}

/* ==========================================================================
 * Positional Sound (emitters and listener)
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgAudioAttenuation, lrg_audio_attenuation,
                     lrg_audio_attenuation_copy,
                     lrg_audio_attenuation_free)

/*
 * lrg_audio_attenuation_new:
 * Allocates an attenuation curve with the given values.
 */
LrgAudioAttenuation *
lrg_audio_attenuation_new (gfloat min_distance,
                           gfloat max_distance,
                           gfloat rolloff,
                           gfloat spatial_blend)
{
    LrgAudioAttenuation *self;

    self = g_new0 (LrgAudioAttenuation, 1);
    self->min_distance = min_distance;
    self->max_distance = max_distance;
    self->rolloff = rolloff;
    self->spatial_blend = spatial_blend;
    return self;
}

/*
 * lrg_audio_attenuation_init_default:
 * Fills in the default curve (also used for a %NULL attenuation).
 */
void
lrg_audio_attenuation_init_default (LrgAudioAttenuation *self)
{
    g_return_if_fail (self != NULL);

    self->min_distance = LRG_AUDIO_DEFAULT_MIN_DISTANCE;
    self->max_distance = LRG_AUDIO_DEFAULT_MAX_DISTANCE;
    self->rolloff = LRG_AUDIO_DEFAULT_ROLLOFF;
    self->spatial_blend = 1.0f;
}

/* lrg_audio_attenuation_copy: plain struct copy */
LrgAudioAttenuation *
lrg_audio_attenuation_copy (const LrgAudioAttenuation *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return g_memdup2 (self, sizeof (LrgAudioAttenuation));
}

/* lrg_audio_attenuation_free: accepts NULL */
void
lrg_audio_attenuation_free (LrgAudioAttenuation *self)
{
    g_free (self);
}

/*
 * lrg_audio_compute_spatial:
 * Gain from the distance curve; pan from the source direction projected
 * on the listener's right (forward x up), faded towards the centre
 * inside min_distance; both mixed with flat playback by spatial_blend.
 */
void
lrg_audio_compute_spatial (const gfloat              *listener_position,
                           const gfloat              *listener_forward,
                           const gfloat              *listener_up,
                           const gfloat              *source_position,
                           const LrgAudioAttenuation *attenuation,
                           gfloat                    *out_gain,
                           gfloat                    *out_pan)
{
    LrgAudioAttenuation defaults;
    const LrgAudioAttenuation *att;
    gfloat forward[3];
    gfloat up[3];
    gfloat right[3];
    gfloat dir[3];
    gfloat distance;
    gfloat length;
    gfloat blend;
    gfloat gain;
    gfloat pan;
    gfloat min_d;

    g_return_if_fail (listener_position != NULL);
    g_return_if_fail (listener_forward != NULL);
    g_return_if_fail (source_position != NULL);

    lrg_audio_attenuation_init_default (&defaults);
    att = attenuation != NULL ? attenuation : &defaults;
    blend = CLAMP (att->spatial_blend, 0.0f, 1.0f);

    /* distance and normalized direction listener -> source */
    dir[0] = source_position[0] - listener_position[0];
    dir[1] = source_position[1] - listener_position[1];
    dir[2] = source_position[2] - listener_position[2];
    distance = vec3_length (dir);

    gain = attenuation_gain (att, distance);
    pan = 0.0f;

    if (distance > 1e-6f)
    {
        /* listener basis: forward (default +Z) and up (default +Y) */
        length = vec3_length (listener_forward);
        if (length > 1e-6f)
        {
            forward[0] = listener_forward[0] / length;
            forward[1] = listener_forward[1] / length;
            forward[2] = listener_forward[2] / length;
        }
        else
        {
            forward[0] = 0.0f;
            forward[1] = 0.0f;
            forward[2] = 1.0f;
        }

        up[0] = 0.0f;
        up[1] = 1.0f;
        up[2] = 0.0f;
        if (listener_up != NULL && vec3_length (listener_up) > 1e-6f)
        {
            up[0] = listener_up[0];
            up[1] = listener_up[1];
            up[2] = listener_up[2];
        }

        /* right = forward x up; looking straight along up has no right */
        right[0] = forward[1] * up[2] - forward[2] * up[1];
        right[1] = forward[2] * up[0] - forward[0] * up[2];
        right[2] = forward[0] * up[1] - forward[1] * up[0];
        length = vec3_length (right);

        if (length > 1e-6f)
        {
            pan = (dir[0] * right[0] + dir[1] * right[1] + dir[2] * right[2]) /
                  (distance * length);

            /* a source inside the head zone drifts to the centre */
            min_d = MAX (att->min_distance, 0.0001f);
            if (distance < min_d)
                pan *= distance / min_d;
        }
    }

    if (out_gain != NULL)
        *out_gain = CLAMP ((1.0f - blend) + blend * gain, 0.0f, 1.0f);
    if (out_pan != NULL)
        *out_pan = CLAMP (blend * pan, -1.0f, 1.0f);
}

/*
 * lrg_audio_manager_set_listener:
 * Stores the ears; voices follow on the next update.
 */
void
lrg_audio_manager_set_listener (LrgAudioManager *self,
                                const gfloat    *position,
                                const gfloat    *forward,
                                const gfloat    *up)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));
    g_return_if_fail (position != NULL);
    g_return_if_fail (forward != NULL);

    memcpy (self->listener_position, position, sizeof (gfloat) * 3);
    memcpy (self->listener_forward, forward, sizeof (gfloat) * 3);
    if (up != NULL)
    {
        memcpy (self->listener_up, up, sizeof (gfloat) * 3);
    }
    else
    {
        self->listener_up[0] = 0.0f;
        self->listener_up[1] = 1.0f;
        self->listener_up[2] = 0.0f;
    }
}

/* lrg_audio_manager_get_listener: copies out what was set */
void
lrg_audio_manager_get_listener (LrgAudioManager *self,
                                gfloat          *out_position,
                                gfloat          *out_forward,
                                gfloat          *out_up)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    if (out_position != NULL)
        memcpy (out_position, self->listener_position, sizeof (gfloat) * 3);
    if (out_forward != NULL)
        memcpy (out_forward, self->listener_forward, sizeof (gfloat) * 3);
    if (out_up != NULL)
        memcpy (out_up, self->listener_up, sizeof (gfloat) * 3);
}

/*
 * lrg_audio_manager_play_sound_at:
 * Looks the sound up, makes an alias of it (fails without an audio
 * device or for an unloaded sound), spatialises and plays it.
 */
guint
lrg_audio_manager_play_sound_at (LrgAudioManager           *self,
                                 const gchar               *bank_name,
                                 const gchar               *sound_name,
                                 const gfloat              *position,
                                 gfloat                     volume,
                                 const LrgAudioAttenuation *attenuation)
{
    LrgSoundBank *bank;
    GrlSound *source;
    GrlSound *alias;
    LrgAudioVoice *voice;
    guint id;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), 0);
    g_return_val_if_fail (bank_name != NULL, 0);
    g_return_val_if_fail (sound_name != NULL, 0);

    bank = lrg_audio_manager_get_bank (self, bank_name);
    if (bank == NULL)
    {
        lrg_log_debug ("Sound bank '%s' not found", bank_name);
        return 0;
    }

    source = lrg_sound_bank_get (bank, sound_name);
    if (source == NULL)
    {
        lrg_log_debug ("Sound '%s' not found in bank '%s'", sound_name, bank_name);
        return 0;
    }

    alias = grl_sound_new_alias (source);
    if (alias == NULL)
    {
        lrg_log_debug ("Cannot create a voice of '%s/%s'", bank_name, sound_name);
        return 0;
    }

    /* the voice: owns the alias and a reference to the shared samples */
    voice = g_new0 (LrgAudioVoice, 1);
    voice->source = g_object_ref (source);
    voice->alias = alias;
    voice->volume = CLAMP (volume, 0.0f, 1.0f);
    voice->positional = (position != NULL);
    if (position != NULL)
        memcpy (voice->position, position, sizeof (gfloat) * 3);
    if (attenuation != NULL)
        voice->attenuation = *attenuation;
    else
        lrg_audio_attenuation_init_default (&voice->attenuation);

    /* ids are never 0 and skip any still in use after wrapping */
    do
    {
        id = self->next_voice++;
        if (self->next_voice == 0)
            self->next_voice = 1;
    }
    while (id == 0 || g_hash_table_contains (self->voices, GUINT_TO_POINTER (id)));

    g_hash_table_insert (self->voices, GUINT_TO_POINTER (id), voice);

    apply_voice (self, voice);
    grl_sound_play (voice->alias);

    g_signal_emit (self, signals[SIGNAL_SOUND_PLAYED], 0, bank_name, sound_name);
    return id;
}

/* lrg_audio_manager_set_voice_position: moves an emitter */
gboolean
lrg_audio_manager_set_voice_position (LrgAudioManager *self,
                                      guint            voice,
                                      const gfloat    *position)
{
    LrgAudioVoice *entry;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (position != NULL, FALSE);

    entry = lookup_voice (self, voice);
    if (entry == NULL)
        return FALSE;

    entry->positional = TRUE;
    memcpy (entry->position, position, sizeof (gfloat) * 3);
    apply_voice (self, entry);
    return TRUE;
}

/* lrg_audio_manager_get_voice_spatial: last applied gain and pan */
gboolean
lrg_audio_manager_get_voice_spatial (LrgAudioManager *self,
                                     guint            voice,
                                     gfloat          *out_gain,
                                     gfloat          *out_pan)
{
    LrgAudioVoice *entry;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);

    entry = lookup_voice (self, voice);
    if (entry == NULL)
        return FALSE;

    if (out_gain != NULL)
        *out_gain = entry->gain;
    if (out_pan != NULL)
        *out_pan = entry->pan;
    return TRUE;
}

/* lrg_audio_manager_stop_voice: stop and release at once */
gboolean
lrg_audio_manager_stop_voice (LrgAudioManager *self,
                              guint            voice)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);

    if (voice == 0)
        return FALSE;
    return g_hash_table_remove (self->voices, GUINT_TO_POINTER (voice));
}

/* lrg_audio_manager_is_voice_playing: alive and still sounding */
gboolean
lrg_audio_manager_is_voice_playing (LrgAudioManager *self,
                                    guint            voice)
{
    LrgAudioVoice *entry;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);

    entry = lookup_voice (self, voice);
    if (entry == NULL || entry->alias == NULL)
        return FALSE;
    return grl_sound_is_playing (entry->alias);
}

/* lrg_audio_manager_get_voice_count: voices not yet released */
guint
lrg_audio_manager_get_voice_count (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), 0);

    return g_hash_table_size (self->voices);
}

/* lrg_audio_manager_stop_all_voices: releases every voice */
void
lrg_audio_manager_stop_all_voices (LrgAudioManager *self)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    g_hash_table_remove_all (self->voices);
}

/* ==========================================================================
 * Music Playback
 * ========================================================================== */

/*
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

/*
 * lrg_audio_manager_play_music_from_file:
 * @self: the audio manager
 * @path: path to the music file
 * @error: return location for a #GError
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/* ==========================================================================
 * Procedural Audio Management
 * ========================================================================== */

/*
 * lrg_audio_manager_add_procedural:
 * @self: the audio manager
 * @name: a unique name for the procedural audio
 * @audio: the procedural audio source to add
 *
 * Registers a procedural audio source with the manager.
 */
void
lrg_audio_manager_add_procedural (LrgAudioManager    *self,
                                   const gchar        *name,
                                   LrgProceduralAudio *audio)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (audio));

    /* Set the name on the audio if not already set */
    if (lrg_procedural_audio_get_name (audio) == NULL)
        lrg_procedural_audio_set_name (audio, name);

    g_hash_table_insert (self->procedural,
                         g_strdup (name),
                         g_object_ref (audio));

    lrg_log_debug ("Added procedural audio '%s'", name);
}

/*
 * lrg_audio_manager_remove_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio to remove
 *
 * Removes a procedural audio source from the manager.
 *
 * Returns: %TRUE if the source was found and removed
 */
gboolean
lrg_audio_manager_remove_procedural (LrgAudioManager *self,
                                      const gchar     *name)
{
    LrgProceduralAudio *audio;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    /* Stop the audio if playing before removing */
    audio = g_hash_table_lookup (self->procedural, name);
    if (audio != NULL && lrg_procedural_audio_is_playing (audio))
    {
        lrg_procedural_audio_stop (audio);
    }

    return g_hash_table_remove (self->procedural, name);
}

/*
 * lrg_audio_manager_get_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio
 *
 * Gets a registered procedural audio source by name.
 *
 * Returns: (transfer none) (nullable): the procedural audio, or %NULL if not found
 */
LrgProceduralAudio *
lrg_audio_manager_get_procedural (LrgAudioManager *self,
                                   const gchar     *name)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_hash_table_lookup (self->procedural, name);
}

/*
 * lrg_audio_manager_get_procedural_names:
 * @self: the audio manager
 *
 * Gets a list of all registered procedural audio names.
 *
 * Returns: (transfer container) (element-type utf8): list of names
 */
GList *
lrg_audio_manager_get_procedural_names (LrgAudioManager *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), NULL);

    return g_hash_table_get_keys (self->procedural);
}

/*
 * lrg_audio_manager_play_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio to play
 *
 * Starts playing a registered procedural audio source.
 *
 * Returns: %TRUE if the source was found and started
 */
gboolean
lrg_audio_manager_play_procedural (LrgAudioManager *self,
                                    const gchar     *name)
{
    LrgProceduralAudio *audio;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    audio = lrg_audio_manager_get_procedural (self, name);
    if (audio == NULL)
    {
        lrg_log_warning ("Procedural audio '%s' not found", name);
        return FALSE;
    }

    lrg_procedural_audio_play (audio);
    return TRUE;
}

/*
 * lrg_audio_manager_stop_procedural:
 * @self: the audio manager
 * @name: the name of the procedural audio to stop
 *
 * Stops a playing procedural audio source.
 *
 * Returns: %TRUE if the source was found and stopped
 */
gboolean
lrg_audio_manager_stop_procedural (LrgAudioManager *self,
                                    const gchar     *name)
{
    LrgProceduralAudio *audio;

    g_return_val_if_fail (LRG_IS_AUDIO_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    audio = lrg_audio_manager_get_procedural (self, name);
    if (audio == NULL)
        return FALSE;

    lrg_procedural_audio_stop (audio);
    return TRUE;
}

static void
stop_procedural_foreach (gpointer key     G_GNUC_UNUSED,
                         gpointer value,
                         gpointer user_data G_GNUC_UNUSED)
{
    LrgProceduralAudio *audio = LRG_PROCEDURAL_AUDIO (value);
    lrg_procedural_audio_stop (audio);
}

/*
 * lrg_audio_manager_stop_all_procedural:
 * @self: the audio manager
 *
 * Stops all playing procedural audio sources.
 */
void
lrg_audio_manager_stop_all_procedural (LrgAudioManager *self)
{
    g_return_if_fail (LRG_IS_AUDIO_MANAGER (self));

    g_hash_table_foreach (self->procedural, stop_procedural_foreach, NULL);
}
