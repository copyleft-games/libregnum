/* lrg-foley.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The synthesized foley (see lrg-foley.h).  Every clip is built from the
 * same few parts: a deterministic noise source (xorshift, seeded by kind
 * and variant), one-pole filters, damped sine "bodies" and short grains,
 * mixed into a float buffer that is then faded, peak-normalized and
 * written as a 16-bit mono WAV.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>

#include "lrg-foley.h"

#define FOLEY_PEAK     (0.8f)
#define FOLEY_FADE     (0.004)

G_DEFINE_ENUM_TYPE (LrgFoleyKind, lrg_foley_kind,
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_DIRT, "footstep-dirt"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_STONE, "footstep-stone"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_WOOD, "footstep-wood"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_GRASS, "footstep-grass"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_GRAVEL, "footstep-gravel"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_MUD, "footstep-mud"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_SNOW, "footstep-snow"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_FOOTSTEP_WATER, "footstep-water"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_ARMOR_CLINK, "armor-clink"),
    G_DEFINE_ENUM_VALUE (LRG_FOLEY_RAIN_LOOP, "rain-loop"))

/* ==========================================================================
 * Parts
 * ========================================================================== */

typedef struct
{
    guint32 state;
    guint   rate;
    gfloat *out;
    guint   frames;
} Synth;

/* xorshift32, never 0 */
static guint32
next_random (Synth *s)
{
    guint32 x = s->state != 0 ? s->state : 0x9E3779B9u;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->state = x;
    return x;
}

/* 0 .. 1 */
static gfloat
unit (Synth *s)
{
    return (gfloat)(next_random (s) & 0xFFFFFFu) / (gfloat)0xFFFFFFu;
}

/* -1 .. 1 white noise */
static gfloat
noise (Synth *s)
{
    return unit (s) * 2.0f - 1.0f;
}

/* a value in [lo, hi] */
static gfloat
between (Synth *s,
         gfloat lo,
         gfloat hi)
{
    return lo + (hi - lo) * unit (s);
}

/* one-pole low-pass coefficient for a cut-off in Hz */
static gfloat
lp_coeff (Synth *s,
          gfloat hz)
{
    return 1.0f - expf (-2.0f * (gfloat)G_PI * hz / (gfloat)s->rate);
}

/*
 * add_body:
 * A damped sine: @amp at @hz, decaying by 1/e in @decay seconds, starting
 * @at seconds in; @sweep multiplies the frequency by that much over the
 * decay (a thud falling in pitch, a bubble rising).
 */
static void
add_body (Synth  *s,
          gdouble at,
          gfloat  hz,
          gfloat  sweep,
          gfloat  decay,
          gfloat  amp)
{
    guint   start = (guint)(at * s->rate);
    guint   length = (guint)(decay * 6.0f * (gfloat)s->rate);
    gdouble phase = 0.0;
    guint   i;

    for (i = 0; i < length && start + i < s->frames; i++)
    {
        gfloat t = (gfloat)i / (gfloat)s->rate;
        gfloat f = hz * powf (sweep, MIN (t / decay, 1.0f));

        phase += 2.0 * G_PI * f / s->rate;
        s->out[start + i] += amp * expf (-t / decay) * (gfloat)sin (phase);
    }
}

/*
 * add_grain:
 * A burst of band-limited noise (high-passed by subtracting a slow
 * low-pass, then low-passed at @hz): @amp, @length seconds with a
 * raised-cosine shape, starting @at seconds in.
 */
static void
add_grain (Synth  *s,
           gdouble at,
           gfloat  length,
           gfloat  low_hz,
           gfloat  high_hz,
           gfloat  amp)
{
    guint  start = (guint)(at * s->rate);
    guint  n = MAX ((guint)(length * (gfloat)s->rate), 2u);
    gfloat a_low = lp_coeff (s, low_hz);
    gfloat a_high = lp_coeff (s, high_hz);
    gfloat slow = 0.0f;
    gfloat fast = 0.0f;
    guint  i;

    for (i = 0; i < n && start + i < s->frames; i++)
    {
        gfloat x = noise (s);
        gfloat shape = 0.5f - 0.5f * cosf (2.0f * (gfloat)G_PI * (gfloat)i / (gfloat)(n - 1));

        slow += a_low * (x - slow);
        fast += a_high * (x - fast);
        s->out[start + i] += amp * shape * (fast - slow);
    }
}

/* low-passed noise held under an exponential decay, @at seconds in */
static void
add_wash (Synth  *s,
          gdouble at,
          gfloat  decay,
          gfloat  hz,
          gfloat  amp)
{
    guint  start = (guint)(at * s->rate);
    guint  length = (guint)(decay * 5.0f * (gfloat)s->rate);
    gfloat a = lp_coeff (s, hz);
    gfloat y = 0.0f;
    guint  i;

    for (i = 0; i < length && start + i < s->frames; i++)
    {
        gfloat t = (gfloat)i / (gfloat)s->rate;

        y += a * (noise (s) - y);
        s->out[start + i] += amp * expf (-t / decay) * y * 3.0f;
    }
}

/* ==========================================================================
 * The kinds
 * ========================================================================== */

static const gdouble durations[] = {
    0.24, 0.22, 0.24, 0.26, 0.32, 0.36, 0.30, 0.42, 0.60, 4.0
};

gdouble
lrg_foley_get_duration (LrgFoleyKind kind)
{
    if ((guint)kind >= G_N_ELEMENTS (durations))
        return 0.0;
    return durations[kind];
}

static void
make_dirt (Synth *s)
{
    add_body (s, 0.0, between (s, 70.0f, 110.0f), 0.6f, 0.035f, 1.0f);
    add_wash (s, 0.0, 0.03f, between (s, 700.0f, 1100.0f), 0.8f);
    add_grain (s, 0.01, 0.05f, 200.0f, 1800.0f, 0.25f);
}

static void
make_stone (Synth *s)
{
    gfloat ring = between (s, 1100.0f, 1700.0f);

    add_grain (s, 0.0, 0.008f, 800.0f, 6000.0f, 0.9f);
    add_body (s, 0.0, between (s, 150.0f, 230.0f), 0.7f, 0.045f, 0.9f);
    add_body (s, 0.0, ring, 1.0f, 0.05f, 0.18f);
    add_body (s, 0.0, ring * 1.51f, 1.0f, 0.035f, 0.1f);
}

static void
make_wood (Synth *s)
{
    gfloat f = between (s, 120.0f, 170.0f);

    add_grain (s, 0.0, 0.006f, 500.0f, 4000.0f, 0.5f);
    add_body (s, 0.0, f, 0.9f, 0.05f, 1.0f);
    add_body (s, 0.0, f * 2.3f, 0.95f, 0.04f, 0.5f);
    add_body (s, 0.0, f * 3.9f, 1.0f, 0.03f, 0.2f);
}

static void
make_grass (Synth *s)
{
    add_body (s, 0.0, between (s, 60.0f, 90.0f), 0.7f, 0.03f, 0.5f);
    add_grain (s, 0.0, 0.09f, 1200.0f, 5000.0f, 0.7f);
    add_grain (s, between (s, 0.05f, 0.09f), 0.08f, 1500.0f, 6000.0f, 0.4f);
}

static void
make_gravel (Synth *s)
{
    guint n = 7 + (guint)(unit (s) * 4.0f);
    guint i;

    add_body (s, 0.0, between (s, 80.0f, 120.0f), 0.7f, 0.03f, 0.35f);
    for (i = 0; i < n; i++)
        add_grain (s, (gdouble)(unit (s) * 0.18f), between (s, 0.006f, 0.018f), 1500.0f,
                   between (s, 4000.0f, 9000.0f), between (s, 0.35f, 0.8f));
}

static void
make_mud (Synth *s)
{
    add_body (s, 0.0, between (s, 90.0f, 130.0f), 0.45f, 0.06f, 0.9f);
    add_wash (s, 0.0, 0.07f, between (s, 350.0f, 600.0f), 1.0f);
    add_body (s, 0.14, between (s, 140.0f, 220.0f), 2.2f, 0.03f, 0.35f);
    add_wash (s, 0.13, 0.04f, 900.0f, 0.3f);
}

static void
make_snow (Synth *s)
{
    guint n = 9 + (guint)(unit (s) * 6.0f);
    guint i;

    add_body (s, 0.0, between (s, 60.0f, 90.0f), 0.7f, 0.025f, 0.25f);
    for (i = 0; i < n; i++)
        add_grain (s, (gdouble)(unit (s) * 0.17f), between (s, 0.008f, 0.02f), 2500.0f,
                   between (s, 6000.0f, 10000.0f), between (s, 0.2f, 0.55f));
}

static void
make_water (Synth *s)
{
    guint n = 3 + (guint)(unit (s) * 3.0f);
    guint i;

    add_wash (s, 0.0, 0.07f, 3500.0f, 0.9f);
    add_grain (s, 0.0, 0.12f, 400.0f, 3000.0f, 0.5f);
    for (i = 0; i < n; i++)
        add_body (s, (gdouble)(0.02f + unit (s) * 0.25f), between (s, 350.0f, 700.0f), 2.6f,
                  between (s, 0.012f, 0.03f), between (s, 0.25f, 0.5f));
}

static void
make_clink (Synth *s)
{
    gfloat base = between (s, 1700.0f, 2600.0f);
    guint  partial;

    add_grain (s, 0.0, 0.004f, 2000.0f, 9000.0f, 0.5f);
    /* inharmonic partials of a struck plate: ratios of a free bar */
    for (partial = 0; partial < 4; partial++)
    {
        static const gfloat ratio[4] = { 1.0f, 2.76f, 5.4f, 8.93f };

        add_body (s, 0.0, base * ratio[partial] * between (s, 0.98f, 1.02f), 1.0f,
                  between (s, 0.04f, 0.09f) / (1.0f + 0.5f * (gfloat)partial), 0.5f / (1.0f + (gfloat)partial));
    }
}

/*
 * make_rain:
 * A bed of high-passed noise with random droplet ticks, built a quarter
 * second longer than it plays; the extra tail is mixed onto the head, so
 * the last sample leads straight into the first.
 */
static void
make_rain (Synth *s,
           guint  playing)
{
    guint  extra = s->frames - playing;
    guint  i;
    gfloat slow = 0.0f;
    gfloat mid = 0.0f;
    gfloat a_slow = lp_coeff (s, 600.0f);
    gfloat a_mid = lp_coeff (s, 7000.0f);
    gfloat drift = 0.0f;
    gfloat a_drift = lp_coeff (s, 0.7f);

    for (i = 0; i < s->frames; i++)
    {
        gfloat x = noise (s);

        slow += a_slow * (x - slow);
        mid += a_mid * (x - mid);
        drift += a_drift * (noise (s) - drift);
        s->out[i] += (mid - slow) * (0.55f + 0.6f * drift);
    }
    for (i = 0; i < (guint)(playing / 600); i++)
        add_grain (s, (gdouble)unit (s) * (gdouble)s->frames / (gdouble)s->rate, between (s, 0.002f, 0.006f),
                   2500.0f, 9000.0f, between (s, 0.15f, 0.5f));

    /* the seam: the tail fades out over the head fading in */
    for (i = 0; i < extra; i++)
    {
        gfloat t = (gfloat)i / (gfloat)extra;

        s->out[i] = s->out[i] * t + s->out[playing + i] * (1.0f - t);
    }
}

/* ==========================================================================
 * WAV image
 * ========================================================================== */

static void
put16 (guint8 *p,
       guint16 v)
{
    p[0] = (guint8)(v & 0xFF);
    p[1] = (guint8)(v >> 8);
}

static void
put32 (guint8 *p,
       guint32 v)
{
    put16 (p, (guint16)(v & 0xFFFF));
    put16 (p + 2, (guint16)(v >> 16));
}

GBytes *
lrg_foley_synthesize (LrgFoleyKind   kind,
                      guint          variant,
                      guint          sample_rate,
                      GError       **error)
{
    Synth    synth;
    guint    playing;
    gboolean loops;
    gfloat   peak;
    guint    fade;
    guint    i;
    guint8  *wav;
    gsize    data_bytes;

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (sample_rate == 0)
        sample_rate = LRG_FOLEY_DEFAULT_RATE;
    if ((guint)kind >= G_N_ELEMENTS (durations))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "unknown foley kind %d", (gint)kind);
        return NULL;
    }
    if (sample_rate < 8000 || sample_rate > 96000)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "foley sample rate %u is outside 8000 .. 96000", sample_rate);
        return NULL;
    }

    loops = kind == LRG_FOLEY_RAIN_LOOP;
    playing = (guint)(durations[kind] * sample_rate);
    synth.rate = sample_rate;
    synth.frames = playing + (loops ? sample_rate / 4 : 0);
    synth.out = g_new0 (gfloat, synth.frames);
    /* the seed mixes the kind and the variant, so no two takes share a stream */
    synth.state = ((guint32)kind + 1u) * 2654435761u ^ (variant + 1u) * 2246822519u;
    next_random (&synth);
    next_random (&synth);

    switch (kind)
    {
    case LRG_FOLEY_FOOTSTEP_DIRT: make_dirt (&synth); break;
    case LRG_FOLEY_FOOTSTEP_STONE: make_stone (&synth); break;
    case LRG_FOLEY_FOOTSTEP_WOOD: make_wood (&synth); break;
    case LRG_FOLEY_FOOTSTEP_GRASS: make_grass (&synth); break;
    case LRG_FOLEY_FOOTSTEP_GRAVEL: make_gravel (&synth); break;
    case LRG_FOLEY_FOOTSTEP_MUD: make_mud (&synth); break;
    case LRG_FOLEY_FOOTSTEP_SNOW: make_snow (&synth); break;
    case LRG_FOLEY_FOOTSTEP_WATER: make_water (&synth); break;
    case LRG_FOLEY_ARMOR_CLINK: make_clink (&synth); break;
    case LRG_FOLEY_RAIN_LOOP:
    default:
        make_rain (&synth, playing);
        break;
    }

    /* short fades (not for the loop, whose ends meet), then the peak */
    if (!loops)
    {
        fade = MAX ((guint)(FOLEY_FADE * sample_rate), 1u);
        for (i = 0; i < fade && i < playing; i++)
        {
            gfloat g = (gfloat)i / (gfloat)fade;

            synth.out[i] *= g;
            synth.out[playing - 1 - i] *= g;
        }
    }
    peak = 0.0f;
    for (i = 0; i < playing; i++)
        peak = MAX (peak, fabsf (synth.out[i]));
    if (peak < 1e-6f)
        peak = 1.0f;

    data_bytes = (gsize)playing * 2;
    wav = g_malloc0 (44 + data_bytes);
    memcpy (wav, "RIFF", 4);
    put32 (wav + 4, (guint32)(36 + data_bytes));
    memcpy (wav + 8, "WAVEfmt ", 8);
    put32 (wav + 16, 16);
    put16 (wav + 20, 1);
    put16 (wav + 22, 1);
    put32 (wav + 24, sample_rate);
    put32 (wav + 28, sample_rate * 2);
    put16 (wav + 32, 2);
    put16 (wav + 34, 16);
    memcpy (wav + 36, "data", 4);
    put32 (wav + 40, (guint32)data_bytes);
    for (i = 0; i < playing; i++)
        put16 (wav + 44 + (gsize)i * 2,
               (guint16)(gint16)lrintf (CLAMP (synth.out[i] / peak * FOLEY_PEAK, -1.0f, 1.0f) * 32767.0f));
    g_free (synth.out);
    return g_bytes_new_take (wav, 44 + data_bytes);
}
