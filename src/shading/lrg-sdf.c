/* lrg-sdf.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Signed distance functions, operators and SDF scene helpers.  The
 * formulations follow Inigo Quilez's articles on iquilezles.org
 * ("2D distance functions", "distance functions", "smooth minimum",
 * "soft shadows", "normals for an SDF", "ellipse distance").
 */

#include "config.h"

#include <math.h>

#include "lrg-sdf.h"

/* ==========================================================================
 * Small vector helpers (file local)
 * ========================================================================== */

static inline gfloat
clampf (gfloat v,
        gfloat lo,
        gfloat hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline gfloat
dot2 (const gfloat a[2],
      const gfloat b[2])
{
	return a[0] * b[0] + a[1] * b[1];
}

static inline gfloat
dot3 (const gfloat a[3],
      const gfloat b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * normalise3:
 * Copies @v into @out scaled to unit length.  Returns FALSE (and
 * leaves @out zero) for a zero-length or non-finite vector.
 */
static gboolean
normalise3 (const gfloat v[3],
            gfloat       out[3])
{
	gfloat len;

	len = sqrtf (dot3 (v, v));
	if (!isfinite (len) || len <= 0.0f)
	{
		out[0] = out[1] = out[2] = 0.0f;
		return FALSE;
	}
	out[0] = v[0] / len;
	out[1] = v[1] / len;
	out[2] = v[2] / len;
	return TRUE;
}

/* ==========================================================================
 * 2D primitives
 * ========================================================================== */

gfloat
lrg_sdf2_circle (const gfloat p[2],
                 gfloat       r)
{
	return hypotf (p[0], p[1]) - r;
}

/*
 * lrg_sdf2_ellipse:
 * Inigo Quilez's closed-form ellipse distance.  The problem is folded
 * into the first quadrant and arranged so that the major axis is y
 * (that keeps the quartic's discriminant well behaved); the cosine of
 * the closest point's parametric angle is then the real root of a
 * quartic, found either through the trigonometric form (three real
 * roots, d < 0) or Cardano's formula (one real root).  Everything runs
 * in double precision: the float version loses several digits close
 * to circular shapes.
 */
gfloat
lrg_sdf2_ellipse (const gfloat p[2],
                  const gfloat ab[2])
{
	gdouble px;
	gdouble py;
	gdouble a;
	gdouble b;
	gdouble tmp;
	gdouble l;
	gdouble m;
	gdouble m2;
	gdouble n;
	gdouble n2;
	gdouble c;
	gdouble c3;
	gdouble q;
	gdouble d;
	gdouble g;
	gdouble co;
	gdouble rx;
	gdouble ry;
	gdouble sy;

	/* fold into the first quadrant */
	px = fabs ((gdouble)p[0]);
	py = fabs ((gdouble)p[1]);
	a = (gdouble)ab[0];
	b = (gdouble)ab[1];

	if (isnan (px) || isnan (py) || isnan (a) || isnan (b))
		return NAN;

	/* degenerate ellipses: a flat segment along the remaining axis */
	if (a <= 0.0 || b <= 0.0)
	{
		gfloat seg_a[2];
		gfloat seg_b[2];
		gfloat q2[2];

		seg_a[0] = -MAX ((gfloat)a, 0.0f);
		seg_a[1] = -MAX ((gfloat)b, 0.0f);
		seg_b[0] = -seg_a[0];
		seg_b[1] = -seg_a[1];
		q2[0] = (gfloat)px;
		q2[1] = (gfloat)py;
		return lrg_sdf2_segment (q2, seg_a, seg_b);
	}

	/* (nearly) a circle: the quartic degenerates (l = 0) */
	if (fabs (a - b) <= 1e-7 * MAX (a, b))
		return (gfloat)(hypot (px, py) - 0.5 * (a + b));

	/* make y the major axis by swapping the coordinates if needed */
	if (px > py)
	{
		tmp = px; px = py; py = tmp;
		tmp = a; a = b; b = tmp;
	}

	l = b * b - a * a;
	m = a * px / l;
	m2 = m * m;
	n = b * py / l;
	n2 = n * n;
	c = (m2 + n2 - 1.0) / 3.0;
	c3 = c * c * c;
	q = c3 + m2 * n2 * 2.0;
	d = c3 + m2 * n2;
	g = m + m * n2;

	if (d < 0.0)
	{
		/* three real roots: trigonometric solution */
		gdouble h;
		gdouble s;
		gdouble t;
		gdouble r1;
		gdouble r2;

		h = acos (CLAMP (q / c3, -1.0, 1.0)) / 3.0;
		s = cos (h);
		t = sin (h) * sqrt (3.0);
		r1 = sqrt (MAX (-c * (s + t + 2.0) + m2, 0.0));
		r2 = sqrt (MAX (-c * (s - t + 2.0) + m2, 0.0));
		if (r1 * r2 > 0.0)
			co = (r2 + (l > 0.0 ? 1.0 : -1.0) * r1 + fabs (g) / (r1 * r2) - m) / 2.0;
		else
			co = 0.0;
	}
	else
	{
		/* one real root: Cardano */
		gdouble h;
		gdouble s;
		gdouble u;
		gdouble rm;

		h = 2.0 * m * n * sqrt (d);
		s = cbrt (q + h);
		u = cbrt (q - h);
		rx = -s - u - c * 4.0 + 2.0 * m2;
		ry = (s - u) * sqrt (3.0);
		rm = sqrt (rx * rx + ry * ry);
		if (rm - rx > 0.0 && rm > 0.0)
			co = (ry / sqrt (rm - rx) + 2.0 * g / rm - m) / 2.0;
		else
			co = 0.0;
	}

	/* closest point on the ellipse, then the signed distance to it */
	co = CLAMP (co, 0.0, 1.0);
	rx = a * co;
	ry = b * sqrt (1.0 - co * co);
	sy = py - ry;
	return (gfloat)(hypot (rx - px, ry - py) * (sy < 0.0 ? -1.0 : 1.0));
}

gfloat
lrg_sdf2_box (const gfloat p[2],
              const gfloat b[2])
{
	gfloat dx;
	gfloat dy;

	/* distance outside (Euclidean) plus distance inside (Chebyshev) */
	dx = fabsf (p[0]) - b[0];
	dy = fabsf (p[1]) - b[1];
	return hypotf (MAX (dx, 0.0f), MAX (dy, 0.0f)) + MIN (MAX (dx, dy), 0.0f);
}

gfloat
lrg_sdf2_round_box (const gfloat p[2],
                    const gfloat b[2],
                    gfloat       r)
{
	gfloat inner[2];

	/* shrink the box by r, then grow its distance by r */
	r = clampf (r, 0.0f, MAX (MIN (b[0], b[1]), 0.0f));
	inner[0] = b[0] - r;
	inner[1] = b[1] - r;
	return lrg_sdf2_box (p, inner) - r;
}

gfloat
lrg_sdf2_segment (const gfloat p[2],
                  const gfloat a[2],
                  const gfloat b[2])
{
	gfloat pa[2];
	gfloat ba[2];
	gfloat len2;
	gfloat h;

	/* project p onto the segment, clamped to the end points */
	pa[0] = p[0] - a[0];
	pa[1] = p[1] - a[1];
	ba[0] = b[0] - a[0];
	ba[1] = b[1] - a[1];
	len2 = dot2 (ba, ba);
	h = len2 > 0.0f ? clampf (dot2 (pa, ba) / len2, 0.0f, 1.0f) : 0.0f;
	return hypotf (pa[0] - ba[0] * h, pa[1] - ba[1] * h);
}

gfloat
lrg_sdf2_capsule (const gfloat p[2],
                  const gfloat a[2],
                  const gfloat b[2],
                  gfloat       r)
{
	return lrg_sdf2_segment (p, a, b) - r;
}

/*
 * lrg_sdf2_triangle:
 * For each edge: the squared distance to the edge (projected,
 * clamped) and the signed area of p against the edge, oriented by the
 * triangle's winding.  The smallest squared distance is the distance;
 * the point is inside when all three signed areas are positive, which
 * the component-wise minimum of the areas tells.
 */
gfloat
lrg_sdf2_triangle (const gfloat p[2],
                   const gfloat a[2],
                   const gfloat b[2],
                   const gfloat c[2])
{
	const gfloat *corner[3];
	gfloat e[3][2];
	gfloat best_d2;
	gfloat best_s;
	gfloat s;
	gfloat winding;
	gint i;

	corner[0] = a;
	corner[1] = b;
	corner[2] = c;
	for (i = 0; i < 3; i++)
	{
		e[i][0] = corner[(i + 1) % 3][0] - corner[i][0];
		e[i][1] = corner[(i + 1) % 3][1] - corner[i][1];
	}

	/* the sign of the winding orients the areas; 0 when degenerate */
	winding = e[0][0] * e[2][1] - e[0][1] * e[2][0];
	s = winding > 0.0f ? 1.0f : (winding < 0.0f ? -1.0f : 0.0f);

	best_d2 = G_MAXFLOAT;
	best_s = G_MAXFLOAT;
	for (i = 0; i < 3; i++)
	{
		gfloat v[2];
		gfloat len2;
		gfloat h;
		gfloat qx;
		gfloat qy;
		gfloat d2;
		gfloat area;

		v[0] = p[0] - corner[i][0];
		v[1] = p[1] - corner[i][1];
		len2 = dot2 (e[i], e[i]);
		h = len2 > 0.0f ? clampf (dot2 (v, e[i]) / len2, 0.0f, 1.0f) : 0.0f;
		qx = v[0] - e[i][0] * h;
		qy = v[1] - e[i][1] * h;
		d2 = qx * qx + qy * qy;
		area = s * (v[0] * e[i][1] - v[1] * e[i][0]);
		best_d2 = MIN (best_d2, d2);
		best_s = MIN (best_s, area);
	}

	if (isnan (p[0]) || isnan (p[1]) || isnan (best_d2) || isnan (best_s))
		return NAN;

	/* inside exactly when every oriented area is positive (see above) */
	return best_s > 0.0f && s != 0.0f ? -sqrtf (best_d2) : sqrtf (best_d2);
}

/* ==========================================================================
 * 3D primitives
 * ========================================================================== */

gfloat
lrg_sdf3_sphere (const gfloat p[3],
                 gfloat       r)
{
	return sqrtf (dot3 (p, p)) - r;
}

gfloat
lrg_sdf3_ellipsoid (const gfloat p[3],
                    const gfloat r[3])
{
	gfloat rr[3];
	gfloat q0[3];
	gfloat q1[3];
	gfloat k0;
	gfloat k1;
	gint i;

	/*
	 * k0 = |p / r| is 1 on the surface; k1 = |p / r^2| is the gradient
	 * length of k0 scaled by k0; k0 (k0 - 1) / k1 is the first order
	 * distance estimate.  At the centre k1 is 0: return minus the
	 * smallest semi-axis (the exact value there).
	 */
	for (i = 0; i < 3; i++)
	{
		rr[i] = MAX (r[i], 1e-6f);
		q0[i] = p[i] / rr[i];
		q1[i] = p[i] / (rr[i] * rr[i]);
	}
	k0 = sqrtf (dot3 (q0, q0));
	k1 = sqrtf (dot3 (q1, q1));
	if (k1 <= 0.0f)
		return -MIN (rr[0], MIN (rr[1], rr[2]));
	return k0 * (k0 - 1.0f) / k1;
}

gfloat
lrg_sdf3_box (const gfloat p[3],
              const gfloat b[3])
{
	gfloat q[3];
	gfloat o[3];
	gint i;

	for (i = 0; i < 3; i++)
	{
		q[i] = fabsf (p[i]) - b[i];
		o[i] = MAX (q[i], 0.0f);
	}
	return sqrtf (dot3 (o, o)) + MIN (MAX (q[0], MAX (q[1], q[2])), 0.0f);
}

gfloat
lrg_sdf3_round_box (const gfloat p[3],
                    const gfloat b[3],
                    gfloat       r)
{
	gfloat inner[3];

	r = clampf (r, 0.0f, MAX (MIN (b[0], MIN (b[1], b[2])), 0.0f));
	inner[0] = b[0] - r;
	inner[1] = b[1] - r;
	inner[2] = b[2] - r;
	return lrg_sdf3_box (p, inner) - r;
}

gfloat
lrg_sdf3_capsule (const gfloat p[3],
                  const gfloat a[3],
                  const gfloat b[3],
                  gfloat       r)
{
	gfloat pa[3];
	gfloat ba[3];
	gfloat d[3];
	gfloat len2;
	gfloat h;
	gint i;

	for (i = 0; i < 3; i++)
	{
		pa[i] = p[i] - a[i];
		ba[i] = b[i] - a[i];
	}
	len2 = dot3 (ba, ba);
	h = len2 > 0.0f ? clampf (dot3 (pa, ba) / len2, 0.0f, 1.0f) : 0.0f;
	for (i = 0; i < 3; i++)
		d[i] = pa[i] - ba[i] * h;
	return sqrtf (dot3 (d, d)) - r;
}

/*
 * lrg_sdf3_round_cone:
 * Works in the 2D half-plane (radial distance, height).  The side of
 * the cone is tangent to both circles; with sin = (r1 - r2) / h and
 * cos = sqrt (1 - sin^2), k = q . (-sin, cos) says whether q projects
 * below the bottom tangent point (bottom sphere), above the top one
 * (top sphere) or onto the side (distance to the tangent line).
 */
gfloat
lrg_sdf3_round_cone (const gfloat p[3],
                     gfloat       r1,
                     gfloat       r2,
                     gfloat       h)
{
	gfloat qx;
	gfloat qy;
	gfloat sb;
	gfloat ca;
	gfloat k;

	qx = hypotf (p[0], p[2]);
	qy = p[1];

	/* one sphere swallows the other (or no height): the larger sphere */
	if (!(h > fabsf (r1 - r2)))
		return MIN (hypotf (qx, qy) - r1, hypotf (qx, qy - MAX (h, 0.0f)) - r2);

	sb = (r1 - r2) / h;
	ca = sqrtf (MAX (1.0f - sb * sb, 0.0f));
	k = -sb * qx + ca * qy;
	if (k < 0.0f)
		return hypotf (qx, qy) - r1;
	if (k > ca * h)
		return hypotf (qx, qy - h) - r2;
	return qx * ca + qy * sb - r1;
}

gfloat
lrg_sdf3_torus (const gfloat p[3],
                const gfloat t[2])
{
	return hypotf (hypotf (p[0], p[2]) - t[0], p[1]) - t[1];
}

/* ==========================================================================
 * Operators
 * ========================================================================== */

gfloat
lrg_sdf_union (gfloat d1,
               gfloat d2)
{
	return MIN (d1, d2);
}

gfloat
lrg_sdf_subtract (gfloat d1,
                  gfloat d2)
{
	return MAX (d1, -d2);
}

gfloat
lrg_sdf_intersect (gfloat d1,
                   gfloat d2)
{
	return MAX (d1, d2);
}

gfloat
lrg_sdf_smooth_union (gfloat d1,
                      gfloat d2,
                      gfloat k)
{
	gfloat h;

	/* quadratic polynomial smooth minimum; k <= 0 is the hard min */
	if (!(k > 0.0f))
		return MIN (d1, d2);
	h = MAX (k - fabsf (d1 - d2), 0.0f) / k;
	return MIN (d1, d2) - h * h * k * 0.25f;
}

gfloat
lrg_sdf_smooth_subtract (gfloat d1,
                         gfloat d2,
                         gfloat k)
{
	return -lrg_sdf_smooth_union (-d1, d2, k);
}

gfloat
lrg_sdf_smooth_intersect (gfloat d1,
                          gfloat d2,
                          gfloat k)
{
	return -lrg_sdf_smooth_union (-d1, -d2, k);
}

gfloat
lrg_sdf_round (gfloat d,
               gfloat r)
{
	return d - r;
}

gfloat
lrg_sdf_onion (gfloat d,
               gfloat t)
{
	return fabsf (d) - t;
}

/* ==========================================================================
 * Scene helpers
 * ========================================================================== */

gfloat
lrg_sdf2_coverage (gfloat d,
                   gfloat pixel_width)
{
	if (isnan (d))
		return 0.0f;
	if (!(pixel_width > 0.0f))
		return d <= 0.0f ? 1.0f : 0.0f;
	return clampf (0.5f - d / pixel_width, 0.0f, 1.0f);
}

void
lrg_sdf3_normal (LrgSdf3Func  func,
                 gpointer     user_data,
                 const gfloat p[3],
                 gfloat       eps,
                 gfloat       out[3])
{
	/* the four corners of a tetrahedron inscribed in the unit cube */
	static const gfloat corners[4][3] = {
		{  1.0f, -1.0f, -1.0f },
		{ -1.0f, -1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f },
		{  1.0f,  1.0f,  1.0f }
	};
	gfloat g[3];
	gint i;

	g_return_if_fail (func != NULL);
	g_return_if_fail (p != NULL);
	g_return_if_fail (out != NULL);

	if (!(eps > 0.0f))
		eps = 1e-3f;

	/*
	 * sum over the corners of corner * f (p + eps * corner): the
	 * constant and the even terms cancel, leaving 4 eps * gradient.
	 */
	g[0] = g[1] = g[2] = 0.0f;
	for (i = 0; i < 4; i++)
	{
		gfloat q[3];
		gfloat d;

		q[0] = p[0] + eps * corners[i][0];
		q[1] = p[1] + eps * corners[i][1];
		q[2] = p[2] + eps * corners[i][2];
		d = func (q, user_data);
		g[0] += corners[i][0] * d;
		g[1] += corners[i][1] * d;
		g[2] += corners[i][2] * d;
	}
	normalise3 (g, out);
}

gboolean
lrg_sdf3_raymarch (LrgSdf3Func  func,
                   gpointer     user_data,
                   const gfloat ro[3],
                   const gfloat rd[3],
                   gfloat       tmax,
                   gint         max_steps,
                   gfloat      *out_t)
{
	gfloat dir[3];
	gfloat t;
	gint i;

	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (ro != NULL, FALSE);
	g_return_val_if_fail (rd != NULL, FALSE);

	t = 0.0f;
	if (out_t != NULL)
		*out_t = 0.0f;
	if (!normalise3 (rd, dir) || !isfinite (ro[0]) || !isfinite (ro[1]) ||
	    !isfinite (ro[2]) || isnan (tmax))
		return FALSE;
	max_steps = CLAMP (max_steps, 1, 100000);

	/* sphere tracing: the reported distance is a safe step */
	for (i = 0; i < max_steps && t <= tmax; i++)
	{
		gfloat q[3];
		gfloat d;

		q[0] = ro[0] + dir[0] * t;
		q[1] = ro[1] + dir[1] * t;
		q[2] = ro[2] + dir[2] * t;
		d = func (q, user_data);
		if (isnan (d))
			break;
		if (d < 1e-4f * MAX (1.0f, t))
		{
			if (out_t != NULL)
				*out_t = t;
			return TRUE;
		}
		t += d;
	}

	if (out_t != NULL)
		*out_t = MIN (t, tmax);
	return FALSE;
}

gfloat
lrg_sdf3_soft_shadow (LrgSdf3Func  func,
                      gpointer     user_data,
                      const gfloat ro[3],
                      const gfloat rd[3],
                      gfloat       tmin,
                      gfloat       tmax,
                      gfloat       k)
{
	gfloat dir[3];
	gfloat res;
	gfloat t;
	gfloat ph;
	gint i;

	g_return_val_if_fail (func != NULL, 1.0f);
	g_return_val_if_fail (ro != NULL, 1.0f);
	g_return_val_if_fail (rd != NULL, 1.0f);

	if (!normalise3 (rd, dir) || isnan (tmin) || isnan (tmax) || isnan (k))
		return 1.0f;

	res = 1.0f;
	tmin = MAX (tmin, 0.0f);
	t = tmin;
	ph = 1e20f;
	for (i = 0; i < 256 && t < tmax; i++)
	{
		gfloat q[3];
		gfloat h;
		gfloat y;
		gfloat d;

		q[0] = ro[0] + dir[0] * t;
		q[1] = ro[1] + dir[1] * t;
		q[2] = ro[2] + dir[2] * t;
		h = func (q, user_data);
		if (isnan (h))
			break;
		if (h < 1e-4f)
			return 0.0f;

		/*
		 * The nearest occluder lies on the intersection of the
		 * spheres of radius ph (previous sample) and h (this one); y
		 * is how far back along the ray its closest approach is and
		 * d its distance from the ray.  k d / (t - y) is the angular
		 * size of the gap, scaled by the hardness.  An occluder placed
		 * before tmin is the surface the ray starts from (a ray
		 * leaving a plane triangulates to its own origin): there the
		 * plain estimate k h / t is used instead.  So is a step where
		 * the distance grew by more than twice the previous one
		 * (y >= h): that cannot happen in an exact field, but it does
		 * in the estimates most scenes use (ellipsoids, smooth blends,
		 * displacement, bounding-volume shortcuts), and the
		 * triangulation would then give d = 0, a false full shadow.
		 */
		y = h * h / (2.0f * ph);
		if (t - y > tmin && y < h)
		{
			d = sqrtf (MAX (h * h - y * y, 0.0f));
			res = MIN (res, k * d / (t - y));
		}
		else
			res = MIN (res, k * h / MAX (t, 1e-6f));
		ph = h;
		t += MAX (h, 1e-4f * (1.0f + t));
	}
	return clampf (res, 0.0f, 1.0f);
}

gfloat
lrg_sdf3_ambient_occlusion (LrgSdf3Func  func,
                            gpointer     user_data,
                            const gfloat p[3],
                            const gfloat n[3])
{
	gfloat occ;
	gfloat sca;
	gint i;

	g_return_val_if_fail (func != NULL, 1.0f);
	g_return_val_if_fail (p != NULL, 1.0f);
	g_return_val_if_fail (n != NULL, 1.0f);

	/*
	 * At height h above an open surface the distance is h; any
	 * shortfall means nearby geometry.  Closer samples weigh more.
	 */
	occ = 0.0f;
	sca = 1.0f;
	for (i = 0; i < 5; i++)
	{
		gfloat q[3];
		gfloat h;
		gfloat d;

		h = 0.01f + 0.12f * (gfloat)i / 4.0f;
		q[0] = p[0] + n[0] * h;
		q[1] = p[1] + n[1] * h;
		q[2] = p[2] + n[2] * h;
		d = func (q, user_data);
		if (isnan (d))
			continue;
		occ += (h - d) * sca;
		sca *= 0.95f;
	}
	return clampf (1.0f - 3.0f * occ, 0.0f, 1.0f);
}
