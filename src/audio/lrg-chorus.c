/* lrg-chorus.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The three-tap chorus (see lrg-chorus.h and
 * docs/modules/audio/effects.org).
 *
 * One ring per channel, long enough for twice the longest base delay.
 * Tap k of channel c reads the ring
 *
 *   delay x (1 + depth x sin (2 pi (phase + k / 3 + c / 4)))
 *
 * seconds back, with linear interpolation between samples; the quarter
 * cycle between channels spreads the taps across the stereo field.  The
 * phase advances by rate / sample_rate per frame.  Per sample:
 *
 *   taps    = the three delayed reads
 *   ring[w] = x + feedback x mean (taps)
 *   y       = dry x + sum (wet[k] taps[k])
 *
 * The ring is sized on the first block for its layout and again only
 * when the rate or the channel count changes.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-chorus.h"

#define MAX_FEEDBACK (0.95f)

struct _LrgChorus
{
    LrgAudioEffect parent_instance;

    gfloat   delay;      /* seconds */
    gfloat   rate;       /* Hz */
    gfloat   depth;
    gfloat   feedback;
    gfloat   dry;
    gfloat   wet[LRG_CHORUS_N_TAPS];

    gdouble  phase;      /* 0 .. 1 */
    gfloat  *ring;       /* size x channels, interleaved */
    guint    size;
    guint    index;      /* the next write */
    guint    sample_rate;
    guint    channels;
};

G_DEFINE_FINAL_TYPE (LrgChorus, lrg_chorus, LRG_TYPE_AUDIO_EFFECT)

static void
size_ring (LrgChorus *self,
           guint      rate,
           guint      channels)
{
    if (rate == self->sample_rate && channels == self->channels && self->ring != NULL)
        return;
    g_free (self->ring);
    /* twice the longest delay plus room for the interpolation */
    self->size = (guint)ceil (2.0 * (gdouble)LRG_CHORUS_MAX_DELAY * (gdouble)rate) + 4;
    self->ring = g_new0 (gfloat, (gsize)self->size * channels);
    self->index = 0;
    self->sample_rate = rate;
    self->channels = channels;
}

/* the ring of channel @c read @back frames (fractional) before the write */
static gfloat
read_back (LrgChorus *self,
           guint      c,
           gdouble    back)
{
    gdouble pos;
    gint    i0;
    guint   a;
    guint   b;
    gdouble frac;

    pos = (gdouble)self->index - back;
    while (pos < 0.0)
        pos += (gdouble)self->size;
    i0 = (gint)floor (pos);
    frac = pos - (gdouble)i0;
    a = (guint)i0 % self->size;
    b = (a + 1) % self->size;
    return (gfloat)((1.0 - frac) * self->ring[(gsize)a * self->channels + c] +
                    frac * self->ring[(gsize)b * self->channels + c]);
}

static void
lrg_chorus_reset (LrgAudioEffect *effect)
{
    LrgChorus *self = LRG_CHORUS (effect);

    if (self->ring != NULL)
        memset (self->ring, 0, (gsize)self->size * self->channels * sizeof (gfloat));
    self->index = 0;
    self->phase = 0.0;
}

static void
lrg_chorus_process (LrgAudioEffect *effect,
                    gfloat         *frames,
                    guint           n_frames,
                    guint           channels,
                    guint           sample_rate)
{
    LrgChorus *self = LRG_CHORUS (effect);
    gdouble    base;
    gdouble    depth;
    gdouble    step;
    gfloat     feedback;
    gfloat     dry;
    gfloat     wet[LRG_CHORUS_N_TAPS];
    guint      f;
    guint      c;
    guint      k;

    size_ring (self, sample_rate, channels);
    base = (gdouble)self->delay * (gdouble)sample_rate;
    depth = (gdouble)self->depth;
    step = (gdouble)self->rate / (gdouble)sample_rate;
    feedback = self->feedback;
    dry = self->dry;
    for (k = 0; k < LRG_CHORUS_N_TAPS; k++)
        wet[k] = self->wet[k];

    for (f = 0; f < n_frames; f++)
    {
        gfloat *frame;
        gfloat *slot;

        frame = frames + (gsize)f * channels;
        slot = self->ring + (gsize)self->index * channels;
        for (c = 0; c < channels; c++)
        {
            gfloat taps[LRG_CHORUS_N_TAPS];
            gfloat sum;
            gfloat out;
            gfloat x;

            x = frame[c];
            sum = 0.0f;
            out = dry * x;
            for (k = 0; k < LRG_CHORUS_N_TAPS; k++)
            {
                gdouble swing;
                gdouble back;

                swing = sin (2.0 * G_PI * (self->phase + (gdouble)k / (gdouble)LRG_CHORUS_N_TAPS +
                                           (gdouble)c * 0.25));
                /* at least one frame back: the write has not happened yet */
                back = MAX (1.0, base * (1.0 + depth * swing));
                taps[k] = read_back (self, c, back);
                sum += taps[k];
                out += wet[k] * taps[k];
            }
            slot[c] = x + feedback * (sum / (gfloat)LRG_CHORUS_N_TAPS);
            frame[c] = out;
        }
        if (++self->index >= self->size)
            self->index = 0;
        self->phase += step;
        if (self->phase >= 1.0)
            self->phase -= floor (self->phase);
    }
}

static void
lrg_chorus_finalize (GObject *object)
{
    LrgChorus *self = LRG_CHORUS (object);

    g_clear_pointer (&self->ring, g_free);

    G_OBJECT_CLASS (lrg_chorus_parent_class)->finalize (object);
}

static void
lrg_chorus_class_init (LrgChorusClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

    object_class->finalize = lrg_chorus_finalize;
    effect_class->process = lrg_chorus_process;
    effect_class->reset = lrg_chorus_reset;
}

static void
lrg_chorus_init (LrgChorus *self)
{
    guint k;

    self->delay = 0.04f;
    self->rate = 0.8f;
    self->depth = 0.03f;
    self->feedback = 0.0f;
    self->dry = 1.0f;
    for (k = 0; k < LRG_CHORUS_N_TAPS; k++)
        self->wet[k] = 0.5f;
}

LrgChorus *
lrg_chorus_new (void)
{
    return g_object_new (LRG_TYPE_CHORUS, NULL);
}

void
lrg_chorus_set_delay (LrgChorus *self,
                      gfloat     delay)
{
    g_return_if_fail (LRG_IS_CHORUS (self));

    self->delay = CLAMP (delay, 0.0f, LRG_CHORUS_MAX_DELAY);
}

gfloat
lrg_chorus_get_delay (LrgChorus *self)
{
    g_return_val_if_fail (LRG_IS_CHORUS (self), 0.0f);

    return self->delay;
}

void
lrg_chorus_set_rate (LrgChorus *self,
                     gfloat     rate)
{
    g_return_if_fail (LRG_IS_CHORUS (self));

    self->rate = CLAMP (rate, 0.0f, LRG_CHORUS_MAX_RATE);
}

gfloat
lrg_chorus_get_rate (LrgChorus *self)
{
    g_return_val_if_fail (LRG_IS_CHORUS (self), 0.0f);

    return self->rate;
}

void
lrg_chorus_set_depth (LrgChorus *self,
                      gfloat     depth)
{
    g_return_if_fail (LRG_IS_CHORUS (self));

    self->depth = CLAMP (depth, 0.0f, 1.0f);
}

gfloat
lrg_chorus_get_depth (LrgChorus *self)
{
    g_return_val_if_fail (LRG_IS_CHORUS (self), 0.0f);

    return self->depth;
}

void
lrg_chorus_set_feedback (LrgChorus *self,
                         gfloat     feedback)
{
    g_return_if_fail (LRG_IS_CHORUS (self));

    self->feedback = CLAMP (feedback, 0.0f, MAX_FEEDBACK);
}

gfloat
lrg_chorus_get_feedback (LrgChorus *self)
{
    g_return_val_if_fail (LRG_IS_CHORUS (self), 0.0f);

    return self->feedback;
}

void
lrg_chorus_set_dry (LrgChorus *self,
                    gfloat     dry)
{
    g_return_if_fail (LRG_IS_CHORUS (self));

    self->dry = CLAMP (dry, 0.0f, 1.0f);
}

gfloat
lrg_chorus_get_dry (LrgChorus *self)
{
    g_return_val_if_fail (LRG_IS_CHORUS (self), 0.0f);

    return self->dry;
}

void
lrg_chorus_set_tap_wet (LrgChorus *self,
                        guint      tap,
                        gfloat     wet)
{
    g_return_if_fail (LRG_IS_CHORUS (self));
    g_return_if_fail (tap < LRG_CHORUS_N_TAPS);

    self->wet[tap] = CLAMP (wet, 0.0f, 1.0f);
}

gfloat
lrg_chorus_get_tap_wet (LrgChorus *self,
                        guint      tap)
{
    g_return_val_if_fail (LRG_IS_CHORUS (self), 0.0f);

    return tap < LRG_CHORUS_N_TAPS ? self->wet[tap] : 0.0f;
}
