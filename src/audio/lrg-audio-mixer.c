/* Configurable PCM audio graph. SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-audio-mixer.h"
#include <gio/gio.h>
#include <math.h>

typedef struct
{
    guint target;
    gdouble gain;
} Send;

typedef struct
{
    gchar *name;
    guint parent;
    gdouble gain;
    gboolean muted;
    GArray *sends;
    gdouble alpha;
    gdouble low[2];
    gdouble feedback, wet;
    gdouble *delay;
    guint delay_frames, cursor;
} Bus;

typedef struct
{
    guint64 id;
    guint bus;
    gfloat *samples;
    gsize count, cursor;
    gdouble gain;
    gboolean loop, paused;
} Voice;

struct _LrgAudioMixer
{
    GObject parent_instance;
    guint rate, channels;
    guint64 last_id;
    GPtrArray *buses;
    GPtrArray *voices;
};
G_DEFINE_TYPE (LrgAudioMixer, lrg_audio_mixer, G_TYPE_OBJECT)

static void
bus_free (gpointer data)
{
    Bus *bus = data;

    g_free (bus->name);
    g_array_unref (bus->sends);
    g_free (bus->delay);
    g_free (bus);
}

static void
voice_free (gpointer data)
{
    Voice *voice = data;

    g_free (voice->samples);
    g_free (voice);
}

static void
lrg_audio_mixer_finalize (GObject *object)
{
    LrgAudioMixer *self = LRG_AUDIO_MIXER (object);

    g_ptr_array_unref (self->buses);
    g_ptr_array_unref (self->voices);
    G_OBJECT_CLASS (lrg_audio_mixer_parent_class)->finalize (object);
}

static void
lrg_audio_mixer_class_init (LrgAudioMixerClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_audio_mixer_finalize;
}

static Bus *
new_bus (const gchar *name,
         guint parent)
{
    Bus *bus = g_new0 (Bus, 1);

    bus->name = g_strdup (name);
    bus->parent = parent;
    bus->gain = 1.0;
    bus->sends = g_array_new (FALSE, FALSE, sizeof (Send));
    return bus;
}

static void
lrg_audio_mixer_init (LrgAudioMixer *self)
{
    self->rate = 48000;
    self->channels = 2;
    self->buses = g_ptr_array_new_with_free_func (bus_free);
    self->voices = g_ptr_array_new_with_free_func (voice_free);
    g_ptr_array_add (self->buses, new_bus ("master", G_MAXUINT));
}

static gint
find_bus (LrgAudioMixer *self,
          const gchar *name)
{
    guint i;

    for (i = 0; i < self->buses->len; i++)
        if (g_strcmp0 (((Bus *)g_ptr_array_index (self->buses, i))->name, name) == 0)
            return (gint)i;
    return -1;
}

static gboolean
valid_gain (gdouble gain)
{
    return isfinite (gain) && gain >= 0.0 && gain <= 4.0;
}

LrgAudioMixer *
lrg_audio_mixer_new (guint sample_rate,
                     guint channels)
{
    LrgAudioMixer *self;

    if (sample_rate < 8000 || sample_rate > 192000 || channels < 1 || channels > 2)
        return NULL;
    self = g_object_new (LRG_TYPE_AUDIO_MIXER, NULL);
    self->rate = sample_rate;
    self->channels = channels;
    return self;
}

gboolean
lrg_audio_mixer_add_bus (LrgAudioMixer *self,
                         const gchar *name,
                         const gchar *parent)
{
    gint p;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    p = find_bus (self, parent);
    if (name == NULL || *name == '\0' || p < 0 || find_bus (self, name) >= 0
        || self->buses->len >= 256)
        return FALSE;
    g_ptr_array_add (self->buses, new_bus (name, (guint)p));
    return TRUE;
}

gboolean
lrg_audio_mixer_set_bus (LrgAudioMixer *self,
                         const gchar *name,
                         gdouble gain,
                         gboolean muted)
{
    gint index;
    Bus *bus;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    index = find_bus (self, name);
    if (index < 0 || !valid_gain (gain))
        return FALSE;
    bus = g_ptr_array_index (self->buses, index);
    bus->gain = gain;
    bus->muted = muted;
    return TRUE;
}

static gboolean
reaches (LrgAudioMixer *self,
         guint from,
         guint target,
         gboolean *seen)
{
    Bus *bus;
    guint i;

    if (from == target)
        return TRUE;
    if (seen[from])
        return FALSE;
    seen[from] = TRUE;
    bus = g_ptr_array_index (self->buses, from);
    if (from != 0 && reaches (self, bus->parent, target, seen))
        return TRUE;
    for (i = 0; i < bus->sends->len; i++)
        if (reaches (self, g_array_index (bus->sends, Send, i).target, target, seen))
            return TRUE;
    return FALSE;
}

gboolean
lrg_audio_mixer_set_send (LrgAudioMixer *self,
                          const gchar *source,
                          const gchar *target,
                          gdouble gain)
{
    gint src, dst;
    Bus *bus;
    gboolean seen[256] = { FALSE };
    guint i;
    Send send;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    src = find_bus (self, source);
    dst = find_bus (self, target);
    if (src <= 0 || dst < 0 || !valid_gain (gain))
        return FALSE;
    bus = g_ptr_array_index (self->buses, src);
    if (bus->parent == (guint)dst || reaches (self, (guint)dst, (guint)src, seen))
        return FALSE;
    for (i = 0; i < bus->sends->len; i++)
    {
        Send *existing = &g_array_index (bus->sends, Send, i);

        if (existing->target == (guint)dst)
        {
            if (gain == 0.0)
                g_array_remove_index (bus->sends, i);
            else
                existing->gain = gain;
            return TRUE;
        }
    }
    if (gain > 0.0)
    {
        send.target = (guint)dst;
        send.gain = gain;
        g_array_append_val (bus->sends, send);
    }
    return TRUE;
}

gboolean
lrg_audio_mixer_set_effects (LrgAudioMixer *self,
                             const gchar *name,
                             gdouble cutoff,
                             gdouble delay_seconds,
                             gdouble feedback,
                             gdouble wet)
{
    gint index;
    Bus *bus;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    index = find_bus (self, name);
    if (index < 0 || !isfinite (cutoff) || cutoff < 0 || cutoff >= self->rate * 0.5
        || !isfinite (delay_seconds) || delay_seconds < 0 || delay_seconds > 2
        || !isfinite (feedback) || feedback < 0 || feedback >= 1 || !isfinite (wet) || wet < 0
        || wet > 1)
        return FALSE;
    bus = g_ptr_array_index (self->buses, index);
    bus->alpha = cutoff == 0 ? 0 : 1.0 - exp (-2.0 * G_PI * cutoff / self->rate);
    bus->low[0] = bus->low[1] = 0;
    bus->feedback = feedback;
    bus->wet = wet;
    bus->delay_frames = delay_seconds == 0 ? 0 : MAX (1u, (guint)(delay_seconds * self->rate));
    bus->cursor = 0;
    g_free (bus->delay);
    bus->delay = g_new0 (gdouble, bus->delay_frames * self->channels);
    return TRUE;
}

guint64
lrg_audio_mixer_play (LrgAudioMixer *self,
                      LrgWaveData *wave,
                      const gchar *bus,
                      gdouble gain,
                      gboolean loop)
{
    g_autoptr (LrgWaveData) converted = NULL;
    Voice *voice;
    gint index;
    gsize i;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
    g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), 0);
    index = find_bus (self, bus);
    if (index < 0 || !valid_gain (gain) || self->last_id == G_MAXUINT64
        || !lrg_wave_data_is_valid (wave) || lrg_wave_data_get_frame_count (wave) == 0)
        return 0;
    converted = lrg_wave_data_convert (wave, self->rate, 32, self->channels);
    if (converted == NULL)
        return 0;
    voice = g_new0 (Voice, 1);
    voice->samples = lrg_wave_data_get_samples (converted, &voice->count);
    if (voice->samples == NULL || voice->count == 0 || voice->count % self->channels != 0)
    {
        voice_free (voice);
        return 0;
    }
    for (i = 0; i < voice->count; i++)
        if (!isfinite (voice->samples[i]))
        {
            voice_free (voice);
            return 0;
        }
    voice->id = ++self->last_id;
    voice->bus = (guint)index;
    voice->gain = gain;
    voice->loop = loop;
    g_ptr_array_add (self->voices, voice);
    return voice->id;
}

gboolean
lrg_audio_mixer_stop_voice (LrgAudioMixer *self,
                            guint64 id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    for (i = 0; i < self->voices->len; i++)
        if (((Voice *)g_ptr_array_index (self->voices, i))->id == id)
        {
            g_ptr_array_remove_index (self->voices, i);
            return TRUE;
        }
    return FALSE;
}

gboolean
lrg_audio_mixer_pause_voice (LrgAudioMixer *self,
                             guint64 id,
                             gboolean paused)
{
    guint i;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    for (i = 0; i < self->voices->len; i++)
    {
        Voice *voice = g_ptr_array_index (self->voices, i);

        if (voice->id == id)
        {
            voice->paused = paused;
            return TRUE;
        }
    }
    return FALSE;
}

guint
lrg_audio_mixer_get_voice_count (LrgAudioMixer *self)
{
    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
    return self->voices->len;
}

gfloat *
lrg_audio_mixer_render (LrgAudioMixer *self,
                        guint frames,
                        gsize *n_samples)
{
    g_autofree gdouble *buffers = NULL;
    gfloat *output;
    guint indegree[256] = { 0 };
    gboolean done[256] = { FALSE };
    gsize count;
    gsize k;
    guint i, j, step;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
    g_return_val_if_fail (n_samples != NULL, NULL);
    *n_samples = 0;
    if (frames == 0 || frames > 65536)
        return NULL;
    count = (gsize)frames * self->channels;
    buffers = g_new0 (gdouble, count * self->buses->len);
    output = g_new (gfloat, count);
    for (i = 0; i < self->voices->len;)
    {
        Voice *voice = g_ptr_array_index (self->voices, i);
        gdouble *dest = buffers + count * voice->bus;

        if (!voice->paused)
            for (k = 0; k < count; k++)
            {
                dest[k] += voice->samples[voice->cursor++] * voice->gain;
                if (voice->cursor == voice->count)
                {
                    if (voice->loop)
                        voice->cursor = 0;
                    else
                        break;
                }
            }
        if (voice->cursor == voice->count)
            g_ptr_array_remove_index (self->voices, i);
        else
            i++;
    }
    for (i = 1; i < self->buses->len; i++)
    {
        Bus *bus = g_ptr_array_index (self->buses, i);

        indegree[bus->parent]++;
        for (j = 0; j < bus->sends->len; j++)
            indegree[g_array_index (bus->sends, Send, j).target]++;
    }
    for (step = 0; step < self->buses->len; step++)
    {
        Bus *bus;
        gdouble *samples;

        for (i = 0; i < self->buses->len; i++)
            if (!done[i] && indegree[i] == 0)
                break;
        g_assert (i < self->buses->len);
        done[i] = TRUE;
        bus = g_ptr_array_index (self->buses, i);
        samples = buffers + count * i;
        for (k = 0; k < count; k++)
        {
            guint channel = (guint)(k % self->channels);
            gdouble sample = samples[k];

            if (bus->alpha > 0)
            {
                bus->low[channel] += bus->alpha * (sample - bus->low[channel]);
                sample = bus->low[channel];
            }
            if (bus->delay_frames > 0)
            {
                guint pos = bus->cursor * self->channels + channel;
                gdouble delayed = bus->delay[pos];

                bus->delay[pos] = sample + delayed * bus->feedback;
                sample = sample * (1.0 - bus->wet) + delayed * bus->wet;
                if (channel + 1 == self->channels)
                    bus->cursor = (bus->cursor + 1) % bus->delay_frames;
            }
            samples[k] = bus->muted ? 0 : sample * bus->gain;
        }
        if (i != 0)
        {
            for (k = 0; k < count; k++)
                buffers[count * bus->parent + k] += samples[k];
            indegree[bus->parent]--;
            for (j = 0; j < bus->sends->len; j++)
            {
                Send send = g_array_index (bus->sends, Send, j);

                for (k = 0; k < count; k++)
                    buffers[count * send.target + k] += samples[k] * send.gain;
                indegree[send.target]--;
            }
        }
    }
    for (k = 0; k < count; k++)
        output[k] = (gfloat)CLAMP (buffers[k], -1.0, 1.0);
    *n_samples = count;
    return output;
}

gboolean
lrg_audio_mixer_pump (LrgAudioMixer *self,
                      GrlAudioStream *stream,
                      guint frames,
                      GError **error)
{
    g_autofree gfloat *samples = NULL;
    gsize count;

    g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
    g_return_val_if_fail (GRL_IS_AUDIO_STREAM (stream), FALSE);
    if (frames == 0 || frames > 65536 || !grl_audio_stream_is_valid (stream)
        || grl_audio_stream_get_sample_rate (stream) != self->rate
        || grl_audio_stream_get_channels (stream) != self->channels
        || grl_audio_stream_get_sample_size (stream) != 32)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Stream format or frame count does not match mixer");
        return FALSE;
    }
    if (!grl_audio_stream_is_playing (stream) || !grl_audio_stream_is_processed (stream))
        return TRUE;
    samples = lrg_audio_mixer_render (self, frames, &count);
    grl_audio_stream_update (stream, samples, (gint)frames);
    return TRUE;
}
