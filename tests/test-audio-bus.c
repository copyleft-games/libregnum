/* test-audio-bus.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the submix bus (LrgAudioBus: sends and inserts of
 * graylib voices) and the voice effects LrgEcho, LrgChorus and
 * LrgPitchShift.
 *
 * Headless: the voices are unloaded graylib objects (g_object_new),
 * whose processors grl_sound_process() runs as the device would; the
 * bus is mixed by hand with lrg_audio_bus_mix().  No audio device, no
 * data file; attaching to the device mix is checked to refuse.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#define RATE (48000)
#define EPSILON (0.0001)

/* raylib's pan law at the centre: 0.5 x 0.5 x (3 - 0.25) */
#define CENTRE_GAIN (0.6875)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gfloat *
constant_block (guint  n_frames,
                gfloat value)
{
    gfloat *frames;
    guint   i;

    frames = g_new (gfloat, (gsize)n_frames * 2);
    for (i = 0; i < n_frames * 2; i++)
        frames[i] = value;
    return frames;
}

/* a mono sine at @hz, @n_frames long */
static gfloat *
sine (guint   n_frames,
      gdouble hz)
{
    gfloat *frames;
    guint   f;

    frames = g_new (gfloat, n_frames);
    for (f = 0; f < n_frames; f++)
        frames[f] = (gfloat)sin (2.0 * G_PI * hz * (gdouble)f / (gdouble)RATE);
    return frames;
}

/* the frequency of a mono signal by its upward zero crossings */
static gdouble
crossing_rate (const gfloat *frames,
               guint         from,
               guint         to)
{
    guint first;
    guint last;
    guint count;
    guint f;

    first = 0;
    last = 0;
    count = 0;
    for (f = from + 1; f < to; f++)
    {
        if (frames[f - 1] < 0.0f && frames[f] >= 0.0f)
        {
            if (count == 0)
                first = f;
            last = f;
            count++;
        }
    }
    if (count < 2)
        return 0.0;
    return (gdouble)(count - 1) * (gdouble)RATE / (gdouble)(last - first);
}

/* ==========================================================================
 * LrgAudioBus
 * ========================================================================== */

static void
test_bus_send (void)
{
    g_autoptr(LrgAudioBus) bus = NULL;
    g_autoptr(GrlSound)    voice = NULL;
    g_autofree gfloat     *frames = NULL;
    g_autofree gfloat     *out = NULL;
    LrgAudioRoute          route;
    guint                  i;

    bus = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_SOUND, NULL);
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_level (bus), 1.0, EPSILON);
    g_assert_nonnull (lrg_audio_bus_get_chain (bus));

    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 0.5f));
    g_assert_true (lrg_audio_bus_is_routed (bus, G_OBJECT (voice)));
    g_assert_cmpuint (lrg_audio_bus_get_n_routes (bus), ==, 1);
    g_assert_cmpuint (grl_sound_get_n_processors (voice), ==, 1);
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_route_level (bus, G_OBJECT (voice), &route), 0.5, EPSILON);
    g_assert_cmpint (route, ==, LRG_AUDIO_ROUTE_SEND);

    /* a send leaves the voice alone and copies it, with the voice's
     * centre-panned gain and the route's level */
    frames = constant_block (64, 1.0f);
    grl_sound_process (voice, frames, 64);
    for (i = 0; i < 128; i++)
        g_assert_cmpfloat_with_epsilon (frames[i], 1.0, EPSILON);
    out = constant_block (64, 0.0f);
    lrg_audio_bus_mix (bus, out, 64, RATE);
    for (i = 0; i < 128; i++)
        g_assert_cmpfloat_with_epsilon (out[i], CENTRE_GAIN * 0.5, EPSILON);

    /* the next block starts empty */
    memset (out, 0, 128 * sizeof (gfloat));
    lrg_audio_bus_mix (bus, out, 64, RATE);
    for (i = 0; i < 128; i++)
        g_assert_cmpfloat_with_epsilon (out[i], 0.0, EPSILON);
}

static void
test_bus_insert_and_pan (void)
{
    g_autoptr(LrgAudioBus) bus = NULL;
    g_autoptr(GrlSound)    voice = NULL;
    g_autofree gfloat     *frames = NULL;
    g_autofree gfloat     *out = NULL;
    guint                  i;

    bus = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_SOUND, NULL);
    grl_sound_set_pan (voice, -1.0f);
    grl_sound_set_volume (voice, 0.5f);
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_INSERT, 1.0f));

    /* an insert takes the voice out of the mix; the bus carries it with
     * the voice's volume and pan (full left: all on the left) */
    frames = constant_block (32, 1.0f);
    grl_sound_process (voice, frames, 32);
    for (i = 0; i < 64; i++)
        g_assert_cmpfloat_with_epsilon (frames[i], 0.0, EPSILON);
    out = constant_block (32, 0.0f);
    lrg_audio_bus_mix (bus, out, 32, RATE);
    for (i = 0; i < 32; i++)
    {
        g_assert_cmpfloat_with_epsilon (out[i * 2], 0.5, EPSILON);
        g_assert_cmpfloat_with_epsilon (out[i * 2 + 1], 0.0, EPSILON);
    }
}

static void
test_bus_chunks_and_voices (void)
{
    g_autoptr(LrgAudioBus) bus = NULL;
    g_autoptr(GrlSound)    one = NULL;
    g_autoptr(GrlMusic)    two = NULL;
    g_autofree gfloat     *a = NULL;
    g_autofree gfloat     *b = NULL;
    g_autofree gfloat     *c = NULL;
    g_autofree gfloat     *out = NULL;
    guint                  f;

    bus = lrg_audio_bus_new ();
    one = g_object_new (GRL_TYPE_SOUND, NULL);
    two = g_object_new (GRL_TYPE_MUSIC, NULL);
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (one), LRG_AUDIO_ROUTE_SEND, 1.0f));
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (two), LRG_AUDIO_ROUTE_SEND, 1.0f));
    g_assert_cmpuint (lrg_audio_bus_get_n_routes (bus), ==, 2);

    /* raylib reads a voice in chunks: they follow each other in the
     * block, and a second voice adds into the same block from its start */
    a = constant_block (16, 1.0f);
    b = constant_block (16, 2.0f);
    c = constant_block (32, 4.0f);
    grl_sound_process (one, a, 16);
    grl_sound_process (one, b, 16);
    grl_music_process (two, c, 32);
    out = constant_block (32, 0.0f);
    lrg_audio_bus_mix (bus, out, 32, RATE);
    for (f = 0; f < 32; f++)
    {
        gdouble want;

        want = ((f < 16 ? 1.0 : 2.0) + 4.0) * CENTRE_GAIN;
        g_assert_cmpfloat_with_epsilon (out[f * 2], want, EPSILON);
        g_assert_cmpfloat_with_epsilon (out[f * 2 + 1], want, EPSILON);
    }

    /* after the mix the cursors start over */
    grl_sound_process (one, b, 16);
    memset (out, 0, 64 * sizeof (gfloat));
    lrg_audio_bus_mix (bus, out, 32, RATE);
    g_assert_cmpfloat_with_epsilon (out[0], 2.0 * CENTRE_GAIN, EPSILON);
    g_assert_cmpfloat_with_epsilon (out[16 * 2], 0.0, EPSILON);
}

static void
test_bus_levels (void)
{
    g_autoptr(LrgAudioBus)    bus = NULL;
    g_autoptr(GrlAudioStream) voice = NULL;
    g_autofree gfloat        *frames = NULL;
    g_autofree gfloat        *out = NULL;
    LrgAudioRoute             route;

    bus = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_AUDIO_STREAM, NULL);
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 1.0f));

    /* routing again changes the kind and level, not the processor count */
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_INSERT, 0.25f));
    g_assert_cmpuint (grl_audio_stream_get_n_processors (voice), ==, 1);
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_route_level (bus, G_OBJECT (voice), &route), 0.25, EPSILON);
    g_assert_cmpint (route, ==, LRG_AUDIO_ROUTE_INSERT);

    /* the return level scales the output; levels are clamped */
    lrg_audio_bus_set_level (bus, 2.0f);
    frames = constant_block (8, 1.0f);
    grl_audio_stream_process (voice, frames, 8);
    g_assert_cmpfloat_with_epsilon (frames[0], 0.0, EPSILON);
    out = constant_block (8, 0.0f);
    lrg_audio_bus_mix (bus, out, 8, RATE);
    g_assert_cmpfloat_with_epsilon (out[0], CENTRE_GAIN * 0.25 * 2.0, EPSILON);

    lrg_audio_bus_set_level (bus, 100.0f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_level (bus), LRG_AUDIO_BUS_MAX_LEVEL, EPSILON);
    lrg_audio_bus_set_level (bus, -1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_level (bus), 0.0, EPSILON);
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 9.0f));
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_route_level (bus, G_OBJECT (voice), NULL),
                                    LRG_AUDIO_BUS_MAX_LEVEL, EPSILON);
}

static void
test_bus_lifetimes (void)
{
    LrgAudioBus *bus;
    GrlSound    *voice;
    GrlSound    *other;
    g_autoptr(GObject) not_a_voice = NULL;
    guint        free_slots;

    free_slots = grl_audio_processor_get_n_free_slots ();
    bus = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_SOUND, NULL);
    other = g_object_new (GRL_TYPE_SOUND, NULL);
    not_a_voice = g_object_new (G_TYPE_OBJECT, NULL);

    /* only graylib voices can be routed */
    g_assert_false (lrg_audio_bus_route (bus, not_a_voice, LRG_AUDIO_ROUTE_SEND, 1.0f));
    g_assert_false (lrg_audio_bus_is_routed (bus, not_a_voice));
    g_assert_cmpfloat_with_epsilon (lrg_audio_bus_get_route_level (bus, not_a_voice, NULL), -1.0, EPSILON);
    g_assert_false (lrg_audio_bus_unroute (bus, not_a_voice));

    /* unrouting gives the slot back */
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 1.0f));
    g_assert_cmpuint (grl_audio_processor_get_n_free_slots (), ==, free_slots - 1);
    g_assert_true (lrg_audio_bus_unroute (bus, G_OBJECT (voice)));
    g_assert_false (lrg_audio_bus_unroute (bus, G_OBJECT (voice)));
    g_assert_cmpuint (grl_sound_get_n_processors (voice), ==, 0);
    g_assert_cmpuint (lrg_audio_bus_get_n_routes (bus), ==, 0);
    g_assert_cmpuint (grl_audio_processor_get_n_free_slots (), ==, free_slots);

    /* a disposed voice leaves the bus */
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 1.0f));
    g_object_unref (voice);
    g_assert_cmpuint (lrg_audio_bus_get_n_routes (bus), ==, 0);
    g_assert_cmpuint (grl_audio_processor_get_n_free_slots (), ==, free_slots);

    /* a disposed bus leaves its voices */
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (other), LRG_AUDIO_ROUTE_INSERT, 1.0f));
    g_assert_cmpuint (grl_sound_get_n_processors (other), ==, 1);
    g_object_unref (bus);
    g_assert_cmpuint (grl_sound_get_n_processors (other), ==, 0);
    g_assert_cmpuint (grl_audio_processor_get_n_free_slots (), ==, free_slots);
    g_object_unref (other);
}

static void
test_bus_send_then_insert (void)
{
    g_autoptr(LrgAudioBus) reverb = NULL;
    g_autoptr(LrgAudioBus) filter = NULL;
    g_autoptr(GrlSound)    voice = NULL;
    g_autofree gfloat     *frames = NULL;
    g_autofree gfloat     *out = NULL;

    reverb = lrg_audio_bus_new ();
    filter = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_SOUND, NULL);

    /* the send runs first and still sees the voice the insert silences */
    g_assert_true (lrg_audio_bus_route (reverb, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 1.0f));
    g_assert_true (lrg_audio_bus_route (filter, G_OBJECT (voice), LRG_AUDIO_ROUTE_INSERT, 1.0f));
    frames = constant_block (4, 1.0f);
    grl_sound_process (voice, frames, 4);
    g_assert_cmpfloat_with_epsilon (frames[0], 0.0, EPSILON);

    out = constant_block (4, 0.0f);
    lrg_audio_bus_mix (reverb, out, 4, RATE);
    g_assert_cmpfloat_with_epsilon (out[0], CENTRE_GAIN, EPSILON);
    memset (out, 0, 8 * sizeof (gfloat));
    lrg_audio_bus_mix (filter, out, 4, RATE);
    g_assert_cmpfloat_with_epsilon (out[0], CENTRE_GAIN, EPSILON);
}

static void
test_bus_chain_tail (void)
{
    g_autoptr(LrgAudioBus) bus = NULL;
    g_autoptr(GrlSound)    voice = NULL;
    g_autoptr(LrgEcho)     echo = NULL;
    g_autofree gfloat     *frames = NULL;
    g_autofree gfloat     *out = NULL;

    bus = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_SOUND, NULL);
    echo = lrg_echo_new (100.0f / (gfloat)RATE, 0.0f);
    lrg_echo_set_dry (echo, 0.0f);
    lrg_audio_bus_add_effect (bus, LRG_AUDIO_EFFECT (echo));
    g_assert_cmpuint (lrg_audio_effect_chain_get_count (lrg_audio_bus_get_chain (bus)), ==, 1);
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_INSERT, 1.0f));

    /* an impulse in the first block comes out of the echo 100 frames
     * later, in the second block, which no voice sent anything into */
    frames = constant_block (64, 0.0f);
    frames[0] = 1.0f;
    frames[1] = 1.0f;
    grl_sound_process (voice, frames, 64);
    out = constant_block (64, 0.0f);
    lrg_audio_bus_mix (bus, out, 64, RATE);
    g_assert_cmpfloat_with_epsilon (out[0], 0.0, EPSILON);
    memset (out, 0, 128 * sizeof (gfloat));
    lrg_audio_bus_mix (bus, out, 64, RATE);
    g_assert_cmpfloat_with_epsilon (out[(100 - 64) * 2], CENTRE_GAIN, EPSILON);

    /* reset forgets the tail */
    frames[0] = 1.0f;
    grl_sound_process (voice, frames, 64);
    lrg_audio_bus_reset (bus);
    memset (out, 0, 128 * sizeof (gfloat));
    lrg_audio_bus_mix (bus, out, 64, RATE);
    memset (out, 0, 128 * sizeof (gfloat));
    lrg_audio_bus_mix (bus, out, 64, RATE);
    g_assert_cmpfloat_with_epsilon (out[(100 - 64) * 2], 0.0, EPSILON);
}

static void
test_bus_overflow (void)
{
    g_autoptr(LrgAudioBus) bus = NULL;
    g_autoptr(GrlSound)    voice = NULL;
    g_autofree gfloat     *frames = NULL;
    g_autofree gfloat     *out = NULL;
    guint                  n;

    /* a block longer than the bus holds: the rest is dropped, no overrun */
    n = LRG_AUDIO_BUS_MAX_FRAMES + 100;
    bus = lrg_audio_bus_new ();
    voice = g_object_new (GRL_TYPE_SOUND, NULL);
    g_assert_true (lrg_audio_bus_route (bus, G_OBJECT (voice), LRG_AUDIO_ROUTE_SEND, 1.0f));
    frames = constant_block (n, 1.0f);
    grl_sound_process (voice, frames, n);
    grl_sound_process (voice, frames, 10);
    out = constant_block (n, 0.0f);
    lrg_audio_bus_mix (bus, out, n, RATE);
    g_assert_cmpfloat_with_epsilon (out[0], CENTRE_GAIN, EPSILON);
    g_assert_cmpfloat_with_epsilon (out[(LRG_AUDIO_BUS_MAX_FRAMES - 1) * 2], CENTRE_GAIN, EPSILON);
    g_assert_cmpfloat_with_epsilon (out[(n - 1) * 2], 0.0, EPSILON);
}

static void
test_bus_headless (void)
{
    g_autoptr(LrgAudioBus) bus = NULL;
    g_autoptr(GEnumClass)  klass = NULL;
    gboolean               measured;

    /* the tests never open a device: attaching refuses */
    bus = lrg_audio_bus_new ();
    g_assert_false (lrg_audio_bus_attach_mix (bus, 0));
    g_assert_false (lrg_audio_bus_is_attached (bus));
    measured = TRUE;
    g_assert_cmpuint (lrg_audio_bus_get_mix_rate (bus, &measured), ==, 0);
    g_assert_false (measured);
    lrg_audio_bus_detach_mix (bus);

    /* the route kinds' nicks */
    klass = g_type_class_ref (LRG_TYPE_AUDIO_ROUTE);
    g_assert_cmpint (g_enum_get_value_by_nick (klass, "send")->value, ==, LRG_AUDIO_ROUTE_SEND);
    g_assert_cmpint (g_enum_get_value_by_nick (klass, "insert")->value, ==, LRG_AUDIO_ROUTE_INSERT);
}

/* ==========================================================================
 * LrgEcho
 * ========================================================================== */

static void
test_echo_impulse (void)
{
    g_autoptr(LrgEcho) echo = NULL;
    g_autofree gfloat *frames = NULL;
    guint              n;
    guint              f;

    echo = lrg_echo_new (0.01f, 0.5f);        /* 480 frames */
    lrg_echo_set_wet (echo, 0.8f);
    n = 2000;
    frames = g_new0 (gfloat, n);
    frames[0] = 1.0f;
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (echo), frames, n, 1, RATE);

    /* dry at once, then repeats every delay, each half the one before */
    g_assert_cmpfloat_with_epsilon (frames[0], 1.0, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[480], 0.8, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[960], 0.4, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[1440], 0.2, EPSILON);
    for (f = 1; f < 480; f++)
        g_assert_cmpfloat_with_epsilon (frames[f], 0.0, EPSILON);

    /* reset forgets the repeats */
    memset (frames, 0, n * sizeof (gfloat));
    frames[0] = 1.0f;
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (echo), frames, 100, 1, RATE);
    lrg_audio_effect_reset (LRG_AUDIO_EFFECT (echo));
    memset (frames, 0, n * sizeof (gfloat));
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (echo), frames, 1000, 1, RATE);
    for (f = 0; f < 1000; f++)
        g_assert_cmpfloat_with_epsilon (frames[f], 0.0, EPSILON);
}

static void
test_echo_parameters (void)
{
    g_autoptr(LrgEcho) echo = NULL;
    g_autofree gfloat *frames = NULL;

    echo = lrg_echo_new (100.0f, 3.0f);
    g_assert_cmpfloat_with_epsilon (lrg_echo_get_delay (echo), LRG_ECHO_MAX_DELAY, EPSILON);
    g_assert_cmpfloat_with_epsilon (lrg_echo_get_feedback (echo), 1.0, EPSILON);
    lrg_echo_set_delay (echo, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_echo_get_delay (echo), LRG_ECHO_MIN_DELAY, EPSILON);
    lrg_echo_set_dry (echo, -2.0f);
    lrg_echo_set_wet (echo, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_echo_get_dry (echo), 0.0, EPSILON);
    g_assert_cmpfloat_with_epsilon (lrg_echo_get_wet (echo), 1.0, EPSILON);

    /* a longer delay after a block regrows the line (stereo here) */
    frames = g_new0 (gfloat, 4000 * 2);
    frames[0] = 1.0f;
    frames[1] = -1.0f;
    lrg_echo_set_delay (echo, 0.001f);
    lrg_echo_set_feedback (echo, 0.0f);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (echo), frames, 100, 2, RATE);
    g_assert_cmpfloat_with_epsilon (frames[48 * 2], 1.0, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[48 * 2 + 1], -1.0, EPSILON);
    memset (frames, 0, 4000 * 2 * sizeof (gfloat));
    frames[0] = 1.0f;
    lrg_echo_set_delay (echo, 0.05f);
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (echo), frames, 4000, 2, RATE);
    g_assert_cmpfloat_with_epsilon (frames[2400 * 2], 1.0, EPSILON);
}

/* ==========================================================================
 * LrgChorus
 * ========================================================================== */

static void
test_chorus_taps (void)
{
    g_autoptr(LrgChorus) chorus = NULL;
    g_autofree gfloat   *frames = NULL;
    guint                n;

    /* no swing: every tap reads exactly the base delay */
    chorus = lrg_chorus_new ();
    lrg_chorus_set_depth (chorus, 0.0f);
    lrg_chorus_set_rate (chorus, 0.0f);
    lrg_chorus_set_delay (chorus, 0.005f);       /* 240 frames */
    lrg_chorus_set_tap_wet (chorus, 0, 0.25f);
    lrg_chorus_set_tap_wet (chorus, 1, 0.25f);
    lrg_chorus_set_tap_wet (chorus, 2, 0.5f);
    n = 1000;
    frames = g_new0 (gfloat, n);
    frames[0] = 1.0f;
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (chorus), frames, n, 1, RATE);
    g_assert_cmpfloat_with_epsilon (frames[0], 1.0, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[240], 1.0, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[100], 0.0, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[480], 0.0, EPSILON);

    /* the dry level alone with every tap off is the identity scaled */
    lrg_audio_effect_reset (LRG_AUDIO_EFFECT (chorus));
    lrg_chorus_set_tap_wet (chorus, 0, 0.0f);
    lrg_chorus_set_tap_wet (chorus, 1, 0.0f);
    lrg_chorus_set_tap_wet (chorus, 2, 0.0f);
    lrg_chorus_set_dry (chorus, 0.5f);
    memset (frames, 0, n * sizeof (gfloat));
    frames[10] = 1.0f;
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (chorus), frames, n, 1, RATE);
    g_assert_cmpfloat_with_epsilon (frames[10], 0.5, EPSILON);
    g_assert_cmpfloat_with_epsilon (frames[250], 0.0, EPSILON);
}

static void
test_chorus_swing (void)
{
    g_autoptr(LrgChorus) chorus = NULL;
    g_autofree gfloat   *frames = NULL;
    gdouble              peak;
    guint                n;
    guint                f;

    /* a swinging chorus over a sine stays bounded and differs between
     * the stereo channels (the quarter cycle between them) */
    chorus = lrg_chorus_new ();
    lrg_chorus_set_delay (chorus, 0.02f);
    lrg_chorus_set_rate (chorus, 3.0f);
    lrg_chorus_set_depth (chorus, 0.5f);
    lrg_chorus_set_feedback (chorus, 0.3f);
    n = RATE / 2;
    frames = g_new0 (gfloat, (gsize)n * 2);
    for (f = 0; f < n; f++)
    {
        frames[f * 2] = (gfloat)sin (2.0 * G_PI * 440.0 * (gdouble)f / (gdouble)RATE);
        frames[f * 2 + 1] = frames[f * 2];
    }
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (chorus), frames, n, 2, RATE);
    peak = 0.0;
    for (f = 0; f < n * 2; f++)
        peak = MAX (peak, fabs ((gdouble)frames[f]));
    g_assert_cmpfloat (peak, <, 4.0);
    g_assert_cmpfloat (peak, >, 0.5);
    g_assert_cmpfloat (fabs ((gdouble)frames[(n - 100) * 2] - (gdouble)frames[(n - 100) * 2 + 1]), >, 1.0e-4);

    /* clamps */
    lrg_chorus_set_delay (chorus, 1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_chorus_get_delay (chorus), LRG_CHORUS_MAX_DELAY, EPSILON);
    lrg_chorus_set_rate (chorus, 100.0f);
    g_assert_cmpfloat_with_epsilon (lrg_chorus_get_rate (chorus), LRG_CHORUS_MAX_RATE, EPSILON);
    lrg_chorus_set_depth (chorus, 3.0f);
    g_assert_cmpfloat_with_epsilon (lrg_chorus_get_depth (chorus), 1.0, EPSILON);
    lrg_chorus_set_feedback (chorus, 3.0f);
    g_assert_cmpfloat (lrg_chorus_get_feedback (chorus), <, 1.0);
    g_assert_cmpfloat_with_epsilon (lrg_chorus_get_tap_wet (chorus, LRG_CHORUS_N_TAPS), 0.0, EPSILON);
}

/* ==========================================================================
 * LrgPitchShift
 * ========================================================================== */

static void
test_pitch_shift_identity (void)
{
    g_autoptr(LrgPitchShift) shift = NULL;
    g_autofree gfloat       *frames = NULL;
    g_autofree gfloat       *copy = NULL;
    guint                    n;

    shift = lrg_pitch_shift_new (1.0f);
    n = 4096;
    frames = sine (n, 300.0);
    copy = g_memdup2 (frames, n * sizeof (gfloat));
    lrg_audio_effect_process (LRG_AUDIO_EFFECT (shift), frames, n, 1, RATE);
    g_assert_cmpmem (frames, n * sizeof (gfloat), copy, n * sizeof (gfloat));

    lrg_pitch_shift_set_ratio (shift, 9.0f);
    g_assert_cmpfloat_with_epsilon (lrg_pitch_shift_get_ratio (shift), LRG_PITCH_SHIFT_MAX_RATIO, EPSILON);
    lrg_pitch_shift_set_ratio (shift, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_pitch_shift_get_ratio (shift), LRG_PITCH_SHIFT_MIN_RATIO, EPSILON);
    lrg_pitch_shift_set_window (shift, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_pitch_shift_get_window (shift), LRG_PITCH_SHIFT_MAX_WINDOW, EPSILON);
    lrg_pitch_shift_set_window (shift, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_pitch_shift_get_window (shift), LRG_PITCH_SHIFT_MIN_WINDOW, EPSILON);
}

static void
test_pitch_shift_ratio (void)
{
    static const gdouble ratios[] = { 0.5, 0.88, 1.13, 2.0 };
    guint                i;

    /* the shifted sine's frequency follows the ratio within a few percent */
    for (i = 0; i < G_N_ELEMENTS (ratios); i++)
    {
        g_autoptr(LrgPitchShift) shift = NULL;
        g_autofree gfloat       *frames = NULL;
        gdouble                  hz;
        guint                    n;

        shift = lrg_pitch_shift_new ((gfloat)ratios[i]);
        n = RATE;
        frames = sine (n, 400.0);
        lrg_audio_effect_process (LRG_AUDIO_EFFECT (shift), frames, n, 1, RATE);
        hz = crossing_rate (frames, RATE / 4, n);
        g_assert_cmpfloat (fabs (hz / (400.0 * ratios[i]) - 1.0), <, 0.05);
    }
}

int
main (gint   argc,
      gchar *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/audio-bus/send", test_bus_send);
    g_test_add_func ("/audio-bus/insert-and-pan", test_bus_insert_and_pan);
    g_test_add_func ("/audio-bus/chunks-and-voices", test_bus_chunks_and_voices);
    g_test_add_func ("/audio-bus/levels", test_bus_levels);
    g_test_add_func ("/audio-bus/lifetimes", test_bus_lifetimes);
    g_test_add_func ("/audio-bus/send-then-insert", test_bus_send_then_insert);
    g_test_add_func ("/audio-bus/chain-tail", test_bus_chain_tail);
    g_test_add_func ("/audio-bus/overflow", test_bus_overflow);
    g_test_add_func ("/audio-bus/headless", test_bus_headless);
    g_test_add_func ("/audio-effects/echo/impulse", test_echo_impulse);
    g_test_add_func ("/audio-effects/echo/parameters", test_echo_parameters);
    g_test_add_func ("/audio-effects/chorus/taps", test_chorus_taps);
    g_test_add_func ("/audio-effects/chorus/swing", test_chorus_swing);
    g_test_add_func ("/audio-effects/pitch-shift/identity", test_pitch_shift_identity);
    g_test_add_func ("/audio-effects/pitch-shift/ratio", test_pitch_shift_ratio);

    return g_test_run ();
}
