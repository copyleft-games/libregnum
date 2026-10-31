/* lrg-echo.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The feedback delay (see lrg-echo.h and docs/modules/audio/effects.org).
 *
 * One ring per channel of delay x rate samples.  Per sample:
 *
 *   d      = ring[i]                 (the input of delay seconds ago,
 *                                     plus its own fed-back repeats)
 *   ring[i] = x + feedback * d
 *   y      = dry * x + wet * d
 *
 * so the first repeat comes after exactly one delay at the wet level and
 * each later one is feedback times the one before.  The rings are sized
 * for the delay and the block's layout on the first block (on the audio
 * thread for a bus on the device mix), and again only when the rate or
 * the channel count changes or the delay grows past the ring.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-echo.h"

struct _LrgEcho
{
    LrgAudioEffect parent_instance;

    gfloat   delay;     /* seconds */
    gfloat   feedback;
    gfloat   dry;
    gfloat   wet;

    gfloat  *ring;      /* size x channels, interleaved */
    guint    size;      /* frames the ring holds */
    guint    used;      /* frames of it the delay uses */
    guint    index;
    guint    rate;      /* the layout the ring is sized for, 0 none */
    guint    channels;
};

G_DEFINE_FINAL_TYPE (LrgEcho, lrg_echo, LRG_TYPE_AUDIO_EFFECT)

/* the ring for the delay at @rate x @channels (a new one forgets) */
static void
size_ring (LrgEcho *self,
           guint    rate,
           guint    channels)
{
    guint frames;

    frames = (guint)MAX (1.0, floor ((gdouble)self->delay * (gdouble)rate + 0.5));
    if (rate != self->rate || channels != self->channels || frames > self->size)
    {
        g_free (self->ring);
        self->ring = g_new0 (gfloat, (gsize)frames * channels);
        self->size = frames;
        self->index = 0;
        self->rate = rate;
        self->channels = channels;
    }
    /* a shorter delay reuses the ring's start */
    if (frames != self->used)
    {
        self->used = frames;
        if (self->index >= frames)
            self->index = 0;
    }
}

static void
lrg_echo_reset (LrgAudioEffect *effect)
{
    LrgEcho *self = LRG_ECHO (effect);

    if (self->ring != NULL)
        memset (self->ring, 0, (gsize)self->size * self->channels * sizeof (gfloat));
    self->index = 0;
}

static void
lrg_echo_process (LrgAudioEffect *effect,
                  gfloat         *frames,
                  guint           n_frames,
                  guint           channels,
                  guint           sample_rate)
{
    LrgEcho *self = LRG_ECHO (effect);
    gfloat   feedback;
    gfloat   dry;
    gfloat   wet;
    guint    f;
    guint    c;

    size_ring (self, sample_rate, channels);
    feedback = self->feedback;
    dry = self->dry;
    wet = self->wet;

    for (f = 0; f < n_frames; f++)
    {
        gfloat *frame;
        gfloat *slot;

        frame = frames + (gsize)f * channels;
        slot = self->ring + (gsize)self->index * channels;
        for (c = 0; c < channels; c++)
        {
            gfloat x;
            gfloat d;

            x = frame[c];
            d = slot[c];
            slot[c] = x + feedback * d;
            frame[c] = dry * x + wet * d;
        }
        if (++self->index >= self->used)
            self->index = 0;
    }
}

static void
lrg_echo_finalize (GObject *object)
{
    LrgEcho *self = LRG_ECHO (object);

    g_clear_pointer (&self->ring, g_free);

    G_OBJECT_CLASS (lrg_echo_parent_class)->finalize (object);
}

static void
lrg_echo_class_init (LrgEchoClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

    object_class->finalize = lrg_echo_finalize;
    effect_class->process = lrg_echo_process;
    effect_class->reset = lrg_echo_reset;
}

static void
lrg_echo_init (LrgEcho *self)
{
    self->delay = 0.5f;
    self->feedback = 0.5f;
    self->dry = 1.0f;
    self->wet = 1.0f;
}

LrgEcho *
lrg_echo_new (gfloat delay,
              gfloat feedback)
{
    LrgEcho *self;

    self = g_object_new (LRG_TYPE_ECHO, NULL);
    lrg_echo_set_delay (self, delay);
    lrg_echo_set_feedback (self, feedback);
    return self;
}

void
lrg_echo_set_delay (LrgEcho *self,
                    gfloat   delay)
{
    g_return_if_fail (LRG_IS_ECHO (self));

    self->delay = CLAMP (delay, LRG_ECHO_MIN_DELAY, LRG_ECHO_MAX_DELAY);
}

gfloat
lrg_echo_get_delay (LrgEcho *self)
{
    g_return_val_if_fail (LRG_IS_ECHO (self), 0.0f);

    return self->delay;
}

void
lrg_echo_set_feedback (LrgEcho *self,
                       gfloat   feedback)
{
    g_return_if_fail (LRG_IS_ECHO (self));

    self->feedback = CLAMP (feedback, 0.0f, 1.0f);
}

gfloat
lrg_echo_get_feedback (LrgEcho *self)
{
    g_return_val_if_fail (LRG_IS_ECHO (self), 0.0f);

    return self->feedback;
}

void
lrg_echo_set_dry (LrgEcho *self,
                  gfloat   dry)
{
    g_return_if_fail (LRG_IS_ECHO (self));

    self->dry = CLAMP (dry, 0.0f, 1.0f);
}

gfloat
lrg_echo_get_dry (LrgEcho *self)
{
    g_return_val_if_fail (LRG_IS_ECHO (self), 0.0f);

    return self->dry;
}

void
lrg_echo_set_wet (LrgEcho *self,
                  gfloat   wet)
{
    g_return_if_fail (LRG_IS_ECHO (self));

    self->wet = CLAMP (wet, 0.0f, 1.0f);
}

gfloat
lrg_echo_get_wet (LrgEcho *self)
{
    g_return_val_if_fail (LRG_IS_ECHO (self), 0.0f);

    return self->wet;
}
