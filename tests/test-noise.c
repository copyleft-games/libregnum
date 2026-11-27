/* test-noise.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the seeded noise and band-limited fBm
 * (shading/lrg-noise.h): range, determinism, seed independence,
 * continuity, non-finite inputs, and the band-limiting itself (variance
 * and high-frequency energy fall as the pixel footprint grows, and the
 * band-limited image is closer to a supersampled reference than the
 * unfiltered one).
 *
 * CPU only: no window, no display.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

typedef gfloat (*Noise3) (gfloat x, gfloat y, gfloat z, guint32 seed);

/* 2D functions through the 3D signature, so one loop can check all four */
static gfloat
value2_as3 (gfloat x, gfloat y, gfloat z, guint32 seed)
{
	return lrg_noise_value2 (x, y, seed);
}

static gfloat
simplex2_as3 (gfloat x, gfloat y, gfloat z, guint32 seed)
{
	return lrg_noise_simplex2 (x, y, seed);
}

typedef struct
{
	const gchar *name;
	Noise3       fn;
} NoiseCase;

static const NoiseCase cases[] = {
	{ "value2", value2_as3 },
	{ "value3", lrg_noise_value3 },
	{ "simplex2", simplex2_as3 },
	{ "simplex3", lrg_noise_simplex3 }
};

/* ==========================================================================
 * Basic noise
 * ========================================================================== */

static void
test_noise_range (void)
{
	guint c;

	for (c = 0; c < G_N_ELEMENTS (cases); c++)
	{
		gdouble lo = 1.0;
		gdouble hi = -1.0;
		gdouble sum = 0.0;
		gint n = 0;
		gint i;
		gint j;

		for (i = 0; i < 300; i++)
		{
			for (j = 0; j < 300; j++)
			{
				gfloat v = cases[c].fn (i * 0.173f - 20.0f, j * 0.131f - 15.0f,
				                        (i ^ j) * 0.057f, 7);

				g_assert_cmpfloat (v, >=, -1.0f);
				g_assert_cmpfloat (v, <=, 1.0f);
				lo = MIN (lo, v);
				hi = MAX (hi, v);
				sum += v;
				n++;
			}
		}

		/* the documented range is actually used, and the mean is near 0 */
		if (lo > -0.75 || hi < 0.75 || fabs (sum / n) > 0.05)
			g_error ("%s: range [%g, %g], mean %g", cases[c].name, lo, hi, sum / n);
	}
}

static void
test_noise_determinism_and_seeds (void)
{
	guint c;

	for (c = 0; c < G_N_ELEMENTS (cases); c++)
	{
		gint differ = 0;
		gint i;
		gdouble corr = 0.0;

		for (i = 0; i < 2000; i++)
		{
			gfloat x = i * 0.377f + 0.1f;
			gfloat y = i * -0.219f + 0.3f;
			gfloat z = i * 0.091f + 0.7f;
			gfloat a = cases[c].fn (x, y, z, 1234);
			gfloat b = cases[c].fn (x, y, z, 1234);
			gfloat d = cases[c].fn (x, y, z, 1235);

			/* same seed: bit-identical */
			g_assert_cmpfloat (a, ==, b);
			if (a != d)
				differ++;
			corr += (gdouble)a * d;
		}

		/*
		 * neighbouring seeds give unrelated fields: uncorrelated, and
		 * almost never equal (2D simplex picks one of 16 gradients
		 * per corner, so with two corners in reach about 1 point in
		 * 256 matches by chance)
		 */
		if (differ < 1960 || fabs (corr / 2000.0) > 0.05)
			g_error ("%s: seeds 1234/1235 differ at %d of 2000, correlation %g",
			         cases[c].name, differ, corr / 2000.0);
	}
}

static void
test_noise_continuity (void)
{
	guint c;

	for (c = 0; c < G_N_ELEMENTS (cases); c++)
	{
		gdouble worst = 0.0;
		gint i;

		/* finite differences stay bounded: no jumps at cell borders */
		for (i = 0; i < 20000; i++)
		{
			gfloat x = i * 0.0131f - 50.0f;
			gfloat y = i * 0.0077f + 3.0f;
			gfloat z = i * 0.0041f - 7.0f;
			gfloat h = 1e-3f;
			gfloat a = cases[c].fn (x, y, z, 99);
			gfloat bx = cases[c].fn (x + h, y, z, 99);
			gfloat by = cases[c].fn (x, y + h, z, 99);
			gfloat bz = cases[c].fn (x, y, z + h, 99);

			worst = MAX (worst, fabs (bx - a) / h);
			worst = MAX (worst, fabs (by - a) / h);
			worst = MAX (worst, fabs (bz - a) / h);
		}
		if (worst > 12.0)
			g_error ("%s: slope %g", cases[c].name, worst);
	}
}

static void
test_noise_lattice (void)
{
	gint i;

	/* simplex noise vanishes on its lattice points (the origin is one) */
	g_assert_cmpfloat (lrg_noise_simplex2 (0.0f, 0.0f, 5), ==, 0.0f);
	g_assert_cmpfloat (lrg_noise_simplex3 (0.0f, 0.0f, 0.0f, 5), ==, 0.0f);

	/* value noise at integers is a lattice value; between them it blends */
	for (i = -5; i < 5; i++)
	{
		gfloat a = lrg_noise_value2 ((gfloat)i, 2.0f, 3);
		gfloat b = lrg_noise_value2 ((gfloat)(i + 1), 2.0f, 3);
		gfloat m = lrg_noise_value2 ((gfloat)i + 0.5f, 2.0f, 3);

		g_assert_cmpfloat_with_epsilon (m, 0.5f * (a + b), 1e-5f);
	}
}

static void
test_noise_non_finite (void)
{
	static const gfloat bad[] = { NAN, INFINITY, -INFINITY, 1e30f };
	guint i;

	for (i = 0; i < G_N_ELEMENTS (bad); i++)
	{
		gfloat v = bad[i];

		g_assert_cmpfloat (lrg_noise_value2 (v, 1.0f, 1), ==, 0.0f);
		g_assert_cmpfloat (lrg_noise_value3 (1.0f, v, 1.0f, 1), ==, 0.0f);
		g_assert_cmpfloat (lrg_noise_simplex2 (1.0f, v, 1), ==, 0.0f);
		g_assert_cmpfloat (lrg_noise_simplex3 (1.0f, 1.0f, v, 1), ==, 0.0f);
		g_assert_cmpfloat (lrg_noise_fbm2 (v, 1.0f, 5, 2.0f, 0.5f, 0.0f, 1), ==, 0.0f);
		g_assert_cmpfloat (lrg_noise_fbm3 (1.0f, 1.0f, v, 5, 2.0f, 0.5f, 0.0f, 1), ==, 0.0f);
	}

	/* large but valid coordinates still work */
	g_assert_cmpfloat (fabsf (lrg_noise_simplex2 (1e9f, -3e8f, 1)), <=, 1.0f);
	g_assert_cmpfloat (fabsf (lrg_noise_value3 (-1e12f, 5e11f, 1.25f, 1)), <=, 1.0f);
}

/* ==========================================================================
 * fBm
 * ========================================================================== */

static void
test_fbm_parameters (void)
{
	gfloat a;
	gfloat b;

	/* no octaves, NaN shape parameters: 0 */
	g_assert_cmpfloat (lrg_noise_fbm2 (0.3f, 0.7f, 0, 2.0f, 0.5f, 0.0f, 1), ==, 0.0f);
	g_assert_cmpfloat (lrg_noise_fbm2 (0.3f, 0.7f, -4, 2.0f, 0.5f, 0.0f, 1), ==, 0.0f);
	g_assert_cmpfloat (lrg_noise_fbm2 (0.3f, 0.7f, 4, NAN, 0.5f, 0.0f, 1), ==, 0.0f);
	g_assert_cmpfloat (lrg_noise_fbm3 (0.3f, 0.7f, 0.1f, 4, 2.0f, NAN, 0.0f, 1), ==, 0.0f);

	/* one octave is the (offset) simplex noise itself: in range, non-zero */
	a = lrg_noise_fbm2 (0.3f, 0.7f, 1, 2.0f, 0.5f, 0.0f, 1);
	g_assert_cmpfloat (fabsf (a), <=, 1.0f);

	/* octave counts beyond the maximum are clamped */
	a = lrg_noise_fbm2 (0.3f, 0.7f, LRG_NOISE_MAX_OCTAVES, 1.5f, 0.9f, 0.0f, 1);
	b = lrg_noise_fbm2 (0.3f, 0.7f, 1000, 1.5f, 0.9f, 0.0f, 1);
	g_assert_cmpfloat (a, ==, b);

	/* NaN footprint behaves like 0 (no band-limiting) */
	a = lrg_noise_fbm2 (0.3f, 0.7f, 6, 2.0f, 0.5f, 0.0f, 9);
	b = lrg_noise_fbm2 (0.3f, 0.7f, 6, 2.0f, 0.5f, NAN, 9);
	g_assert_cmpfloat (a, ==, b);

	/* lacunarity <= 1 does not loop forever or crash */
	a = lrg_noise_fbm2 (0.3f, 0.7f, 8, 1.0f, 0.5f, 0.6f, 9);
	g_assert_cmpfloat (fabsf (a), <=, 1.0f);
	a = lrg_noise_fbm3 (0.3f, 0.7f, 0.2f, 8, 0.5f, 0.5f, 0.1f, 9);
	g_assert_cmpfloat (fabsf (a), <=, 1.0f);

	/* deterministic, seed-dependent */
	g_assert_cmpfloat (lrg_noise_fbm3 (1.1f, 2.2f, 3.3f, 6, 2.0f, 0.5f, 0.01f, 4), ==,
	                   lrg_noise_fbm3 (1.1f, 2.2f, 3.3f, 6, 2.0f, 0.5f, 0.01f, 4));
	g_assert_cmpfloat (lrg_noise_fbm3 (1.1f, 2.2f, 3.3f, 6, 2.0f, 0.5f, 0.01f, 4), !=,
	                   lrg_noise_fbm3 (1.1f, 2.2f, 3.3f, 6, 2.0f, 0.5f, 0.01f, 5));
}

/* variance of fBm over a grid at the given footprint */
static gdouble
fbm_variance (gint    dims,
              gfloat  footprint)
{
	gdouble sum = 0.0;
	gdouble sum2 = 0.0;
	gint n = 0;
	gint i;
	gint j;

	for (i = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			gfloat x = i * 0.37f;
			gfloat y = j * 0.29f;
			gfloat v = dims == 2
				? lrg_noise_fbm2 (x, y, 8, 2.0f, 0.5f, footprint, 21)
				: lrg_noise_fbm3 (x, y, 0.5f * x - y, 8, 2.0f, 0.5f, footprint, 21);

			g_assert_cmpfloat (fabsf (v), <=, 1.0f);
			sum += v;
			sum2 += (gdouble)v * v;
			n++;
		}
	}
	return sum2 / n - (sum / n) * (sum / n);
}

static void
test_fbm_variance_falls (void)
{
	static const gfloat fps[] = { 0.0f, 0.004f, 0.016f, 0.06f, 0.25f, 0.4f, 1.0f, 4.0f };
	gint dims;

	for (dims = 2; dims <= 3; dims++)
	{
		gdouble prev = G_MAXDOUBLE;
		guint i;

		for (i = 0; i < G_N_ELEMENTS (fps); i++)
		{
			gdouble v = fbm_variance (dims, fps[i]);

			/*
			 * Expected variance falls strictly; a finite grid's sample
			 * variance may wobble by a fraction of a percent when only
			 * a tiny octave goes (cross terms), so allow 1%.
			 */
			if (v > prev * 1.01 + 1e-9)
				g_error ("fbm%d: variance rose from %g to %g at footprint %g",
				         dims, prev, v, fps[i]);
			prev = v;
		}

		/* a footprint wider than the base wavelength removes everything */
		g_assert_cmpfloat (prev, ==, 0.0);
		g_assert_cmpfloat (fbm_variance (dims, 0.0f), >, 0.02);

		/* and the fall is substantial once real octaves fade */
		g_assert_cmpfloat (fbm_variance (dims, 0.25f), <, 0.9 * fbm_variance (dims, 0.0f));
		g_assert_cmpfloat (fbm_variance (dims, 1.0f), <, 0.1 * fbm_variance (dims, 0.0f));
	}
}

/*
 * high_freq_stats:
 * Renders a W x W image of 10-octave fBm (lacunarity 2, gain @gain)
 * with pixel size @pixel (noise units), one sample per pixel centre,
 * and returns:
 *  - @out_hf: the mean squared difference between horizontal
 *    neighbours, in absolute units (aliased, pixel-level noise drives
 *    it up; a signal limited to a quarter cycle per pixel keeps it
 *    below 2 x its variance);
 *  - @out_err: the RMS error against an 8 x 8 supersampled,
 *    box-filtered reference rendered without band-limiting, i.e. what
 *    brute-force supersampling converges towards.
 */
static void
high_freq_stats (gfloat   pixel,
                 gfloat   gain,
                 gboolean band_limit,
                 gdouble *out_hf,
                 gdouble *out_err)
{
	enum { W = 48, SS = 8 };
	gfloat img[W][W];
	gdouble diff2 = 0.0;
	gdouble err2 = 0.0;
	gint x;
	gint y;

	for (y = 0; y < W; y++)
	{
		for (x = 0; x < W; x++)
		{
			gfloat cx = (x + 0.5f) * pixel + 3.0f;
			gfloat cy = (y + 0.5f) * pixel - 1.0f;
			gdouble ref = 0.0;
			gint sx;
			gint sy;

			img[y][x] = lrg_noise_fbm2 (cx, cy, 10, 2.0f, gain, band_limit ? pixel : 0.0f, 77);
			for (sy = 0; sy < SS; sy++)
				for (sx = 0; sx < SS; sx++)
					ref += lrg_noise_fbm2 (cx + ((sx + 0.5f) / SS - 0.5f) * pixel,
					                       cy + ((sy + 0.5f) / SS - 0.5f) * pixel,
					                       10, 2.0f, gain, 0.0f, 77);
			ref /= SS * SS;
			err2 += (img[y][x] - ref) * (img[y][x] - ref);
		}
	}
	for (y = 0; y < W; y++)
		for (x = 0; x + 1 < W; x++)
			diff2 += (img[y][x + 1] - img[y][x]) * (img[y][x + 1] - img[y][x]);

	*out_hf = diff2 / (W * (W - 1));
	*out_err = sqrt (err2 / (W * W));
}

static void
test_fbm_band_limited_energy (void)
{
	/*
	 * check_err: with gain 0.5 the octaves past Nyquist carry under 2%
	 * of the energy, so the aliasing is smaller than the difference
	 * between the box filter of the reference and the smoothstep fade
	 * itself; the error comparison is only meaningful for rougher fBm.
	 */
	static const struct { gfloat pixel; gfloat gain; gboolean check_err; } runs[] = {
		{ 1.0f / 16.0f, 0.5f, FALSE },	/* ordinary fBm, top octaves 32 cycles/pixel */
		{ 1.0f / 16.0f, 0.75f, TRUE },	/* rough fBm (feathers, fur): strong high octaves */
		{ 1.0f / 4.0f, 0.7f, TRUE },	/* a far-away surface */
		{ 1.0f, 0.6f, TRUE }		/* footprint = the base wavelength: nothing fits */
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (runs); i++)
	{
		gdouble hf_raw;
		gdouble err_raw;
		gdouble hf_bl;
		gdouble err_bl;

		high_freq_stats (runs[i].pixel, runs[i].gain, FALSE, &hf_raw, &err_raw);
		high_freq_stats (runs[i].pixel, runs[i].gain, TRUE, &hf_bl, &err_bl);
		if (g_test_verbose ())
			g_printerr ("pixel %g gain %g: hf raw %g bl %g, err raw %g bl %g\n",
			            runs[i].pixel, runs[i].gain, hf_raw, hf_bl, err_raw, err_bl);

		/* less pixel-level energy than the aliased image, and closer to supersampling */
		g_assert_cmpfloat (hf_bl, <, hf_raw);
		if (runs[i].check_err)
		{
			g_assert_cmpfloat (err_bl, <, err_raw);
			/* rough fBm: at least half the pixel-level energy was aliasing */
			g_assert_cmpfloat (hf_bl, <, 0.5 * hf_raw);
		}
	}

	/*
	 * Footprint = 1 noise unit = one pixel per base wavelength: every
	 * octave is beyond Nyquist, so the band-limited image is flat
	 * (exactly the mean, 0) while the unfiltered one is pure aliasing.
	 */
	{
		gdouble hf_raw;
		gdouble err_raw;
		gdouble hf_bl;
		gdouble err_bl;

		high_freq_stats (1.0f, 0.6f, FALSE, &hf_raw, &err_raw);
		high_freq_stats (1.0f, 0.6f, TRUE, &hf_bl, &err_bl);
		g_assert_cmpfloat (hf_bl, ==, 0.0);
		g_assert_cmpfloat (hf_raw, >, 0.01);
		g_assert_cmpfloat (err_bl, <, 0.5 * err_raw);
	}
}

static void
test_fbm_footprint_continuity (void)
{
	gint i;

	/* no popping: a 1% change of footprint changes the value very little */
	for (i = 0; i < 500; i++)
	{
		gfloat x = i * 0.731f;
		gfloat y = i * -0.417f;
		gfloat fp = 0.001f + i * 0.0005f;
		gfloat a = lrg_noise_fbm2 (x, y, 10, 2.0f, 0.5f, fp, 3);
		gfloat b = lrg_noise_fbm2 (x, y, 10, 2.0f, 0.5f, fp * 1.01f, 3);

		g_assert_cmpfloat (fabsf (a - b), <, 0.02f);
	}
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/noise/range", test_noise_range);
	g_test_add_func ("/noise/determinism-and-seeds", test_noise_determinism_and_seeds);
	g_test_add_func ("/noise/continuity", test_noise_continuity);
	g_test_add_func ("/noise/lattice", test_noise_lattice);
	g_test_add_func ("/noise/non-finite", test_noise_non_finite);
	g_test_add_func ("/noise/fbm/parameters", test_fbm_parameters);
	g_test_add_func ("/noise/fbm/variance-falls", test_fbm_variance_falls);
	g_test_add_func ("/noise/fbm/band-limited-energy", test_fbm_band_limited_energy);
	g_test_add_func ("/noise/fbm/footprint-continuity", test_fbm_footprint_continuity);

	return g_test_run ();
}
