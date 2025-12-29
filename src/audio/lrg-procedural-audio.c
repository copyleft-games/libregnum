/* lrg-procedural-audio.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-procedural-audio.h"
#include "../lrg-log.h"

/* Default buffer size for procedural audio (frames per update) */
#define DEFAULT_BUFFER_SIZE 4096

typedef struct
{
    GrlAudioStream *stream;
    gchar          *name;
    guint           sample_rate;
    guint           channels;
    gfloat         *buffer;
    gsize           buffer_size;
} LrgProceduralAudioPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgProceduralAudio, lrg_procedural_audio, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_SAMPLE_RATE,
    PROP_CHANNELS,
    PROP_VOLUME,
    PROP_PITCH,
    PROP_PAN,
    PROP_PLAYING,
    PROP_VALID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default generate() implementation - produces silence
 * ========================================================================== */

static void
lrg_procedural_audio_real_generate (LrgProceduralAudio *self,
                                    gfloat             *buffer,
                                    gint                frame_count)
{
    LrgProceduralAudioPrivate *priv;
    gsize                      sample_count;

    priv = lrg_procedural_audio_get_instance_private (self);
    sample_count = (gsize)frame_count * priv->channels;

    /* Default implementation produces silence */
    memset (buffer, 0, sample_count * sizeof (gfloat));
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_procedural_audio_constructed (GObject *object)
{
    LrgProceduralAudio        *self = LRG_PROCEDURAL_AUDIO (object);
    LrgProceduralAudioPrivate *priv = lrg_procedural_audio_get_instance_private (self);

    G_OBJECT_CLASS (lrg_procedural_audio_parent_class)->constructed (object);

    /* Create the underlying audio stream (32-bit float samples) */
    priv->stream = grl_audio_stream_new (priv->sample_rate, 32, priv->channels);
    if (priv->stream == NULL)
    {
        /*
         * Use debug level since this is expected to fail in headless
         * environments without audio devices.
         */
        lrg_debug (LRG_LOG_DOMAIN_AUDIO,
                   "Failed to create audio stream for procedural audio");
        return;
    }

    /* Allocate buffer for generate() calls */
    priv->buffer_size = DEFAULT_BUFFER_SIZE * priv->channels;
    priv->buffer = g_new0 (gfloat, priv->buffer_size);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO,
               "Created procedural audio: %u Hz, %u channels",
               priv->sample_rate, priv->channels);
}

static void
lrg_procedural_audio_finalize (GObject *object)
{
    LrgProceduralAudio        *self = LRG_PROCEDURAL_AUDIO (object);
    LrgProceduralAudioPrivate *priv = lrg_procedural_audio_get_instance_private (self);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO,
                   "Finalizing procedural audio: %s",
                   priv->name ? priv->name : "(unnamed)");

    /* Stop playback before cleanup */
    if (priv->stream != NULL && grl_audio_stream_is_playing (priv->stream))
    {
        grl_audio_stream_stop (priv->stream);
    }

    g_clear_object (&priv->stream);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->buffer, g_free);

    G_OBJECT_CLASS (lrg_procedural_audio_parent_class)->finalize (object);
}

static void
lrg_procedural_audio_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgProceduralAudio        *self = LRG_PROCEDURAL_AUDIO (object);
    LrgProceduralAudioPrivate *priv = lrg_procedural_audio_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_SAMPLE_RATE:
        g_value_set_uint (value, priv->sample_rate);
        break;
    case PROP_CHANNELS:
        g_value_set_uint (value, priv->channels);
        break;
    case PROP_VOLUME:
        g_value_set_float (value, lrg_procedural_audio_get_volume (self));
        break;
    case PROP_PITCH:
        g_value_set_float (value, lrg_procedural_audio_get_pitch (self));
        break;
    case PROP_PAN:
        g_value_set_float (value, lrg_procedural_audio_get_pan (self));
        break;
    case PROP_PLAYING:
        g_value_set_boolean (value, lrg_procedural_audio_is_playing (self));
        break;
    case PROP_VALID:
        g_value_set_boolean (value, lrg_procedural_audio_is_valid (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_procedural_audio_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgProceduralAudio        *self = LRG_PROCEDURAL_AUDIO (object);
    LrgProceduralAudioPrivate *priv = lrg_procedural_audio_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        lrg_procedural_audio_set_name (self, g_value_get_string (value));
        break;
    case PROP_SAMPLE_RATE:
        priv->sample_rate = g_value_get_uint (value);
        break;
    case PROP_CHANNELS:
        priv->channels = g_value_get_uint (value);
        break;
    case PROP_VOLUME:
        lrg_procedural_audio_set_volume (self, g_value_get_float (value));
        break;
    case PROP_PITCH:
        lrg_procedural_audio_set_pitch (self, g_value_get_float (value));
        break;
    case PROP_PAN:
        lrg_procedural_audio_set_pan (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_procedural_audio_class_init (LrgProceduralAudioClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = lrg_procedural_audio_constructed;
    object_class->finalize = lrg_procedural_audio_finalize;
    object_class->get_property = lrg_procedural_audio_get_property;
    object_class->set_property = lrg_procedural_audio_set_property;

    /* Set default virtual method implementation */
    klass->generate = lrg_procedural_audio_real_generate;

    /**
     * LrgProceduralAudio:name:
     *
     * Optional name identifier for debugging.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Optional name identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProceduralAudio:sample-rate:
     *
     * The audio sample rate in Hz.
     */
    properties[PROP_SAMPLE_RATE] =
        g_param_spec_uint ("sample-rate",
                           "Sample Rate",
                           "Audio sample rate in Hz",
                           8000, 192000, 44100,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgProceduralAudio:channels:
     *
     * Number of audio channels (1 = mono, 2 = stereo).
     */
    properties[PROP_CHANNELS] =
        g_param_spec_uint ("channels",
                           "Channels",
                           "Number of audio channels",
                           1, 8, 2,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgProceduralAudio:volume:
     *
     * Playback volume (0.0 to 1.0).
     */
    properties[PROP_VOLUME] =
        g_param_spec_float ("volume",
                            "Volume",
                            "Playback volume",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProceduralAudio:pitch:
     *
     * Playback pitch multiplier (1.0 = normal).
     */
    properties[PROP_PITCH] =
        g_param_spec_float ("pitch",
                            "Pitch",
                            "Pitch multiplier",
                            0.1f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProceduralAudio:pan:
     *
     * Stereo pan position (-1.0 = left, 0.0 = center, 1.0 = right).
     */
    properties[PROP_PAN] =
        g_param_spec_float ("pan",
                            "Pan",
                            "Stereo pan position",
                            -1.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProceduralAudio:playing:
     *
     * Whether the audio is currently playing.
     */
    properties[PROP_PLAYING] =
        g_param_spec_boolean ("playing",
                              "Playing",
                              "Whether audio is playing",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgProceduralAudio:valid:
     *
     * Whether the audio stream is valid.
     */
    properties[PROP_VALID] =
        g_param_spec_boolean ("valid",
                              "Valid",
                              "Whether stream is valid",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procedural_audio_init (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv = lrg_procedural_audio_get_instance_private (self);

    priv->stream = NULL;
    priv->name = NULL;
    priv->sample_rate = 44100;
    priv->channels = 2;
    priv->buffer = NULL;
    priv->buffer_size = 0;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_procedural_audio_new:
 * @sample_rate: sample rate in Hz (e.g., 44100, 48000)
 * @channels: number of channels (1 = mono, 2 = stereo)
 *
 * Creates a new procedural audio source.
 *
 * Returns: (transfer full) (nullable): A new #LrgProceduralAudio
 */
LrgProceduralAudio *
lrg_procedural_audio_new (guint sample_rate,
                          guint channels)
{
    LrgProceduralAudio        *self;
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (sample_rate >= 8000 && sample_rate <= 192000, NULL);
    g_return_val_if_fail (channels >= 1 && channels <= 8, NULL);

    self = g_object_new (LRG_TYPE_PROCEDURAL_AUDIO,
                         "sample-rate", sample_rate,
                         "channels", channels,
                         NULL);

    /* Check if stream creation succeeded (happens in constructed) */
    priv = lrg_procedural_audio_get_instance_private (self);
    if (priv->stream == NULL)
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_procedural_audio_get_name:
 * @self: a #LrgProceduralAudio
 *
 * Gets the optional name identifier.
 *
 * Returns: (transfer none) (nullable): the name, or %NULL
 */
const gchar *
lrg_procedural_audio_get_name (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), NULL);

    priv = lrg_procedural_audio_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_procedural_audio_set_name:
 * @self: a #LrgProceduralAudio
 * @name: (nullable): the name identifier
 *
 * Sets an optional name identifier for debugging.
 */
void
lrg_procedural_audio_set_name (LrgProceduralAudio *self,
                               const gchar        *name)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) == 0)
        return;

    g_free (priv->name);
    priv->name = g_strdup (name);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

/**
 * lrg_procedural_audio_get_sample_rate:
 * @self: a #LrgProceduralAudio
 *
 * Gets the sample rate.
 *
 * Returns: the sample rate in Hz
 */
guint
lrg_procedural_audio_get_sample_rate (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), 0);

    priv = lrg_procedural_audio_get_instance_private (self);
    return priv->sample_rate;
}

/**
 * lrg_procedural_audio_get_channels:
 * @self: a #LrgProceduralAudio
 *
 * Gets the number of audio channels.
 *
 * Returns: the channel count (1 = mono, 2 = stereo)
 */
guint
lrg_procedural_audio_get_channels (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), 0);

    priv = lrg_procedural_audio_get_instance_private (self);
    return priv->channels;
}

/**
 * lrg_procedural_audio_is_valid:
 * @self: a #LrgProceduralAudio
 *
 * Checks if the audio stream is valid and ready for playback.
 *
 * Returns: %TRUE if valid
 */
gboolean
lrg_procedural_audio_is_valid (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), FALSE);

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return FALSE;

    return grl_audio_stream_is_valid (priv->stream);
}

/* ==========================================================================
 * Playback Control
 * ========================================================================== */

/**
 * lrg_procedural_audio_play:
 * @self: a #LrgProceduralAudio
 *
 * Starts playing the procedural audio.
 */
void
lrg_procedural_audio_play (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    grl_audio_stream_play (priv->stream);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO,
                   "Started procedural audio: %s",
                   priv->name ? priv->name : "(unnamed)");
}

/**
 * lrg_procedural_audio_stop:
 * @self: a #LrgProceduralAudio
 *
 * Stops the procedural audio playback.
 */
void
lrg_procedural_audio_stop (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    grl_audio_stream_stop (priv->stream);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);

    lrg_debug (LRG_LOG_DOMAIN_AUDIO,
                   "Stopped procedural audio: %s",
                   priv->name ? priv->name : "(unnamed)");
}

/**
 * lrg_procedural_audio_pause:
 * @self: a #LrgProceduralAudio
 *
 * Pauses the procedural audio playback.
 */
void
lrg_procedural_audio_pause (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    grl_audio_stream_pause (priv->stream);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

/**
 * lrg_procedural_audio_resume:
 * @self: a #LrgProceduralAudio
 *
 * Resumes paused procedural audio.
 */
void
lrg_procedural_audio_resume (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    grl_audio_stream_resume (priv->stream);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

/**
 * lrg_procedural_audio_is_playing:
 * @self: a #LrgProceduralAudio
 *
 * Checks if the audio is currently playing.
 *
 * Returns: %TRUE if playing
 */
gboolean
lrg_procedural_audio_is_playing (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), FALSE);

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return FALSE;

    return grl_audio_stream_is_playing (priv->stream);
}

/**
 * lrg_procedural_audio_update:
 * @self: a #LrgProceduralAudio
 *
 * Updates the audio stream by generating new samples if needed.
 */
void
lrg_procedural_audio_update (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;
    LrgProceduralAudioClass   *klass;
    gint                       frame_count;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL || !grl_audio_stream_is_playing (priv->stream))
        return;

    /* Check if the buffer needs more data */
    if (!grl_audio_stream_is_processed (priv->stream))
        return;

    /* Calculate frame count from buffer size */
    frame_count = (gint)(priv->buffer_size / priv->channels);
    if (frame_count > DEFAULT_BUFFER_SIZE)
        frame_count = DEFAULT_BUFFER_SIZE;

    /* Call the virtual generate() method */
    klass = LRG_PROCEDURAL_AUDIO_GET_CLASS (self);
    if (klass->generate != NULL)
    {
        klass->generate (self, priv->buffer, frame_count);
    }

    /* Feed the generated samples to the audio stream */
    grl_audio_stream_update (priv->stream, priv->buffer, frame_count);
}

/* ==========================================================================
 * Audio Parameters
 * ========================================================================== */

/**
 * lrg_procedural_audio_set_volume:
 * @self: a #LrgProceduralAudio
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the playback volume.
 */
void
lrg_procedural_audio_set_volume (LrgProceduralAudio *self,
                                 gfloat              volume)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    volume = CLAMP (volume, 0.0f, 1.0f);
    grl_audio_stream_set_volume (priv->stream, volume);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOLUME]);
}

/**
 * lrg_procedural_audio_get_volume:
 * @self: a #LrgProceduralAudio
 *
 * Gets the current volume level.
 *
 * Returns: the volume (0.0 to 1.0)
 */
gfloat
lrg_procedural_audio_get_volume (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), 0.0f);

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return 0.0f;

    return grl_audio_stream_get_volume (priv->stream);
}

/**
 * lrg_procedural_audio_set_pitch:
 * @self: a #LrgProceduralAudio
 * @pitch: pitch multiplier (1.0 = normal)
 *
 * Sets the playback pitch.
 */
void
lrg_procedural_audio_set_pitch (LrgProceduralAudio *self,
                                gfloat              pitch)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    pitch = CLAMP (pitch, 0.1f, 10.0f);
    grl_audio_stream_set_pitch (priv->stream, pitch);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
}

/**
 * lrg_procedural_audio_get_pitch:
 * @self: a #LrgProceduralAudio
 *
 * Gets the current pitch multiplier.
 *
 * Returns: the pitch multiplier
 */
gfloat
lrg_procedural_audio_get_pitch (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), 1.0f);

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return 1.0f;

    return grl_audio_stream_get_pitch (priv->stream);
}

/**
 * lrg_procedural_audio_set_pan:
 * @self: a #LrgProceduralAudio
 * @pan: pan position (-1.0 = left, 0.0 = center, 1.0 = right)
 *
 * Sets the stereo pan position.
 */
void
lrg_procedural_audio_set_pan (LrgProceduralAudio *self,
                              gfloat              pan)
{
    LrgProceduralAudioPrivate *priv;

    g_return_if_fail (LRG_IS_PROCEDURAL_AUDIO (self));

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return;

    pan = CLAMP (pan, -1.0f, 1.0f);
    grl_audio_stream_set_pan (priv->stream, pan);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAN]);
}

/**
 * lrg_procedural_audio_get_pan:
 * @self: a #LrgProceduralAudio
 *
 * Gets the current pan position.
 *
 * Returns: the pan position (-1.0 to 1.0)
 */
gfloat
lrg_procedural_audio_get_pan (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), 0.0f);

    priv = lrg_procedural_audio_get_instance_private (self);

    if (priv->stream == NULL)
        return 0.0f;

    return grl_audio_stream_get_pan (priv->stream);
}

/* ==========================================================================
 * Access Underlying
 * ========================================================================== */

/**
 * lrg_procedural_audio_get_audio_stream:
 * @self: a #LrgProceduralAudio
 *
 * Gets the underlying #GrlAudioStream.
 *
 * Returns: (transfer none) (nullable): the underlying #GrlAudioStream
 */
GrlAudioStream *
lrg_procedural_audio_get_audio_stream (LrgProceduralAudio *self)
{
    LrgProceduralAudioPrivate *priv;

    g_return_val_if_fail (LRG_IS_PROCEDURAL_AUDIO (self), NULL);

    priv = lrg_procedural_audio_get_instance_private (self);
    return priv->stream;
}
