/* test-post-cpu.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the CPU path of the post-processing stack
 * (LrgPostEffectClass.apply_cpu, lrg_post_effect_apply_cpu(),
 * lrg_post_processor_apply_to_buffer() / _apply_to_image()) and LrgBlur.
 *
 * Headless (no window, no GL context): the class layout; which effects
 * have a CPU path; the wrapper's enabled / unsupported handling; every
 * effect against an independent reference on small known images
 * (tonemap and colour grade, LUT, colour-blind filter, palettize and
 * sharpen against their per-pixel references; vignette against its
 * formula; blur against a direct 2D convolution, the mean, the variance
 * and the identity; FXAA on flat images and a hard diagonal; grain
 * determinism and statistics; bloom below the threshold, the spread and
 * the energy of a bright pixel); the processor's order, skipping of
 * disabled effects and NOT_SUPPORTED error; the RGBA8 round trip.
 *
 * GPU (only under the isolated X display, tests/xrun.sh style: DISPLAY
 * set and no WAYLAND_DISPLAY): every effect's GPU result against its CPU
 * result on the same image, within a tolerance.
 */

#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static gboolean gpu_ready = FALSE;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* a deterministic hash in [0, 1) for test images */
static gfloat
hash01 (guint x,
        guint y,
        guint k)
{
	guint32 h = (guint32) x * 73856093u ^ (guint32) y * 19349663u ^ (guint32) k * 83492791u;

	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (gfloat) (h & 0xffffu) / 65536.0f;
}

static gfloat *
buffer_new (guint w,
            guint h)
{
	return g_new0 (gfloat, (gsize) w * h * 4);
}

/* fills a buffer with one colour, alpha @a */
static void
buffer_fill (gfloat *b,
             guint   w,
             guint   h,
             gfloat  r,
             gfloat  g,
             gfloat  bl,
             gfloat  a)
{
	gsize i;

	for (i = 0; i < (gsize) w * h; i++)
	{
		b[i * 4 + 0] = r;
		b[i * 4 + 1] = g;
		b[i * 4 + 2] = bl;
		b[i * 4 + 3] = a;
	}
}

/* a varied image: gradients, noise, a bright HDR block, varying alpha */
static void
buffer_pattern (gfloat *b,
                guint   w,
                guint   h)
{
	guint x, y;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gfloat *p = b + ((gsize) y * w + x) * 4;

			p[0] = (gfloat) x / (gfloat) MAX (w - 1, 1u);
			p[1] = (gfloat) y / (gfloat) MAX (h - 1, 1u);
			p[2] = hash01 (x, y, 2);
			p[3] = 0.25f + 0.75f * hash01 (x, y, 3);
			if (x > w / 3 && x < w / 2 && y > h / 3 && y < h / 2)
				p[0] = p[1] = p[2] = 2.5f;
		}
}

static gfloat *
pixel (gfloat *b,
       guint   w,
       guint   x,
       guint   y)
{
	return b + ((gsize) y * w + x) * 4;
}

static gfloat
max_abs_diff (const gfloat *a,
              const gfloat *b,
              gsize         n)
{
	gfloat m = 0.0f;
	gsize  i;

	for (i = 0; i < n; i++)
		m = MAX (m, fabsf (a[i] - b[i]));
	return m;
}

static void
assert_alpha_kept (const gfloat *before,
                   const gfloat *after,
                   guint         w,
                   guint         h)
{
	gsize i;

	for (i = 0; i < (gsize) w * h; i++)
		g_assert_cmpfloat (before[i * 4 + 3], ==, after[i * 4 + 3]);
}

/* ==========================================================================
 * The class and the wrapper
 * ========================================================================== */

static void
test_class_layout (void)
{
	/* apply_cpu took one reserved slot: the class size is unchanged
	 * (five vfuncs + the new one + seven reserved pointers) */
	g_assert_cmpuint (sizeof (LrgPostEffectClass), ==, sizeof (GObjectClass) + 13 * sizeof (gpointer));
	g_assert_false (IsWindowReady ());
}

static void
test_has_cpu (void)
{
	GType with[] = {
		LRG_TYPE_BLOOM, LRG_TYPE_BLUR, LRG_TYPE_COLOR_GRADE, LRG_TYPE_COLORBLIND_FILTER,
		LRG_TYPE_FILM_GRAIN, LRG_TYPE_FXAA, LRG_TYPE_PALETTIZE, LRG_TYPE_SHARPEN,
		LRG_TYPE_TONEMAP, LRG_TYPE_VIGNETTE
	};
	GType without[] = { LRG_TYPE_SCREEN_SHAKE, LRG_TYPE_HEAT_HAZE };
	guint i;

	for (i = 0; i < G_N_ELEMENTS (with); i++)
	{
		g_autoptr(LrgPostEffect) e = g_object_new (with[i], NULL);

		g_assert_true (lrg_post_effect_has_cpu (e));
	}
	for (i = 0; i < G_N_ELEMENTS (without); i++)
	{
		g_autoptr(LrgPostEffect) e = g_object_new (without[i], NULL);
		gfloat                   px[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

		g_assert_false (lrg_post_effect_has_cpu (e));
		g_assert_false (lrg_post_effect_apply_cpu (e, px, 1, 1, 0.0f));
		g_assert_cmpfloat (px[0], ==, 0.5f);
	}
	/* the registry knows the new effect */
	g_assert_true (lrg_post_stack_lookup_effect_type ("blur") == LRG_TYPE_BLUR);
}

static void
test_wrapper_enabled_intensity (void)
{
	g_autoptr(LrgVignette) v = lrg_vignette_new ();
	g_autofree gfloat     *b = buffer_new (9, 9);
	g_autofree gfloat     *ref = buffer_new (9, 9);

	buffer_fill (b, 9, 9, 0.8f, 0.8f, 0.8f, 1.0f);
	memcpy (ref, b, 9 * 9 * 4 * sizeof (gfloat));
	lrg_vignette_set_intensity (v, 1.0f);

	/* disabled: FALSE and untouched, as the GPU chain skips it */
	lrg_post_effect_set_enabled (LRG_POST_EFFECT (v), FALSE);
	g_assert_false (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (v), b, 9, 9, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, ref, 9 * 9 * 4), ==, 0.0f);

	/* enabled at intensity 0: runs, changes nothing */
	lrg_post_effect_set_enabled (LRG_POST_EFFECT (v), TRUE);
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (v), 0.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (v), b, 9, 9, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, ref, 9 * 9 * 4), ==, 0.0f);

	/* no initialisation needed (there are no GPU resources) */
	g_assert_false (lrg_post_effect_is_initialized (LRG_POST_EFFECT (v)));
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (v), 1.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (v), b, 9, 9, 0.0f));
	g_assert_cmpfloat (pixel (b, 9, 0, 0)[0], <, 0.8f);
}

/* ==========================================================================
 * Per-pixel effects against their references
 * ========================================================================== */

static void
test_tonemap (void)
{
	LrgTonemapOperator ops[] = {
		LRG_TONEMAP_OPERATOR_NONE, LRG_TONEMAP_OPERATOR_REINHARD,
		LRG_TONEMAP_OPERATOR_FILMIC, LRG_TONEMAP_OPERATOR_ACES
	};
	guint w = 17, h = 11, o;

	for (o = 0; o < G_N_ELEMENTS (ops); o++)
	{
		g_autoptr(LrgTonemap) tm = lrg_tonemap_new ();
		g_autofree gfloat    *b = buffer_new (w, h);
		g_autofree gfloat    *src = buffer_new (w, h);
		gsize                 i;

		lrg_tonemap_set_operator (tm, ops[o]);
		lrg_tonemap_set_exposure (tm, 0.5f);
		lrg_tonemap_set_linear_input (tm, o % 2 == 1);
		lrg_post_effect_set_intensity (LRG_POST_EFFECT (tm), o == 3 ? 0.6f : 1.0f);
		buffer_pattern (b, w, h);
		memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
		g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (tm), b, w, h, 0.0f));
		for (i = 0; i < (gsize) w * h; i++)
		{
			gfloat expect[3];

			lrg_tonemap_apply_rgb (tm, src + i * 4, expect);
			g_assert_cmpfloat (b[i * 4 + 0], ==, expect[0]);
			g_assert_cmpfloat (b[i * 4 + 1], ==, expect[1]);
			g_assert_cmpfloat (b[i * 4 + 2], ==, expect[2]);
		}
		assert_alpha_kept (src, b, w, h);
	}
}

static void
test_color_grade_and_lut (void)
{
	g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();
	g_autoptr(LrgColorLut)   lut = NULL;
	g_autofree gfloat       *table = g_new (gfloat, 8 * 8 * 8 * 3);
	g_autofree gfloat       *b = buffer_new (13, 9);
	g_autofree gfloat       *src = buffer_new (13, 9);
	guint                    r, g, bl;
	gsize                    i;

	/* identity grade: nothing happens */
	buffer_pattern (b, 13, 9);
	memcpy (src, b, 13 * 9 * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grade), b, 13, 9, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, src, 13 * 9 * 4), ==, 0.0f);

	/* a grade plus a channel-swizzling table */
	for (bl = 0; bl < 8; bl++)
		for (g = 0; g < 8; g++)
			for (r = 0; r < 8; r++)
			{
				gfloat *e = table + ((gsize) r + g * 8 + bl * 64) * 3;

				e[0] = (gfloat) g / 7.0f;
				e[1] = (gfloat) bl / 7.0f;
				e[2] = 1.0f - (gfloat) r / 7.0f;
			}
	lut = lrg_color_lut_new_from_data (8, table, "swizzle");
	lrg_color_grade_set_exposure (grade, 0.3f);
	lrg_color_grade_set_contrast (grade, 1.2f);
	lrg_color_grade_set_saturation (grade, 0.8f);
	lrg_color_grade_set_temperature (grade, 0.4f);
	lrg_color_grade_set_lift (grade, 0.02f, 0.0f, -0.01f);
	lrg_color_grade_set_lut (grade, lut, 0.7f);
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (grade), 0.9f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grade), b, 13, 9, 0.0f));
	for (i = 0; i < 13 * 9; i++)
	{
		gfloat expect[3];

		lrg_color_grade_apply_rgb (grade, src + i * 4, expect);
		g_assert_cmpfloat (b[i * 4 + 0], ==, expect[0]);
		g_assert_cmpfloat (b[i * 4 + 1], ==, expect[1]);
		g_assert_cmpfloat (b[i * 4 + 2], ==, expect[2]);
	}
	assert_alpha_kept (src, b, 13, 9);

	/* the table's own reference: a LUT-only grade at strength 1 is the
	 * clamped input looked up */
	{
		g_autoptr(LrgColorGrade) only = lrg_color_grade_new ();
		gfloat                   px[4] = { 0.3f, 0.6f, 0.9f, 1.0f };
		gfloat                   expect[3];

		lrg_color_grade_set_lut (only, lut, 1.0f);
		lrg_color_lut_apply_rgb (lut, px, 1.0f, expect);
		g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (only), px, 1, 1, 0.0f));
		g_assert_cmpfloat_with_epsilon (px[0], expect[0], 1e-6f);
		g_assert_cmpfloat_with_epsilon (px[1], expect[1], 1e-6f);
		g_assert_cmpfloat_with_epsilon (px[2], expect[2], 1e-6f);
		/* the swizzle itself: red out = green in */
		g_assert_cmpfloat_with_epsilon (px[0], 0.6f, 1e-5f);
	}
}

static void
test_colorblind (void)
{
	g_autoptr(LrgColorblindFilter) f = lrg_colorblind_filter_new_with_type (LRG_COLORBLIND_DEUTERANOPIA);
	g_autofree gfloat             *b = buffer_new (11, 7);
	g_autofree gfloat             *src = buffer_new (11, 7);
	guint                          mode;
	gsize                          i;

	for (mode = 0; mode < 2; mode++)
	{
		lrg_colorblind_filter_set_mode (f, mode == 0 ? LRG_COLORBLIND_MODE_SIMULATE : LRG_COLORBLIND_MODE_CORRECT);
		lrg_colorblind_filter_set_strength (f, 0.8f);
		buffer_pattern (b, 11, 7);
		memcpy (src, b, 11 * 7 * 4 * sizeof (gfloat));
		g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (f), b, 11, 7, 0.0f));
		g_assert_cmpfloat (max_abs_diff (b, src, 11 * 7 * 4), >, 0.01f);
		for (i = 0; i < 11 * 7; i++)
		{
			gfloat expect[3];

			lrg_colorblind_filter_apply_rgb (f, src + i * 4, expect);
			g_assert_cmpfloat (b[i * 4 + 0], ==, expect[0]);
			g_assert_cmpfloat (b[i * 4 + 1], ==, expect[1]);
			g_assert_cmpfloat (b[i * 4 + 2], ==, expect[2]);
		}
		assert_alpha_kept (src, b, 11, 7);
	}
}

static void
test_palettize (void)
{
	static const guint8      pal[] = { 0, 0, 0, 255, 255, 255, 200, 40, 40, 30, 90, 200 };
	g_autoptr(LrgPalettize)  p = lrg_palettize_new ();
	g_autoptr(GBytes)        bytes = g_bytes_new_static (pal, sizeof pal);
	g_autoptr(GError)        error = NULL;
	g_autofree gfloat       *b = buffer_new (16, 12);
	g_autofree gfloat       *src = buffer_new (16, 12);
	guint                    x, y;

	g_assert_true (lrg_palettize_set_palette (p, bytes, &error));
	g_assert_no_error (error);
	lrg_palettize_set_dither (p, TRUE);
	lrg_palettize_set_dither_strength (p, 0.3f);
	buffer_pattern (b, 16, 12);
	memcpy (src, b, 16 * 12 * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (p), b, 16, 12, 0.0f));

	/* the dither cell follows gl_FragCoord: rows count from the bottom */
	for (y = 0; y < 12; y++)
		for (x = 0; x < 16; x++)
		{
			gfloat expect[3];

			lrg_palettize_apply_rgb (p, x, 12 - 1 - y, pixel (src, 16, x, y), expect);
			g_assert_cmpfloat (pixel (b, 16, x, y)[0], ==, expect[0]);
			g_assert_cmpfloat (pixel (b, 16, x, y)[1], ==, expect[1]);
			g_assert_cmpfloat (pixel (b, 16, x, y)[2], ==, expect[2]);
		}
	assert_alpha_kept (src, b, 16, 12);
}

static void
test_sharpen (void)
{
	g_autoptr(LrgSharpen) s = lrg_sharpen_new ();
	g_autofree gfloat    *b = buffer_new (10, 8);
	g_autofree gfloat    *src = buffer_new (10, 8);
	gint                  x, y;

	lrg_sharpen_set_sharpness (s, 0.8f);
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (s), 0.75f);
	buffer_pattern (b, 10, 8);
	/* keep the pattern in 0..1 so the adaptive amplitude is non-zero */
	for (x = 0; x < 10 * 8 * 4; x++)
		if (x % 4 != 3)
			b[x] = MIN (b[x], 0.9f);
	memcpy (src, b, 10 * 8 * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (s), b, 10, 8, 0.0f));
	for (y = 0; y < 8; y++)
		for (x = 0; x < 10; x++)
		{
			gfloat *c = pixel (src, 10, (guint) x, (guint) y);
			gfloat  sh[3];
			guint   k;

			/* neighbours clamped to the edge, as the GL sampler */
			lrg_sharpen_pixel (0.8f, c,
			                   pixel (src, 10, (guint) x, (guint) MAX (y - 1, 0)),
			                   pixel (src, 10, (guint) x, (guint) MIN (y + 1, 7)),
			                   pixel (src, 10, (guint) MAX (x - 1, 0), (guint) y),
			                   pixel (src, 10, (guint) MIN (x + 1, 9), (guint) y), sh);
			for (k = 0; k < 3; k++)
				g_assert_cmpfloat_with_epsilon (pixel (b, 10, (guint) x, (guint) y)[k],
				                                c[k] + (sh[k] - c[k]) * 0.75f, 1e-6f);
		}
	assert_alpha_kept (src, b, 10, 8);
}

/* the vignette's formula, in double, written out independently */
static gdouble
vignette_factor (guint   x,
                 guint   y,
                 guint   w,
                 guint   h,
                 gdouble strength,
                 gdouble radius,
                 gdouble smooth,
                 gdouble round)
{
	gdouble dx = ((gdouble) x + 0.5) / w - 0.5;
	gdouble dy = ((gdouble) y + 0.5) / h - 0.5;
	gdouble aspect = (gdouble) w / h;
	gdouble dist, t;

	dx *= 1.0 + (aspect - 1.0) * round;
	dist = sqrt (dx * dx + dy * dy) * 1.41421;
	t = (dist - radius) / MAX (smooth, 0.001);
	t = CLAMP (t, 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t) * strength;
}

static void
test_vignette (void)
{
	g_autoptr(LrgVignette) v = lrg_vignette_new ();
	guint                  w = 33, h = 21, x, y;
	g_autofree gfloat     *b = buffer_new (w, h);
	g_autofree gfloat     *src = buffer_new (w, h);

	lrg_vignette_set_intensity (v, 0.8f);
	lrg_vignette_set_radius (v, 0.3f);
	lrg_vignette_set_smoothness (v, 0.4f);
	lrg_vignette_set_roundness (v, 0.5f);
	lrg_vignette_set_color (v, 0.1f, 0.0f, 0.2f);
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (v), 0.5f);
	buffer_fill (b, w, h, 0.7f, 0.6f, 0.5f, 0.75f);
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (v), b, w, h, 0.0f));

	/* the centre pixel (odd size: exactly at the centre) is unchanged */
	g_assert_cmpfloat (pixel (b, w, 16, 10)[0], ==, 0.7f);
	g_assert_cmpfloat (pixel (b, w, 16, 10)[2], ==, 0.5f);

	/* every pixel follows mix (c, color, v) with v from the formula */
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gdouble f = vignette_factor (x, y, w, h, 0.8 * 0.5, 0.3, 0.4, 0.5);

			g_assert_cmpfloat_with_epsilon (pixel (b, w, x, y)[0], 0.7 + (0.1 - 0.7) * f, 1e-5);
			g_assert_cmpfloat_with_epsilon (pixel (b, w, x, y)[1], 0.6 + (0.0 - 0.6) * f, 1e-5);
			g_assert_cmpfloat_with_epsilon (pixel (b, w, x, y)[2], 0.5 + (0.2 - 0.5) * f, 1e-5);
		}

	/* the corners are darker, and darker the further out */
	g_assert_cmpfloat (pixel (b, w, 0, 0)[0], <, 0.6f);
	g_assert_cmpfloat (pixel (b, w, w - 1, h - 1)[0], <, 0.6f);
	g_assert_cmpfloat (pixel (b, w, 0, 0)[0], <, pixel (b, w, 8, 5)[0]);
	assert_alpha_kept (src, b, w, h);
}

/* ==========================================================================
 * Blur
 * ========================================================================== */

static void
test_blur_kernel (void)
{
	g_autoptr(LrgBlur) blur = lrg_blur_new ();
	g_autofree gfloat *k = NULL;
	guint              n, i, r;
	gdouble            sum;

	g_assert_cmpuint (lrg_blur_get_radius (blur), ==, 4);
	for (r = 0; r <= LRG_BLUR_MAX_RADIUS; r += 4)
	{
		lrg_blur_set_radius (blur, r);
		g_clear_pointer (&k, g_free);
		k = lrg_blur_get_kernel (blur, &n);
		g_assert_cmpuint (n, ==, 2 * r + 1);
		sum = 0.0;
		for (i = 0; i < n; i++)
		{
			sum += k[i];
			g_assert_cmpfloat (k[i], ==, k[n - 1 - i]);   /* symmetric */
			if (i > 0 && i <= r)
				g_assert_cmpfloat (k[i], >=, k[i - 1]);  /* rises to the centre */
		}
		g_assert_cmpfloat_with_epsilon (sum, 1.0, 1e-5);
	}

	/* the weights are a Gaussian with sigma radius / 2 by default */
	lrg_blur_set_radius (blur, 6);
	g_clear_pointer (&k, g_free);
	k = lrg_blur_get_kernel (blur, &n);
	g_assert_cmpfloat_with_epsilon (k[6 + 3] / k[6], exp (-9.0 / (2.0 * 9.0)), 1e-5);
	lrg_blur_set_sigma (blur, 1.5f);
	g_clear_pointer (&k, g_free);
	k = lrg_blur_get_kernel (blur, &n);
	g_assert_cmpfloat_with_epsilon (k[6 + 2] / k[6], exp (-4.0 / (2.0 * 2.25)), 1e-5);

	/* radius 0: the single weight 1; out of range is clamped */
	lrg_blur_set_radius (blur, 0);
	g_clear_pointer (&k, g_free);
	k = lrg_blur_get_kernel (blur, &n);
	g_assert_cmpuint (n, ==, 1);
	g_assert_cmpfloat (k[0], ==, 1.0f);
	lrg_blur_set_radius (blur, 1000);
	g_assert_cmpuint (lrg_blur_get_radius (blur), ==, LRG_BLUR_MAX_RADIUS);
}

static void
test_blur_image (void)
{
	g_autoptr(LrgBlur) blur = lrg_blur_new ();
	guint              w = 40, h = 30, x, y, n;
	g_autofree gfloat *b = buffer_new (w, h);
	g_autofree gfloat *src = buffer_new (w, h);
	g_autofree gfloat *k = NULL;
	gdouble            sum_in = 0.0, sum_out = 0.0, var_in = 0.0, var_out = 0.0, mean;
	gint               r;

	/* radius 0 is the identity */
	lrg_blur_set_radius (blur, 0);
	buffer_pattern (b, w, h);
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (blur), b, w, h, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, src, (gsize) w * h * 4), ==, 0.0f);

	/* against a direct 2D convolution with the outer product of the
	 * kernel, taps clamped to the edge */
	lrg_blur_set_radius (blur, 3);
	lrg_blur_set_sigma (blur, 1.2f);
	k = lrg_blur_get_kernel (blur, &n);
	r = 3;
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (blur), b, w, h, 0.0f));
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gdouble acc[3] = { 0.0, 0.0, 0.0 };
			gint    i, j;
			guint   c;

			for (j = -r; j <= r; j++)
				for (i = -r; i <= r; i++)
				{
					gint sx = CLAMP ((gint) x + i, 0, (gint) w - 1);
					gint sy = CLAMP ((gint) y + j, 0, (gint) h - 1);

					for (c = 0; c < 3; c++)
						acc[c] += (gdouble) k[i + r] * k[j + r] * pixel (src, w, (guint) sx, (guint) sy)[c];
				}
			for (c = 0; c < 3; c++)
				g_assert_cmpfloat_with_epsilon (pixel (b, w, x, y)[c], acc[c], 1e-5);
		}
	assert_alpha_kept (src, b, w, h);

	/* the mean is kept (the mass stays away from the edges) and the
	 * variance drops: random dots in the middle of a black frame */
	buffer_fill (b, w, h, 0.0f, 0.0f, 0.0f, 1.0f);
	for (y = 8; y < h - 8; y++)
		for (x = 8; x < w - 8; x++)
			pixel (b, w, x, y)[1] = hash01 (x, y, 9) > 0.7f ? 1.0f : 0.0f;
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	lrg_blur_set_radius (blur, 4);
	lrg_blur_set_sigma (blur, 0.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (blur), b, w, h, 0.0f));
	for (x = 0; x < w * h; x++)
	{
		sum_in += src[x * 4 + 1];
		sum_out += b[x * 4 + 1];
	}
	g_assert_cmpfloat_with_epsilon (sum_out, sum_in, 1e-3);
	mean = sum_in / (w * h);
	for (x = 0; x < w * h; x++)
	{
		var_in += (src[x * 4 + 1] - mean) * (src[x * 4 + 1] - mean);
		var_out += (b[x * 4 + 1] - mean) * (b[x * 4 + 1] - mean);
	}
	g_assert_cmpfloat (var_out, <, var_in * 0.5);

	/* the base intensity mixes the blur with the source */
	{
		g_autofree gfloat *full = g_memdup2 (b, (gsize) w * h * 4 * sizeof (gfloat));
		gsize              i;

		memcpy (b, src, (gsize) w * h * 4 * sizeof (gfloat));
		lrg_post_effect_set_intensity (LRG_POST_EFFECT (blur), 0.25f);
		g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (blur), b, w, h, 0.0f));
		for (i = 0; i < (gsize) w * h * 4; i += 4)
			g_assert_cmpfloat_with_epsilon (b[i + 1], src[i + 1] + (full[i + 1] - src[i + 1]) * 0.25f, 1e-6);
	}
}

static void
test_blur_in_stack (void)
{
	static const gchar          rpost[] =
		"version: 1\n"
		"effects:\n"
		"  - type: blur\n"
		"    weight: 0.8\n"
		"    params: { radius: 6, sigma: 2.5 }\n";
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgPostStack)     stack = lrg_post_stack_new_from_data (rpost, -1, NULL, &error);
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (8, 8);
	LrgPostEffect              *e;

	g_assert_no_error (error);
	g_assert_true (lrg_post_stack_build (stack, proc, &error));
	g_assert_no_error (error);
	e = lrg_post_processor_get_effect (proc, "blur");
	g_assert_nonnull (e);
	g_assert_true (LRG_IS_BLUR (e));
	g_assert_cmpuint (lrg_blur_get_radius (LRG_BLUR (e)), ==, 6);
	g_assert_cmpfloat_with_epsilon (lrg_blur_get_sigma (LRG_BLUR (e)), 2.5f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_post_effect_get_intensity (e), 0.8f, 1e-6f);
}

/* ==========================================================================
 * FXAA
 * ========================================================================== */

static void
test_fxaa (void)
{
	g_autoptr(LrgFxaa) fxaa = lrg_fxaa_new ();
	guint              w = 24, h = 24, x, y, between = 0;
	g_autofree gfloat *b = buffer_new (w, h);
	g_autofree gfloat *src = buffer_new (w, h);
	gdouble            jag_in = 0.0, jag_out = 0.0;

	/* a flat image is left alone */
	buffer_fill (b, w, h, 0.4f, 0.5f, 0.6f, 1.0f);
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (fxaa), b, w, h, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, src, (gsize) w * h * 4), ==, 0.0f);

	/* a hard, aliased edge at a shallow slope (one step every 3 rows) */
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gfloat v = (x * 3 > y + 20) ? 1.0f : 0.0f;
			gfloat *p = pixel (b, w, x, y);

			p[0] = p[1] = p[2] = v;
			p[3] = 1.0f;
		}
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (fxaa), b, w, h, 0.0f));
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gfloat v = pixel (b, w, x, y)[1];

			g_assert_cmpfloat (v, >=, 0.0f);
			g_assert_cmpfloat (v, <=, 1.0f);
			if (v > 0.02f && v < 0.98f)
				between++;
			/* far from the edge nothing changes */
			if (abs ((gint) x * 3 - (gint) y - 20) > 9)
				g_assert_cmpfloat (v, ==, pixel (src, w, x, y)[1]);
		}
	g_assert_cmpuint (between, >, h / 2);

	/* softer: the vertical jumps along the edge shrink */
	for (y = 1; y < h; y++)
		for (x = 0; x < w; x++)
		{
			gdouble a = pixel (src, w, x, y)[1] - pixel (src, w, x, y - 1)[1];
			gdouble c = pixel (b, w, x, y)[1] - pixel (b, w, x, y - 1)[1];

			jag_in += a * a;
			jag_out += c * c;
		}
	g_assert_cmpfloat (jag_out, <, jag_in);

	/* intensity 0: identity */
	memcpy (b, src, (gsize) w * h * 4 * sizeof (gfloat));
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (fxaa), 0.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (fxaa), b, w, h, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, src, (gsize) w * h * 4), ==, 0.0f);
}

/* ==========================================================================
 * Film grain
 * ========================================================================== */

static void
test_grain (void)
{
	g_autoptr(LrgFilmGrain) grain = lrg_film_grain_new ();
	guint                   w = 128, h = 128;
	g_autofree gfloat      *a = buffer_new (w, h);
	g_autofree gfloat      *b = buffer_new (w, h);
	g_autofree gfloat      *c = buffer_new (w, h);
	gdouble                 sum = 0.0, sq = 0.0, mean, sd, amount;
	gsize                   i, n = (gsize) w * h;

	lrg_film_grain_set_size (grain, 1.0f);
	lrg_film_grain_set_intensity (grain, 0.2f);
	lrg_film_grain_set_luminance_response (grain, 0.5f);
	buffer_fill (a, w, h, 0.5f, 0.5f, 0.5f, 0.6f);
	buffer_fill (b, w, h, 0.5f, 0.5f, 0.5f, 0.6f);
	buffer_fill (c, w, h, 0.5f, 0.5f, 0.5f, 0.6f);

	/* the same time: the same grain; another grain frame: other grain */
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grain), a, w, h, 2.0f));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grain), b, w, h, 2.0f));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grain), c, w, h, 2.5f));
	g_assert_cmpfloat (max_abs_diff (a, b, n * 4), ==, 0.0f);
	g_assert_cmpfloat (max_abs_diff (a, c, n * 4), >, 0.01f);
	for (i = 0; i < n; i++)
		g_assert_cmpfloat (a[i * 4 + 3], ==, 0.6f);

	/* monochrome: the same offset on every channel; statistics of the
	 * offset: uniform in +/- amount / 2, amount = intensity * (1 - luma *
	 * response), so mean 0 and sd amount / sqrt (12) */
	amount = 0.2 * (1.0 - 0.5 * 0.5);
	for (i = 0; i < n; i++)
	{
		gdouble d = a[i * 4] - 0.5;

		g_assert_cmpfloat (a[i * 4], ==, a[i * 4 + 1]);
		g_assert_cmpfloat (fabs (d), <=, amount * 0.5 + 1e-6);
		sum += d;
		sq += d * d;
	}
	mean = sum / n;
	sd = sqrt (sq / n - mean * mean);
	g_assert_cmpfloat (fabs (mean), <, 5.0 * (amount / sqrt (12.0)) / sqrt ((gdouble) n));
	g_assert_cmpfloat_with_epsilon (sd, amount / sqrt (12.0), amount / sqrt (12.0) * 0.05);

	/* coloured grain: the channels differ */
	lrg_film_grain_set_colored (grain, TRUE);
	buffer_fill (a, w, h, 0.5f, 0.5f, 0.5f, 1.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grain), a, w, h, 2.0f));
	g_assert_cmpfloat (a[0], !=, a[1]);

	/* intensity 0: identity */
	buffer_fill (a, w, h, 0.5f, 0.5f, 0.5f, 1.0f);
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (grain), 0.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (grain), a, w, h, 2.0f));
	g_assert_cmpfloat (a[0], ==, 0.5f);
}

/* ==========================================================================
 * Bloom
 * ========================================================================== */

static void
test_bloom (void)
{
	g_autoptr(LrgBloom) bloom = lrg_bloom_new ();
	guint               w = 64, h = 64, x, y;
	g_autofree gfloat  *b = buffer_new (w, h);
	g_autofree gfloat  *src = buffer_new (w, h);
	gdouble             added = 0.0;
	gfloat              tiny[3 * 3 * 4];
	gfloat              tiny_src[3 * 3 * 4];

	/* below the threshold (and its soft knee) nothing changes */
	lrg_bloom_set_threshold (bloom, 0.8f);
	lrg_bloom_set_soft_knee (bloom, 0.5f);
	buffer_pattern (b, w, h);
	for (x = 0; x < w * h * 4; x++)
		if (x % 4 != 3)
			b[x] = MIN (b[x], 0.39f);
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (bloom), b, w, h, 0.0f));
	g_assert_cmpfloat (max_abs_diff (b, src, (gsize) w * h * 4), ==, 0.0f);

	/* one bright pixel spreads into a glow around it */
	buffer_fill (b, w, h, 0.0f, 0.0f, 0.0f, 1.0f);
	pixel (b, w, 32, 32)[0] = pixel (b, w, 32, 32)[1] = pixel (b, w, 32, 32)[2] = 4.0f;
	memcpy (src, b, (gsize) w * h * 4 * sizeof (gfloat));
	lrg_bloom_set_intensity (bloom, 1.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (bloom), b, w, h, 0.0f));
	g_assert_cmpfloat (pixel (b, w, 32, 32)[0], >, 4.0f);
	g_assert_cmpfloat (pixel (b, w, 36, 32)[0], >, 0.001f);
	g_assert_cmpfloat (pixel (b, w, 28, 32)[0], >, 0.001f);
	g_assert_cmpfloat (pixel (b, w, 32, 37)[1], >, 0.001f);
	g_assert_cmpfloat (pixel (b, w, 32, 35)[0], >, pixel (b, w, 32, 40)[0]);
	g_assert_cmpfloat (pixel (b, w, 0, 0)[0], <, 1e-6f);

	/* energy: each level keeps the mass of the prefiltered level 0 and
	 * the composite divides by the level count, so the light added is
	 * the prefiltered mass in full-resolution pixels. The four prefilter
	 * taps average the 2x2 block holding the pixel (1.0); the soft knee
	 * keeps max (clamp (1 - 0.8 + 0.4, 0, 0.8)^2 / 1.6, 1 - 0.8) = 0.225
	 * of it; one level-0 texel is 4 pixels: 0.9 per channel. */
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			added += pixel (b, w, x, y)[0] - pixel (src, w, x, y)[0];
	g_assert_cmpfloat_with_epsilon (added, 0.9, 0.9 * 0.01);
	assert_alpha_kept (src, b, w, h);

	/* the tint and the effect's own intensity scale the glow */
	memcpy (b, src, (gsize) w * h * 4 * sizeof (gfloat));
	lrg_bloom_set_tint (bloom, 1.0f, 0.5f, 0.0f);
	lrg_bloom_set_intensity (bloom, 2.0f);
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (bloom), b, w, h, 0.0f));
	g_assert_cmpfloat_with_epsilon (pixel (b, w, 36, 32)[1] * 2.0f, pixel (b, w, 36, 32)[0], 1e-6f);
	g_assert_cmpfloat (pixel (b, w, 36, 32)[2], ==, 0.0f);

	/* an image too small for a single level is left alone */
	for (x = 0; x < 36; x++)
		tiny[x] = tiny_src[x] = 3.0f;
	g_assert_true (lrg_post_effect_apply_cpu (LRG_POST_EFFECT (bloom), tiny, 3, 3, 0.0f));
	g_assert_cmpfloat (max_abs_diff (tiny, tiny_src, 36), ==, 0.0f);
}

/* ==========================================================================
 * The processor
 * ========================================================================== */

static void
test_processor_chain (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (8, 8);
	g_autoptr(LrgTonemap)       tm = lrg_tonemap_new ();
	g_autoptr(LrgVignette)      vig = lrg_vignette_new ();
	g_autoptr(LrgBlur)          blur = lrg_blur_new ();
	g_autoptr(GError)           error = NULL;
	guint                       w = 20, h = 14;
	g_autofree gfloat          *b = buffer_new (w, h);
	g_autofree gfloat          *ref = buffer_new (w, h);
	g_autofree gfloat          *src = buffer_new (w, h);

	lrg_tonemap_set_operator (tm, LRG_TONEMAP_OPERATOR_ACES);
	lrg_vignette_set_intensity (vig, 1.0f);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (tm), 10);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (vig), 20);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (blur), 30);
	lrg_post_effect_set_enabled (LRG_POST_EFFECT (blur), FALSE);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (vig));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (tm));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (blur));
	buffer_pattern (src, w, h);

	/* priority order (tonemap, then vignette); the disabled blur skipped;
	 * the processor's own size (8x8) does not matter */
	memcpy (b, src, (gsize) w * h * 4 * sizeof (gfloat));
	memcpy (ref, src, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_processor_apply_to_buffer (proc, b, w, h, 0.0f, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_post_processor_get_pass_count (proc), ==, 2);
	lrg_post_effect_apply_cpu (LRG_POST_EFFECT (tm), ref, w, h, 0.0f);
	lrg_post_effect_apply_cpu (LRG_POST_EFFECT (vig), ref, w, h, 0.0f);
	g_assert_cmpfloat (max_abs_diff (b, ref, (gsize) w * h * 4), ==, 0.0f);

	/* the other order gives another image */
	lrg_post_effect_set_priority (LRG_POST_EFFECT (vig), 5);
	lrg_post_processor_sort_effects (proc);
	memcpy (b, src, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_processor_apply_to_buffer (proc, b, w, h, 0.0f, &error));
	g_assert_cmpfloat (max_abs_diff (b, ref, (gsize) w * h * 4), >, 1e-3f);

	/* a disabled processor leaves the image alone */
	lrg_post_processor_set_enabled (proc, FALSE);
	memcpy (b, src, (gsize) w * h * 4 * sizeof (gfloat));
	g_assert_true (lrg_post_processor_apply_to_buffer (proc, b, w, h, 0.0f, &error));
	g_assert_cmpfloat (max_abs_diff (b, src, (gsize) w * h * 4), ==, 0.0f);
	g_assert_cmpuint (lrg_post_processor_get_pass_count (proc), ==, 0);
}

static void
test_processor_not_supported (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (16, 16);
	g_autoptr(LrgTonemap)       tm = lrg_tonemap_new ();
	g_autoptr(LrgScreenShake)   shake = lrg_screen_shake_new ();
	g_autoptr(GError)           error = NULL;
	g_autofree gfloat          *b = buffer_new (6, 4);
	g_autofree gfloat          *src = buffer_new (6, 4);

	lrg_post_effect_set_priority (LRG_POST_EFFECT (tm), 0);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (shake), 10);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (tm));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (shake));
	buffer_pattern (b, 6, 4);
	memcpy (src, b, 6 * 4 * 4 * sizeof (gfloat));

	/* an enabled effect without a CPU path: an error naming it, and the
	 * buffer untouched (the tonemap before it did not run either) */
	g_assert_false (lrg_post_processor_apply_to_buffer (proc, b, 6, 4, 0.0f, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_nonnull (strstr (error->message, "screen-shake"));
	g_assert_cmpfloat (max_abs_diff (b, src, 6 * 4 * 4), ==, 0.0f);
	g_clear_error (&error);

	/* disabled, it is no obstacle */
	lrg_post_effect_set_enabled (LRG_POST_EFFECT (shake), FALSE);
	g_assert_true (lrg_post_processor_apply_to_buffer (proc, b, 6, 4, 0.0f, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat (max_abs_diff (b, src, 6 * 4 * 4), >, 0.0f);
}

static void
test_image_round_trip (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (16, 16);
	g_autoptr(LrgTonemap)       tm = lrg_tonemap_new ();
	g_autoptr(GrlImage)         img = NULL;
	g_autoptr(GrlImage)         fimg = NULL;
	g_autoptr(GError)           error = NULL;
	g_autofree guint8          *bytes = g_new (guint8, 256 * 4 * 4);
	g_autofree gfloat          *floats = buffer_new (4, 2);
	const guint8               *data;
	guint                       i;

	/* every byte value, in every channel */
	for (i = 0; i < 256 * 4 * 4; i++)
		bytes[i] = (guint8) ((i / 4 + (i % 4) * 37) & 0xff);
	img = grl_image_new_from_pixels (256, 4, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, bytes);
	g_assert_nonnull (img);

	/* an empty stack is the identity */
	g_assert_true (lrg_post_processor_apply_to_image (proc, img, 0.0f, &error));
	g_assert_no_error (error);
	data = grl_image_get_pixels (img, NULL);
	g_assert_cmpmem (data, 256 * 4 * 4, bytes, 256 * 4 * 4);

	/* a tonemap: each byte becomes the rounded reference */
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (tm));
	g_assert_true (lrg_post_processor_apply_to_image (proc, img, 0.0f, &error));
	for (i = 0; i < 256 * 4; i++)
	{
		const guint8 *in = bytes + i * 4;
		const guint8 *out = data + i * 4;
		gfloat        rgb[3] = { in[0] / 255.0f, in[1] / 255.0f, in[2] / 255.0f };
		guint         k;

		lrg_tonemap_apply_rgb (tm, rgb, rgb);
		for (k = 0; k < 3; k++)
			g_assert_cmpint (out[k], ==, (gint) lroundf (CLAMP (rgb[k], 0.0f, 1.0f) * 255.0f));
		g_assert_cmpuint (out[3], ==, in[3]);
	}

	/* float RGBA: processed in place, no conversion. graylib makes
	 * float images by converting RGBA8 ones (bytes / 255) and reads
	 * them back through its RGBA8 view, so compare within one step. */
	fimg = grl_image_new_from_pixels (4, 2, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, bytes + 100 * 4);
	grl_image_set_format (fimg, GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
	g_assert_cmpint (grl_image_get_format (fimg), ==, GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
	for (i = 0; i < 4 * 2 * 4; i++)
		floats[i] = (gfloat) bytes[100 * 4 + i] / 255.0f;
	g_assert_true (lrg_post_processor_apply_to_image (proc, fimg, 0.0f, &error));
	g_assert_no_error (error);
	g_assert_cmpint (grl_image_get_format (fimg), ==, GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
	lrg_post_effect_apply_cpu (LRG_POST_EFFECT (tm), floats, 4, 2, 0.0f);
	data = grl_image_get_pixels (fimg, NULL);
	for (i = 0; i < 4 * 2 * 4; i++)
		g_assert_cmpint (abs ((gint) data[i] - (gint) (CLAMP (floats[i], 0.0f, 1.0f) * 255.0f)), <=, 1);

	/* other formats are refused */
	grl_image_set_format (fimg, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8);
	g_assert_false (lrg_post_processor_apply_to_image (proc, fimg, 0.0f, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

/* ==========================================================================
 * GPU against CPU
 * ========================================================================== */

#define GPU_W (64)
#define GPU_H (48)

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* Never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (GPU_W, GPU_H, "test-post-cpu");
	return IsWindowReady ();
}

/* an opaque RGBA8 scene: gradients, noise, a white block, a diagonal */
static GrlImage *
gpu_scene (void)
{
	g_autofree guint8 *px = g_new (guint8, GPU_W * GPU_H * 4);
	guint              x, y;

	for (y = 0; y < GPU_H; y++)
		for (x = 0; x < GPU_W; x++)
		{
			guint8 *p = px + (y * GPU_W + x) * 4;

			p[0] = (guint8) (x * 255 / (GPU_W - 1));
			p[1] = (guint8) (y * 255 / (GPU_H - 1));
			p[2] = (guint8) (hash01 (x, y, 5) * 255.0f);
			p[3] = 255;
			if (x > 20 && x < 30 && y > 14 && y < 24)
				p[0] = p[1] = p[2] = 255;
			if (x > y + 30)
				p[0] = p[1] = p[2] = 20;
		}
	return grl_image_new_from_pixels (GPU_W, GPU_H, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, px);
}

/* draws @scene through @proc's GPU chain, reads the result back top-down */
static Image
gpu_frame (LrgPostProcessor *proc,
           GrlImage         *scene)
{
	RenderTexture2D outer = LoadRenderTexture (GPU_W, GPU_H);
	Image           src;
	Texture2D       tex;
	Image           img;

	/* a raylib view of the scene's pixels (graylib keeps its handle private) */
	src.data = grl_image_get_pixels (scene, NULL);
	src.width = GPU_W;
	src.height = GPU_H;
	src.mipmaps = 1;
	src.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	tex = LoadTextureFromImage (src);

	BeginDrawing ();
	BeginTextureMode (outer);
	ClearBackground (MAGENTA);
	lrg_post_processor_begin_capture (proc);
	ClearBackground (BLACK);
	DrawTexture (tex, 0, 0, WHITE);
	lrg_post_processor_end_capture (proc);
	lrg_post_processor_render (proc, 1.0f / 60.0f);
	EndTextureMode ();
	EndDrawing ();
	img = LoadImageFromTexture (outer.texture);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageFlipVertical (&img);
	UnloadTexture (tex);
	UnloadRenderTexture (outer);
	return img;
}

static void
gpu_compare (const gchar   *what,
             LrgPostEffect *effect,
             gint           tolerance,
             gdouble        outlier_fraction)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (GPU_W, GPU_H);
	g_autoptr(GrlImage)         scene = gpu_scene ();
	g_autoptr(GrlImage)         cpu = gpu_scene ();
	g_autoptr(GError)           error = NULL;
	const guint8               *c;
	Image                       gpu;
	guint                       i, outliers = 0;
	gint                        worst = 0;

	lrg_post_processor_add_effect (proc, effect);
	gpu = gpu_frame (proc, scene);
	g_assert_cmpuint (lrg_post_processor_get_pass_count (proc), ==, 1);

	/* the same frame on the CPU: render() hands the effects 1/60 s */
	g_assert_true (lrg_post_processor_apply_to_image (proc, cpu, 1.0f / 60.0f, &error));
	g_assert_no_error (error);
	c = grl_image_get_pixels (cpu, NULL);
	for (i = 0; i < GPU_W * GPU_H * 4; i++)
	{
		gint d = abs ((gint) ((const guint8 *) gpu.data)[i] - (gint) c[i]);

		worst = MAX (worst, d);
		if (d > tolerance)
			outliers++;
	}
	g_test_message ("%s: worst difference %d/255, %u channels over %d", what, worst, outliers, tolerance);
	g_assert_cmpfloat ((gdouble) outliers, <=, outlier_fraction * GPU_W * GPU_H * 4);
	UnloadImage (gpu);
	lrg_post_processor_release (proc);
}

static void
test_gpu_vs_cpu (void)
{
	static const guint8 pal[] = { 0, 0, 0, 255, 255, 255, 200, 40, 40, 30, 90, 200, 240, 200, 60 };
	g_autoptr(GBytes)   bytes = g_bytes_new_static (pal, sizeof pal);

	gpu_ready = gpu_init ();
	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	{
		g_autoptr(LrgBloom) e = lrg_bloom_new ();

		lrg_bloom_set_threshold (e, 0.7f);
		lrg_bloom_set_intensity (e, 1.5f);
		gpu_compare ("bloom", LRG_POST_EFFECT (e), 4, 0.01);
	}
	{
		g_autoptr(LrgTonemap) e = lrg_tonemap_new ();

		lrg_tonemap_set_operator (e, LRG_TONEMAP_OPERATOR_ACES);
		gpu_compare ("tonemap", LRG_POST_EFFECT (e), 2, 0.0);
	}
	{
		g_autoptr(LrgFxaa) e = lrg_fxaa_new ();

		gpu_compare ("fxaa", LRG_POST_EFFECT (e), 3, 0.005);
	}
	{
		g_autoptr(LrgColorGrade) e = lrg_color_grade_new ();

		lrg_color_grade_set_contrast (e, 1.3f);
		lrg_color_grade_set_saturation (e, 0.7f);
		lrg_color_grade_set_temperature (e, 0.3f);
		gpu_compare ("color-grade", LRG_POST_EFFECT (e), 2, 0.0);
	}
	{
		g_autoptr(LrgVignette) e = lrg_vignette_new ();

		lrg_vignette_set_intensity (e, 0.9f);
		gpu_compare ("vignette", LRG_POST_EFFECT (e), 2, 0.0);
	}
	{
		g_autoptr(LrgFilmGrain) e = lrg_film_grain_new ();

		lrg_film_grain_set_intensity (e, 0.3f);
		lrg_film_grain_set_speed (e, 30.0f);
		gpu_compare ("film-grain", LRG_POST_EFFECT (e), 2, 0.0);
	}
	{
		g_autoptr(LrgSharpen) e = lrg_sharpen_new ();

		gpu_compare ("sharpen", LRG_POST_EFFECT (e), 2, 0.0);
	}
	{
		g_autoptr(LrgColorblindFilter) e = lrg_colorblind_filter_new_with_type (LRG_COLORBLIND_PROTANOPIA);

		gpu_compare ("colorblind-filter", LRG_POST_EFFECT (e), 2, 0.0);
	}
	{
		g_autoptr(LrgPalettize) e = lrg_palettize_new ();

		g_assert_true (lrg_palettize_set_palette (e, bytes, NULL));
		lrg_palettize_set_dither (e, TRUE);
		/* a channel on a rounding boundary may pick the other colour */
		gpu_compare ("palettize", LRG_POST_EFFECT (e), 2, 0.01);
	}
	{
		g_autoptr(LrgBlur) e = lrg_blur_new ();

		lrg_blur_set_radius (e, 5);
		gpu_compare ("blur", LRG_POST_EFFECT (e), 2, 0.0);
	}
}

gint
main (gint   argc,
      gchar *argv[])
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	/* The window opens only in the GPU test (the last one), so every CPU
	 * test above runs without a GL context, as apply_cpu promises. */

	g_test_add_func ("/post-cpu/class-layout", test_class_layout);
	g_test_add_func ("/post-cpu/has-cpu", test_has_cpu);
	g_test_add_func ("/post-cpu/wrapper", test_wrapper_enabled_intensity);
	g_test_add_func ("/post-cpu/tonemap", test_tonemap);
	g_test_add_func ("/post-cpu/color-grade", test_color_grade_and_lut);
	g_test_add_func ("/post-cpu/colorblind", test_colorblind);
	g_test_add_func ("/post-cpu/palettize", test_palettize);
	g_test_add_func ("/post-cpu/sharpen", test_sharpen);
	g_test_add_func ("/post-cpu/vignette", test_vignette);
	g_test_add_func ("/post-cpu/blur-kernel", test_blur_kernel);
	g_test_add_func ("/post-cpu/blur-image", test_blur_image);
	g_test_add_func ("/post-cpu/blur-in-stack", test_blur_in_stack);
	g_test_add_func ("/post-cpu/fxaa", test_fxaa);
	g_test_add_func ("/post-cpu/grain", test_grain);
	g_test_add_func ("/post-cpu/bloom", test_bloom);
	g_test_add_func ("/post-cpu/processor-chain", test_processor_chain);
	g_test_add_func ("/post-cpu/processor-not-supported", test_processor_not_supported);
	g_test_add_func ("/post-cpu/image-round-trip", test_image_round_trip);
	g_test_add_func ("/post-cpu/gpu/vs-cpu", test_gpu_vs_cpu);

	ret = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return ret;
}
