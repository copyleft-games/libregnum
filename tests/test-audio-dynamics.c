/* test-audio-dynamics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the 0.10.0 audio building blocks: pull-source voices in
 * LrgAudioMixer (LrgAudioSource), the unclipped mixer output, and the
 * effects LrgCompressor, LrgLimiter, LrgDistortion, LrgEqualizer,
 * LrgStereoWidener, LrgFilter and LrgGate.  Everything runs on synthetic
 * buffers; no audio device and no data file.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#define RATE (48000)

/* ==========================================================================
 * A test source: a sine of a given amplitude for a given number of frames
 * ========================================================================== */

#define TEST_TYPE_SINE_SOURCE (test_sine_source_get_type ())
G_DECLARE_FINAL_TYPE (TestSineSource, test_sine_source, TEST, SINE_SOURCE, GObject)

struct _TestSineSource
{
	GObject parent_instance;
	gdouble hz;
	gdouble amp;
	guint64 length;     /* frames, 0 = endless */
	guint64 pos;
	guint   renders;
};

static void test_sine_source_iface_init (LrgAudioSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestSineSource, test_sine_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_AUDIO_SOURCE, test_sine_source_iface_init))

static gboolean
sine_render (LrgAudioSource *source,
             gfloat         *out,
             guint           frames,
             guint           channels,
             guint           sample_rate)
{
	TestSineSource *self = TEST_SINE_SOURCE (source);
	guint           f, c;

	self->renders++;
	for (f = 0; f < frames; f++)
	{
		gdouble s;

		if (self->length > 0 && self->pos >= self->length)
			return FALSE;
		s = self->amp * sin (2.0 * G_PI * self->hz * (gdouble) self->pos / (gdouble) sample_rate);
		for (c = 0; c < channels; c++)
			out[f * channels + c] += (gfloat) s;
		self->pos++;
	}
	return self->length == 0 || self->pos < self->length;
}

static void
sine_reset (LrgAudioSource *source)
{
	TEST_SINE_SOURCE (source)->pos = 0;
}

static void
test_sine_source_iface_init (LrgAudioSourceInterface *iface)
{
	iface->render = sine_render;
	iface->reset = sine_reset;
}

static void
test_sine_source_class_init (TestSineSourceClass *klass)
{
	(void) klass;
}

static void
test_sine_source_init (TestSineSource *self)
{
	self->hz = 1000.0;
	self->amp = 0.5;
}

static TestSineSource *
sine_source_new (gdouble hz,
                 gdouble amp,
                 guint64 length)
{
	TestSineSource *self = g_object_new (TEST_TYPE_SINE_SOURCE, NULL);

	self->hz = hz;
	self->amp = amp;
	self->length = length;
	return self;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* a stereo (or mono) sine of @frames frames at @amp */
static gfloat *
make_sine (gdouble hz,
           gdouble amp,
           guint   frames,
           guint   channels)
{
	gfloat *buf = g_new (gfloat, (gsize) frames * channels);
	guint   f, c;

	for (f = 0; f < frames; f++)
		for (c = 0; c < channels; c++)
			buf[f * channels + c] = (gfloat) (amp * sin (2.0 * G_PI * hz * f / RATE));
	return buf;
}

static gdouble
peak_of (const gfloat *buf,
         gsize         from,
         gsize         to)
{
	gdouble p = 0.0;
	gsize   i;

	for (i = from; i < to; i++)
		p = MAX (p, fabs ((gdouble) buf[i]));
	return p;
}

/* the steady-state amplitude gain of @effect for a mono sine at @hz */
static gdouble
tone_gain (LrgAudioEffect *effect,
           gdouble         hz)
{
	guint   frames = RATE / 2;
	gfloat *buf = make_sine (hz, 0.25, frames, 1);
	gdouble gain;

	lrg_audio_effect_reset (effect);
	lrg_audio_effect_process (effect, buf, frames, 1, RATE);
	gain = peak_of (buf, frames / 2, frames) / 0.25;
	g_free (buf);
	return gain;
}

/* ==========================================================================
 * Source voices
 * ========================================================================== */

static void
test_source_voice (void)
{
	g_autoptr(LrgAudioMixer)  mixer = lrg_audio_mixer_new (RATE, 2);
	g_autoptr(TestSineSource) src = sine_source_new (1000.0, 0.5, 1000);
	g_autofree gfloat        *out = NULL;
	gsize                     n = 0;
	guint64                   id;

	id = lrg_audio_mixer_play_source (mixer, LRG_AUDIO_SOURCE (src), "master", 1.0);
	g_assert_cmpuint (id, !=, 0);
	g_assert_true (lrg_audio_mixer_has_voice (mixer, id));
	/* pitch and voice low-pass do not apply to source voices */
	g_assert_false (lrg_audio_mixer_set_voice_pitch (mixer, id, 2.0));
	g_assert_false (lrg_audio_mixer_set_voice_lowpass (mixer, id, 1000.0));

	out = lrg_audio_mixer_render (mixer, 2048, &n);
	g_assert_cmpuint (n, ==, 4096);
	/* the sine is there for 1000 frames, then silence; the voice ended */
	g_assert_cmpfloat (peak_of (out, 0, 2000), >, 0.45);
	g_assert_cmpfloat (peak_of (out, 2000, 4096), ==, 0.0);
	g_assert_false (lrg_audio_mixer_has_voice (mixer, id));

	/* an unknown bus is refused */
	g_assert_cmpuint (lrg_audio_mixer_play_source (mixer, LRG_AUDIO_SOURCE (src), "nope", 1.0), ==, 0);
}

static void
test_source_voice_gain_pan (void)
{
	g_autoptr(LrgAudioMixer)  mixer = lrg_audio_mixer_new (RATE, 2);
	g_autoptr(TestSineSource) src = sine_source_new (500.0, 0.5, 0);
	g_autofree gfloat        *out = NULL;
	gsize                     n = 0;
	guint64                   id;
	gdouble                   left = 0.0, right = 0.0;
	gsize                     i;

	id = lrg_audio_mixer_play_source (mixer, LRG_AUDIO_SOURCE (src), "master", 0.5);
	g_assert_true (lrg_audio_mixer_set_voice_pan (mixer, id, 1.0, 0.0));
	out = lrg_audio_mixer_render (mixer, 4800, &n);
	for (i = 0; i < n; i += 2)
	{
		left = MAX (left, fabs (out[i]));
		right = MAX (right, fabs (out[i + 1]));
	}
	g_assert_cmpfloat (left, <, 1e-6);
	g_assert_cmpfloat_with_epsilon (right, 0.25, 0.01);
}

static void
test_mixer_clip (void)
{
	g_autoptr(LrgAudioMixer)  mixer = lrg_audio_mixer_new (RATE, 1);
	g_autoptr(TestSineSource) src = sine_source_new (250.0, 1.0, 0);
	g_autofree gfloat        *a = NULL;
	g_autofree gfloat        *b = NULL;
	gsize                     n = 0;

	g_assert_true (lrg_audio_mixer_get_clip (mixer));
	lrg_audio_mixer_play_source (mixer, LRG_AUDIO_SOURCE (src), "master", 2.0);
	a = lrg_audio_mixer_render (mixer, 960, &n);
	g_assert_cmpfloat (peak_of (a, 0, n), <=, 1.0);

	lrg_audio_mixer_set_clip (mixer, FALSE);
	g_assert_false (lrg_audio_mixer_get_clip (mixer));
	b = lrg_audio_mixer_render (mixer, 960, &n);
	g_assert_cmpfloat (peak_of (b, 0, n), >, 1.9);
}

/* ==========================================================================
 * Effects
 * ========================================================================== */

static void
test_compressor (void)
{
	g_autoptr(LrgCompressor) comp = lrg_compressor_new (-20.0, 4.0);
	gfloat                  *buf;
	gdouble                  out_db;
	guint                    frames = RATE;

	g_object_set (comp, "knee", 0.0, "attack", 0.001, "release", 0.05, NULL);

	/* a -4 dBFS sine is 16 dB over: the output is near -20 + 16/4 = -16 */
	buf = make_sine (1000.0, lrg_audio_db_to_gain (-4.0), frames, 2);
	lrg_audio_effect_process (LRG_AUDIO_EFFECT (comp), buf, frames, 2, RATE);
	out_db = lrg_audio_gain_to_db (peak_of (buf, (gsize) frames, (gsize) frames * 2));
	g_assert_cmpfloat_with_epsilon (out_db, -16.0, 1.0);
	g_assert_cmpfloat (lrg_compressor_get_gain_reduction (comp), <, -10.0);
	g_free (buf);

	/* below the threshold nothing changes (the tone is at -12 dBFS) */
	g_object_set (comp, "threshold", -6.0, NULL);
	lrg_audio_effect_reset (LRG_AUDIO_EFFECT (comp));
	g_assert_cmpfloat_with_epsilon (tone_gain (LRG_AUDIO_EFFECT (comp), 1000.0), 1.0, 0.01);

	/* out-of-range construction values are clamped, not refused */
	{
		g_autoptr(LrgCompressor) wild = lrg_compressor_new (-500.0, 100.0);
		gdouble                  t = 0.0, r = 0.0;

		g_object_get (wild, "threshold", &t, "ratio", &r, NULL);
		g_assert_cmpfloat (t, ==, -60.0);
		g_assert_cmpfloat (r, ==, 30.0);
	}
}

static void
test_limiter (void)
{
	g_autoptr(LrgLimiter) lim = lrg_limiter_new (-1.0);
	guint                 frames = RATE * 2;
	gfloat               *buf = g_new (gfloat, (gsize) frames * 2);
	guint64               rng = 7;
	gdouble               ceiling = lrg_audio_db_to_gain (-1.0);
	gsize                 i;

	/* loud noise bursts with sudden peaks well over full scale */
	for (i = 0; i < (gsize) frames * 2; i++)
	{
		rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
		buf[i] = (gfloat) ((((gdouble) (rng >> 33) / 2147483648.0) * 2.0 - 1.0) * ((i / 9600) % 2 ? 3.0 : 0.3));
	}
	lrg_audio_effect_process (LRG_AUDIO_EFFECT (lim), buf, frames, 2, RATE);
	g_assert_cmpfloat (peak_of (buf, 0, (gsize) frames * 2), <=, ceiling + 1e-6);
	g_assert_cmpfloat (lrg_limiter_get_gain_reduction (lim), <, -3.0);
	g_assert_cmpuint (lrg_limiter_get_latency (lim, RATE), ==, 72);
	g_free (buf);

	/* quiet material passes at unity, delayed by the look-ahead */
	lrg_audio_effect_reset (LRG_AUDIO_EFFECT (lim));
	g_assert_cmpfloat_with_epsilon (tone_gain (LRG_AUDIO_EFFECT (lim), 440.0), 1.0, 0.01);
}

static void
test_distortion (void)
{
	LrgDistortionMode modes[] = { LRG_DISTORTION_MODE_TANH, LRG_DISTORTION_MODE_SOFT_CLIP,
	                              LRG_DISTORTION_MODE_HARD_CLIP, LRG_DISTORTION_MODE_FOLD,
	                              LRG_DISTORTION_MODE_BITCRUSH };
	guint             m;

	for (m = 0; m < G_N_ELEMENTS (modes); m++)
	{
		g_autoptr(LrgDistortion) dist = lrg_distortion_new (modes[m], 24.0);
		gfloat                  *buf = make_sine (220.0, 0.8, 4800, 2);
		gsize                    i;

		g_object_set (dist, "output", 0.0, "tone", 20000.0, NULL);
		lrg_audio_effect_process (LRG_AUDIO_EFFECT (dist), buf, 4800, 2, RATE);
		for (i = 0; i < 9600; i++)
			g_assert_true (isfinite (buf[i]));
		/* every curve is bounded near full scale and not silent */
		g_assert_cmpfloat (peak_of (buf, 0, 9600), <=, 1.01);
		g_assert_cmpfloat (peak_of (buf, 0, 9600), >, 0.3);
		g_free (buf);
	}
}

static void
test_equalizer (void)
{
	g_autoptr(LrgEqualizer) eq = lrg_equalizer_new ();

	/* flat: untouched */
	g_assert_cmpfloat_with_epsilon (tone_gain (LRG_AUDIO_EFFECT (eq), 1000.0), 1.0, 0.001);

	/* a +6 dB peak at 1 kHz: about 2x at the centre, ~1 far away */
	g_object_set (eq, "mid1-freq", 1000.0, "mid1-gain", 6.0, "mid1-q", 2.0, NULL);
	g_assert_cmpfloat_with_epsilon (tone_gain (LRG_AUDIO_EFFECT (eq), 1000.0), 1.995, 0.05);
	g_assert_cmpfloat_with_epsilon (tone_gain (LRG_AUDIO_EFFECT (eq), 100.0), 1.0, 0.05);

	/* a low cut removes rumble */
	g_object_set (eq, "mid1-gain", 0.0, "low-cut", 200.0, NULL);
	g_assert_cmpfloat (tone_gain (LRG_AUDIO_EFFECT (eq), 30.0), <, 0.1);
}

static void
test_widener (void)
{
	g_autoptr(LrgStereoWidener) wide = lrg_stereo_widener_new (0.0);
	gfloat                     *buf = g_new (gfloat, 2000);
	guint                       f;

	for (f = 0; f < 1000; f++)
	{
		buf[f * 2] = (gfloat) sin (f * 0.05);
		buf[f * 2 + 1] = (gfloat) cos (f * 0.07);
	}
	lrg_audio_effect_process (LRG_AUDIO_EFFECT (wide), buf, 1000, 2, RATE);
	for (f = 0; f < 1000; f++)
		g_assert_cmpfloat_with_epsilon (buf[f * 2], buf[f * 2 + 1], 1e-6);
	g_free (buf);

	/* width 1 is the identity */
	g_object_set (wide, "width", 1.0, NULL);
	{
		gfloat in[4] = { 0.3f, -0.2f, 0.9f, 0.1f };
		gfloat out[4];

		memcpy (out, in, sizeof in);
		lrg_audio_effect_process (LRG_AUDIO_EFFECT (wide), out, 2, 2, RATE);
		for (f = 0; f < 4; f++)
			g_assert_cmpfloat_with_epsilon (out[f], in[f], 1e-6);
	}
}

static void
test_filter (void)
{
	g_autoptr(LrgFilter) lp = lrg_filter_new (LRG_FILTER_MODE_LOWPASS, 500.0, 0.7071);
	gdouble              one, four;

	g_assert_cmpfloat (tone_gain (LRG_AUDIO_EFFECT (lp), 100.0), >, 0.9);
	one = tone_gain (LRG_AUDIO_EFFECT (lp), 4000.0);
	g_assert_cmpfloat (one, <, 0.05);

	/* four stages are much steeper */
	g_object_set (lp, "stages", 4.0, NULL);
	four = tone_gain (LRG_AUDIO_EFFECT (lp), 4000.0);
	g_assert_cmpfloat (four, <, one * 0.01);

	/* the mode is a property too */
	g_object_set (lp, "mode", LRG_FILTER_MODE_HIGHPASS, "stages", 1.0, NULL);
	g_assert_cmpfloat (tone_gain (LRG_AUDIO_EFFECT (lp), 100.0), <, 0.1);
}

static void
test_gate (void)
{
	g_autoptr(LrgGate) gate = lrg_gate_new (-20.0);
	gfloat            *loud = make_sine (300.0, 0.5, 4800, 1);
	gfloat            *quiet = make_sine (300.0, 0.01, 9600, 1);

	g_object_set (gate, "hold", 0.0, "release", 0.005, NULL);
	lrg_audio_effect_process (LRG_AUDIO_EFFECT (gate), loud, 4800, 1, RATE);
	g_assert_cmpfloat (peak_of (loud, 2400, 4800), >, 0.45);
	lrg_audio_effect_process (LRG_AUDIO_EFFECT (gate), quiet, 9600, 1, RATE);
	g_assert_cmpfloat (peak_of (quiet, 4800, 9600), <, 1e-4);
	g_free (loud);
	g_free (quiet);
}

/* ==========================================================================
 * The .rmixer registry knows the new effects
 * ========================================================================== */

static void
test_registry (void)
{
	const gchar *names[] = { "compressor", "limiter", "distortion", "equalizer", "stereo-widener",
	                         "filter", "gate" };
	guint        i;

	for (i = 0; i < G_N_ELEMENTS (names); i++)
	{
		GType t = lrg_mixer_asset_lookup_effect_type (names[i]);

		g_assert_true (g_type_is_a (t, LRG_TYPE_AUDIO_EFFECT));
	}
}

/* a mixer bus chain with the master trio renders and stays bounded */
static void
test_master_chain (void)
{
	g_autoptr(LrgAudioMixer)  mixer = lrg_audio_mixer_new (RATE, 2);
	g_autoptr(TestSineSource) src = sine_source_new (80.0, 0.9, 0);
	g_autoptr(LrgEqualizer)   eq = lrg_equalizer_new ();
	g_autoptr(LrgCompressor)  comp = lrg_compressor_new (-12.0, 3.0);
	g_autoptr(LrgLimiter)     lim = lrg_limiter_new (-1.0);
	LrgAudioEffectChain      *chain;
	g_autofree gfloat        *out = NULL;
	gsize                     n = 0;

	lrg_audio_mixer_set_clip (mixer, FALSE);
	chain = lrg_audio_mixer_get_bus_chain (mixer, "master");
	lrg_audio_effect_chain_add (chain, LRG_AUDIO_EFFECT (eq));
	lrg_audio_effect_chain_add (chain, LRG_AUDIO_EFFECT (comp));
	lrg_audio_effect_chain_add (chain, LRG_AUDIO_EFFECT (lim));
	g_object_set (comp, "makeup", 12.0, NULL);
	lrg_audio_mixer_play_source (mixer, LRG_AUDIO_SOURCE (src), "master", 2.0);
	out = lrg_audio_mixer_render (mixer, RATE / 2, &n);
	g_assert_cmpfloat (peak_of (out, 0, n), <=, lrg_audio_db_to_gain (-1.0) + 1e-6);
	g_assert_cmpfloat (peak_of (out, n / 2, n), >, 0.5);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/audio-dynamics/source/voice", test_source_voice);
	g_test_add_func ("/audio-dynamics/source/gain-pan", test_source_voice_gain_pan);
	g_test_add_func ("/audio-dynamics/mixer/clip", test_mixer_clip);
	g_test_add_func ("/audio-dynamics/compressor", test_compressor);
	g_test_add_func ("/audio-dynamics/limiter", test_limiter);
	g_test_add_func ("/audio-dynamics/distortion", test_distortion);
	g_test_add_func ("/audio-dynamics/equalizer", test_equalizer);
	g_test_add_func ("/audio-dynamics/widener", test_widener);
	g_test_add_func ("/audio-dynamics/filter", test_filter);
	g_test_add_func ("/audio-dynamics/gate", test_gate);
	g_test_add_func ("/audio-dynamics/registry", test_registry);
	g_test_add_func ("/audio-dynamics/master-chain", test_master_chain);
	return g_test_run ();
}
