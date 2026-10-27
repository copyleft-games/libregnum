/* test-audio-effects.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the DSP effects (LrgReverb, LrgLowPass), their chain
 * (LrgAudioEffectChain) and the layered ambient bed (LrgSoundBed).
 * Everything runs on synthetic buffers: impulses, sines and constant
 * clips written here, no audio device and no data file.  The mix
 * attachment is checked to refuse without a device.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#define RATE (48000)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* the energy of channel @c of @frames between two frame indices */
static gdouble
energy (const gfloat *frames,
        guint         channels,
        guint         c,
        guint         from,
        guint         to)
{
    gdouble sum;
    guint   f;

    sum = 0.0;
    for (f = from; f < to; f++)
        sum += (gdouble)frames[f * channels + c] * (gdouble)frames[f * channels + c];
    return sum;
}

/* an impulse of 1 on every channel at frame 0, the rest silence */
static gfloat *
impulse (guint n_frames,
         guint channels)
{
    gfloat *frames;
    guint   c;

    frames = g_new0 (gfloat, (gsize)n_frames * channels);
    for (c = 0; c < channels; c++)
        frames[c] = 1.0f;
    return frames;
}

/* peak amplitude of a mono sine at @hz run through @effect (after settling) */
static gdouble
sine_gain (LrgAudioEffect *effect,
           gdouble         hz)
{
    g_autofree gfloat *frames = NULL;
    guint              n;
    guint              f;
    gdouble            peak;

    n = RATE;
    frames = g_new (gfloat, n);
    for (f = 0; f < n; f++)
        frames[f] = (gfloat)sin (2.0 * G_PI * hz * (gdouble)f / RATE);
    lrg_audio_effect_reset (effect);
    lrg_audio_effect_process (effect, frames, n, 1, RATE);
    peak = 0.0;
    for (f = n / 2; f < n; f++)
        peak = MAX (peak, fabs ((gdouble)frames[f]));
    return peak;
}

/* ==========================================================================
 * Reverb
 * ========================================================================== */

/* fully dry, the reverb is transparent: the impulse comes back as it went in */
static void
test_reverb_dry_is_identity (void)
{
    g_autoptr(LrgReverb) reverb = NULL;
    g_autofree gfloat   *frames = NULL;
    guint                f;

    reverb = lrg_reverb_new ();
    lrg_reverb_set_preset (reverb, LRG_REVERB_PRESET_OFF);
    frames = impulse (RATE / 4, 2);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, RATE / 4, 2, RATE);
    g_assert_cmpfloat_with_epsilon (frames[0], 1.0f, 1e-6);
    g_assert_cmpfloat_with_epsilon (frames[1], 1.0f, 1e-6);
    for (f = 1; f < RATE / 4; f++)
    {
        g_assert_cmpfloat_with_epsilon (frames[2 * f], 0.0f, 1e-7);
        g_assert_cmpfloat_with_epsilon (frames[2 * f + 1], 0.0f, 1e-7);
    }
}

/*
 * The impulse response of a cave: nothing before the first comb delay
 * plus the pre-delay, then a tail that decays (the second half second
 * holds less energy than the first, the third less than the second),
 * decorrelated between left and right, and finite.
 */
static void
test_reverb_impulse_response (void)
{
    g_autoptr(LrgReverb) reverb = NULL;
    g_autofree gfloat   *frames = NULL;
    guint                n;
    guint                silent;
    guint                f;
    gdouble              e1;
    gdouble              e2;
    gdouble              e3;
    gboolean             differs;

    reverb = lrg_reverb_new ();
    lrg_reverb_set_preset (reverb, LRG_REVERB_PRESET_CAVE);
    g_assert_cmpint (lrg_reverb_get_preset (reverb), ==, LRG_REVERB_PRESET_CAVE);
    /* no glide from the room preset of the constructor in this test */
    lrg_audio_effect_reset (LRG_AUDIO_EFFECT (reverb));

    n = 3 * RATE / 2;
    frames = impulse (n, 2);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, n, 2, RATE);

    /* the shortest comb (1116 at 44.1 kHz) plus 20 ms pre-delay */
    silent = (guint)(1116.0 * RATE / 44100.0) + (guint)(0.020 * RATE) - 2;
    for (f = 1; f < silent; f++)
        g_assert_cmpfloat_with_epsilon (frames[2 * f], 0.0f, 1e-7);

    e1 = energy (frames, 2, 0, 1, RATE / 2);
    e2 = energy (frames, 2, 0, RATE / 2, RATE);
    e3 = energy (frames, 2, 0, RATE, n);
    g_assert_cmpfloat (e1, >, 0.0);
    g_assert_cmpfloat (e2, <, e1);
    g_assert_cmpfloat (e3, <, e2);
    g_assert_cmpfloat (e3, >, 0.0);   /* a cave still rings after a second */

    differs = FALSE;
    for (f = silent; f < n && !differs; f++)
    {
        g_assert_true (isfinite (frames[2 * f]));
        differs = fabsf (frames[2 * f] - frames[2 * f + 1]) > 1e-6f;
    }
    g_assert_true (differs);
}

/* a larger room keeps more energy late in the tail than a small one */
static void
test_reverb_room_size (void)
{
    g_autoptr(LrgReverb) small = NULL;
    g_autoptr(LrgReverb) large = NULL;
    g_autofree gfloat   *a = NULL;
    g_autofree gfloat   *b = NULL;
    guint                n;
    gdouble              ratio_small;
    gdouble              ratio_large;

    n = RATE;
    small = lrg_reverb_new ();
    large = lrg_reverb_new ();
    lrg_reverb_set_preset (small, LRG_REVERB_PRESET_ROOM);
    lrg_reverb_set_preset (large, LRG_REVERB_PRESET_QUARRY);
    lrg_reverb_set_wet (small, 0.5f);
    lrg_reverb_set_wet (large, 0.5f);
    lrg_reverb_set_pre_delay (small, 0.0f);
    lrg_reverb_set_pre_delay (large, 0.0f);
    g_assert_cmpfloat (lrg_reverb_get_room_size (large), >, lrg_reverb_get_room_size (small));

    a = impulse (n, 1);
    b = impulse (n, 1);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (small), a, n, 1, RATE);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (large), b, n, 1, RATE);
    ratio_small = energy (a, 1, 0, n / 2, n) / energy (a, 1, 0, 1, n / 2);
    ratio_large = energy (b, 1, 0, n / 2, n) / energy (b, 1, 0, 1, n / 2);
    g_assert_cmpfloat (ratio_large, >, ratio_small * 2.0);
}

/* reset forgets the tail; disabling passes the samples untouched */
static void
test_reverb_reset_and_disable (void)
{
    g_autoptr(LrgReverb) reverb = NULL;
    g_autofree gfloat   *frames = NULL;
    g_autofree gfloat   *quiet = NULL;
    guint                n;
    guint                f;

    n = RATE / 2;
    reverb = lrg_reverb_new ();
    lrg_reverb_set_preset (reverb, LRG_REVERB_PRESET_HALL);
    frames = impulse (n, 2);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, n, 2, RATE);

    /* after a reset a block of silence stays silent */
    lrg_audio_effect_reset (LRG_AUDIO_EFFECT (reverb));
    quiet = g_new0 (gfloat, (gsize)n * 2);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), quiet, n, 2, RATE);
    for (f = 0; f < 2 * n; f++)
        g_assert_cmpfloat (quiet[f], ==, 0.0f);

    /* disabled: an impulse goes through unchanged */
    lrg_audio_effect_set_enabled (LRG_AUDIO_EFFECT (reverb), FALSE);
    g_assert_false (lrg_audio_effect_get_enabled (LRG_AUDIO_EFFECT (reverb)));
    g_free (frames);
    frames = impulse (n, 2);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, n, 2, RATE);
    g_assert_cmpfloat (frames[0], ==, 1.0f);
    for (f = 2; f < 2 * n; f++)
        g_assert_cmpfloat (frames[f], ==, 0.0f);

    /* unsupported layouts are left alone */
    lrg_audio_effect_set_enabled (LRG_AUDIO_EFFECT (reverb), TRUE);
    g_free (frames);
    frames = impulse (16, 2);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, 16, 0, RATE);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, 16, 2, 100);
    g_assert_cmpfloat (frames[0], ==, 1.0f);
}

/* parameters clamp; wet changes glide instead of jumping */
static void
test_reverb_parameters (void)
{
    g_autoptr(LrgReverb) reverb = NULL;
    g_autofree gfloat   *frames = NULL;

    reverb = lrg_reverb_new ();
    lrg_reverb_set_room_size (reverb, 3.0f);
    g_assert_cmpfloat (lrg_reverb_get_room_size (reverb), ==, 1.0f);
    lrg_reverb_set_damping (reverb, -1.0f);
    g_assert_cmpfloat (lrg_reverb_get_damping (reverb), ==, 0.0f);
    lrg_reverb_set_pre_delay (reverb, 9.0f);
    g_assert_cmpfloat (lrg_reverb_get_pre_delay (reverb), ==, LRG_REVERB_MAX_PRE_DELAY);
    lrg_reverb_set_width (reverb, 0.25f);
    g_assert_cmpfloat (lrg_reverb_get_width (reverb), ==, 0.25f);

    /* dry 1 -> 0: the very first frame is still almost fully dry */
    lrg_reverb_set_wet (reverb, 0.0f);
    lrg_reverb_set_dry (reverb, 0.0f);
    g_assert_cmpfloat (lrg_reverb_get_dry (reverb), ==, 0.0f);
    frames = impulse (8, 1);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb), frames, 8, 1, RATE);
    g_assert_cmpfloat (frames[0], >, 0.99f);
}

/* ==========================================================================
 * Low-pass
 * ========================================================================== */

/* DC passes at unity, the impulse response sums to the DC gain */
static void
test_low_pass_impulse_response (void)
{
    g_autoptr(LrgLowPass) filter = NULL;
    g_autofree gfloat    *frames = NULL;
    gdouble               sum;
    guint                 n;
    guint                 f;

    n = RATE / 4;
    filter = lrg_low_pass_new (1000.0f);
    g_assert_cmpfloat (lrg_low_pass_get_cutoff (filter), ==, 1000.0f);
    g_assert_cmpfloat (lrg_low_pass_get_current_cutoff (filter), ==, 1000.0f);
    frames = impulse (n, 1);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, n, 1, RATE);
    sum = 0.0;
    for (f = 0; f < n; f++)
        sum += frames[f];
    g_assert_cmpfloat_with_epsilon (sum, 1.0, 1e-3);
    /* the response is smeared: the first sample is far below the impulse */
    g_assert_cmpfloat (frames[0], <, 0.05f);
    g_assert_cmpfloat_with_epsilon (frames[n - 1], 0.0f, 1e-6);
}

/* a decade below the cut-off passes, a decade above loses ~40 dB */
static void
test_low_pass_frequency_response (void)
{
    g_autoptr(LrgLowPass) filter = NULL;
    gdouble               low;
    gdouble               high;
    gdouble               at_cutoff;

    filter = lrg_low_pass_new (800.0f);
    low = sine_gain (LRG_AUDIO_EFFECT (filter), 80.0);
    at_cutoff = sine_gain (LRG_AUDIO_EFFECT (filter), 800.0);
    high = sine_gain (LRG_AUDIO_EFFECT (filter), 8000.0);
    g_assert_cmpfloat_with_epsilon (low, 1.0, 0.02);
    g_assert_cmpfloat_with_epsilon (at_cutoff, 0.7071, 0.03);  /* -3 dB, Butterworth */
    g_assert_cmpfloat (high, <, 0.015);                       /* below -36 dB */
}

/* the cut-off glides geometrically, and an open filter is transparent */
static void
test_low_pass_glide_and_open (void)
{
    g_autoptr(LrgLowPass) filter = NULL;
    g_autofree gfloat    *frames = NULL;
    gfloat                mid;
    guint                 f;

    filter = lrg_low_pass_new (LRG_LOW_PASS_OPEN);
    frames = g_new0 (gfloat, RATE);
    for (f = 0; f < RATE; f++)
        frames[f] = (f % 2) ? 0.5f : -0.5f;   /* Nyquist: what a filter kills first */
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, RATE, 1, RATE);
    for (f = 0; f < RATE; f++)
        g_assert_cmpfloat (fabsf (frames[f]), ==, 0.5f);

    /* diving: 20 kHz -> 500 Hz over the glide (0.08 s time constant) */
    lrg_low_pass_set_cutoff (filter, 500.0f);
    g_assert_cmpfloat (lrg_low_pass_get_current_cutoff (filter), ==, LRG_LOW_PASS_OPEN);
    memset (frames, 0, sizeof (gfloat) * RATE);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, (guint)(0.08 * RATE), 1, RATE);
    mid = lrg_low_pass_get_current_cutoff (filter);
    g_assert_cmpfloat (mid, <, LRG_LOW_PASS_OPEN);
    g_assert_cmpfloat (mid, >, 500.0f);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, RATE, 1, RATE);
    g_assert_cmpfloat (lrg_low_pass_get_current_cutoff (filter), ==, 500.0f);

    /* clamping */
    lrg_low_pass_set_cutoff (filter, 1.0f);
    g_assert_cmpfloat (lrg_low_pass_get_cutoff (filter), ==, LRG_LOW_PASS_MIN_CUTOFF);
    lrg_low_pass_set_resonance (filter, 100.0f);
    g_assert_cmpfloat (lrg_low_pass_get_resonance (filter), ==, 10.0f);
    lrg_low_pass_set_glide (filter, 0.0f);
    lrg_low_pass_set_cutoff (filter, 2000.0f);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, 32, 1, RATE);
    g_assert_cmpfloat (lrg_low_pass_get_current_cutoff (filter), ==, 2000.0f);
}

/* ==========================================================================
 * High-pass
 * ========================================================================== */

/* no DC passes: the impulse response sums to 0 and a constant decays */
static void
test_high_pass_impulse_response (void)
{
    g_autoptr(LrgHighPass) filter = NULL;
    g_autofree gfloat     *frames = NULL;
    gdouble                sum;
    guint                  n;
    guint                  f;

    n = RATE / 4;
    filter = lrg_high_pass_new (1000.0f);
    g_assert_cmpfloat (lrg_high_pass_get_cutoff (filter), ==, 1000.0f);
    g_assert_cmpfloat (lrg_high_pass_get_current_cutoff (filter), ==, 1000.0f);
    frames = impulse (n, 1);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, n, 1, RATE);
    sum = 0.0;
    for (f = 0; f < n; f++)
        sum += frames[f];
    g_assert_cmpfloat_with_epsilon (sum, 0.0, 1e-3);
    /* the edge goes through: the first sample is most of the impulse */
    g_assert_cmpfloat (frames[0], >, 0.8f);
    g_assert_cmpfloat_with_epsilon (frames[n - 1], 0.0f, 1e-6);

    /* a constant input (DC, stereo) dies away in both channels */
    g_free (frames);
    frames = g_new (gfloat, (gsize)n * 2);
    for (f = 0; f < 2 * n; f++)
        frames[f] = 0.5f;
    lrg_audio_effect_reset (LRG_AUDIO_EFFECT (filter));
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, n, 2, RATE);
    g_assert_cmpfloat_with_epsilon (frames[2 * n - 2], 0.0f, 1e-5);
    g_assert_cmpfloat_with_epsilon (frames[2 * n - 1], 0.0f, 1e-5);
}

/* a decade above the cut-off passes, a decade below loses ~40 dB */
static void
test_high_pass_frequency_response (void)
{
    g_autoptr(LrgHighPass) filter = NULL;
    gdouble                low;
    gdouble                high;
    gdouble                at_cutoff;

    filter = lrg_high_pass_new (800.0f);
    low = sine_gain (LRG_AUDIO_EFFECT (filter), 80.0);
    at_cutoff = sine_gain (LRG_AUDIO_EFFECT (filter), 800.0);
    /* 7900 Hz: not a divisor of the rate, so the samples reach the peak */
    high = sine_gain (LRG_AUDIO_EFFECT (filter), 7900.0);
    g_assert_cmpfloat_with_epsilon (high, 1.0, 0.03);
    g_assert_cmpfloat_with_epsilon (at_cutoff, 0.7071, 0.03);  /* -3 dB, Butterworth */
    g_assert_cmpfloat (low, <, 0.015);                         /* below -36 dB */

    /* resonance peaks just above the cut-off */
    lrg_high_pass_set_resonance (filter, 4.0f);
    g_assert_cmpfloat (sine_gain (LRG_AUDIO_EFFECT (filter), 820.0), >, 2.0);
}

/* the cut-off glides geometrically, and an open filter is transparent */
static void
test_high_pass_glide_and_open (void)
{
    g_autoptr(LrgHighPass) filter = NULL;
    g_autofree gfloat     *frames = NULL;
    g_autofree gfloat     *copy = NULL;
    gfloat                 mid;
    guint                  f;

    filter = lrg_high_pass_new (LRG_HIGH_PASS_OPEN);
    frames = g_new0 (gfloat, RATE);
    for (f = 0; f < RATE; f++)
        frames[f] = (gfloat)(0.5 * sin (2.0 * G_PI * 30.0 * (gdouble)f / RATE));
    copy = g_memdup2 (frames, sizeof (gfloat) * RATE);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, RATE, 1, RATE);
    for (f = 0; f < RATE; f++)
        g_assert_cmpfloat (frames[f], ==, copy[f]);

    /* 20 Hz -> 500 Hz over the glide (0.08 s time constant) */
    lrg_high_pass_set_cutoff (filter, 500.0f);
    g_assert_cmpfloat (lrg_high_pass_get_current_cutoff (filter), ==, LRG_HIGH_PASS_OPEN);
    memset (frames, 0, sizeof (gfloat) * RATE);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, (guint)(0.08 * RATE), 1, RATE);
    mid = lrg_high_pass_get_current_cutoff (filter);
    g_assert_cmpfloat (mid, >, LRG_HIGH_PASS_OPEN);
    g_assert_cmpfloat (mid, <, 500.0f);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, RATE, 1, RATE);
    g_assert_cmpfloat (lrg_high_pass_get_current_cutoff (filter), ==, 500.0f);

    /* clamping */
    lrg_high_pass_set_cutoff (filter, 1.0f);
    g_assert_cmpfloat (lrg_high_pass_get_cutoff (filter), ==, LRG_HIGH_PASS_OPEN);
    lrg_high_pass_set_cutoff (filter, 96000.0f);
    g_assert_cmpfloat (lrg_high_pass_get_cutoff (filter), ==, LRG_HIGH_PASS_MAX_CUTOFF);
    lrg_high_pass_set_resonance (filter, 100.0f);
    g_assert_cmpfloat (lrg_high_pass_get_resonance (filter), ==, 10.0f);
    lrg_high_pass_set_resonance (filter, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_high_pass_get_resonance (filter), 0.1f, 1e-6);
    lrg_high_pass_set_glide (filter, 9.0f);
    g_assert_cmpfloat (lrg_high_pass_get_glide (filter), ==, 5.0f);
    lrg_high_pass_set_glide (filter, 0.0f);
    lrg_high_pass_set_cutoff (filter, 2000.0f);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter), frames, 32, 1, RATE);
    g_assert_cmpfloat (lrg_high_pass_get_current_cutoff (filter), ==, 2000.0f);
}

/* ==========================================================================
 * Chain
 * ========================================================================== */

/* effects run in order; removing one takes it out; no device: no mix */
static void
test_chain (void)
{
    g_autoptr(LrgAudioEffectChain) chain = NULL;
    g_autoptr(LrgLowPass)          filter = NULL;
    g_autoptr(LrgReverb)           reverb = NULL;
    g_autofree gfloat             *a = NULL;
    g_autofree gfloat             *b = NULL;
    guint                          n;
    guint                          f;

    n = RATE / 4;
    chain = lrg_audio_effect_chain_new ();
    filter = lrg_low_pass_new (600.0f);
    reverb = lrg_reverb_new ();
    lrg_reverb_set_preset (reverb, LRG_REVERB_PRESET_STONE_ROOM);

    lrg_audio_effect_chain_add (chain, LRG_AUDIO_EFFECT (filter));
    lrg_audio_effect_chain_add (chain, LRG_AUDIO_EFFECT (reverb));
    lrg_audio_effect_chain_add (chain, LRG_AUDIO_EFFECT (filter));   /* no duplicate */
    g_assert_cmpuint (lrg_audio_effect_chain_get_count (chain), ==, 2);
    g_assert_true (lrg_audio_effect_chain_get_effect (chain, 0) == LRG_AUDIO_EFFECT (filter));
    g_assert_true (lrg_audio_effect_chain_get_effect (chain, 1) == LRG_AUDIO_EFFECT (reverb));
    g_assert_null (lrg_audio_effect_chain_get_effect (chain, 2));

    /* the chain equals the two effects one after the other */
    a = impulse (n, 2);
    lrg_audio_effect_chain_process (chain, a, n, 2, RATE);
    {
        g_autoptr(LrgLowPass) filter2 = lrg_low_pass_new (600.0f);
        g_autoptr(LrgReverb)  reverb2 = lrg_reverb_new ();

        lrg_reverb_set_preset (reverb2, LRG_REVERB_PRESET_STONE_ROOM);
        b = impulse (n, 2);
        lrg_audio_effect_process (LRG_AUDIO_EFFECT (filter2), b, n, 2, RATE);
        lrg_audio_effect_process (LRG_AUDIO_EFFECT (reverb2), b, n, 2, RATE);
    }
    for (f = 0; f < 2 * n; f++)
        g_assert_cmpfloat_with_epsilon (a[f], b[f], 1e-6);

    g_assert_true (lrg_audio_effect_chain_remove (chain, LRG_AUDIO_EFFECT (reverb)));
    g_assert_false (lrg_audio_effect_chain_remove (chain, LRG_AUDIO_EFFECT (reverb)));
    g_assert_cmpuint (lrg_audio_effect_chain_get_count (chain), ==, 1);
    lrg_audio_effect_chain_reset (chain);

    /* headless: the device mix cannot be attached */
    g_assert_false (lrg_audio_effect_chain_attach_mix (chain, 0));
    g_assert_false (lrg_audio_effect_chain_is_attached (chain));
    lrg_audio_effect_chain_detach_mix (chain);
}

/* ==========================================================================
 * Sound bed
 * ========================================================================== */

/* a constant clip of @value, @frames long, mono */
static gboolean
add_constant (LrgSoundBed *bed,
              const gchar *name,
              gfloat       value,
              guint        frames,
              guint        rate)
{
    g_autofree gfloat *samples = NULL;
    g_autoptr(GError)  error = NULL;
    guint              f;
    gboolean           ok;

    samples = g_new (gfloat, frames);
    for (f = 0; f < frames; f++)
        samples[f] = value;
    ok = lrg_sound_bed_add_layer_samples (bed, name, samples, frames, 1, rate, &error);
    g_assert_no_error (error);
    return ok;
}

/* two layers at full level add up; a silent layer adds nothing */
static void
test_bed_layering (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autofree gfloat     *out = NULL;
    g_auto(GStrv)          names = NULL;
    guint                  f;

    bed = lrg_sound_bed_new (RATE);
    g_assert_cmpuint (lrg_sound_bed_get_sample_rate (bed), ==, RATE);
    g_assert_true (add_constant (bed, "wind", 0.25f, 1000, RATE));
    g_assert_true (add_constant (bed, "rain", 0.125f, 700, RATE));
    g_assert_true (add_constant (bed, "drips", 0.5f, 300, RATE));
    g_assert_true (lrg_sound_bed_has_layer (bed, "rain"));
    names = lrg_sound_bed_get_layer_names (bed);
    g_assert_cmpuint (g_strv_length (names), ==, 3);
    g_assert_cmpstr (names[0], ==, "wind");

    out = g_new (gfloat, 2 * 4096);
    /* nothing audible yet */
    lrg_sound_bed_render (bed, out, 4096);
    for (f = 0; f < 2 * 4096; f++)
        g_assert_cmpfloat (out[f], ==, 0.0f);

    g_assert_true (lrg_sound_bed_set_level (bed, "wind", 1.0f, 0.0));
    g_assert_true (lrg_sound_bed_set_level (bed, "rain", 1.0f, 0.0));
    g_assert_false (lrg_sound_bed_set_level (bed, "nope", 1.0f, 0.0));
    lrg_sound_bed_render (bed, out, 4096);
    for (f = 0; f < 4096; f++)
    {
        g_assert_cmpfloat_with_epsilon (out[2 * f], 0.375f, 1e-6);
        g_assert_cmpfloat_with_epsilon (out[2 * f + 1], 0.375f, 1e-6);
    }
    /* the loops wrapped: 4096 frames of 1000- and 700-frame clips */
    g_assert_cmpuint (lrg_sound_bed_get_play_count (bed, "wind"), ==, 5);
    g_assert_cmpuint (lrg_sound_bed_get_play_count (bed, "rain"), ==, 6);
    g_assert_cmpuint (lrg_sound_bed_get_play_count (bed, "drips"), ==, 0);

    /* the master volume scales the lot */
    lrg_sound_bed_set_volume (bed, 0.5f);
    lrg_sound_bed_render (bed, out, 16);
    g_assert_cmpfloat_with_epsilon (out[0], 0.1875f, 1e-6);

    g_assert_true (lrg_sound_bed_remove_layer (bed, "rain"));
    g_assert_false (lrg_sound_bed_has_layer (bed, "rain"));
}

/* a loop is seamless: a ramp clip comes back sample for sample */
static void
test_bed_loop_seam (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autoptr(GError)      error = NULL;
    gfloat                 ramp[200];
    gfloat                 out[2 * 1000];
    guint                  f;

    bed = lrg_sound_bed_new (RATE);
    for (f = 0; f < 100; f++)
    {
        ramp[2 * f] = (gfloat)f / 100.0f;
        ramp[2 * f + 1] = -(gfloat)f / 100.0f;
    }
    g_assert_true (lrg_sound_bed_add_layer_samples (bed, "ramp", ramp, 200, 2, RATE, &error));
    lrg_sound_bed_set_level (bed, "ramp", 1.0f, 0.0);
    lrg_sound_bed_render (bed, out, 1000);
    for (f = 0; f < 1000; f++)
    {
        g_assert_cmpfloat_with_epsilon (out[2 * f], (gfloat)(f % 100) / 100.0f, 1e-6);
        g_assert_cmpfloat_with_epsilon (out[2 * f + 1], -(gfloat)(f % 100) / 100.0f, 1e-6);
    }
}

/* a crossfade: half way both are at half, at the end only the new one */
static void
test_bed_crossfade (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autofree gfloat     *out = NULL;

    bed = lrg_sound_bed_new (RATE);
    add_constant (bed, "town", 0.5f, 4800, RATE);
    add_constant (bed, "forest", 0.25f, 4800, RATE);
    lrg_sound_bed_set_level (bed, "town", 1.0f, 0.0);

    lrg_sound_bed_crossfade (bed, "town", "forest", 1.0);
    g_assert_cmpfloat (lrg_sound_bed_get_level (bed, "town"), ==, 0.0f);
    g_assert_cmpfloat (lrg_sound_bed_get_level (bed, "forest"), ==, 1.0f);

    out = g_new (gfloat, 2 * RATE);
    lrg_sound_bed_render (bed, out, RATE / 2);
    g_assert_cmpfloat_with_epsilon (lrg_sound_bed_get_gain (bed, "town"), 0.5f, 1e-3);
    g_assert_cmpfloat_with_epsilon (lrg_sound_bed_get_gain (bed, "forest"), 0.5f, 1e-3);
    /* the last frame of the half: 0.5 * 0.5 + 0.25 * 0.5 */
    g_assert_cmpfloat_with_epsilon (out[2 * (RATE / 2 - 1)], 0.375f, 1e-3);

    lrg_sound_bed_render (bed, out, RATE);
    g_assert_cmpfloat (lrg_sound_bed_get_gain (bed, "town"), ==, 0.0f);
    g_assert_cmpfloat (lrg_sound_bed_get_gain (bed, "forest"), ==, 1.0f);
    g_assert_cmpfloat_with_epsilon (out[2 * (RATE - 1)], 0.25f, 1e-6);
}

/* scattered shots: silence between plays, gaps inside their bounds */
static void
test_bed_scatter (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autofree gfloat     *out = NULL;
    guint                  n;
    guint                  f;
    guint                  shots;
    guint                  gap;
    guint                  min_gap;
    guint                  max_gap;
    gboolean               in_shot;

    n = 10 * RATE;
    bed = lrg_sound_bed_new (RATE);
    add_constant (bed, "drip", 1.0f, RATE / 10, RATE);   /* 0.1 s shots */
    g_assert_true (lrg_sound_bed_set_scatter (bed, "drip", 0.5, 1.0));
    g_assert_false (lrg_sound_bed_set_scatter (bed, "nope", 0.5, 1.0));
    lrg_sound_bed_set_seed (bed, 7);
    lrg_sound_bed_set_scatter (bed, "drip", 0.5, 1.0);   /* re-roll the first gap */
    lrg_sound_bed_set_level (bed, "drip", 1.0f, 0.0);

    out = g_new (gfloat, 2 * (gsize)n);
    lrg_sound_bed_render (bed, out, n);

    shots = 0;
    gap = 0;
    min_gap = G_MAXUINT;
    max_gap = 0;
    in_shot = FALSE;
    for (f = 0; f < n; f++)
    {
        gboolean on = out[2 * f] > 0.5f;

        if (on && !in_shot)
        {
            shots++;
            if (shots > 1)
            {
                min_gap = MIN (min_gap, gap);
                max_gap = MAX (max_gap, gap);
            }
        }
        if (!on)
            gap = in_shot ? 1 : gap + 1;
        in_shot = on;
    }
    /* 10 s of 0.1 s shots and 0.5 .. 1 s gaps: between 9 and 17 shots */
    g_assert_cmpuint (shots, >=, 9);
    g_assert_cmpuint (shots, <=, 17);
    g_assert_cmpuint (shots, ==, lrg_sound_bed_get_play_count (bed, "drip"));
    g_assert_cmpuint (min_gap, >=, (guint)(0.5 * RATE) - 1);
    g_assert_cmpuint (max_gap, <=, (guint)(1.0 * RATE) + 1);
}

/* the bed's own effects run on its mix; bad input is refused */
static void
test_bed_effects_and_errors (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autoptr(LrgLowPass)  filter = NULL;
    g_autoptr(GError)      error = NULL;
    g_autofree gfloat     *out = NULL;
    gfloat                 samples[4] = { 1.0f, -1.0f, 1.0f, -1.0f };
    guint                  n;
    guint                  f;
    gdouble                peak;

    n = RATE / 2;
    bed = lrg_sound_bed_new (RATE);
    /* a 11025 Hz clip alternating +-1: resampled, still a bright buzz */
    g_assert_true (lrg_sound_bed_add_layer_samples (bed, "buzz", samples, 4, 1, 11025, &error));
    lrg_sound_bed_set_level (bed, "buzz", 1.0f, 0.0);
    filter = lrg_low_pass_new (200.0f);
    lrg_audio_effect_chain_add (lrg_sound_bed_get_effects (bed), LRG_AUDIO_EFFECT (filter));
    out = g_new (gfloat, 2 * (gsize)n);
    lrg_sound_bed_render (bed, out, n);
    peak = 0.0;
    for (f = n / 2; f < n; f++)
        peak = MAX (peak, fabs ((gdouble)out[2 * f]));
    g_assert_cmpfloat (peak, <, 0.2);   /* the low-pass took the buzz away */

    /* errors */
    g_assert_false (lrg_sound_bed_add_layer_samples (bed, "buzz", samples, 4, 1, RATE, &error));
    g_assert_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_EXISTS);
    g_clear_error (&error);
    g_assert_false (lrg_sound_bed_add_layer_samples (bed, "x", samples, 4, 3, RATE, &error));
    g_assert_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_sound_bed_add_layer_samples (bed, "x", samples, 0, 1, RATE, &error));
    g_assert_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_INVALID);
    g_clear_error (&error);
    g_assert_false (lrg_sound_bed_add_layer_samples (bed, "x", samples, 4, 1, 10, &error));
    g_assert_error (error, LRG_SOUND_BED_ERROR, LRG_SOUND_BED_ERROR_INVALID);
    g_clear_error (&error);

    /* headless: no stream (starting one would open the device) */
    g_assert_false (lrg_sound_bed_has_stream (bed));
    lrg_sound_bed_stop_stream (bed);
    lrg_sound_bed_update (bed);
}

/* a wave data clip goes in through lrg_sound_bed_add_layer() */
static void
test_bed_wave_data (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autoptr(LrgWaveData) wave = NULL;
    g_autoptr(GError)      error = NULL;
    gfloat                 samples[64];
    gfloat                 out[2 * 64];
    guint                  f;

    for (f = 0; f < 64; f++)
        samples[f] = 0.5f;
    wave = lrg_wave_data_new_from_samples (RATE, 32, 1, (const guint8 *)samples, sizeof (samples));
    g_assert_nonnull (wave);
    bed = lrg_sound_bed_new (RATE);
    g_assert_true (lrg_sound_bed_add_layer (bed, "half", wave, &error));
    g_assert_no_error (error);
    lrg_sound_bed_set_level (bed, "half", 1.0f, 0.0);
    lrg_sound_bed_render (bed, out, 64);
    g_assert_cmpfloat_with_epsilon (out[10], 0.5f, 1e-3);
}

/* the device rate measured by the mix callback snaps to a standard rate */
static void
test_snap_sample_rate (void)
{
    g_assert_cmpuint (lrg_audio_snap_sample_rate (48000.0), ==, 48000);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (47210.0), ==, 48000);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (44873.0), ==, 44100);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (96500.0), ==, 96000);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (22100.0), ==, 22050);
    /* nothing near: not believed */
    g_assert_cmpuint (lrg_audio_snap_sample_rate (65000.0), ==, 0);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (0.0), ==, 0);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (-5.0), ==, 0);
    g_assert_cmpuint (lrg_audio_snap_sample_rate (1.0e9), ==, 0);
}

/* a chain that is not attached has no mix rate */
static void
test_mix_rate_unattached (void)
{
    g_autoptr(LrgAudioEffectChain) chain = lrg_audio_effect_chain_new ();
    gboolean measured = TRUE;

    g_assert_cmpuint (lrg_audio_effect_chain_get_mix_rate (chain, &measured), ==, 0);
    g_assert_false (measured);
}

/*
 * The bed on the audio thread (LRG_TEST_AUDIO_DEVICE=1): calls read back
 * at once, the audio thread renders (the play counts move without any
 * render() or update() call rendering), commands apply in order, and
 * after stop_stream () the renderer is back on this thread with the
 * state the calls asked for.
 */
static void
test_bed_stream (void)
{
    g_autoptr(LrgSoundBed) bed = NULL;
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) names = NULL;
    GrlAudioDevice *device;
    gfloat buffer[2 * 64];
    gint64 deadline;

    if (g_strcmp0 (g_getenv ("LRG_TEST_AUDIO_DEVICE"), "1") != 0)
    {
        g_test_skip ("set LRG_TEST_AUDIO_DEVICE=1 to open the audio device");
        return;
    }
    device = grl_audio_device_get_default ();
    if (device != NULL && !grl_audio_device_is_ready (device))
        grl_audio_device_init_audio (device);
    if (device == NULL || !grl_audio_device_is_ready (device))
    {
        g_test_skip ("no audio device");
        return;
    }
    grl_audio_device_set_master_volume (device, 0.0f);

    bed = lrg_sound_bed_new (RATE);
    g_assert_true (lrg_sound_bed_start_stream (bed, &error));
    g_assert_no_error (error);

    /* offline rendering is silent while the audio thread renders */
    buffer[0] = 1.0f;
    g_assert_cmpuint (lrg_sound_bed_render (bed, buffer, 64), ==, 0);
    g_assert_cmpfloat (buffer[0], ==, 0.0f);

    /* calls read back at once */
    g_assert_true (add_constant (bed, "wind", 0.25f, RATE / 100, RATE));
    g_assert_true (add_constant (bed, "rain", 0.5f, RATE / 100, RATE));
    g_assert_true (lrg_sound_bed_has_layer (bed, "wind"));
    g_assert_true (lrg_sound_bed_set_level (bed, "wind", 0.75f, 0.0));
    g_assert_cmpfloat (lrg_sound_bed_get_level (bed, "wind"), ==, 0.75f);
    g_assert_cmpfloat (lrg_sound_bed_get_gain (bed, "wind"), ==, 0.75f);
    lrg_sound_bed_crossfade (bed, NULL, "rain", 0.2);
    g_assert_cmpfloat (lrg_sound_bed_get_level (bed, "rain"), ==, 1.0f);
    lrg_sound_bed_set_volume (bed, 0.5f);
    g_assert_cmpfloat (lrg_sound_bed_get_volume (bed), ==, 0.5f);

    /* the audio thread plays them: 10 ms clips loop, the counts climb */
    deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;
    while ((lrg_sound_bed_get_play_count (bed, "wind") < 10 ||
            lrg_sound_bed_get_gain (bed, "rain") < 1.0f) &&
           g_get_monotonic_time () < deadline)
    {
        lrg_sound_bed_update (bed);
        g_usleep (10000);
    }
    g_assert_cmpuint (lrg_sound_bed_get_play_count (bed, "wind"), >=, 10);
    g_assert_cmpfloat (lrg_sound_bed_get_gain (bed, "rain"), ==, 1.0f);

    /* a removal is seen at once; the layer is freed by a later call */
    g_assert_true (lrg_sound_bed_remove_layer (bed, "rain"));
    g_assert_false (lrg_sound_bed_has_layer (bed, "rain"));
    names = lrg_sound_bed_get_layer_names (bed);
    g_assert_cmpuint (g_strv_length (names), ==, 1);
    g_assert_true (add_constant (bed, "rain", 1.0f, RATE / 100, RATE));
    g_assert_true (lrg_sound_bed_set_level (bed, "rain", 0.5f, 0.0));

    /* back on this thread: wind 0.25 x 0.75 + rain 1.0 x 0.5, x volume 0.5 */
    lrg_sound_bed_stop_stream (bed);
    g_assert_false (lrg_sound_bed_has_stream (bed));
    g_assert_cmpuint (lrg_sound_bed_render (bed, buffer, 64), ==, 64);
    g_assert_cmpfloat_with_epsilon (buffer[0], 0.5f * (0.25f * 0.75f + 1.0f * 0.5f), 1e-5);
    g_assert_cmpfloat_with_epsilon (buffer[1], buffer[0], 1e-6);

    grl_audio_device_close (device);
}

gint
main (gint   argc,
      gchar *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/audio-effects/reverb/dry-is-identity", test_reverb_dry_is_identity);
    g_test_add_func ("/audio-effects/reverb/impulse-response", test_reverb_impulse_response);
    g_test_add_func ("/audio-effects/reverb/room-size", test_reverb_room_size);
    g_test_add_func ("/audio-effects/reverb/reset-and-disable", test_reverb_reset_and_disable);
    g_test_add_func ("/audio-effects/reverb/parameters", test_reverb_parameters);
    g_test_add_func ("/audio-effects/low-pass/impulse-response", test_low_pass_impulse_response);
    g_test_add_func ("/audio-effects/low-pass/frequency-response", test_low_pass_frequency_response);
    g_test_add_func ("/audio-effects/low-pass/glide-and-open", test_low_pass_glide_and_open);
    g_test_add_func ("/audio-effects/high-pass/impulse-response", test_high_pass_impulse_response);
    g_test_add_func ("/audio-effects/high-pass/frequency-response", test_high_pass_frequency_response);
    g_test_add_func ("/audio-effects/high-pass/glide-and-open", test_high_pass_glide_and_open);
    g_test_add_func ("/audio-effects/chain", test_chain);
    g_test_add_func ("/audio-effects/sound-bed/layering", test_bed_layering);
    g_test_add_func ("/audio-effects/sound-bed/loop-seam", test_bed_loop_seam);
    g_test_add_func ("/audio-effects/sound-bed/crossfade", test_bed_crossfade);
    g_test_add_func ("/audio-effects/sound-bed/scatter", test_bed_scatter);
    g_test_add_func ("/audio-effects/sound-bed/effects-and-errors", test_bed_effects_and_errors);
    g_test_add_func ("/audio-effects/sound-bed/wave-data", test_bed_wave_data);
    g_test_add_func ("/audio-effects/sound-bed/stream", test_bed_stream);
    g_test_add_func ("/audio-effects/mix-rate/snap", test_snap_sample_rate);
    g_test_add_func ("/audio-effects/mix-rate/unattached", test_mix_rate_unattached);

    return g_test_run ();
}
