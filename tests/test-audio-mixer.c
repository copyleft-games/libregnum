/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <math.h>

static LrgWaveData *
wave_from (const gfloat *samples, guint count, guint channels)
{
    return lrg_wave_data_new_from_samples (8000, 32, channels, (const guint8 *)samples,
                                           count * sizeof (gfloat));
}

static void
test_routing (void)
{
    const gfloat pcm[] = { 0.5f, -0.5f, 0.25f, -0.25f };
    g_autoptr (LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 2);
    g_autoptr (LrgWaveData) wave = wave_from (pcm, 4, 2);
    g_autofree gfloat *out = NULL;
    gsize count;

    g_assert_true (lrg_audio_mixer_add_bus (mixer, "sfx", "master"));
    g_assert_true (lrg_audio_mixer_add_bus (mixer, "weapons", "sfx"));
    g_assert_true (lrg_audio_mixer_add_bus (mixer, "aux", "master"));
    g_assert_true (lrg_audio_mixer_set_bus (mixer, "sfx", 0.5, FALSE));
    g_assert_true (lrg_audio_mixer_set_bus (mixer, "master", 0.5, FALSE));
    g_assert_true (lrg_audio_mixer_set_send (mixer, "weapons", "aux", 0.25));
    g_assert_cmpuint (lrg_audio_mixer_play (mixer, wave, "weapons", 1, FALSE), >, 0);
    g_clear_object (&wave); /* Mixer owns independent converted samples. */
    out = lrg_audio_mixer_render (mixer, 3, &count);
    g_assert_cmpuint (count, ==, 6);
    g_assert_cmpfloat_with_epsilon (out[0], 0.1875, 1e-6);
    g_assert_cmpfloat_with_epsilon (out[1], -0.1875, 1e-6);
    g_assert_cmpfloat_with_epsilon (out[2], 0.09375, 1e-6);
    g_assert_cmpfloat (out[4], ==, 0);
    g_assert_cmpuint (lrg_audio_mixer_get_voice_count (mixer), ==, 0);
}

static void
test_graph_validation (void)
{
    g_autoptr (LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);

    g_assert_null (lrg_audio_mixer_new (0, 1));
    g_assert_null (lrg_audio_mixer_new (8000, 3));
    g_assert_false (lrg_audio_mixer_add_bus (mixer, "a", "missing"));
    g_assert_true (lrg_audio_mixer_add_bus (mixer, "a", "master"));
    g_assert_true (lrg_audio_mixer_add_bus (mixer, "b", "a"));
    g_assert_true (lrg_audio_mixer_add_bus (mixer, "c", "master"));
    g_assert_false (lrg_audio_mixer_add_bus (mixer, "a", "master"));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "a", "b", 1));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "a", "a", 1));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "master", "a", 1));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "a", "master", 1));
    g_assert_true (lrg_audio_mixer_set_send (mixer, "b", "c", 1));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "c", "b", 1));
    g_assert_true (lrg_audio_mixer_set_send (mixer, "c", "a", 1));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "a", "c", 1));
    g_assert_true (lrg_audio_mixer_set_send (mixer, "b", "c", 0));
    g_assert_true (lrg_audio_mixer_set_send (mixer, "c", "a", 1));
    g_assert_false (lrg_audio_mixer_set_bus (mixer, "master", NAN, FALSE));
    g_assert_false (lrg_audio_mixer_set_send (mixer, "b", "c", INFINITY));
    g_assert_false (lrg_audio_mixer_set_effects (mixer, "master", 4000, 0, 0, 0));
    g_assert_false (lrg_audio_mixer_set_effects (mixer, "master", 0, NAN, 0, 0));
    g_assert_false (lrg_audio_mixer_set_effects (mixer, "master", 0, 1, 1, 1));
}

static void
test_voices (void)
{
    const gfloat pcm[] = { 0.25f, 0.5f };
    g_autoptr (LrgWaveData) wave = wave_from (pcm, 2, 1);
    g_autoptr (LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);
    g_autofree gfloat *out = NULL;
    guint64 id;
    gsize count;

    id = lrg_audio_mixer_play (mixer, wave, "master", 1, TRUE);
    g_assert_true (lrg_audio_mixer_pause_voice (mixer, id, TRUE));
    out = lrg_audio_mixer_render (mixer, 3, &count);
    g_assert_cmpfloat (out[0], ==, 0);
    g_clear_pointer (&out, g_free);
    g_assert_true (lrg_audio_mixer_pause_voice (mixer, id, FALSE));
    out = lrg_audio_mixer_render (mixer, 3, &count);
    g_assert_cmpfloat (out[0], ==, 0.25);
    g_assert_cmpfloat (out[1], ==, 0.5);
    g_assert_cmpfloat (out[2], ==, 0.25);
    g_clear_pointer (&out, g_free);
    g_assert_true (lrg_audio_mixer_set_bus (mixer, "master", 1, TRUE));
    out = lrg_audio_mixer_render (mixer, 1, &count);
    g_assert_cmpfloat (out[0], ==, 0);
    g_clear_pointer (&out, g_free);
    lrg_audio_mixer_set_bus (mixer, "master", 1, FALSE);
    out = lrg_audio_mixer_render (mixer, 1, &count);
    g_assert_cmpfloat (out[0], ==, 0.25); /* Muting still advanced the voice. */
    g_assert_true (lrg_audio_mixer_stop_voice (mixer, id));
    g_assert_false (lrg_audio_mixer_stop_voice (mixer, id));
    g_assert_false (lrg_audio_mixer_pause_voice (mixer, id, TRUE));
    g_assert_cmpuint (lrg_audio_mixer_get_voice_count (mixer), ==, 0);
}

static void
test_delay (void)
{
    const gfloat pcm[] = { 1, 0, 0, 0 };
    g_autoptr (LrgWaveData) wave = wave_from (pcm, 4, 1);
    g_autoptr (LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 1);
    g_autofree gfloat *out = NULL;
    gsize count;

    g_assert_true (lrg_audio_mixer_set_effects (mixer, "master", 0, 2.0 / 8000, 0.5, 1));
    lrg_audio_mixer_play (mixer, wave, "master", 1, FALSE);
    out = lrg_audio_mixer_render (mixer, 3, &count);
    g_assert_cmpfloat (out[0], ==, 0);
    g_assert_cmpfloat (out[1], ==, 0);
    g_assert_cmpfloat (out[2], ==, 1);
    g_clear_pointer (&out, g_free);
    out = lrg_audio_mixer_render (mixer, 4, &count);
    g_assert_cmpfloat (out[0], ==, 0);
    g_assert_cmpfloat (out[1], ==, 0.5);
    g_assert_cmpfloat (out[3], ==, 0.25);
    g_assert_cmpuint (lrg_audio_mixer_get_voice_count (mixer), ==, 0);
}

static void
test_lowpass_and_blocks (void)
{
    const gfloat pcm[] = { 1, 0, -1, 0, 1, 0, -1, 0 };
    g_autoptr (LrgWaveData) wave = wave_from (pcm, 8, 2);
    g_autoptr (LrgAudioMixer) a = lrg_audio_mixer_new (8000, 2);
    g_autoptr (LrgAudioMixer) b = lrg_audio_mixer_new (8000, 2);
    g_autofree gfloat *whole = NULL;
    g_autofree gfloat *first = NULL;
    g_autofree gfloat *second = NULL;
    gsize count;
    guint i;

    lrg_audio_mixer_set_effects (a, "master", 1000, 0, 0, 0);
    lrg_audio_mixer_set_effects (b, "master", 1000, 0, 0, 0);
    lrg_audio_mixer_play (a, wave, "master", 1, FALSE);
    lrg_audio_mixer_play (b, wave, "master", 1, FALSE);
    whole = lrg_audio_mixer_render (a, 4, &count);
    first = lrg_audio_mixer_render (b, 1, &count);
    second = lrg_audio_mixer_render (b, 3, &count);
    g_assert_cmpfloat (whole[0], >, 0);
    g_assert_cmpfloat (whole[0], <, 1);
    g_assert_cmpfloat (whole[1], ==, 0); /* No channel cross-talk. */
    for (i = 0; i < 2; i++)
        g_assert_cmpfloat (whole[i], ==, first[i]);
    for (i = 0; i < 6; i++)
        g_assert_cmpfloat (whole[i + 2], ==, second[i]);
}

static void
test_clipping_and_conversion (void)
{
    const gfloat pcm[] = { 0.75, 0.75, 0.75, 0.75 };
    g_autoptr (LrgWaveData) wave = wave_from (pcm, 4, 1);
    g_autoptr (LrgAudioMixer) mixer = lrg_audio_mixer_new (8000, 2);
    g_autofree gfloat *out = NULL;
    gsize count = 42;

    g_assert_null (lrg_audio_mixer_render (mixer, 0, &count));
    g_assert_cmpuint (count, ==, 0);
    g_assert_cmpuint (lrg_audio_mixer_play (mixer, wave, "missing", 1, FALSE), ==, 0);
    g_assert_cmpuint (lrg_audio_mixer_play (mixer, wave, "master", NAN, FALSE), ==, 0);
    lrg_audio_mixer_play (mixer, wave, "master", 1, FALSE);
    lrg_audio_mixer_play (mixer, wave, "master", 1, FALSE);
    out = lrg_audio_mixer_render (mixer, 1, &count);
    g_assert_cmpuint (count, ==, 2);
    g_assert_cmpfloat (out[0], ==, 1);
    g_assert_cmpfloat (out[1], ==, 1);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mixer/routing", test_routing);
    g_test_add_func ("/mixer/graph-validation", test_graph_validation);
    g_test_add_func ("/mixer/voices", test_voices);
    g_test_add_func ("/mixer/delay", test_delay);
    g_test_add_func ("/mixer/lowpass-blocks", test_lowpass_and_blocks);
    g_test_add_func ("/mixer/clipping-conversion", test_clipping_and_conversion);
    return g_test_run ();
}
