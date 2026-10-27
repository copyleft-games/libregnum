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
 * Then the bed's effect chain processes the mix (under the chain's own
 * lock, as the device mix does).
 *
 * Threads (as LrgMidiSynth, see lrg-midi-synth.c and
 * lrg-audio-thread-private.h): the renderer owns the layers' playback
 * state, the master volume it mixes with and the scatter dice.  With a
 * stream it is the audio device's thread, pulled by graylib's stream
 * callback; the game thread's calls become commands (add, remove,
 * scatter, level, volume, seed) on a lock-free list, applied in order at
 * the start of the next block, and what the renderer is done with
 * (commands, removed layers) comes back on the garbage list for the game
 * thread to free.  A layer's clip and name never change after it is
 * added, so both threads read them.  The game thread keeps its own list
 * of the layers (names, the level last set); the renderer publishes each
 * layer's gain and play count in atomics after every block, with the
 * serial of the last level change it applied, so get_gain() right after
 * an instant set_level() reads the new level even before the audio
 * thread got to it.  Without a stream (or when graylib had no callback
 * slot and update() pumps the stream as before) the game thread is the
 * renderer and commands apply at once.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include <graylib.h>

#include "lrg-sound-bed.h"
#include "lrg-audio-thread-private.h"
#include "../lrg-log.h"

typedef struct _Layer Layer;

struct _Layer
{
    LrgAudioNode node;        /* first: the garbage list links it */

    /* fixed once added: both threads read them */
    gchar   *name;
    gfloat  *samples;         /* n_frames x channels, at the bed's rate */
    gsize    n_frames;
    guint    channels;

    /* the game thread's */
    gfloat   level;           /* the target last set */
    guint    level_serial;    /* moved by every set_level() */
    gboolean level_instant;   /* the last one jumped (no fade) */

    /* the renderer's */
    Layer   *next;            /* the renderer's list, in insertion order */
    gsize    position;        /* next frame to read */
    gfloat   gain;            /* now */
    gfloat   target;
    gfloat   step;            /* per frame, >= 0 */
    gboolean scatter;
    gdouble  min_gap;         /* seconds */
    gdouble  max_gap;
    gsize    wait;            /* frames of silence left before the next play */
    gboolean playing;         /* a scattered layer inside a play */
    guint    plays;
    guint    applied_level_serial;

    /* published by the renderer (atomics) */
    gint     pub_gain;        /* the gfloat's bits */
    gint     pub_plays;
    gint     pub_level_serial;
};

/* what a command does on the renderer */
typedef enum
{
    BED_ADD,        /* layer joins the renderer's list (handed over) */
    BED_REMOVE,     /* layer leaves it (and comes back as garbage) */
    BED_SCATTER,    /* layer's gaps: min_gap .. max_gap */
    BED_LEVEL,      /* layer's fade to level over fade seconds */
    BED_VOLUME,     /* the master volume */
    BED_SEED        /* the scatter dice */
} BedCommandType;

typedef struct
{
    LrgAudioNode   node;      /* first: the command and garbage lists link it */
    BedCommandType type;
    Layer         *layer;     /* ADD: owned until applied; else borrowed */
    gdouble        min_gap;
    gdouble        max_gap;
    gdouble        fade;
    gfloat         level;     /* LEVEL: the level; VOLUME: the volume */
    guint          serial;    /* LEVEL */
    guint32        seed;
} BedCommand;

struct _LrgSoundBed
{
    GObject              parent_instance;

    guint                sample_rate;
    LrgAudioEffectChain *effects;  /* locks itself */

    /* the game thread's */
    GPtrArray           *layers;   /* Layer, borrowed, in insertion order */
    gfloat               volume;
    gfloat              *stream_buffer;
    GrlAudioStream      *stream;   /* nullable */
    gboolean             threaded; /* the stream's callback renders */

    /* the renderer's */
    Layer               *first;    /* the layers it mixes, owned */
    Layer               *last;
    gfloat               mix_volume;
    GRand               *rand;

    /* between them */
    LrgAudioNode        *commands; /* atomic */
    LrgAudioNode        *garbage;  /* atomic */
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

static void
bed_command_free (gpointer data)
{
    BedCommand *command = data;

    /* an ADD never applied still owns its layer */
    if (command->type == BED_ADD && command->layer != NULL)
        layer_free (command->layer);
    g_free (command);
}

static BedCommand *
bed_command_new (BedCommandType  type,
                 Layer          *layer)
{
    BedCommand *command;

    command = g_new0 (BedCommand, 1);
    command->node.free_func = bed_command_free;
    command->type = type;
    command->layer = layer;
    return command;
}

/* the game thread's lookup (its own list) */
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

/* a gap of the layer in frames, at least one (renderer) */
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

/* forward: the renderer (below) */
static void bed_sync (LrgSoundBed *self);

static void
lrg_sound_bed_dispose (GObject *object)
{
    LrgSoundBed *self = LRG_SOUND_BED (object);

    /* the renderer comes back to this thread before the effects go */
    lrg_sound_bed_stop_stream (self);
    bed_sync (self);
    g_clear_object (&self->effects);

    G_OBJECT_CLASS (lrg_sound_bed_parent_class)->dispose (object);
}

static void
lrg_sound_bed_finalize (GObject *object)
{
    LrgSoundBed *self = LRG_SOUND_BED (object);
    Layer       *layer;

    bed_sync (self);
    g_ptr_array_unref (self->layers);
    layer = self->first;
    while (layer != NULL)
    {
        Layer *next = layer->next;

        layer_free (layer);
        layer = next;
    }
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
    self->layers = g_ptr_array_new ();
    self->volume = 1.0f;
    self->mix_volume = 1.0f;
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
 * The renderer (the audio thread's while a stream callback renders)
 * ========================================================================== */

/* the fade as set_level() set it inline before */
static void
layer_set_level (LrgSoundBed *self,
                 Layer       *layer,
                 gfloat       level,
                 gdouble      fade)
{
    gdouble frames;

    layer->target = level;
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
}

static void
bed_apply (LrgSoundBed *self,
           BedCommand  *command)
{
    Layer *layer = command->layer;

    switch (command->type)
    {
    case BED_ADD:
        /* appended: the mix (and the dice) keep the insertion order */
        layer->next = NULL;
        if (self->last != NULL)
            self->last->next = layer;
        else
            self->first = layer;
        self->last = layer;
        command->layer = NULL;
        break;

    case BED_REMOVE:
        {
            Layer *prev = NULL;
            Layer *at;

            for (at = self->first; at != NULL && at != layer; at = at->next)
                prev = at;
            if (at == NULL)
                break;
            if (prev != NULL)
                prev->next = at->next;
            else
                self->first = at->next;
            if (self->last == at)
                self->last = prev;
            lrg_audio_list_push (&self->garbage, &at->node);
        }
        break;

    case BED_SCATTER:
        layer->min_gap = command->min_gap;
        layer->max_gap = command->max_gap;
        layer->scatter = command->max_gap > 0.0;
        layer->playing = FALSE;
        layer->position = 0;
        layer->wait = layer->scatter ? roll_gap (self, layer) : 0;
        break;

    case BED_LEVEL:
        layer_set_level (self, layer, command->level, command->fade);
        layer->applied_level_serial = command->serial;
        break;

    case BED_VOLUME:
        self->mix_volume = command->level;
        break;

    case BED_SEED:
    default:
        g_rand_set_seed (self->rand, command->seed);
        break;
    }
}

/* applies every pending command, oldest first; they go back as garbage */
static void
bed_drain (LrgSoundBed *self)
{
    LrgAudioNode *first;
    LrgAudioNode *last;
    LrgAudioNode *node;

    first = lrg_audio_list_take_fifo (&self->commands);
    last = NULL;
    for (node = first; node != NULL; node = node->next)
    {
        bed_apply (self, (BedCommand *)node);
        last = node;
    }
    if (first != NULL)
        lrg_audio_list_push_chain (&self->garbage, first, last);
}

/* each layer's gain and plays for the getters (the serial last: a reader
 * that sees it sees that gain or a newer one) */
static void
bed_publish (LrgSoundBed *self)
{
    Layer *layer;

    for (layer = self->first; layer != NULL; layer = layer->next)
    {
        lrg_audio_atomic_float_set (&layer->pub_gain, layer->gain);
        g_atomic_int_set (&layer->pub_plays, (gint)layer->plays);
        g_atomic_int_set (&layer->pub_level_serial, (gint)layer->applied_level_serial);
    }
}

/* forward: the mix (below, with layer_mix) */
static void bed_mix (LrgSoundBed *self, gfloat *frames, guint n_frames);

/* graylib's stream callback, on the audio device's thread */
static void
bed_stream_fill (gpointer buffer,
                 guint    n_frames,
                 gpointer user_data)
{
    LrgSoundBed *self = user_data;

    bed_drain (self);
    bed_mix (self, (gfloat *)buffer, n_frames);
    bed_publish (self);
}

/* the game thread: frees what the renderer let go of */
static void
bed_collect (LrgSoundBed *self)
{
    lrg_audio_list_free_all (&self->garbage);
}

/* the renderer catches up on the game thread (it owns the state now) */
static void
bed_sync (LrgSoundBed *self)
{
    bed_drain (self);
    bed_publish (self);
    bed_collect (self);
}

/* hands a command to the renderer: the audio thread's next block, or now */
static void
bed_submit (LrgSoundBed *self,
            BedCommand  *command)
{
    lrg_audio_list_push (&self->commands, &command->node);
    if (!self->threaded)
    {
        bed_drain (self);
        bed_publish (self);
    }
    bed_collect (self);
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
    layer->node.free_func = layer_free;
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
    bed_submit (self, bed_command_new (BED_ADD, layer));
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
    (void)g_ptr_array_remove (self->layers, layer);
    bed_submit (self, bed_command_new (BED_REMOVE, layer));
    return TRUE;
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
    Layer      *layer;
    BedCommand *command;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    layer = find_layer (self, name);
    if (layer == NULL)
        return FALSE;
    command = bed_command_new (BED_SCATTER, layer);
    command->min_gap = MAX (min_gap, 0.0);
    command->max_gap = MAX (max_gap, command->min_gap);
    bed_submit (self, command);
    return TRUE;
}

gboolean
lrg_sound_bed_set_level (LrgSoundBed *self,
                         const gchar *name,
                         gfloat       level,
                         gdouble      fade)
{
    Layer      *layer;
    BedCommand *command;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), FALSE);

    layer = find_layer (self, name);
    if (layer == NULL)
        return FALSE;
    layer->level = CLAMP (level, 0.0f, 1.0f);
    layer->level_serial++;
    layer->level_instant = fade * (gdouble)self->sample_rate < 1.0;
    command = bed_command_new (BED_LEVEL, layer);
    command->level = layer->level;
    command->fade = fade;
    command->serial = layer->level_serial;
    bed_submit (self, command);
    return TRUE;
}

gfloat
lrg_sound_bed_get_level (LrgSoundBed *self,
                         const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0.0f);

    layer = find_layer (self, name);
    return layer != NULL ? layer->level : 0.0f;
}

gfloat
lrg_sound_bed_get_gain (LrgSoundBed *self,
                        const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0.0f);

    layer = find_layer (self, name);
    if (layer == NULL)
        return 0.0f;
    /* an instant level the renderer has not applied yet reads as set */
    if ((guint)g_atomic_int_get (&layer->pub_level_serial) != layer->level_serial &&
        layer->level_instant)
        return layer->level;
    return lrg_audio_atomic_float_get (&layer->pub_gain);
}

guint
lrg_sound_bed_get_play_count (LrgSoundBed *self,
                              const gchar *name)
{
    Layer *layer;

    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0);

    layer = find_layer (self, name);
    return layer != NULL ? (guint)g_atomic_int_get (&layer->pub_plays) : 0;
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
    BedCommand *command;

    g_return_if_fail (LRG_IS_SOUND_BED (self));

    self->volume = CLAMP (volume, 0.0f, 1.0f);
    command = bed_command_new (BED_VOLUME, NULL);
    command->level = self->volume;
    bed_submit (self, command);
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
    BedCommand *command;

    g_return_if_fail (LRG_IS_SOUND_BED (self));

    command = bed_command_new (BED_SEED, NULL);
    command->seed = seed;
    bed_submit (self, command);
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

        g = layer->gain * self->mix_volume;
        out[2 * f] += left * g;
        out[2 * f + 1] += right * g;

        if (layer->gain < layer->target)
            layer->gain = MIN (layer->gain + layer->step, layer->target);
        else if (layer->gain > layer->target)
            layer->gain = MAX (layer->gain - layer->step, layer->target);
    }
}

/* the renderer's mix: every audible layer, then the effects */
static void
bed_mix (LrgSoundBed *self,
         gfloat      *frames,
         guint        n_frames)
{
    Layer *layer;

    if (n_frames == 0)
        return;
    memset (frames, 0, sizeof (gfloat) * 2 * (gsize)n_frames);
    for (layer = self->first; layer != NULL; layer = layer->next)
    {
        if (layer->gain <= 0.0f && layer->target <= 0.0f)
            continue;
        layer_mix (self, layer, frames, n_frames);
    }
    lrg_audio_effect_chain_process (self->effects, frames, n_frames, 2, self->sample_rate);
}

/* offline: the caller's thread renders; silence while the audio thread does */
guint
lrg_sound_bed_render (LrgSoundBed *self,
                      gfloat      *frames,
                      guint        n_frames)
{
    g_return_val_if_fail (LRG_IS_SOUND_BED (self), 0);
    g_return_val_if_fail (frames != NULL || n_frames == 0, 0);

    if (n_frames == 0)
        return 0;
    if (self->threaded)
    {
        memset (frames, 0, sizeof (gfloat) * 2 * (gsize)n_frames);
        return 0;
    }
    bed_drain (self);
    bed_mix (self, frames, n_frames);
    bed_publish (self);
    bed_collect (self);
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
    /* the renderer moves to the audio thread with the callback; the
     * fallback (no callback slot) pumps from update () as before */
    bed_sync (self);
    grl_audio_stream_set_callback (self->stream, bed_stream_fill, self, NULL);
    self->threaded = grl_audio_stream_has_callback (self->stream);
    if (!self->threaded && self->stream_buffer == NULL)
        self->stream_buffer = g_new0 (gfloat, 2 * LRG_SOUND_BED_STREAM_FRAMES);
    grl_audio_stream_play (self->stream);
    return TRUE;
}

void
lrg_sound_bed_update (LrgSoundBed *self)
{
    guint i;

    g_return_if_fail (LRG_IS_SOUND_BED (self));

    bed_collect (self);
    if (self->stream == NULL || self->threaded)
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
    /* detached under raylib's lock: the renderer is this thread's again */
    grl_audio_stream_set_callback (self->stream, NULL, NULL, NULL);
    self->threaded = FALSE;
    bed_sync (self);
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
