/* test-lightmap-bake.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the lightmap baker (LrgLightmapBaker), its ray tracing
 * hierarchy (LrgBakeBvh) and the lightmap chart unwrapper (LrgUvUnwrap,
 * LrgUvMesh).  Everything is headless: geometry is generated here.
 *
 * Analytic checks:
 *   - BVH closest hit / any hit against brute force
 *   - unwrap: charts inside [0,1] with the border padding, no overlaps
 *     between or inside charts, padding between charts, determinism
 *   - furnace: a closed emissive box of albedo rho after B bounces gives
 *     Le (1 + rho + ... + rho^B) everywhere (emission is direct light)
 *   - one emissive ceiling over black walls: the floor centre receives
 *     Le x the view factor of the ceiling square
 *   - a point light's baked direct term follows the lit shader's
 *     pow (c (1 - d / r)^2, 2.2) cos falloff
 *   - an occluder shadows; sky-only lighting; SH probes of a uniform sky
 *   - cancellation, monotonic progress, determinism across thread counts
 *
 * `-m perf` runs the Cornell-box benchmark; LRG_BAKE_SHOT_DIR=<dir>
 * writes the baked atlas and a CPU ray-cast render of the Cornell box as
 * PNG files for a visual check.
 */

#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* deterministic uniform in [0, 1) */
static gfloat
rnd (guint32 *s)
{
	*s = *s * 1664525u + 1013904223u;
	return (gfloat)(*s >> 8) / 16777216.0f;
}

/* a quad of side @size centred on @c facing @n (counter-clockwise) */
static LrgUvMesh *
make_quad (const gfloat *c,
           const gfloat *n,
           gfloat        size_u,
           gfloat        size_v)
{
	gfloat   a[3];
	gfloat   t[3];
	gfloat   b[3];
	gfloat   pos[12];
	guint32  idx[6] = { 0, 1, 2, 0, 2, 3 };
	gfloat   uv2[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	gfloat   su[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
	gfloat   sv[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
	gfloat   len;
	LrgUvMesh *mesh;
	gint     k;
	gint     j;

	if (fabsf (n[1]) < 0.9f)
	{
		a[0] = 0.0f; a[1] = 1.0f; a[2] = 0.0f;
	}
	else
	{
		a[0] = 1.0f; a[1] = 0.0f; a[2] = 0.0f;
	}
	/* t = a x n, b = n x t: t x b = n */
	t[0] = a[1] * n[2] - a[2] * n[1];
	t[1] = a[2] * n[0] - a[0] * n[2];
	t[2] = a[0] * n[1] - a[1] * n[0];
	len = sqrtf (t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
	for (k = 0; k < 3; k++)
		t[k] /= len;
	b[0] = n[1] * t[2] - n[2] * t[1];
	b[1] = n[2] * t[0] - n[0] * t[2];
	b[2] = n[0] * t[1] - n[1] * t[0];
	for (j = 0; j < 4; j++)
		for (k = 0; k < 3; k++)
			pos[j * 3 + k] = c[k] + t[k] * su[j] * size_u * 0.5f + b[k] * sv[j] * size_v * 0.5f;
	mesh = lrg_uv_mesh_new (pos, 12, idx, 6);
	lrg_uv_mesh_compute_normals (mesh);
	lrg_uv_mesh_set_uv2 (mesh, uv2, 8);
	return mesh;
}

/* the six inward faces of a box of edge @s centred on the origin */
static void
box_face (guint   f,
          gfloat  s,
          gfloat *c,
          gfloat *n)
{
	gint axis = (gint)(f / 2u);
	gfloat sign = (f % 2u == 0u) ? 1.0f : -1.0f;

	c[0] = c[1] = c[2] = 0.0f;
	n[0] = n[1] = n[2] = 0.0f;
	c[axis] = sign * s * 0.5f;
	n[axis] = -sign;   /* facing inwards */
}

/* Brute-force closest hit for the BVH check. */
static gboolean
brute_hit (const gfloat *pos,
           guint         n_tris,
           const gfloat *o,
           const gfloat *d,
           gfloat        t_max,
           gfloat       *out_t,
           guint        *out_tri)
{
	gboolean hit = FALSE;
	guint    i;

	*out_t = t_max;
	for (i = 0; i < n_tris; i++)
	{
		const gfloat *v0 = pos + i * 9;
		gfloat e1[3], e2[3], p[3], s[3], q[3];
		gfloat det, inv, u, v, t;
		gint   k;

		for (k = 0; k < 3; k++)
		{
			e1[k] = v0[3 + k] - v0[k];
			e2[k] = v0[6 + k] - v0[k];
		}
		p[0] = d[1] * e2[2] - d[2] * e2[1];
		p[1] = d[2] * e2[0] - d[0] * e2[2];
		p[2] = d[0] * e2[1] - d[1] * e2[0];
		det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
		if (fabsf (det) < 1e-20f)
			continue;
		inv = 1.0f / det;
		for (k = 0; k < 3; k++)
			s[k] = o[k] - v0[k];
		u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
		if (u < 0.0f || u > 1.0f)
			continue;
		q[0] = s[1] * e1[2] - s[2] * e1[1];
		q[1] = s[2] * e1[0] - s[0] * e1[2];
		q[2] = s[0] * e1[1] - s[1] * e1[0];
		v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
		if (v < 0.0f || u + v > 1.0f)
			continue;
		t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
		if (t > 0.0f && t < *out_t)
		{
			*out_t = t;
			*out_tri = i;
			hit = TRUE;
		}
	}
	return hit;
}

/* ==========================================================================
 * BVH
 * ========================================================================== */

static void
test_bvh_vs_brute (void)
{
	g_autoptr(LrgBakeBvh) bvh = lrg_bake_bvh_new ();
	const guint           n_tris = 600;
	g_autofree gfloat    *pos = g_new (gfloat, n_tris * 9);
	guint32               seed = 12345;
	gfloat                bmin[3];
	gfloat                bmax[3];
	guint                 i;
	guint                 hits = 0;

	/* small random triangles in a 10^3 box */
	for (i = 0; i < n_tris; i++)
	{
		gfloat c[3];
		gint   j;
		gint   k;

		for (k = 0; k < 3; k++)
			c[k] = rnd (&seed) * 10.0f;
		for (j = 0; j < 3; j++)
			for (k = 0; k < 3; k++)
				pos[i * 9 + j * 3 + k] = c[k] + (rnd (&seed) - 0.5f) * 1.5f;
	}
	g_assert_true (lrg_bake_bvh_build (bvh, pos, n_tris * 9, NULL, 0));
	g_assert_cmpuint (lrg_bake_bvh_get_n_triangles (bvh), ==, n_tris);
	g_assert_cmpuint (lrg_bake_bvh_get_n_nodes (bvh), <, 2 * n_tris);
	g_assert_true (lrg_bake_bvh_get_bounds (bvh, bmin, bmax));
	g_assert_cmpfloat (bmin[0], <, 0.5f);
	g_assert_cmpfloat (bmax[0], >, 9.5f);

	for (i = 0; i < 4000; i++)
	{
		gfloat     o[3];
		gfloat     d[3];
		gfloat     t_brute;
		guint      tri_brute = 0;
		gboolean   hb;
		gboolean   hv;
		gboolean   occ;
		LrgBakeHit hit;
		gint       k;

		for (k = 0; k < 3; k++)
		{
			o[k] = rnd (&seed) * 14.0f - 2.0f;
			d[k] = rnd (&seed) * 2.0f - 1.0f;
		}
		/* some axis-aligned rays exercise the zero-direction paths */
		if (i % 10 == 0)
		{
			d[0] = 0.0f;
			d[1] = 0.0f;
		}
		if (fabsf (d[0]) + fabsf (d[1]) + fabsf (d[2]) < 1e-3f)
			d[2] = 1.0f;
		hb = brute_hit (pos, n_tris, o, d, G_MAXFLOAT, &t_brute, &tri_brute);
		hv = lrg_bake_bvh_intersect (bvh, o, d, 0.0f, G_MAXFLOAT, &hit);
		g_assert_cmpint (hb, ==, hv);
		if (hb)
		{
			hits++;
			g_assert_cmpfloat_with_epsilon (hit.t, t_brute, 1e-4f * MAX (1.0f, t_brute));
			if (hit.triangle != tri_brute)
				g_assert_cmpfloat_with_epsilon (hit.t, t_brute, 1e-5f);
		}
		/* any-hit agrees with the closest hit within a bound */
		occ = lrg_bake_bvh_occluded (bvh, o, d, 0.0f, 3.0f);
		g_assert_cmpint (occ, ==, hb && t_brute < 3.0f);
	}
	g_assert_cmpuint (hits, >, 200);
}

static void
test_bvh_backface_and_index (void)
{
	g_autoptr(LrgBakeBvh) bvh = lrg_bake_bvh_new ();
	/* two triangles of a square in the z = 0 plane facing +Z */
	gfloat     pos[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
	guint32    idx[6] = { 0, 1, 2, 0, 2, 3 };
	guint32    bad[3] = { 0, 1, 7 };
	gfloat     o[3] = { 0.75f, 0.25f, 1.0f };
	gfloat     d[3] = { 0.0f, 0.0f, -1.0f };
	LrgBakeHit hit;

	g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*out of range*");
	g_assert_false (lrg_bake_bvh_build (bvh, pos, 12, bad, 3));
	g_test_assert_expected_messages ();

	g_assert_true (lrg_bake_bvh_build (bvh, pos, 12, idx, 6));
	g_assert_true (lrg_bake_bvh_intersect (bvh, o, d, 0.0f, G_MAXFLOAT, &hit));
	g_assert_cmpuint (hit.triangle, ==, 0);
	g_assert_false (hit.backface);
	g_assert_cmpfloat_with_epsilon (hit.t, 1.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (1.0f - hit.u - hit.v, 0.25f, 1e-5f);

	/* from below: a back face */
	o[2] = -1.0f;
	d[2] = 1.0f;
	o[0] = 0.25f;
	o[1] = 0.75f;
	g_assert_true (lrg_bake_bvh_intersect (bvh, o, d, 0.0f, G_MAXFLOAT, &hit));
	g_assert_cmpuint (hit.triangle, ==, 1);
	g_assert_true (hit.backface);
	/* the segment stops short of the plane */
	g_assert_false (lrg_bake_bvh_occluded (bvh, o, d, 0.0f, 0.9f));
}

/* ==========================================================================
 * Unwrap
 * ========================================================================== */

/* squared distance from p to triangle abc in 2D */
static gfloat
dist2_tri (const gfloat *p,
           const gfloat *a,
           const gfloat *b,
           const gfloat *c)
{
	const gfloat *v[3];
	gfloat d1;
	gfloat d2;
	gfloat d3;
	gfloat best = G_MAXFLOAT;
	gint   e;

	v[0] = a;
	v[1] = b;
	v[2] = c;
	d1 = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
	d2 = (c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0]);
	d3 = (a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0]);
	if ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))
		return 0.0f;
	for (e = 0; e < 3; e++)
	{
		const gfloat *s = v[e];
		const gfloat *t = v[(e + 1) % 3];
		gfloat dx = t[0] - s[0];
		gfloat dy = t[1] - s[1];
		gfloat l2 = dx * dx + dy * dy;
		gfloat u = l2 > 0 ? ((p[0] - s[0]) * dx + (p[1] - s[1]) * dy) / l2 : 0.0f;
		gfloat x;
		gfloat y;

		u = CLAMP (u, 0.0f, 1.0f);
		x = s[0] + dx * u - p[0];
		y = s[1] + dy * u - p[1];
		best = MIN (best, x * x + y * y);
	}
	return best;
}

/* strict interior test with a margin (shared edges are not overlaps) */
static gboolean
inside_strict (const gfloat *p,
               const gfloat *a,
               const gfloat *b,
               const gfloat *c)
{
	gfloat area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	gfloat s = area > 0 ? 1.0f : -1.0f;
	gfloat d1 = s * ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]));
	gfloat d2 = s * ((c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0]));
	gfloat d3 = s * ((a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0]));
	gfloat m = 1e-4f * fabsf (area);

	return fabsf (area) > 1e-12f && d1 > m && d2 > m && d3 > m;
}

static LrgUvMesh *
make_bumpy_grid (guint n)
{
	g_autofree gfloat  *pos = g_new (gfloat, (n + 1) * (n + 1) * 3);
	g_autofree guint32 *idx = g_new (guint32, n * n * 6);
	guint               x;
	guint               z;
	guint               t = 0;
	LrgUvMesh          *mesh;

	for (z = 0; z <= n; z++)
	{
		for (x = 0; x <= n; x++)
		{
			gfloat *p = pos + (z * (n + 1) + x) * 3;

			p[0] = (gfloat)x / (gfloat)n * 4.0f;
			p[2] = (gfloat)z / (gfloat)n * 4.0f;
			p[1] = 0.6f * sinf (p[0] * 2.0f) * cosf (p[2] * 1.5f);
		}
	}
	for (z = 0; z < n; z++)
	{
		for (x = 0; x < n; x++)
		{
			guint32 a = z * (n + 1) + x;

			idx[t++] = a;
			idx[t++] = a + n + 1;
			idx[t++] = a + n + 2;
			idx[t++] = a;
			idx[t++] = a + n + 2;
			idx[t++] = a + 1;
		}
	}
	mesh = lrg_uv_mesh_new (pos, (n + 1) * (n + 1) * 3, idx, t);
	lrg_uv_mesh_compute_normals (mesh);
	return mesh;
}

static void
check_unwrap (LrgUvMesh *mesh)
{
	g_autoptr(LrgUvUnwrap) unwrap = lrg_uv_unwrap_new ();
	g_autoptr(LrgUvMesh)   out = NULL;
	g_autoptr(LrgUvMesh)   again = NULL;
	g_autoptr(GError)      error = NULL;
	const guint            res = lrg_uv_unwrap_get_resolution (unwrap);
	const guint            pad = lrg_uv_unwrap_get_padding (unwrap);
	const gfloat          *uv2;
	const guint32         *idx;
	const guint32         *xref;
	const guint32         *charts;
	const gfloat          *pin;
	const gfloat          *pout;
	gsize                  n_idx;
	gsize                  n_uv;
	guint                  n_xref;
	guint                  n_charts_tri;
	guint                  nt;
	guint                  i;
	g_autofree gint32     *owner = NULL;
	const guint            fine = 512;
	g_autofree guint8     *cover = NULL;

	out = lrg_uv_unwrap_run (unwrap, mesh, &error);
	g_assert_no_error (error);
	g_assert_nonnull (out);
	nt = lrg_uv_mesh_get_n_triangles (out);
	g_assert_cmpuint (nt, ==, lrg_uv_mesh_get_n_triangles (mesh));
	g_assert_cmpuint (lrg_uv_unwrap_get_n_charts (unwrap), >, 0);
	uv2 = lrg_uv_mesh_get_uv2 (out, &n_uv);
	idx = lrg_uv_mesh_get_indices (out, &n_idx);
	xref = lrg_uv_unwrap_get_xrefs (unwrap, &n_xref);
	charts = lrg_uv_unwrap_get_chart_ids (unwrap, &n_charts_tri);
	g_assert_cmpuint (n_xref, ==, lrg_uv_mesh_get_n_vertices (out));
	g_assert_cmpuint (n_charts_tri, ==, nt);

	/* positions come back through the cross-references */
	pin = lrg_uv_mesh_get_positions (mesh, NULL);
	pout = lrg_uv_mesh_get_positions (out, NULL);
	for (i = 0; i < n_xref; i++)
		g_assert_cmpmem (pout + i * 3, 3 * sizeof (gfloat), pin + xref[i] * 3, 3 * sizeof (gfloat));

	/* charts inside [0, 1] with the border padding */
	for (i = 0; i < n_uv; i++)
	{
		g_assert_cmpfloat (uv2[i], >=, (gfloat)pad / (gfloat)res - 1e-4f);
		g_assert_cmpfloat (uv2[i], <=, 1.0f - (gfloat)pad / (gfloat)res + 1e-4f);
	}

	/* texels a chart touches (within 0.71 texel) never meet another chart:
	 * charts are 2 x padding texels apart */
	owner = g_new (gint32, res * res);
	for (i = 0; i < res * res; i++)
		owner[i] = -1;
	for (i = 0; i < nt; i++)
	{
		gfloat a[2], b[2], c[2];
		gint   x0, y0, x1, y1, x, y;

		a[0] = uv2[idx[i * 3] * 2] * res;     a[1] = uv2[idx[i * 3] * 2 + 1] * res;
		b[0] = uv2[idx[i * 3 + 1] * 2] * res; b[1] = uv2[idx[i * 3 + 1] * 2 + 1] * res;
		c[0] = uv2[idx[i * 3 + 2] * 2] * res; c[1] = uv2[idx[i * 3 + 2] * 2 + 1] * res;
		x0 = MAX (0, (gint)floorf (MIN (a[0], MIN (b[0], c[0]))) - 1);
		y0 = MAX (0, (gint)floorf (MIN (a[1], MIN (b[1], c[1]))) - 1);
		x1 = MIN ((gint)res - 1, (gint)ceilf (MAX (a[0], MAX (b[0], c[0]))) + 1);
		y1 = MIN ((gint)res - 1, (gint)ceilf (MAX (a[1], MAX (b[1], c[1]))) + 1);
		for (y = y0; y <= y1; y++)
		{
			for (x = x0; x <= x1; x++)
			{
				gfloat p[2] = { x + 0.5f, y + 0.5f };

				if (dist2_tri (p, a, b, c) > 0.5f)
					continue;
				if (owner[y * res + x] >= 0)
					g_assert_cmpint (owner[y * res + x], ==, (gint32)charts[i]);
				owner[y * res + x] = (gint32)charts[i];
			}
		}
	}

	/* no two triangles overlap (fine raster of strict interiors) */
	cover = g_new0 (guint8, fine * fine);
	for (i = 0; i < nt; i++)
	{
		const gfloat *a = uv2 + idx[i * 3] * 2;
		const gfloat *b = uv2 + idx[i * 3 + 1] * 2;
		const gfloat *c = uv2 + idx[i * 3 + 2] * 2;
		gint x0 = (gint)floorf (MIN (a[0], MIN (b[0], c[0])) * fine);
		gint y0 = (gint)floorf (MIN (a[1], MIN (b[1], c[1])) * fine);
		gint x1 = (gint)ceilf (MAX (a[0], MAX (b[0], c[0])) * fine);
		gint y1 = (gint)ceilf (MAX (a[1], MAX (b[1], c[1])) * fine);
		gint x, y;

		for (y = MAX (y0, 0); y < MIN (y1, (gint)fine); y++)
		{
			for (x = MAX (x0, 0); x < MIN (x1, (gint)fine); x++)
			{
				gfloat p[2] = { (x + 0.5f) / fine, (y + 0.5f) / fine };

				if (!inside_strict (p, a, b, c))
					continue;
				g_assert_cmpuint (cover[y * fine + x], ==, 0);
				cover[y * fine + x] = 1;
			}
		}
	}
	g_assert_cmpfloat (lrg_uv_unwrap_get_utilization (unwrap), >, 0.05);
	g_assert_cmpfloat (lrg_uv_unwrap_get_utilization (unwrap), <=, 1.0);

	/* deterministic: the same uv2 bit for bit */
	again = lrg_uv_unwrap_run (unwrap, mesh, &error);
	g_assert_no_error (error);
	g_assert_cmpmem (lrg_uv_mesh_get_uv2 (again, NULL), n_uv * sizeof (gfloat), uv2, n_uv * sizeof (gfloat));
}

static void
test_unwrap_primitives (void)
{
	LrgPrimitiveType prims[] = { LRG_PRIMITIVE_CUBE, LRG_PRIMITIVE_UV_SPHERE, LRG_PRIMITIVE_PLANE,
	                             LRG_PRIMITIVE_CYLINDER, LRG_PRIMITIVE_CONE, LRG_PRIMITIVE_TORUS,
	                             LRG_PRIMITIVE_CIRCLE, LRG_PRIMITIVE_RECTANGLE_2D };
	gfloat slab[4] = { 6.0f, 0.2f, 4.0f, 0.0f };
	guint  i;

	for (i = 0; i < G_N_ELEMENTS (prims); i++)
	{
		g_autoptr(LrgUvMesh) mesh = lrg_uv_mesh_new_primitive (prims[i], NULL);

		g_assert_nonnull (mesh);
		g_assert_nonnull (lrg_uv_mesh_get_normals (mesh, NULL));
		g_assert_cmpfloat (lrg_uv_mesh_get_surface_area (mesh, NULL), >, 0.1);
		check_unwrap (mesh);
	}
	/* a sized primitive (a floor slab) and the lightmap shortcut */
	{
		g_autoptr(LrgUvMesh) mesh = lrg_uv_mesh_new_primitive (LRG_PRIMITIVE_CUBE, slab);
		g_autoptr(LrgUvMesh) lm = lrg_uv_unwrap_new_lightmap_primitive (LRG_PRIMITIVE_CUBE, slab);

		g_assert_cmpfloat_with_epsilon (lrg_uv_mesh_get_surface_area (mesh, NULL),
		                                2.0 * (6.0 * 4.0 + 6.0 * 0.2 + 4.0 * 0.2), 1e-3);
		check_unwrap (mesh);
		g_assert_nonnull (lrg_uv_mesh_get_uv2 (lm, NULL));
		g_assert_null (lrg_uv_mesh_new_primitive (LRG_PRIMITIVE_GRID, NULL));
	}
}

static void
test_unwrap_curved (void)
{
	g_autoptr(LrgUvMesh) grid = make_bumpy_grid (24);

	check_unwrap (grid);
}

static void
test_unwrap_errors_and_raylib (void)
{
	g_autoptr(LrgUvUnwrap) unwrap = lrg_uv_unwrap_new ();
	g_autoptr(GError)      error = NULL;
	gfloat                 pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	guint32                bad[3] = { 0, 1, 9 };
	g_autoptr(LrgUvMesh)   one = lrg_uv_mesh_new (pos, 9, NULL, 0);
	g_autoptr(LrgUvMesh)   none = lrg_uv_mesh_new (pos, 6, NULL, 0);
	g_autoptr(LrgUvMesh)   out = NULL;
	g_autoptr(LrgUvMesh)   back = NULL;
	g_autoptr(LrgUvMesh)   cube = lrg_uv_unwrap_new_lightmap_primitive (LRG_PRIMITIVE_CUBE, NULL);
	guint                  sizes[6] = { 10, 10, 30, 5, 70, 70 };
	guint                  place[9];
	guint                  pages = 0;
	Mesh                   rl;

	g_assert_null (lrg_uv_mesh_new (pos, 9, bad, 3));
	g_assert_null (lrg_uv_unwrap_run (unwrap, none, &error));
	g_assert_error (error, LRG_UV_UNWRAP_ERROR, LRG_UV_UNWRAP_ERROR_EMPTY);
	g_clear_error (&error);
	out = lrg_uv_unwrap_run (unwrap, one, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_uv_unwrap_get_n_charts (unwrap), ==, 1);

	/* raylib round trip keeps uv2 */
	lrg_uv_mesh_to_raylib (cube, &rl);
	g_assert_nonnull (rl.texcoords2);
	g_assert_cmpint (rl.vertexCount, ==, (gint)lrg_uv_mesh_get_n_vertices (cube));
	back = lrg_uv_mesh_new_from_raylib (&rl);
	g_assert_cmpmem (lrg_uv_mesh_get_uv2 (back, NULL), rl.vertexCount * 2 * sizeof (gfloat),
	                 lrg_uv_mesh_get_uv2 (cube, NULL), rl.vertexCount * 2 * sizeof (gfloat));
	MemFree (rl.vertices);
	MemFree (rl.normals);
	MemFree (rl.texcoords);
	MemFree (rl.texcoords2);
	MemFree (rl.indices);

	/* the rect packer: a 70 x 70 rectangle needs a second 64 page... no, refuse */
	g_assert_false (lrg_uv_unwrap_pack_rects (sizes, 6, 64, 0, place, &pages));
	sizes[4] = 40;
	sizes[5] = 40;
	g_assert_true (lrg_uv_unwrap_pack_rects (sizes, 6, 64, 0, place, &pages));
	g_assert_cmpuint (pages, >=, 1);
}

/* ==========================================================================
 * Scenes
 * ========================================================================== */

static LrgLightmapBaker *
new_baker (guint samples,
           guint bounces)
{
	LrgLightmapBaker *baker = lrg_lightmap_baker_new ();

	g_object_set (lrg_lightmap_baker_get_settings (baker), "samples", samples, "bounces", bounces,
	              "denoise", FALSE, "auto-probe-volume", FALSE, NULL);
	return baker;
}

/* adds the six inward faces of a unit box; returns nothing */
static void
add_box (LrgLightmapBaker *baker,
         gfloat            size,
         const gfloat     *albedo,
         const gfloat     *emission,
         gint              emissive_face)
{
	guint f;

	for (f = 0; f < 6; f++)
	{
		gfloat                c[3];
		gfloat                n[3];
		g_autoptr(LrgUvMesh)  q = NULL;
		g_autofree gchar     *guid = g_strdup_printf ("box-face-%u", f);
		const gfloat         *em = (emissive_face < 0 || (gint)f == emissive_face) ? emission : NULL;

		box_face (f, size, c, n);
		q = make_quad (c, n, size, size);
		g_assert_true (lrg_lightmap_baker_add_mesh (baker, guid, q, NULL, albedo, em, 1.0f,
		                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
	}
}

static gfloat
display_of (gfloat linear)
{
	return powf (linear, 1.0f / 2.2f);
}

static void
test_furnace (void)
{
	g_autoptr(LrgLightmapBaker) baker = new_baker (4096, 3);
	gfloat albedo[3];
	gfloat emission[3];
	gfloat p[3] = { 0.0f, -0.5f, 0.0f };
	gfloat n[3] = { 0.0f, 1.0f, 0.0f };
	gfloat p2[3] = { 0.3f, 0.2f, 0.5f };
	gfloat n2[3] = { 0.0f, 0.0f, -1.0f };
	gfloat rgb[3];
	gfloat expect = 0.2f * (1.0f + 0.5f + 0.25f + 0.125f);
	gint   k;

	/* linear albedo 0.5, linear emission 0.2 on every face */
	for (k = 0; k < 3; k++)
	{
		albedo[k] = display_of (0.5f);
		emission[k] = display_of (0.2f);
	}
	add_box (baker, 1.0f, albedo, emission, -1);
	g_assert_true (lrg_lightmap_baker_evaluate (baker, p, n, rgb));
	for (k = 0; k < 3; k++)
		g_assert_cmpfloat_with_epsilon (rgb[k], expect, expect * 0.02f);
	g_assert_true (lrg_lightmap_baker_evaluate (baker, p2, n2, rgb));
	g_assert_cmpfloat_with_epsilon (rgb[1], expect, expect * 0.02f);
}

/* view factor from a point to a parallel square centred above it */
static gdouble
view_factor_square (gdouble half,
                    gdouble h)
{
	gdouble a = half / h;
	gdouble s = sqrt (1.0 + a * a);
	gdouble q = (a / s * atan (a / s)) * 2.0 / (2.0 * G_PI);

	return 4.0 * q;
}

static void
test_emissive_ceiling (void)
{
	g_autoptr(LrgLightmapBaker) baker = new_baker (16384, 1);
	gfloat black[3] = { 0.0f, 0.0f, 0.0f };
	gfloat emission[3];
	gfloat floor_p[3] = { 0.0f, -0.5f, 0.0f };
	gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	gfloat wall_n[3] = { 1.0f, 0.0f, 0.0f };
	gfloat low[3] = { -0.5f, -0.4f, 0.0f };
	gfloat high[3] = { -0.5f, 0.4f, 0.0f };
	gfloat rgb[3];
	gfloat rl[3];
	gfloat rh[3];
	gdouble f = view_factor_square (0.5, 1.0);
	gint   k;

	for (k = 0; k < 3; k++)
		emission[k] = display_of (1.0f);
	/* face 2 is +Y: the ceiling */
	add_box (baker, 1.0f, black, emission, 2);
	g_assert_true (lrg_lightmap_baker_evaluate (baker, floor_p, up, rgb));
	g_assert_cmpfloat_with_epsilon (rgb[0], f, f * 0.03);
	/* the walls: brighter near the ceiling */
	g_assert_true (lrg_lightmap_baker_evaluate (baker, low, wall_n, rl));
	g_assert_true (lrg_lightmap_baker_evaluate (baker, high, wall_n, rh));
	g_assert_cmpfloat (rh[0], >, rl[0] * 1.5f);
}

/* a 6 x 6 floor with uv2 = (x, z) mapped to [0, 1] */
static LrgUvMesh *
make_floor (gfloat size)
{
	gfloat c[3] = { 0.0f, 0.0f, 0.0f };
	gfloat n[3] = { 0.0f, 1.0f, 0.0f };
	LrgUvMesh *mesh = make_quad (c, n, size, size);
	const gfloat *pos = lrg_uv_mesh_get_positions (mesh, NULL);
	gfloat uv2[8];
	gint   i;

	for (i = 0; i < 4; i++)
	{
		uv2[i * 2] = pos[i * 3] / size + 0.5f;
		uv2[i * 2 + 1] = pos[i * 3 + 2] / size + 0.5f;
	}
	lrg_uv_mesh_set_uv2 (mesh, uv2, 8);
	return mesh;
}

static void
test_point_light_falloff (void)
{
	g_autoptr(LrgLightmapBaker) baker = new_baker (4, 0);
	g_autoptr(LrgUvMesh)        floor_mesh = make_floor (6.0f);
	g_autoptr(GError)           error = NULL;
	LrgLight3DDesc              light;
	LrgLightmap3D              *lm;
	gfloat                      xs[4] = { 0.0f, 0.8f, 1.6f, 2.4f };
	guint                       i;

	g_object_set (lrg_lightmap_baker_get_settings (baker), "texels-per-unit", 24.0f, "light-radius", 0.0f, NULL);
	g_assert_true (lrg_lightmap_baker_add_mesh (baker, "floor", floor_mesh, NULL, NULL, NULL, 1.0f,
	                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
	lrg_light3d_desc_init (&light);
	light.kind = LRG_LIGHT3D_POINT;
	light.position[1] = 1.0f;
	light.range = 5.0f;
	light.color[0] = light.color[1] = light.color[2] = 0.8f;
	light.intensity = 1.0f;
	/* realtime lights are not baked */
	g_assert_false (lrg_lightmap_baker_add_light (baker, &light, -1.0f));
	light.bake_mode = LRG_LIGHT_MODE_BAKED;
	g_assert_true (lrg_lightmap_baker_add_light (baker, &light, -1.0f));
	g_assert_cmpuint (lrg_lightmap_baker_get_n_lights (baker), ==, 1);

	g_assert_true (lrg_lightmap_baker_bake (baker, NULL, &error));
	g_assert_no_error (error);
	lm = lrg_lightmap_baker_get_lightmap (baker);
	g_assert_nonnull (lm);
	g_assert_cmpuint (lrg_lightmap3d_get_n_pages (lm), ==, 1);
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 1);

	for (i = 0; i < G_N_ELEMENTS (xs); i++)
	{
		gfloat d = sqrtf (1.0f + xs[i] * xs[i]);
		gfloat k = 1.0f - d / 5.0f;
		gfloat expect = powf (0.8f * k * k, 2.2f) * (1.0f / d);
		gfloat uv2[2] = { xs[i] / 6.0f + 0.5f, 0.5f };
		gfloat rgb[3];
		gfloat ev[3];
		gfloat p[3] = { xs[i], 0.0f, 0.0f };
		gfloat n[3] = { 0.0f, 1.0f, 0.0f };

		g_assert_true (lrg_lightmap3d_sample (lm, "floor", uv2, rgb));
		g_assert_cmpfloat_with_epsilon (rgb[0], expect, expect * 0.03f + 1e-4f);
		g_assert_cmpfloat_with_epsilon (rgb[2], expect, expect * 0.03f + 1e-4f);
		g_assert_true (lrg_lightmap_baker_evaluate (baker, p, n, ev));
		g_assert_cmpfloat_with_epsilon (ev[1], expect, expect * 1e-3f + 1e-6f);
	}

	/* a spot light pointing down: inside the cone lit, outside dark */
	{
		g_autoptr(LrgLightmapBaker) spot_baker = new_baker (4, 0);
		gfloat                      in_p[3] = { 0.2f, 0.0f, 0.0f };
		gfloat                      out_p[3] = { 2.0f, 0.0f, 0.0f };
		gfloat                      n[3] = { 0.0f, 1.0f, 0.0f };
		gfloat                      a[3];
		gfloat                      b[3];

		g_assert_true (lrg_lightmap_baker_add_mesh (spot_baker, "floor", floor_mesh, NULL, NULL, NULL, 1.0f,
		                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
		light.kind = LRG_LIGHT3D_SPOT;
		light.direction[0] = 0.0f;
		light.direction[1] = -1.0f;
		light.direction[2] = 0.0f;
		light.inner_angle = 20.0f * G_PI / 180.0f;
		light.outer_angle = 30.0f * G_PI / 180.0f;
		g_assert_true (lrg_lightmap_baker_add_light (spot_baker, &light, 0.0f));
		g_assert_true (lrg_lightmap_baker_evaluate (spot_baker, in_p, n, a));
		g_assert_true (lrg_lightmap_baker_evaluate (spot_baker, out_p, n, b));
		g_assert_cmpfloat (a[0], >, 0.1f);
		g_assert_cmpfloat (b[0], ==, 0.0f);
	}
}

static void
test_shadow (void)
{
	g_autoptr(LrgLightmapBaker) baker = new_baker (256, 0);
	g_autoptr(LrgUvMesh)        floor_mesh = make_floor (6.0f);
	g_autoptr(LrgUvMesh)        blocker = lrg_uv_mesh_new_primitive (LRG_PRIMITIVE_CUBE, NULL);
	gfloat                      m[16];
	gfloat                      loc[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                      rot[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                      scl[3] = { 0.6f, 0.1f, 0.6f };
	gfloat                      n[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                      under[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                      edge[3] = { 0.45f, 0.0f, 0.0f };
	gfloat                      open[3] = { 2.0f, 0.0f, 0.0f };
	gfloat                      a[3];
	gfloat                      b[3];
	gfloat                      c[3];
	LrgLight3DDesc              light;

	g_assert_true (lrg_lightmap_baker_add_mesh (baker, "floor", floor_mesh, NULL, NULL, NULL, 1.0f,
	                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
	lrg_math3d_compose (loc, rot, scl, m);
	g_assert_true (lrg_lightmap_baker_add_mesh (baker, NULL, blocker, m, NULL, NULL, 1.0f,
	                                            LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE));
	lrg_light3d_desc_init (&light);
	light.kind = LRG_LIGHT3D_POINT;
	light.position[1] = 3.0f;
	light.range = 10.0f;
	light.bake_mode = LRG_LIGHT_MODE_BAKED;
	g_assert_true (lrg_lightmap_baker_add_light (baker, &light, 0.3f));
	g_assert_true (lrg_lightmap_baker_evaluate (baker, under, n, a));
	g_assert_true (lrg_lightmap_baker_evaluate (baker, edge, n, b));
	g_assert_true (lrg_lightmap_baker_evaluate (baker, open, n, c));
	/* umbra black, penumbra between, open ground lit */
	g_assert_cmpfloat (a[0], <, 1e-6f);
	g_assert_cmpfloat (c[0], >, 0.05f);
	g_assert_cmpfloat (b[0], >, a[0]);
	/* the penumbra is darker than the same point without the blocker */
	{
		g_autoptr(LrgLightmapBaker) free_baker = new_baker (256, 0);
		gfloat                      u[3];

		g_assert_true (lrg_lightmap_baker_add_mesh (free_baker, "floor", floor_mesh, NULL, NULL, NULL, 1.0f,
		                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
		g_assert_true (lrg_lightmap_baker_add_light (free_baker, &light, 0.3f));
		g_assert_true (lrg_lightmap_baker_evaluate (free_baker, edge, n, u));
		g_assert_cmpfloat (b[0], <, u[0] * 0.9f);
	}

	/* a directional sun straight down: same umbra */
	{
		g_autoptr(LrgLightmapBaker) sun_baker = new_baker (64, 0);
		gfloat                      s[3];
		gfloat                      o[3];

		g_assert_true (lrg_lightmap_baker_add_mesh (sun_baker, "floor", floor_mesh, NULL, NULL, NULL, 1.0f,
		                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
		g_assert_true (lrg_lightmap_baker_add_mesh (sun_baker, NULL, blocker, m, NULL, NULL, 1.0f,
		                                            LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE));
		light.kind = LRG_LIGHT3D_DIRECTIONAL;
		light.direction[0] = 0.0f;
		light.direction[1] = -1.0f;
		light.direction[2] = 0.0f;
		g_assert_true (lrg_lightmap_baker_add_light (sun_baker, &light, -1.0f));
		g_assert_true (lrg_lightmap_baker_evaluate (sun_baker, under, n, s));
		g_assert_true (lrg_lightmap_baker_evaluate (sun_baker, open, n, o));
		g_assert_cmpfloat (s[0], <, 1e-6f);
		g_assert_cmpfloat_with_epsilon (o[0], 1.0f, 1e-4f);
	}
}

static void
test_sky_only (void)
{
	g_autoptr(LrgLightmapBaker) baker = new_baker (8192, 1);
	g_autoptr(LrgUvMesh)        floor_mesh = make_floor (4.0f);
	g_autoptr(LrgEnvironment)   env = lrg_environment_new ();
	gfloat                      p[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                      n[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                      zen[3] = { 0.2f, 0.4f, 0.9f };
	gfloat                      hor[3] = { 0.9f, 0.8f, 0.7f };
	gfloat                      gnd[3] = { 0.1f, 0.1f, 0.1f };
	gfloat                      sh[27];
	gfloat                      expect[3];
	gfloat                      rgb[3];
	gint                        k;

	g_assert_true (lrg_lightmap_baker_add_mesh (baker, "floor", floor_mesh, NULL, NULL, NULL, 1.0f,
	                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
	/* a flat sky: the upward plane sees exactly the sky radiance */
	lrg_lightmap_baker_set_sky_color (baker, 0.3f, 0.5f, 0.7f);
	g_assert_true (lrg_lightmap_baker_evaluate (baker, p, n, rgb));
	g_assert_cmpfloat_with_epsilon (rgb[0], 0.3f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (rgb[2], 0.7f, 1e-4f);

	/* a gradient sky: the cosine-weighted hemisphere integral of the sky
	 * (midpoint quadrature over theta / phi), and close to the
	 * environment's own L2 SH irradiance */
	lrg_environment_set_gradient (env, zen, hor, gnd);
	lrg_lightmap_baker_set_environment (baker, env);
	{
		const gint nt = 256;
		const gint np = 256;
		gdouble    acc[3] = { 0.0, 0.0, 0.0 };
		gint       i;
		gint       j;

		for (i = 0; i < nt; i++)
		{
			gdouble th = (i + 0.5) / nt * G_PI * 0.5;

			for (j = 0; j < np; j++)
			{
				gdouble ph = (j + 0.5) / np * 2.0 * G_PI;
				gfloat  d[3];
				gfloat  l[3];
				gdouble w = cos (th) * sin (th) * (G_PI * 0.5 / nt) * (2.0 * G_PI / np) / G_PI;

				d[0] = (gfloat)(sin (th) * cos (ph));
				d[1] = (gfloat)cos (th);
				d[2] = (gfloat)(sin (th) * sin (ph));
				lrg_environment_sample_sky (env, d, l);
				for (k = 0; k < 3; k++)
					acc[k] += l[k] * w;
			}
		}
		for (k = 0; k < 3; k++)
			expect[k] = (gfloat)acc[k];
	}
	g_assert_true (lrg_lightmap_baker_evaluate (baker, p, n, rgb));
	for (k = 0; k < 3; k++)
		g_assert_cmpfloat_with_epsilon (rgb[k], expect[k], expect[k] * 0.02f);
	lrg_environment_compute_sh (env, 64, sh);
	lrg_environment_sh_irradiance (sh, n, expect);
	for (k = 0; k < 3; k++)
		g_assert_cmpfloat_with_epsilon (rgb[k], expect[k], expect[k] * 0.1f);
}

static void
test_probe_uniform_sky (void)
{
	g_autoptr(LrgLightmapBaker) baker = new_baker (16, 1);
	g_autoptr(GError)           error = NULL;
	gfloat                      mn[3] = { -1.0f, -1.0f, -1.0f };
	gfloat                      mx[3] = { 1.0f, 1.0f, 1.0f };
	gfloat                      centre[3] = { 0.1f, 0.2f, -0.3f };
	gfloat                      dirs[4][3] = { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0.577f, -0.577f, 0.577f } };
	GPtrArray                  *grids;
	LrgProbeGrid               *grid;
	gfloat                      sh[27];
	guint                       i;

	g_object_set (lrg_lightmap_baker_get_settings (baker), "probe-samples", 1024, NULL);
	lrg_lightmap_baker_set_sky_color (baker, 0.4f, 0.6f, 0.8f);
	lrg_lightmap_baker_add_probe_volume (baker, mn, mx, 2, 2, 2);
	g_assert_true (lrg_lightmap_baker_bake (baker, NULL, &error));
	g_assert_no_error (error);
	grids = lrg_lightmap_baker_get_probe_grids (baker);
	g_assert_cmpuint (grids->len, ==, 1);
	grid = g_ptr_array_index (grids, 0);
	g_assert_cmpuint (lrg_probe_grid_get_n_probes (grid), ==, 8);
	g_assert_cmpuint (lrg_probe_grid_get_n_valid (grid), ==, 8);

	/* the DC term of a constant L is L sqrt (4 pi) x Y00 x 4 pi... = L / Y00 x Y00^2 */
	g_assert_true (lrg_probe_grid_get_probe (grid, 0, sh));
	g_assert_cmpfloat_with_epsilon (sh[0] * 0.282095f, 0.4f, 0.4f * 0.01f);
	for (i = 3; i < 27; i++)
		g_assert_cmpfloat (fabsf (sh[i]), <, 0.02f);
	for (i = 0; i < 4; i++)
	{
		gfloat rgb[3];

		g_assert_true (lrg_probe_grid_sample (grid, centre, dirs[i], rgb));
		g_assert_cmpfloat_with_epsilon (rgb[0], 0.4f, 0.4f * 0.03f);
		g_assert_cmpfloat_with_epsilon (rgb[1], 0.6f, 0.6f * 0.03f);
		g_assert_cmpfloat_with_epsilon (rgb[2], 0.8f, 0.8f * 0.03f);
	}
}

/* ==========================================================================
 * Cornell box
 * ========================================================================== */

typedef struct
{
	const gchar *guid;
	LrgUvMesh   *mesh;
	gfloat       m[16];
	gfloat       albedo[3];
	gfloat       emission[3];
} SceneItem;

/* a 4 x 3 x 4 room of slabs, two boxes, a sphere, a torus, a ceiling lamp */
static GArray *
cornell_items (void)
{
	GArray *items = g_array_new (FALSE, TRUE, sizeof (SceneItem));
	struct
	{
		const gchar     *guid;
		LrgPrimitiveType prim;
		gfloat           size[3];
		gfloat           loc[3];
		gfloat           rot_y;
		gfloat           albedo[3];
		gfloat           emission;
	} defs[] = {
		{ "floor", LRG_PRIMITIVE_CUBE, { 4.4f, 0.2f, 4.4f }, { 0.0f, -0.1f, 0.0f }, 0, { 0.8f, 0.8f, 0.8f }, 0 },
		{ "ceiling", LRG_PRIMITIVE_CUBE, { 4.4f, 0.2f, 4.4f }, { 0.0f, 3.1f, 0.0f }, 0, { 0.8f, 0.8f, 0.8f }, 0 },
		{ "back", LRG_PRIMITIVE_CUBE, { 4.4f, 3.0f, 0.2f }, { 0.0f, 1.5f, -2.1f }, 0, { 0.8f, 0.8f, 0.8f }, 0 },
		{ "left", LRG_PRIMITIVE_CUBE, { 0.2f, 3.0f, 4.0f }, { -2.1f, 1.5f, 0.0f }, 0, { 0.75f, 0.15f, 0.12f }, 0 },
		{ "right", LRG_PRIMITIVE_CUBE, { 0.2f, 3.0f, 4.0f }, { 2.1f, 1.5f, 0.0f }, 0, { 0.15f, 0.6f, 0.2f }, 0 },
		{ "tall", LRG_PRIMITIVE_CUBE, { 1.0f, 2.0f, 1.0f }, { -0.8f, 1.0f, -0.6f }, 0.35f, { 0.8f, 0.8f, 0.8f }, 0 },
		{ "short", LRG_PRIMITIVE_CUBE, { 1.0f, 1.0f, 1.0f }, { 0.9f, 0.5f, 0.6f }, -0.3f, { 0.8f, 0.8f, 0.8f }, 0 },
		{ "sphere", LRG_PRIMITIVE_UV_SPHERE, { 0.45f, 0, 0 }, { 0.9f, 1.45f, 0.6f }, 0, { 0.9f, 0.85f, 0.4f }, 0 },
		{ "torus", LRG_PRIMITIVE_TORUS, { 0.5f, 0, 0 }, { 0.6f, 0.2f, -1.3f }, 0, { 0.3f, 0.4f, 0.9f }, 0 },
		{ "lamp", LRG_PRIMITIVE_CUBE, { 1.0f, 0.05f, 1.0f }, { 0.0f, 2.97f, 0.0f }, 0, { 0.9f, 0.9f, 0.9f }, 1.0f },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (defs); i++)
	{
		SceneItem it;
		gfloat    rot[3] = { 0.0f, defs[i].rot_y, 0.0f };
		gfloat    one[3] = { 1.0f, 1.0f, 1.0f };
		gint      k;

		memset (&it, 0, sizeof it);
		it.guid = defs[i].guid;
		it.mesh = lrg_uv_mesh_new_primitive (defs[i].prim, defs[i].size);
		lrg_math3d_compose (defs[i].loc, rot, one, it.m);
		for (k = 0; k < 3; k++)
		{
			it.albedo[k] = defs[i].albedo[k];
			it.emission[k] = defs[i].emission * 4.0f;
		}
		g_array_append_val (items, it);
	}
	return items;
}

static void
cornell_items_free (GArray *items)
{
	guint i;

	for (i = 0; i < items->len; i++)
		g_object_unref (g_array_index (items, SceneItem, i).mesh);
	g_array_unref (items);
}

static void
add_cornell (LrgLightmapBaker *baker,
             GArray           *items)
{
	LrgLight3DDesc light;
	guint          i;

	for (i = 0; i < items->len; i++)
	{
		SceneItem *it = &g_array_index (items, SceneItem, i);

		g_assert_true (lrg_lightmap_baker_add_mesh (baker, it->guid, it->mesh, it->m, it->albedo,
		                                            it->emission[0] > 0.0f ? it->emission : NULL, 1.0f,
		                                            LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT));
	}
	/* a baked spot in a corner and a mixed point light */
	lrg_light3d_desc_init (&light);
	light.kind = LRG_LIGHT3D_SPOT;
	light.position[0] = 1.6f;
	light.position[1] = 2.6f;
	light.position[2] = 1.6f;
	light.direction[0] = -0.5f;
	light.direction[1] = -0.7f;
	light.direction[2] = -0.5f;
	light.color[0] = 1.0f;
	light.color[1] = 0.85f;
	light.color[2] = 0.6f;
	light.intensity = 1.0f;
	light.range = 7.0f;
	light.inner_angle = 0.35f;
	light.outer_angle = 0.6f;
	light.bake_mode = LRG_LIGHT_MODE_BAKED;
	lrg_lightmap_baker_add_light (baker, &light, 0.1f);
	light.kind = LRG_LIGHT3D_POINT;
	light.position[0] = -1.2f;
	light.position[1] = 2.2f;
	light.position[2] = 1.2f;
	light.color[0] = 0.5f;
	light.color[1] = 0.6f;
	light.color[2] = 1.0f;
	light.range = 5.0f;
	light.bake_mode = LRG_LIGHT_MODE_MIXED;
	lrg_lightmap_baker_add_light (baker, &light, 0.1f);
}

/* ACES-ish filmic for the PNG exports */
static guint8
tone (gfloat x)
{
	gfloat v = x * 1.4f;

	v = (v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f);
	v = powf (CLAMP (v, 0.0f, 1.0f), 1.0f / 2.2f);
	return (guint8)(v * 255.0f + 0.5f);
}

/* the baked atlas page and a CPU ray-cast view of the room */
static void
write_shots (LrgLightmapBaker *baker,
             GArray           *items,
             const gchar      *dir,
             const gchar      *tag)
{
	LrgLightmap3D     *lm = lrg_lightmap_baker_get_lightmap (baker);
	guint              w = 0;
	guint              h = 0;
	const gfloat      *page = lrg_lightmap_baker_get_page_data (baker, 0, &w, &h);
	g_autofree guint8 *px = NULL;
	g_autoptr(GrlImage) img = NULL;
	g_autofree gchar  *path = NULL;
	guint              i;

	g_assert_nonnull (page);
	px = g_new (guint8, w * h * 4);
	for (i = 0; i < w * h; i++)
	{
		px[i * 4] = tone (page[i * 3]);
		px[i * 4 + 1] = tone (page[i * 3 + 1]);
		px[i * 4 + 2] = tone (page[i * 3 + 2]);
		px[i * 4 + 3] = 255;
	}
	img = grl_image_new_from_pixels ((gint)w, (gint)h, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, px);
	path = g_strdup_printf ("%s/m34-baker-%s-atlas.png", dir, tag);
	g_assert_true (grl_image_export (img, path));
	g_clear_object (&img);
	g_clear_pointer (&path, g_free);

	/* the render: world triangles of every object, uv2 per corner */
	{
		const guint           W = 480;
		const guint           H = 360;
		g_autoptr(GArray)     wp = g_array_new (FALSE, FALSE, sizeof (gfloat));
		g_autoptr(GArray)     wuv = g_array_new (FALSE, FALSE, sizeof (gfloat));
		g_autoptr(GArray)     wobj = g_array_new (FALSE, FALSE, sizeof (guint));
		g_autoptr(LrgBakeBvh) bvh = lrg_bake_bvh_new ();
		g_autofree guint8    *rp = g_new (guint8, W * H * 4);
		gfloat                eye[3] = { 0.0f, 1.5f, 6.2f };
		guint                 x;
		guint                 y;

		for (i = 0; i < items->len; i++)
		{
			SceneItem     *it = &g_array_index (items, SceneItem, i);
			LrgUvMesh     *mesh = lrg_lightmap_baker_get_object_mesh (baker, it->guid);
			const gfloat  *pos = lrg_uv_mesh_get_positions (mesh, NULL);
			const gfloat  *uv2 = lrg_uv_mesh_get_uv2 (mesh, NULL);
			gsize          ni = 0;
			const guint32 *idx = lrg_uv_mesh_get_indices (mesh, &ni);
			gsize          k;

			for (k = 0; k < ni; k++)
			{
				gfloat p[3];

				lrg_math3d_transform_point (it->m, pos + idx[k] * 3, p);
				g_array_append_vals (wp, p, 3);
				g_array_append_vals (wuv, uv2 + idx[k] * 2, 2);
				if (k % 3 == 0)
					g_array_append_val (wobj, i);
			}
		}
		lrg_bake_bvh_build (bvh, (const gfloat *)wp->data, wp->len, NULL, 0);
		for (y = 0; y < H; y++)
		{
			for (x = 0; x < W; x++)
			{
				gfloat     d[3];
				LrgBakeHit hit;
				gfloat     c[3] = { 0.0f, 0.0f, 0.0f };
				gint       k;

				d[0] = ((gfloat)x + 0.5f - W * 0.5f) / (gfloat)H * 0.9f;
				d[1] = -((gfloat)y + 0.5f - H * 0.5f) / (gfloat)H * 0.9f;
				d[2] = -1.0f;
				if (lrg_bake_bvh_intersect (bvh, eye, d, 0.0f, G_MAXFLOAT, &hit) && !hit.backface)
				{
					guint          t = hit.triangle;
					SceneItem     *it = &g_array_index (items, SceneItem, g_array_index (wobj, guint, t));
					const gfloat  *u = (const gfloat *)wuv->data + t * 6;
					gfloat         w0 = 1.0f - hit.u - hit.v;
					gfloat         uv[2];
					gfloat         irr[3];

					uv[0] = u[0] * w0 + u[2] * hit.u + u[4] * hit.v;
					uv[1] = u[1] * w0 + u[3] * hit.u + u[5] * hit.v;
					lrg_lightmap3d_sample (lm, it->guid, uv, irr);
					for (k = 0; k < 3; k++)
						c[k] = irr[k] * powf (it->albedo[k], 2.2f) + powf (it->emission[k], 2.2f);
				}
				for (k = 0; k < 3; k++)
					rp[(y * W + x) * 4 + k] = tone (c[k]);
				rp[(y * W + x) * 4 + 3] = 255;
			}
		}
		img = grl_image_new_from_pixels ((gint)W, (gint)H, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, rp);
		path = g_strdup_printf ("%s/m34-baker-%s-render.png", dir, tag);
		g_assert_true (grl_image_export (img, path));
	}
}

static void
test_cornell_bake (void)
{
	g_autoptr(LrgLightmapBaker) baker = lrg_lightmap_baker_new ();
	g_autoptr(GError)           error = NULL;
	GArray                     *items = cornell_items ();
	const gchar                *dir = g_getenv ("LRG_BAKE_SHOT_DIR");
	LrgLightmap3D              *lm;
	gdouble                     v;
	gfloat                      floor_uv[2] = { 0.5f, 0.5f };
	gfloat                      rgb[3];

	g_object_set (lrg_lightmap_baker_get_settings (baker), "samples", dir != NULL ? 256 : 32,
	              "texels-per-unit", dir != NULL ? 16.0f : 6.0f, "probe-spacing", 1.5f, "probe-samples", 64,
	              NULL);
	add_cornell (baker, items);
	g_assert_true (lrg_lightmap_baker_bake (baker, NULL, &error));
	g_assert_no_error (error);
	lm = lrg_lightmap_baker_get_lightmap (baker);
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, items->len);
	g_assert_cmpuint (lrg_lightmap_baker_get_n_texels (baker), >, 1000);
	g_assert_true (lrg_lightmap3d_get_setting (lm, "samples", &v));
	g_assert_cmpfloat (v, ==, dir != NULL ? 256.0 : 32.0);
	/* the room is lit, and every object's uv2 mesh is reported */
	g_assert_true (lrg_lightmap3d_sample (lm, "floor", floor_uv, rgb));
	g_assert_nonnull (lrg_lightmap_baker_get_object_mesh (baker, "torus"));
	g_assert_null (lrg_lightmap_baker_get_object_mesh (baker, "nope"));
	/* the auto probe volume */
	g_assert_cmpuint (lrg_lightmap_baker_get_probe_grids (baker)->len, ==, 1);
	g_assert_cmpuint (lrg_probe_grid_get_n_valid (g_ptr_array_index (lrg_lightmap_baker_get_probe_grids (baker), 0)),
	                  >, 0);
	if (dir != NULL)
		write_shots (baker, items, dir, "cornell");
	cornell_items_free (items);
}

static void
test_determinism (void)
{
	g_autoptr(LrgLightmapBaker) a = lrg_lightmap_baker_new ();
	g_autoptr(LrgLightmapBaker) b = lrg_lightmap_baker_new ();
	g_autoptr(GError)           error = NULL;
	GArray                     *items = cornell_items ();
	const gfloat               *pa;
	const gfloat               *pb;
	guint                       wa = 0;
	guint                       wb = 0;

	g_object_set (lrg_lightmap_baker_get_settings (a), "samples", 8, "texels-per-unit", 3.0f, "threads", 1,
	              "auto-probe-volume", FALSE, NULL);
	g_object_set (lrg_lightmap_baker_get_settings (b), "samples", 8, "texels-per-unit", 3.0f, "threads", 5,
	              "auto-probe-volume", FALSE, NULL);
	add_cornell (a, items);
	add_cornell (b, items);
	g_assert_true (lrg_lightmap_baker_bake (a, NULL, &error));
	g_assert_true (lrg_lightmap_baker_bake (b, NULL, &error));
	g_assert_no_error (error);
	pa = lrg_lightmap_baker_get_page_data (a, 0, &wa, NULL);
	pb = lrg_lightmap_baker_get_page_data (b, 0, &wb, NULL);
	g_assert_cmpuint (wa, ==, wb);
	g_assert_cmpmem (pa, wa * wa * 3 * sizeof (gfloat), pb, wb * wb * 3 * sizeof (gfloat));

	/* another seed changes the noise */
	g_object_set (lrg_lightmap_baker_get_settings (b), "seed", 7, NULL);
	g_assert_true (lrg_lightmap_baker_bake (b, NULL, &error));
	pb = lrg_lightmap_baker_get_page_data (b, 0, &wb, NULL);
	g_assert_true (memcmp (pa, pb, wa * wa * 3 * sizeof (gfloat)) != 0);
	cornell_items_free (items);
}

/* --- async: progress and cancellation ------------------------------------ */

typedef struct
{
	GMainLoop *loop;
	GArray    *fractions;
	GArray    *phases;
	gboolean   ok;
	GError    *error;
} AsyncState;

static void
on_progress (LrgLightmapBaker     *baker,
             LrgLightmapBakePhase  phase,
             gdouble               fraction,
             gpointer              user_data)
{
	AsyncState *st = user_data;
	gint        p = (gint)phase;

	g_array_append_val (st->fractions, fraction);
	g_array_append_val (st->phases, p);
}

static void
on_baked (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
	AsyncState *st = user_data;

	st->ok = lrg_lightmap_baker_bake_finish (LRG_LIGHTMAP_BAKER (source), result, &st->error);
	g_main_loop_quit (st->loop);
}

static void
test_async_progress (void)
{
	g_autoptr(LrgLightmapBaker) baker = lrg_lightmap_baker_new ();
	GArray                     *items = cornell_items ();
	AsyncState                  st;
	guint                       i;

	memset (&st, 0, sizeof st);
	st.loop = g_main_loop_new (NULL, FALSE);
	st.fractions = g_array_new (FALSE, FALSE, sizeof (gdouble));
	st.phases = g_array_new (FALSE, FALSE, sizeof (gint));
	g_object_set (lrg_lightmap_baker_get_settings (baker), "samples", 16, "texels-per-unit", 4.0f,
	              "probe-samples", 32, NULL);
	add_cornell (baker, items);
	g_signal_connect (baker, "progress", G_CALLBACK (on_progress), &st);
	lrg_lightmap_baker_bake_async (baker, NULL, on_baked, &st);
	g_assert_true (lrg_lightmap_baker_is_running (baker));
	g_main_loop_run (st.loop);
	g_assert_no_error (st.error);
	g_assert_true (st.ok);
	g_assert_false (lrg_lightmap_baker_is_running (baker));
	g_assert_nonnull (lrg_lightmap_baker_get_lightmap (baker));

	/* let any late idle run: it must not break monotonicity */
	while (g_main_context_iteration (NULL, FALSE))
		;
	g_assert_cmpuint (st.fractions->len, >=, 4);
	for (i = 1; i < st.fractions->len; i++)
		g_assert_cmpfloat (g_array_index (st.fractions, gdouble, i), >=,
		                   g_array_index (st.fractions, gdouble, i - 1));
	g_assert_cmpfloat (g_array_index (st.fractions, gdouble, st.fractions->len - 1), ==, 1.0);
	g_assert_cmpint (g_array_index (st.phases, gint, st.phases->len - 1), ==, LRG_LIGHTMAP_BAKE_PHASE_DONE);
	g_assert_cmpfloat (lrg_lightmap_baker_get_bake_time (baker), >, 0.0);

	g_array_unref (st.fractions);
	g_array_unref (st.phases);
	g_main_loop_unref (st.loop);
	cornell_items_free (items);
}

static gboolean
cancel_soon (gpointer data)
{
	g_cancellable_cancel (G_CANCELLABLE (data));
	return G_SOURCE_REMOVE;
}

static void
test_async_cancel (void)
{
	g_autoptr(LrgLightmapBaker) baker = lrg_lightmap_baker_new ();
	g_autoptr(GCancellable)     cancel = g_cancellable_new ();
	GArray                     *items = cornell_items ();
	AsyncState                  st;
	gint64                      t0;

	memset (&st, 0, sizeof st);
	st.loop = g_main_loop_new (NULL, FALSE);
	/* far too much work to finish before the cancel */
	g_object_set (lrg_lightmap_baker_get_settings (baker), "samples", 65536, "texels-per-unit", 16.0f, NULL);
	add_cornell (baker, items);
	lrg_lightmap_baker_bake_async (baker, cancel, on_baked, &st);
	g_timeout_add (50, cancel_soon, cancel);
	t0 = g_get_monotonic_time ();
	g_main_loop_run (st.loop);
	g_assert_false (st.ok);
	g_assert_error (st.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error (&st.error);
	/* stops promptly */
	g_assert_cmpint (g_get_monotonic_time () - t0, <, 10 * G_TIME_SPAN_SECOND);
	g_assert_false (lrg_lightmap_baker_is_running (baker));
	g_assert_null (lrg_lightmap_baker_get_lightmap (baker));

	/* a pre-cancelled sync bake fails the same way */
	{
		g_autoptr(GError) error = NULL;

		g_assert_false (lrg_lightmap_baker_bake (baker, cancel, &error));
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	}
	g_main_loop_unref (st.loop);
	cornell_items_free (items);
}

/* --- world collection ---------------------------------------------------- */

static void
test_add_world (void)
{
	g_autoptr(LrgWorld3D)       world = lrg_world3d_new ();
	g_autoptr(LrgLightmapBaker) baker = lrg_lightmap_baker_new ();
	g_autoptr(GError)           error = NULL;
	LrgEntity3D                *floor_e = lrg_entity3d_new ();
	LrgEntity3D                *dyn_e = lrg_entity3d_new ();
	LrgMeshRendererComponent   *mr = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_CUBE);
	LrgMeshRendererComponent   *mr2 = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_UV_SPHERE);

	lrg_entity3d_set_guid (floor_e, "guid-floor");
	lrg_mesh_renderer_component_set_size (mr, 4.0f, 0.2f, 4.0f);
	lrg_mesh_renderer_component_set_static_lighting (mr, TRUE);
	lrg_mesh_renderer_component_set_lightmap_scale (mr, 2.0f);
	lrg_entity3d_add_component (floor_e, LRG_COMPONENT (mr));
	lrg_entity3d_add_component (dyn_e, LRG_COMPONENT (mr2));
	lrg_world3d_add_entity (world, floor_e);
	lrg_world3d_add_entity (world, dyn_e);

	g_object_set (lrg_lightmap_baker_get_settings (baker), "samples", 4, "texels-per-unit", 2.0f,
	              "auto-probe-volume", FALSE, NULL);
	g_assert_cmpuint (lrg_lightmap_baker_add_world (baker, world), ==, 1);
	g_assert_cmpuint (lrg_lightmap_baker_get_n_objects (baker), ==, 1);
	lrg_lightmap_baker_set_sky_color (baker, 1.0f, 1.0f, 1.0f);
	g_assert_true (lrg_lightmap_baker_bake (baker, NULL, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_lightmap3d_get_entry (lrg_lightmap_baker_get_lightmap (baker), "guid-floor", NULL, NULL));
	/* the baked mesh is exactly the regenerable primitive */
	{
		gfloat               params[4] = { 4.0f, 0.2f, 4.0f, 0.0f };
		g_autoptr(LrgUvMesh) regen = lrg_uv_unwrap_new_lightmap_primitive (LRG_PRIMITIVE_CUBE, params);
		LrgUvMesh           *baked = lrg_lightmap_baker_get_object_mesh (baker, "guid-floor");
		gsize                n = 0;
		const gfloat        *uv = lrg_uv_mesh_get_uv2 (baked, &n);

		g_assert_cmpmem (uv, n * sizeof (gfloat), lrg_uv_mesh_get_uv2 (regen, NULL), n * sizeof (gfloat));
	}
}

/* --- benchmark (-m perf) ------------------------------------------------- */

static void
test_benchmark (void)
{
	GArray *items;
	gfloat  densities[3] = { 4.0f, 8.0f, 16.0f };
	guint   threads[3] = { 1, 4, 0 };
	guint   tris = 0;
	guint   d;
	guint   t;
	guint   i;

	if (!g_test_perf ())
	{
		g_test_skip ("run with -m perf");
		return;
	}
	items = cornell_items ();
	for (i = 0; i < items->len; i++)
		tris += lrg_uv_mesh_get_n_triangles (g_array_index (items, SceneItem, i).mesh);
	g_test_message ("Cornell box: %u triangles, 64 samples, 2 bounces, denoise on, no probes", tris);
	for (d = 0; d < G_N_ELEMENTS (densities); d++)
	{
		for (t = 0; t < G_N_ELEMENTS (threads); t++)
		{
			g_autoptr(LrgLightmapBaker) baker = lrg_lightmap_baker_new ();
			g_autoptr(GError)           error = NULL;

			g_object_set (lrg_lightmap_baker_get_settings (baker), "samples", 64, "texels-per-unit", densities[d],
			              "threads", threads[t], "auto-probe-volume", FALSE, NULL);
			add_cornell (baker, items);
			g_assert_true (lrg_lightmap_baker_bake (baker, NULL, &error));
			g_test_minimized_result (lrg_lightmap_baker_get_bake_time (baker),
			                         "tpu %.0f threads %u: %u texels, %.3f s", densities[d],
			                         threads[t] ? threads[t] : g_get_num_processors (),
			                         lrg_lightmap_baker_get_n_texels (baker),
			                         lrg_lightmap_baker_get_bake_time (baker));
		}
	}
	cornell_items_free (items);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/lightmap-bake/bvh/vs-brute-force", test_bvh_vs_brute);
	g_test_add_func ("/lightmap-bake/bvh/backface-and-index", test_bvh_backface_and_index);
	g_test_add_func ("/lightmap-bake/unwrap/primitives", test_unwrap_primitives);
	g_test_add_func ("/lightmap-bake/unwrap/curved", test_unwrap_curved);
	g_test_add_func ("/lightmap-bake/unwrap/errors-and-raylib", test_unwrap_errors_and_raylib);
	g_test_add_func ("/lightmap-bake/trace/furnace", test_furnace);
	g_test_add_func ("/lightmap-bake/trace/emissive-ceiling", test_emissive_ceiling);
	g_test_add_func ("/lightmap-bake/trace/point-light-falloff", test_point_light_falloff);
	g_test_add_func ("/lightmap-bake/trace/shadow", test_shadow);
	g_test_add_func ("/lightmap-bake/trace/sky-only", test_sky_only);
	g_test_add_func ("/lightmap-bake/probes/uniform-sky", test_probe_uniform_sky);
	g_test_add_func ("/lightmap-bake/bake/cornell", test_cornell_bake);
	g_test_add_func ("/lightmap-bake/bake/determinism", test_determinism);
	g_test_add_func ("/lightmap-bake/bake/async-progress", test_async_progress);
	g_test_add_func ("/lightmap-bake/bake/async-cancel", test_async_cancel);
	g_test_add_func ("/lightmap-bake/bake/add-world", test_add_world);
	g_test_add_func ("/lightmap-bake/bake/benchmark", test_benchmark);

	return g_test_run ();
}
