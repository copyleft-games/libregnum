/* lrg-reel-audio-track.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-audio-track.h"
#include "../audio/lrg-wave-data.h"
#include <gio/gio.h>
#include <math.h>

/*
 * ReelAudioClip — one timed clip entry on the track.
 *
 * trim_start and trim_end are in seconds relative to the beginning of @wave.
 * trim_end <= 0 means "use the wave's full remaining duration".
 */
typedef struct
{
    LrgWaveData *wave;
    gint         from_frame;
    gdouble      volume;
    gdouble      trim_start;
    gdouble      trim_end;
} ReelAudioClip;

struct _LrgReelAudioTrack
{
    GObject  parent_instance;

    gdouble  fps;
    GArray  *clips; /* of ReelAudioClip */
};

G_DEFINE_FINAL_TYPE (LrgReelAudioTrack, lrg_reel_audio_track, G_TYPE_OBJECT)

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void
reel_audio_clip_clear (ReelAudioClip *clip)
{
    g_clear_object (&clip->wave);
}

/* --------------------------------------------------------------------------
 * GObject lifecycle
 * -------------------------------------------------------------------------- */

static void
lrg_reel_audio_track_finalize (GObject *object)
{
    LrgReelAudioTrack *self = LRG_REEL_AUDIO_TRACK (object);
    guint              i;

    for (i = 0; i < self->clips->len; i++)
        reel_audio_clip_clear (&g_array_index (self->clips, ReelAudioClip, i));

    g_clear_pointer (&self->clips, g_array_unref);

    G_OBJECT_CLASS (lrg_reel_audio_track_parent_class)->finalize (object);
}

static void
lrg_reel_audio_track_class_init (LrgReelAudioTrackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_audio_track_finalize;
}

static void
lrg_reel_audio_track_init (LrgReelAudioTrack *self)
{
    self->fps   = 24.0;
    self->clips = g_array_new (FALSE, TRUE, sizeof (ReelAudioClip));
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_audio_track_new:
 * @fps: the frame rate of the owning composition (frames per second, > 0).
 *
 * Creates a new, empty audio track.
 *
 * Returns: (transfer full): a new #LrgReelAudioTrack
 *
 * Since: 1.0
 */
LrgReelAudioTrack *
lrg_reel_audio_track_new (gdouble fps)
{
    LrgReelAudioTrack *self;

    self = g_object_new (LRG_TYPE_REEL_AUDIO_TRACK, NULL);
    self->fps = (fps > 0.0) ? fps : 24.0;

    return self;
}

/**
 * lrg_reel_audio_track_add:
 * @self: a #LrgReelAudioTrack
 * @wave: (transfer none): the wave data to place on the track
 * @from_frame: composition frame at which the clip starts
 * @volume: linear volume scalar (1.0 = unity gain)
 * @trim_start_sec: seconds into @wave at which playback begins (>= 0)
 * @trim_end_sec: seconds into @wave at which playback ends; pass <= 0 to use
 *     the full duration from @trim_start_sec to the end of the wave
 *
 * Adds an audio clip to the track.  @wave is referenced internally and will be
 * released when the track is finalized.
 *
 * Since: 1.0
 */
void
lrg_reel_audio_track_add (LrgReelAudioTrack *self,
                           LrgWaveData       *wave,
                           gint               from_frame,
                           gdouble            volume,
                           gdouble            trim_start_sec,
                           gdouble            trim_end_sec)
{
    ReelAudioClip clip;

    g_return_if_fail (LRG_IS_REEL_AUDIO_TRACK (self));
    g_return_if_fail (LRG_IS_WAVE_DATA (wave));

    clip.wave        = g_object_ref (wave);
    clip.from_frame  = from_frame;
    clip.volume      = volume;
    clip.trim_start  = trim_start_sec;
    clip.trim_end    = trim_end_sec;

    g_array_append_val (self->clips, clip);
}

/**
 * lrg_reel_audio_track_mix:
 * @self: a #LrgReelAudioTrack
 * @sample_rate: output sample rate in Hz (must be > 0)
 * @channels: number of output channels (must be > 0)
 * @total_frames: total composition length in video frames (must be > 0)
 * @error: (nullable): return location for a #GError
 *
 * Renders all clips on the track into a single #LrgWaveData at the requested
 * format.  Each clip's wave is converted to @sample_rate / @channels as needed,
 * scaled by its volume, and accumulated at the correct temporal position.  The
 * final buffer is clamped to [-1.0, 1.0].
 *
 * On invalid arguments @error is set using the %G_IO_ERROR domain and %NULL is
 * returned.
 *
 * Returns: (transfer full) (nullable): a new #LrgWaveData, or %NULL on error
 *
 * Since: 1.0
 */
LrgWaveData *
lrg_reel_audio_track_mix (LrgReelAudioTrack *self,
                           guint              sample_rate,
                           guint              channels,
                           guint              total_frames,
                           GError           **error)
{
    gdouble      total_seconds;
    guint        frames_per_channel;
    gsize        total;
    gfloat      *acc;
    guint        i;
    gsize        j;
    LrgWaveData *out;
    guint        out_fc;
    guint        out_total;

    g_return_val_if_fail (LRG_IS_REEL_AUDIO_TRACK (self), NULL);

    /* Step 1 — validate. */
    if (sample_rate == 0 || channels == 0 || total_frames == 0)
    {
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_INVALID_ARGUMENT,
                     "lrg_reel_audio_track_mix: sample_rate, channels, and "
                     "total_frames must all be greater than zero");
        return NULL;
    }

    /* Step 2 — compute output buffer dimensions. */
    total_seconds      = (gdouble) total_frames / self->fps;
    frames_per_channel = (guint) ceil (total_seconds * (gdouble) sample_rate);

    if (frames_per_channel == 0)
        frames_per_channel = 1;

    total = (gsize) frames_per_channel * channels;

    /* Step 3 — allocate silence accumulation buffer. */
    acc = g_new0 (gfloat, total);

    /* Step 4 — accumulate each clip. */
    for (i = 0; i < self->clips->len; i++)
    {
        ReelAudioClip       clip;
        g_autoptr(LrgWaveData) conv = NULL;
        gsize               count;
        gfloat             *samples;
        guint               src_frames;
        gint                src_lo;
        gint                src_hi;
        gint                dst_start;
        gint                d;

        clip    = g_array_index (self->clips, ReelAudioClip, i);
        count   = 0;
        samples = NULL;

        /* Convert to target format if needed. */
        if (lrg_wave_data_get_sample_rate (clip.wave) != sample_rate ||
            lrg_wave_data_get_channels (clip.wave)    != channels)
        {
            conv = lrg_wave_data_convert (clip.wave, sample_rate, 32, channels);
        }
        else
        {
            conv = g_object_ref (clip.wave);
        }

        if (conv == NULL)
            continue;

        samples = lrg_wave_data_get_samples (conv, &count);

        if (samples == NULL || count == 0)
        {
            g_free (samples);
            continue;
        }

        src_frames = (guint) (count / channels);

        /* Trim: convert trim times (seconds) to source frame indices. */
        src_lo = (gint) floor (clip.trim_start * (gdouble) sample_rate);
        src_hi = (clip.trim_end > 0.0)
                     ? (gint) ceil (clip.trim_end * (gdouble) sample_rate)
                     : (gint) src_frames;

        src_lo = CLAMP (src_lo, 0, (gint) src_frames);
        src_hi = CLAMP (src_hi, src_lo, (gint) src_frames);

        /* Destination start in output frames. */
        dst_start = (gint) lround (
            (gdouble) clip.from_frame / self->fps * (gdouble) sample_rate);

        /* Accumulate. */
        for (d = 0; (src_lo + d) < src_hi; d++)
        {
            gint dst;
            guint c;

            dst = dst_start + d;

            if (dst < 0)
                continue;

            if (dst >= (gint) frames_per_channel)
                break;

            for (c = 0; c < channels; c++)
            {
                acc[(gsize) dst * channels + c] +=
                    samples[(gsize)(src_lo + d) * channels + c] *
                    (gfloat) clip.volume;
            }
        }

        g_free (samples);
        /* conv freed by g_autoptr at end of iteration */
    }

    /* Step 5 — clamp to [-1, 1]. */
    for (j = 0; j < total; j++)
        acc[j] = CLAMP (acc[j], -1.0f, 1.0f);

    /* Step 6 — build result LrgWaveData. */
    out = lrg_wave_data_new_procedural (sample_rate,
                                        channels,
                                        (gfloat) frames_per_channel /
                                            (gfloat) sample_rate);

    if (out == NULL)
    {
        g_free (acc);
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "lrg_reel_audio_track_mix: failed to allocate output "
                     "LrgWaveData");
        return NULL;
    }

    /*
     * Guard against floating-point rounding in new_procedural: query the
     * actual frame count and cap the copy length to the smaller of the two
     * so set_samples never writes past the wave's internal buffer.
     */
    out_fc    = lrg_wave_data_get_frame_count (out);
    out_total = out_fc * channels;

    lrg_wave_data_set_samples (out, acc, MIN (total, (gsize) out_total));

    g_free (acc);

    return out;
}

/**
 * lrg_reel_audio_track_get_n_clips:
 * @self: a #LrgReelAudioTrack
 *
 * Returns: the number of clips currently on the track
 *
 * Since: 1.0
 */
guint
lrg_reel_audio_track_get_n_clips (LrgReelAudioTrack *self)
{
    g_return_val_if_fail (LRG_IS_REEL_AUDIO_TRACK (self), 0);

    return self->clips->len;
}

/**
 * lrg_reel_audio_track_get_fps:
 * @self: a #LrgReelAudioTrack
 *
 * Returns: the frame rate of the owning composition in frames per second
 *
 * Since: 1.0
 */
gdouble
lrg_reel_audio_track_get_fps (LrgReelAudioTrack *self)
{
    g_return_val_if_fail (LRG_IS_REEL_AUDIO_TRACK (self), 0.0);

    return self->fps;
}
