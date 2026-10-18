/* lrg-reverb.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The comb / all-pass reverb (see lrg-reverb.h and
 * docs/modules/audio/effects.org).
 *
 * Signal flow per block:
 *
 *   1. the input of every channel is summed to mono and scaled
 *      (FIXED_GAIN), then delayed by the pre-delay;
 *   2. two lanes (left and right tails) each run eight damped feedback
 *      comb filters in parallel and four all-pass filters in series; the
 *      right lane's delays are STEREO_SPREAD samples longer, which
 *      decorrelates the two tails;
 *   3. each output channel gets its dry input plus a width-weighted mix
 *      of the two tails; wet and dry glide to their targets.
 *
 * The delay lengths are the classic tunings at 44100 Hz scaled to the
 * block's sample rate.  They are (re)allocated when the rate changes,
 * which happens on the first block (on the audio thread for a chain on
 * the device mix) and never again in a running game.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-reverb.h"

#define N_COMBS        (8)
#define N_ALLPASSES    (4)
#define N_LANES        (2)
#define TUNING_RATE    (44100.0)
#define STEREO_SPREAD  (23)
#define FIXED_GAIN     (0.015f)
#define SCALE_WET      (3.0f)
#define SCALE_DAMP     (0.4f)
#define SCALE_ROOM     (0.28f)
#define OFFSET_ROOM    (0.7f)
#define ALLPASS_FEEDBACK (0.5f)
#define GLIDE_SECONDS  (0.03)

/* delay lengths in samples at 44100 Hz */
static const guint comb_tuning[N_COMBS] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const guint allpass_tuning[N_ALLPASSES] = { 556, 441, 341, 225 };

/* one delay line of a comb or an all-pass filter */
typedef struct
{
    gfloat *buffer;
    guint   size;
    guint   index;
    gfloat  store;   /* the comb's one-pole damping state */
} DelayLine;

typedef struct
{
    DelayLine combs[N_COMBS];
    DelayLine allpasses[N_ALLPASSES];
} Lane;

/* the parameters of a preset */
typedef struct
{
    gfloat room_size;
    gfloat damping;
    gfloat wet;
    gfloat dry;
    gfloat width;
    gfloat pre_delay;
} PresetValues;

static const PresetValues preset_values[] = {
    /* OFF */        { 0.50f, 0.50f, 0.00f, 1.00f, 1.00f, 0.000f },
    /* ROOM */       { 0.45f, 0.60f, 0.16f, 1.00f, 1.00f, 0.005f },
    /* STONE_ROOM */ { 0.68f, 0.30f, 0.28f, 0.95f, 1.00f, 0.010f },
    /* CAVE */       { 0.88f, 0.22f, 0.40f, 0.85f, 1.00f, 0.020f },
    /* QUARRY */     { 0.95f, 0.15f, 0.48f, 0.80f, 1.00f, 0.045f },
    /* HALL */       { 0.82f, 0.40f, 0.34f, 0.90f, 1.00f, 0.025f },
    /* UNDERWATER */ { 0.70f, 0.90f, 0.50f, 0.60f, 0.50f, 0.000f }
};

struct _LrgReverb
{
    LrgAudioEffect  parent_instance;

    LrgReverbPreset preset;
    gfloat          room_size;
    gfloat          damping;
    gfloat          wet;        /* targets */
    gfloat          dry;
    gfloat          width;
    gfloat          pre_delay;

    gfloat          wet_now;    /* glided values */
    gfloat          dry_now;

    guint           rate;       /* the rate the lines are sized for, 0 none */
    Lane            lanes[N_LANES];
    gfloat         *pre;        /* pre-delay ring */
    guint           pre_size;
    guint           pre_index;
};

G_DEFINE_FINAL_TYPE (LrgReverb, lrg_reverb, LRG_TYPE_AUDIO_EFFECT)

G_DEFINE_ENUM_TYPE (LrgReverbPreset, lrg_reverb_preset,
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_OFF, "off"),
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_ROOM, "room"),
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_STONE_ROOM, "stone-room"),
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_CAVE, "cave"),
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_QUARRY, "quarry"),
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_HALL, "hall"),
    G_DEFINE_ENUM_VALUE (LRG_REVERB_PRESET_UNDERWATER, "underwater"))

/* ==========================================================================
 * Delay lines
 * ========================================================================== */

static void
lines_free (LrgReverb *self)
{
    guint l;
    guint i;

    for (l = 0; l < N_LANES; l++)
    {
        for (i = 0; i < N_COMBS; i++)
            g_clear_pointer (&self->lanes[l].combs[i].buffer, g_free);
        for (i = 0; i < N_ALLPASSES; i++)
            g_clear_pointer (&self->lanes[l].allpasses[i].buffer, g_free);
    }
    g_clear_pointer (&self->pre, g_free);
    self->rate = 0;
}

/* a line of @tuning samples at 44100 Hz, at @rate, plus @spread */
static void
line_alloc (DelayLine *line,
            guint      tuning,
            guint      spread,
            guint      rate)
{
    line->size = (guint)((gdouble)(tuning + spread) * (gdouble)rate / TUNING_RATE);
    if (line->size < 1)
        line->size = 1;
    line->buffer = g_new0 (gfloat, line->size);
    line->index = 0;
    line->store = 0.0f;
}

static void
lines_alloc (LrgReverb *self,
             guint      rate)
{
    guint l;
    guint i;

    lines_free (self);
    for (l = 0; l < N_LANES; l++)
    {
        for (i = 0; i < N_COMBS; i++)
            line_alloc (&self->lanes[l].combs[i], comb_tuning[i], l * STEREO_SPREAD, rate);
        for (i = 0; i < N_ALLPASSES; i++)
            line_alloc (&self->lanes[l].allpasses[i], allpass_tuning[i], l * STEREO_SPREAD, rate);
    }
    self->pre_size = (guint)(LRG_REVERB_MAX_PRE_DELAY * (gfloat)rate) + 1;
    self->pre = g_new0 (gfloat, self->pre_size);
    self->pre_index = 0;
    self->rate = rate;
}

/* a damped feedback comb: the delayed sample, low-passed into the loop */
static inline gfloat
comb_tick (DelayLine *line,
           gfloat     input,
           gfloat     feedback,
           gfloat     damp)
{
    gfloat output;

    output = line->buffer[line->index];
    line->store = output * (1.0f - damp) + line->store * damp;
    line->buffer[line->index] = input + line->store * feedback;
    if (++line->index >= line->size)
        line->index = 0;
    return output;
}

/* a Schroeder all-pass diffuser */
static inline gfloat
allpass_tick (DelayLine *line,
              gfloat     input)
{
    gfloat delayed;
    gfloat output;

    delayed = line->buffer[line->index];
    output = delayed - input;
    line->buffer[line->index] = input + delayed * ALLPASS_FEEDBACK;
    if (++line->index >= line->size)
        line->index = 0;
    return output;
}

static gfloat
lane_tick (Lane   *lane,
           gfloat  input,
           gfloat  feedback,
           gfloat  damp)
{
    gfloat sum;
    guint  i;

    sum = 0.0f;
    for (i = 0; i < N_COMBS; i++)
        sum += comb_tick (&lane->combs[i], input, feedback, damp);
    for (i = 0; i < N_ALLPASSES; i++)
        sum = allpass_tick (&lane->allpasses[i], sum);
    return sum;
}

/* ==========================================================================
 * LrgAudioEffect
 * ========================================================================== */

static void
lrg_reverb_reset (LrgAudioEffect *effect)
{
    LrgReverb *self = LRG_REVERB (effect);
    guint      l;
    guint      i;

    if (self->rate == 0)
        return;
    for (l = 0; l < N_LANES; l++)
    {
        for (i = 0; i < N_COMBS; i++)
        {
            memset (self->lanes[l].combs[i].buffer, 0, sizeof (gfloat) * self->lanes[l].combs[i].size);
            self->lanes[l].combs[i].store = 0.0f;
        }
        for (i = 0; i < N_ALLPASSES; i++)
            memset (self->lanes[l].allpasses[i].buffer, 0,
                    sizeof (gfloat) * self->lanes[l].allpasses[i].size);
    }
    memset (self->pre, 0, sizeof (gfloat) * self->pre_size);
}

/*
 * lrg_reverb_process:
 * The block's parameters are read once (they are plain floats written
 * by the main thread); wet and dry glide per frame.
 */
static void
lrg_reverb_process (LrgAudioEffect *effect,
                    gfloat         *frames,
                    guint           n_frames,
                    guint           channels,
                    guint           sample_rate)
{
    LrgReverb *self = LRG_REVERB (effect);
    gfloat     feedback;
    gfloat     damp;
    gfloat     width;
    gfloat     wet_target;
    gfloat     dry_target;
    gfloat     step;
    guint      delay;
    guint      f;
    guint      c;

    if (self->rate != sample_rate)
        lines_alloc (self, sample_rate);

    feedback = self->room_size * SCALE_ROOM + OFFSET_ROOM;
    damp = self->damping * SCALE_DAMP;
    width = self->width;
    wet_target = self->wet;
    dry_target = self->dry;
    step = (gfloat)(1.0 / (GLIDE_SECONDS * (gdouble)sample_rate));
    delay = (guint)(self->pre_delay * (gfloat)sample_rate);
    if (delay >= self->pre_size)
        delay = self->pre_size - 1;

    for (f = 0; f < n_frames; f++)
    {
        gfloat *frame;
        gfloat  input;
        gfloat  delayed;
        gfloat  tail[N_LANES];
        gfloat  wet1;
        gfloat  wet2;
        guint   read;

        frame = frames + (gsize)f * channels;

        /* 1. mono input into the pre-delay */
        input = 0.0f;
        for (c = 0; c < channels; c++)
            input += frame[c];
        input *= FIXED_GAIN;
        self->pre[self->pre_index] = input;
        read = (self->pre_index + self->pre_size - delay) % self->pre_size;
        delayed = self->pre[read];
        if (++self->pre_index >= self->pre_size)
            self->pre_index = 0;

        /* 2. the two tails */
        tail[0] = lane_tick (&self->lanes[0], delayed, feedback, damp);
        tail[1] = lane_tick (&self->lanes[1], delayed, feedback, damp);

        /* 3. the glide and the output mix */
        if (self->wet_now < wet_target)
            self->wet_now = MIN (self->wet_now + step, wet_target);
        else if (self->wet_now > wet_target)
            self->wet_now = MAX (self->wet_now - step, wet_target);
        if (self->dry_now < dry_target)
            self->dry_now = MIN (self->dry_now + step, dry_target);
        else if (self->dry_now > dry_target)
            self->dry_now = MAX (self->dry_now - step, dry_target);

        wet1 = self->wet_now * SCALE_WET * (width / 2.0f + 0.5f);
        wet2 = self->wet_now * SCALE_WET * ((1.0f - width) / 2.0f);
        if (channels == 1)
        {
            frame[0] = frame[0] * self->dry_now + (tail[0] + tail[1]) * 0.5f * self->wet_now * SCALE_WET;
            continue;
        }
        for (c = 0; c < channels; c++)
        {
            gfloat own;
            gfloat other;

            own = tail[c % 2];
            other = tail[(c + 1) % 2];
            frame[c] = frame[c] * self->dry_now + own * wet1 + other * wet2;
        }
    }
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_reverb_finalize (GObject *object)
{
    lines_free (LRG_REVERB (object));

    G_OBJECT_CLASS (lrg_reverb_parent_class)->finalize (object);
}

static void
lrg_reverb_class_init (LrgReverbClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

    object_class->finalize = lrg_reverb_finalize;
    effect_class->process = lrg_reverb_process;
    effect_class->reset = lrg_reverb_reset;
}

static void
lrg_reverb_init (LrgReverb *self)
{
    lrg_reverb_set_preset (self, LRG_REVERB_PRESET_ROOM);
    self->wet_now = self->wet;
    self->dry_now = self->dry;
}

LrgReverb *
lrg_reverb_new (void)
{
    return g_object_new (LRG_TYPE_REVERB, NULL);
}

void
lrg_reverb_set_preset (LrgReverb       *self,
                       LrgReverbPreset  preset)
{
    const PresetValues *values;

    g_return_if_fail (LRG_IS_REVERB (self));
    g_return_if_fail ((guint)preset < G_N_ELEMENTS (preset_values));

    values = &preset_values[preset];
    self->preset = preset;
    self->room_size = values->room_size;
    self->damping = values->damping;
    self->wet = values->wet;
    self->dry = values->dry;
    self->width = values->width;
    self->pre_delay = values->pre_delay;
}

LrgReverbPreset
lrg_reverb_get_preset (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), LRG_REVERB_PRESET_OFF);

    return self->preset;
}

void
lrg_reverb_set_room_size (LrgReverb *self,
                          gfloat     room_size)
{
    g_return_if_fail (LRG_IS_REVERB (self));

    self->room_size = CLAMP (room_size, 0.0f, 1.0f);
}

gfloat
lrg_reverb_get_room_size (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), 0.0f);

    return self->room_size;
}

void
lrg_reverb_set_damping (LrgReverb *self,
                        gfloat     damping)
{
    g_return_if_fail (LRG_IS_REVERB (self));

    self->damping = CLAMP (damping, 0.0f, 1.0f);
}

gfloat
lrg_reverb_get_damping (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), 0.0f);

    return self->damping;
}

void
lrg_reverb_set_wet (LrgReverb *self,
                    gfloat     wet)
{
    g_return_if_fail (LRG_IS_REVERB (self));

    self->wet = CLAMP (wet, 0.0f, 1.0f);
}

gfloat
lrg_reverb_get_wet (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), 0.0f);

    return self->wet;
}

void
lrg_reverb_set_dry (LrgReverb *self,
                    gfloat     dry)
{
    g_return_if_fail (LRG_IS_REVERB (self));

    self->dry = CLAMP (dry, 0.0f, 1.0f);
}

gfloat
lrg_reverb_get_dry (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), 0.0f);

    return self->dry;
}

void
lrg_reverb_set_width (LrgReverb *self,
                      gfloat     width)
{
    g_return_if_fail (LRG_IS_REVERB (self));

    self->width = CLAMP (width, 0.0f, 1.0f);
}

gfloat
lrg_reverb_get_width (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), 0.0f);

    return self->width;
}

void
lrg_reverb_set_pre_delay (LrgReverb *self,
                          gfloat     seconds)
{
    g_return_if_fail (LRG_IS_REVERB (self));

    self->pre_delay = CLAMP (seconds, 0.0f, LRG_REVERB_MAX_PRE_DELAY);
}

gfloat
lrg_reverb_get_pre_delay (LrgReverb *self)
{
    g_return_val_if_fail (LRG_IS_REVERB (self), 0.0f);

    return self->pre_delay;
}
