/* lrg-music-track.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"
#include "lrg-music-track.h"
#include "../lrg-log.h"

/* Private structure */
struct _LrgMusicTrack
{
    GObject parent_instance;

    GrlMusic   *music;
    gchar      *name;

    /* Loop points (-1 means use default) */
    gfloat      loop_start;
    gfloat      loop_end;
    gboolean    has_loop_points;

    /* Volume and effects */
    gfloat      volume;
    gfloat      pitch;
    gboolean    looping;

    /* Crossfade support */
    gfloat      fade_in;
    gfloat      fade_out;

    /* Internal state for fading */
    gfloat      target_volume;
    gfloat      fade_timer;
    gfloat      fade_duration;
    gboolean    fading_in;
    gboolean    fading_out;
};

G_DEFINE_FINAL_TYPE (LrgMusicTrack, lrg_music_track, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MUSIC,
    PROP_NAME,
    PROP_VOLUME,
    PROP_PITCH,
    PROP_LOOPING,
    PROP_LOOP_START,
    PROP_LOOP_END,
    PROP_FADE_IN,
    PROP_FADE_OUT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_music_track_finalize (GObject *object)
{
    LrgMusicTrack *self = LRG_MUSIC_TRACK (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_object (&self->music);

    G_OBJECT_CLASS (lrg_music_track_parent_class)->finalize (object);
}

static void
lrg_music_track_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgMusicTrack *self = LRG_MUSIC_TRACK (object);

    switch (prop_id)
    {
    case PROP_MUSIC:
        g_value_set_object (value, self->music);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_VOLUME:
        g_value_set_float (value, self->volume);
        break;
    case PROP_PITCH:
        g_value_set_float (value, self->pitch);
        break;
    case PROP_LOOPING:
        g_value_set_boolean (value, self->looping);
        break;
    case PROP_LOOP_START:
        g_value_set_float (value, self->loop_start);
        break;
    case PROP_LOOP_END:
        g_value_set_float (value, self->loop_end);
        break;
    case PROP_FADE_IN:
        g_value_set_float (value, self->fade_in);
        break;
    case PROP_FADE_OUT:
        g_value_set_float (value, self->fade_out);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_music_track_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgMusicTrack *self = LRG_MUSIC_TRACK (object);

    switch (prop_id)
    {
    case PROP_MUSIC:
        g_clear_object (&self->music);
        self->music = g_value_dup_object (value);
        break;
    case PROP_NAME:
        lrg_music_track_set_name (self, g_value_get_string (value));
        break;
    case PROP_VOLUME:
        lrg_music_track_set_volume (self, g_value_get_float (value));
        break;
    case PROP_PITCH:
        lrg_music_track_set_pitch (self, g_value_get_float (value));
        break;
    case PROP_LOOPING:
        lrg_music_track_set_looping (self, g_value_get_boolean (value));
        break;
    case PROP_LOOP_START:
        self->loop_start = g_value_get_float (value);
        self->has_loop_points = (self->loop_start >= 0 || self->loop_end >= 0);
        break;
    case PROP_LOOP_END:
        self->loop_end = g_value_get_float (value);
        self->has_loop_points = (self->loop_start >= 0 || self->loop_end >= 0);
        break;
    case PROP_FADE_IN:
        self->fade_in = g_value_get_float (value);
        break;
    case PROP_FADE_OUT:
        self->fade_out = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_music_track_class_init (LrgMusicTrackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_music_track_finalize;
    object_class->get_property = lrg_music_track_get_property;
    object_class->set_property = lrg_music_track_set_property;

    /**
     * LrgMusicTrack:music:
     *
     * The underlying GrlMusic object.
     */
    properties[PROP_MUSIC] =
        g_param_spec_object ("music",
                             "Music",
                             "The underlying GrlMusic object",
                             GRL_TYPE_MUSIC,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:name:
     *
     * The track name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "The track name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:volume:
     *
     * The track volume.
     */
    properties[PROP_VOLUME] =
        g_param_spec_float ("volume",
                            "Volume",
                            "The track volume",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:pitch:
     *
     * The track pitch multiplier.
     */
    properties[PROP_PITCH] =
        g_param_spec_float ("pitch",
                            "Pitch",
                            "The track pitch multiplier",
                            0.1f, 4.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:looping:
     *
     * Whether the track loops.
     */
    properties[PROP_LOOPING] =
        g_param_spec_boolean ("looping",
                              "Looping",
                              "Whether the track loops",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:loop-start:
     *
     * The loop start point in seconds (-1 = beginning).
     */
    properties[PROP_LOOP_START] =
        g_param_spec_float ("loop-start",
                            "Loop Start",
                            "The loop start point in seconds",
                            -1.0f, G_MAXFLOAT, -1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:loop-end:
     *
     * The loop end point in seconds (-1 = end of track).
     */
    properties[PROP_LOOP_END] =
        g_param_spec_float ("loop-end",
                            "Loop End",
                            "The loop end point in seconds",
                            -1.0f, G_MAXFLOAT, -1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:fade-in:
     *
     * The fade-in duration in seconds.
     */
    properties[PROP_FADE_IN] =
        g_param_spec_float ("fade-in",
                            "Fade In",
                            "The fade-in duration in seconds",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgMusicTrack:fade-out:
     *
     * The fade-out duration in seconds.
     */
    properties[PROP_FADE_OUT] =
        g_param_spec_float ("fade-out",
                            "Fade Out",
                            "The fade-out duration in seconds",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_music_track_init (LrgMusicTrack *self)
{
    self->volume = 1.0f;
    self->pitch = 1.0f;
    self->looping = TRUE;
    self->loop_start = -1.0f;
    self->loop_end = -1.0f;
    self->has_loop_points = FALSE;
    self->fade_in = 0.0f;
    self->fade_out = 0.0f;
    self->target_volume = 1.0f;
    self->fade_timer = 0.0f;
    self->fade_duration = 0.0f;
    self->fading_in = FALSE;
    self->fading_out = FALSE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_music_track_new:
 * @music: the underlying GrlMusic
 *
 * Creates a new music track wrapping the given GrlMusic.
 *
 * Returns: (transfer full): A new #LrgMusicTrack
 */
LrgMusicTrack *
lrg_music_track_new (GrlMusic *music)
{
    g_return_val_if_fail (GRL_IS_MUSIC (music), NULL);

    return g_object_new (LRG_TYPE_MUSIC_TRACK,
                         "music", music,
                         NULL);
}

/**
 * lrg_music_track_new_from_file:
 * @path: path to the music file
 * @error: (optional): return location for a #GError
 *
 * Loads a music track from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgMusicTrack, or %NULL on error
 */
LrgMusicTrack *
lrg_music_track_new_from_file (const gchar  *path,
                                GError      **error)
{
    g_autoptr(GrlMusic) music = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    music = grl_music_new_from_file (path, error);
    if (music == NULL)
        return NULL;

    return lrg_music_track_new (music);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_music_track_get_music:
 * @self: a #LrgMusicTrack
 *
 * Gets the underlying GrlMusic.
 *
 * Returns: (transfer none): the GrlMusic
 */
GrlMusic *
lrg_music_track_get_music (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), NULL);

    return self->music;
}

/**
 * lrg_music_track_get_name:
 * @self: a #LrgMusicTrack
 *
 * Gets the track name.
 *
 * Returns: (transfer none) (nullable): the track name
 */
const gchar *
lrg_music_track_get_name (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), NULL);

    return self->name;
}

/**
 * lrg_music_track_set_name:
 * @self: a #LrgMusicTrack
 * @name: (nullable): the track name
 *
 * Sets the track name.
 */
void
lrg_music_track_set_name (LrgMusicTrack *self,
                           const gchar   *name)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    if (g_strcmp0 (self->name, name) != 0)
    {
        g_free (self->name);
        self->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

/* ==========================================================================
 * Loop Points
 * ========================================================================== */

/**
 * lrg_music_track_set_loop_points:
 * @self: a #LrgMusicTrack
 * @start: loop start in seconds (negative means beginning)
 * @end: loop end in seconds (negative means end of track)
 *
 * Sets custom loop points for the music.
 */
void
lrg_music_track_set_loop_points (LrgMusicTrack *self,
                                  gfloat         start,
                                  gfloat         end)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    self->loop_start = start;
    self->loop_end = end;
    self->has_loop_points = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_START]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_END]);
}

/**
 * lrg_music_track_get_loop_start:
 * @self: a #LrgMusicTrack
 *
 * Gets the loop start point.
 *
 * Returns: the loop start in seconds (-1 means beginning)
 */
gfloat
lrg_music_track_get_loop_start (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), -1.0f);

    return self->loop_start;
}

/**
 * lrg_music_track_get_loop_end:
 * @self: a #LrgMusicTrack
 *
 * Gets the loop end point.
 *
 * Returns: the loop end in seconds (-1 means end of track)
 */
gfloat
lrg_music_track_get_loop_end (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), -1.0f);

    return self->loop_end;
}

/**
 * lrg_music_track_clear_loop_points:
 * @self: a #LrgMusicTrack
 *
 * Clears custom loop points, reverting to default looping.
 */
void
lrg_music_track_clear_loop_points (LrgMusicTrack *self)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    self->loop_start = -1.0f;
    self->loop_end = -1.0f;
    self->has_loop_points = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_START]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_END]);
}

/**
 * lrg_music_track_has_loop_points:
 * @self: a #LrgMusicTrack
 *
 * Checks if custom loop points are set.
 *
 * Returns: %TRUE if custom loop points are set
 */
gboolean
lrg_music_track_has_loop_points (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), FALSE);

    return self->has_loop_points;
}

/* ==========================================================================
 * Playback Control
 * ========================================================================== */

/**
 * lrg_music_track_play:
 * @self: a #LrgMusicTrack
 *
 * Starts playing the music track.
 */
void
lrg_music_track_play (LrgMusicTrack *self)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));
    g_return_if_fail (self->music != NULL);

    /* Handle fade-in */
    if (self->fade_in > 0.0f)
    {
        self->target_volume = self->volume;
        grl_music_set_volume (self->music, 0.0f);
        self->fade_timer = 0.0f;
        self->fade_duration = self->fade_in;
        self->fading_in = TRUE;
        self->fading_out = FALSE;
    }
    else
    {
        grl_music_set_volume (self->music, self->volume);
    }

    grl_music_set_pitch (self->music, self->pitch);
    grl_music_set_looping (self->music, self->looping && !self->has_loop_points);
    grl_music_play (self->music);
}

/**
 * lrg_music_track_stop:
 * @self: a #LrgMusicTrack
 *
 * Stops the music track and resets to the beginning.
 */
void
lrg_music_track_stop (LrgMusicTrack *self)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));
    g_return_if_fail (self->music != NULL);

    self->fading_in = FALSE;
    self->fading_out = FALSE;
    grl_music_stop (self->music);
}

/**
 * lrg_music_track_pause:
 * @self: a #LrgMusicTrack
 *
 * Pauses the music track.
 */
void
lrg_music_track_pause (LrgMusicTrack *self)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));
    g_return_if_fail (self->music != NULL);

    grl_music_pause (self->music);
}

/**
 * lrg_music_track_resume:
 * @self: a #LrgMusicTrack
 *
 * Resumes a paused music track.
 */
void
lrg_music_track_resume (LrgMusicTrack *self)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));
    g_return_if_fail (self->music != NULL);

    grl_music_resume (self->music);
}

/**
 * lrg_music_track_update:
 * @self: a #LrgMusicTrack
 *
 * Updates the music stream and handles loop point checking.
 *
 * This must be called every frame while music is playing.
 */
void
lrg_music_track_update (LrgMusicTrack *self)
{
    gfloat current_position;
    gfloat effective_loop_end;
    gfloat effective_loop_start;
    gfloat dt;
    gfloat progress;
    gfloat new_volume;

    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));
    g_return_if_fail (self->music != NULL);

    /* Update the underlying music stream */
    grl_music_update (self->music);

    /* Handle fade-in */
    if (self->fading_in)
    {
        /* Estimate dt as 1/60th of a second (typical frame time) */
        dt = 1.0f / 60.0f;
        self->fade_timer += dt;

        if (self->fade_timer >= self->fade_duration)
        {
            grl_music_set_volume (self->music, self->target_volume);
            self->fading_in = FALSE;
        }
        else
        {
            progress = self->fade_timer / self->fade_duration;
            new_volume = progress * self->target_volume;
            grl_music_set_volume (self->music, new_volume);
        }
    }

    /* Handle fade-out */
    if (self->fading_out)
    {
        dt = 1.0f / 60.0f;
        self->fade_timer += dt;

        if (self->fade_timer >= self->fade_duration)
        {
            grl_music_stop (self->music);
            self->fading_out = FALSE;
        }
        else
        {
            progress = 1.0f - (self->fade_timer / self->fade_duration);
            new_volume = progress * self->target_volume;
            grl_music_set_volume (self->music, new_volume);
        }
    }

    /* Handle custom loop points */
    if (self->has_loop_points && self->looping && grl_music_is_playing (self->music))
    {
        current_position = grl_music_get_time_played (self->music);
        effective_loop_end = (self->loop_end >= 0.0f) ?
                             self->loop_end :
                             grl_music_get_time_length (self->music);
        effective_loop_start = (self->loop_start >= 0.0f) ?
                               self->loop_start :
                               0.0f;

        if (current_position >= effective_loop_end)
        {
            grl_music_seek (self->music, effective_loop_start);
            lrg_log_debug ("Looped track '%s' from %.2f to %.2f",
                          self->name ? self->name : "(unnamed)",
                          effective_loop_end, effective_loop_start);
        }
    }
}

/**
 * lrg_music_track_is_playing:
 * @self: a #LrgMusicTrack
 *
 * Checks if the music track is currently playing.
 *
 * Returns: %TRUE if playing
 */
gboolean
lrg_music_track_is_playing (LrgMusicTrack *self)
{
    unsigned char raw;

    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), FALSE);

    if (self->music == NULL)
        return FALSE;

    raw = grl_music_is_playing (self->music);
    return raw != 0;
}

/* ==========================================================================
 * Looping
 * ========================================================================== */

/**
 * lrg_music_track_set_looping:
 * @self: a #LrgMusicTrack
 * @looping: whether to loop
 *
 * Sets whether the music should loop.
 */
void
lrg_music_track_set_looping (LrgMusicTrack *self,
                              gboolean       looping)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    if (self->looping != looping)
    {
        self->looping = looping;

        /* If we have custom loop points, we handle looping manually */
        if (self->music != NULL && !self->has_loop_points)
            grl_music_set_looping (self->music, looping);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOPING]);
    }
}

/**
 * lrg_music_track_get_looping:
 * @self: a #LrgMusicTrack
 *
 * Gets whether the music loops.
 *
 * Returns: %TRUE if looping
 */
gboolean
lrg_music_track_get_looping (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), FALSE);

    return self->looping;
}

/* ==========================================================================
 * Position and Duration
 * ========================================================================== */

/**
 * lrg_music_track_seek:
 * @self: a #LrgMusicTrack
 * @position: position in seconds
 *
 * Seeks to a position in the track.
 */
void
lrg_music_track_seek (LrgMusicTrack *self,
                       gfloat         position)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));
    g_return_if_fail (self->music != NULL);

    grl_music_seek (self->music, position);
}

/**
 * lrg_music_track_get_position:
 * @self: a #LrgMusicTrack
 *
 * Gets the current playback position.
 *
 * Returns: position in seconds
 */
gfloat
lrg_music_track_get_position (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), 0.0f);

    if (self->music == NULL)
        return 0.0f;

    return grl_music_get_time_played (self->music);
}

/**
 * lrg_music_track_get_duration:
 * @self: a #LrgMusicTrack
 *
 * Gets the total track duration.
 *
 * Returns: duration in seconds
 */
gfloat
lrg_music_track_get_duration (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), 0.0f);

    if (self->music == NULL)
        return 0.0f;

    return grl_music_get_time_length (self->music);
}

/* ==========================================================================
 * Volume and Effects
 * ========================================================================== */

/**
 * lrg_music_track_set_volume:
 * @self: a #LrgMusicTrack
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the track volume.
 */
void
lrg_music_track_set_volume (LrgMusicTrack *self,
                             gfloat         volume)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    volume = CLAMP (volume, 0.0f, 1.0f);

    if (self->volume != volume)
    {
        self->volume = volume;
        self->target_volume = volume;

        /* Only update if not fading */
        if (!self->fading_in && !self->fading_out && self->music != NULL)
            grl_music_set_volume (self->music, volume);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOLUME]);
    }
}

/**
 * lrg_music_track_get_volume:
 * @self: a #LrgMusicTrack
 *
 * Gets the track volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
gfloat
lrg_music_track_get_volume (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), 1.0f);

    return self->volume;
}

/**
 * lrg_music_track_set_pitch:
 * @self: a #LrgMusicTrack
 * @pitch: pitch multiplier (1.0 = normal)
 *
 * Sets the track pitch.
 */
void
lrg_music_track_set_pitch (LrgMusicTrack *self,
                            gfloat         pitch)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    pitch = CLAMP (pitch, 0.1f, 4.0f);

    if (self->pitch != pitch)
    {
        self->pitch = pitch;

        if (self->music != NULL)
            grl_music_set_pitch (self->music, pitch);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
    }
}

/**
 * lrg_music_track_get_pitch:
 * @self: a #LrgMusicTrack
 *
 * Gets the track pitch.
 *
 * Returns: pitch multiplier
 */
gfloat
lrg_music_track_get_pitch (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), 1.0f);

    return self->pitch;
}

/* ==========================================================================
 * Crossfade Support
 * ========================================================================== */

/**
 * lrg_music_track_set_fade_in:
 * @self: a #LrgMusicTrack
 * @duration: fade-in duration in seconds
 *
 * Sets the fade-in duration when starting the track.
 */
void
lrg_music_track_set_fade_in (LrgMusicTrack *self,
                              gfloat         duration)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    duration = MAX (0.0f, duration);

    if (self->fade_in != duration)
    {
        self->fade_in = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FADE_IN]);
    }
}

/**
 * lrg_music_track_get_fade_in:
 * @self: a #LrgMusicTrack
 *
 * Gets the fade-in duration.
 *
 * Returns: fade-in duration in seconds
 */
gfloat
lrg_music_track_get_fade_in (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), 0.0f);

    return self->fade_in;
}

/**
 * lrg_music_track_set_fade_out:
 * @self: a #LrgMusicTrack
 * @duration: fade-out duration in seconds
 *
 * Sets the fade-out duration when stopping the track.
 */
void
lrg_music_track_set_fade_out (LrgMusicTrack *self,
                               gfloat         duration)
{
    g_return_if_fail (LRG_IS_MUSIC_TRACK (self));

    duration = MAX (0.0f, duration);

    if (self->fade_out != duration)
    {
        self->fade_out = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FADE_OUT]);
    }
}

/**
 * lrg_music_track_get_fade_out:
 * @self: a #LrgMusicTrack
 *
 * Gets the fade-out duration.
 *
 * Returns: fade-out duration in seconds
 */
gfloat
lrg_music_track_get_fade_out (LrgMusicTrack *self)
{
    g_return_val_if_fail (LRG_IS_MUSIC_TRACK (self), 0.0f);

    return self->fade_out;
}
