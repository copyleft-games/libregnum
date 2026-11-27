/* test-image-resample.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for lrg_image_downsample().
 *
 * The references are computed here, independently of the library: the
 * sRGB transfer functions and the box / Mitchell / Lanczos kernels are
 * re-derived in doubles, and every expected byte comes from them.
 * Everything is headless (no GL context).
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Independent references
 * ========================================================================== */

/* ref_decode: sRGB byte to linear light, IEC 61966-2-1 */
static gdouble
ref_decode (guint8 b)
{
	gdouble v = b / 255.0;

	return v <= 0.04045 ? v / 12.92 : pow ((v + 0.055) / 1.055, 2.4);
}

/* ref_encode: linear (clamped to 0..1) to a rounded sRGB byte */
static gint
ref_encode (gdouble v)
{
	gdouble e;

	v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
	e = v <= 0.0031308 ? v * 12.92 : 1.055 * pow (v, 1.0 / 2.4) - 0.055;
	return (gint) floor (e * 255.0 + 0.5);
}

/* ref_kernel: the three kernels in output-pixel units */
static gdouble
ref_kernel (LrgResampleFilter filter,
            gdouble           t)
{
	gdouble x = fabs (t);
	gdouble b = 1.0 / 3.0;
	gdouble c = 1.0 / 3.0;

	switch (filter)
	{
	case LRG_RESAMPLE_FILTER_MITCHELL:
		/* the general Mitchell-Netravali cubic, evaluated with B and C */
		if (x < 1.0)
			return ((12 - 9 * b - 6 * c) * x * x * x + (-18 + 12 * b + 6 * c) * x * x + (6 - 2 * b)) / 6.0;
		if (x < 2.0)
			return ((-b - 6 * c) * x * x * x + (6 * b + 30 * c) * x * x + (-12 * b - 48 * c) * x + (8 * b + 24 * c)) / 6.0;
		return 0.0;
	case LRG_RESAMPLE_FILTER_LANCZOS3:
		if (x == 0.0)
			return 1.0;
		if (x >= 3.0)
			return 0.0;
		return (sin (G_PI * x) / (G_PI * x)) * (sin (G_PI * x / 3.0) / (G_PI * x / 3.0));
	case LRG_RESAMPLE_FILTER_BOX:
	default:
		return x < 0.5 ? 1.0 : 0.0;
	}
}

/* ref_downsample_row: one opaque grey row (bytes) shrunk by factor,
 * unclamped linear results in out_lin (n / factor values) */
static void
ref_downsample_row (const guint8      *row,
                    gint               n,
                    gint               factor,
                    LrgResampleFilter  filter,
                    gdouble           *out_lin)
{
	gint o;

	for (o = 0; o < n / factor; o++)
	{
		gdouble cen = (o + 0.5) * factor;
		gdouble sum = 0.0;
		gdouble wsum = 0.0;
		gint    s;

		/* every source pixel in a generous window; clamp to the edge */
		for (s = (gint) cen - 4 * factor; s <= (gint) cen + 4 * factor; s++)
		{
			gdouble w = ref_kernel (filter, (s + 0.5 - cen) / factor);
			gint    si = CLAMP (s, 0, n - 1);

			sum += w * ref_decode (row[si]);
			wsum += w;
		}
		out_lin[o] = sum / wsum;
	}
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static GrlImage *
image_from_rgba (gint          w,
                 gint          h,
                 const guint8 *rgba)
{
	return grl_image_new_from_pixels (w, h, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, rgba);
}

static const guint8 *
pixels (GrlImage *img)
{
	return grl_image_get_pixels (img, NULL);
}

static const LrgResampleFilter all_filters[] = {
	LRG_RESAMPLE_FILTER_BOX,
	LRG_RESAMPLE_FILTER_MITCHELL,
	LRG_RESAMPLE_FILTER_LANCZOS3
};

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The enum is registered with the expected nicks */
static void
test_resample_enum (void)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_RESAMPLE_FILTER);

	g_assert_cmpstr (g_enum_get_value (klass, LRG_RESAMPLE_FILTER_BOX)->value_nick, ==, "box");
	g_assert_cmpstr (g_enum_get_value (klass, LRG_RESAMPLE_FILTER_MITCHELL)->value_nick, ==, "mitchell");
	g_assert_cmpstr (g_enum_get_value (klass, LRG_RESAMPLE_FILTER_LANCZOS3)->value_nick, ==, "lanczos3");
}

/* A box downsample of a 2x2 block is the linear-light mean */
static void
test_resample_box_2x2 (void)
{
	/* 4 x 2: two 2x2 blocks with different, known channel values */
	static const guint8 src[] = {
		0, 255, 128, 255,     255, 10, 64, 255,       40, 40, 40, 255,   200, 200, 200, 255,
		128, 64, 0, 255,      64, 128, 255, 255,      90, 90, 90, 255,   250, 250, 250, 255,
	};
	g_autoptr(GrlImage) in = image_from_rgba (4, 2, src);
	g_autoptr(GrlImage) out = NULL;
	const guint8       *p;
	gint                blk, c;

	out = lrg_image_downsample (in, 2, LRG_RESAMPLE_FILTER_BOX);
	g_assert_nonnull (out);
	g_assert_cmpint (grl_image_get_width (out), ==, 2);
	g_assert_cmpint (grl_image_get_height (out), ==, 1);
	p = pixels (out);

	for (blk = 0; blk < 2; blk++)
	{
		for (c = 0; c < 3; c++)
		{
			gint    x0 = blk * 2;
			gdouble mean = (ref_decode (src[(0 * 4 + x0) * 4 + c]) +
			                ref_decode (src[(0 * 4 + x0 + 1) * 4 + c]) +
			                ref_decode (src[(1 * 4 + x0) * 4 + c]) +
			                ref_decode (src[(1 * 4 + x0 + 1) * 4 + c])) / 4.0;

			g_assert_cmpint (p[blk * 4 + c], ==, ref_encode (mean));
		}
		g_assert_cmpint (p[blk * 4 + 3], ==, 255);
	}
}

/* A 1-px black / white checkerboard halves to linear mid grey: sRGB
 * 188, not the 128 a gamma-space average gives */
static void
test_resample_checkerboard (void)
{
	guint8              src[8 * 8 * 4];
	g_autoptr(GrlImage) in = NULL;
	g_autoptr(GrlImage) out = NULL;
	const guint8       *p;
	gint                x, y, i;

	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++)
		{
			guint8 v = ((x + y) & 1) ? 255 : 0;

			src[(y * 8 + x) * 4 + 0] = v;
			src[(y * 8 + x) * 4 + 1] = v;
			src[(y * 8 + x) * 4 + 2] = v;
			src[(y * 8 + x) * 4 + 3] = 255;
		}
	in = image_from_rgba (8, 8, src);
	out = lrg_image_downsample (in, 2, LRG_RESAMPLE_FILTER_BOX);
	p = pixels (out);

	g_assert_cmpint (ref_encode (0.5), ==, 188);
	for (i = 0; i < 4 * 4; i++)
	{
		g_assert_cmpint (p[i * 4 + 0], ==, 188);
		g_assert_cmpint (p[i * 4 + 1], ==, 188);
		g_assert_cmpint (p[i * 4 + 2], ==, 188);
		g_assert_cmpint (p[i * 4 + 3], ==, 255);
	}
}

/* Constant images stay constant under every filter and factor, also
 * with sizes that are not multiples of the factor and with alpha */
static void
test_resample_constant (void)
{
	static const guint8 colours[][4] = {
		{ 200, 100, 30, 255 },
		{ 17, 250, 128, 77 },
		{ 0, 0, 0, 0 },
		{ 255, 255, 255, 1 },
	};
	gsize f, ci;
	guint factor;

	for (ci = 0; ci < G_N_ELEMENTS (colours); ci++)
		for (f = 0; f < G_N_ELEMENTS (all_filters); f++)
			for (factor = 2; factor <= 4; factor++)
			{
				g_autoptr(GrlColor) col = grl_color_new (colours[ci][0], colours[ci][1],
				                                         colours[ci][2], colours[ci][3]);
				g_autoptr(GrlImage) in = grl_image_new_color (10, 7, col);
				g_autoptr(GrlImage) out = lrg_image_downsample (in, factor, all_filters[f]);
				const guint8       *p;
				gint                i, n;

				g_assert_nonnull (out);
				g_assert_cmpint (grl_image_get_width (out), ==, 10 / (gint) factor);
				g_assert_cmpint (grl_image_get_height (out), ==, 7 / (gint) factor);
				p = pixels (out);
				n = grl_image_get_width (out) * grl_image_get_height (out);
				for (i = 0; i < n; i++)
				{
					g_assert_cmpint (p[i * 4 + 3], ==, colours[ci][3]);
					if (colours[ci][3] == 0)
						continue;   /* no colour to keep where nothing is covered */
					g_assert_cmpint (p[i * 4 + 0], ==, colours[ci][0]);
					g_assert_cmpint (p[i * 4 + 1], ==, colours[ci][1]);
					g_assert_cmpint (p[i * 4 + 2], ==, colours[ci][2]);
				}
			}
}

/* Premultiplied filtering: an opaque red shape next to transparent black
 * keeps its colour at the edge, only the alpha falls off */
static void
test_resample_alpha_edge (void)
{
	guint8 src[8 * 2 * 4];
	gsize  f;
	gint   x, y;

	/* columns 0..2 opaque red, 3..7 transparent black */
	for (y = 0; y < 2; y++)
		for (x = 0; x < 8; x++)
		{
			guint8 *d = src + (y * 8 + x) * 4;

			d[0] = x < 3 ? 255 : 0;
			d[1] = 0;
			d[2] = 0;
			d[3] = x < 3 ? 255 : 0;
		}

	for (f = 0; f < G_N_ELEMENTS (all_filters); f++)
	{
		g_autoptr(GrlImage) in = image_from_rgba (8, 2, src);
		g_autoptr(GrlImage) out = lrg_image_downsample (in, 2, all_filters[f]);
		const guint8       *p = pixels (out);

		for (x = 0; x < 4; x++)
		{
			if (p[x * 4 + 3] == 0)
				continue;
			/* a straight average would give dark red (128) at the edge */
			g_assert_cmpint (p[x * 4 + 0], ==, 255);
			g_assert_cmpint (p[x * 4 + 1], ==, 0);
			g_assert_cmpint (p[x * 4 + 2], ==, 0);
		}

		/* the box straddling the edge (source columns 2, 3) is half covered */
		if (all_filters[f] == LRG_RESAMPLE_FILTER_BOX)
		{
			g_assert_cmpint (p[0 * 4 + 3], ==, 255);
			g_assert_cmpint (p[1 * 4 + 3], ==, 128);
			g_assert_cmpint (p[2 * 4 + 3], ==, 0);
		}
	}
}

/* Mitchell and Lanczos match an independent 1-D reference, ringing
 * clamped: a thin bright line on black overshoots and undershoots in the
 * unclamped reference, and the output stays in 0..255 without wrapping */
static void
test_resample_kernels_reference (void)
{
	enum { W = 48, H = 3 };
	guint8 row[W];
	guint8 src[W * H * 4];
	gsize  f;
	gint   x, y;
	guint  factor;

	/* black row with a 3-px white line and a soft ramp further right */
	for (x = 0; x < W; x++)
	{
		if (x >= 12 && x < 15)
			row[x] = 255;
		else if (x >= 30)
			row[x] = (guint8) ((x - 30) * 14);
		else
			row[x] = 0;
	}
	for (factor = 2; factor <= 3; factor++)
		for (f = 0; f < G_N_ELEMENTS (all_filters); f++)
		{
			g_autoptr(GrlImage) in = NULL;
			g_autoptr(GrlImage) out = NULL;
			gdouble             ref[W];
			gboolean            rang_low = FALSE;
			gboolean            rang_high = FALSE;
			const guint8       *p;
			gint                ow;

			/* H identical rows (H = factor rows used): the vertical pass of a
			 * constant column is the identity */
			for (y = 0; y < H; y++)
				for (x = 0; x < W; x++)
				{
					guint8 *d = src + (y * W + x) * 4;

					d[0] = d[1] = d[2] = row[x];
					d[3] = 255;
				}
			in = image_from_rgba (W, (gint) factor, src);
			out = lrg_image_downsample (in, factor, all_filters[f]);
			g_assert_nonnull (out);
			ow = W / (gint) factor;
			g_assert_cmpint (grl_image_get_width (out), ==, ow);
			p = pixels (out);

			ref_downsample_row (row, W, (gint) factor, all_filters[f], ref);
			for (x = 0; x < ow; x++)
			{
				gint want = ref_encode (ref[x]);

				if (ref[x] < -1e-4)
					rang_low = TRUE;
				if (ref[x] > 1.0 + 1e-4)
					rang_high = TRUE;
				/* float vs double near a rounding boundary: one step */
				g_assert_cmpint (ABS ((gint) p[x * 4 + 0] - want), <=, 1);
				g_assert_cmpint (p[x * 4 + 0], ==, p[x * 4 + 1]);
				g_assert_cmpint (p[x * 4 + 3], ==, 255);
			}

			/* the negative lobes do ring on this input (so the clamp is
			 * what kept the output in range) */
			if (all_filters[f] == LRG_RESAMPLE_FILTER_LANCZOS3)
				g_assert_true (rang_low);
			if (all_filters[f] == LRG_RESAMPLE_FILTER_BOX)
				g_assert_false (rang_low || rang_high);
		}
}

/* Factor 1 is an exact copy, not the same object */
static void
test_resample_factor_one (void)
{
	static const guint8 src[] = {
		1, 2, 3, 4,   250, 128, 0, 255,   9, 99, 199, 33,
	};
	g_autoptr(GrlImage) in = image_from_rgba (3, 1, src);
	g_autoptr(GrlImage) out = lrg_image_downsample (in, 1, LRG_RESAMPLE_FILTER_LANCZOS3);

	g_assert_nonnull (out);
	g_assert_true (out != in);
	g_assert_cmpint (grl_image_get_width (out), ==, 3);
	g_assert_cmpint (grl_image_get_height (out), ==, 1);
	g_assert_cmpmem (pixels (out), sizeof src, src, sizeof src);
}

/* Too small for the factor: NULL, no warning; the source is untouched */
static void
test_resample_too_small (void)
{
	g_autoptr(GrlColor) col = grl_color_new (10, 20, 30, 255);
	g_autoptr(GrlImage) in = grl_image_new_color (3, 8, col);

	g_assert_null (lrg_image_downsample (in, 4, LRG_RESAMPLE_FILTER_BOX));
	g_assert_cmpint (grl_image_get_width (in), ==, 3);
}

/* Factor 0 and factors above the maximum are programmer errors */
static void
test_resample_invalid_factor (void)
{
	if (g_test_subprocess ())
	{
		g_autoptr(GrlColor) col = grl_color_new (10, 20, 30, 255);
		g_autoptr(GrlImage) in = grl_image_new_color (64, 64, col);
		const gchar        *which = g_getenv ("LRG_TEST_FACTOR");
		guint               factor = (guint) g_ascii_strtoull (which, NULL, 10);

		lrg_image_downsample (in, factor, LRG_RESAMPLE_FILTER_BOX);
		return;
	}

	g_setenv ("LRG_TEST_FACTOR", "0", TRUE);
	g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*factor*");

	g_setenv ("LRG_TEST_FACTOR", "17", TRUE);
	g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*factor*");
	g_unsetenv ("LRG_TEST_FACTOR");
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/image-resample/enum", test_resample_enum);
	g_test_add_func ("/image-resample/box-2x2", test_resample_box_2x2);
	g_test_add_func ("/image-resample/checkerboard", test_resample_checkerboard);
	g_test_add_func ("/image-resample/constant", test_resample_constant);
	g_test_add_func ("/image-resample/alpha-edge", test_resample_alpha_edge);
	g_test_add_func ("/image-resample/kernels-reference", test_resample_kernels_reference);
	g_test_add_func ("/image-resample/factor-one", test_resample_factor_one);
	g_test_add_func ("/image-resample/too-small", test_resample_too_small);
	g_test_add_func ("/image-resample/invalid-factor", test_resample_invalid_factor);

	return g_test_run ();
}
