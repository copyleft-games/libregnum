/* test-sdf.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the signed distance functions (shading/lrg-sdf.h) and
 * lrg_image_canvas_fill_sdf().  Reference values are computed
 * independently: closed-form distances at chosen points, and brute
 * force minima over densely sampled boundaries for the shapes whose
 * exact formula is not obvious (ellipse, triangle, ellipsoid).
 *
 * Everything is CPU only: no window, no GL context, no display.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* a tiny deterministic generator for the randomised checks */
static guint32 rng_state = 0x12345678U;

static gfloat
rnd (gfloat lo,
     gfloat hi)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return lo + (hi - lo) * (gfloat)(rng_state >> 8) / 16777216.0f;
}

/*
 * ellipse_brute:
 * Signed distance to an ellipse by sampling the boundary at @n
 * parameter values and refining the best one with a local search.
 */
static gdouble
ellipse_brute (gdouble px,
               gdouble py,
               gdouble a,
               gdouble b)
{
	const gint n = 20000;
	gdouble best;
	gdouble best_t;
	gdouble step;
	gint i;
	gint it;
	gboolean inside;

	best = G_MAXDOUBLE;
	best_t = 0.0;
	for (i = 0; i < n; i++)
	{
		gdouble t = 2.0 * G_PI * i / n;
		gdouble d = hypot (a * cos (t) - px, b * sin (t) - py);

		if (d < best)
		{
			best = d;
			best_t = t;
		}
	}

	/* golden-ish refinement around the best sample */
	step = 2.0 * G_PI / n;
	for (it = 0; it < 60; it++)
	{
		gdouble tl = best_t - step;
		gdouble tr = best_t + step;
		gdouble dl = hypot (a * cos (tl) - px, b * sin (tl) - py);
		gdouble dr = hypot (a * cos (tr) - px, b * sin (tr) - py);

		if (dl < best)
		{
			best = dl;
			best_t = tl;
		}
		else if (dr < best)
		{
			best = dr;
			best_t = tr;
		}
		else
			step *= 0.5;
	}

	inside = (px * px) / (a * a) + (py * py) / (b * b) < 1.0;
	return inside ? -best : best;
}

/* unsigned distance from p to segment ab, in double, written out longhand */
static gdouble
seg_dist (gdouble px, gdouble py,
          gdouble ax, gdouble ay,
          gdouble bx, gdouble by)
{
	gdouble vx = bx - ax;
	gdouble vy = by - ay;
	gdouble l2 = vx * vx + vy * vy;
	gdouble t = l2 > 0.0 ? ((px - ax) * vx + (py - ay) * vy) / l2 : 0.0;

	t = CLAMP (t, 0.0, 1.0);
	return hypot (px - (ax + t * vx), py - (ay + t * vy));
}

/* signed triangle distance from edge distances and barycentric inside test */
static gdouble
triangle_brute (const gfloat p[2],
                const gfloat a[2],
                const gfloat b[2],
                const gfloat c[2])
{
	gdouble d;
	gdouble d1;
	gdouble d2;
	gdouble d3;
	gboolean neg;
	gboolean pos;

	d = MIN (seg_dist (p[0], p[1], a[0], a[1], b[0], b[1]),
	         MIN (seg_dist (p[0], p[1], b[0], b[1], c[0], c[1]),
	              seg_dist (p[0], p[1], c[0], c[1], a[0], a[1])));
	d1 = (p[0] - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (p[1] - b[1]);
	d2 = (p[0] - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (p[1] - c[1]);
	d3 = (p[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (p[1] - a[1]);
	neg = d1 < 0 || d2 < 0 || d3 < 0;
	pos = d1 > 0 || d2 > 0 || d3 > 0;
	return (!(neg && pos)) ? -d : d;
}

/* ellipsoid distance by sampling the surface on a fine parameter grid */
static gdouble
ellipsoid_brute (const gfloat p[3],
                 const gfloat r[3])
{
	const gint n = 600;
	gdouble best;
	gint i;
	gint j;
	gdouble k;

	best = G_MAXDOUBLE;
	for (i = 0; i <= n; i++)
	{
		gdouble th = G_PI * i / n;

		for (j = 0; j < 2 * n; j++)
		{
			gdouble ph = G_PI * j / n;
			gdouble x = r[0] * sin (th) * cos (ph);
			gdouble y = r[1] * cos (th);
			gdouble z = r[2] * sin (th) * sin (ph);
			gdouble d = sqrt ((x - p[0]) * (x - p[0]) + (y - p[1]) * (y - p[1]) +
			                  (z - p[2]) * (z - p[2]));

			best = MIN (best, d);
		}
	}
	k = (p[0] * p[0]) / (r[0] * r[0]) + (p[1] * p[1]) / (r[1] * r[1]) +
	    (p[2] * p[2]) / (r[2] * r[2]);
	return k < 1.0 ? -best : best;
}

/* ==========================================================================
 * 2D primitives
 * ========================================================================== */

static void
test_sdf2_circle (void)
{
	gfloat p[2] = { 3.0f, 4.0f };
	gfloat o[2] = { 0.0f, 0.0f };

	g_assert_cmpfloat_with_epsilon (lrg_sdf2_circle (p, 2.0f), 3.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_circle (o, 2.0f), -2.0f, 1e-6f);
	p[0] = 0.0f;
	p[1] = 2.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_circle (p, 2.0f), 0.0f, 1e-6f);
}

static void
test_sdf2_ellipse_brute_force (void)
{
	static const gfloat shapes[][2] = {
		{ 2.0f, 1.0f }, { 1.0f, 3.0f }, { 5.0f, 0.2f }, { 0.3f, 4.0f },
		{ 1.0f, 1.001f }, { 1.5f, 1.4999f }, { 1.0f, 0.95f }
	};
	guint s;
	gint i;

	for (s = 0; s < G_N_ELEMENTS (shapes); s++)
	{
		gfloat extent = MAX (shapes[s][0], shapes[s][1]);

		for (i = 0; i < 60; i++)
		{
			gfloat p[2];
			gdouble ref;
			gfloat got;

			p[0] = rnd (-2.0f * extent, 2.0f * extent);
			p[1] = rnd (-2.0f * extent, 2.0f * extent);
			ref = ellipse_brute (p[0], p[1], shapes[s][0], shapes[s][1]);
			got = lrg_sdf2_ellipse (p, shapes[s]);
			if (fabs (got - ref) > 1e-3 * MAX (1.0, extent))
				g_error ("ellipse (%g, %g) at (%g, %g): got %g, brute force %g",
				         shapes[s][0], shapes[s][1], p[0], p[1], got, ref);
		}
	}
}

static void
test_sdf2_ellipse_special (void)
{
	gfloat ab[2] = { 3.0f, 1.0f };
	gfloat p[2];

	/* on the axes the distance is exact and simple */
	p[0] = 5.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), 2.0f, 1e-4f);
	p[0] = 0.0f; p[1] = -4.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), 3.0f, 1e-4f);
	p[0] = 0.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), -1.0f, 1e-4f);

	/* a point on the boundary */
	p[0] = 3.0f * cosf (0.7f);
	p[1] = 1.0f * sinf (0.7f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), 0.0f, 1e-4f);

	/* circle */
	ab[0] = ab[1] = 2.0f;
	p[0] = 3.0f; p[1] = 4.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), 3.0f, 1e-5f);

	/* degenerate: a flat segment from (-2, 0) to (2, 0) */
	ab[0] = 2.0f; ab[1] = 0.0f;
	p[0] = 3.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), 1.0f, 1e-5f);
	p[0] = 1.0f; p[1] = 2.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_ellipse (p, ab), 2.0f, 1e-5f);
}

static void
test_sdf2_box (void)
{
	gfloat b[2] = { 1.0f, 2.0f };
	gfloat p[2];

	p[0] = 3.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_box (p, b), 2.0f, 1e-6f);
	p[0] = 2.0f; p[1] = 3.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_box (p, b), (gfloat)G_SQRT2, 1e-6f);
	p[0] = 0.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_box (p, b), -1.0f, 1e-6f);
	p[0] = 0.5f; p[1] = -1.8f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_box (p, b), -0.2f, 1e-6f);
}

static void
test_sdf2_round_box (void)
{
	gfloat b[2] = { 1.0f, 1.0f };
	gfloat p[2];

	/* flat sides keep the outer extent */
	p[0] = 2.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_round_box (p, b, 0.5f), 1.0f, 1e-6f);

	/* corner: distance to the corner circle centred at (0.5, 0.5) */
	p[0] = 2.0f; p[1] = 2.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_round_box (p, b, 0.5f),
	                                1.5f * (gfloat)G_SQRT2 - 0.5f, 1e-5f);

	/* r = 0 is the plain box; an oversized r is clamped to a circle */
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_round_box (p, b, 0.0f), lrg_sdf2_box (p, b), 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_round_box (p, b, 5.0f),
	                                2.0f * (gfloat)G_SQRT2 - 1.0f, 1e-5f);
}

static void
test_sdf2_segment_capsule (void)
{
	gfloat a[2] = { 0.0f, 0.0f };
	gfloat b[2] = { 2.0f, 0.0f };
	gfloat p[2];

	p[0] = 1.0f; p[1] = 1.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_segment (p, a, b), 1.0f, 1e-6f);
	p[0] = 3.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_segment (p, a, b), 1.0f, 1e-6f);
	p[0] = -3.0f; p[1] = 4.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_segment (p, a, b), 5.0f, 1e-6f);

	/* capsule = segment - r */
	p[0] = 1.0f; p[1] = 0.25f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_capsule (p, a, b, 0.5f), -0.25f, 1e-6f);

	/* degenerate segment: a point */
	p[0] = 3.0f; p[1] = 4.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_segment (p, a, a), 5.0f, 1e-6f);
}

static void
test_sdf2_triangle (void)
{
	gfloat a[2] = { 0.0f, 0.0f };
	gfloat b[2] = { 4.0f, 0.0f };
	gfloat c[2] = { 1.0f, 3.0f };
	gfloat p[2];
	gint i;

	/* a known point below the base */
	p[0] = 2.0f; p[1] = -1.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_triangle (p, a, b, c), 1.0f, 1e-6f);

	/* inside, nearest the base */
	p[0] = 2.0f; p[1] = 0.25f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_triangle (p, a, b, c), -0.25f, 1e-6f);

	/* random triangles in both windings against the brute force */
	for (i = 0; i < 400; i++)
	{
		gfloat t[3][2];
		gint k;
		gdouble ref;
		gfloat got;

		for (k = 0; k < 3; k++)
		{
			t[k][0] = rnd (-3.0f, 3.0f);
			t[k][1] = rnd (-3.0f, 3.0f);
		}
		p[0] = rnd (-4.0f, 4.0f);
		p[1] = rnd (-4.0f, 4.0f);
		ref = triangle_brute (p, t[0], t[1], t[2]);
		got = lrg_sdf2_triangle (p, t[0], t[1], t[2]);
		g_assert_cmpfloat_with_epsilon (got, ref, 1e-4);
		got = lrg_sdf2_triangle (p, t[0], t[2], t[1]);
		g_assert_cmpfloat_with_epsilon (got, ref, 1e-4);
	}

	/* degenerate (collinear) triangle: unsigned distance, never inside */
	c[0] = 2.0f; c[1] = 0.0f;
	p[0] = 2.0f; p[1] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_triangle (p, a, b, c), 0.0f, 1e-6f);
	p[0] = 2.0f; p[1] = 1.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_triangle (p, a, b, c), 1.0f, 1e-6f);
}

/* ==========================================================================
 * 3D primitives
 * ========================================================================== */

static void
test_sdf3_sphere_box (void)
{
	gfloat p[3] = { 2.0f, 3.0f, 6.0f };
	gfloat b[3] = { 1.0f, 2.0f, 3.0f };

	g_assert_cmpfloat_with_epsilon (lrg_sdf3_sphere (p, 1.0f), 6.0f, 1e-6f);

	/* corner region: distance to the corner (1, 2, 3) */
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_box (p, b), sqrtf (1.0f + 1.0f + 9.0f), 1e-5f);
	p[0] = 0.0f; p[1] = 0.0f; p[2] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_box (p, b), -1.0f, 1e-6f);
	p[2] = 5.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_box (p, b), 2.0f, 1e-6f);

	/* rounded box: faces keep the extent, corners are rounded */
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_box (p, b, 0.5f), 2.0f, 1e-6f);
	p[0] = 2.0f; p[1] = 3.0f; p[2] = 4.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_box (p, b, 0.5f),
	                                sqrtf (3.0f * 1.5f * 1.5f) - 0.5f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_box (p, b, 0.0f), lrg_sdf3_box (p, b), 1e-6f);
}

static void
test_sdf3_ellipsoid_bound (void)
{
	gfloat r[3] = { 2.0f, 1.0f, 0.5f };
	gfloat p[3];
	gint i;

	/* exact on the axes */
	p[0] = 3.0f; p[1] = 0.0f; p[2] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_ellipsoid (p, r), 1.0f, 1e-5f);
	p[0] = 0.0f; p[1] = 0.0f; p[2] = -2.5f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_ellipsoid (p, r), 2.0f, 1e-5f);

	/* zero on the surface; centre gives minus the smallest semi-axis */
	p[0] = 2.0f * sinf (0.6f) * cosf (1.1f);
	p[1] = 1.0f * cosf (0.6f);
	p[2] = 0.5f * sinf (0.6f) * sinf (1.1f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_ellipsoid (p, r), 0.0f, 1e-5f);
	p[0] = p[1] = p[2] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_ellipsoid (p, r), -0.5f, 1e-6f);

	/*
	 * Elsewhere it is a bound: right sign, and within a modest factor
	 * of the true distance (measured against a sampled surface).
	 */
	for (i = 0; i < 12; i++)
	{
		gdouble ref;
		gfloat got;

		p[0] = rnd (-4.0f, 4.0f);
		p[1] = rnd (-2.0f, 2.0f);
		p[2] = rnd (-1.5f, 1.5f);
		ref = ellipsoid_brute (p, r);
		got = lrg_sdf3_ellipsoid (p, r);
		g_assert_true ((got < 0.0f) == (ref < 0.0));
		g_assert_cmpfloat (fabs (got), <=, 1.6 * fabs (ref) + 1e-2);
		g_assert_cmpfloat (fabs (got), >=, 0.4 * fabs (ref) - 1e-2);
	}
}

static void
test_sdf3_capsule (void)
{
	gfloat a[3] = { 0.0f, 0.0f, 0.0f };
	gfloat b[3] = { 0.0f, 4.0f, 0.0f };
	gfloat p[3] = { 3.0f, 2.0f, 0.0f };

	g_assert_cmpfloat_with_epsilon (lrg_sdf3_capsule (p, a, b, 1.0f), 2.0f, 1e-6f);
	p[0] = 0.0f; p[1] = 7.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_capsule (p, a, b, 1.0f), 2.0f, 1e-6f);
	p[1] = 2.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_capsule (p, a, b, 1.0f), -1.0f, 1e-6f);

	/* equal end points: a sphere */
	p[0] = 0.0f; p[1] = 3.0f; p[2] = 4.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_capsule (p, a, a, 1.0f), 4.0f, 1e-6f);
}

static void
test_sdf3_round_cone (void)
{
	gfloat p[3];
	gint i;

	/* below the bottom sphere and above the top one */
	p[0] = 0.0f; p[1] = -3.0f; p[2] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_cone (p, 1.0f, 0.5f, 2.0f), 2.0f, 1e-5f);
	p[1] = 5.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_cone (p, 1.0f, 0.5f, 2.0f), 2.5f, 1e-5f);

	/* equal radii: a capsule */
	for (i = 0; i < 100; i++)
	{
		gfloat a[3] = { 0.0f, 0.0f, 0.0f };
		gfloat b[3] = { 0.0f, 2.0f, 0.0f };

		p[0] = rnd (-3.0f, 3.0f);
		p[1] = rnd (-3.0f, 5.0f);
		p[2] = rnd (-3.0f, 3.0f);
		g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_cone (p, 0.7f, 0.7f, 2.0f),
		                                lrg_sdf3_capsule (p, a, b, 0.7f), 1e-5f);
	}

	/*
	 * Side: for r1 = 1, r2 = 0.5, h = 2 the tangent line in the (radial,
	 * y) half-plane has unit normal (cos, sin) with sin = 0.25; a point
	 * pushed out along it from the tangent point on the bottom circle
	 * is at the pushed distance.
	 */
	{
		gfloat sb = 0.25f;
		gfloat ca = sqrtf (1.0f - sb * sb);
		gfloat tx = ca * 1.0f;
		gfloat ty = sb * 1.0f;
		gfloat q[3];

		/* move along the tangent line a little, then out by 0.75 */
		tx += -sb * 0.5f;
		ty += ca * 0.5f;
		q[0] = tx + ca * 0.75f;
		q[1] = ty + sb * 0.75f;
		q[2] = 0.0f;
		g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_cone (q, 1.0f, 0.5f, 2.0f), 0.75f, 1e-5f);
	}

	/* one sphere swallows the other: the larger sphere */
	p[0] = 5.0f; p[1] = 0.0f; p[2] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_cone (p, 3.0f, 0.5f, 1.0f), 2.0f, 1e-5f);
	p[0] = 0.0f; p[1] = 5.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_round_cone (p, 0.5f, 3.0f, 1.0f), 1.0f, 1e-5f);
}

static void
test_sdf3_torus (void)
{
	gfloat t[2] = { 2.0f, 0.5f };
	gfloat p[3] = { 3.5f, 0.0f, 0.0f };

	g_assert_cmpfloat_with_epsilon (lrg_sdf3_torus (p, t), 1.0f, 1e-6f);
	p[0] = 0.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_torus (p, t), 1.5f, 1e-6f);
	p[0] = 0.0f; p[2] = -2.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_torus (p, t), -0.5f, 1e-6f);
	p[1] = 2.0f;
	g_assert_cmpfloat_with_epsilon (lrg_sdf3_torus (p, t), 1.5f, 1e-6f);
}

/* ==========================================================================
 * Operators
 * ========================================================================== */

static void
test_sdf_ops (void)
{
	gfloat x;

	g_assert_cmpfloat (lrg_sdf_union (1.0f, -2.0f), ==, -2.0f);
	g_assert_cmpfloat (lrg_sdf_intersect (1.0f, -2.0f), ==, 1.0f);
	g_assert_cmpfloat (lrg_sdf_subtract (-1.0f, -0.5f), ==, 0.5f);
	g_assert_cmpfloat (lrg_sdf_subtract (-1.0f, 2.0f), ==, -1.0f);
	g_assert_cmpfloat (lrg_sdf_round (1.0f, 0.25f), ==, 0.75f);
	g_assert_cmpfloat (lrg_sdf_onion (-1.0f, 0.25f), ==, 0.75f);
	g_assert_cmpfloat (lrg_sdf_onion (0.1f, 0.25f), <, 0.0f);

	/* smooth union: exact far apart, min - k / 4 where equal, never above min */
	g_assert_cmpfloat (lrg_sdf_smooth_union (1.0f, 3.0f, 0.5f), ==, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf_smooth_union (1.0f, 1.0f, 0.4f), 0.9f, 1e-6f);
	g_assert_cmpfloat (lrg_sdf_smooth_union (1.0f, 2.0f, 0.0f), ==, 1.0f);
	g_assert_cmpfloat (lrg_sdf_smooth_union (1.0f, 2.0f, -1.0f), ==, 1.0f);
	for (x = -2.0f; x <= 2.0f; x += 0.01f)
	{
		gfloat s = lrg_sdf_smooth_union (x, 0.0f, 1.0f);
		gfloat s2 = lrg_sdf_smooth_union (x + 0.01f, 0.0f, 1.0f);

		g_assert_cmpfloat (s, <=, MIN (x, 0.0f) + 1e-6f);
		g_assert_cmpfloat (s, >=, MIN (x, 0.0f) - 0.25f - 1e-6f);
		/* continuity (Lipschitz 1 in each argument) */
		g_assert_cmpfloat (fabsf (s2 - s), <=, 0.0101f);
	}

	/* smooth subtract / intersect: the mirror identities, exact far apart */
	g_assert_cmpfloat (lrg_sdf_smooth_subtract (-1.0f, 3.0f, 0.5f), ==, -1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf_smooth_subtract (0.3f, -0.7f, 0.2f),
	                                -lrg_sdf_smooth_union (-0.3f, -0.7f, 0.2f), 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf_smooth_subtract (1.0f, -1.0f, 0.4f), 1.1f, 1e-6f);
	g_assert_cmpfloat (lrg_sdf_smooth_intersect (1.0f, -3.0f, 0.5f), ==, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf_smooth_intersect (1.0f, 1.0f, 0.4f), 1.1f, 1e-6f);
	g_assert_cmpfloat (lrg_sdf_smooth_intersect (0.2f, 0.1f, 0.0f), ==, 0.2f);
}

/* ==========================================================================
 * Coverage
 * ========================================================================== */

static void
test_sdf2_coverage (void)
{
	g_assert_cmpfloat (lrg_sdf2_coverage (0.0f, 1.0f), ==, 0.5f);
	g_assert_cmpfloat (lrg_sdf2_coverage (-1.0f, 1.0f), ==, 1.0f);
	g_assert_cmpfloat (lrg_sdf2_coverage (1.0f, 1.0f), ==, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_coverage (0.25f, 1.0f), 0.25f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_sdf2_coverage (0.25f, 2.0f), 0.375f, 1e-6f);
	g_assert_cmpfloat (lrg_sdf2_coverage (-0.1f, 0.0f), ==, 1.0f);
	g_assert_cmpfloat (lrg_sdf2_coverage (0.1f, 0.0f), ==, 0.0f);
	g_assert_cmpfloat (lrg_sdf2_coverage (NAN, 1.0f), ==, 0.0f);
	g_assert_cmpfloat (lrg_sdf2_coverage (INFINITY, 1.0f), ==, 0.0f);
	g_assert_cmpfloat (lrg_sdf2_coverage (-INFINITY, 1.0f), ==, 1.0f);
	g_assert_cmpfloat (lrg_sdf2_coverage (0.1f, NAN), ==, 0.0f);
}

/* ==========================================================================
 * Scene helpers
 * ========================================================================== */

/* a unit sphere centred at the point in user_data (or the origin) */
static gfloat
scene_sphere (const gfloat p[3],
              gpointer     user_data)
{
	const gfloat *c = user_data;
	gfloat q[3];

	if (c == NULL)
		return lrg_sdf3_sphere (p, 1.0f);
	q[0] = p[0] - c[0];
	q[1] = p[1] - c[1];
	q[2] = p[2] - c[2];
	return lrg_sdf3_sphere (q, 1.0f);
}

/* a ground plane y = 0 and a sphere of radius 0.5 floating at (0, 1, 0) */
static gfloat
scene_ground_ball (const gfloat p[3],
                   gpointer     user_data)
{
	gfloat q[3];

	q[0] = p[0];
	q[1] = p[1] - 1.0f;
	q[2] = p[2];
	return lrg_sdf_union (p[1], lrg_sdf3_sphere (q, 0.5f));
}

/* an inside corner: floor y = 0 and wall x = 0 (solid at x < 0 and y < 0) */
static gfloat
scene_corner (const gfloat p[3],
              gpointer     user_data)
{
	return lrg_sdf_union (p[0], p[1]);
}

static gfloat
scene_constant (const gfloat p[3],
                gpointer     user_data)
{
	return 1.0f;
}

static gfloat
scene_nan (const gfloat p[3],
           gpointer     user_data)
{
	return NAN;
}

static void
test_sdf3_normal (void)
{
	gfloat centre[3] = { 1.0f, -2.0f, 0.5f };
	gfloat n[3];
	gint i;

	/* sphere: the normal is the direction from the centre */
	for (i = 0; i < 50; i++)
	{
		gfloat d[3];
		gfloat len;
		gfloat p[3];

		d[0] = rnd (-1.0f, 1.0f);
		d[1] = rnd (-1.0f, 1.0f);
		d[2] = rnd (-1.0f, 1.0f);
		len = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (len < 0.1f)
			continue;
		d[0] /= len; d[1] /= len; d[2] /= len;
		p[0] = centre[0] + d[0];
		p[1] = centre[1] + d[1];
		p[2] = centre[2] + d[2];
		lrg_sdf3_normal (scene_sphere, centre, p, 1e-3f, n);
		g_assert_cmpfloat_with_epsilon (n[0], d[0], 1e-3f);
		g_assert_cmpfloat_with_epsilon (n[1], d[1], 1e-3f);
		g_assert_cmpfloat_with_epsilon (n[2], d[2], 1e-3f);
	}

	/* ground plane, default eps */
	{
		gfloat p[3] = { 3.0f, 0.0f, -2.0f };

		lrg_sdf3_normal (scene_ground_ball, NULL, p, 0.0f, n);
		g_assert_cmpfloat_with_epsilon (n[0], 0.0f, 1e-4f);
		g_assert_cmpfloat_with_epsilon (n[1], 1.0f, 1e-4f);
		g_assert_cmpfloat_with_epsilon (n[2], 0.0f, 1e-4f);

		/* no gradient, or a NaN field: zero vector */
		lrg_sdf3_normal (scene_constant, NULL, p, 1e-3f, n);
		g_assert_cmpfloat (n[0], ==, 0.0f);
		g_assert_cmpfloat (n[1], ==, 0.0f);
		g_assert_cmpfloat (n[2], ==, 0.0f);
		lrg_sdf3_normal (scene_nan, NULL, p, 1e-3f, n);
		g_assert_cmpfloat (n[0], ==, 0.0f);
		g_assert_cmpfloat (n[1], ==, 0.0f);
		g_assert_cmpfloat (n[2], ==, 0.0f);
	}
}

static void
test_sdf3_raymarch (void)
{
	gfloat ro[3] = { 0.0f, 0.0f, -5.0f };
	gfloat rd[3] = { 0.0f, 0.0f, 1.0f };
	gfloat t;

	/* straight at a unit sphere: hit at 4 */
	g_assert_true (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));
	g_assert_cmpfloat_with_epsilon (t, 4.0f, 1e-3f);

	/* direction need not be unit length; out_t is optional */
	rd[2] = 3.0f;
	g_assert_true (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));
	g_assert_cmpfloat_with_epsilon (t, 4.0f, 1e-3f);
	g_assert_true (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, NULL));

	/* grazing ray off-axis: hit at 5 - sqrt (1 - 0.6^2) */
	ro[0] = 0.6f;
	g_assert_true (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 256, &t));
	g_assert_cmpfloat_with_epsilon (t, 5.0f - 0.8f, 1e-3f);
	ro[0] = 0.0f;

	/* too short a range, or a miss */
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 3.0f, 128, &t));
	g_assert_cmpfloat (t, <=, 3.0f);
	rd[0] = 0.0f; rd[1] = 1.0f; rd[2] = 0.0f;
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));

	/* starting inside hits at 0 */
	ro[2] = 0.0f;
	g_assert_true (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));
	g_assert_cmpfloat (t, ==, 0.0f);

	/* bad inputs never hit */
	ro[2] = -5.0f;
	rd[0] = rd[1] = rd[2] = 0.0f;
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));
	rd[2] = NAN;
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));
	rd[2] = 1.0f;
	ro[0] = INFINITY;
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 128, &t));
	ro[0] = 0.0f;
	g_assert_false (lrg_sdf3_raymarch (scene_nan, NULL, ro, rd, 100.0f, 128, &t));
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, NAN, 128, &t));
	/* a zero step budget is clamped to one step */
	g_assert_false (lrg_sdf3_raymarch (scene_sphere, NULL, ro, rd, 100.0f, 0, &t));
}

static void
test_sdf3_soft_shadow (void)
{
	gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	gfloat p[3];
	gfloat s_open;
	gfloat s_edge;
	gfloat s_far;
	gfloat s_hard;

	/* open sky */
	p[0] = 5.0f; p[1] = 0.0f; p[2] = 0.0f;
	s_open = lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 10.0f, 16.0f);
	g_assert_cmpfloat_with_epsilon (s_open, 1.0f, 1e-6f);

	/* straight under the ball: fully shadowed */
	p[0] = 0.0f;
	g_assert_cmpfloat (lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 10.0f, 16.0f),
	                   ==, 0.0f);

	/* just outside the ball's silhouette: penumbra, lighter further out */
	p[0] = 0.55f;
	s_edge = lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 10.0f, 16.0f);
	p[0] = 0.9f;
	s_far = lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 10.0f, 16.0f);
	g_assert_cmpfloat (s_edge, >, 0.0f);
	g_assert_cmpfloat (s_edge, <, 1.0f);
	g_assert_cmpfloat (s_far, >, s_edge);

	/* a harder k narrows the penumbra (brighter at the same point) */
	p[0] = 0.55f;
	s_hard = lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 10.0f, 64.0f);
	g_assert_cmpfloat (s_hard, >=, s_edge);

	/* the light closer than the ball: nothing in between */
	g_assert_cmpfloat (lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 0.3f, 16.0f),
	                   >, 0.9f);

	/* bad inputs: fully lit, no crash */
	up[1] = 0.0f;
	g_assert_cmpfloat (lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, 0.02f, 10.0f, 16.0f),
	                   ==, 1.0f);
	up[1] = 1.0f;
	g_assert_cmpfloat (lrg_sdf3_soft_shadow (scene_nan, NULL, p, up, 0.02f, 10.0f, 16.0f),
	                   ==, 1.0f);
	g_assert_cmpfloat (lrg_sdf3_soft_shadow (scene_ground_ball, NULL, p, up, NAN, 10.0f, 16.0f),
	                   ==, 1.0f);
}

static void
test_sdf3_ambient_occlusion (void)
{
	gfloat n[3] = { 0.0f, 1.0f, 0.0f };
	gfloat p[3] = { 5.0f, 0.0f, 0.0f };
	gfloat open;
	gfloat crevice;
	gfloat deeper;

	open = lrg_sdf3_ambient_occlusion (scene_corner, NULL, p, n);
	g_assert_cmpfloat_with_epsilon (open, 1.0f, 1e-5f);

	p[0] = 0.05f;
	crevice = lrg_sdf3_ambient_occlusion (scene_corner, NULL, p, n);
	p[0] = 0.01f;
	deeper = lrg_sdf3_ambient_occlusion (scene_corner, NULL, p, n);
	g_assert_cmpfloat (crevice, <, open);
	g_assert_cmpfloat (deeper, <, crevice);
	g_assert_cmpfloat (deeper, >=, 0.0f);

	/* a NaN field gives no occlusion rather than garbage */
	g_assert_cmpfloat (lrg_sdf3_ambient_occlusion (scene_nan, NULL, p, n), ==, 1.0f);
}

/* ==========================================================================
 * Non-finite inputs
 * ========================================================================== */

static void
test_sdf_non_finite (void)
{
	static const gfloat bad[] = { NAN, INFINITY, -INFINITY, 1e30f, -1e30f };
	guint i;

	/* none of these may crash; NaN in gives NaN (or a defined value) out */
	for (i = 0; i < G_N_ELEMENTS (bad); i++)
	{
		gfloat v = bad[i];
		gfloat p2[2] = { v, 0.5f };
		gfloat p3[3] = { 0.5f, v, 0.25f };
		gfloat ab[2] = { 2.0f, 1.0f };
		gfloat b2[2] = { 1.0f, 1.0f };
		gfloat b3[3] = { 1.0f, 1.0f, 1.0f };
		gfloat z2[2] = { 0.0f, 0.0f };
		gfloat o2[2] = { 1.0f, 0.0f };
		gfloat c2[2] = { 0.0f, 1.0f };
		gfloat z3[3] = { 0.0f, 0.0f, 0.0f };
		gfloat o3[3] = { 0.0f, 1.0f, 0.0f };
		gfloat t[2] = { 1.0f, 0.25f };
		gfloat bad_ab[2] = { v, v };
		gfloat sink;

		sink = lrg_sdf2_circle (p2, 1.0f);
		sink += lrg_sdf2_ellipse (p2, ab);
		sink += lrg_sdf2_ellipse (ab, bad_ab);
		sink += lrg_sdf2_box (p2, b2);
		sink += lrg_sdf2_round_box (p2, b2, v);
		sink += lrg_sdf2_segment (p2, z2, o2);
		sink += lrg_sdf2_capsule (p2, z2, o2, 0.1f);
		sink += lrg_sdf2_triangle (p2, z2, o2, c2);
		sink += lrg_sdf2_triangle (z2, p2, o2, c2);
		sink += lrg_sdf3_sphere (p3, 1.0f);
		sink += lrg_sdf3_ellipsoid (p3, b3);
		sink += lrg_sdf3_box (p3, b3);
		sink += lrg_sdf3_round_box (p3, b3, 0.2f);
		sink += lrg_sdf3_capsule (p3, z3, o3, 0.2f);
		sink += lrg_sdf3_round_cone (p3, 1.0f, 0.5f, 2.0f);
		sink += lrg_sdf3_round_cone (o3, 1.0f, 0.5f, v);
		sink += lrg_sdf3_torus (p3, t);
		sink += lrg_sdf_smooth_union (v, 1.0f, 0.5f);
		sink += lrg_sdf_smooth_subtract (v, 1.0f, v);
		sink += lrg_sdf_smooth_intersect (1.0f, v, 0.5f);
		sink += lrg_sdf2_coverage (v, 1.0f);
		(void)sink;

		/* where the result is defined it must be the exact limit */
		if (isnan (v))
		{
			g_assert_true (isnan (lrg_sdf2_circle (p2, 1.0f)));
			g_assert_true (isnan (lrg_sdf2_ellipse (p2, ab)));
			g_assert_true (isnan (lrg_sdf2_triangle (p2, z2, o2, c2)));
		}
		else if (isinf (v))
		{
			g_assert_true (isinf (lrg_sdf2_circle (p2, 1.0f)));
			g_assert_true (lrg_sdf3_box (p3, b3) > 1e30f);
		}
	}
}

/* ==========================================================================
 * lrg_image_canvas_fill_sdf()
 * ========================================================================== */

typedef struct
{
	gfloat cx;
	gfloat cy;
	gfloat r;
} Circle;

static gfloat
field_circle (const gfloat p[2],
              gpointer     user_data)
{
	const Circle *c = user_data;
	gfloat q[2];

	q[0] = p[0] - c->cx;
	q[1] = p[1] - c->cy;
	return lrg_sdf2_circle (q, c->r);
}

static gfloat
field_nan (const gfloat p[2],
           gpointer     user_data)
{
	return NAN;
}

static void
get_rgba (LrgImageCanvas *canvas,
          gint            x,
          gint            y,
          guint8          out[4])
{
	g_autoptr(GrlColor) c = grl_image_get_pixel (lrg_image_canvas_get_image (canvas), x, y);

	g_assert_nonnull (c);
	out[0] = c->r;
	out[1] = c->g;
	out[2] = c->b;
	out[3] = c->a;
}

static void
test_fill_sdf_coverage (void)
{
	g_autoptr(LrgImageCanvas) canvas = NULL;
	GrlColor bg = { 255, 0, 0, 255 };
	GrlColor white = { 255, 255, 255, 200 };
	Circle circle = { 16.0f, 16.0f, 7.3f };
	gint x;
	gint y;
	gint partial;
	gint full;
	gint outside;

	canvas = lrg_image_canvas_new (32, 32, &bg);
	g_assert_nonnull (canvas);

	/* REPLACE (the default): pixel = colour with alpha x coverage */
	lrg_image_canvas_fill_sdf (canvas, field_circle, &circle, &white);

	partial = full = outside = 0;
	for (y = 0; y < 32; y++)
	{
		for (x = 0; x < 32; x++)
		{
			gfloat p[2] = { x + 0.5f, y + 0.5f };
			gfloat cov = lrg_sdf2_coverage (field_circle (p, &circle), 1.0f);
			gint want = (gint)lrintf (200.0f * cov);
			guint8 px[4];

			get_rgba (canvas, x, y, px);
			if (want <= 0)
			{
				/* outside: untouched background */
				g_assert_cmpint (px[0], ==, 255);
				g_assert_cmpint (px[1], ==, 0);
				g_assert_cmpint (px[2], ==, 0);
				g_assert_cmpint (px[3], ==, 255);
				outside++;
			}
			else
			{
				g_assert_cmpint (px[0], ==, 255);
				g_assert_cmpint (px[1], ==, 255);
				g_assert_cmpint (px[2], ==, 255);
				g_assert_cmpint (px[3], ==, want);
				if (want == 200)
					full++;
				else
					partial++;
			}
		}
	}

	/* the area is right to within the edge ring, and the edge is partial */
	g_assert_cmpint (full, >, 0);
	g_assert_cmpint (partial, >, 20);
	g_assert_cmpint (outside, >, 0);
	{
		gfloat area = 0.0f;
		guint8 px[4];

		for (y = 0; y < 32; y++)
			for (x = 0; x < 32; x++)
			{
				get_rgba (canvas, x, y, px);
				if (px[1] == 255)
					area += px[3] / 200.0f;
			}
		g_assert_cmpfloat (fabsf (area - (gfloat)G_PI * 7.3f * 7.3f), <, 2.0f);
	}

	/* the centre is fully covered */
	{
		guint8 px[4];

		get_rgba (canvas, 16, 16, px);
		g_assert_cmpint (px[3], ==, 200);
	}
}

static void
test_fill_sdf_over_blend (void)
{
	g_autoptr(LrgImageCanvas) canvas = NULL;
	GrlColor black = { 0, 0, 0, 255 };
	GrlColor white = { 255, 255, 255, 255 };
	Circle circle = { 16.0f, 16.0f, 8.0f };
	gint x;
	gint grey = 0;
	guint8 px[4];

	canvas = lrg_image_canvas_new (32, 32, &black);
	lrg_image_canvas_set_blend_mode (canvas, GRL_IMAGE_BLEND_OVER);
	lrg_image_canvas_fill_sdf (canvas, field_circle, &circle, &white);

	/* interior white, far corner black, and grey steps across the edge */
	get_rgba (canvas, 16, 16, px);
	g_assert_cmpint (px[0], ==, 255);
	get_rgba (canvas, 0, 0, px);
	g_assert_cmpint (px[0], ==, 0);
	g_assert_cmpint (px[3], ==, 255);
	for (x = 0; x < 32; x++)
	{
		get_rgba (canvas, x, 20, px);
		g_assert_cmpint (px[3], ==, 255);	/* OVER on opaque stays opaque */
		if (px[0] > 0 && px[0] < 255)
			grey++;
	}
	g_assert_cmpint (grey, >=, 2);
}

static void
test_fill_sdf_rect_and_clip (void)
{
	g_autoptr(LrgImageCanvas) canvas = NULL;
	GrlColor clear = { 0, 0, 0, 0 };
	GrlColor white = { 255, 255, 255, 255 };
	GrlColor none = { 255, 255, 255, 0 };
	Circle circle = { 16.0f, 16.0f, 12.0f };
	GrlRectangle clip = { 20.0f, 0.0f, 12.0f, 32.0f };
	guint8 px[4];

	canvas = lrg_image_canvas_new (32, 32, &clear);

	/* only the left half is visited */
	lrg_image_canvas_fill_sdf_rect (canvas, 0, 0, 16, 32, field_circle, &circle, &white);
	get_rgba (canvas, 10, 16, px);
	g_assert_cmpint (px[3], ==, 255);
	get_rgba (canvas, 20, 16, px);
	g_assert_cmpint (px[3], ==, 0);

	/* the clip rectangle limits the full fill too */
	lrg_image_canvas_set_clip_rect (canvas, &clip);
	lrg_image_canvas_fill_sdf (canvas, field_circle, &circle, &white);
	get_rgba (canvas, 18, 16, px);
	g_assert_cmpint (px[3], ==, 0);
	get_rgba (canvas, 22, 16, px);
	g_assert_cmpint (px[3], ==, 255);
	lrg_image_canvas_set_clip_rect (canvas, NULL);

	/* off-image, empty, huge and NaN cases draw nothing and do not crash */
	lrg_image_canvas_fill_sdf_rect (canvas, -100, -100, 50, 50, field_circle, &circle, &white);
	lrg_image_canvas_fill_sdf_rect (canvas, 0, 0, 0, 10, field_circle, &circle, &white);
	lrg_image_canvas_fill_sdf_rect (canvas, -5, -5, G_MAXINT, G_MAXINT, field_nan, NULL, &white);
	lrg_image_canvas_fill_sdf (canvas, field_nan, NULL, &white);
	lrg_image_canvas_fill_sdf (canvas, field_circle, &circle, &none);
	get_rgba (canvas, 18, 16, px);
	g_assert_cmpint (px[3], ==, 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/sdf/2d/circle", test_sdf2_circle);
	g_test_add_func ("/sdf/2d/ellipse-brute-force", test_sdf2_ellipse_brute_force);
	g_test_add_func ("/sdf/2d/ellipse-special", test_sdf2_ellipse_special);
	g_test_add_func ("/sdf/2d/box", test_sdf2_box);
	g_test_add_func ("/sdf/2d/round-box", test_sdf2_round_box);
	g_test_add_func ("/sdf/2d/segment-capsule", test_sdf2_segment_capsule);
	g_test_add_func ("/sdf/2d/triangle", test_sdf2_triangle);
	g_test_add_func ("/sdf/2d/coverage", test_sdf2_coverage);
	g_test_add_func ("/sdf/3d/sphere-box", test_sdf3_sphere_box);
	g_test_add_func ("/sdf/3d/ellipsoid-bound", test_sdf3_ellipsoid_bound);
	g_test_add_func ("/sdf/3d/capsule", test_sdf3_capsule);
	g_test_add_func ("/sdf/3d/round-cone", test_sdf3_round_cone);
	g_test_add_func ("/sdf/3d/torus", test_sdf3_torus);
	g_test_add_func ("/sdf/ops", test_sdf_ops);
	g_test_add_func ("/sdf/scene/normal", test_sdf3_normal);
	g_test_add_func ("/sdf/scene/raymarch", test_sdf3_raymarch);
	g_test_add_func ("/sdf/scene/soft-shadow", test_sdf3_soft_shadow);
	g_test_add_func ("/sdf/scene/ambient-occlusion", test_sdf3_ambient_occlusion);
	g_test_add_func ("/sdf/non-finite", test_sdf_non_finite);
	g_test_add_func ("/sdf/canvas/fill-coverage", test_fill_sdf_coverage);
	g_test_add_func ("/sdf/canvas/fill-over-blend", test_fill_sdf_over_blend);
	g_test_add_func ("/sdf/canvas/fill-rect-and-clip", test_fill_sdf_rect_and_clip);

	return g_test_run ();
}
