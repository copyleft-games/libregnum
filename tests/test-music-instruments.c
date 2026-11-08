/* test-music-instruments.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the 0.10.0 instruments: the LrgInstrument clock and
 * queue (sample-accurate notes), LrgSynth (every preset, polyphony and
 * stealing, mono legato, determinism), LrgDrumMachine (every piece,
 * hat choke, kits), LrgSampler (pitch, loops, round robin),
 * LrgFoleyInstrument and LrgSoundFontInstrument (a tiny SoundFont written
 * by tests/lrg-test-midi.h), note names, and an instrument played by an
 * LrgAudioMixer.  No audio device and no data file.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#include "lrg-test-midi.h"

#define RATE (48000)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* renders @frames stereo frames of @inst into a new zeroed buffer */
static gfloat *
render (LrgInstrument *inst,
        guint          frames)
{
	gfloat *buf = g_new0 (gfloat, (gsize) frames * 2);

	lrg_instrument_render (inst, buf, frames, 2, RATE);
	return buf;
}

static gdouble
peak (const gfloat *buf,
      gsize         from,
      gsize         to)
{
	gdouble p = 0.0;
	gsize   i;

	for (i = from; i < to; i++)
		p = MAX (p, fabs ((gdouble) buf[i]));
	return p;
}

static gboolean
all_finite (const gfloat *buf,
            gsize         n)
{
	gsize i;

	for (i = 0; i < n; i++)
		if (!isfinite (buf[i]))
			return FALSE;
	return TRUE;
}

/* rising zero crossings of the left channel over [from, to) frames */
static guint
crossings (const gfloat *buf,
           guint         from,
           guint         to)
{
	guint f, n = 0;

	for (f = from + 1; f < to; f++)
		if (buf[(f - 1) * 2] < 0.0f && buf[f * 2] >= 0.0f)
			n++;
	return n;
}

/* ==========================================================================
 * The clock and the queue
 * ========================================================================== */

static void
test_sample_accurate (void)
{
	g_autoptr(LrgSynth) synth = lrg_synth_new ();
	g_autofree gfloat  *buf = NULL;
	guint               f;

	g_object_set (synth, "amp-attack", 0.0, NULL);
	/* a note at frame 1000 of a 4096-frame render made in odd blocks */
	lrg_instrument_schedule (LRG_INSTRUMENT (synth), 1000, LRG_INSTRUMENT_EVENT_NOTE_ON, 69, 1.0);
	g_assert_cmpuint (lrg_instrument_get_pending (LRG_INSTRUMENT (synth)), ==, 1);
	buf = g_new0 (gfloat, 4096 * 2);
	for (f = 0; f < 4096; f += 333)
		lrg_instrument_render (LRG_INSTRUMENT (synth), buf + f * 2, MIN (333u, 4096 - f), 2, RATE);
	g_assert_cmpfloat (peak (buf, 0, 2000), ==, 0.0);
	g_assert_cmpfloat (peak (buf, 2000, 2100), >, 0.01);
	g_assert_cmpuint (lrg_instrument_get_position (LRG_INSTRUMENT (synth)), ==, 4096);
	g_assert_cmpuint (lrg_instrument_get_pending (LRG_INSTRUMENT (synth)), ==, 0);

	/* a frame already rendered plays at the start of the next render */
	lrg_instrument_schedule (LRG_INSTRUMENT (synth), 10, LRG_INSTRUMENT_EVENT_NOTE_OFF, 69, 0.0);
	g_free (render (LRG_INSTRUMENT (synth), 16));
	g_assert_cmpuint (lrg_instrument_get_pending (LRG_INSTRUMENT (synth)), ==, 0);

	/* reset: silent, clock at 0 */
	lrg_instrument_reset (LRG_INSTRUMENT (synth));
	g_assert_cmpuint (lrg_instrument_get_position (LRG_INSTRUMENT (synth)), ==, 0);
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (synth)), ==, 0);
}

static void
test_note_names (void)
{
	g_autofree gchar *a = lrg_music_key_to_name (61);

	g_assert_cmpint (lrg_music_key_from_name ("C4"), ==, 60);
	g_assert_cmpint (lrg_music_key_from_name ("A4"), ==, 69);
	g_assert_cmpint (lrg_music_key_from_name ("F#2"), ==, 42);
	g_assert_cmpint (lrg_music_key_from_name ("Bb3"), ==, 58);
	g_assert_cmpint (lrg_music_key_from_name ("c-1"), ==, 0);
	g_assert_cmpint (lrg_music_key_from_name ("36"), ==, 36);
	g_assert_cmpint (lrg_music_key_from_name ("H2"), ==, -1);
	g_assert_cmpint (lrg_music_key_from_name ("C"), ==, -1);
	g_assert_cmpint (lrg_music_key_from_name ("G9"), ==, 127);
	g_assert_cmpint (lrg_music_key_from_name ("A9"), ==, -1);
	g_assert_cmpstr (a, ==, "C#4");
	g_assert_cmpfloat_with_epsilon (lrg_music_key_to_hz (69), 440.0, 1e-9);
}

/* ==========================================================================
 * LrgSynth
 * ========================================================================== */

static void
test_synth_presets (void)
{
	g_auto(GStrv) names = lrg_synth_list_presets ();
	guint         i;

	g_assert_cmpuint (g_strv_length (names), >=, 10);
	for (i = 0; names[i] != NULL; i++)
	{
		g_autoptr(LrgSynth) synth = lrg_synth_new_preset (names[i]);
		g_autofree gfloat  *buf = NULL;

		g_assert_nonnull (synth);
		g_assert_nonnull (lrg_synth_get_preset_description (names[i]));
		lrg_instrument_note_on (LRG_INSTRUMENT (synth), 45, 0.9);
		lrg_instrument_note_on (LRG_INSTRUMENT (synth), 57, 0.9);
		lrg_instrument_note_on (LRG_INSTRUMENT (synth), 64, 0.9);
		buf = render (LRG_INSTRUMENT (synth), RATE * 3 / 2);
		g_assert_true (all_finite (buf, (gsize) RATE * 3));
		g_test_message ("preset %s peak %.3f", names[i], peak (buf, 0, (gsize) RATE * 3));
		g_assert_cmpfloat (peak (buf, 0, (gsize) RATE * 3), >, 0.005);
		g_assert_cmpfloat (peak (buf, 0, (gsize) RATE * 3), <, 2.0);
	}
	g_assert_null (lrg_synth_new_preset ("nope"));
}

static void
test_synth_release (void)
{
	g_autoptr(LrgSynth) synth = lrg_synth_new ();
	g_autofree gfloat  *a = NULL;
	g_autofree gfloat  *b = NULL;

	g_object_set (synth, "amp-release", 0.05, NULL);
	lrg_instrument_note_on (LRG_INSTRUMENT (synth), 60, 1.0);
	a = render (LRG_INSTRUMENT (synth), 4800);
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (synth)), ==, 1);
	lrg_instrument_note_off (LRG_INSTRUMENT (synth), 60);
	b = render (LRG_INSTRUMENT (synth), RATE);
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (synth)), ==, 0);
	g_assert_cmpfloat (peak (b, (gsize) RATE, (gsize) RATE * 2), ==, 0.0);
}

static void
test_synth_polyphony (void)
{
	g_autoptr(LrgSynth) synth = lrg_synth_new ();
	gint                k;

	g_object_set (synth, "polyphony", 3u, NULL);
	for (k = 50; k < 58; k++)
		lrg_instrument_note_on (LRG_INSTRUMENT (synth), k, 0.8);
	g_free (render (LRG_INSTRUMENT (synth), 256));
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (synth)), ==, 3);

	/* mono legato: one voice whatever is held */
	lrg_instrument_reset (LRG_INSTRUMENT (synth));
	lrg_instrument_set_param (LRG_INSTRUMENT (synth), "mono", 1);
	lrg_instrument_note_on (LRG_INSTRUMENT (synth), 50, 0.8);
	lrg_instrument_note_on (LRG_INSTRUMENT (synth), 55, 0.8);
	g_free (render (LRG_INSTRUMENT (synth), 256));
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (synth)), ==, 1);
	/* releasing the newer key falls back to the held one, still sounding */
	lrg_instrument_note_off (LRG_INSTRUMENT (synth), 55);
	g_free (render (LRG_INSTRUMENT (synth), 256));
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (synth)), ==, 1);
}

static void
test_synth_pitch (void)
{
	g_autoptr(LrgSynth) synth = lrg_synth_new ();
	g_autofree gfloat  *buf = NULL;
	guint               n;

	/* a sine at A4 crosses zero 440 times a second */
	lrg_instrument_set_param (LRG_INSTRUMENT (synth), "osc1-wave", LRG_SYNTH_WAVE_SINE);
	lrg_instrument_set_param (LRG_INSTRUMENT (synth), "amp-sustain", 1);
	lrg_instrument_note_on (LRG_INSTRUMENT (synth), 69, 1.0);
	buf = render (LRG_INSTRUMENT (synth), RATE);
	n = crossings (buf, 0, RATE);
	g_assert_cmpuint (n, >=, 438);
	g_assert_cmpuint (n, <=, 442);

	/* a +12 semitone bend doubles it */
	lrg_instrument_schedule (LRG_INSTRUMENT (synth), RATE, LRG_INSTRUMENT_EVENT_PITCH_BEND, 0, 12.0);
	g_free (buf);
	buf = render (LRG_INSTRUMENT (synth), RATE);
	n = crossings (buf, 0, RATE);
	g_assert_cmpuint (n, >=, 876);
	g_assert_cmpuint (n, <=, 884);
}

static void
test_synth_deterministic (void)
{
	g_autoptr(LrgSynth) a = lrg_synth_new_preset ("synthwave-lead");
	g_autoptr(LrgSynth) b = lrg_synth_new_preset ("synthwave-lead");
	g_autofree gfloat  *ba = NULL;
	g_autofree gfloat  *bb = NULL;

	lrg_instrument_note_on (LRG_INSTRUMENT (a), 64, 0.7);
	lrg_instrument_note_on (LRG_INSTRUMENT (b), 64, 0.7);
	lrg_instrument_set_param (LRG_INSTRUMENT (a), "noise-level", 0.3);
	lrg_instrument_set_param (LRG_INSTRUMENT (b), "noise-level", 0.3);
	ba = render (LRG_INSTRUMENT (a), 9600);
	bb = render (LRG_INSTRUMENT (b), 9600);
	g_assert_cmpmem (ba, 9600 * 2 * sizeof (gfloat), bb, 9600 * 2 * sizeof (gfloat));
}

static void
test_synth_params (void)
{
	g_autoptr(LrgSynth) synth = lrg_synth_new ();

	/* clamped by name; unknown names refused */
	g_assert_true (lrg_instrument_set_param (LRG_INSTRUMENT (synth), "cutoff", 1e9));
	g_assert_cmpfloat (lrg_instrument_get_param (LRG_INSTRUMENT (synth), "cutoff"), ==, 20000.0);
	g_assert_false (lrg_instrument_set_param (LRG_INSTRUMENT (synth), "warp-drive", 1));
	g_assert_true (isnan (lrg_instrument_get_param (LRG_INSTRUMENT (synth), "warp-drive")));
	g_assert_true (lrg_instrument_set_param (LRG_INSTRUMENT (synth), "polyphony", 1000));
	g_assert_cmpuint (lrg_instrument_get_polyphony (LRG_INSTRUMENT (synth)), ==, LRG_INSTRUMENT_MAX_POLYPHONY);

	/* a preset resets what it does not set */
	lrg_instrument_set_param (LRG_INSTRUMENT (synth), "fm", 3);
	g_assert_true (lrg_synth_load_preset (synth, "synthwave-bass"));
	g_assert_cmpfloat (lrg_instrument_get_param (LRG_INSTRUMENT (synth), "fm"), ==, 0.0);
	g_assert_cmpfloat (lrg_instrument_get_param (LRG_INSTRUMENT (synth), "cutoff"), ==, 380.0);
}

/* ==========================================================================
 * LrgDrumMachine
 * ========================================================================== */

static void
test_drums (void)
{
	g_auto(GStrv) pieces = lrg_drum_machine_list_pieces ();
	guint         kit, i;

	g_assert_cmpuint (g_strv_length (pieces), ==, 13);
	g_assert_cmpint (lrg_drum_machine_piece_to_key ("kick"), ==, 36);
	g_assert_cmpint (lrg_drum_machine_piece_to_key ("SD"), ==, 38);
	g_assert_cmpint (lrg_drum_machine_piece_to_key ("hh"), ==, 42);
	g_assert_cmpint (lrg_drum_machine_piece_to_key ("bongo"), ==, -1);
	g_assert_cmpstr (lrg_drum_machine_key_to_piece (46), ==, "open-hat");
	g_assert_null (lrg_drum_machine_key_to_piece (90));

	for (kit = 0; kit < 3; kit++)
		for (i = 0; pieces[i] != NULL; i++)
		{
			g_autoptr(LrgDrumMachine) drums = lrg_drum_machine_new ((LrgDrumKit) kit);
			g_autofree gfloat        *buf = NULL;

			lrg_instrument_note_on (LRG_INSTRUMENT (drums), lrg_drum_machine_piece_to_key (pieces[i]), 1.0);
			buf = render (LRG_INSTRUMENT (drums), RATE * 8);
			g_assert_true (all_finite (buf, (gsize) RATE * 16));
			g_assert_cmpfloat (peak (buf, 0, (gsize) RATE), >, 0.02);
			g_assert_cmpfloat (peak (buf, 0, (gsize) RATE * 16), <, 2.0);
			/* every hit dies away within eight seconds */
			g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (drums)), ==, 0);
		}
}

static void
test_drum_choke (void)
{
	g_autoptr(LrgDrumMachine) drums = lrg_drum_machine_new (LRG_DRUM_KIT_808);
	g_autofree gfloat        *buf = NULL;

	lrg_instrument_note_on (LRG_INSTRUMENT (drums), 46, 1.0);
	g_free (render (LRG_INSTRUMENT (drums), 2400));
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (drums)), ==, 1);
	/* a closed hat chokes the ringing open hat within a few ms: only the
	 * closed hat is left */
	lrg_instrument_note_on (LRG_INSTRUMENT (drums), 42, 1.0);
	buf = render (LRG_INSTRUMENT (drums), 480);
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (drums)), ==, 1);

	/* the kit is a property and resets the pieces */
	lrg_instrument_set_param (LRG_INSTRUMENT (drums), "kick-tune", 5);
	g_object_set (drums, "kit", LRG_DRUM_KIT_909, NULL);
	g_assert_cmpint (lrg_drum_machine_get_kit (drums), ==, LRG_DRUM_KIT_909);
	g_assert_cmpfloat (lrg_instrument_get_param (LRG_INSTRUMENT (drums), "kick-tune"), ==, 2.0);
}

/* ==========================================================================
 * LrgSampler, LrgFoleyInstrument
 * ========================================================================== */

/* a mono 440 Hz sine of @seconds at RATE */
static LrgWaveData *
sine_wave (gdouble seconds)
{
	guint      n = (guint) (seconds * RATE);
	gfloat    *pcm = g_new (gfloat, n);
	LrgWaveData *wave;
	guint      i;

	for (i = 0; i < n; i++)
		pcm[i] = (gfloat) (0.5 * sin (2.0 * G_PI * 440.0 * i / RATE));
	wave = lrg_wave_data_new_from_samples (RATE, 32, 1, (const guint8 *) pcm, n * sizeof (gfloat));
	g_free (pcm);
	return wave;
}

static void
test_sampler (void)
{
	g_autoptr(LrgWaveData) wave = sine_wave (0.5);
	g_autoptr(LrgSampler)  sampler = lrg_sampler_new_from_wave (wave, 69);
	g_autofree gfloat     *buf = NULL;
	guint                  n;

	g_assert_nonnull (sampler);
	g_assert_cmpuint (lrg_sampler_get_n_zones (sampler), ==, 1);

	/* an octave up plays twice as fast: ~880 crossings a second, 0.25 s */
	lrg_instrument_note_on (LRG_INSTRUMENT (sampler), 81, 1.0);
	buf = render (LRG_INSTRUMENT (sampler), RATE / 4);
	n = crossings (buf, 0, RATE / 4);
	g_assert_cmpuint (n, >=, 215);
	g_assert_cmpuint (n, <=, 225);
	g_free (render (LRG_INSTRUMENT (sampler), RATE));
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (sampler)), ==, 0);

	/* a loop keeps a held note going past the end of the recording */
	g_assert_true (lrg_sampler_set_zone_loop (sampler, 0, 0, 24000));
	g_assert_false (lrg_sampler_set_zone_loop (sampler, 0, 100, 99999999));
	lrg_instrument_note_on (LRG_INSTRUMENT (sampler), 69, 1.0);
	g_free (buf);
	buf = render (LRG_INSTRUMENT (sampler), RATE * 2);
	g_assert_cmpfloat (peak (buf, (gsize) RATE * 3, (gsize) RATE * 4), >, 0.2);

	/* no zone for a velocity: nothing plays */
	lrg_instrument_reset (LRG_INSTRUMENT (sampler));
	g_assert_true (lrg_sampler_set_zone_velocity (sampler, 0, 0.5, 1.0));
	lrg_instrument_note_on (LRG_INSTRUMENT (sampler), 69, 0.2);
	g_free (render (LRG_INSTRUMENT (sampler), 64));
	g_assert_cmpuint (lrg_instrument_get_active_voices (LRG_INSTRUMENT (sampler)), ==, 0);
}

static void
test_foley_instrument (void)
{
	g_autoptr(LrgFoleyPatch)      patch = lrg_foley_patch_new_from_kind (LRG_FOLEY_FOOTSTEP_STONE, 0, RATE, NULL);
	g_autoptr(LrgFoleyInstrument) steps = lrg_foley_instrument_new (patch);
	g_autofree gfloat            *a = NULL;
	g_autofree gfloat            *b = NULL;

	g_assert_nonnull (lrg_foley_instrument_get_patch (steps));
	lrg_instrument_note_on (LRG_INSTRUMENT (steps), 60, 1.0);
	a = render (LRG_INSTRUMENT (steps), RATE / 2);
	g_assert_cmpfloat (peak (a, 0, (gsize) RATE), >, 0.2);

	/* the next note is another take */
	lrg_instrument_note_on (LRG_INSTRUMENT (steps), 60, 1.0);
	b = render (LRG_INSTRUMENT (steps), RATE / 2);
	g_assert_true (memcmp (a, b, sizeof (gfloat) * RATE) != 0);
}

/* ==========================================================================
 * LrgSoundFontInstrument
 * ========================================================================== */

static void
test_sound_font (void)
{
	g_autoptr(GError)                 error = NULL;
	g_autoptr(GBytes)                 sf2 = test_midi_build_sf2 ();
	g_autoptr(LrgSoundFontInstrument) inst = NULL;
	g_autoptr(GBytes)                 junk = g_bytes_new_static ("RIFF....", 8);
	g_autofree gfloat                *buf = NULL;

	inst = lrg_sound_font_instrument_new_from_bytes (sf2, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_sound_font_instrument_get_preset_count (inst), >=, 1);
	lrg_instrument_note_on (LRG_INSTRUMENT (inst), 60, 1.0);
	buf = render (LRG_INSTRUMENT (inst), 9600);
	g_assert_true (all_finite (buf, 19200));
	g_assert_cmpfloat (peak (buf, 0, 19200), >, 0.0);

	g_assert_null (lrg_sound_font_instrument_new_from_bytes (junk, &error));
	g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_INVALID);
}

/* ==========================================================================
 * In a mixer
 * ========================================================================== */

static void
test_in_mixer (void)
{
	g_autoptr(LrgAudioMixer)  mixer = lrg_audio_mixer_new (RATE, 2);
	g_autoptr(LrgDrumMachine) drums = lrg_drum_machine_new (LRG_DRUM_KIT_909);
	g_autofree gfloat        *out = NULL;
	gsize                     n = 0;

	g_assert_true (lrg_audio_mixer_add_bus (mixer, "drums", "master"));
	g_assert_cmpuint (lrg_audio_mixer_play_source (mixer, LRG_AUDIO_SOURCE (drums), "drums", 1.0), !=, 0);
	lrg_instrument_schedule (LRG_INSTRUMENT (drums), 4800, LRG_INSTRUMENT_EVENT_NOTE_ON, 36, 1.0);
	out = lrg_audio_mixer_render (mixer, 9600, &n);
	g_assert_cmpfloat (peak (out, 0, 9600), ==, 0.0);
	g_assert_cmpfloat (peak (out, 9600, n), >, 0.1);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/music-instruments/sample-accurate", test_sample_accurate);
	g_test_add_func ("/music-instruments/note-names", test_note_names);
	g_test_add_func ("/music-instruments/synth/presets", test_synth_presets);
	g_test_add_func ("/music-instruments/synth/release", test_synth_release);
	g_test_add_func ("/music-instruments/synth/polyphony", test_synth_polyphony);
	g_test_add_func ("/music-instruments/synth/pitch", test_synth_pitch);
	g_test_add_func ("/music-instruments/synth/deterministic", test_synth_deterministic);
	g_test_add_func ("/music-instruments/synth/params", test_synth_params);
	g_test_add_func ("/music-instruments/drums", test_drums);
	g_test_add_func ("/music-instruments/drums/choke", test_drum_choke);
	g_test_add_func ("/music-instruments/sampler", test_sampler);
	g_test_add_func ("/music-instruments/foley", test_foley_instrument);
	g_test_add_func ("/music-instruments/sound-font", test_sound_font);
	g_test_add_func ("/music-instruments/in-mixer", test_in_mixer);
	return g_test_run ();
}
