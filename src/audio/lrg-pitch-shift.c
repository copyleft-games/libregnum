/* lrg-pitch-shift.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The pitch shifter (see lrg-pitch-shift.h and
 * docs/modules/audio/effects.org): the classic two-head delay-line
 * shifter.
 *
 * The input is written into a ring.  Two read heads trail the write by
 * delays that sweep through one window W at the speed (1 - ratio): a
 * head reading a delay that shrinks by (ratio - 1) samples per sample
 * plays the signal ratio times as fast, i.e. ratio times as high.  When
 * a head's delay runs out of the window it jumps to the other end; the
 * jump is hidden by weighting each head with sin^2 (pi phase) of its
 * place in the window, and the two heads are half a window apart, so
 * their weights always sum to 1 (sin^2 + cos^2) and each head is silent
 * exactly when it jumps.
 *
 * Per frame: phase += (1 - ratio) / W, kept in 0 .. 1; head A reads
 * phase x W frames back, head B ((phase + 0.5) mod 1) x W, both with
 * linear interpolation; y = sin^2 (pi phase_A) A + sin^2 (pi phase_B) B.
 *
 * A ratio of exactly 1 passes the samples untouched (and keeps writing
 * the ring, so a later change has history).  The ring is sized on the
 * first block for the window and the layout, and again only when they
 * change.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-pitch-shift.h"

struct _LrgPitchShift
{
    LrgAudioEffect parent_instance;

    gfloat   ratio;
    gfloat   window;     /* seconds */

    gdouble  phase;      /* head A's place in the window, 0 .. 1 */
    gfloat  *ring;       /* size x channels, interleaved */
    guint    size;
    guint    index;      /* the next write */
    guint    span;       /* the window in frames for (rate, window) */
    guint    rate;       /* the layout the ring is sized for, 0 none */
    guint    channels;
    gfloat   sized_window;
};

G_DEFINE_FINAL_TYPE (LrgPitchShift, lrg_pitch_shift, LRG_TYPE_AUDIO_EFFECT)

static void
size_ring (LrgPitchShift *self,
           guint          rate,
           guint          channels)
{
    if (rate == self->rate && channels == self->channels &&
        self->window == self->sized_window && self->ring != NULL)
        return;
    g_free (self->ring);
    self->span = (guint)MAX (4.0, floor ((gdouble)self->window * (gdouble)rate + 0.5));
    /* the window, one frame for the interpolation and a margin */
    self->size = self->span + 4;
    self->ring = g_new0 (gfloat, (gsize)self->size * channels);
    self->index = 0;
    self->phase = 0.0;
    self->rate = rate;
    self->channels = channels;
    self->sized_window = self->window;
}

/* channel @c read @back frames (fractional, >= 1) before the next write */
static gfloat
read_back (LrgPitchShift *self,
           guint          c,
           gdouble        back)
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
lrg_pitch_shift_reset (LrgAudioEffect *effect)
{
    LrgPitchShift *self = LRG_PITCH_SHIFT (effect);

    if (self->ring != NULL)
        memset (self->ring, 0, (gsize)self->size * self->channels * sizeof (gfloat));
    self->index = 0;
    self->phase = 0.0;
}

static void
lrg_pitch_shift_process (LrgAudioEffect *effect,
                         gfloat         *frames,
                         guint           n_frames,
                         guint           channels,
                         guint           sample_rate)
{
    LrgPitchShift *self = LRG_PITCH_SHIFT (effect);
    gboolean       bypass;
    gdouble        step;
    gdouble        span;
    guint          f;
    guint          c;

    size_ring (self, sample_rate, channels);
    bypass = self->ratio == 1.0f;
    span = (gdouble)self->span;
    step = (1.0 - (gdouble)self->ratio) / span;

    for (f = 0; f < n_frames; f++)
    {
        gfloat  *frame;
        gfloat  *slot;
        gdouble  phase_b;
        gdouble  gain_a;
        gdouble  gain_b;
        gdouble  back_a;
        gdouble  back_b;

        frame = frames + (gsize)f * channels;
        slot = self->ring + (gsize)self->index * channels;

        /* the heads' places and weights (a delay of at least one frame:
         * the newest sample is the one written last) */
        phase_b = self->phase + 0.5;
        if (phase_b >= 1.0)
            phase_b -= 1.0;
        gain_a = sin (G_PI * self->phase);
        gain_a *= gain_a;
        gain_b = sin (G_PI * phase_b);
        gain_b *= gain_b;
        back_a = 1.0 + self->phase * span;
        back_b = 1.0 + phase_b * span;

        for (c = 0; c < channels; c++)
        {
            gfloat x;

            x = frame[c];
            if (!bypass)
                frame[c] = (gfloat)(gain_a * read_back (self, c, back_a) +
                                    gain_b * read_back (self, c, back_b));
            slot[c] = x;
        }
        if (++self->index >= self->size)
            self->index = 0;

        if (!bypass)
        {
            self->phase += step;
            if (self->phase >= 1.0)
                self->phase -= floor (self->phase);
            else if (self->phase < 0.0)
                self->phase -= floor (self->phase);
        }
    }
}

static void
lrg_pitch_shift_finalize (GObject *object)
{
    LrgPitchShift *self = LRG_PITCH_SHIFT (object);

    g_clear_pointer (&self->ring, g_free);

    G_OBJECT_CLASS (lrg_pitch_shift_parent_class)->finalize (object);
}

static void
lrg_pitch_shift_class_init (LrgPitchShiftClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

    object_class->finalize = lrg_pitch_shift_finalize;
    effect_class->process = lrg_pitch_shift_process;
    effect_class->reset = lrg_pitch_shift_reset;
}

static void
lrg_pitch_shift_init (LrgPitchShift *self)
{
    self->ratio = 1.0f;
    self->window = 0.04f;
}

LrgPitchShift *
lrg_pitch_shift_new (gfloat ratio)
{
    LrgPitchShift *self;

    self = g_object_new (LRG_TYPE_PITCH_SHIFT, NULL);
    lrg_pitch_shift_set_ratio (self, ratio);
    return self;
}

void
lrg_pitch_shift_set_ratio (LrgPitchShift *self,
                           gfloat         ratio)
{
    g_return_if_fail (LRG_IS_PITCH_SHIFT (self));

    self->ratio = CLAMP (ratio, LRG_PITCH_SHIFT_MIN_RATIO, LRG_PITCH_SHIFT_MAX_RATIO);
}

gfloat
lrg_pitch_shift_get_ratio (LrgPitchShift *self)
{
    g_return_val_if_fail (LRG_IS_PITCH_SHIFT (self), 1.0f);

    return self->ratio;
}

void
lrg_pitch_shift_set_window (LrgPitchShift *self,
                            gfloat         window)
{
    g_return_if_fail (LRG_IS_PITCH_SHIFT (self));

    self->window = CLAMP (window, LRG_PITCH_SHIFT_MIN_WINDOW, LRG_PITCH_SHIFT_MAX_WINDOW);
}

gfloat
lrg_pitch_shift_get_window (LrgPitchShift *self)
{
    g_return_val_if_fail (LRG_IS_PITCH_SHIFT (self), 0.0f);

    return self->window;
}
