/* test-image-raster.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for lrg_image_draw_triangle_textured(), the CPU textured-triangle
 * rasteriser: coverage of known triangles, the top-left fill rule (shared
 * edges drawn exactly once, no seams in a jittered mesh), UV mapping and
 * bilinear values against hand-computed ones, premultiplied sampling,
 * tint, clipping and degenerate input.  Everything runs headless.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

static const GrlColor BLACK = { 0, 0, 0, 255 };
static const GrlColor CLEAR = { 0, 0, 0, 0 };
static const GrlColor WHITE = { 255, 255, 255, 255 };

/* The RGBA bytes of pixel (x, y). */
static const guint8 *
px (GrlImage *img,
    gint      x,
    gint      y)
{
	const guint8 *p = grl_image_get_pixels (img, NULL);

	return p + ((gsize)y * (gsize)grl_image_get_width (img) + (gsize)x) * 4;
}

static void
assert_px (GrlImage *img,
           gint      x,
           gint      y,
           guint8    r,
           guint8    g,
           guint8    b,
           guint8    a)
{
	const guint8 *p = px (img, x, y);

	if (p[0] != r || p[1] != g || p[2] != b || p[3] != a)
		g_error ("pixel (%d, %d) is (%u, %u, %u, %u), expected (%u, %u, %u, %u)",
		         x, y, p[0], p[1], p[2], p[3], r, g, b, a);
}

/* Pixels whose alpha is not 0. */
static guint
count_set (GrlImage *img)
{
	gint  w = grl_image_get_width (img);
	gint  h = grl_image_get_height (img);
	guint n = 0;
	gint  x;
	gint  y;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			if (px (img, x, y)[3] != 0)
				n++;
	return n;
}

/* ==========================================================================
 * Coverage
 * ========================================================================== */

/*
 * The right triangle (0, 0), (8, 0), (0, 8): pixel (i, j) has its centre
 * inside when i + j + 1 < 8; on the hypotenuse (i + j + 1 == 8, a bottom
 * right edge) it is left to the neighbour, so i + j <= 6: 28 pixels.
 */
static void
test_coverage_right_triangle (void)
{
	g_autoptr(GrlImage) img = grl_image_new_color (16, 16, &CLEAR);
	const gfloat        xy[6] = { 0, 0, 8, 0, 0, 8 };
	gint                x;
	gint                y;

	lrg_image_draw_triangle_textured (img, NULL, xy, NULL, &WHITE);
	g_assert_cmpuint (count_set (img), ==, 28);
	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			g_assert_cmpint (px (img, x, y)[3] != 0, ==, x + y <= 6);
}

/* A thin sliver between pixel centres covers nothing; one through a row
 * of centres along its top edge covers that row. */
static void
test_coverage_top_left (void)
{
	g_autoptr(GrlImage) img = grl_image_new_color (8, 8, &CLEAR);
	const gfloat        sliver[6] = { 0, 2.6f, 8, 2.6f, 8, 2.9f };
	const gfloat        top[6] = { 0, 3.5f, 8, 3.5f, 0, 3.9f };
	const gfloat        bottom[6] = { 0, 5.1f, 8, 5.5f, 0, 5.5f };

	lrg_image_draw_triangle_textured (img, NULL, sliver, NULL, &WHITE);
	g_assert_cmpuint (count_set (img), ==, 0);

	/* centres (i + 0.5, 3.5) lie on the top edge: drawn (i < 8 with the
	 * left end at x = 0 and the right corner at x = 8 shrinking the
	 * triangle to nothing at x = 8) */
	lrg_image_draw_triangle_textured (img, NULL, top, NULL, &WHITE);
	g_assert_cmpuint (count_set (img), ==, 8);
	g_assert_cmpuint (px (img, 0, 3)[3], ==, 255);

	/* centres (i + 0.5, 5.5) lie on the bottom edge: not drawn by this
	 * triangle (its other centres are not inside either) */
	lrg_image_draw_triangle_textured (img, NULL, bottom, NULL, &WHITE);
	g_assert_cmpuint (count_set (img), ==, 8);
}

/* ==========================================================================
 * Shared edges: no double blending, no holes
 * ========================================================================== */

/*
 * A translucent quad made of two triangles over opaque black: the
 * diagonal runs through pixel centres, and every covered pixel has
 * exactly the single-blend value (a double blend would be brighter, a
 * hole black).
 */
static void
test_shared_edge_translucent_quad (void)
{
	g_autoptr(GrlImage) img = grl_image_new_color (12, 12, &BLACK);
	const gfloat        t1[6] = { 1, 1, 11, 1, 11, 11 };
	const gfloat        t2[6] = { 1, 1, 11, 11, 1, 11 };
	GrlColor            half = { 255, 255, 255, 128 };
	gint                x;
	gint                y;

	lrg_image_draw_triangle_textured (img, NULL, t1, NULL, &half);
	lrg_image_draw_triangle_textured (img, NULL, t2, NULL, &half);
	for (y = 0; y < 12; y++)
	{
		for (x = 0; x < 12; x++)
		{
			gboolean inside = x >= 1 && x < 11 && y >= 1 && y < 11;

			if (inside)
				assert_px (img, x, y, 128, 128, 128, 255);
			else
				assert_px (img, x, y, 0, 0, 0, 255);
		}
	}
}

/*
 * An irregular quad split along a diagonal from (1.9, 2.2) to
 * (13.1, 7.8), which passes through the pixel centres (2.5, 2.5),
 * (4.5, 3.5) .. (12.5, 7.5): drawn one triangle per image, the two masks
 * never share a pixel, and their union is exactly the set of centres
 * strictly inside the quad (no outer edge passes through a centre).
 */
static void
test_shared_edge_irregular (void)
{
	g_autoptr(GrlImage) a = grl_image_new_color (16, 12, &CLEAR);
	g_autoptr(GrlImage) b = grl_image_new_color (16, 12, &CLEAR);
	const gfloat        q[8] = { 1.9f, 2.2f, 13.3f, 1.1f, 13.1f, 7.8f, 1.2f, 9.7f };
	const gfloat        ta[6] = { q[0], q[1], q[2], q[3], q[4], q[5] };
	const gfloat        tb[6] = { q[0], q[1], q[4], q[5], q[6], q[7] };
	guint               expected = 0;
	guint               on_diagonal = 0;
	gint                x;
	gint                y;

	lrg_image_draw_triangle_textured (a, NULL, ta, NULL, &WHITE);
	lrg_image_draw_triangle_textured (b, NULL, tb, NULL, &WHITE);
	for (y = 0; y < 12; y++)
	{
		for (x = 0; x < 16; x++)
		{
			gdouble  cx = x + 0.5;
			gdouble  cy = y + 0.5;
			gboolean in = TRUE;
			gint     e;
			gboolean sa = px (a, x, y)[3] != 0;
			gboolean sb = px (b, x, y)[3] != 0;

			/* inside the convex quad: the same side of all four edges */
			for (e = 0; e < 4; e++)
			{
				gdouble ax = q[e * 2];
				gdouble ay = q[e * 2 + 1];
				gdouble bx = q[((e + 1) % 4) * 2];
				gdouble by = q[((e + 1) % 4) * 2 + 1];
				gdouble c = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);

				g_assert_cmpfloat (fabs (c), >, 1e-6);   /* no centre on an outer edge */
				if (c < 0.0)
					in = FALSE;
			}
			g_assert_false (sa && sb);
			g_assert_cmpint (sa || sb, ==, in);
			if (in)
				expected++;
			if (in && fabs ((12.5 - 2.5) * (cy - 2.5) - (7.5 - 2.5) * (cx - 2.5)) < 1e-9)
				on_diagonal++;
		}
	}
	g_assert_cmpuint (count_set (a) + count_set (b), ==, expected);
	g_assert_cmpuint (on_diagonal, >=, 5);   /* the rule was exercised */
}

/*
 * A 6x6-cell grid mesh with jittered interior vertices on a quarter-pixel
 * lattice (so many edges and corners land exactly on pixel centres):
 * every centre strictly inside the outer rectangle is covered by exactly
 * one triangle, every centre outside by none.
 */
static void
test_shared_edge_jittered_mesh (void)
{
	const gint          n = 6;
	const gfloat        x0 = 2.3f;
	const gfloat        y0 = 1.7f;
	const gfloat        cell = 5.0f;
	gfloat              vx[7][7];
	gfloat              vy[7][7];
	g_autofree guint   *count = g_new0 (guint, 40 * 40);
	g_autoptr(GRand)    rng = g_rand_new_with_seed (1234);
	gint                i;
	gint                j;
	gint                x;
	gint                y;

	for (j = 0; j <= n; j++)
	{
		for (i = 0; i <= n; i++)
		{
			vx[j][i] = x0 + cell * (gfloat)i;
			vy[j][i] = y0 + cell * (gfloat)j;
			if (i > 0 && i < n && j > 0 && j < n)
			{
				/* snap to a lattice that contains the pixel centres */
				vx[j][i] = floorf (vx[j][i] * 2.0f) / 2.0f + 0.5f * (gfloat)g_rand_int_range (rng, -3, 4);
				vy[j][i] = floorf (vy[j][i] * 2.0f) / 2.0f + 0.5f * (gfloat)g_rand_int_range (rng, -3, 4);
			}
		}
	}

	/* each triangle into its own mask; the masks are summed */
	for (j = 0; j < n; j++)
	{
		for (i = 0; i < n; i++)
		{
			gfloat t[2][6] = {
				{ vx[j][i], vy[j][i], vx[j][i + 1], vy[j][i + 1], vx[j + 1][i + 1], vy[j + 1][i + 1] },
				{ vx[j][i], vy[j][i], vx[j + 1][i + 1], vy[j + 1][i + 1], vx[j + 1][i], vy[j + 1][i] },
			};
			gint k;

			for (k = 0; k < 2; k++)
			{
				g_autoptr(GrlImage) mask = grl_image_new_color (40, 40, &CLEAR);

				lrg_image_draw_triangle_textured (mask, NULL, t[k], NULL, &WHITE);
				for (y = 0; y < 40; y++)
					for (x = 0; x < 40; x++)
						if (px (mask, x, y)[3] != 0)
							count[y * 40 + x]++;
			}
		}
	}
	for (y = 0; y < 40; y++)
	{
		for (x = 0; x < 40; x++)
		{
			gfloat   cx = (gfloat)x + 0.5f;
			gfloat   cy = (gfloat)y + 0.5f;
			gboolean in = cx > x0 && cx < x0 + cell * n && cy > y0 && cy < y0 + cell * n;

			if (count[y * 40 + x] != (in ? 1u : 0u))
				g_error ("pixel (%d, %d) covered %u times", x, y, count[y * 40 + x]);
		}
	}
}

/* ==========================================================================
 * Texture mapping
 * ========================================================================== */

/* Draws the quad (0, 0) .. (w, h) of @dst with UVs 0 .. 1. */
static void
draw_full_quad (GrlImage       *dst,
                GrlImage       *src,
                const GrlColor *tint)
{
	gfloat       w = (gfloat)grl_image_get_width (dst);
	gfloat       h = (gfloat)grl_image_get_height (dst);
	const gfloat xy1[6] = { 0, 0, w, 0, w, h };
	const gfloat uv1[6] = { 0, 0, 1, 0, 1, 1 };
	const gfloat xy2[6] = { 0, 0, w, h, 0, h };
	const gfloat uv2[6] = { 0, 0, 1, 1, 0, 1 };

	lrg_image_draw_triangle_textured (dst, src, xy1, uv1, tint);
	lrg_image_draw_triangle_textured (dst, src, xy2, uv2, tint);
}

/* A 2x2 texture (red, green / blue, white) magnified to 4x4: the
 * corner pixels clamp to one texel, the inner ones mix 3:1 and 9:3:3:1. */
static void
test_uv_bilinear (void)
{
	g_autoptr(GrlImage) src = grl_image_new_color (2, 2, &CLEAR);
	g_autoptr(GrlImage) dst = grl_image_new_color (4, 4, &CLEAR);
	guint8             *s = grl_image_get_pixels (src, NULL);
	const guint8        texels[16] = { 255, 0, 0, 255,   0, 255, 0, 255,
	                                   0, 0, 255, 255,   255, 255, 255, 255 };

	memcpy (s, texels, sizeof texels);
	draw_full_quad (dst, src, NULL);

	/* pixel centre 0.5 / 4 = u 0.125 -> texel x -0.25: clamped to texel 0 */
	assert_px (dst, 0, 0, 255, 0, 0, 255);
	assert_px (dst, 3, 0, 0, 255, 0, 255);
	assert_px (dst, 0, 3, 0, 0, 255, 255);
	assert_px (dst, 3, 3, 255, 255, 255, 255);
	/* u 0.375 -> texel x 0.25: 0.75 red + 0.25 green = (191.25, 63.75, 0) */
	assert_px (dst, 1, 0, 191, 64, 0, 255);
	/* (0.375, 0.375): 0.5625 red, 0.1875 green, 0.1875 blue, 0.0625 white
	 * = (159.375, 63.75, 63.75) */
	assert_px (dst, 1, 1, 159, 64, 64, 255);
	/* (0.625, 0.625): 0.0625 red, 0.1875 green, 0.1875 blue, 0.5625 white
	 * = (159.375, 191.25, 191.25) */
	assert_px (dst, 2, 2, 159, 191, 191, 255);
}

/* A texel that is fully transparent adds no colour: opaque red next to
 * transparent green blends to red at partial alpha, not to olive. */
static void
test_uv_premultiplied (void)
{
	g_autoptr(GrlImage) src = grl_image_new_color (2, 1, &CLEAR);
	g_autoptr(GrlImage) dst = grl_image_new_color (4, 1, &CLEAR);
	guint8             *s = grl_image_get_pixels (src, NULL);
	const guint8        texels[8] = { 255, 0, 0, 255,   0, 255, 0, 0 };

	memcpy (s, texels, sizeof texels);
	draw_full_quad (dst, src, NULL);
	assert_px (dst, 0, 0, 255, 0, 0, 255);
	assert_px (dst, 1, 0, 255, 0, 0, 191);   /* 0.75 coverage of red */
	assert_px (dst, 2, 0, 255, 0, 0, 64);    /* 0.25 */
	assert_px (dst, 3, 0, 0, 0, 0, 0);       /* untouched */
}

/* UVs that map only part of the texture, and a rotated mapping. */
static void
test_uv_mapping (void)
{
	g_autoptr(GrlImage) src = grl_image_new_color (4, 1, &CLEAR);
	g_autoptr(GrlImage) dst = grl_image_new_color (2, 2, &BLACK);
	guint8             *s = grl_image_get_pixels (src, NULL);
	const gfloat        xy1[6] = { 0, 0, 2, 0, 2, 2 };
	const gfloat        xy2[6] = { 0, 0, 2, 2, 0, 2 };
	const gfloat        uv1[6] = { 0.5f, 0, 1, 0, 1, 1 };
	const gfloat        uv2[6] = { 0.5f, 0, 1, 1, 0.5f, 1 };
	guint               i;

	/* texel i has red 40 * i + 10 */
	for (i = 0; i < 4; i++)
	{
		s[i * 4] = (guint8)(40 * i + 10);
		s[i * 4 + 3] = 255;
	}
	lrg_image_draw_triangle_textured (dst, src, xy1, uv1, NULL);
	lrg_image_draw_triangle_textured (dst, src, xy2, uv2, NULL);
	/* x centre 0.5 of 2 -> u = 0.5 + 0.25 * 0.5 = 0.625 -> texel 2.0:
	 * exactly texel 2 (90); x centre 1.5 -> u 0.875 -> texel 3.0 (130) */
	assert_px (dst, 0, 0, 90, 0, 0, 255);
	assert_px (dst, 1, 1, 130, 0, 0, 255);
}

/* ==========================================================================
 * Tint, clipping, degenerate input
 * ========================================================================== */

static void
test_tint (void)
{
	g_autoptr(GrlImage) src = grl_image_new_color (2, 2, &WHITE);
	g_autoptr(GrlImage) dst = grl_image_new_color (4, 4, &BLACK);
	GrlColor            orange = { 255, 128, 0, 255 };
	GrlColor            half_red = { 255, 0, 0, 128 };

	/* textured: white texels times the tint */
	draw_full_quad (dst, src, &orange);
	assert_px (dst, 1, 2, 255, 128, 0, 255);

	/* untextured: the tint alone, alpha blended over the orange */
	draw_full_quad (dst, NULL, &half_red);
	/* red: 255 * 0.502 + 255 * 0.498 = 255; green 128 * 0.498 = 63.75 */
	assert_px (dst, 2, 1, 255, 64, 0, 255);

	/* a fully transparent tint draws nothing */
	draw_full_quad (dst, NULL, &CLEAR);
	assert_px (dst, 2, 1, 255, 64, 0, 255);
}

static void
test_clipping (void)
{
	g_autoptr(GrlImage) img = grl_image_new_color (8, 8, &CLEAR);
	const gfloat        huge[6] = { -100, -100, 300, -100, -100, 300 };
	const gfloat        off[6] = { -50, -50, -10, -50, -10, -10 };
	g_autoptr(GrlRectangle) clip = grl_rectangle_new (2, 3, 4, 2);
	gint                x;
	gint                y;

	/* a triangle far larger than the image fills it, without walking
	 * the whole bounding box outside */
	lrg_image_draw_triangle_textured (img, NULL, huge, NULL, &WHITE);
	g_assert_cmpuint (count_set (img), ==, 64);

	/* one entirely off the image draws nothing */
	{
		g_autoptr(GrlImage) other = grl_image_new_color (8, 8, &CLEAR);

		lrg_image_draw_triangle_textured (other, NULL, off, NULL, &WHITE);
		g_assert_cmpuint (count_set (other), ==, 0);

		/* the clip rectangle confines drawing */
		grl_image_set_clip_rect (other, clip);
		lrg_image_draw_triangle_textured (other, NULL, huge, NULL, &WHITE);
		g_assert_cmpuint (count_set (other), ==, 8);
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				g_assert_cmpint (px (other, x, y)[3] != 0, ==,
				                 x >= 2 && x < 6 && y >= 3 && y < 5);
	}
}

static void
test_degenerate (void)
{
	g_autoptr(GrlImage) img = grl_image_new_color (8, 8, &CLEAR);
	g_autoptr(GrlImage) a = grl_image_new_color (8, 8, &CLEAR);
	g_autoptr(GrlImage) b = grl_image_new_color (8, 8, &CLEAR);
	const gfloat        point[6] = { 3, 3, 3, 3, 3, 3 };
	const gfloat        line[6] = { 0, 0, 4, 4, 8, 8 };
	const gfloat        nan_xy[6] = { 0, 0, NAN, 0, 0, 8 };
	const gfloat        inf_xy[6] = { 0, 0, INFINITY, 0, 0, 8 };
	const gfloat        far_xy[6] = { 0, 0, 1e9f, 0, 0, 8 };
	const gfloat        cw[6] = { 0.3f, 0.2f, 7.6f, 1.4f, 2.2f, 7.9f };
	const gfloat        ccw[6] = { 0.3f, 0.2f, 2.2f, 7.9f, 7.6f, 1.4f };
	const guint8       *pa;
	const guint8       *pb;
	gsize               size;

	lrg_image_draw_triangle_textured (img, NULL, point, NULL, &WHITE);
	lrg_image_draw_triangle_textured (img, NULL, line, NULL, &WHITE);
	lrg_image_draw_triangle_textured (img, NULL, nan_xy, NULL, &WHITE);
	lrg_image_draw_triangle_textured (img, NULL, inf_xy, NULL, &WHITE);
	lrg_image_draw_triangle_textured (img, NULL, far_xy, NULL, &WHITE);
	g_assert_cmpuint (count_set (img), ==, 0);

	/* the winding does not matter: both orders draw the same pixels */
	lrg_image_draw_triangle_textured (a, NULL, cw, NULL, &WHITE);
	lrg_image_draw_triangle_textured (b, NULL, ccw, NULL, &WHITE);
	g_assert_cmpuint (count_set (a), >, 10);
	pa = grl_image_get_pixels (a, &size);
	pb = grl_image_get_pixels (b, NULL);
	g_assert_cmpint (memcmp (pa, pb, size), ==, 0);

	/* UVs that are NaN or far outside 0 .. 1 clamp instead of crashing */
	{
		g_autoptr(GrlImage) src = grl_image_new_color (2, 2, &WHITE);
		const gfloat        xy[6] = { 0, 0, 8, 0, 0, 8 };
		const gfloat        uv[6] = { NAN, -1e30f, 1e30f, 5, -3, NAN };

		lrg_image_draw_triangle_textured (img, src, xy, uv, NULL);
		g_assert_cmpuint (count_set (img), ==, 28);
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/image-raster/coverage/right-triangle", test_coverage_right_triangle);
	g_test_add_func ("/image-raster/coverage/top-left", test_coverage_top_left);
	g_test_add_func ("/image-raster/shared-edge/translucent-quad", test_shared_edge_translucent_quad);
	g_test_add_func ("/image-raster/shared-edge/irregular", test_shared_edge_irregular);
	g_test_add_func ("/image-raster/shared-edge/jittered-mesh", test_shared_edge_jittered_mesh);
	g_test_add_func ("/image-raster/uv/bilinear", test_uv_bilinear);
	g_test_add_func ("/image-raster/uv/premultiplied", test_uv_premultiplied);
	g_test_add_func ("/image-raster/uv/mapping", test_uv_mapping);
	g_test_add_func ("/image-raster/tint", test_tint);
	g_test_add_func ("/image-raster/clipping", test_clipping);
	g_test_add_func ("/image-raster/degenerate", test_degenerate);

	return g_test_run ();
}
