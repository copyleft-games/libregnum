/* lrg-wave-data.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"
#include "lrg-wave-data.h"
#include "../lrg-log.h"

/* Private structure */
struct _LrgWaveData
{
    GObject  parent_instance;

    GrlWave *wave;          /* Underlying graylib wave */
    gchar   *name;          /* Optional identifier */
    gchar   *source_path;   /* Original file path if loaded from file */
};

G_DEFINE_FINAL_TYPE (LrgWaveData, lrg_wave_data, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-wave-data-error-quark, lrg_wave_data_error)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_SOURCE_PATH,
    PROP_FRAME_COUNT,
    PROP_SAMPLE_RATE,
    PROP_SAMPLE_SIZE,
    PROP_CHANNELS,
    PROP_DURATION,
    PROP_VALID,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_wave_data_finalize (GObject *object)
{
    LrgWaveData *self = LRG_WAVE_DATA (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->source_path, g_free);
    g_clear_pointer (&self->wave, grl_wave_free);

    G_OBJECT_CLASS (lrg_wave_data_parent_class)->finalize (object);
}

static void
lrg_wave_data_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgWaveData *self = LRG_WAVE_DATA (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_SOURCE_PATH:
        g_value_set_string (value, self->source_path);
        break;
    case PROP_FRAME_COUNT:
        g_value_set_uint (value, lrg_wave_data_get_frame_count (self));
        break;
    case PROP_SAMPLE_RATE:
        g_value_set_uint (value, lrg_wave_data_get_sample_rate (self));
        break;
    case PROP_SAMPLE_SIZE:
        g_value_set_uint (value, lrg_wave_data_get_sample_size (self));
        break;
    case PROP_CHANNELS:
        g_value_set_uint (value, lrg_wave_data_get_channels (self));
        break;
    case PROP_DURATION:
        g_value_set_float (value, lrg_wave_data_get_duration (self));
        break;
    case PROP_VALID:
        g_value_set_boolean (value, lrg_wave_data_is_valid (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_wave_data_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgWaveData *self = LRG_WAVE_DATA (object);

    switch (prop_id)
    {
    case PROP_NAME:
        lrg_wave_data_set_name (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_wave_data_class_init (LrgWaveDataClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_wave_data_finalize;
    object_class->get_property = lrg_wave_data_get_property;
    object_class->set_property = lrg_wave_data_set_property;

    /**
     * LrgWaveData:name:
     *
     * An optional name identifier.
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
     * LrgWaveData:source-path:
     *
     * The original file path if loaded from file.
     */
    properties[PROP_SOURCE_PATH] =
        g_param_spec_string ("source-path",
                             "Source Path",
                             "Original file path if loaded from file",
                             NULL,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveData:frame-count:
     *
     * The total number of frames.
     */
    properties[PROP_FRAME_COUNT] =
        g_param_spec_uint ("frame-count",
                           "Frame Count",
                           "Total number of frames",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveData:sample-rate:
     *
     * The sample rate in Hz.
     */
    properties[PROP_SAMPLE_RATE] =
        g_param_spec_uint ("sample-rate",
                           "Sample Rate",
                           "Sample rate in Hz",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveData:sample-size:
     *
     * The bits per sample (8, 16, or 32).
     */
    properties[PROP_SAMPLE_SIZE] =
        g_param_spec_uint ("sample-size",
                           "Sample Size",
                           "Bits per sample",
                           0, 32, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveData:channels:
     *
     * The number of audio channels.
     */
    properties[PROP_CHANNELS] =
        g_param_spec_uint ("channels",
                           "Channels",
                           "Number of audio channels",
                           0, 8, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveData:duration:
     *
     * The duration in seconds.
     */
    properties[PROP_DURATION] =
        g_param_spec_float ("duration",
                            "Duration",
                            "Duration in seconds",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgWaveData:valid:
     *
     * Whether the wave data is valid.
     */
    properties[PROP_VALID] =
        g_param_spec_boolean ("valid",
                              "Valid",
                              "Whether the wave data is valid",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_wave_data_init (LrgWaveData *self)
{
    self->wave = NULL;
    self->name = NULL;
    self->source_path = NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_wave_data_new_from_file:
 * @path: (type filename): path to the audio file
 * @error: (nullable): return location for a #GError
 *
 * Loads wave data from an audio file.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LrgWaveData *
lrg_wave_data_new_from_file (const gchar  *path,
                              GError      **error)
{
    LrgWaveData *self;
    g_autoptr(GrlWave) wave = NULL;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    wave = grl_wave_new_from_file (path, error);
    if (wave == NULL)
        return NULL;

    self = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    self->wave = g_steal_pointer (&wave);
    self->source_path = g_strdup (path);

    lrg_log_debug ("Loaded wave data from '%s' (%.2fs, %uHz, %u-bit, %uch)",
                  path,
                  lrg_wave_data_get_duration (self),
                  lrg_wave_data_get_sample_rate (self),
                  lrg_wave_data_get_sample_size (self),
                  lrg_wave_data_get_channels (self));

    return self;
}

/**
 * lrg_wave_data_new_from_memory:
 * @file_type: file type extension (e.g., ".wav", ".ogg")
 * @data: (array length=data_size): audio file data in memory
 * @data_size: size of @data in bytes
 * @error: (nullable): return location for a #GError
 *
 * Loads wave data from a memory buffer containing an audio file.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LrgWaveData *
lrg_wave_data_new_from_memory (const gchar  *file_type,
                                const guint8 *data,
                                gsize         data_size,
                                GError      **error)
{
    LrgWaveData *self;
    g_autoptr(GrlWave) wave = NULL;

    g_return_val_if_fail (file_type != NULL, NULL);
    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (data_size > 0, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    wave = grl_wave_new_from_memory (file_type, data, data_size, error);
    if (wave == NULL)
        return NULL;

    self = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    self->wave = g_steal_pointer (&wave);

    lrg_log_debug ("Loaded wave data from memory (%.2fs, %uHz, %u-bit, %uch)",
                  lrg_wave_data_get_duration (self),
                  lrg_wave_data_get_sample_rate (self),
                  lrg_wave_data_get_sample_size (self),
                  lrg_wave_data_get_channels (self));

    return self;
}

/**
 * lrg_wave_data_new_from_samples:
 * @sample_rate: sample rate in Hz (e.g., 44100)
 * @sample_size: bits per sample (8, 16, or 32)
 * @channels: number of channels (1 = mono, 2 = stereo)
 * @data: (array length=data_size): raw sample data
 * @data_size: size of @data in bytes
 *
 * Creates wave data from raw sample data.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LrgWaveData *
lrg_wave_data_new_from_samples (guint         sample_rate,
                                 guint         sample_size,
                                 guint         channels,
                                 const guint8 *data,
                                 gsize         data_size)
{
    LrgWaveData *self;
    GrlWave *wave;

    g_return_val_if_fail (sample_rate > 0, NULL);
    g_return_val_if_fail (sample_size == 8 || sample_size == 16 || sample_size == 32, NULL);
    g_return_val_if_fail (channels > 0 && channels <= 8, NULL);
    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (data_size > 0, NULL);

    wave = grl_wave_new_from_samples (sample_rate, sample_size, channels, data, data_size);
    if (wave == NULL)
        return NULL;

    self = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    self->wave = wave;

    return self;
}

/**
 * lrg_wave_data_new_procedural:
 * @sample_rate: sample rate in Hz (e.g., 44100)
 * @channels: number of channels (1 = mono, 2 = stereo)
 * @duration: duration in seconds
 *
 * Creates empty wave data for procedural generation.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData
 */
LrgWaveData *
lrg_wave_data_new_procedural (guint  sample_rate,
                               guint  channels,
                               gfloat duration)
{
    LrgWaveData *self;
    guint frame_count;
    gsize data_size;
    guint8 *data;

    g_return_val_if_fail (sample_rate > 0, NULL);
    g_return_val_if_fail (channels > 0 && channels <= 8, NULL);
    g_return_val_if_fail (duration > 0.0f, NULL);

    /* Calculate frame count and allocate zeroed buffer */
    frame_count = (guint)(sample_rate * duration);
    data_size = frame_count * channels * sizeof (gfloat);  /* 32-bit float format */
    data = g_malloc0 (data_size);

    self = lrg_wave_data_new_from_samples (sample_rate, 32, channels, data, data_size);
    g_free (data);

    return self;
}

/**
 * lrg_wave_data_new_from_grl_wave:
 * @wave: a #GrlWave
 *
 * Creates wave data from a graylib #GrlWave.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData
 */
LrgWaveData *
lrg_wave_data_new_from_grl_wave (GrlWave *wave)
{
    LrgWaveData *self;

    g_return_val_if_fail (wave != NULL, NULL);

    self = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    self->wave = grl_wave_copy (wave);

    return self;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_wave_data_get_name:
 * @self: a #LrgWaveData
 *
 * Gets the optional name identifier.
 *
 * Returns: (transfer none) (nullable): the name, or %NULL
 */
const gchar *
lrg_wave_data_get_name (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);

    return self->name;
}

/**
 * lrg_wave_data_set_name:
 * @self: a #LrgWaveData
 * @name: (nullable): the name identifier
 *
 * Sets an optional name identifier.
 */
void
lrg_wave_data_set_name (LrgWaveData *self,
                         const gchar *name)
{
    g_return_if_fail (LRG_IS_WAVE_DATA (self));

    if (g_strcmp0 (self->name, name) != 0)
    {
        g_free (self->name);
        self->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

/**
 * lrg_wave_data_get_source_path:
 * @self: a #LrgWaveData
 *
 * Gets the original file path if loaded from file.
 *
 * Returns: (transfer none) (nullable): the source path, or %NULL
 */
const gchar *
lrg_wave_data_get_source_path (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);

    return self->source_path;
}

/**
 * lrg_wave_data_get_frame_count:
 * @self: a #LrgWaveData
 *
 * Gets the total number of frames in the wave data.
 *
 * Returns: the frame count
 */
guint
lrg_wave_data_get_frame_count (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), 0);

    if (self->wave == NULL)
        return 0;

    return grl_wave_get_frame_count (self->wave);
}

/**
 * lrg_wave_data_get_sample_rate:
 * @self: a #LrgWaveData
 *
 * Gets the sample rate (frequency) of the wave data.
 *
 * Returns: the sample rate in Hz
 */
guint
lrg_wave_data_get_sample_rate (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), 0);

    if (self->wave == NULL)
        return 0;

    return grl_wave_get_sample_rate (self->wave);
}

/**
 * lrg_wave_data_get_sample_size:
 * @self: a #LrgWaveData
 *
 * Gets the bit depth of the wave samples.
 *
 * Returns: bits per sample (8, 16, or 32)
 */
guint
lrg_wave_data_get_sample_size (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), 0);

    if (self->wave == NULL)
        return 0;

    return grl_wave_get_sample_size (self->wave);
}

/**
 * lrg_wave_data_get_channels:
 * @self: a #LrgWaveData
 *
 * Gets the number of audio channels.
 *
 * Returns: the channel count (1 = mono, 2 = stereo)
 */
guint
lrg_wave_data_get_channels (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), 0);

    if (self->wave == NULL)
        return 0;

    return grl_wave_get_channels (self->wave);
}

/**
 * lrg_wave_data_get_duration:
 * @self: a #LrgWaveData
 *
 * Gets the duration of the wave data in seconds.
 *
 * Returns: the duration in seconds
 */
gfloat
lrg_wave_data_get_duration (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), 0.0f);

    if (self->wave == NULL)
        return 0.0f;

    return grl_wave_get_duration (self->wave);
}

/**
 * lrg_wave_data_is_valid:
 * @self: a #LrgWaveData
 *
 * Checks if the wave data is valid.
 *
 * Returns: %TRUE if the wave data is valid and ready to use
 */
gboolean
lrg_wave_data_is_valid (LrgWaveData *self)
{
    unsigned char raw;

    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), FALSE);

    if (self->wave == NULL)
        return FALSE;

    raw = grl_wave_is_valid (self->wave);
    return raw != 0;
}

/* ==========================================================================
 * Manipulation
 * ========================================================================== */

/**
 * lrg_wave_data_copy:
 * @self: a #LrgWaveData
 *
 * Creates a deep copy of the wave data.
 *
 * Returns: (transfer full): A new #LrgWaveData with copied data
 */
LrgWaveData *
lrg_wave_data_copy (LrgWaveData *self)
{
    LrgWaveData *copy;

    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);
    g_return_val_if_fail (self->wave != NULL, NULL);

    copy = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    copy->wave = grl_wave_copy (self->wave);
    copy->name = g_strdup (self->name);
    copy->source_path = g_strdup (self->source_path);

    return copy;
}

/**
 * lrg_wave_data_crop:
 * @self: a #LrgWaveData
 * @start_time: start time in seconds
 * @end_time: end time in seconds
 *
 * Creates a new wave containing only the specified time range.
 *
 * Returns: (transfer full): A new cropped #LrgWaveData
 */
LrgWaveData *
lrg_wave_data_crop (LrgWaveData *self,
                     gfloat       start_time,
                     gfloat       end_time)
{
    LrgWaveData *cropped;
    guint sample_rate;
    gint init_frame;
    gint final_frame;
    GrlWave *cropped_wave;

    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);
    g_return_val_if_fail (self->wave != NULL, NULL);
    g_return_val_if_fail (start_time >= 0.0f, NULL);
    g_return_val_if_fail (end_time > start_time, NULL);

    sample_rate = grl_wave_get_sample_rate (self->wave);
    init_frame = (gint)(start_time * sample_rate);
    final_frame = (gint)(end_time * sample_rate);

    cropped_wave = grl_wave_crop (self->wave, init_frame, final_frame);
    if (cropped_wave == NULL)
        return NULL;

    cropped = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    cropped->wave = cropped_wave;

    return cropped;
}

/**
 * lrg_wave_data_resample:
 * @self: a #LrgWaveData
 * @new_sample_rate: new sample rate in Hz
 *
 * Creates a new wave with the specified sample rate.
 *
 * Returns: (transfer full): A new resampled #LrgWaveData
 */
LrgWaveData *
lrg_wave_data_resample (LrgWaveData *self,
                         guint        new_sample_rate)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);
    g_return_val_if_fail (self->wave != NULL, NULL);
    g_return_val_if_fail (new_sample_rate > 0, NULL);

    return lrg_wave_data_convert (self,
                                   new_sample_rate,
                                   grl_wave_get_sample_size (self->wave),
                                   grl_wave_get_channels (self->wave));
}

/**
 * lrg_wave_data_convert:
 * @self: a #LrgWaveData
 * @sample_rate: new sample rate in Hz
 * @sample_size: new bits per sample (8, 16, or 32)
 * @channels: new channel count (1 or 2)
 *
 * Creates a new wave with converted format settings.
 *
 * Returns: (transfer full): A new converted #LrgWaveData
 */
LrgWaveData *
lrg_wave_data_convert (LrgWaveData *self,
                        guint        sample_rate,
                        guint        sample_size,
                        guint        channels)
{
    LrgWaveData *converted;
    GrlWave *converted_wave;

    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);
    g_return_val_if_fail (self->wave != NULL, NULL);
    g_return_val_if_fail (sample_rate > 0, NULL);
    g_return_val_if_fail (sample_size == 8 || sample_size == 16 || sample_size == 32, NULL);
    g_return_val_if_fail (channels > 0 && channels <= 8, NULL);

    converted_wave = grl_wave_format (self->wave, sample_rate, sample_size, channels);
    if (converted_wave == NULL)
        return NULL;

    converted = g_object_new (LRG_TYPE_WAVE_DATA, NULL);
    converted->wave = converted_wave;

    return converted;
}

/* ==========================================================================
 * Sample Access
 * ========================================================================== */

/**
 * lrg_wave_data_get_samples:
 * @self: a #LrgWaveData
 * @out_count: (out): return location for the number of samples
 *
 * Gets all samples as normalized 32-bit floats.
 *
 * Returns: (transfer full) (array length=out_count): float sample array
 */
gfloat *
lrg_wave_data_get_samples (LrgWaveData *self,
                            gsize       *out_count)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);
    g_return_val_if_fail (self->wave != NULL, NULL);
    g_return_val_if_fail (out_count != NULL, NULL);

    return grl_wave_load_samples (self->wave, out_count);
}

/**
 * lrg_wave_data_set_samples:
 * @self: a #LrgWaveData
 * @samples: (array length=count): normalized float samples (-1.0 to 1.0)
 * @count: number of samples
 *
 * Sets the sample data from normalized 32-bit floats.
 */
void
lrg_wave_data_set_samples (LrgWaveData  *self,
                            const gfloat *samples,
                            gsize         count)
{
    guint sample_rate;
    guint channels;
    gsize data_size;

    g_return_if_fail (LRG_IS_WAVE_DATA (self));
    g_return_if_fail (samples != NULL);
    g_return_if_fail (count > 0);

    /* Get current format parameters */
    if (self->wave == NULL)
    {
        lrg_log_warning ("Cannot set samples: wave data not initialized");
        return;
    }

    sample_rate = grl_wave_get_sample_rate (self->wave);
    channels = grl_wave_get_channels (self->wave);
    data_size = count * sizeof (gfloat);

    /* Free old wave and create new one with the sample data */
    grl_wave_free (self->wave);
    self->wave = grl_wave_new_from_samples (sample_rate, 32, channels,
                                             (const guint8 *)samples, data_size);

    /* Notify property changes */
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
}

/* ==========================================================================
 * Export
 * ========================================================================== */

/**
 * lrg_wave_data_export_wav:
 * @self: a #LrgWaveData
 * @path: (type filename): output file path
 * @error: (nullable): return location for a #GError
 *
 * Exports the wave data to a WAV file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_wave_data_export_wav (LrgWaveData  *self,
                           const gchar  *path,
                           GError      **error)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->wave == NULL)
    {
        g_set_error (error,
                     LRG_WAVE_DATA_ERROR,
                     LRG_WAVE_DATA_ERROR_INVALID_PARAMS,
                     "Wave data is not initialized");
        return FALSE;
    }

    if (!grl_wave_export (self->wave, path, error))
    {
        return FALSE;
    }

    lrg_log_debug ("Exported wave data to '%s'", path);
    return TRUE;
}

/* ==========================================================================
 * Conversion
 * ========================================================================== */

/**
 * lrg_wave_data_to_sound:
 * @self: a #LrgWaveData
 *
 * Converts the wave data to a playable #GrlSound.
 *
 * Returns: (transfer full) (nullable): A new #GrlSound
 */
GrlSound *
lrg_wave_data_to_sound (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);

    if (self->wave == NULL)
        return NULL;

    return grl_sound_new_from_grl_wave (self->wave);
}

/**
 * lrg_wave_data_get_grl_wave:
 * @self: a #LrgWaveData
 *
 * Gets the underlying #GrlWave.
 *
 * Returns: (transfer none) (nullable): the underlying #GrlWave
 */
GrlWave *
lrg_wave_data_get_grl_wave (LrgWaveData *self)
{
    g_return_val_if_fail (LRG_IS_WAVE_DATA (self), NULL);

    return self->wave;
}
