/* test-hdr-canvas.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgHdrCanvas and lrg_tonemap_operator_apply_rgb().
 *
 * Expected values are computed here from the formulas (premultiplied
 * source over, the tone-map curves as written in lrg-tonemap.c before
 * the curves moved into the shared helper, the IEC 61966-2-1 sRGB
 * encode), not read back from the library.  Headless: no GL context.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-6)

/* ==========================================================================
 * Independent references
 * ========================================================================== */

/* ref_curve: the operator curves exactly as the old lrg_tonemap.c had them */
static gfloat
ref_hable (gfloat x)
{
	const gfloat a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;

	return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

static gfloat
ref_curve (LrgTonemapOperator op,
           gfloat             x,
           gfloat             w)
{
	switch (op)
	{
	case LRG_TONEMAP_OPERATOR_REINHARD:
		return x * (1.0f + x / (w * w)) / (1.0f + x);
	case LRG_TONEMAP_OPERATOR_FILMIC:
		return ref_hable (x * 2.0f) / ref_hable (w);
	case LRG_TONEMAP_OPERATOR_ACES:
		return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
	case LRG_TONEMAP_OPERATOR_NONE:
	default:
		return x;
	}
}

/* ref_old_apply_rgb: the body of lrg_tonemap_apply_rgb() before this change */
static void
ref_old_apply_rgb (LrgTonemapOperator op,
                   gfloat             exposure,
                   gfloat             white,
                   gboolean           linear_input,
                   gfloat             k,
                   const gfloat      *rgb,
                   gfloat            *out)
{
	gfloat scale = exp2f (exposure);
	gint   i;

	for (i = 0; i < 3; i++)
	{
		gfloat x = MAX (rgb[i], 0.0f);
		gfloat m;

		if (!linear_input)
			x = powf (x, 2.2f);
		m = ref_curve (op, x * scale, white);
		m = powf (CLAMP (m, 0.0f, 1.0f), 1.0f / 2.2f);
		out[i] = rgb[i] + (m - rgb[i]) * k;
	}
}

/* ref_encode: linear 0..1 to a rounded sRGB byte (doubles) */
static gint
ref_encode (gdouble v)
{
	gdouble e;

	v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
	e = v <= 0.0031308 ? v * 12.92 : 1.055 * pow (v, 1.0 / 2.4) - 0.055;
	return (gint) floor (e * 255.0 + 0.5);
}

static const LrgTonemapOperator all_ops[] = {
	LRG_TONEMAP_OPERATOR_NONE,
	LRG_TONEMAP_OPERATOR_REINHARD,
	LRG_TONEMAP_OPERATOR_FILMIC,
	LRG_TONEMAP_OPERATOR_ACES
};

/* ==========================================================================
 * Construction and storage
 * ========================================================================== */

static void
test_hdr_canvas_new (void)
{
	g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (5, 3);
	const gfloat           *data;
	gsize                   n = 0;
	gsize                   i;

	g_assert_true (LRG_IS_HDR_CANVAS (canvas));
	g_assert_cmpuint (lrg_hdr_canvas_get_width (canvas), ==, 5);
	g_assert_cmpuint (lrg_hdr_canvas_get_height (canvas), ==, 3);
	data = lrg_hdr_canvas_get_data (canvas, &n);
	g_assert_cmpuint (n, ==, 5 * 3 * 4);
	for (i = 0; i < n; i++)
		g_assert_cmpfloat (data[i], ==, 0.0f);
}

static void
test_hdr_canvas_new_invalid (void)
{
	if (g_test_subprocess ())
	{
		lrg_hdr_canvas_new (0, 4);
		return;
	}
	g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*width*");
}

/* Straight alpha in, premultiplied in storage, straight alpha out */
static void
test_hdr_canvas_premultiplied (void)
{
	g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (4, 4);
	const gfloat           *data = lrg_hdr_canvas_get_data (canvas, NULL);
	gfloat                  px[4];

	lrg_hdr_canvas_set_pixel (canvas, 1, 2, 2.0f, 0.5f, 0.25f, 0.5f);

	/* storage at (1, 2) is colour x alpha */
	g_assert_cmpfloat_with_epsilon (data[(2 * 4 + 1) * 4 + 0], 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (data[(2 * 4 + 1) * 4 + 1], 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (data[(2 * 4 + 1) * 4 + 2], 0.125f, EPS);
	g_assert_cmpfloat_with_epsilon (data[(2 * 4 + 1) * 4 + 3], 0.5f, EPS);

	/* and reads back as the straight colour */
	lrg_hdr_canvas_get_pixel (canvas, 1, 2, px);
	g_assert_cmpfloat_with_epsilon (px[0], 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (px[1], 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (px[2], 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (px[3], 0.5f, EPS);

	/* alpha 0 has no colour to recover; alpha is clamped into 0..1 */
	lrg_hdr_canvas_set_pixel (canvas, 0, 0, 3.0f, 3.0f, 3.0f, 0.0f);
	lrg_hdr_canvas_get_pixel (canvas, 0, 0, px);
	g_assert_cmpfloat (px[0], ==, 0.0f);
	g_assert_cmpfloat (px[3], ==, 0.0f);
	lrg_hdr_canvas_set_pixel (canvas, 3, 3, 0.5f, 0.5f, 0.5f, 7.0f);
	lrg_hdr_canvas_get_pixel (canvas, 3, 3, px);
	g_assert_cmpfloat (px[3], ==, 1.0f);
	g_assert_cmpfloat_with_epsilon (px[0], 0.5f, EPS);

	/* outside: writes ignored, reads zero */
	lrg_hdr_canvas_set_pixel (canvas, -1, 0, 1.0f, 1.0f, 1.0f, 1.0f);
	lrg_hdr_canvas_set_pixel (canvas, 4, 0, 1.0f, 1.0f, 1.0f, 1.0f);
	lrg_hdr_canvas_get_pixel (canvas, 0, 99, px);
	g_assert_cmpfloat (px[0], ==, 0.0f);
	g_assert_cmpfloat (px[3], ==, 0.0f);

	/* clear stores premultiplied too */
	lrg_hdr_canvas_clear (canvas, 4.0f, 2.0f, 1.0f, 0.25f);
	g_assert_cmpfloat_with_epsilon (data[0], 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (data[1], 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (data[2], 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (data[3], 0.25f, EPS);
	lrg_hdr_canvas_get_pixel (canvas, 3, 3, px);
	g_assert_cmpfloat_with_epsilon (px[0], 4.0f, EPS);
}

/* Source over in premultiplied form, alpha included */
static void
test_hdr_canvas_blend_over (void)
{
	g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (3, 1);
	const gfloat           *d = lrg_hdr_canvas_get_data (canvas, NULL);
	gfloat                  px[4];

	/* over an opaque pixel: lerp of the straight colours */
	lrg_hdr_canvas_set_pixel (canvas, 0, 0, 0.2f, 0.4f, 0.6f, 1.0f);
	lrg_hdr_canvas_blend_over (canvas, 0, 0, 1.0f, 0.0f, 0.0f, 0.25f);
	g_assert_cmpfloat_with_epsilon (d[0], 0.25f + 0.2f * 0.75f, EPS);
	g_assert_cmpfloat_with_epsilon (d[1], 0.4f * 0.75f, EPS);
	g_assert_cmpfloat_with_epsilon (d[2], 0.6f * 0.75f, EPS);
	g_assert_cmpfloat_with_epsilon (d[3], 1.0f, EPS);

	/* over transparent: the source itself */
	lrg_hdr_canvas_blend_over (canvas, 1, 0, 3.0f, 2.0f, 1.0f, 0.5f);
	lrg_hdr_canvas_get_pixel (canvas, 1, 0, px);
	g_assert_cmpfloat_with_epsilon (px[0], 3.0f, EPS);
	g_assert_cmpfloat_with_epsilon (px[1], 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (px[2], 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (px[3], 0.5f, EPS);

	/* half red over half blue: premultiplied (0.5, 0, 0.25, 0.75) */
	lrg_hdr_canvas_set_pixel (canvas, 2, 0, 0.0f, 0.0f, 1.0f, 0.5f);
	lrg_hdr_canvas_blend_over (canvas, 2, 0, 1.0f, 0.0f, 0.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (d[8 + 0], 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (d[8 + 1], 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (d[8 + 2], 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (d[8 + 3], 0.75f, EPS);

	/* outside is ignored (no crash, nothing changes) */
	lrg_hdr_canvas_blend_over (canvas, 3, 0, 1.0f, 1.0f, 1.0f, 1.0f);
	g_assert_cmpfloat_with_epsilon (d[8 + 3], 0.75f, EPS);
}

/* add sums light into the stored colour, alpha untouched */
static void
test_hdr_canvas_add (void)
{
	g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (2, 1);
	const gfloat           *d = lrg_hdr_canvas_get_data (canvas, NULL);

	lrg_hdr_canvas_set_pixel (canvas, 0, 0, 0.5f, 0.5f, 0.5f, 1.0f);
	lrg_hdr_canvas_add (canvas, 0, 0, 10.0f, 0.25f, 0.0f);
	lrg_hdr_canvas_add (canvas, 0, 0, 1.0f, 0.0f, 0.0f);
	g_assert_cmpfloat_with_epsilon (d[0], 11.5f, EPS);
	g_assert_cmpfloat_with_epsilon (d[1], 0.75f, EPS);
	g_assert_cmpfloat_with_epsilon (d[2], 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (d[3], 1.0f, EPS);
	lrg_hdr_canvas_add (canvas, 5, 5, 1.0f, 1.0f, 1.0f);
}

/* ==========================================================================
 * Shading
 * ========================================================================== */

typedef struct
{
	gfloat edge;
	guint  calls;
	gfloat min_x, max_x;
} EdgeShader;

/* white left of the edge, nothing right of it */
static void
edge_shade (gfloat   x,
            gfloat   y,
            gfloat  *out,
            gpointer user_data)
{
	EdgeShader *s = user_data;

	(void) y;
	s->calls++;
	s->min_x = MIN (s->min_x, x);
	s->max_x = MAX (s->max_x, x);
	if (x < s->edge)
	{
		out[0] = out[1] = out[2] = 1.0f;
		out[3] = 1.0f;
	}
	else
	{
		/* colour without alpha must not leak in */
		out[0] = 100.0f;
		out[3] = 0.0f;
	}
}

static void
test_hdr_canvas_shade_coverage (void)
{
	gfloat px[4];
	guint  samples;

	/* samples 1: the pixel centre decides; 10.5 lies right of 10.25 */
	{
		g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (16, 2);
		EdgeShader              s = { 10.25f, 0, 1e9f, -1e9f };

		lrg_hdr_canvas_shade (canvas, edge_shade, &s, 1);
		g_assert_cmpuint (s.calls, ==, 16 * 2);
		g_assert_cmpfloat_with_epsilon (s.min_x, 0.5f, EPS);
		g_assert_cmpfloat_with_epsilon (s.max_x, 15.5f, EPS);
		lrg_hdr_canvas_get_pixel (canvas, 9, 0, px);
		g_assert_cmpfloat_with_epsilon (px[3], 1.0f, EPS);
		lrg_hdr_canvas_get_pixel (canvas, 10, 1, px);
		g_assert_cmpfloat (px[3], ==, 0.0f);
	}

	/* samples 4: x = 10.125 is the only one of 10.125 .. 10.875 inside,
	 * so the pixel is a quarter covered, and still pure white */
	{
		g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (16, 2);
		EdgeShader              s = { 10.25f, 0, 1e9f, -1e9f };

		lrg_hdr_canvas_shade (canvas, edge_shade, &s, 4);
		g_assert_cmpuint (s.calls, ==, 16 * 2 * 16);
		g_assert_cmpfloat_with_epsilon (s.min_x, 0.125f, EPS);
		g_assert_cmpfloat_with_epsilon (s.max_x, 15.875f, EPS);
		lrg_hdr_canvas_get_pixel (canvas, 10, 0, px);
		g_assert_cmpfloat_with_epsilon (px[3], 0.25f, EPS);
		g_assert_cmpfloat_with_epsilon (px[0], 1.0f, EPS);
		g_assert_cmpfloat_with_epsilon (px[1], 1.0f, EPS);
		lrg_hdr_canvas_get_pixel (canvas, 9, 0, px);
		g_assert_cmpfloat_with_epsilon (px[3], 1.0f, EPS);
		lrg_hdr_canvas_get_pixel (canvas, 11, 0, px);
		g_assert_cmpfloat (px[3], ==, 0.0f);
	}

	/* the average composites over existing content: a quarter white over
	 * opaque black is premultiplied (0.25, 0.25, 0.25, 1) */
	{
		g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (16, 1);
		EdgeShader              s = { 10.25f, 0, 1e9f, -1e9f };
		const gfloat           *d = lrg_hdr_canvas_get_data (canvas, NULL);

		lrg_hdr_canvas_clear (canvas, 0.0f, 0.0f, 0.0f, 1.0f);
		lrg_hdr_canvas_shade (canvas, edge_shade, &s, 4);
		g_assert_cmpfloat_with_epsilon (d[10 * 4 + 0], 0.25f, EPS);
		g_assert_cmpfloat_with_epsilon (d[10 * 4 + 3], 1.0f, EPS);
		g_assert_cmpfloat_with_epsilon (d[15 * 4 + 0], 0.0f, EPS);
	}

	/* 0 samples counts as 1; huge counts are capped */
	for (samples = 0; samples <= 1; samples++)
	{
		g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (2, 1);
		EdgeShader              s = { 10.0f, 0, 1e9f, -1e9f };

		lrg_hdr_canvas_shade (canvas, edge_shade, &s, samples);
		g_assert_cmpuint (s.calls, ==, 2);
	}
	{
		g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (1, 1);
		EdgeShader              s = { 10.0f, 0, 1e9f, -1e9f };

		lrg_hdr_canvas_shade (canvas, edge_shade, &s, 1000);
		g_assert_cmpuint (s.calls, ==, LRG_HDR_CANVAS_MAX_SAMPLES * LRG_HDR_CANVAS_MAX_SAMPLES);
	}
}

/* ==========================================================================
 * Tone mapping
 * ========================================================================== */

/* The shared helper reproduces the old curve arithmetic, and
 * lrg_tonemap_apply_rgb() (which now calls it) still matches the old
 * per-channel code for every operator and setting */
static void
test_tonemap_helper_matches_old (void)
{
	static const gfloat inputs[][3] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.18f, 0.5f, 1.0f }, { 2.0f, 4.0f, 16.0f },
		{ -1.0f, 0.001f, 100.0f }, { 0.73f, 0.04f, 7.5f },
	};
	static const gfloat whites[] = { 1.0f, 4.0f, 11.2f };
	static const gfloat exposures[] = { -2.0f, 0.0f, 1.5f };
	g_autoptr(LrgTonemap) tm = lrg_tonemap_new ();
	gsize o, i, w, e;
	gint  lin, c;

	for (o = 0; o < G_N_ELEMENTS (all_ops); o++)
		for (w = 0; w < G_N_ELEMENTS (whites); w++)
			for (i = 0; i < G_N_ELEMENTS (inputs); i++)
			{
				gfloat out[3];

				/* the helper alone: curve, clamp, nothing else */
				lrg_tonemap_operator_apply_rgb (all_ops[o], whites[w], inputs[i], out);
				for (c = 0; c < 3; c++)
				{
					gfloat x = MAX (inputs[i][c], 0.0f);
					gfloat want = CLAMP (ref_curve (all_ops[o], x, whites[w]), 0.0f, 1.0f);

					g_assert_cmpfloat_with_epsilon (out[c], want, EPS);
				}

				/* the effect's CPU path, every exposure and input mode */
				lrg_tonemap_set_operator (tm, all_ops[o]);
				lrg_tonemap_set_white_point (tm, whites[w]);
				for (e = 0; e < G_N_ELEMENTS (exposures); e++)
					for (lin = 0; lin <= 1; lin++)
					{
						gfloat got[3], want[3];

						lrg_tonemap_set_exposure (tm, exposures[e]);
						lrg_tonemap_set_linear_input (tm, lin);
						lrg_tonemap_apply_rgb (tm, inputs[i], got);
						ref_old_apply_rgb (all_ops[o], exposures[e], whites[w], lin,
						                   lrg_post_effect_get_intensity (LRG_POST_EFFECT (tm)),
						                   inputs[i], want);
						for (c = 0; c < 3; c++)
							g_assert_cmpfloat_with_epsilon (got[c], want[c], EPS);
					}
			}
}

/* Edge cases of the helper: NaN, aliasing, a degenerate white point */
static void
test_tonemap_helper_edges (void)
{
	gfloat v[3] = { NAN, 1.0f, 2.0f };
	gfloat want1 = CLAMP (ref_curve (LRG_TONEMAP_OPERATOR_REINHARD, 1.0f, 0.1f), 0.0f, 1.0f);

	/* in == out, NaN counts as 0, white point 0 is clamped to 0.1 */
	lrg_tonemap_operator_apply_rgb (LRG_TONEMAP_OPERATOR_REINHARD, 0.0f, v, v);
	g_assert_cmpfloat (v[0], ==, 0.0f);
	g_assert_cmpfloat_with_epsilon (v[1], want1, EPS);
	g_assert_true (isfinite (v[2]) && v[2] >= 0.0f && v[2] <= 1.0f);
}

/* to_image is exposure, the helper and an sRGB encode, per operator */
static void
test_hdr_canvas_to_image_operators (void)
{
	static const gfloat colours[][4] = {
		{ 0.0f, 0.0f, 0.0f, 1.0f }, { 0.18f, 0.18f, 0.18f, 1.0f },
		{ 1.0f, 0.5f, 0.25f, 1.0f }, { 8.0f, 3.0f, 0.02f, 1.0f },
		{ 40.0f, 0.7f, 2.5f, 1.0f }, { 2.0f, 1.0f, 0.5f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.0f },
	};
	const gfloat exposure = 0.5f;
	gsize        o, i;
	gint         c;

	for (o = 0; o < G_N_ELEMENTS (all_ops); o++)
	{
		g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (G_N_ELEMENTS (colours), 1);
		g_autoptr(GrlImage)     img = NULL;
		const guint8           *p;

		for (i = 0; i < G_N_ELEMENTS (colours); i++)
			lrg_hdr_canvas_set_pixel (canvas, (gint) i, 0, colours[i][0], colours[i][1],
			                          colours[i][2], colours[i][3]);
		img = lrg_hdr_canvas_to_image (canvas, all_ops[o], exposure, FALSE);
		g_assert_nonnull (img);
		g_assert_cmpint (grl_image_get_width (img), ==, (gint) G_N_ELEMENTS (colours));
		p = grl_image_get_pixels (img, NULL);

		for (i = 0; i < G_N_ELEMENTS (colours); i++)
		{
			gfloat in[3], mapped[3];

			if (colours[i][3] == 0.0f)
			{
				/* transparent stays transparent black */
				for (c = 0; c < 4; c++)
					g_assert_cmpint (p[i * 4 + c], ==, 0);
				continue;
			}
			/* the straight colour (exposed) goes through the helper */
			for (c = 0; c < 3; c++)
				in[c] = colours[i][c] * exp2f (exposure);
			lrg_tonemap_operator_apply_rgb (all_ops[o], LRG_HDR_CANVAS_WHITE_POINT, in, mapped);
			for (c = 0; c < 3; c++)
				g_assert_cmpint (ABS ((gint) p[i * 4 + c] - ref_encode (mapped[c])), <=, 0);
			g_assert_cmpint (p[i * 4 + 3], ==, (gint) floor (colours[i][3] * 255.0 + 0.5));
		}
	}
}

/* Dither moves every value by at most one step, really moves some,
 * leaves alpha alone and is deterministic */
static void
test_hdr_canvas_dither (void)
{
	g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new (256, 8);
	g_autoptr(GrlImage)     plain = NULL;
	g_autoptr(GrlImage)     dith = NULL;
	g_autoptr(GrlImage)     dith2 = NULL;
	const guint8           *a, *b, *b2;
	gsize                   i, n, moved = 0;
	gint                    x, y;

	/* a slow, dark gradient: the case that bands */
	for (y = 0; y < 8; y++)
		for (x = 0; x < 256; x++)
			lrg_hdr_canvas_set_pixel (canvas, x, y, x / 2048.0f, x / 4096.0f, 0.01f + y / 1000.0f,
			                          0.5f + x / 512.0f);

	plain = lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 1.0f, FALSE);
	dith = lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 1.0f, TRUE);
	dith2 = lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 1.0f, TRUE);
	a = grl_image_get_pixels (plain, NULL);
	b = grl_image_get_pixels (dith, NULL);
	b2 = grl_image_get_pixels (dith2, NULL);
	n = 256 * 8;
	for (i = 0; i < n; i++)
	{
		gint c;

		for (c = 0; c < 3; c++)
		{
			gint d = (gint) b[i * 4 + c] - (gint) a[i * 4 + c];

			g_assert_cmpint (ABS (d), <=, 1);
			if (d != 0)
				moved++;
		}
		g_assert_cmpint (b[i * 4 + 3], ==, a[i * 4 + 3]);
	}
	g_assert_cmpuint (moved, >, n / 10);
	g_assert_cmpmem (b, n * 4, b2, n * 4);
}

/* LrgHdrImage round trip: opaque exactly, translucent over black */
static void
test_hdr_canvas_hdr_image (void)
{
	static const gfloat rgb[] = {
		0.0f, 1.0f, 2.0f,   100.0f, 0.5f, 0.001f,
		3.25f, 3.5f, 3.75f,  7.0f, 8.0f, 9.0f,
	};
	g_autoptr(LrgHdrImage)  src = lrg_hdr_image_new_from_data (2, 2, rgb);
	g_autoptr(LrgHdrCanvas) canvas = lrg_hdr_canvas_new_from_hdr_image (src);
	g_autoptr(LrgHdrImage)  back = NULL;
	g_autoptr(LrgHdrImage)  half = NULL;
	const gfloat           *d;
	gfloat                  px[4];
	gsize                   n = 0, i;

	g_assert_cmpuint (lrg_hdr_canvas_get_width (canvas), ==, 2);
	g_assert_cmpuint (lrg_hdr_canvas_get_height (canvas), ==, 2);
	lrg_hdr_canvas_get_pixel (canvas, 1, 0, px);
	g_assert_cmpfloat (px[0], ==, 100.0f);
	g_assert_cmpfloat (px[3], ==, 1.0f);

	back = lrg_hdr_canvas_to_hdr_image (canvas);
	d = lrg_hdr_image_get_data (back, &n);
	g_assert_cmpuint (n, ==, G_N_ELEMENTS (rgb));
	for (i = 0; i < n; i++)
		g_assert_cmpfloat (d[i], ==, rgb[i]);

	/* translucent pixels export composited over black */
	lrg_hdr_canvas_set_pixel (canvas, 0, 0, 4.0f, 2.0f, 1.0f, 0.5f);
	half = lrg_hdr_canvas_to_hdr_image (canvas);
	d = lrg_hdr_image_get_data (half, NULL);
	g_assert_cmpfloat_with_epsilon (d[0], 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (d[1], 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (d[2], 0.5f, EPS);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/hdr-canvas/new", test_hdr_canvas_new);
	g_test_add_func ("/hdr-canvas/new-invalid", test_hdr_canvas_new_invalid);
	g_test_add_func ("/hdr-canvas/premultiplied", test_hdr_canvas_premultiplied);
	g_test_add_func ("/hdr-canvas/blend-over", test_hdr_canvas_blend_over);
	g_test_add_func ("/hdr-canvas/add", test_hdr_canvas_add);
	g_test_add_func ("/hdr-canvas/shade-coverage", test_hdr_canvas_shade_coverage);
	g_test_add_func ("/hdr-canvas/tonemap-helper-matches-old", test_tonemap_helper_matches_old);
	g_test_add_func ("/hdr-canvas/tonemap-helper-edges", test_tonemap_helper_edges);
	g_test_add_func ("/hdr-canvas/to-image-operators", test_hdr_canvas_to_image_operators);
	g_test_add_func ("/hdr-canvas/dither", test_hdr_canvas_dither);
	g_test_add_func ("/hdr-canvas/hdr-image-round-trip", test_hdr_canvas_hdr_image);

	return g_test_run ();
}
