/* lrg-sound-bed.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Layered ambient loops (see lrg-sound-bed.h and
 * docs/modules/audio/sound-bed.org).
 *
 * Every layer holds its clip as interleaved floats at the bed's rate
 * (resampled linearly when added).  render() walks the layers; a layer
 * whose gain and target are both 0 is skipped and keeps its place.  For
 * an audible layer each frame:
 *
 *   - a scattered layer between two plays counts its gap down and adds
 *     silence; when the gap ends the clip starts from its beginning;
 *   - otherwise the clip's frame at the layer's position is added (mono
 *     to both sides) times the layer's gain and the master volume; at the
 *     clip's end a looping layer wraps to its start (seamless) and a
 *     scattered one rolls its next gap;
 *   - the gain takes one step of its linear fade.
 *
 * Then the bed's effect chain processes the mix.  Rendering happens on
 * the thread that calls render() or update() (the game's), so the bed
 * needs no lock.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include <graylib.h>

#include "lrg-sound-bed.h"
#include "../lrg-log.h"

typedef struct
{
    gchar   *name;
    gfloat  *samples;     /* n_frames x channels, at the bed's rate */
    gsize    n_frames;
    guint    channels;
    gsize    position;    /* next frame to read */

    gfloat   gain;        /* now */
    gfloat   target;
    gfloat   step;        /* per frame, >= 0 */

    gboolean scatter;
    gdouble  min_gap;     /* seconds */
    gdouble  max_gap;
    gsize    wait;        /* frames of silence left before the next play */
    gboolean playing;     /* a scattered layer inside a play */

    guint    plays;
} Layer;

struct _LrgSoundBed
{
    GObject              parent_instance;

    guint                sample_rate;
    GPtrArray           *layers;   /* Layer, in insertion order */
    gfloat               volume;
    GRand               *rand;
    LrgAudioEffectChain *effects;

    gfloat              *stream_buffer;
    GrlAudioStream      *stream;   /* nullable */
};

G_DEFINE_FINAL_TYPE (LrgSoundBed, lrg_sound_bed, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-sound-bed-error-quark, lrg_sound_bed_error)

static void
layer_free (gpointer data)
{
    Layer *layer = data;

    g_free (layer->name);
    g_free (layer->samples);
    g_free (layer);
}

static Layer *
find_layer (LrgSoundBed *self,
            const gchar *name)
{
    guint i;

    if (name == NULL)
        return NULL;
    for (i = 0; i < self->layers->len; i++)
    {
        Layer *layer = g_ptr_array_index (self->layers, i);

        if (g_strcmp0 (layer->name, name) == 0)
            return layer;
    }
    return NULL;
}

/* a gap of the layer in frames, at least one */
static gsize
roll_gap (LrgSoundBed *self,
          Layer       *layer)
{
    gdouble seconds;

    seconds = layer->max_gap > layer->min_gap ?
              g_rand_double_range (self->rand, layer->min_gap, layer->max_gap) :
              layer->min_gap;
    return MAX ((gsize)(seconds * (gdouble)self->sample_rate), (gsize)1);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_sound_bed_dispose (GObject *object)
{
    LrgSoundBed *self = LRG_SOUND_BED (object);

    lrg_sound_bed_stop_stream (self);
    g_clear_object (&self->effects);

    G_OBJECT_CLASS (lrg_sound_bed_parent_class)->dispose (object);
}

static void
lrg_sound_bed_finalize (GObject *object)
{
    LrgSoundBed *self = LRG_SOUND_BED (object);

    g_ptr_array_unref (self->layers);
    g_rand_free (self->rand);
    g_free (self->stream_buffer);

    G_OBJECT_CLASS (lrg_sound_bed_parent_class)->finalize (object);
}

static void
lrg_sound_bed_class_init (LrgSoundBedClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_sound_bed_dispose;
    object_class->finalize = lrg_sound_bed_finalize;
}

static void
lrg_sound_bed_init (LrgSoundBed *self)
{
    self->sample_rate = 44100;
    self->layers = g_ptr_array_new_with_free_func (layer_free);
    self->volume = 1.0f;
    self->rand = g_rand_new_with_seed (0x5eedbed);
    self->effects = lrg_audio_effect_chain_new ();
}

LrgSoundBed *
lrg_sound_bed_new (guint sample_rate)
{
    LrgSoundBed *self;

    g_return_val_if_fail (sample_rate >= 8000 && sample_rate <= 192000, NULL);

    self = g_object_new (LRG_TYPE_SOUND_BED, NULL);
    self->sample_rate = sample_rate;
    return self;
}

guint
lrg_sound_bed_get_sample_rate (LrgSoundBed *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0);

    return self->sample_rate;
}

/* ==========================================================================
 * Layers
 * ========================================================================== */

/*
 * lrg_sound_bed_add_layer_samples:
 * Linear interpolation to the bed's rate; the last source frame is held
 * for the final fraction (a loop's seam then never reads past the end).
 */
gboolean
lrg_sound_bed_add_layer_samples (LrgSoundBed   *self,
                                 const gchar   *name,
                                 const gfloat  *samples,
                                 gsize          n_samples,
                                 guint          channels,
                                 guint          sample_rate,
                                 GError       **error)
{
    Layer  *layer;
    gsize   src_frames;
    gsize   dst_frames;
    gsize   f;
    guint   c;
    gdouble ratio;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (find_layer (self, name) != NULL)
    {
        g_set_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_EXISTS,
                     "the bed has a layer \"%s\"", name);
        return FALSE;
    }
    if (samples == NULL || channels < 1 || channels > 2 || n_samples < channels ||
        sample_rate < 1000 || sample_rate > 192000)
    {
        g_set_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_INVALID,
                     "layer \"%s\": unusable samples (%" G_GSIZE_FORMAT " samples, %u channels, %u Hz)",
                     name, n_samples, channels, sample_rate);
        return FALSE;
    }

    src_frames = n_samples / channels;
    ratio = (gdouble)sample_rate / (gdouble)self->sample_rate;
    dst_frames = MAX ((gsize)((gdouble)src_frames / ratio), (gsize)1);

    layer = g_new0 (Layer, 1);
    layer->name = g_strdup (name);
    layer->channels = channels;
    layer->n_frames = dst_frames;
    layer->samples = g_new (gfloat, dst_frames * channels);
    for (f = 0; f < dst_frames; f++)
    {
        gdouble pos;
        gsize   i0;
        gsize   i1;
        gfloat  t;

        pos = (gdouble)f * ratio;
        i0 = MIN ((gsize)pos, src_frames - 1);
        i1 = MIN (i0 + 1, src_frames - 1);
        t = (gfloat)(pos - (gdouble)i0);
        for (c = 0; c < channels; c++)
            layer->samples[f * channels + c] = samples[i0 * channels + c] * (1.0f - t) +
                                               samples[i1 * channels + c] * t;
    }
    g_ptr_array_add (self->layers, layer);
    return TRUE;
}

gboolean
lrg_sound_bed_add_layer (LrgSoundBed  *self,
                         const gchar  *name,
                         LrgWaveData  *wave,
                         GError      **error)
{
    g_autofree gfloat *samples = NULL;
    gsize              count;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);
    g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), FALSE);

    count = 0;
    if (lrg_wave_data_is_valid (wave))
        samples = lrg_wave_data_get_samples (wave, &count);
    return lrg_sound_bed_add_layer_samples (self, name, samples, count,
                                            lrg_wave_data_get_channels (wave),
                                            lrg_wave_data_get_sample_rate (wave), error);
}

gboolean
lrg_sound_bed_remove_layer (LrgSoundBed *self,
                            const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    layer = find_layer (self, name);
    if (layer == NULL)
        return FALSE;
    return g_ptr_array_remove (self->layers, layer);
}

gboolean
lrg_sound_bed_has_layer (LrgSoundBed *self,
                         const gchar *name)
{
    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    return find_layer (self, name) != NULL;
}

GStrv
lrg_sound_bed_get_layer_names (LrgSoundBed *self)
{
    GStrvBuilder *builder;
    GStrv         names;
    guint         i;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), NULL);

    builder = g_strv_builder_new ();
    for (i = 0; i < self->layers->len; i++)
        g_strv_builder_add (builder, ((Layer *)g_ptr_array_index (self->layers, i))->name);
    names = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return names;
}

gboolean
lrg_sound_bed_set_scatter (LrgSoundBed *self,
                           const gchar *name,
                           gdouble      min_gap,
                           gdouble      max_gap)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    layer = find_layer (self, name);
    if (layer == NULL)
        return FALSE;
    min_gap = MAX (min_gap, 0.0);
    max_gap = MAX (max_gap, min_gap);
    layer->min_gap = min_gap;
    layer->max_gap = max_gap;
    layer->scatter = max_gap > 0.0;
    layer->playing = FALSE;
    layer->position = 0;
    layer->wait = layer->scatter ? roll_gap (self, layer) : 0;
    return TRUE;
}

gboolean
lrg_sound_bed_set_level (LrgSoundBed *self,
                         const gchar *name,
                         gfloat       level,
                         gdouble      fade)
{
    Layer  *layer;
    gdouble frames;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    layer = find_layer (self, name);
    if (layer == NULL)
        return FALSE;
    layer->target = CLAMP (level, 0.0f, 1.0f);
    frames = fade * (gdouble)self->sample_rate;
    if (frames < 1.0)
    {
        layer->gain = layer->target;
        layer->step = 0.0f;
    }
    else
    {
        layer->step = (gfloat)(fabs ((gdouble)layer->target - (gdouble)layer->gain) / frames);
    }
    return TRUE;
}

gfloat
lrg_sound_bed_get_level (LrgSoundBed *self,
                         const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0.0f);

    layer = find_layer (self, name);
    return layer != NULL ? layer->target : 0.0f;
}

gfloat
lrg_sound_bed_get_gain (LrgSoundBed *self,
                        const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0.0f);

    layer = find_layer (self, name);
    return layer != NULL ? layer->gain : 0.0f;
}

guint
lrg_sound_bed_get_play_count (LrgSoundBed *self,
                              const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0);

    layer = find_layer (self, name);
    return layer != NULL ? layer->plays : 0;
}

void
lrg_sound_bed_crossfade (LrgSoundBed *self,
                         const gchar *from,
                         const gchar *to,
                         gdouble      seconds)
{
    g_return_if_fail (LRG_IS_SOUND_BED (self));

    if (from != NULL)
        (void)lrg_sound_bed_set_level (self, from, 0.0f, seconds);
    if (to != NULL)
        (void)lrg_sound_bed_set_level (self, to, 1.0f, seconds);
}

void
lrg_sound_bed_set_volume (LrgSoundBed *self,
                          gfloat       volume)
{
    g_return_if_fail (LRG_IS_SOUND_BED (self));

    self->volume = CLAMP (volume, 0.0f, 1.0f);
}

gfloat
lrg_sound_bed_get_volume (LrgSoundBed *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0.0f);

    return self->volume;
}

void
lrg_sound_bed_set_seed (LrgSoundBed *self,
                        guint32      seed)
{
    g_return_if_fail (LRG_IS_SOUND_BED (self));

    g_rand_set_seed (self->rand, seed);
}

LrgAudioEffectChain *
lrg_sound_bed_get_effects (LrgSoundBed *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BED (self), NULL);

    return self->effects;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/* adds @n_frames of @layer to @out (see the file comment) */
static void
layer_mix (LrgSoundBed *self,
           Layer       *layer,
           gfloat      *out,
           guint        n_frames)
{
    guint f;

    for (f = 0; f < n_frames; f++)
    {
        gfloat left;
        gfloat right;
        gfloat g;

        left = 0.0f;
        right = 0.0f;
        if (layer->scatter && !layer->playing)
        {
            if (layer->wait > 0)
                layer->wait--;
            if (layer->wait == 0)
            {
                layer->playing = TRUE;
                layer->position = 0;
            }
        }
        else
        {
            const gfloat *frame;

            if (layer->position == 0)
                layer->plays++;
            frame = layer->samples + layer->position * layer->channels;
            left = frame[0];
            right = layer->channels == 2 ? frame[1] : frame[0];
            if (++layer->position >= layer->n_frames)
            {
                layer->position = 0;
                if (layer->scatter)
                {
                    layer->playing = FALSE;
                    layer->wait = roll_gap (self, layer);
                }
            }
        }

        g = layer->gain * self->volume;
        out[2 * f] += left * g;
        out[2 * f + 1] += right * g;

        if (layer->gain < layer->target)
            layer->gain = MIN (layer->gain + layer->step, layer->target);
        else if (layer->gain > layer->target)
            layer->gain = MAX (layer->gain - layer->step, layer->target);
    }
}

guint
lrg_sound_bed_render (LrgSoundBed *self,
                      gfloat      *frames,
                      guint        n_frames)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0);
    g_return_val_if_fail (frames != NULL || n_frames == 0, 0);

    if (n_frames == 0)
        return 0;
    memset (frames, 0, sizeof (gfloat) * 2 * (gsize)n_frames);
    for (i = 0; i < self->layers->len; i++)
    {
        Layer *layer = g_ptr_array_index (self->layers, i);

        if (layer->gain <= 0.0f && layer->target <= 0.0f)
            continue;
        layer_mix (self, layer, frames, n_frames);
    }
    lrg_audio_effect_chain_process (self->effects, frames, n_frames, 2, self->sample_rate);
    return n_frames;
}

/* ==========================================================================
 * Audio device stream (as LrgMidiSynth's)
 * ========================================================================== */

gboolean
lrg_sound_bed_start_stream (LrgSoundBed  *self,
                            GError      **error)
{
    GrlAudioDevice *device;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->stream != NULL)
        return TRUE;

    device = grl_audio_device_get_default ();
    if (device == NULL || !grl_audio_device_is_ready (device))
    {
        g_set_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_NO_DEVICE,
                     "the audio device is not open");
        return FALSE;
    }

    /* every buffer we write fills one exactly (see lrg-midi-synth.c) */
    grl_audio_stream_set_default_buffer_size (LRG_SOUND_BED_STREAM_FRAMES);
    self->stream = grl_audio_stream_new (self->sample_rate, 32, 2);
    if (self->stream == NULL)
    {
        g_set_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_NO_DEVICE,
                     "could not open an audio stream");
        return FALSE;
    }
    if (self->stream_buffer == NULL)
        self->stream_buffer = g_new0 (gfloat, 2 * LRG_SOUND_BED_STREAM_FRAMES);
    grl_audio_stream_play (self->stream);
    return TRUE;
}

void
lrg_sound_bed_update (LrgSoundBed *self)
{
    guint i;

    g_return_if_fail (LRG_IS_SOUND_BED (self));

    if (self->stream == NULL)
        return;
    for (i = 0; i < 2 && grl_audio_stream_is_processed (self->stream); i++)
    {
        lrg_sound_bed_render (self, self->stream_buffer, LRG_SOUND_BED_STREAM_FRAMES);
        grl_audio_stream_update (self->stream, self->stream_buffer, LRG_SOUND_BED_STREAM_FRAMES);
    }
}

void
lrg_sound_bed_stop_stream (LrgSoundBed *self)
{
    g_return_if_fail (LRG_IS_SOUND_BED (self));

    if (self->stream == NULL)
        return;
    if (grl_audio_stream_is_playing (self->stream))
        grl_audio_stream_stop (self->stream);
    g_clear_object (&self->stream);
}

gboolean
lrg_sound_bed_has_stream (LrgSoundBed *self)
{
    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    return self->stream != NULL;
}
