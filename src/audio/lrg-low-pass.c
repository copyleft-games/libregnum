/* lrg-low-pass.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The gliding biquad low-pass (see lrg-low-pass.h and
 * docs/modules/audio/effects.org).
 *
 * The coefficients are the cookbook's LPF:
 *
 *   w0 = 2 pi fc / fs, alpha = sin(w0) / (2 Q)
 *   b0 = b2 = (1 - cos w0) / 2, b1 = 1 - cos w0
 *   a0 = 1 + alpha, a1 = -2 cos w0, a2 = 1 - alpha
 *
 * normalised by a0 and run in transposed direct form II, one state pair
 * per channel.  While the cut-off glides (geometrically, so an octave
 * takes the same time anywhere in the range) the coefficients are
 * recomputed every SUB_BLOCK frames.  A filter that is fully open passes
 * the samples untouched and forgets its state.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-low-pass.h"

#define SUB_BLOCK       (32)
#define DEFAULT_Q       (0.70710678f)
#define DEFAULT_GLIDE   (0.08f)
#define MAX_GLIDE       (5.0f)
#define SNAP_RATIO      (0.002)

struct _LrgLowPass
{
    LrgAudioEffect parent_instance;

    gfloat target;          /* Hz */
    gfloat current;         /* Hz, where the glide is */
    gfloat q;
    gfloat glide;           /* seconds */

    /* the coefficients for (coef_cutoff, coef_rate, coef_q) */
    gfloat coef_cutoff;
    guint  coef_rate;
    gfloat coef_q;
    gfloat b0, b1, b2, a1, a2;

    gfloat z1[LRG_AUDIO_EFFECT_MAX_CHANNELS];
    gfloat z2[LRG_AUDIO_EFFECT_MAX_CHANNELS];
};

G_DEFINE_FINAL_TYPE (LrgLowPass, lrg_low_pass, LRG_TYPE_AUDIO_EFFECT)

static void
compute_coefficients (LrgLowPass *self,
                      gfloat      cutoff,
                      guint       rate)
{
    gdouble fc;
    gdouble w0;
    gdouble alpha;
    gdouble cw;
    gdouble a0;

    if (cutoff == self->coef_cutoff && rate == self->coef_rate && self->q == self->coef_q)
        return;

    /* below Nyquist with margin: the bilinear transform warps above */
    fc = MIN ((gdouble)cutoff, 0.45 * (gdouble)rate);
    w0 = 2.0 * G_PI * fc / (gdouble)rate;
    alpha = sin (w0) / (2.0 * (gdouble)self->q);
    cw = cos (w0);
    a0 = 1.0 + alpha;

    self->b0 = (gfloat)(((1.0 - cw) / 2.0) / a0);
    self->b1 = (gfloat)((1.0 - cw) / a0);
    self->b2 = self->b0;
    self->a1 = (gfloat)((-2.0 * cw) / a0);
    self->a2 = (gfloat)((1.0 - alpha) / a0);
    self->coef_cutoff = cutoff;
    self->coef_rate = rate;
    self->coef_q = self->q;
}

/* one glide step over @frames frames */
static void
glide_step (LrgLowPass *self,
            guint       frames,
            guint       rate)
{
    gdouble k;
    gdouble cur;
    gdouble tgt;

    if (self->current == self->target)
        return;
    if (self->glide <= 0.0f)
    {
        self->current = self->target;
        return;
    }
    k = 1.0 - exp (-(gdouble)frames / ((gdouble)self->glide * (gdouble)rate));
    cur = log ((gdouble)self->current);
    tgt = log ((gdouble)self->target);
    cur += (tgt - cur) * k;
    self->current = (gfloat)exp (cur);
    if (fabs ((gdouble)self->current / (gdouble)self->target - 1.0) < SNAP_RATIO)
        self->current = self->target;
}

static void
lrg_low_pass_reset (LrgAudioEffect *effect)
{
    LrgLowPass *self = LRG_LOW_PASS (effect);

    memset (self->z1, 0, sizeof (self->z1));
    memset (self->z2, 0, sizeof (self->z2));
}

static void
lrg_low_pass_process (LrgAudioEffect *effect,
                      gfloat         *frames,
                      guint           n_frames,
                      guint           channels,
                      guint           sample_rate)
{
    LrgLowPass *self = LRG_LOW_PASS (effect);
    guint       done;

    done = 0;
    while (done < n_frames)
    {
        guint block;
        guint f;
        guint c;

        block = MIN (n_frames - done, (guint)SUB_BLOCK);

        /* fully open: untouched, and no stale state for later */
        if (self->current >= LRG_LOW_PASS_OPEN && self->target >= LRG_LOW_PASS_OPEN)
        {
            lrg_low_pass_reset (effect);
            return;
        }

        compute_coefficients (self, self->current, sample_rate);
        for (f = 0; f < block; f++)
        {
            gfloat *frame;

            frame = frames + (gsize)(done + f) * channels;
            for (c = 0; c < channels; c++)
            {
                gfloat x;
                gfloat y;

                x = frame[c];
                y = self->b0 * x + self->z1[c];
                self->z1[c] = self->b1 * x - self->a1 * y + self->z2[c];
                self->z2[c] = self->b2 * x - self->a2 * y;
                frame[c] = y;
            }
        }
        glide_step (self, block, sample_rate);
        done += block;
    }
}

static void
lrg_low_pass_class_init (LrgLowPassClass *klass)
{
    LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

    effect_class->process = lrg_low_pass_process;
    effect_class->reset = lrg_low_pass_reset;
}

static void
lrg_low_pass_init (LrgLowPass *self)
{
    self->target = LRG_LOW_PASS_OPEN;
    self->current = LRG_LOW_PASS_OPEN;
    self->q = DEFAULT_Q;
    self->glide = DEFAULT_GLIDE;
    self->coef_cutoff = -1.0f;
}

LrgLowPass *
lrg_low_pass_new (gfloat cutoff)
{
    LrgLowPass *self;

    self = g_object_new (LRG_TYPE_LOW_PASS, NULL);
    lrg_low_pass_set_cutoff (self, cutoff);
    self->current = self->target;
    return self;
}

void
lrg_low_pass_set_cutoff (LrgLowPass *self,
                         gfloat      cutoff)
{
    g_return_if_fail (LRG_IS_LOW_PASS (self));

    self->target = CLAMP (cutoff, LRG_LOW_PASS_MIN_CUTOFF, LRG_LOW_PASS_OPEN);
}

gfloat
lrg_low_pass_get_cutoff (LrgLowPass *self)
{
    g_return_val_if_fail (LRG_IS_LOW_PASS (self), 0.0f);

    return self->target;
}

gfloat
lrg_low_pass_get_current_cutoff (LrgLowPass *self)
{
    g_return_val_if_fail (LRG_IS_LOW_PASS (self), 0.0f);

    return self->current;
}

void
lrg_low_pass_set_resonance (LrgLowPass *self,
                            gfloat      q)
{
    g_return_if_fail (LRG_IS_LOW_PASS (self));

    self->q = CLAMP (q, 0.1f, 10.0f);
}

gfloat
lrg_low_pass_get_resonance (LrgLowPass *self)
{
    g_return_val_if_fail (LRG_IS_LOW_PASS (self), 0.0f);

    return self->q;
}

void
lrg_low_pass_set_glide (LrgLowPass *self,
                        gfloat      seconds)
{
    g_return_if_fail (LRG_IS_LOW_PASS (self));

    self->glide = CLAMP (seconds, 0.0f, MAX_GLIDE);
}

gfloat
lrg_low_pass_get_glide (LrgLowPass *self)
{
    g_return_val_if_fail (LRG_IS_LOW_PASS (self), 0.0f);

    return self->glide;
}
