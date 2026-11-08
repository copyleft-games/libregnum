/* lrg-collide.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe:
 * the closest-point-on-simplex routines (Geometry/ClosestPoint.h), the
 * face clipping (Geometry/ClipPoly.h), the manifold construction and
 * contact reduction (Physics/Collision/ManifoldBetweenTwoFaces.cpp) and the
 * inactive-edge normal fix (Physics/Collision/ActiveEdges.h), translated to
 * GLib C.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions: The above copyright notice and this
 *   permission notice shall be included in all copies or substantial
 *   portions of the Software.  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
 *   WARRANTY OF ANY KIND.
 *
 * Narrowphase: GJK (closest points), EPA (penetration depth), specialised
 * sphere / capsule / box pairs, contact manifolds by face clipping with
 * feature ids, convex vs plane, convex vs triangle mesh / heightfield with
 * internal-edge removal, compounds, world ray casts and conservative
 * advancement shape casts.
 *
 * Everything runs in double precision in a frame shifted to shape A's
 * position, so world coordinates far from the origin keep their accuracy.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-collide.h"
#include "lrg-physics-shape-private.h"
#include "../core/lrg-math3d.h"

/* ==========================================================================
 * Double vector helpers
 * ========================================================================== */

static inline void
d3_set (gdouble *o, gdouble x, gdouble y, gdouble z)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline void
d3_copy (gdouble *o, const gdouble *a)
{
	o[0] = a[0];
	o[1] = a[1];
	o[2] = a[2];
}

static inline void
d3_sub (gdouble *o, const gdouble *a, const gdouble *b)
{
	o[0] = a[0] - b[0];
	o[1] = a[1] - b[1];
	o[2] = a[2] - b[2];
}

static inline void
d3_add (gdouble *o, const gdouble *a, const gdouble *b)
{
	o[0] = a[0] + b[0];
	o[1] = a[1] + b[1];
	o[2] = a[2] + b[2];
}

static inline void
d3_madd (gdouble *o, const gdouble *a, const gdouble *b, gdouble s)
{
	o[0] = a[0] + b[0] * s;
	o[1] = a[1] + b[1] * s;
	o[2] = a[2] + b[2] * s;
}

static inline void
d3_scale (gdouble *o, const gdouble *a, gdouble s)
{
	o[0] = a[0] * s;
	o[1] = a[1] * s;
	o[2] = a[2] * s;
}

static inline gdouble
d3_dot (const gdouble *a, const gdouble *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
d3_cross (gdouble *o, const gdouble *a, const gdouble *b)
{
	gdouble x;
	gdouble y;
	gdouble z;

	x = a[1] * b[2] - a[2] * b[1];
	y = a[2] * b[0] - a[0] * b[2];
	z = a[0] * b[1] - a[1] * b[0];
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline gdouble
d3_len (const gdouble *a)
{
	return sqrt (d3_dot (a, a));
}

static inline gdouble
d3_normalize (gdouble *o, const gdouble *a)
{
	gdouble l;

	l = d3_len (a);
	if (l < 1e-30)
	{
		d3_set (o, 0.0, 1.0, 0.0);
		return 0.0;
	}
	d3_scale (o, a, 1.0 / l);
	return l;
}

/* Any unit vector perpendicular to unit @a. */
static void
d3_perp (gdouble *o, const gdouble *a)
{
	gdouble t[3];

	if (fabs (a[0]) < 0.57)
		d3_set (t, 1.0, 0.0, 0.0);
	else
		d3_set (t, 0.0, 1.0, 0.0);
	d3_cross (o, a, t);
	d3_normalize (o, o);
}

/* ==========================================================================
 * LrgContactManifold
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgContactManifold, lrg_contact_manifold,
                     lrg_contact_manifold_copy, lrg_contact_manifold_free)

LrgContactManifold *
lrg_contact_manifold_new (void)
{
	LrgContactManifold *m;

	m = g_new0 (LrgContactManifold, 1);
	lrg_contact_manifold_reset (m);
	return m;
}

LrgContactManifold *
lrg_contact_manifold_copy (const LrgContactManifold *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (LrgContactManifold *)g_memdup2 (self, sizeof (LrgContactManifold));
}

void
lrg_contact_manifold_free (LrgContactManifold *self)
{
	g_free (self);
}

void
lrg_contact_manifold_reset (LrgContactManifold *self)
{
	g_return_if_fail (self != NULL);
	memset (self, 0, sizeof (LrgContactManifold));
	self->normal[1] = 1.0f;
	self->child_a = -1;
	self->child_b = -1;
	self->sub_shape_a = LRG_PHYSICS_NO_SUB_SHAPE;
	self->sub_shape_b = LRG_PHYSICS_NO_SUB_SHAPE;
}

gboolean
lrg_contact_manifold_get_point (const LrgContactManifold *self,
                                guint                     index,
                                gfloat                   *point_a,
                                gfloat                   *point_b,
                                gfloat                   *depth,
                                guint32                  *feature_id)
{
	g_return_val_if_fail (self != NULL, FALSE);
	if (index >= self->n_points)
		return FALSE;
	if (point_a != NULL)
		memcpy (point_a, &self->points_a[index * 3], sizeof (gfloat) * 3);
	if (point_b != NULL)
		memcpy (point_b, &self->points_b[index * 3], sizeof (gfloat) * 3);
	if (depth != NULL)
		*depth = self->depths[index];
	if (feature_id != NULL)
		*feature_id = self->feature_ids[index];
	return TRUE;
}

/* ==========================================================================
 * Proxies: a convex shape (or a triangle) placed in the working frame
 * ========================================================================== */

typedef struct
{
	LrgPhysicsShape *shape;     /* NULL: triangle in tri[] */
	gdouble          R[9];      /* row-major rotation, world = R * local + t */
	gdouble          t[3];
	gdouble          tri[9];
	gdouble          radius;    /* core radius */
	guint            tri_id;
} Proxy;

/* Places @shape with the column-major float matrix @xf, shifted by @shift. */
static void
proxy_init_shape (Proxy           *p,
                  LrgPhysicsShape *shape,
                  const gfloat    *xf,
                  const gdouble   *shift)
{
	guint r;
	guint c;

	memset (p, 0, sizeof (*p));
	p->shape = shape;
	for (r = 0; r < 3; r++)
		for (c = 0; c < 3; c++)
			p->R[r * 3 + c] = (gdouble)xf[c * 4 + r];
	for (r = 0; r < 3; r++)
		p->t[r] = (gdouble)xf[12 + r] - shift[r];
	p->radius = (gdouble)lrg_physics_shape_core_radius (shape);
}

static void
proxy_init_tri (Proxy         *p,
                const gdouble *v,
                guint          id)
{
	memset (p, 0, sizeof (*p));
	memcpy (p->tri, v, sizeof (gdouble) * 9);
	p->R[0] = 1.0;
	p->R[4] = 1.0;
	p->R[8] = 1.0;
	p->t[0] = (v[0] + v[3] + v[6]) / 3.0;
	p->t[1] = (v[1] + v[4] + v[7]) / 3.0;
	p->t[2] = (v[2] + v[5] + v[8]) / 3.0;
	p->tri_id = id;
}

static void
proxy_to_world (const Proxy  *p,
                const gfloat *l,
                gdouble      *w)
{
	guint r;

	for (r = 0; r < 3; r++)
		w[r] = p->R[r * 3 + 0] * l[0] + p->R[r * 3 + 1] * l[1] + p->R[r * 3 + 2] * l[2] + p->t[r];
}

static void
proxy_dir_to_local (const Proxy   *p,
                    const gdouble *d,
                    gfloat        *l)
{
	guint c;

	for (c = 0; c < 3; c++)
		l[c] = (gfloat)(p->R[0 * 3 + c] * d[0] + p->R[1 * 3 + c] * d[1] + p->R[2 * 3 + c] * d[2]);
}

/* Support point of the core (no convex radius) along world direction d. */
static void
proxy_support_core (const Proxy   *p,
                    const gdouble *d,
                    gdouble       *out)
{
	if (p->shape == NULL)
	{
		gdouble d0;
		gdouble d1;
		gdouble d2;

		d0 = d3_dot (&p->tri[0], d);
		d1 = d3_dot (&p->tri[3], d);
		d2 = d3_dot (&p->tri[6], d);
		if (d0 >= d1 && d0 >= d2)
			d3_copy (out, &p->tri[0]);
		else if (d1 >= d2)
			d3_copy (out, &p->tri[3]);
		else
			d3_copy (out, &p->tri[6]);
		return;
	}
	{
		gfloat l[3];
		gfloat s[3];

		proxy_dir_to_local (p, d, l);
		lrg_physics_shape_support_core (p->shape, l, s);
		proxy_to_world (p, s, out);
	}
}

/* Support of the full shape (core + radius). */
static void
proxy_support_full (const Proxy   *p,
                    const gdouble *d,
                    gdouble       *out)
{
	proxy_support_core (p, d, out);
	if (p->radius > 0.0)
	{
		gdouble n[3];

		d3_normalize (n, d);
		d3_madd (out, out, n, p->radius);
	}
}

/* Supporting face of the full shape along world direction d. */
static guint
proxy_face (const Proxy   *p,
            const gdouble *d,
            gdouble       *out,
            guint         *face_id)
{
	if (p->shape == NULL)
	{
		gdouble e1[3];
		gdouble e2[3];
		gdouble n[3];

		/* The triangle, wound counter-clockwise about d. */
		d3_sub (e1, &p->tri[3], &p->tri[0]);
		d3_sub (e2, &p->tri[6], &p->tri[0]);
		d3_cross (n, e1, e2);
		d3_copy (&out[0], &p->tri[0]);
		if (d3_dot (n, d) >= 0.0)
		{
			d3_copy (&out[3], &p->tri[3]);
			d3_copy (&out[6], &p->tri[6]);
		}
		else
		{
			d3_copy (&out[3], &p->tri[6]);
			d3_copy (&out[6], &p->tri[3]);
		}
		*face_id = 0;
		return 3;
	}
	{
		gfloat l[3];
		gfloat f[LRG_SHAPE_MAX_FACE_POINTS * 3];
		guint n;
		guint i;

		proxy_dir_to_local (p, d, l);
		n = lrg_physics_shape_supporting_face (p->shape, l, f, face_id);
		for (i = 0; i < n; i++)
			proxy_to_world (p, &f[i * 3], &out[i * 3]);
		return n;
	}
}

/* ==========================================================================
 * GJK (closest points of two convex sets)
 * ========================================================================== */

typedef struct
{
	gdouble w[4][3];   /* support point of the Minkowski difference A - B */
	gdouble a[4][3];   /* support point on A */
	gdouble b[4][3];   /* support point on B */
	gdouble l[4];      /* barycentric weights of the closest point */
	guint   n;
} Simplex;

/* Barycentric coordinates of the origin projected on segment ab (port of
 * ClosestPoint::GetBaryCentricCoordinates, 2 points). */
static void
bary2 (const gdouble *a,
       const gdouble *b,
       gdouble       *u,
       gdouble       *v)
{
	gdouble ab[3];
	gdouble den;

	d3_sub (ab, b, a);
	den = d3_dot (ab, ab);
	if (den < 1e-30)
	{
		if (d3_dot (a, a) < d3_dot (b, b))
		{
			*u = 1.0;
			*v = 0.0;
		}
		else
		{
			*u = 0.0;
			*v = 1.0;
		}
		return;
	}
	*v = -d3_dot (a, ab) / den;
	*u = 1.0 - *v;
}

/* Barycentric coordinates of the origin projected on triangle abc (port of
 * the 3-point ClosestPoint::GetBaryCentricCoordinates). */
static void
bary3 (const gdouble *a,
       const gdouble *b,
       const gdouble *c,
       gdouble       *u,
       gdouble       *v,
       gdouble       *w)
{
	gdouble v0[3];
	gdouble v1[3];
	gdouble v2[3];
	gdouble d00;
	gdouble d11;
	gdouble d22;

	d3_sub (v0, b, a);
	d3_sub (v1, c, a);
	d3_sub (v2, c, b);
	d00 = d3_dot (v0, v0);
	d11 = d3_dot (v1, v1);
	d22 = d3_dot (v2, v2);
	if (d00 <= d22)
	{
		gdouble d01;
		gdouble den;

		d01 = d3_dot (v0, v1);
		den = d00 * d11 - d01 * d01;
		if (den <= 1e-12 * d00 * d11)
		{
			if (d00 > d11)
			{
				bary2 (a, b, u, v);
				*w = 0.0;
			}
			else
			{
				bary2 (a, c, u, w);
				*v = 0.0;
			}
			return;
		}
		{
			gdouble a0;
			gdouble a1;

			a0 = d3_dot (a, v0);
			a1 = d3_dot (a, v1);
			*v = (d01 * a1 - d11 * a0) / den;
			*w = (d01 * a0 - d00 * a1) / den;
			*u = 1.0 - *v - *w;
		}
	}
	else
	{
		gdouble d12;
		gdouble den;

		d12 = d3_dot (v1, v2);
		den = d11 * d22 - d12 * d12;
		if (den <= 1e-12 * d11 * d22)
		{
			if (d11 > d22)
			{
				bary2 (a, c, u, w);
				*v = 0.0;
			}
			else
			{
				bary2 (b, c, v, w);
				*u = 0.0;
			}
			return;
		}
		{
			gdouble c1;
			gdouble c2;

			c1 = d3_dot (c, v1);
			c2 = d3_dot (c, v2);
			*u = (d22 * c1 - d12 * c2) / den;
			*v = (d11 * c2 - d12 * c1) / den;
			*w = 1.0 - *u - *v;
		}
	}
}

/* Closest point to the origin on triangle abc as weights (la, lb, lc);
 * Voronoi-region walk (Ericson 5.1.5, as in Jolt's
 * GetClosestPointOnTriangle).  Returns the mask of used vertices. */
static guint
closest_on_triangle (const gdouble *a,
                     const gdouble *b,
                     const gdouble *c,
                     gdouble       *l)
{
	gdouble ab[3];
	gdouble ac[3];
	gdouble ap[3];
	gdouble bp[3];
	gdouble cp[3];
	gdouble d1;
	gdouble d2;
	gdouble d3;
	gdouble d4;
	gdouble d5;
	gdouble d6;
	gdouble va;
	gdouble vb;
	gdouble vc;
	gdouble n[3];

	d3_sub (ab, b, a);
	d3_sub (ac, c, a);
	d3_cross (n, ab, ac);
	if (d3_dot (n, n) < 1e-24)
	{
		/* Degenerate: best of the three edges. */
		gdouble best;
		guint mask;
		gdouble u;
		gdouble v;
		gdouble q[3];
		gdouble dd;

		l[0] = l[1] = l[2] = 0.0;
		bary2 (a, b, &u, &v);
		u = CLAMP (u, 0.0, 1.0);
		v = 1.0 - u;
		d3_scale (q, a, u);
		d3_madd (q, q, b, v);
		best = d3_dot (q, q);
		l[0] = u;
		l[1] = v;
		mask = 3;
		bary2 (a, c, &u, &v);
		u = CLAMP (u, 0.0, 1.0);
		v = 1.0 - u;
		d3_scale (q, a, u);
		d3_madd (q, q, c, v);
		dd = d3_dot (q, q);
		if (dd < best)
		{
			best = dd;
			l[0] = u;
			l[1] = 0.0;
			l[2] = v;
			mask = 5;
		}
		bary2 (b, c, &u, &v);
		u = CLAMP (u, 0.0, 1.0);
		v = 1.0 - u;
		d3_scale (q, b, u);
		d3_madd (q, q, c, v);
		dd = d3_dot (q, q);
		if (dd < best)
		{
			l[0] = 0.0;
			l[1] = u;
			l[2] = v;
			mask = 6;
		}
		return mask;
	}

	d3_scale (ap, a, -1.0);
	d1 = d3_dot (ab, ap);
	d2 = d3_dot (ac, ap);
	if (d1 <= 0.0 && d2 <= 0.0)
	{
		l[0] = 1.0; l[1] = 0.0; l[2] = 0.0;
		return 1;
	}
	d3_scale (bp, b, -1.0);
	d3 = d3_dot (ab, bp);
	d4 = d3_dot (ac, bp);
	if (d3 >= 0.0 && d4 <= d3)
	{
		l[0] = 0.0; l[1] = 1.0; l[2] = 0.0;
		return 2;
	}
	vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
	{
		gdouble v;

		v = d1 / (d1 - d3);
		l[0] = 1.0 - v; l[1] = v; l[2] = 0.0;
		return 3;
	}
	d3_scale (cp, c, -1.0);
	d5 = d3_dot (ab, cp);
	d6 = d3_dot (ac, cp);
	if (d6 >= 0.0 && d5 <= d6)
	{
		l[0] = 0.0; l[1] = 0.0; l[2] = 1.0;
		return 4;
	}
	vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
	{
		gdouble w;

		w = d2 / (d2 - d6);
		l[0] = 1.0 - w; l[1] = 0.0; l[2] = w;
		return 5;
	}
	va = d3 * d6 - d5 * d4;
	if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
	{
		gdouble w;

		w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		l[0] = 0.0; l[1] = 1.0 - w; l[2] = w;
		return 6;
	}
	{
		gdouble den;

		den = 1.0 / (va + vb + vc);
		l[1] = vb * den;
		l[2] = vc * den;
		l[0] = 1.0 - l[1] - l[2];
	}
	return 7;
}

/* Keeps the simplex vertices in @mask (bit i = vertex i), in order. */
static void
simplex_reduce (Simplex *s,
                guint    mask,
                const gdouble *l)
{
	guint i;
	guint k;

	k = 0;
	for (i = 0; i < s->n; i++)
		if (mask & (1u << i))
		{
			if (k != i)
			{
				d3_copy (s->w[k], s->w[i]);
				d3_copy (s->a[k], s->a[i]);
				d3_copy (s->b[k], s->b[i]);
			}
			s->l[k] = l[i];
			k++;
		}
	s->n = k;
}

/* Closest point of the simplex to the origin; reduces the simplex to the
 * supporting feature.  Returns FALSE when the origin is inside a
 * tetrahedron. */
static gboolean
simplex_solve (Simplex *s,
               gdouble *v)
{
	gdouble l[4];
	guint mask;
	guint i;

	switch (s->n)
	{
	case 1:
		l[0] = 1.0;
		mask = 1;
		break;
	case 2:
		{
			gdouble u;
			gdouble w;

			bary2 (s->w[0], s->w[1], &u, &w);
			if (w <= 0.0)
			{
				l[0] = 1.0;
				mask = 1;
			}
			else if (u <= 0.0)
			{
				l[1] = 1.0;
				mask = 2;
			}
			else
			{
				l[0] = u;
				l[1] = w;
				mask = 3;
			}
		}
		break;
	case 3:
		mask = closest_on_triangle (s->w[0], s->w[1], s->w[2], l);
		break;
	default:
		{
			/* Tetrahedron: test the origin against the planes of the
			 * faces that look away from the opposite vertex. */
			static const guint faces[4][4] = {
				{ 0, 1, 2, 3 }, { 0, 2, 3, 1 }, { 0, 3, 1, 2 }, { 1, 3, 2, 0 }
			};
			gdouble best;
			guint f;
			gboolean outside_any;

			best = G_MAXDOUBLE;
			mask = 15;
			outside_any = FALSE;
			for (f = 0; f < 4; f++)
			{
				const gdouble *a;
				const gdouble *b;
				const gdouble *c;
				const gdouble *d;
				gdouble ab[3];
				gdouble ac[3];
				gdouble ad[3];
				gdouble n[3];
				gdouble sp;
				gdouble sd;

				a = s->w[faces[f][0]];
				b = s->w[faces[f][1]];
				c = s->w[faces[f][2]];
				d = s->w[faces[f][3]];
				d3_sub (ab, b, a);
				d3_sub (ac, c, a);
				d3_sub (ad, d, a);
				d3_cross (n, ab, ac);
				sp = -d3_dot (a, n);
				sd = d3_dot (ad, n);
				/* Origin and d on opposite sides (or d on the plane). */
				if (sp * sd < 1e-20 || fabs (sd) < 1e-30)
				{
					gdouble tl[3];
					guint tm;
					gdouble q[3];
					gdouble dd;

					outside_any = TRUE;
					tm = closest_on_triangle (a, b, c, tl);
					d3_scale (q, a, tl[0]);
					d3_madd (q, q, b, tl[1]);
					d3_madd (q, q, c, tl[2]);
					dd = d3_dot (q, q);
					if (dd < best)
					{
						best = dd;
						l[0] = l[1] = l[2] = l[3] = 0.0;
						mask = 0;
						if (tm & 1) { l[faces[f][0]] = tl[0]; mask |= 1u << faces[f][0]; }
						if (tm & 2) { l[faces[f][1]] = tl[1]; mask |= 1u << faces[f][1]; }
						if (tm & 4) { l[faces[f][2]] = tl[2]; mask |= 1u << faces[f][2]; }
					}
				}
			}
			if (!outside_any)
			{
				d3_set (v, 0.0, 0.0, 0.0);
				return FALSE;
			}
		}
		break;
	}
	simplex_reduce (s, mask, l);
	d3_set (v, 0.0, 0.0, 0.0);
	for (i = 0; i < s->n; i++)
		d3_madd (v, v, s->w[i], s->l[i]);
	return TRUE;
}

/*
 * gjk:
 * @full: use full supports (with radius) instead of cores
 * @max_dist: stop early once the distance is proven larger than this
 *
 * Returns: the distance between the sets (0 when they overlap), the
 *   closest points in @pa / @pb and the final simplex in @s.
 */
static gdouble
gjk (const Proxy *A,
     const Proxy *B,
     gboolean     full,
     gdouble      max_dist,
     gdouble     *pa,
     gdouble     *pb,
     Simplex     *s)
{
	gdouble v[3];
	gdouble prev;
	guint iter;
	guint i;

	s->n = 0;
	d3_sub (v, A->t, B->t);
	if (d3_dot (v, v) < 1e-20)
		d3_set (v, 1.0, 0.0, 0.0);
	prev = G_MAXDOUBLE;

	for (iter = 0; iter < 64; iter++)
	{
		gdouble nd[3];
		gdouble sa[3];
		gdouble sb[3];
		gdouble w[3];
		gdouble vv;
		gdouble vw;
		gboolean dup;

		/* New support point of A - B in direction -v. */
		d3_scale (nd, v, -1.0);
		if (full)
		{
			proxy_support_full (A, nd, sa);
			proxy_support_full (B, v, sb);
		}
		else
		{
			proxy_support_core (A, nd, sa);
			proxy_support_core (B, v, sb);
		}
		d3_sub (w, sa, sb);
		vv = d3_dot (v, v);
		vw = d3_dot (v, w);

		/* Early out: separated by more than max_dist. */
		if (s->n > 0 && vw > 0.0 && vw * vw > max_dist * max_dist * vv)
			break;

		/* Converged: no progress along -v. */
		if (s->n > 0 && vv - vw <= 1e-12 * vv + 1e-24)
			break;
		dup = FALSE;
		for (i = 0; i < s->n; i++)
			if (s->w[i][0] == w[0] && s->w[i][1] == w[1] && s->w[i][2] == w[2])
				dup = TRUE;
		if (dup)
			break;

		d3_copy (s->w[s->n], w);
		d3_copy (s->a[s->n], sa);
		d3_copy (s->b[s->n], sb);
		s->n++;
		if (!simplex_solve (s, v))
		{
			/* Origin enclosed: overlap. */
			d3_set (pa, 0.0, 0.0, 0.0);
			d3_set (pb, 0.0, 0.0, 0.0);
			for (i = 0; i < s->n; i++)
			{
				d3_madd (pa, pa, s->a[i], 0.25);
				d3_madd (pb, pb, s->b[i], 0.25);
			}
			return 0.0;
		}
		vv = d3_dot (v, v);
		if (vv < 1e-20)
			break;
		if (vv >= prev)
			break;
		prev = vv;
	}

	d3_set (pa, 0.0, 0.0, 0.0);
	d3_set (pb, 0.0, 0.0, 0.0);
	for (i = 0; i < s->n; i++)
	{
		d3_madd (pa, pa, s->a[i], s->l[i]);
		d3_madd (pb, pb, s->b[i], s->l[i]);
	}
	if (d3_dot (v, v) < 1e-20)
		return 0.0;
	return d3_len (v);
}

/* ==========================================================================
 * EPA (penetration depth of overlapping full shapes)
 * ========================================================================== */

#define EPA_MAX_POINTS (128)
#define EPA_MAX_FACES  (256)

typedef struct
{
	guint    v[3];
	gdouble  n[3];
	gdouble  d;
	gboolean alive;
} EpaFace;

typedef struct
{
	gdouble w[EPA_MAX_POINTS][3];
	gdouble a[EPA_MAX_POINTS][3];
	gdouble b[EPA_MAX_POINTS][3];
	guint   n_points;
	EpaFace f[EPA_MAX_FACES];
	guint   n_faces;
} Epa;

static guint
epa_add_support (Epa         *e,
                 const Proxy *A,
                 const Proxy *B,
                 const gdouble *d)
{
	gdouble nd[3];
	guint i;

	if (e->n_points >= EPA_MAX_POINTS)
		return G_MAXUINT;
	i = e->n_points++;
	d3_scale (nd, d, -1.0);
	proxy_support_full (A, d, e->a[i]);
	proxy_support_full (B, nd, e->b[i]);
	d3_sub (e->w[i], e->a[i], e->b[i]);
	return i;
}

static gboolean
epa_make_face (Epa  *e,
               guint a,
               guint b,
               guint c)
{
	EpaFace *f;
	gdouble e1[3];
	gdouble e2[3];

	if (e->n_faces >= EPA_MAX_FACES)
		return FALSE;
	f = &e->f[e->n_faces++];
	f->v[0] = a;
	f->v[1] = b;
	f->v[2] = c;
	d3_sub (e1, e->w[b], e->w[a]);
	d3_sub (e2, e->w[c], e->w[a]);
	d3_cross (f->n, e1, e2);
	d3_normalize (f->n, f->n);
	f->d = d3_dot (f->n, e->w[a]);
	f->alive = TRUE;
	return TRUE;
}

/* Adds point @pi to the polytope: removes every face it sees and fans the
 * horizon to it.  Returns FALSE when the point sees no face or capacity
 * ran out. */
static gboolean
epa_add_point (Epa  *e,
               guint pi)
{
	guint i;
	guint k;
	guint edges[EPA_MAX_FACES * 3][2];
	guint n_edges;
	gboolean any;

	n_edges = 0;
	any = FALSE;
	for (i = 0; i < e->n_faces; i++)
	{
		EpaFace *f;

		f = &e->f[i];
		if (!f->alive || d3_dot (f->n, e->w[pi]) - f->d <= 1e-10)
			continue;
		any = TRUE;
		f->alive = FALSE;
		for (k = 0; k < 3; k++)
		{
			guint a;
			guint b;
			guint m;
			gboolean twin;

			a = f->v[k];
			b = f->v[(k + 1) % 3];
			/* A shared edge between two removed faces cancels out. */
			twin = FALSE;
			for (m = 0; m < n_edges; m++)
				if (edges[m][0] == b && edges[m][1] == a)
				{
					edges[m][0] = edges[n_edges - 1][0];
					edges[m][1] = edges[n_edges - 1][1];
					n_edges--;
					twin = TRUE;
					break;
				}
			if (!twin)
			{
				edges[n_edges][0] = a;
				edges[n_edges][1] = b;
				n_edges++;
			}
		}
	}
	if (!any)
		return FALSE;

	/* Compact dead faces so the array does not run out. */
	k = 0;
	for (i = 0; i < e->n_faces; i++)
		if (e->f[i].alive)
			e->f[k++] = e->f[i];
	e->n_faces = k;

	for (i = 0; i < n_edges; i++)
		if (!epa_make_face (e, edges[i][0], edges[i][1], pi))
			return FALSE;
	return TRUE;
}

/*
 * epa:
 *
 * Penetration of two overlapping full shapes, seeded by the simplex of a
 * full-shape GJK that found the overlap.  Writes the unit normal A->B, the
 * depth and the deepest points.  Returns FALSE when no polytope around the
 * origin could be built (the shapes merely touch).
 */
static gboolean
epa (const Proxy   *A,
     const Proxy   *B,
     const Simplex *s,
     gdouble       *n,
     gdouble       *depth,
     gdouble       *pa,
     gdouble       *pb)
{
	Epa *e;
	guint i;
	guint i0;
	guint i1;
	guint i2;
	guint i3;
	gdouble best;
	gdouble c[3];
	guint iter;
	EpaFace *closest;
	gboolean ok;

	e = g_new (Epa, 1);
	e->n_points = 0;
	e->n_faces = 0;
	ok = FALSE;

	/* Seed points. */
	if (s->n >= 2)
		for (i = 0; i < s->n; i++)
		{
			d3_copy (e->w[i], s->w[i]);
			d3_copy (e->a[i], s->a[i]);
			d3_copy (e->b[i], s->b[i]);
			e->n_points++;
		}
	if (e->n_points < 2)
	{
		static const gdouble dirs[4][3] = {
			{ 0, 1, 0 }, { -1, -1, -1 }, { 1, -1, -1 }, { 0, -1, 1 }
		};

		e->n_points = 0;
		for (i = 0; i < 4; i++)
			epa_add_support (e, A, B, dirs[i]);
	}
	else if (e->n_points == 2)
	{
		/* Three directions around the segment, 120 degrees apart. */
		gdouble ax[3];
		gdouble p1[3];
		gdouble p2[3];

		d3_sub (ax, e->w[1], e->w[0]);
		d3_normalize (ax, ax);
		d3_perp (p1, ax);
		d3_cross (p2, ax, p1);
		for (i = 0; i < 3; i++)
		{
			gdouble ang;
			gdouble d[3];

			ang = 2.0 * G_PI * (gdouble)i / 3.0;
			d3_scale (d, p1, cos (ang));
			d3_madd (d, d, p2, sin (ang));
			epa_add_support (e, A, B, d);
		}
	}
	else if (e->n_points == 3)
	{
		gdouble e1[3];
		gdouble e2[3];
		gdouble nn[3];

		d3_sub (e1, e->w[1], e->w[0]);
		d3_sub (e2, e->w[2], e->w[0]);
		d3_cross (nn, e1, e2);
		d3_normalize (nn, nn);
		epa_add_support (e, A, B, nn);
		d3_scale (nn, nn, -1.0);
		epa_add_support (e, A, B, nn);
	}

	/* Largest tetrahedron from the seeds. */
	i0 = 0;
	i1 = G_MAXUINT;
	best = 1e-24;
	for (i = 1; i < e->n_points; i++)
	{
		gdouble d[3];

		d3_sub (d, e->w[i], e->w[i0]);
		if (d3_dot (d, d) > best)
		{
			best = d3_dot (d, d);
			i1 = i;
		}
	}
	if (i1 == G_MAXUINT)
		goto out;
	i2 = G_MAXUINT;
	best = 1e-24;
	for (i = 0; i < e->n_points; i++)
	{
		gdouble e1[3];
		gdouble e2[3];
		gdouble cr[3];

		d3_sub (e1, e->w[i1], e->w[i0]);
		d3_sub (e2, e->w[i], e->w[i0]);
		d3_cross (cr, e1, e2);
		if (d3_dot (cr, cr) > best)
		{
			best = d3_dot (cr, cr);
			i2 = i;
		}
	}
	if (i2 == G_MAXUINT)
		goto out;
	i3 = G_MAXUINT;
	best = 1e-18;
	for (i = 0; i < e->n_points; i++)
	{
		gdouble e1[3];
		gdouble e2[3];
		gdouble e3[3];
		gdouble cr[3];
		gdouble vol;

		d3_sub (e1, e->w[i1], e->w[i0]);
		d3_sub (e2, e->w[i2], e->w[i0]);
		d3_sub (e3, e->w[i], e->w[i0]);
		d3_cross (cr, e1, e2);
		vol = fabs (d3_dot (cr, e3));
		if (vol > best)
		{
			best = vol;
			i3 = i;
		}
	}
	if (i3 == G_MAXUINT)
	{
		/* Flat seed: add supports on both sides of its plane. */
		gdouble e1[3];
		gdouble e2[3];
		gdouble nn[3];
		guint ia;
		guint ib;

		d3_sub (e1, e->w[i1], e->w[i0]);
		d3_sub (e2, e->w[i2], e->w[i0]);
		d3_cross (nn, e1, e2);
		d3_normalize (nn, nn);
		ia = epa_add_support (e, A, B, nn);
		d3_scale (nn, nn, -1.0);
		ib = epa_add_support (e, A, B, nn);
		if (ia == G_MAXUINT || ib == G_MAXUINT)
			goto out;
		{
			gdouble da[3];
			gdouble db[3];

			d3_sub (da, e->w[ia], e->w[i0]);
			d3_sub (db, e->w[ib], e->w[i0]);
			d3_cross (e1, e1, e2);
			i3 = fabs (d3_dot (e1, da)) >= fabs (d3_dot (e1, db)) ? ia : ib;
			if (fabs (d3_dot (e1, i3 == ia ? da : db)) < 1e-18)
				goto out;
		}
	}

	/* Initial tetrahedron, faces wound outward. */
	for (i = 0; i < 3; i++)
		c[i] = (e->w[i0][i] + e->w[i1][i] + e->w[i2][i] + e->w[i3][i]) * 0.25;
	{
		guint tet[4][3];
		guint t;

		tet[0][0] = i0; tet[0][1] = i1; tet[0][2] = i2;
		tet[1][0] = i0; tet[1][1] = i3; tet[1][2] = i1;
		tet[2][0] = i1; tet[2][1] = i3; tet[2][2] = i2;
		tet[3][0] = i2; tet[3][1] = i3; tet[3][2] = i0;
		for (t = 0; t < 4; t++)
		{
			EpaFace *f;
			gdouble dc;

			epa_make_face (e, tet[t][0], tet[t][1], tet[t][2]);
			f = &e->f[e->n_faces - 1];
			dc = d3_dot (f->n, c) - f->d;
			if (dc > 0.0)
			{
				e->n_faces--;
				epa_make_face (e, tet[t][0], tet[t][2], tet[t][1]);
			}
		}
	}
	/* Remaining seeds. */
	for (i = 0; i < e->n_points; i++)
	{
		guint k;
		gboolean outside;

		if (i == i0 || i == i1 || i == i2 || i == i3)
			continue;
		outside = FALSE;
		for (k = 0; k < e->n_faces; k++)
			if (e->f[k].alive && d3_dot (e->f[k].n, e->w[i]) - e->f[k].d > 1e-10)
				outside = TRUE;
		if (outside)
			epa_add_point (e, i);
	}

	/* Grow until the origin is inside: expand towards any face that has
	 * the origin in front of it. */
	for (iter = 0; iter < 32; iter++)
	{
		EpaFace *worst;
		guint k;
		guint pi;
		gdouble nn[3];

		worst = NULL;
		for (k = 0; k < e->n_faces; k++)
			if (e->f[k].alive && e->f[k].d < -1e-10 && (worst == NULL || e->f[k].d < worst->d))
				worst = &e->f[k];
		if (worst == NULL)
			break;
		d3_copy (nn, worst->n);
		pi = epa_add_support (e, A, B, nn);
		if (pi == G_MAXUINT || d3_dot (nn, e->w[pi]) - worst->d <= 1e-10)
			goto out;
		if (!epa_add_point (e, pi))
			goto out;
	}

	/* Main loop: push the closest face outward until it is on the hull. */
	closest = NULL;
	for (iter = 0; iter < 64; iter++)
	{
		guint k;
		guint pi;
		gdouble gap;

		closest = NULL;
		for (k = 0; k < e->n_faces; k++)
			if (e->f[k].alive && (closest == NULL || e->f[k].d < closest->d))
				closest = &e->f[k];
		if (closest == NULL)
			goto out;
		pi = epa_add_support (e, A, B, closest->n);
		if (pi == G_MAXUINT)
			break;
		gap = d3_dot (closest->n, e->w[pi]) - closest->d;
		if (gap < 1e-7 + 1e-6 * fabs (closest->d))
			break;
		{
			EpaFace keep;

			keep = *closest;
			if (!epa_add_point (e, pi))
			{
				/* Numerically flat: the old closest face stands. */
				e->f[0] = keep;
				e->n_faces = 1;
				break;
			}
		}
	}
	closest = NULL;
	{
		guint k;

		for (k = 0; k < e->n_faces; k++)
			if (e->f[k].alive && (closest == NULL || e->f[k].d < closest->d))
				closest = &e->f[k];
	}
	if (closest == NULL)
		goto out;

	/* Result: the origin's projection on the closest face, mapped to A and
	 * B through its barycentric coordinates. */
	{
		gdouble p[3];
		gdouble q0[3];
		gdouble q1[3];
		gdouble q2[3];
		gdouble u;
		gdouble v;
		gdouble w;
		const guint *fv;

		d3_scale (p, closest->n, closest->d);
		fv = closest->v;
		d3_sub (q0, e->w[fv[0]], p);
		d3_sub (q1, e->w[fv[1]], p);
		d3_sub (q2, e->w[fv[2]], p);
		bary3 (q0, q1, q2, &u, &v, &w);
		d3_scale (pa, e->a[fv[0]], u);
		d3_madd (pa, pa, e->a[fv[1]], v);
		d3_madd (pa, pa, e->a[fv[2]], w);
		d3_scale (pb, e->b[fv[0]], u);
		d3_madd (pb, pb, e->b[fv[1]], v);
		d3_madd (pb, pb, e->b[fv[2]], w);
		d3_copy (n, closest->n);
		*depth = MAX (closest->d, 0.0);
		ok = TRUE;
	}

out:
	g_free (e);
	return ok;
}

/* ==========================================================================
 * Penetration / separation of two convex proxies
 * ========================================================================== */

typedef struct
{
	gdouble n[3];      /* unit, A -> B */
	gdouble depth;     /* > 0 overlap, < 0 separation */
	gdouble pa[3];
	gdouble pb[3];
} Penetration;

/* Closest points between segments p1-q1 and p2-q2 (Ericson 5.1.9). */
static void
closest_seg_seg (const gdouble *p1,
                 const gdouble *q1,
                 const gdouble *p2,
                 const gdouble *q2,
                 gdouble       *c1,
                 gdouble       *c2)
{
	gdouble d1[3];
	gdouble d2[3];
	gdouble r[3];
	gdouble a;
	gdouble e;
	gdouble f;
	gdouble s;
	gdouble t;

	d3_sub (d1, q1, p1);
	d3_sub (d2, q2, p2);
	d3_sub (r, p1, p2);
	a = d3_dot (d1, d1);
	e = d3_dot (d2, d2);
	f = d3_dot (d2, r);
	if (a <= 1e-24 && e <= 1e-24)
	{
		s = 0.0;
		t = 0.0;
	}
	else if (a <= 1e-24)
	{
		s = 0.0;
		t = CLAMP (f / e, 0.0, 1.0);
	}
	else
	{
		gdouble c;

		c = d3_dot (d1, r);
		if (e <= 1e-24)
		{
			t = 0.0;
			s = CLAMP (-c / a, 0.0, 1.0);
		}
		else
		{
			gdouble b;
			gdouble den;

			b = d3_dot (d1, d2);
			den = a * e - b * b;
			if (den > 1e-18 * a * e)
				s = CLAMP ((b * f - c * e) / den, 0.0, 1.0);
			else
				s = 0.0;
			t = (b * s + f) / e;
			if (t < 0.0)
			{
				t = 0.0;
				s = CLAMP (-c / a, 0.0, 1.0);
			}
			else if (t > 1.0)
			{
				t = 1.0;
				s = CLAMP ((b - c) / a, 0.0, 1.0);
			}
		}
	}
	d3_madd (c1, p1, d1, s);
	d3_madd (c2, p2, d2, t);
}

/* Segment core of a sphere (a point) or capsule in the working frame. */
static gboolean
proxy_segment (const Proxy *p,
               gdouble     *a,
               gdouble     *b)
{
	LrgPhysicsShapeKind k;
	gdouble h;
	guint i;

	if (p->shape == NULL)
		return FALSE;
	k = p->shape->kind;
	if (k != LRG_PHYSICS_SHAPE_KIND_SPHERE && k != LRG_PHYSICS_SHAPE_KIND_CAPSULE)
		return FALSE;
	h = k == LRG_PHYSICS_SHAPE_KIND_CAPSULE ? (gdouble)p->shape->half_height : 0.0;
	for (i = 0; i < 3; i++)
	{
		/* Column 1 of R is the local Y axis. */
		a[i] = p->t[i] + p->R[i * 3 + 1] * h;
		b[i] = p->t[i] - p->R[i * 3 + 1] * h;
	}
	return TRUE;
}

/* Normal for coincident cores: from A's centre to B's, else +Y. */
static void
fallback_normal (const Proxy *A,
                 const Proxy *B,
                 gdouble     *n)
{
	gdouble d[3];

	d3_sub (d, B->t, A->t);
	if (d3_dot (d, d) < 1e-24)
		d3_set (n, 0.0, 1.0, 0.0);
	else
		d3_normalize (n, d);
}

/*
 * convex_penetration:
 *
 * Signed separation of two convex proxies: GJK on the cores (cheap closed
 * form for sphere/capsule pairs), radii added on top; EPA on the full
 * shapes when the cores overlap.  Returns FALSE when the pair is apart by
 * more than @spec.
 */
static gboolean
convex_penetration (const Proxy *A,
                    const Proxy *B,
                    gdouble      spec,
                    Penetration *out)
{
	gdouble rsum;
	gdouble ca[3];
	gdouble cb[3];
	gdouble d;
	gdouble a0[3];
	gdouble a1[3];
	gdouble b0[3];
	gdouble b1[3];
	Simplex s;

	d3_set (a0, 0.0, 0.0, 0.0);
	d3_set (a1, 0.0, 0.0, 0.0);
	rsum = A->radius + B->radius;
	if (proxy_segment (A, a0, a1) && proxy_segment (B, b0, b1))
	{
		gdouble diff[3];

		closest_seg_seg (a0, a1, b0, b1, ca, cb);
		d3_sub (diff, cb, ca);
		d = d3_len (diff);
	}
	else
		d = gjk (A, B, FALSE, rsum + spec, ca, cb, &s);

	if (d > rsum + spec)
		return FALSE;

	if (d > 1e-7)
	{
		/* Cores apart: the radii make up the overlap. */
		gdouble diff[3];

		d3_sub (diff, cb, ca);
		d3_scale (out->n, diff, 1.0 / d);
		out->depth = rsum - d;
		d3_madd (out->pa, ca, out->n, A->radius);
		d3_madd (out->pb, cb, out->n, -B->radius);
		return TRUE;
	}

	if (rsum > 0.0 && A->shape != NULL && B->shape != NULL
	    && A->shape->kind <= LRG_PHYSICS_SHAPE_KIND_CAPSULE
	    && B->shape->kind <= LRG_PHYSICS_SHAPE_KIND_CAPSULE)
	{
		/* Coincident sphere/capsule cores: any direction works. */
		gdouble ax[3];

		d3_sub (ax, a1, a0);
		if (d3_dot (ax, ax) > 1e-20)
			d3_perp (out->n, ax);
		else
			fallback_normal (A, B, out->n);
		out->depth = rsum;
		d3_madd (out->pa, ca, out->n, A->radius);
		d3_madd (out->pb, cb, out->n, -B->radius);
		return TRUE;
	}

	/* Deep: EPA on the full shapes. */
	{
		gdouble fa[3];
		gdouble fb[3];
		gdouble fd;

		fd = gjk (A, B, TRUE, G_MAXDOUBLE, fa, fb, &s);
		if (fd > 1e-7)
		{
			gdouble diff[3];

			/* Full shapes apart (radius-free cores just touch). */
			d3_sub (diff, fb, fa);
			d3_scale (out->n, diff, 1.0 / fd);
			out->depth = -fd;
			d3_copy (out->pa, fa);
			d3_copy (out->pb, fb);
			return fd <= spec;
		}
		if (!epa (A, B, &s, out->n, &out->depth, out->pa, out->pb))
		{
			fallback_normal (A, B, out->n);
			out->depth = 0.0;
			d3_copy (out->pa, fa);
			d3_copy (out->pb, fb);
		}
		return TRUE;
	}
}

/* ==========================================================================
 * Manifold construction (port of ManifoldBetweenTwoFaces / ClipPoly /
 * PruneContactPoints from Jolt, with feature ids threaded through)
 * ========================================================================== */

#define WORK_MAX_POINTS (64)
#define POLY_MAX (LRG_SHAPE_MAX_FACE_POINTS * 2 + 8)

typedef struct
{
	gdouble n[3];
	guint   count;
	gdouble pa[WORK_MAX_POINTS][3];
	gdouble pb[WORK_MAX_POINTS][3];
	guint32 id[WORK_MAX_POINTS];
	guint   sub_b;
	guint   mat_b;
	gdouble max_depth;
} Work;

typedef struct
{
	gdouble p[POLY_MAX][3];
	guint32 id[POLY_MAX];
	guint   n;
} Poly;

/* Mixes a feature part into a 32-bit id (multiply / xor-shift). */
static guint32
mix_id (guint32 h,
        guint32 v)
{
	h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	return h;
}

/* Clip @in against the half space behind plane (origin, normal): points
 * with (origin - p) . normal < 0 are kept (ClipPolyVsPlane). */
static void
clip_vs_plane (const Poly    *in,
               const gdouble *origin,
               const gdouble *normal,
               guint32        plane_id,
               Poly          *out)
{
	const gdouble *e1;
	guint32 id1;
	gdouble prev_num;
	gboolean prev_in;
	guint j;
	gdouble d[3];

	out->n = 0;
	if (in->n == 0)
		return;
	e1 = in->p[in->n - 1];
	id1 = in->id[in->n - 1];
	d3_sub (d, origin, e1);
	prev_num = d3_dot (d, normal);
	prev_in = prev_num < 0.0;
	for (j = 0; j < in->n; j++)
	{
		const gdouble *e2;
		gdouble num;
		gboolean cur_in;

		e2 = in->p[j];
		d3_sub (d, origin, e2);
		num = d3_dot (d, normal);
		cur_in = num < 0.0;
		if (cur_in != prev_in)
		{
			gdouble e12[3];
			gdouble den;

			d3_sub (e12, e2, e1);
			den = d3_dot (e12, normal);
			if (den != 0.0 && out->n < POLY_MAX)
			{
				d3_madd (out->p[out->n], e1, e12, prev_num / den);
				out->id[out->n] = mix_id (mix_id (plane_id, id1), in->id[j]) | 0x80000000u;
				out->n++;
			}
			else
				cur_in = prev_in;
		}
		if (cur_in && out->n < POLY_MAX)
		{
			d3_copy (out->p[out->n], e2);
			out->id[out->n] = in->id[j];
			out->n++;
		}
		prev_num = num;
		prev_in = cur_in;
		e1 = e2;
		id1 = in->id[j];
	}
}

/* Clip @in against every edge of the convex polygon @clip
 * (ClipPolyVsPoly); @clip_normal orients the inward edge planes. */
static void
clip_vs_poly (const Poly    *in,
              const Poly    *clip,
              const gdouble *clip_normal,
              Poly          *out)
{
	Poly tmp[2];
	guint i;
	const Poly *src;

	src = in;
	out->n = 0;
	for (i = 0; i < clip->n; i++)
	{
		const gdouble *c1;
		const gdouble *c2;
		gdouble edge[3];
		gdouble cn[3];
		Poly *tgt;

		c1 = clip->p[i];
		c2 = clip->p[(i + 1) % clip->n];
		d3_sub (edge, c2, c1);
		d3_cross (cn, clip_normal, edge);
		tgt = (i == clip->n - 1) ? out : &tmp[i & 1];
		clip_vs_plane (src, c1, cn, 0x100u + i, tgt);
		if (tgt->n < 3 && !(in->n == 2 && tgt->n == 2))
		{
			out->n = 0;
			return;
		}
		src = tgt;
	}
}

/* Clip polygon @in against the slab of edge v1-v2 (ClipPolyVsEdge). */
static void
clip_vs_edge (const Poly    *in,
              const gdouble *v1,
              const gdouble *v2,
              const gdouble *clip_normal,
              Poly          *out)
{
	gdouble edge[3];
	gdouble en[3];
	gdouble pn[3];
	gdouble t1[3];
	gdouble t2[3];
	gdouble pnl;
	gdouble p1[3];
	gdouble p2[3];
	gdouble v12[3];
	gdouble v12l;
	const gdouble *e1;
	guint32 id1;
	gdouble prev_num;
	gboolean prev_in;
	guint j;
	gdouble d[3];

	out->n = 0;
	d3_sub (edge, v2, v1);
	d3_cross (en, clip_normal, edge);
	d3_sub (t1, in->p[2], in->p[0]);
	d3_sub (t2, in->p[1], in->p[0]);
	d3_cross (pn, t1, t2);
	pnl = d3_dot (pn, pn);
	if (pnl < 1e-30)
		return;
	d3_sub (d, in->p[0], v1);
	d3_madd (p1, v1, pn, d3_dot (pn, d) / pnl);
	d3_sub (d, in->p[0], v2);
	d3_madd (p2, v2, pn, d3_dot (pn, d) / pnl);
	d3_sub (v12, p2, p1);
	v12l = d3_dot (v12, v12);

	e1 = in->p[in->n - 1];
	id1 = in->id[in->n - 1];
	d3_sub (d, v1, e1);
	prev_num = d3_dot (d, en);
	prev_in = prev_num < 0.0;
	for (j = 0; j < in->n; j++)
	{
		const gdouble *e2;
		gdouble num;
		gboolean cur_in;

		e2 = in->p[j];
		d3_sub (d, v1, e2);
		num = d3_dot (d, en);
		cur_in = num < 0.0;
		if (cur_in != prev_in && out->n < POLY_MAX)
		{
			gdouble e12[3];
			gdouble den;
			gdouble cp[3];
			gdouble proj;
			guint32 id;

			d3_sub (e12, e2, e1);
			den = d3_dot (e12, en);
			if (den != 0.0)
				d3_madd (cp, e1, e12, prev_num / den);
			else
				d3_copy (cp, e1);
			d3_sub (d, cp, p1);
			proj = d3_dot (d, v12);
			id = mix_id (id1, in->id[j]) | 0x40000000u;
			if (proj < 0.0)
			{
				d3_copy (out->p[out->n], p1);
				id = 0x20000001u;
			}
			else if (proj > v12l)
			{
				d3_copy (out->p[out->n], p2);
				id = 0x20000002u;
			}
			else
				d3_copy (out->p[out->n], cp);
			out->id[out->n] = id;
			out->n++;
		}
		prev_num = num;
		prev_in = cur_in;
		e1 = e2;
		id1 = in->id[j];
	}
}

/* Empties a working manifold without clearing its point arrays. */
static void
work_init (Work *w)
{
	d3_set (w->n, 0.0, 1.0, 0.0);
	w->count = 0;
	w->sub_b = LRG_PHYSICS_NO_SUB_SHAPE;
	w->mat_b = 0;
	w->max_depth = 0.0;
}

/* Appends one contact to @w (dropping beyond capacity). */
static void
work_add (Work          *w,
          const gdouble *pa,
          const gdouble *pb,
          guint32        id)
{
	gdouble d[3];
	gdouble depth;

	if (w->count >= WORK_MAX_POINTS)
		return;
	d3_copy (w->pa[w->count], pa);
	d3_copy (w->pb[w->count], pb);
	w->id[w->count] = id;
	d3_sub (d, pa, pb);
	depth = d3_dot (d, w->n);
	if (w->count == 0 || depth > w->max_depth)
		w->max_depth = depth;
	w->count++;
}

/* Index of the vertex of @poly closest to @p. */
static guint32
nearest_vertex (const Poly    *poly,
                const gdouble *p)
{
	guint32 best;
	gdouble best_d;
	guint i;

	best = 0;
	best_d = G_MAXDOUBLE;
	for (i = 0; i < poly->n; i++)
	{
		gdouble d[3];
		gdouble dd;

		d3_sub (d, poly->p[i], p);
		dd = d3_dot (d, d);
		if (dd < best_d - 1e-12)
		{
			best_d = dd;
			best = i;
		}
	}
	return best;
}

/*
 * manifold_between_faces:
 *
 * Contact points from the supporting faces of A (along +n) and B (along
 * -n): face B clipped against face A (or the other way round when A's
 * face has more points), projected back onto the clipping face's plane;
 * points further apart than @spec are dropped.  Falls back to the single
 * pair of deepest points.
 */
static void
manifold_between_faces (const Proxy       *A,
                        const Proxy       *B,
                        const Penetration *pen,
                        gdouble            spec,
                        Work              *w)
{
	Poly fa;
	Poly fb;
	guint ida;
	guint idb;
	gdouble nb[3];
	guint i;
	guint32 base;
	guint old;
	gdouble tmp[LRG_SHAPE_MAX_FACE_POINTS * 3];

	d3_copy (w->n, pen->n);
	fa.n = proxy_face (A, pen->n, tmp, &ida);
	for (i = 0; i < fa.n; i++)
	{
		d3_copy (fa.p[i], &tmp[i * 3]);
		fa.id[i] = 0x1000u + i;
	}
	d3_scale (nb, pen->n, -1.0);
	fb.n = proxy_face (B, nb, tmp, &idb);
	for (i = 0; i < fb.n; i++)
	{
		d3_copy (fb.p[i], &tmp[i * 3]);
		fb.id[i] = 0x2000u + i;
	}
	base = mix_id (mix_id (2166136261u, ida), idb + 0x10000u);
	old = w->count;

	if (MIN (fa.n, fb.n) >= 2)
	{
		const Poly *f1;
		const Poly *f2;
		gdouble axis[3];
		gboolean swapped;
		gdouble po[3];
		gdouble fe[3];
		gdouble pn[3];
		Poly clipped;

		/* Clip the polygon with more points against the other. */
		if (fb.n >= 3 || fa.n < 3)
		{
			f1 = &fa;
			f2 = &fb;
			d3_copy (axis, pen->n);
			swapped = FALSE;
		}
		else
		{
			f1 = &fb;
			f2 = &fa;
			d3_scale (axis, pen->n, -1.0);
			swapped = TRUE;
		}
		d3_copy (po, f1->p[0]);
		d3_sub (fe, f1->p[1], po);

		clipped.n = 0;
		if (f1->n >= 3 && f2->n >= 2)
		{
			gdouble e2[3];

			clip_vs_poly (f2, f1, axis, &clipped);
			d3_sub (e2, f1->p[2], po);
			d3_cross (pn, fe, e2);
		}
		else if (f1->n == 2 && f2->n >= 3)
		{
			gdouble t[3];

			clip_vs_edge (f2, f1->p[0], f1->p[1], axis, &clipped);
			d3_cross (t, fe, axis);
			d3_cross (pn, t, fe);
		}
		else
		{
			/* Two segments (capsule sides): keep the part of segment 2
			 * that lies in the slab of segment 1, if they are parallel. */
			gdouble u1[3];
			gdouble u2[3];
			gdouble l1;
			gdouble t[3];

			l1 = d3_normalize (u1, fe);
			d3_sub (u2, f2->p[1], f2->p[0]);
			d3_normalize (u2, u2);
			if (l1 > 1e-9 && fabs (d3_dot (u1, u2)) > 0.998)
			{
				gdouble s0;
				gdouble s1;
				gdouble lo;
				gdouble hi;
				gdouble d[3];
				guint k;

				d3_sub (d, f2->p[0], po);
				s0 = d3_dot (d, u1);
				d3_sub (d, f2->p[1], po);
				s1 = d3_dot (d, u1);
				lo = MAX (MIN (s0, s1), 0.0);
				hi = MIN (MAX (s0, s1), l1);
				if (hi >= lo && fabs (s1 - s0) > 1e-12)
					for (k = 0; k < 2; k++)
					{
						gdouble s;
						gdouble f;

						s = k == 0 ? lo : hi;
						f = (s - s0) / (s1 - s0);
						d3_scale (clipped.p[clipped.n], f2->p[0], 1.0 - f);
						d3_madd (clipped.p[clipped.n], clipped.p[clipped.n], f2->p[1], f);
						clipped.id[clipped.n] = 0x3000u + k;
						clipped.n++;
					}
			}
			d3_cross (t, fe, axis);
			d3_cross (pn, t, fe);
		}

		/* Project the clipped points onto face 1's plane. */
		{
			gdouble ad;

			ad = d3_dot (axis, pn);
			if (ad != 0.0)
				for (i = 0; i < clipped.n; i++)
				{
					gdouble d[3];
					gdouble dist;
					gdouble p1[3];
					guint32 id;

					d3_sub (d, clipped.p[i], po);
					dist = d3_dot (d, pn) / ad;
					if (dist >= spec)
						continue;
					d3_madd (p1, clipped.p[i], axis, -dist);
					/* Feature id: the nearest vertex of each face.  Unlike
					 * clip provenance this does not flip when a corner
					 * sits exactly on a clipping plane, so ids persist
					 * through aligned resting contacts. */
					id = mix_id (mix_id (base, nearest_vertex (f1, p1) + (swapped ? 0x200u : 0x100u)),
					             nearest_vertex (f2, clipped.p[i]) + (swapped ? 0x100u : 0x200u));
					if (swapped)
						work_add (w, clipped.p[i], p1, id);
					else
						work_add (w, p1, clipped.p[i], id);
				}
		}
	}

	if (w->count == old)
		work_add (w, pen->pa, pen->pb, mix_id (base, 0xfffu));
}

/* Reduce to at most 4 points (port of Jolt's PruneContactPoints):
 * keep the point with the largest (lever arm x depth), the one furthest
 * from it, then the furthest on each side of that line. */
static void
prune_points (Work          *w,
              const gdouble *centre)
{
	gdouble proj[WORK_MAX_POINTS][3];
	gdouble dsq[WORK_MAX_POINTS];
	guint i;
	guint p1;
	guint p2;
	guint p3;
	guint p4;
	gdouble val;
	gdouble perp[3];
	gdouble e[3];
	gdouble min_v;
	gdouble max_v;
	guint keep[4];
	guint n_keep;

	if (w->count <= 4)
		return;
	for (i = 0; i < w->count; i++)
	{
		gdouble v1[3];
		gdouble d[3];

		d3_sub (v1, w->pa[i], centre);
		d3_madd (proj[i], v1, w->n, -d3_dot (v1, w->n));
		d3_sub (d, w->pb[i], w->pa[i]);
		dsq[i] = MAX (1e-6, d3_dot (d, d));
	}
	p1 = 0;
	val = MAX (1e-6, d3_dot (proj[0], proj[0])) * dsq[0];
	for (i = 1; i < w->count; i++)
	{
		gdouble v;

		v = MAX (1e-6, d3_dot (proj[i], proj[i])) * dsq[i];
		if (v > val)
		{
			val = v;
			p1 = i;
		}
	}
	p2 = G_MAXUINT;
	val = -G_MAXDOUBLE;
	for (i = 0; i < w->count; i++)
		if (i != p1)
		{
			gdouble d[3];
			gdouble v;

			d3_sub (d, proj[i], proj[p1]);
			v = MAX (1e-6, d3_dot (d, d)) * dsq[i];
			if (v > val)
			{
				val = v;
				p2 = i;
			}
		}
	d3_sub (e, proj[p2], proj[p1]);
	d3_cross (perp, e, w->n);
	p3 = G_MAXUINT;
	p4 = G_MAXUINT;
	min_v = 0.0;
	max_v = 0.0;
	for (i = 0; i < w->count; i++)
		if (i != p1 && i != p2)
		{
			gdouble d[3];
			gdouble v;

			d3_sub (d, proj[i], proj[p1]);
			v = d3_dot (perp, d);
			if (v < min_v)
			{
				min_v = v;
				p3 = i;
			}
			else if (v > max_v)
			{
				max_v = v;
				p4 = i;
			}
		}
	n_keep = 0;
	keep[n_keep++] = p1;
	if (p3 != G_MAXUINT)
		keep[n_keep++] = p3;
	keep[n_keep++] = p2;
	if (p4 != G_MAXUINT)
		keep[n_keep++] = p4;

	/* Compact the kept points in place (through a small copy). */
	{
		gdouble ka[4][3];
		gdouble kb[4][3];
		guint32 kid[4];
		gdouble nd;

		for (i = 0; i < n_keep; i++)
		{
			d3_copy (ka[i], w->pa[keep[i]]);
			d3_copy (kb[i], w->pb[keep[i]]);
			kid[i] = w->id[keep[i]];
		}
		w->count = 0;
		nd = 0.0;
		for (i = 0; i < n_keep; i++)
		{
			gdouble d[3];
			gdouble depth;

			d3_copy (w->pa[i], ka[i]);
			d3_copy (w->pb[i], kb[i]);
			w->id[i] = kid[i];
			d3_sub (d, ka[i], kb[i]);
			depth = d3_dot (d, w->n);
			if (i == 0 || depth > nd)
				nd = depth;
		}
		w->count = n_keep;
		w->max_depth = nd;
	}
}

/* Converts a working manifold to the public float struct (world space,
 * shift added back) and appends it. */
static void
emit_fill (Work               *w,
           const gdouble      *shift,
           const gdouble      *centre,
           gint                child_a,
           gint                child_b,
           guint               sub_a,
           guint               sub_b,
           guint               mat_a,
           guint               mat_b,
           LrgContactManifold *mp)
{
	guint i;
	guint k;

	prune_points (w, centre);
	lrg_contact_manifold_reset (mp);
	for (k = 0; k < 3; k++)
		mp->normal[k] = (gfloat)w->n[k];
	mp->n_points = MIN (w->count, (guint)LRG_CONTACT_MANIFOLD_MAX_POINTS);
	for (i = 0; i < mp->n_points; i++)
	{
		gdouble d[3];

		for (k = 0; k < 3; k++)
		{
			mp->points_a[i * 3 + k] = (gfloat)(w->pa[i][k] + shift[k]);
			mp->points_b[i * 3 + k] = (gfloat)(w->pb[i][k] + shift[k]);
		}
		d3_sub (d, w->pa[i], w->pb[i]);
		mp->depths[i] = (gfloat)d3_dot (d, w->n);
		mp->feature_ids[i] = w->id[i];
		for (k = 0; k < i; k++)
			if (mp->feature_ids[k] == mp->feature_ids[i])
				mp->feature_ids[i] = mix_id (mp->feature_ids[i], i + 1u);
	}
	mp->child_a = child_a;
	mp->child_b = child_b;
	mp->sub_shape_a = sub_a;
	mp->sub_shape_b = sub_b;
	mp->material_a = mat_a;
	mp->material_b = mat_b;
}

/* Fills and appends a manifold when @w has points. */
static void
emit (Work          *w,
      const gdouble *shift,
      const gdouble *centre,
      gint           child_a,
      gint           child_b,
      guint          sub_a,
      guint          sub_b,
      guint          mat_a,
      guint          mat_b,
      GArray        *out)
{
	LrgContactManifold m;

	if (w->count == 0)
		return;
	emit_fill (w, shift, centre, child_a, child_b, sub_a, sub_b, mat_a, mat_b, &m);
	g_array_append_val (out, m);
}

/* ==========================================================================
 * Specialised pairs
 * ========================================================================== */

/* Sphere (A) vs box (B), closed form.  Returns FALSE beyond @spec. */
static gboolean
sphere_box (const Proxy *A,
            const Proxy *B,
            gdouble      spec,
            Penetration *pen)
{
	gdouble rel[3];
	gdouble lc[3];
	gdouble q[3];
	gdouble h[3];
	gdouble r;
	guint i;
	gboolean inside;

	r = A->radius;
	d3_sub (rel, A->t, B->t);
	for (i = 0; i < 3; i++)
	{
		lc[i] = B->R[0 * 3 + i] * rel[0] + B->R[1 * 3 + i] * rel[1] + B->R[2 * 3 + i] * rel[2];
		h[i] = (gdouble)B->shape->half[i];
		q[i] = CLAMP (lc[i], -h[i], h[i]);
	}
	inside = (q[0] == lc[0] && q[1] == lc[1] && q[2] == lc[2]);
	{
		gdouble nl[3];
		gdouble dist;
		gdouble wq[3];
		guint k;

		if (!inside)
		{
			gdouble d[3];

			d3_sub (d, lc, q);
			dist = d3_len (d);
			if (dist - r > spec)
				return FALSE;
			d3_scale (nl, d, 1.0 / dist);
			/* nl points from the box surface to the sphere centre. */
			pen->depth = r - dist;
		}
		else
		{
			/* Centre inside: leave through the nearest face. */
			gdouble best;
			guint ax;
			gdouble sgn;

			best = G_MAXDOUBLE;
			ax = 0;
			sgn = 1.0;
			for (k = 0; k < 3; k++)
			{
				gdouble dp;
				gdouble dn;

				dp = h[k] - lc[k];
				dn = lc[k] + h[k];
				if (dp < best)
				{
					best = dp;
					ax = k;
					sgn = 1.0;
				}
				if (dn < best)
				{
					best = dn;
					ax = k;
					sgn = -1.0;
				}
			}
			d3_set (nl, 0.0, 0.0, 0.0);
			nl[ax] = sgn;
			d3_copy (q, lc);
			q[ax] = sgn * h[ax];
			pen->depth = r + best;
		}
		/* World: n (A->B) = -(box->sphere normal). */
		for (k = 0; k < 3; k++)
		{
			wq[k] = B->R[k * 3 + 0] * q[0] + B->R[k * 3 + 1] * q[1] + B->R[k * 3 + 2] * q[2] + B->t[k];
			pen->n[k] = -(B->R[k * 3 + 0] * nl[0] + B->R[k * 3 + 1] * nl[1] + B->R[k * 3 + 2] * nl[2]);
		}
		d3_copy (pen->pb, wq);
		d3_madd (pen->pa, A->t, pen->n, r);
	}
	return TRUE;
}

/* Box (A) vs box (B): separating axis test over the 15 axes.  Writes the
 * penetration and, for an edge-edge axis, the single contact into @w.
 * Returns 0 for no contact, 1 for a face axis (caller clips faces), 2 for
 * an edge contact already written. */
static guint
box_box (const Proxy *A,
         const Proxy *B,
         gdouble      spec,
         Penetration *pen,
         Work        *w)
{
	gdouble ua[3][3];
	gdouble ub[3][3];
	gdouble ea[3];
	gdouble eb[3];
	gdouble Rm[3][3];
	gdouble AR[3][3];
	gdouble t[3];
	gdouble tw[3];
	guint i;
	guint j;
	gdouble best_face;
	gdouble face_axis[3];
	gdouble best_edge;
	gdouble edge_axis[3];
	guint edge_i;
	guint edge_j;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
		{
			ua[i][j] = A->R[j * 3 + i];
			ub[i][j] = B->R[j * 3 + i];
		}
		ea[i] = (gdouble)A->shape->half[i];
		eb[i] = (gdouble)B->shape->half[i];
	}
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
		{
			Rm[i][j] = d3_dot (ua[i], ub[j]);
			AR[i][j] = fabs (Rm[i][j]) + 1e-9;
		}
	d3_sub (tw, B->t, A->t);
	for (i = 0; i < 3; i++)
		t[i] = d3_dot (tw, ua[i]);

	best_face = G_MAXDOUBLE;
	best_edge = G_MAXDOUBLE;
	edge_i = 0;
	edge_j = 0;
	d3_set (face_axis, 0.0, 1.0, 0.0);
	d3_set (edge_axis, 0.0, 1.0, 0.0);

	/* A's face axes. */
	for (i = 0; i < 3; i++)
	{
		gdouble rb;
		gdouble p;

		rb = eb[0] * AR[i][0] + eb[1] * AR[i][1] + eb[2] * AR[i][2];
		p = ea[i] + rb - fabs (t[i]);
		if (p < -spec)
			return 0;
		if (p < best_face)
		{
			best_face = p;
			d3_scale (face_axis, ua[i], t[i] >= 0.0 ? 1.0 : -1.0);
		}
	}
	/* B's face axes (slightly penalised so A's win ties: stable choice). */
	for (j = 0; j < 3; j++)
	{
		gdouble ra;
		gdouble tb;
		gdouble p;

		ra = ea[0] * AR[0][j] + ea[1] * AR[1][j] + ea[2] * AR[2][j];
		tb = t[0] * Rm[0][j] + t[1] * Rm[1][j] + t[2] * Rm[2][j];
		p = ra + eb[j] - fabs (tb);
		if (p < -spec)
			return 0;
		if (p < best_face * 0.999 - 1e-6)
		{
			best_face = p;
			d3_scale (face_axis, ub[j], tb >= 0.0 ? 1.0 : -1.0);
		}
	}
	/* Edge-edge axes ua[i] x ub[j]. */
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
		{
			gdouble L[3];
			gdouble ll;
			gdouble ra;
			gdouble rb;
			gdouble tl;
			gdouble p;
			guint k;

			d3_cross (L, ua[i], ub[j]);
			ll = d3_len (L);
			if (ll < 1e-5)
				continue;
			d3_scale (L, L, 1.0 / ll);
			ra = 0.0;
			rb = 0.0;
			for (k = 0; k < 3; k++)
			{
				ra += ea[k] * fabs (d3_dot (ua[k], L));
				rb += eb[k] * fabs (d3_dot (ub[k], L));
			}
			tl = d3_dot (tw, L);
			p = ra + rb - fabs (tl);
			if (p < -spec)
				return 0;
			if (p < best_edge)
			{
				best_edge = p;
				d3_scale (edge_axis, L, tl >= 0.0 ? 1.0 : -1.0);
				edge_i = i;
				edge_j = j;
			}
		}

	/* Prefer faces unless an edge axis is clearly better. */
	if (best_edge < best_face * 0.95 - 0.005)
	{
		gdouble ca[3];
		gdouble cb[3];
		gdouble a0[3];
		gdouble a1[3];
		gdouble b0[3];
		gdouble b1[3];
		gdouble pa[3];
		gdouble pb[3];
		guint k;
		guint32 id;
		gdouble d[3];

		/* A's edge parallel to ua[edge_i] furthest along +n, B's along -n. */
		d3_copy (ca, A->t);
		d3_copy (cb, B->t);
		id = 2166136261u;
		for (k = 0; k < 3; k++)
		{
			if (k != edge_i)
			{
				gdouble s;

				s = d3_dot (edge_axis, ua[k]) >= 0.0 ? 1.0 : -1.0;
				d3_madd (ca, ca, ua[k], s * ea[k]);
				id = mix_id (id, s > 0.0 ? k * 2 : k * 2 + 1);
			}
			if (k != edge_j)
			{
				gdouble s;

				s = d3_dot (edge_axis, ub[k]) <= 0.0 ? 1.0 : -1.0;
				d3_madd (cb, cb, ub[k], s * eb[k]);
				id = mix_id (id, 0x10u + (s > 0.0 ? k * 2 : k * 2 + 1));
			}
		}
		id = mix_id (id, 0x100u + edge_i * 3 + edge_j);
		d3_madd (a0, ca, ua[edge_i], -ea[edge_i]);
		d3_madd (a1, ca, ua[edge_i], ea[edge_i]);
		d3_madd (b0, cb, ub[edge_j], -eb[edge_j]);
		d3_madd (b1, cb, ub[edge_j], eb[edge_j]);
		closest_seg_seg (a0, a1, b0, b1, pa, pb);
		d3_copy (pen->n, edge_axis);
		d3_copy (pen->pa, pa);
		d3_copy (pen->pb, pb);
		d3_sub (d, pa, pb);
		pen->depth = d3_dot (d, edge_axis);
		if (pen->depth < -spec)
			return 0;
		d3_copy (w->n, edge_axis);
		work_add (w, pa, pb, id);
		return 2;
	}

	/* Face axis: deepest points along it for the fallback single point. */
	d3_copy (pen->n, face_axis);
	pen->depth = best_face;
	{
		gdouble nd[3];

		proxy_support_full (A, face_axis, pen->pa);
		d3_scale (nd, face_axis, -1.0);
		proxy_support_full (B, nd, pen->pb);
	}
	return 1;
}

/* ==========================================================================
 * Pair handlers
 * ========================================================================== */

/* Convex A vs convex B (both proxies in the working frame). */
static gboolean
collide_convex_convex (const Proxy *A,
                       const Proxy *B,
                       gdouble      spec,
                       Work        *w)
{
	Penetration pen;
	LrgPhysicsShapeKind ka;
	LrgPhysicsShapeKind kb;

	work_init (w);
	ka = A->shape != NULL ? A->shape->kind : LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH;
	kb = B->shape != NULL ? B->shape->kind : LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH;

	/* Sphere-sphere: one point, closed form. */
	if (ka == LRG_PHYSICS_SHAPE_KIND_SPHERE && kb == LRG_PHYSICS_SHAPE_KIND_SPHERE)
	{
		gdouble d[3];
		gdouble l;
		gdouble rsum;

		rsum = A->radius + B->radius;
		d3_sub (d, B->t, A->t);
		l = d3_len (d);
		if (l - rsum > spec)
			return FALSE;
		if (l > 1e-12)
			d3_scale (pen.n, d, 1.0 / l);
		else
			d3_set (pen.n, 0.0, 1.0, 0.0);
		d3_copy (w->n, pen.n);
		d3_madd (pen.pa, A->t, pen.n, A->radius);
		d3_madd (pen.pb, B->t, pen.n, -B->radius);
		work_add (w, pen.pa, pen.pb, 1u);
		return TRUE;
	}

	/* Sphere-box / box-sphere. */
	if (ka == LRG_PHYSICS_SHAPE_KIND_SPHERE && kb == LRG_PHYSICS_SHAPE_KIND_BOX)
	{
		if (!sphere_box (A, B, spec, &pen))
			return FALSE;
		d3_copy (w->n, pen.n);
		work_add (w, pen.pa, pen.pb, 2u);
		return TRUE;
	}
	if (ka == LRG_PHYSICS_SHAPE_KIND_BOX && kb == LRG_PHYSICS_SHAPE_KIND_SPHERE)
	{
		if (!sphere_box (B, A, spec, &pen))
			return FALSE;
		d3_scale (w->n, pen.n, -1.0);
		work_add (w, pen.pb, pen.pa, 2u);
		return TRUE;
	}

	/* Box-box: SAT, then faces (or one edge point). */
	if (ka == LRG_PHYSICS_SHAPE_KIND_BOX && kb == LRG_PHYSICS_SHAPE_KIND_BOX)
	{
		guint r;

		r = box_box (A, B, spec, &pen, w);
		if (r == 0)
			return FALSE;
		if (r == 1)
			manifold_between_faces (A, B, &pen, spec, w);
		return w->count > 0;
	}

	/* Everything else: GJK/EPA (closed-form cores for sphere/capsule
	 * pairs), then face clipping. */
	if (!convex_penetration (A, B, spec, &pen))
		return FALSE;
	manifold_between_faces (A, B, &pen, spec, w);
	return w->count > 0;
}

/* Port of ActiveEdges::FixNormal: returns TRUE if @normal (pointing out
 * of the triangle) should be replaced by the triangle normal because the
 * contact lies on an inactive edge or vertex. */
static gboolean
fix_normal_needed (const gdouble *v0,
                   const gdouble *v1,
                   const gdouble *v2,
                   const gdouble *tri_n,
                   guint          active,
                   const gdouble *point,
                   const gdouble *normal)
{
	gdouble a[3];
	gdouble b[3];
	gdouble c[3];
	gdouble u;
	gdouble v;
	gdouble w;
	guint edge;
	const gdouble eps = 1e-4;

	if (active == 7)
		return FALSE;
	if (active == 0)
		return TRUE;
	/* Parallel to the face normal already (within 1 degree). */
	if (d3_dot (tri_n, normal) > 0.999848 * d3_len (normal) * d3_len (tri_n))
		return FALSE;
	d3_sub (a, v0, point);
	d3_sub (b, v1, point);
	d3_sub (c, v2, point);
	bary3 (a, b, c, &u, &v, &w);
	if (u > 1.0 - eps)
		edge = 5;
	else if (v > 1.0 - eps)
		edge = 3;
	else if (w > 1.0 - eps)
		edge = 6;
	else if (u < eps)
		edge = 2;
	else if (v < eps)
		edge = 4;
	else if (w < eps)
		edge = 1;
	else
		return TRUE;  /* interior: the face normal is the right one */
	return (active & edge) == 0;
}

typedef struct
{
	const Proxy *A;
	const gfloat *xf_static;  /* static shape's world matrix */
	const gdouble *shift;
	gdouble       spec;
	GArray       *works;      /* Work */
	gboolean      overlap_only;
	gboolean      found;
} TriCtx;

/* Collides the convex proxy with one (local) triangle of the static shape. */
static gboolean
tri_cb (guint         index,
        const gfloat *v,
        guint         material,
        guint         user_id,
        guint         active_edges,
        gpointer      user_data)
{
	TriCtx *ctx;
	gdouble wv[9];
	Proxy T;
	Penetration pen;
	Work w;
	gdouble tn[3];
	gdouble e1[3];
	gdouble e2[3];
	gdouble toa[3];
	gdouble out_n[3];
	guint k;

	(void)user_id;
	ctx = (TriCtx *)user_data;
	for (k = 0; k < 3; k++)
	{
		gfloat p[3];

		lrg_math3d_transform_point (ctx->xf_static, &v[k * 3], p);
		wv[k * 3 + 0] = (gdouble)p[0] - ctx->shift[0];
		wv[k * 3 + 1] = (gdouble)p[1] - ctx->shift[1];
		wv[k * 3 + 2] = (gdouble)p[2] - ctx->shift[2];
	}
	proxy_init_tri (&T, wv, index);

	if (!convex_penetration (ctx->A, &T, ctx->spec, &pen))
		return TRUE;
	if (ctx->overlap_only)
	{
		if (pen.depth >= 0.0)
		{
			ctx->found = TRUE;
			return FALSE;
		}
		return TRUE;
	}

	/* Triangle normal, turned towards the convex shape (two-sided). */
	d3_sub (e1, &wv[3], &wv[0]);
	d3_sub (e2, &wv[6], &wv[0]);
	d3_cross (tn, e1, e2);
	d3_normalize (tn, tn);
	d3_sub (toa, ctx->A->t, &wv[0]);
	if (d3_dot (tn, toa) < 0.0)
		d3_scale (tn, tn, -1.0);

	/* Internal edge removal: an edge/vertex normal on an inactive
	 * feature becomes the face normal and the depth is re-measured. */
	d3_scale (out_n, pen.n, -1.0);
	if (fix_normal_needed (&wv[0], &wv[3], &wv[6], tn, active_edges, pen.pb, out_n))
	{
		gdouble d[3];
		gdouble depth;

		d3_scale (pen.n, tn, -1.0);
		proxy_support_full (ctx->A, pen.n, pen.pa);
		d3_sub (d, pen.pa, &wv[0]);
		depth = -d3_dot (d, tn);
		if (depth < -ctx->spec)
			return TRUE;
		pen.depth = depth;
		d3_madd (pen.pb, pen.pa, tn, depth);
	}

	work_init (&w);
	manifold_between_faces (ctx->A, &T, &pen, ctx->spec, &w);
	if (w.count == 0)
		return TRUE;
	/* Feature ids carry the triangle id. */
	for (k = 0; k < w.count; k++)
		w.id[k] = mix_id (w.id[k], index + 0x9e3779b9u);
	w.sub_b = index;
	w.mat_b = material;
	g_array_append_val (ctx->works, w);
	return TRUE;
}

/* Sorts working manifolds deepest first, ties by triangle id. */
static gint
work_cmp (gconstpointer a,
          gconstpointer b)
{
	const Work *wa;
	const Work *wb;

	wa = (const Work *)a;
	wb = (const Work *)b;
	if (wa->max_depth > wb->max_depth)
		return -1;
	if (wa->max_depth < wb->max_depth)
		return 1;
	return (wa->sub_b < wb->sub_b) ? -1 : (wa->sub_b > wb->sub_b ? 1 : 0);
}

/* Convex A vs a mesh or heightfield B.  Manifolds with matching normals
 * are merged. */
static void
collide_convex_triangles (LrgPhysicsShape *a,
                          const gfloat    *xa,
                          LrgPhysicsShape *b,
                          const gfloat    *xb,
                          gdouble          spec,
                          gint             child_a,
                          gint             child_b,
                          GArray          *out)
{
	gdouble shift[3];
	Proxy A;
	TriCtx ctx;
	gfloat wmin[3];
	gfloat wmax[3];
	gfloat inv[16];
	gfloat lmin[3];
	gfloat lmax[3];
	g_autoptr(GArray) works = NULL;
	g_autoptr(GArray) merged = NULL;
	guint i;
	guint k;

	for (k = 0; k < 3; k++)
		shift[k] = (gdouble)xa[12 + k];
	proxy_init_shape (&A, a, xa, shift);

	/* The convex shape's bounds in the static shape's local frame. */
	lrg_physics_shape_get_world_bounds (a, xa, wmin, wmax);
	for (k = 0; k < 3; k++)
	{
		wmin[k] -= (gfloat)spec;
		wmax[k] += (gfloat)spec;
	}
	lrg_math3d_invert (xb, inv);
	{
		gfloat c[3];
		gfloat e[3];
		gfloat lc[3];

		for (k = 0; k < 3; k++)
		{
			c[k] = (wmin[k] + wmax[k]) * 0.5f;
			e[k] = (wmax[k] - wmin[k]) * 0.5f;
		}
		lrg_math3d_transform_point (inv, c, lc);
		for (k = 0; k < 3; k++)
		{
			gfloat le;

			le = fabsf (inv[0 + k]) * e[0] + fabsf (inv[4 + k]) * e[1] + fabsf (inv[8 + k]) * e[2];
			lmin[k] = lc[k] - le;
			lmax[k] = lc[k] + le;
		}
	}

	works = g_array_new (FALSE, FALSE, sizeof (Work));
	ctx.A = &A;
	ctx.xf_static = xb;
	ctx.shift = shift;
	ctx.spec = spec;
	ctx.works = works;
	ctx.overlap_only = FALSE;
	ctx.found = FALSE;
	lrg_physics_shape_foreach_triangle (b, lmin, lmax, tri_cb, &ctx);
	if (works->len == 0)
		return;

	/* Merge manifolds whose normals agree within ~2.5 degrees. */
	g_array_sort (works, work_cmp);
	merged = g_array_new (FALSE, FALSE, sizeof (Work));
	for (i = 0; i < works->len; i++)
	{
		Work *w;
		guint m;
		gboolean done;

		w = &g_array_index (works, Work, i);
		done = FALSE;
		for (m = 0; m < merged->len && !done; m++)
		{
			Work *g;

			g = &g_array_index (merged, Work, m);
			if (d3_dot (g->n, w->n) > 0.999)
			{
				for (k = 0; k < w->count; k++)
				{
					gdouble pb[3];
					gdouble d[3];

					/* Re-project onto the group normal's depth. */
					d3_sub (d, w->pa[k], w->pb[k]);
					d3_madd (pb, w->pa[k], g->n, -d3_dot (d, g->n));
					work_add (g, w->pa[k], pb, w->id[k]);
				}
				done = TRUE;
			}
		}
		if (!done)
			g_array_append_val (merged, *w);
	}
	for (i = 0; i < merged->len; i++)
	{
		Work *w;

		w = &g_array_index (merged, Work, i);
		emit (w, shift, A.t, child_a, child_b, LRG_PHYSICS_NO_SUB_SHAPE, w->sub_b, 0, w->mat_b,
		      out);
	}
}

/* Convex A vs plane B. */
static void
collide_convex_plane (LrgPhysicsShape *a,
                      const gfloat    *xa,
                      LrgPhysicsShape *b,
                      const gfloat    *xb,
                      gdouble          spec,
                      gint             child_a,
                      gint             child_b,
                      GArray          *out)
{
	gdouble shift[3];
	Proxy A;
	gdouble pn[3];
	gdouble pd;
	gdouble n[3];
	gdouble deep[3];
	gdouble f[LRG_SHAPE_MAX_FACE_POINTS * 3];
	guint nf;
	guint id;
	guint i;
	Work w;
	gfloat wn[3];
	gfloat lp[3];
	gfloat wp[3];

	for (i = 0; i < 3; i++)
		shift[i] = (gdouble)xa[12 + i];
	proxy_init_shape (&A, a, xa, shift);

	/* World plane: normal and a point, shifted. */
	lrg_math3d_transform_direction (xb, b->plane_n, wn);
	lp[0] = b->plane_n[0] * b->plane_d;
	lp[1] = b->plane_n[1] * b->plane_d;
	lp[2] = b->plane_n[2] * b->plane_d;
	lrg_math3d_transform_point (xb, lp, wp);
	for (i = 0; i < 3; i++)
		pn[i] = (gdouble)wn[i];
	d3_normalize (pn, pn);
	pd = 0.0;
	for (i = 0; i < 3; i++)
		pd += pn[i] * ((gdouble)wp[i] - shift[i]);

	/* A -> B normal is into the plane. */
	d3_scale (n, pn, -1.0);
	proxy_support_full (&A, n, deep);
	if (pd - d3_dot (pn, deep) < -spec)
		return;

	work_init (&w);
	d3_copy (w.n, n);
	nf = proxy_face (&A, n, f, &id);
	for (i = 0; i < nf; i++)
	{
		gdouble depth;
		gdouble pb[3];

		depth = pd - d3_dot (pn, &f[i * 3]);
		if (depth < -spec)
			continue;
		d3_madd (pb, &f[i * 3], pn, depth);
		work_add (&w, &f[i * 3], pb, mix_id (mix_id (2166136261u, id), i));
	}
	if (w.count == 0)
	{
		gdouble pb[3];
		gdouble depth;

		depth = pd - d3_dot (pn, deep);
		d3_madd (pb, deep, pn, depth);
		work_add (&w, deep, pb, mix_id (2166136261u, 0xfffu));
	}
	emit (&w, shift, A.t, child_a, child_b, LRG_PHYSICS_NO_SUB_SHAPE, LRG_PHYSICS_NO_SUB_SHAPE,
	      0, 0, out);
}

static gboolean
is_static_kind (LrgPhysicsShapeKind k)
{
	return k == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH || k == LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD
	       || k == LRG_PHYSICS_SHAPE_KIND_PLANE;
}

static gboolean
aabb_overlap (const gfloat *amin,
              const gfloat *amax,
              const gfloat *bmin,
              const gfloat *bmax,
              gfloat        margin)
{
	guint i;

	for (i = 0; i < 3; i++)
		if (amin[i] > bmax[i] + margin || amax[i] < bmin[i] - margin)
			return FALSE;
	return TRUE;
}

/* Flips manifolds [first, len) so A and B swap roles. */
static void
flip_manifolds (GArray *out,
                guint   first)
{
	guint i;

	for (i = first; i < out->len; i++)
	{
		LrgContactManifold *m;
		LrgContactManifold t;
		guint k;

		m = &g_array_index (out, LrgContactManifold, i);
		t = *m;
		for (k = 0; k < 3; k++)
			m->normal[k] = -t.normal[k];
		memcpy (m->points_a, t.points_b, sizeof (m->points_a));
		memcpy (m->points_b, t.points_a, sizeof (m->points_b));
		m->child_a = t.child_b;
		m->child_b = t.child_a;
		m->sub_shape_a = t.sub_shape_b;
		m->sub_shape_b = t.sub_shape_a;
		m->material_a = t.material_b;
		m->material_b = t.material_a;
	}
}

/* Main dispatcher: compounds recurse, static-vs-convex swaps, then the
 * pair handler.  @child_a / @child_b are the top-level child indices. */
static void
collide_dispatch (LrgPhysicsShape *a,
                  const gfloat    *xa,
                  gint             child_a,
                  LrgPhysicsShape *b,
                  const gfloat    *xb,
                  gint             child_b,
                  gdouble          spec,
                  GArray          *out)
{
	LrgPhysicsShapeKind ka;
	LrgPhysicsShapeKind kb;

	ka = a->kind;
	kb = b->kind;

	if (ka == LRG_PHYSICS_SHAPE_KIND_COMPOUND || kb == LRG_PHYSICS_SHAPE_KIND_COMPOUND)
	{
		gboolean split_a;
		LrgPhysicsShape *c;
		const gfloat *xc;
		LrgPhysicsShape *o;
		const gfloat *xo;
		gfloat omin[3];
		gfloat omax[3];
		guint i;

		/* Recurse into the compound side (A first). */
		split_a = ka == LRG_PHYSICS_SHAPE_KIND_COMPOUND;
		c = split_a ? a : b;
		xc = split_a ? xa : xb;
		o = split_a ? b : a;
		xo = split_a ? xb : xa;
		lrg_physics_shape_get_world_bounds (o, xo, omin, omax);
		for (i = 0; i < c->children->len; i++)
		{
			const LrgShapeChild *ch;
			gfloat cx[16];
			gfloat cmin[3];
			gfloat cmax[3];

			ch = lrg_physics_shape_get_child_internal (c, i);
			lrg_math3d_multiply (xc, ch->xf, cx);
			if (o->kind != LRG_PHYSICS_SHAPE_KIND_PLANE)
			{
				lrg_physics_shape_get_world_bounds (ch->shape, cx, cmin, cmax);
				if (!aabb_overlap (cmin, cmax, omin, omax, (gfloat)spec))
					continue;
			}
			if (split_a)
				collide_dispatch (ch->shape, cx, child_a < 0 ? (gint)i : child_a, b, xb, child_b,
				                  spec, out);
			else
				collide_dispatch (a, xa, child_a, ch->shape, cx, child_b < 0 ? (gint)i : child_b,
				                  spec, out);
		}
		return;
	}

	if (is_static_kind (ka) && is_static_kind (kb))
		return;

	if (is_static_kind (ka))
	{
		guint first;

		first = out->len;
		collide_dispatch (b, xb, child_b, a, xa, child_a, spec, out);
		flip_manifolds (out, first);
		return;
	}

	if (kb == LRG_PHYSICS_SHAPE_KIND_PLANE)
	{
		collide_convex_plane (a, xa, b, xb, spec, child_a, child_b, out);
		return;
	}
	if (kb == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH || kb == LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD)
	{
		collide_convex_triangles (a, xa, b, xb, spec, child_a, child_b, out);
		return;
	}

	/* Convex vs convex. */
	{
		gdouble shift[3];
		Proxy A;
		Proxy B;
		Work w;
		guint k;

		for (k = 0; k < 3; k++)
			shift[k] = (gdouble)xa[12 + k];
		proxy_init_shape (&A, a, xa, shift);
		proxy_init_shape (&B, b, xb, shift);
		if (collide_convex_convex (&A, &B, spec, &w))
			emit (&w, shift, A.t, child_a, child_b, LRG_PHYSICS_NO_SUB_SHAPE,
			      LRG_PHYSICS_NO_SUB_SHAPE, 0, 0, out);
	}
}

/* Deepest-first order of the public manifolds (stable on ties). */
static gint
manifold_cmp (gconstpointer pa,
              gconstpointer pb)
{
	const LrgContactManifold *a;
	const LrgContactManifold *b;
	gfloat da;
	gfloat db;
	guint i;

	a = (const LrgContactManifold *)pa;
	b = (const LrgContactManifold *)pb;
	da = -G_MAXFLOAT;
	db = -G_MAXFLOAT;
	for (i = 0; i < a->n_points; i++)
		da = MAX (da, a->depths[i]);
	for (i = 0; i < b->n_points; i++)
		db = MAX (db, b->depths[i]);
	if (da > db)
		return -1;
	if (da < db)
		return 1;
	return 0;
}

/* ==========================================================================
 * Public contact API
 * ========================================================================== */

guint
lrg_collide_shapes_all (LrgPhysicsShape *a,
                        const gfloat    *xf_a,
                        LrgPhysicsShape *b,
                        const gfloat    *xf_b,
                        gfloat           speculative_distance,
                        GArray          *out)
{
	g_autoptr(GArray) tmp = NULL;
	gdouble spec;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (a), 0);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (b), 0);
	g_return_val_if_fail (xf_a != NULL && xf_b != NULL, 0);
	g_return_val_if_fail (out != NULL, 0);
	g_return_val_if_fail (g_array_get_element_size (out) == sizeof (LrgContactManifold), 0);

	spec = (isfinite (speculative_distance) && speculative_distance > 0.0f)
	       ? (gdouble)speculative_distance : 0.0;
	tmp = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	collide_dispatch (a, xf_a, -1, b, xf_b, -1, spec, tmp);
	if (tmp->len > 1)
		g_array_sort (tmp, manifold_cmp);
	if (tmp->len > 0)
		g_array_append_vals (out, tmp->data, tmp->len);
	return tmp->len;
}

gboolean
lrg_collide_shapes (LrgPhysicsShape    *a,
                    const gfloat       *xf_a,
                    LrgPhysicsShape    *b,
                    const gfloat       *xf_b,
                    LrgContactManifold *out,
                    gfloat              speculative_distance)
{
	g_autoptr(GArray) all = NULL;

	g_return_val_if_fail (out != NULL, FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (a), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (b), FALSE);
	g_return_val_if_fail (xf_a != NULL && xf_b != NULL, FALSE);
	lrg_contact_manifold_reset (out);

	/* Convex pairs make at most one manifold: skip the arrays. */
	if (lrg_physics_shape_is_convex (a) && lrg_physics_shape_is_convex (b))
	{
		gdouble shift[3];
		gdouble spec;
		Proxy A;
		Proxy B;
		Work w;
		guint k;

		spec = (isfinite (speculative_distance) && speculative_distance > 0.0f)
		       ? (gdouble)speculative_distance : 0.0;
		for (k = 0; k < 3; k++)
			shift[k] = (gdouble)xf_a[12 + k];
		proxy_init_shape (&A, a, xf_a, shift);
		proxy_init_shape (&B, b, xf_b, shift);
		if (!collide_convex_convex (&A, &B, spec, &w))
			return FALSE;
		emit_fill (&w, shift, A.t, -1, -1, LRG_PHYSICS_NO_SUB_SHAPE, LRG_PHYSICS_NO_SUB_SHAPE,
		           0, 0, out);
		return out->n_points > 0;
	}

	all = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	if (lrg_collide_shapes_all (a, xf_a, b, xf_b, speculative_distance, all) == 0)
		return FALSE;
	*out = g_array_index (all, LrgContactManifold, 0);
	return out->n_points > 0;
}

/* Closest points of two convex proxies: distance (negative when
 * penetrating), points and the A->B normal. */
static void
proxy_closest (const Proxy *A,
               const Proxy *B,
               gdouble     *dist,
               gdouble     *pa,
               gdouble     *pb,
               gdouble     *n)
{
	Penetration pen;

	if (convex_penetration (A, B, G_MAXDOUBLE, &pen))
	{
		*dist = -pen.depth;
		d3_copy (pa, pen.pa);
		d3_copy (pb, pen.pb);
		d3_copy (n, pen.n);
		return;
	}
	/* Unreachable with an infinite speculative distance. */
	*dist = G_MAXDOUBLE;
	d3_copy (pa, A->t);
	d3_copy (pb, B->t);
	fallback_normal (A, B, n);
}

gboolean
lrg_collide_closest_points (LrgPhysicsShape *a,
                            const gfloat    *xf_a,
                            LrgPhysicsShape *b,
                            const gfloat    *xf_b,
                            gfloat          *distance,
                            gfloat          *point_a,
                            gfloat          *point_b,
                            gfloat          *normal)
{
	gdouble shift[3];
	Proxy A;
	Proxy B;
	gdouble d;
	gdouble pa[3];
	gdouble pb[3];
	gdouble n[3];
	guint k;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (a), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (b), FALSE);
	g_return_val_if_fail (xf_a != NULL && xf_b != NULL, FALSE);
	if (!lrg_physics_shape_is_convex (a) || !lrg_physics_shape_is_convex (b))
		return FALSE;

	for (k = 0; k < 3; k++)
		shift[k] = (gdouble)xf_a[12 + k];
	proxy_init_shape (&A, a, xf_a, shift);
	proxy_init_shape (&B, b, xf_b, shift);
	proxy_closest (&A, &B, &d, pa, pb, n);
	if (distance != NULL)
		*distance = (gfloat)d;
	for (k = 0; k < 3; k++)
	{
		if (point_a != NULL)
			point_a[k] = (gfloat)(pa[k] + shift[k]);
		if (point_b != NULL)
			point_b[k] = (gfloat)(pb[k] + shift[k]);
		if (normal != NULL)
			normal[k] = (gfloat)n[k];
	}
	return TRUE;
}

gboolean
lrg_collide_overlap (LrgPhysicsShape *a,
                     const gfloat    *xf_a,
                     LrgPhysicsShape *b,
                     const gfloat    *xf_b)
{
	g_autoptr(GArray) all = NULL;
	guint i;
	guint k;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (a), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (b), FALSE);
	g_return_val_if_fail (xf_a != NULL && xf_b != NULL, FALSE);

	/* Convex pairs: GJK only. */
	if (lrg_physics_shape_is_convex (a) && lrg_physics_shape_is_convex (b))
	{
		gdouble shift[3];
		Proxy A;
		Proxy B;
		gdouble pa[3];
		gdouble pb[3];
		Simplex s;
		gdouble d;

		for (k = 0; k < 3; k++)
			shift[k] = (gdouble)xf_a[12 + k];
		proxy_init_shape (&A, a, xf_a, shift);
		proxy_init_shape (&B, b, xf_b, shift);
		d = gjk (&A, &B, FALSE, A.radius + B.radius, pa, pb, &s);
		return d <= A.radius + B.radius + 1e-9;
	}

	all = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	lrg_collide_shapes_all (a, xf_a, b, xf_b, 0.0f, all);
	for (i = 0; i < all->len; i++)
	{
		const LrgContactManifold *m;

		m = &g_array_index (all, LrgContactManifold, i);
		for (k = 0; k < m->n_points; k++)
			if (m->depths[k] >= 0.0f)
				return TRUE;
	}
	return FALSE;
}

/* ==========================================================================
 * Rays
 * ========================================================================== */

gboolean
lrg_collide_ray (LrgPhysicsShape *shape,
                 const gfloat    *xf,
                 const gfloat    *origin,
                 const gfloat    *dir,
                 gfloat           max_fraction,
                 LrgRayHit       *hit)
{
	gfloat inv[16];
	gfloat lo[3];
	gfloat ld[3];
	LrgRayHit local;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (shape), FALSE);
	g_return_val_if_fail (xf != NULL && origin != NULL && dir != NULL, FALSE);

	lrg_math3d_invert (xf, inv);
	lrg_math3d_transform_point (inv, origin, lo);
	lrg_math3d_transform_direction (inv, dir, ld);
	if (!lrg_physics_shape_cast_ray (shape, lo, ld, max_fraction, &local))
		return FALSE;
	if (hit != NULL)
	{
		guint k;

		*hit = local;
		for (k = 0; k < 3; k++)
			hit->point[k] = origin[k] + dir[k] * local.fraction;
		lrg_math3d_transform_direction (xf, local.normal, hit->normal);
	}
	return TRUE;
}

gfloat
lrg_collide_internal_core_distance (LrgPhysicsShape *shape,
                                    const gfloat    *point,
                                    gfloat          *closest)
{
	Proxy A;
	Proxy P;
	gdouble shift[3];
	gdouble tri[9];
	gdouble pa[3];
	gdouble pb[3];
	Simplex s;
	gdouble d;
	gfloat id[16];
	guint k;

	lrg_math3d_identity (id);
	d3_set (shift, 0.0, 0.0, 0.0);
	proxy_init_shape (&A, shape, id, shift);
	for (k = 0; k < 9; k++)
		tri[k] = (gdouble)point[k % 3];
	proxy_init_tri (&P, tri, 0);
	d = gjk (&A, &P, FALSE, G_MAXDOUBLE, pa, pb, &s);
	for (k = 0; k < 3; k++)
		closest[k] = (gfloat)pa[k];
	return (gfloat)d;
}

/* ==========================================================================
 * Shape cast: conservative advancement
 * ========================================================================== */

typedef struct
{
	gfloat p0[3];
	gfloat p1[3];
	gfloat q0[4];
	gfloat q1[4];
	gfloat angle;   /* total rotation angle over the sweep */
} Sweep;

static void
sweep_init (Sweep        *sw,
            const gfloat *from,
            const gfloat *to)
{
	gfloat d;

	sw->p0[0] = from[12]; sw->p0[1] = from[13]; sw->p0[2] = from[14];
	sw->p1[0] = to[12]; sw->p1[1] = to[13]; sw->p1[2] = to[14];
	lrg_math3d_matrix_to_quat (from, sw->q0);
	lrg_math3d_matrix_to_quat (to, sw->q1);
	d = fabsf (sw->q0[0] * sw->q1[0] + sw->q0[1] * sw->q1[1] + sw->q0[2] * sw->q1[2]
	           + sw->q0[3] * sw->q1[3]);
	sw->angle = 2.0f * acosf (MIN (1.0f, d));
}

static void
sweep_at (const Sweep *sw,
          gfloat       t,
          gfloat      *xf)
{
	gfloat p[3];
	gfloat q[4];
	gfloat one[3];
	guint k;

	for (k = 0; k < 3; k++)
		p[k] = sw->p0[k] + (sw->p1[k] - sw->p0[k]) * t;
	lrg_math3d_quat_slerp (sw->q0, sw->q1, t, q);
	one[0] = one[1] = one[2] = 1.0f;
	lrg_math3d_compose_quat (p, q, one, xf);
}

#define CAST_TOLERANCE (1e-3)

/* Conservative advancement of the moving convex shape @m (child matrix
 * @cxf relative to the swept frame) against one static convex proxy
 * provider.  @target_tri non-NULL: the target is that world triangle. */
static gboolean
cast_convex (LrgPhysicsShape *m,
             const gfloat    *cxf,
             const Sweep     *sw,
             LrgPhysicsShape *target,
             const gfloat    *xf_target,
             const gdouble   *target_tri,
             gfloat           t_max,
             gfloat          *toi,
             LrgRayHit       *hit)
{
	gdouble disp[3];
	gdouble rmax;
	gfloat t;
	guint it;
	guint k;

	for (k = 0; k < 3; k++)
		disp[k] = (gdouble)(sw->p1[k] - sw->p0[k]);
	/* Lever arm of the child's surface around the sweep pivot. */
	rmax = (gdouble)m->max_extent + sqrt ((gdouble)(cxf[12] * cxf[12] + cxf[13] * cxf[13]
	                                                 + cxf[14] * cxf[14]));
	t = 0.0f;
	for (it = 0; it < 64; it++)
	{
		gfloat xf[16];
		gfloat mx[16];
		gdouble shift[3];
		Proxy A;
		Proxy B;
		gdouble dist;
		gdouble pa[3];
		gdouble pb[3];
		gdouble n[3];
		gdouble bound;

		sweep_at (sw, t, xf);
		lrg_math3d_multiply (xf, cxf, mx);
		for (k = 0; k < 3; k++)
			shift[k] = (gdouble)mx[12 + k];
		proxy_init_shape (&A, m, mx, shift);
		if (target_tri != NULL)
		{
			gdouble tv[9];

			for (k = 0; k < 9; k++)
				tv[k] = target_tri[k] - shift[k % 3];
			proxy_init_tri (&B, tv, 0);
		}
		else
			proxy_init_shape (&B, target, xf_target, shift);
		proxy_closest (&A, &B, &dist, pa, pb, n);

		if (dist <= CAST_TOLERANCE)
		{
			if (t > t_max)
				return FALSE;
			*toi = t;
			if (hit != NULL)
			{
				lrg_ray_hit_reset (hit);
				hit->fraction = t;
				for (k = 0; k < 3; k++)
				{
					hit->point[k] = (gfloat)((pa[k] + pb[k]) * 0.5 + shift[k]);
					hit->normal[k] = (gfloat)(-n[k]);
				}
			}
			return TRUE;
		}
		bound = d3_dot (disp, n) + (gdouble)sw->angle * rmax;
		if (bound <= 1e-9)
			return FALSE;
		t += (gfloat)((dist - CAST_TOLERANCE * 0.5) / bound);
		if (t > t_max || t > 1.0f)
			return FALSE;
	}
	return FALSE;
}

typedef struct
{
	LrgPhysicsShape *m;
	const gfloat    *cxf;
	const Sweep     *sw;
	const gfloat    *xf_target;
	gfloat           best;
	gboolean         any;
	LrgRayHit        hit;
} CastTriCtx;

static gboolean
cast_tri_cb (guint         index,
             const gfloat *v,
             guint         material,
             guint         user_id,
             guint         active_edges,
             gpointer      user_data)
{
	CastTriCtx *ctx;
	gdouble tri[9];
	guint k;
	gfloat toi;
	LrgRayHit h;

	(void)active_edges;
	ctx = (CastTriCtx *)user_data;
	for (k = 0; k < 3; k++)
	{
		gfloat p[3];

		lrg_math3d_transform_point (ctx->xf_target, &v[k * 3], p);
		tri[k * 3 + 0] = p[0];
		tri[k * 3 + 1] = p[1];
		tri[k * 3 + 2] = p[2];
	}
	if (cast_convex (ctx->m, ctx->cxf, ctx->sw, NULL, NULL, tri, ctx->best, &toi, &h)
	    && (!ctx->any || toi < ctx->best))
	{
		ctx->best = toi;
		ctx->any = TRUE;
		ctx->hit = h;
		ctx->hit.sub_shape = index;
		ctx->hit.material = material;
		ctx->hit.user_id = user_id;
	}
	return TRUE;
}

/* Recursive sweep of moving shape @m (placed by @cxf in the swept frame)
 * against @target. */
static gboolean
cast_any (LrgPhysicsShape *m,
          const gfloat    *cxf,
          const Sweep     *sw,
          const gfloat    *xf_from,
          const gfloat    *xf_to,
          LrgPhysicsShape *target,
          const gfloat    *xf_target,
          gint             target_child,
          gfloat           t_max,
          gfloat          *toi,
          LrgRayHit       *hit)
{
	guint i;
	gboolean any;
	gfloat best;

	any = FALSE;
	best = t_max;

	/* Moving compound: each child separately. */
	if (m->kind == LRG_PHYSICS_SHAPE_KIND_COMPOUND)
	{
		for (i = 0; i < m->children->len; i++)
		{
			const LrgShapeChild *ch;
			gfloat cx[16];
			gfloat t;
			LrgRayHit h;

			ch = lrg_physics_shape_get_child_internal (m, i);
			lrg_math3d_multiply (cxf, ch->xf, cx);
			if (cast_any (ch->shape, cx, sw, xf_from, xf_to, target, xf_target, target_child,
			              best, &t, &h)
			    && (!any || t < best))
			{
				best = t;
				*toi = t;
				*hit = h;
				any = TRUE;
			}
		}
		return any;
	}
	if (!lrg_physics_shape_is_convex (m))
		return FALSE;

	switch (target->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_COMPOUND:
		for (i = 0; i < target->children->len; i++)
		{
			const LrgShapeChild *ch;
			gfloat tx[16];
			gfloat t;
			LrgRayHit h;

			ch = lrg_physics_shape_get_child_internal (target, i);
			lrg_math3d_multiply (xf_target, ch->xf, tx);
			if (cast_any (m, cxf, sw, xf_from, xf_to, ch->shape, tx,
			              target_child < 0 ? (gint)i : target_child, best, &t, &h)
			    && (!any || t < best))
			{
				best = t;
				*toi = t;
				*hit = h;
				any = TRUE;
			}
		}
		return any;

	case LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH:
	case LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD:
		{
			CastTriCtx ctx;
			gfloat mn0[3];
			gfloat mx0[3];
			gfloat mn1[3];
			gfloat mx1[3];
			gfloat f[16];
			gfloat inv[16];
			gfloat lmin[3];
			gfloat lmax[3];
			gfloat c[3];
			gfloat e[3];
			gfloat lc[3];
			guint k;

			/* Swept bounds (from + to, padded by the rotation sweep). */
			lrg_math3d_multiply (xf_from, cxf, f);
			lrg_physics_shape_get_world_bounds (m, f, mn0, mx0);
			lrg_math3d_multiply (xf_to, cxf, f);
			lrg_physics_shape_get_world_bounds (m, f, mn1, mx1);
			for (k = 0; k < 3; k++)
			{
				gfloat pad;

				pad = sw->angle > 0.0f ? m->max_extent : 0.0f;
				mn0[k] = MIN (mn0[k], mn1[k]) - pad - (gfloat)CAST_TOLERANCE;
				mx0[k] = MAX (mx0[k], mx1[k]) + pad + (gfloat)CAST_TOLERANCE;
				c[k] = (mn0[k] + mx0[k]) * 0.5f;
				e[k] = (mx0[k] - mn0[k]) * 0.5f;
			}
			lrg_math3d_invert (xf_target, inv);
			lrg_math3d_transform_point (inv, c, lc);
			for (k = 0; k < 3; k++)
			{
				gfloat le;

				le = fabsf (inv[0 + k]) * e[0] + fabsf (inv[4 + k]) * e[1] + fabsf (inv[8 + k]) * e[2];
				lmin[k] = lc[k] - le;
				lmax[k] = lc[k] + le;
			}
			ctx.m = m;
			ctx.cxf = cxf;
			ctx.sw = sw;
			ctx.xf_target = xf_target;
			ctx.best = t_max;
			ctx.any = FALSE;
			lrg_physics_shape_foreach_triangle (target, lmin, lmax, cast_tri_cb, &ctx);
			if (!ctx.any)
				return FALSE;
			*toi = ctx.best;
			*hit = ctx.hit;
			hit->child = target_child;
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_PLANE:
		{
			/* A plane is convex for this purpose: march with the deepest
			 * support point of the moving shape. */
			gfloat t;
			guint it;
			gfloat wn[3];
			gfloat lp[3];
			gfloat wp[3];
			gfloat pd;
			gdouble disp[3];
			gdouble rmax;
			guint k;

			lrg_math3d_transform_direction (xf_target, target->plane_n, wn);
			for (k = 0; k < 3; k++)
				lp[k] = target->plane_n[k] * target->plane_d;
			lrg_math3d_transform_point (xf_target, lp, wp);
			pd = wn[0] * wp[0] + wn[1] * wp[1] + wn[2] * wp[2];
			for (k = 0; k < 3; k++)
				disp[k] = (gdouble)(sw->p1[k] - sw->p0[k]);
			rmax = (gdouble)m->max_extent + sqrt ((gdouble)(cxf[12] * cxf[12] + cxf[13] * cxf[13]
			                                                 + cxf[14] * cxf[14]));
			t = 0.0f;
			for (it = 0; it < 64; it++)
			{
				gfloat xf[16];
				gfloat mx[16];
				gfloat ld[3];
				gfloat neg[3];
				gfloat s[3];
				gfloat ws[3];
				gfloat inv[16];
				gdouble dist;
				gdouble bound;

				sweep_at (sw, t, xf);
				lrg_math3d_multiply (xf, cxf, mx);
				lrg_math3d_invert (mx, inv);
				neg[0] = -wn[0];
				neg[1] = -wn[1];
				neg[2] = -wn[2];
				lrg_math3d_transform_direction (inv, neg, ld);
				lrg_physics_shape_support (m, ld, s);
				lrg_math3d_transform_point (mx, s, ws);
				dist = (gdouble)(wn[0] * ws[0] + wn[1] * ws[1] + wn[2] * ws[2] - pd);
				if (dist <= CAST_TOLERANCE)
				{
					if (t > t_max)
						return FALSE;
					*toi = t;
					lrg_ray_hit_reset (hit);
					hit->fraction = t;
					for (k = 0; k < 3; k++)
					{
						hit->point[k] = ws[k];
						hit->normal[k] = wn[k];
					}
					hit->child = target_child;
					return TRUE;
				}
				bound = -(disp[0] * wn[0] + disp[1] * wn[1] + disp[2] * wn[2])
				        + (gdouble)sw->angle * rmax;
				if (bound <= 1e-9)
					return FALSE;
				t += (gfloat)((dist - CAST_TOLERANCE * 0.5) / bound);
				if (t > t_max || t > 1.0f)
					return FALSE;
			}
			return FALSE;
		}

	default:
		if (cast_convex (m, cxf, sw, target, xf_target, NULL, t_max, toi, hit))
		{
			hit->child = target_child;
			return TRUE;
		}
		return FALSE;
	}
}

gboolean
lrg_collide_cast (LrgPhysicsShape *shape,
                  const gfloat    *xf_from,
                  const gfloat    *xf_to,
                  LrgPhysicsShape *target,
                  const gfloat    *xf_target,
                  gfloat          *toi,
                  LrgRayHit       *hit)
{
	Sweep sw;
	gfloat id[16];
	gfloat t;
	LrgRayHit h;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (shape), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (target), FALSE);
	g_return_val_if_fail (xf_from != NULL && xf_to != NULL && xf_target != NULL, FALSE);

	sweep_init (&sw, xf_from, xf_to);
	lrg_math3d_identity (id);
	lrg_ray_hit_reset (&h);
	if (!cast_any (shape, id, &sw, xf_from, xf_to, target, xf_target, -1, 1.0f, &t, &h))
		return FALSE;
	if (toi != NULL)
		*toi = t;
	if (hit != NULL)
		*hit = h;
	return TRUE;
}
