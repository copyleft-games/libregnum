/* test-music-player.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgSongPlayer, headless (lrg_song_player_render(), no
 * audio device): the same samples as the offline render, loops, section
 * jumps on the bar, layers, stingers, live notes, swapping an edited song
 * at the same beat, stopping at the end, meters, and refusing an output
 * without a device.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#define RATE (48000)

/* 8 beats at 120 BPM: a sine note on every beat (key 60 + beat), and an
 * idle "fx" track with no clips; markers A at 0 and B at 4 */
static LrgSong *
song_new (void)
{
	LrgSong *song = lrg_song_new ();
	gint     b;

	g_object_set (song, "bpm", 120.0, NULL);
	lrg_song_set_instrument (song, "lead", "synth", "init");
	lrg_song_set_instrument_param (song, "lead", "osc1-wave", 0);
	lrg_song_set_instrument_param (song, "lead", "amp-attack", 0);
	lrg_song_set_instrument_param (song, "lead", "amp-release", 0.01);
	lrg_song_add_pattern (song, "line", 8);
	for (b = 0; b < 8; b++)
		lrg_song_pattern_add_note (song, "line", b, 60 + b, 0.5, 0.9);
	lrg_song_add_pattern (song, "hit", 1);
	lrg_song_pattern_add_note (song, "hit", 0, 72, 0.5, 1.0);
	lrg_song_add_track (song, "melody", LRG_SONG_TRACK_INSTRUMENT, "lead");
	lrg_song_add_clip (song, "melody", "line", 0, 0);
	lrg_song_add_track (song, "fx", LRG_SONG_TRACK_INSTRUMENT, "lead");
	lrg_song_add_marker (song, 0, "A");
	lrg_song_add_marker (song, 4, "B");
	return song;
}

static gdouble
peak (const gfloat *s,
      gsize         from,
      gsize         to)
{
	gdouble p = 0.0;
	gsize   i;

	for (i = from; i < to; i++)
		p = MAX (p, fabs ((gdouble) s[i]));
	return p;
}

static void
test_matches_offline (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = song_new ();
	g_autoptr(LrgSongPlayer)        player = lrg_song_player_new (song, &error);
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          off = NULL;
	g_autofree gfloat              *ref = NULL;
	g_autofree gfloat              *live = g_new (gfloat, RATE * 4 * 2);
	gsize                           n = 0;
	guint                           f;

	g_assert_no_error (error);
	o->tail = 0.0;
	off = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	ref = lrg_wave_data_get_samples (off, &n);
	g_assert_cmpuint (n, ==, RATE * 4 * 2);

	/* the same song in odd-sized calls renders the same samples */
	lrg_song_player_play (player);
	for (f = 0; f < RATE * 4; f += 1000)
		g_assert_true (lrg_song_player_render (player, live + f * 2, MIN (1000u, RATE * 4 - f)));
	g_assert_cmpmem (ref, n * sizeof (gfloat), live, n * sizeof (gfloat));
	g_assert_cmpfloat_with_epsilon (lrg_song_player_get_beat (player), 8.0, 1e-6);
	g_assert_true (lrg_song_player_is_playing (player));
}

static void
test_loop_and_seek (void)
{
	g_autoptr(LrgSong)       song = song_new ();
	g_autoptr(LrgSongPlayer) player = lrg_song_player_new (song, NULL);
	g_autofree gfloat       *buf = g_new (gfloat, RATE * 2);

	g_assert_true (lrg_song_player_set_loop (player, TRUE, 2, 4));
	g_assert_false (lrg_song_player_set_loop (player, TRUE, 4, 2));
	lrg_song_player_seek (player, 2);
	lrg_song_player_play (player);
	/* 1.5 s = 3 beats from beat 2 in a 2-beat loop: beat 3 */
	lrg_song_player_render (player, buf, RATE);
	lrg_song_player_render (player, buf, RATE / 2);
	g_assert_cmpfloat_with_epsilon (lrg_song_player_get_beat (player), 3.0, 1e-3);

	/* stop goes to the loop start and is silent */
	lrg_song_player_stop (player);
	g_assert_false (lrg_song_player_is_playing (player));
	lrg_song_player_render (player, buf, RATE / 10);
	g_assert_cmpfloat (peak (buf, 0, RATE / 10 * 2), ==, 0.0);
	g_assert_cmpfloat_with_epsilon (lrg_song_player_get_beat (player), 2.0, 1e-6);
}

static void
test_section (void)
{
	g_autoptr(LrgSong)       song = song_new ();
	g_autoptr(LrgSongPlayer) player = lrg_song_player_new (song, NULL);
	g_autofree gfloat       *buf = g_new (gfloat, RATE * 2);

	lrg_song_player_play (player);
	lrg_song_player_render (player, buf, RATE / 4);          /* beat 0.5 */
	g_assert_true (lrg_song_player_queue_section (player, "B", LRG_SONG_QUANTIZE_BAR));
	g_assert_false (lrg_song_player_queue_section (player, "Z", LRG_SONG_QUANTIZE_BAR));
	/* the next bar line is beat 4 (2 s); render to 2.25 s: jumped to B
	 * at 2 s, a quarter second past it is beat 4.5 */
	lrg_song_player_render (player, buf, RATE);
	lrg_song_player_render (player, buf, RATE);
	g_assert_cmpfloat_with_epsilon (lrg_song_player_get_beat (player), 4.5, 1e-3);

	/* now: at once */
	lrg_song_player_queue_section (player, "A", LRG_SONG_QUANTIZE_NOW);
	lrg_song_player_render (player, buf, RATE / 2);
	g_assert_cmpfloat_with_epsilon (lrg_song_player_get_beat (player), 1.0, 1e-3);
}

static void
test_layers_stingers_notes (void)
{
	g_autoptr(LrgSong)       song = song_new ();
	g_autoptr(LrgSongPlayer) player = lrg_song_player_new (song, NULL);
	g_autofree gfloat       *buf = g_new (gfloat, RATE * 2);

	lrg_song_player_play (player);

	/* a layer at -144 dB silences the melody (after its 10 ms fade) */
	g_assert_true (lrg_song_player_set_layer (player, "melody", -144, 0.01));
	g_assert_false (lrg_song_player_set_layer (player, "nope", 0, 0));
	lrg_song_player_render (player, buf, RATE / 2);
	g_assert_cmpfloat (peak (buf, RATE / 10 * 2, RATE / 2 * 2), <, 1e-6);

	/* a stinger on the idle fx track sounds over it from the next beat */
	g_assert_true (lrg_song_player_play_stinger (player, "fx", "hit", LRG_SONG_QUANTIZE_BEAT));
	g_assert_false (lrg_song_player_play_stinger (player, "fx", "nope", LRG_SONG_QUANTIZE_BEAT));
	lrg_song_player_render (player, buf, RATE / 2);
	g_assert_cmpfloat (peak (buf, 0, RATE / 2 * 2), >, 0.05);

	/* a live note */
	lrg_song_player_render (player, buf, RATE / 2);   /* the stinger ends */
	g_assert_true (lrg_song_player_note_on (player, "fx", 69, 1.0));
	g_assert_false (lrg_song_player_note_on (player, "nope", 69, 1.0));
	lrg_song_player_render (player, buf, RATE / 10);
	g_assert_cmpfloat (peak (buf, 0, RATE / 10 * 2), >, 0.05);
	g_assert_true (lrg_song_player_note_off (player, "fx", 69));

	/* meters */
	{
		gdouble p = 0.0, r = 0.0;

		g_assert_true (lrg_song_player_get_meter (player, "master", &p, &r));
		g_assert_cmpfloat (p, >, 0.0);
		g_assert_false (lrg_song_player_get_meter (player, "nope", &p, &r));
	}
}

static void
test_swap (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgSong)       song = song_new ();
	g_autoptr(LrgSongPlayer) player = lrg_song_player_new (song, NULL);
	g_autofree gfloat       *buf = g_new (gfloat, RATE * 2);

	lrg_song_player_play (player);
	lrg_song_player_render (player, buf, RATE);          /* beat 2 */
	/* a faster edit swaps in at the same beat */
	g_object_set (song, "bpm", 240.0, NULL);
	g_assert_true (lrg_song_player_set_song (player, song, &error));
	g_assert_no_error (error);
	lrg_song_player_render (player, buf, RATE / 4);      /* + 1 beat at 240 */
	g_assert_cmpfloat_with_epsilon (lrg_song_player_get_beat (player), 3.0, 1e-3);

	/* a broken edit is refused, the old song keeps playing */
	lrg_song_set_track (song, "melody", "instrument", "missing", 0);
	g_assert_false (lrg_song_player_set_song (player, song, &error));
	g_assert_nonnull (error);
}

static void
test_end_and_output (void)
{
	g_autoptr(LrgSong)       song = song_new ();
	g_autoptr(LrgSongPlayer) player = lrg_song_player_new (song, NULL);
	g_autofree gfloat       *buf = g_new (gfloat, RATE * 2);
	guint                    i;

	lrg_song_player_play (player);
	/* 4 s of music and a 2 s tail: stopped by 7 s */
	for (i = 0; i < 7; i++)
		lrg_song_player_render (player, buf, RATE);
	g_assert_false (lrg_song_player_is_playing (player));

	g_assert_false (lrg_song_player_has_output (player));
	g_assert_cmpuint (lrg_song_player_get_underruns (player), ==, 0);
}

/* the real device, opt-in (LRG_TEST_AUDIO_DEVICE=1): plays a second
 * through the render thread and the stream callback */
static void
test_output (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgSong)       song = NULL;
	g_autoptr(LrgSongPlayer) player = NULL;
	guint                    i;

	if (g_strcmp0 (g_getenv ("LRG_TEST_AUDIO_DEVICE"), "1") != 0)
	{
		g_test_skip ("set LRG_TEST_AUDIO_DEVICE=1 to open the audio device");
		return;
	}
	song = song_new ();
	player = lrg_song_player_new (song, NULL);
	if (!lrg_song_player_start_output (player, &error))
	{
		g_test_skip (error->message);
		return;
	}
	lrg_song_player_play (player);
	for (i = 0; i < 100; i++)
	{
		lrg_song_player_update (player);
		g_usleep (10000);
	}
	g_assert_cmpfloat (lrg_song_player_get_beat (player), >, 1.0);
	g_assert_false (lrg_song_player_render (player, NULL, 0));
	lrg_song_player_stop_output (player);
	g_assert_false (lrg_song_player_has_output (player));
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/music-player/matches-offline", test_matches_offline);
	g_test_add_func ("/music-player/loop-and-seek", test_loop_and_seek);
	g_test_add_func ("/music-player/section", test_section);
	g_test_add_func ("/music-player/layers-stingers-notes", test_layers_stingers_notes);
	g_test_add_func ("/music-player/swap", test_swap);
	g_test_add_func ("/music-player/end", test_end_and_output);
	g_test_add_func ("/music-player/output", test_output);
	return g_test_run ();
}
