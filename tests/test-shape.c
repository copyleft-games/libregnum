/* test-shape.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgPhysicsShape and the narrowphase in lrg-collide: analytic
 * mass properties, quickhull invariants, GJK distances and EPA depths
 * against closed forms, contact manifolds (box resting, edge-edge,
 * capsule, feature-id persistence), convex vs triangle mesh/heightfield
 * with internal-edge removal, ray casts per shape, shape-cast time of
 * impact, determinism and throughput numbers.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define NEAR(a, b, tol) (fabs ((double)(a) - (double)(b)) <= (tol))

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Rigid transform: translation + rotation of @angle about @axis. */
static void
make_xf (gfloat       *out,
         gfloat        x,
         gfloat        y,
         gfloat        z,
         const gfloat *axis,
         gfloat        angle)
{
	gfloat p[3];
	gfloat q[4];
	gfloat one[3] = { 1.0f, 1.0f, 1.0f };

	p[0] = x;
	p[1] = y;
	p[2] = z;
	if (axis != NULL)
		lrg_math3d_quat_from_axis_angle (axis, angle, q);
	else
		lrg_math3d_quat_identity (q);
	lrg_math3d_compose_quat (p, q, one, out);
}

static void
make_pos (gfloat *out,
          gfloat  x,
          gfloat  y,
          gfloat  z)
{
	make_xf (out, x, y, z, NULL, 0.0f);
}

static const gfloat AX_X[3] = { 1.0f, 0.0f, 0.0f };
static const gfloat AX_Y[3] = { 0.0f, 1.0f, 0.0f };
static const gfloat AX_Z[3] = { 0.0f, 0.0f, 1.0f };

static const gfloat cube_pts[24] = {
	-1, -1, -1,  1, -1, -1,  1, 1, -1,  -1, 1, -1,
	-1, -1, 1,   1, -1, 1,   1, 1, 1,   -1, 1, 1
};

/* ==========================================================================
 * Mass properties
 * ========================================================================== */

static void
test_mass_primitives (void)
{
	g_autoptr(LrgPhysicsShape) sphere = NULL;
	g_autoptr(LrgPhysicsShape) box = NULL;
	g_autoptr(LrgPhysicsShape) cyl = NULL;
	g_autoptr(LrgPhysicsShape) cap = NULL;
	gfloat m;
	gfloat c[3];
	gfloat I[9];
	gdouble r;
	gdouble h;
	gdouble mc;
	gdouble ms;

	/* Sphere r=2, density 3: m = 3 * 4/3 pi 8, I = 2/5 m r^2. */
	sphere = lrg_physics_shape_new_sphere (2.0f);
	g_assert_true (lrg_physics_shape_get_mass_properties (sphere, 3.0f, &m, c, I));
	g_assert_true (NEAR (m, 3.0 * 4.0 / 3.0 * G_PI * 8.0, 1e-3));
	g_assert_true (NEAR (I[0], 0.4 * m * 4.0, 1e-2));
	g_assert_true (NEAR (I[4], I[0], 1e-4) && NEAR (I[8], I[0], 1e-4));
	g_assert_true (NEAR (I[1], 0.0, 1e-6));

	/* Box 2x4x6 (half 1,2,3), density 2: m = 96, Ix = m/12 (16 + 36). */
	box = lrg_physics_shape_new_box (1.0f, 2.0f, 3.0f);
	g_assert_true (lrg_physics_shape_get_mass_properties (box, 2.0f, &m, c, I));
	g_assert_true (NEAR (m, 96.0, 1e-3));
	g_assert_true (NEAR (I[0], 96.0 / 12.0 * (16.0 + 36.0), 1e-2));
	g_assert_true (NEAR (I[4], 96.0 / 12.0 * (4.0 + 36.0), 1e-2));
	g_assert_true (NEAR (I[8], 96.0 / 12.0 * (4.0 + 16.0), 1e-2));
	g_assert_true (NEAR (lrg_physics_shape_get_volume (box), 48.0, 1e-4));

	/* Cylinder r=0.5 half height 1 along Y. */
	cyl = lrg_physics_shape_new_cylinder (0.5f, 1.0f);
	g_assert_true (lrg_physics_shape_get_mass_properties (cyl, 1.0f, &m, c, I));
	g_assert_true (NEAR (m, G_PI * 0.25 * 2.0, 1e-4));
	g_assert_true (NEAR (I[4], 0.5 * m * 0.25, 1e-4));
	g_assert_true (NEAR (I[0], m * (3.0 * 0.25 + 4.0) / 12.0, 1e-4));

	/* Capsule r=0.5 half height 1: cylinder + two hemispheres. */
	cap = lrg_physics_shape_new_capsule (0.5f, 1.0f);
	g_assert_true (lrg_physics_shape_get_mass_properties (cap, 1.0f, &m, c, I));
	r = 0.5;
	h = 2.0;
	mc = G_PI * r * r * h;
	ms = 4.0 / 3.0 * G_PI * r * r * r;
	g_assert_true (NEAR (m, mc + ms, 1e-4));
	g_assert_true (NEAR (I[4], mc * r * r / 2.0 + ms * 0.4 * r * r, 1e-4));
	g_assert_true (NEAR (I[0], mc * (h * h / 12.0 + r * r / 4.0)
	                           + ms * (0.4 * r * r + h * h / 4.0 + 3.0 * h * r / 8.0), 1e-4));

	/* Static kinds have no mass. */
	{
		gfloat n[3] = { 0, 1, 0 };
		g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (n, 0.0f);

		g_assert_false (lrg_physics_shape_get_mass_properties (plane, 1.0f, &m, c, I));
	}
	g_assert_false (lrg_physics_shape_get_mass_properties (box, 0.0f, &m, c, I));
}

static void
test_mass_hull_cube (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsShape) hull = NULL;
	g_autoptr(LrgPhysicsShape) box = NULL;
	gfloat pts[24];
	gfloat m;
	gfloat c[3];
	gfloat I[9];
	gfloat bm;
	gfloat bc[3];
	gfloat bI[9];
	guint i;

	/* A cube hull offset by (1, 2, 3) must match the box, COM shifted. */
	for (i = 0; i < 24; i++)
		pts[i] = cube_pts[i] + (gfloat)(i % 3 + 1);
	hull = lrg_physics_shape_new_convex_hull (pts, 24, 0.0f, &error);
	g_assert_no_error (error);
	box = lrg_physics_shape_new_box (1.0f, 1.0f, 1.0f);
	g_assert_true (lrg_physics_shape_get_mass_properties (hull, 1.5f, &m, c, I));
	g_assert_true (lrg_physics_shape_get_mass_properties (box, 1.5f, &bm, bc, bI));
	g_assert_true (NEAR (m, bm, 1e-4));
	g_assert_true (NEAR (c[0], 1.0, 1e-5) && NEAR (c[1], 2.0, 1e-5) && NEAR (c[2], 3.0, 1e-5));
	for (i = 0; i < 9; i++)
		g_assert_true (NEAR (I[i], bI[i], 1e-3));
	g_assert_cmpuint (lrg_physics_shape_get_hull_face_count (hull), ==, 6);
}

static void
test_mass_compound (void)
{
	g_autoptr(LrgPhysicsShape) half = NULL;
	g_autoptr(LrgPhysicsShape) whole = NULL;
	g_autoptr(LrgPhysicsShape) comp = NULL;
	gfloat p0[3] = { -0.5f, 0.0f, 0.0f };
	gfloat p1[3] = { 0.5f, 0.0f, 0.0f };
	gfloat m;
	gfloat c[3];
	gfloat I[9];
	gfloat wm;
	gfloat wc[3];
	gfloat wI[9];
	guint i;

	/* Two 1x2x2 boxes side by side == one 2x2x2 box. */
	half = lrg_physics_shape_new_box (0.5f, 1.0f, 1.0f);
	whole = lrg_physics_shape_new_box (1.0f, 1.0f, 1.0f);
	comp = lrg_physics_shape_new_compound ();
	g_assert_cmpint (lrg_physics_shape_compound_add_child (comp, half, p0, NULL), ==, 0);
	g_assert_cmpint (lrg_physics_shape_compound_add_child (comp, half, p1, NULL), ==, 1);
	g_assert_cmpint (lrg_physics_shape_compound_add_child (comp, comp, p1, NULL), ==, -1);
	g_assert_cmpuint (lrg_physics_shape_get_child_count (comp), ==, 2);
	g_assert_true (lrg_physics_shape_get_child (comp, 1, NULL, NULL) == half);
	g_assert_true (lrg_physics_shape_get_mass_properties (comp, 1.0f, &m, c, I));
	g_assert_true (lrg_physics_shape_get_mass_properties (whole, 1.0f, &wm, wc, wI));
	g_assert_true (NEAR (m, wm, 1e-4));
	for (i = 0; i < 3; i++)
		g_assert_true (NEAR (c[i], 0.0, 1e-5));
	for (i = 0; i < 9; i++)
		g_assert_true (NEAR (I[i], wI[i], 1e-3));
}

/* ==========================================================================
 * Quickhull
 * ========================================================================== */

/* Checks the hull of @pts: every input point inside every face plane,
 * every face planar and convex (all hull vertices behind it), every
 * directed edge used once with its twin present, V - E + F = 2. */
static void
check_hull (const gfloat *pts,
            guint         n_floats,
            gfloat        radius)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsShape) hull = NULL;
	const gfloat *v;
	guint nv;
	guint nf;
	guint f;
	guint i;
	guint edges;
	g_autoptr(GHashTable) dir = NULL;

	hull = lrg_physics_shape_new_convex_hull (pts, n_floats, radius, &error);
	g_assert_no_error (error);
	v = lrg_physics_shape_get_hull_vertices (hull, &nv);
	nv /= 3;
	nf = lrg_physics_shape_get_hull_face_count (hull);
	g_assert_cmpuint (nf, >=, 4);
	dir = g_hash_table_new (g_int64_hash, g_int64_equal);
	edges = 0;
	for (f = 0; f < nf; f++)
	{
		guint n;
		gfloat pl[4];
		const guint *idx;

		idx = lrg_physics_shape_get_hull_face (hull, f, &n, pl);
		g_assert_nonnull (idx);
		g_assert_cmpuint (n, >=, 3);
		g_assert_true (NEAR (sqrt (pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]), 1.0, 1e-4));
		/* Input points behind the plane. */
		for (i = 0; i < n_floats / 3; i++)
		{
			const gfloat *p = &pts[i * 3];

			g_assert_cmpfloat (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3], <=, 1e-4);
		}
		/* Face vertices on the plane. */
		for (i = 0; i < n; i++)
		{
			const gfloat *p = &v[idx[i] * 3];

			g_assert_cmpfloat (fabs (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3]), <=, 1e-3);
		}
		/* Directed edges unique. */
		for (i = 0; i < n; i++)
		{
			gint64 *key = g_new (gint64, 1);

			*key = ((gint64)idx[i] << 32) | (gint64)idx[(i + 1) % n];
			g_assert_false (g_hash_table_contains (dir, key));
			g_hash_table_add (dir, key);
			edges++;
		}
	}
	/* Every directed edge has its twin. */
	{
		GHashTableIter it;
		gpointer key;

		g_hash_table_iter_init (&it, dir);
		while (g_hash_table_iter_next (&it, &key, NULL))
		{
			gint64 k = *(gint64 *)key;
			gint64 tw = ((k & 0xffffffff) << 32) | (k >> 32);

			g_assert_true (g_hash_table_contains (dir, &tw));
		}
		/* free keys (the table owns nothing) */
		g_hash_table_iter_init (&it, dir);
		while (g_hash_table_iter_next (&it, &key, NULL))
			g_free (key);
	}
	g_assert_cmpint ((gint)nv - (gint)(edges / 2) + (gint)nf, ==, 2);
}

static void
test_quickhull_random (void)
{
	g_autoptr(GRand) rng = g_rand_new_with_seed (1234);
	guint sizes[] = { 4, 12, 50, 200, 1000 };
	guint s;

	for (s = 0; s < G_N_ELEMENTS (sizes); s++)
	{
		guint trial;

		for (trial = 0; trial < 6; trial++)
		{
			g_autofree gfloat *pts = g_new (gfloat, sizes[s] * 3);
			guint i;

			for (i = 0; i < sizes[s]; i++)
			{
				gfloat x;
				gfloat y;
				gfloat z;

				x = (gfloat)g_rand_double_range (rng, -2.0, 2.0);
				y = (gfloat)g_rand_double_range (rng, -1.0, 1.0);
				z = (gfloat)g_rand_double_range (rng, -3.0, 3.0);
				/* Odd trials: points on a sphere (all on the hull). */
				if (trial % 2 == 1)
				{
					gfloat l = sqrtf (x * x + y * y + z * z) + 1e-6f;

					x /= l;
					y /= l;
					z /= l;
				}
				pts[i * 3] = x;
				pts[i * 3 + 1] = y;
				pts[i * 3 + 2] = z;
			}
			check_hull (pts, sizes[s] * 3, trial == 2 ? 0.05f : 0.0f);
		}
	}
	/* Cube with interior and face-centre points: still 8 / 6. */
	{
		gfloat pts[24 + 9];
		g_autoptr(LrgPhysicsShape) h = NULL;
		guint nv;

		memcpy (pts, cube_pts, sizeof (cube_pts));
		pts[24] = 0; pts[25] = 0; pts[26] = 0;
		pts[27] = 0; pts[28] = 1; pts[29] = 0;
		pts[30] = 1; pts[31] = 0.2f; pts[32] = 0.1f;
		check_hull (pts, 33, 0.0f);
		h = lrg_physics_shape_new_convex_hull (pts, 33, 0.0f, NULL);
		lrg_physics_shape_get_hull_vertices (h, &nv);
		g_assert_cmpuint (nv / 3, ==, 8);
		g_assert_cmpuint (lrg_physics_shape_get_hull_face_count (h), ==, 6);
	}
}

static void
test_quickhull_degenerate (void)
{
	g_autoptr(GError) error = NULL;
	gfloat flat[12] = { 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1 };
	gfloat bad[12] = { 0, 0, 0, 1, 0, 0, 0, 0, 1, NAN, 0, 1 };
	LrgPhysicsShape *s;

	s = lrg_physics_shape_new_convex_hull (flat, 12, 0.0f, &error);
	g_assert_null (s);
	g_assert_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_DEGENERATE);
	g_clear_error (&error);
	s = lrg_physics_shape_new_convex_hull (bad, 12, 0.0f, &error);
	g_assert_null (s);
	g_assert_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID);
	g_clear_error (&error);
	s = lrg_physics_shape_new_convex_hull (flat, 6, 0.0f, &error);
	g_assert_null (s);
	g_assert_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID);
}

/* ==========================================================================
 * GJK / EPA
 * ========================================================================== */

static void
test_gjk_distance (void)
{
	g_autoptr(LrgPhysicsShape) s1 = lrg_physics_shape_new_sphere (0.5f);
	g_autoptr(LrgPhysicsShape) s2 = lrg_physics_shape_new_sphere (1.0f);
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (1.0f, 1.0f, 1.0f);
	g_autoptr(LrgPhysicsShape) cap = lrg_physics_shape_new_capsule (0.25f, 1.0f);
	g_autoptr(LrgPhysicsShape) hull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.0f, NULL);
	gfloat xa[16];
	gfloat xb[16];
	gfloat d;
	gfloat pa[3];
	gfloat pb[3];
	gfloat n[3];

	/* Sphere-sphere along a diagonal: |c| - 1.5. */
	make_pos (xa, 0, 0, 0);
	make_pos (xb, 2, 2, 1);
	g_assert_true (lrg_collide_closest_points (s1, xa, s2, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, 3.0 - 1.5, 1e-4));
	g_assert_true (NEAR (n[0], 2.0 / 3.0, 1e-4) && NEAR (n[2], 1.0 / 3.0, 1e-4));

	/* Box vs sphere: face region, edge region, corner region. */
	make_pos (xb, 3, 0.2f, 0);
	g_assert_true (lrg_collide_closest_points (box, xa, s1, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, 3.0 - 1.0 - 0.5, 1e-4));
	g_assert_true (NEAR (pa[0], 1.0, 1e-4) && NEAR (pa[1], 0.2, 1e-4));
	make_pos (xb, 3, 3, 0);
	g_assert_true (lrg_collide_closest_points (box, xa, s1, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, sqrt (8.0) - 0.5, 1e-4));
	make_pos (xb, 3, 3, 3);
	g_assert_true (lrg_collide_closest_points (box, xa, s1, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, sqrt (12.0) - 0.5, 1e-4));

	/* Box vs box separated along X with rotation about X (face gap). */
	make_xf (xb, 3.5f, 0, 0, AX_X, 0.3f);
	g_assert_true (lrg_collide_closest_points (box, xa, box, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, 1.5, 1e-4));

	/* Capsule (vertical, segment +-1, r 0.25) vs sphere beside it. */
	make_pos (xb, 2, 0.5f, 0);
	g_assert_true (lrg_collide_closest_points (cap, xa, s1, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, 2.0 - 0.75, 1e-4));
	/* Capsule vs capsule crossed (one along X, offset in Z by 2). */
	make_xf (xb, 0, 0, 2, AX_Z, (gfloat)G_PI_2);
	g_assert_true (lrg_collide_closest_points (cap, xa, cap, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, 2.0 - 0.5, 1e-4));
	/* Capsule above a box. */
	make_xf (xb, 0.3f, 3.0f, 0, AX_Z, (gfloat)G_PI_2);
	g_assert_true (lrg_collide_closest_points (box, xa, cap, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, 3.0 - 1.0 - 0.25, 1e-4));
	g_assert_true (NEAR (n[1], 1.0, 1e-4));

	/* Hull (cube) vs sphere, corner region. */
	make_pos (xb, 2, 2, 2);
	g_assert_true (lrg_collide_closest_points (hull, xa, s1, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, sqrt (3.0) - 0.5, 1e-4));

	/* Non-convex shapes are refused. */
	{
		gfloat pn[3] = { 0, 1, 0 };
		g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (pn, 0.0f);

		g_assert_false (lrg_collide_closest_points (plane, xa, s1, xb, &d, NULL, NULL, NULL));
	}
}

static void
test_epa_depth (void)
{
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (1.0f, 1.0f, 1.0f);
	g_autoptr(LrgPhysicsShape) hull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.0f, NULL);
	g_autoptr(LrgPhysicsShape) s1 = lrg_physics_shape_new_sphere (0.5f);
	g_autoptr(LrgPhysicsShape) cyl = lrg_physics_shape_new_cylinder (0.5f, 1.0f);
	gfloat xa[16];
	gfloat xb[16];
	gfloat d;
	gfloat pa[3];
	gfloat pb[3];
	gfloat n[3];

	make_pos (xa, 0, 0, 0);

	/* Hull vs hull overlapping by 0.1 along X: EPA depth 0.1, normal +X. */
	make_pos (xb, 1.9f, 0.3f, -0.2f);
	g_assert_true (lrg_collide_closest_points (hull, xa, hull, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, -0.1, 1e-4));
	g_assert_true (NEAR (n[0], 1.0, 1e-4));
	/* ... along -Y by 0.25. */
	make_pos (xb, 0.2f, -1.75f, 0.1f);
	g_assert_true (lrg_collide_closest_points (hull, xa, hull, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, -0.25, 1e-4));
	g_assert_true (NEAR (n[1], -1.0, 1e-4));

	/* Sphere centre inside the hull, 0.3 below the top face: the
	 * penetration is 0.3 + 0.5 along +Y. */
	make_pos (xb, 0.1f, 0.7f, 0.0f);
	g_assert_true (lrg_collide_closest_points (hull, xa, s1, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, -0.8, 1e-3));
	g_assert_true (NEAR (n[1], 1.0, 1e-3));

	/* Cylinder standing on the box, sunk 0.05. */
	make_pos (xb, 0.2f, 1.95f, 0.0f);
	g_assert_true (lrg_collide_closest_points (box, xa, cyl, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, -0.05, 1e-3));
	g_assert_true (NEAR (n[1], 1.0, 1e-3));

	/* Rotated hull: 45 degrees about Y, overlap along X measured to the
	 * rotated corner edge (sqrt 2). */
	make_xf (xb, 1.0f + (gfloat)G_SQRT2 - 0.1f, 0.0f, 0.0f, AX_Y, (gfloat)G_PI_4);
	g_assert_true (lrg_collide_closest_points (hull, xa, hull, xb, &d, pa, pb, n));
	g_assert_true (NEAR (d, -0.1, 2e-3));
	g_assert_true (NEAR (n[0], 1.0, 1e-3));
}


/* Random overlapping convex pairs: the manifold must agree with GJK/EPA
 * (finite unit normal, deepest point equal to the EPA depth), and box-box
 * SAT must agree with EPA on the same boxes built as hulls. */
static void
test_random_consistency (void)
{
	g_autoptr(GRand) rng = g_rand_new_with_seed (99);
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (0.5f, 0.3f, 0.7f);
	g_autoptr(LrgPhysicsShape) hull = NULL;
	g_autoptr(LrgPhysicsShape) cap = lrg_physics_shape_new_capsule (0.3f, 0.4f);
	g_autoptr(LrgPhysicsShape) cyl = lrg_physics_shape_new_cylinder (0.4f, 0.5f);
	g_autoptr(LrgPhysicsShape) sph = lrg_physics_shape_new_sphere (0.45f);
	g_autoptr(LrgPhysicsShape) rh = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.1f, NULL);
	LrgPhysicsShape *shapes[6];
	gfloat bp[24];
	guint i;
	guint checked;

	for (i = 0; i < 24; i++)
		bp[i] = cube_pts[i] * (i % 3 == 0 ? 0.5f : (i % 3 == 1 ? 0.3f : 0.7f));
	hull = lrg_physics_shape_new_convex_hull (bp, 24, 0.0f, NULL);
	shapes[0] = box; shapes[1] = hull; shapes[2] = cap;
	shapes[3] = cyl; shapes[4] = sph; shapes[5] = rh;
	checked = 0;
	for (i = 0; i < 3000; i++)
	{
		LrgPhysicsShape *a;
		LrgPhysicsShape *b;
		gfloat xa[16];
		gfloat xb[16];
		gfloat ax[3];
		gfloat bx[3];
		gfloat d;
		gfloat n[3];
		LrgContactManifold m;
		guint k;
		gfloat deepest;

		a = shapes[g_rand_int_range (rng, 0, 6)];
		b = shapes[g_rand_int_range (rng, 0, 6)];
		for (k = 0; k < 3; k++)
		{
			ax[k] = (gfloat)g_rand_double_range (rng, -1, 1);
			bx[k] = (gfloat)g_rand_double_range (rng, -1, 1);
		}
		make_xf (xa, 0, 0, 0, ax, (gfloat)g_rand_double_range (rng, 0, 6.28));
		make_xf (xb, (gfloat)g_rand_double_range (rng, -1.2, 1.2),
		         (gfloat)g_rand_double_range (rng, -1.2, 1.2),
		         (gfloat)g_rand_double_range (rng, -1.2, 1.2), bx,
		         (gfloat)g_rand_double_range (rng, 0, 6.28));
		g_assert_true (lrg_collide_closest_points (a, xa, b, xb, &d, NULL, NULL, n));
		g_assert_true (isfinite (d));
		g_assert_true (NEAR (sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0, 1e-3));
		if (d > -2e-3)
			continue;
		g_assert_true (lrg_collide_shapes (a, xa, b, xb, &m, 0.0f));
		g_assert_cmpuint (m.n_points, >=, 1);
		g_assert_cmpuint (m.n_points, <=, 4);
		deepest = -G_MAXFLOAT;
		for (k = 0; k < m.n_points; k++)
		{
			g_assert_true (isfinite (m.points_a[k * 3]) && isfinite (m.points_b[k * 3 + 2]));
			deepest = MAX (deepest, m.depths[k]);
		}
		/* Clipped points never exceed the true depth by much, and the
		 * deepest is close to it (curved faces may lose a little). */
		g_assert_cmpfloat (deepest, <=, -d * 1.1f + 5e-3f);
		g_assert_cmpfloat (deepest, >=, (-d) * 0.5f - 2e-3f);
		g_assert_true (NEAR (sqrt (m.normal[0] * m.normal[0] + m.normal[1] * m.normal[1]
		                           + m.normal[2] * m.normal[2]), 1.0, 1e-4));
		/* The normal separates: moving B along it reduces the overlap. */
		g_assert_cmpfloat (m.normal[0] * n[0] + m.normal[1] * n[1] + m.normal[2] * n[2], >, 0.0f);
		checked++;

		/* SAT (box-box) vs EPA (same boxes as hulls). */
		if (a == box && b == box)
		{
			gfloat dh;

			g_assert_true (lrg_collide_closest_points (hull, xa, hull, xb, &dh, NULL, NULL, NULL));
			/* SAT prefers face axes within 5 %, so it may report a
			 * slightly deeper face contact than the exact minimum. */
			g_assert_true (NEAR (deepest, -dh, 2e-3 + 0.05 * -dh));
		}
	}
	g_assert_cmpuint (checked, >, 1000);
}

/* ==========================================================================
 * Manifolds
 * ========================================================================== */

static void
test_manifold_box_resting (void)
{
	g_autoptr(LrgPhysicsShape) ground = lrg_physics_shape_new_box (5.0f, 0.5f, 5.0f);
	g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.5f, 0.5f, 0.5f);
	LrgContactManifold m;
	gfloat xa[16];
	gfloat xb[16];
	guint i;

	make_pos (xa, 0, -0.5f, 0);
	make_pos (xb, 1.0f, 0.49f, 2.0f);
	g_assert_true (lrg_collide_shapes (ground, xa, cube, xb, &m, 0.02f));
	g_assert_cmpuint (m.n_points, ==, 4);
	g_assert_true (NEAR (m.normal[1], 1.0, 1e-5));
	for (i = 0; i < 4; i++)
	{
		gfloat pa[3];
		gfloat pb[3];
		gfloat depth;

		g_assert_true (lrg_contact_manifold_get_point (&m, i, pa, pb, &depth, NULL));
		g_assert_true (NEAR (depth, 0.01, 1e-4));
		g_assert_true (NEAR (fabs (pb[0] - 1.0), 0.5, 1e-4));
		g_assert_true (NEAR (fabs (pb[2] - 2.0), 0.5, 1e-4));
		g_assert_true (NEAR (pb[1], -0.01, 1e-4));
		g_assert_true (NEAR (pa[1], 0.0, 1e-4));
	}
	/* Swapped order: normal flips, points still 4. */
	g_assert_true (lrg_collide_shapes (cube, xb, ground, xa, &m, 0.02f));
	g_assert_cmpuint (m.n_points, ==, 4);
	g_assert_true (NEAR (m.normal[1], -1.0, 1e-5));

	/* Rotated about Y: 4 points, corners on the rotated square. */
	make_xf (xb, 0.0f, 0.49f, 0.0f, AX_Y, 0.5f);
	g_assert_true (lrg_collide_shapes (ground, xa, cube, xb, &m, 0.0f));
	g_assert_cmpuint (m.n_points, ==, 4);
	for (i = 0; i < 4; i++)
	{
		gfloat r = sqrtf (m.points_b[i * 3] * m.points_b[i * 3]
		                  + m.points_b[i * 3 + 2] * m.points_b[i * 3 + 2]);

		g_assert_true (NEAR (r, G_SQRT2 * 0.5, 1e-4));
	}

	/* Speculative: 0.03 apart is reported with depth -0.03 only when the
	 * speculative distance covers it. */
	make_pos (xb, 0, 0.53f, 0);
	g_assert_false (lrg_collide_shapes (ground, xa, cube, xb, &m, 0.01f));
	g_assert_true (lrg_collide_shapes (ground, xa, cube, xb, &m, 0.05f));
	g_assert_cmpuint (m.n_points, ==, 4);
	g_assert_true (NEAR (m.depths[0], -0.03, 1e-4));
}

static void
test_manifold_edge_edge (void)
{
	g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.5f, 0.5f, 0.5f);
	LrgContactManifold m;
	gfloat xa[16];
	gfloat xb[16];
	gdouble top;

	/* A: rotated 45 deg about Z (top edge along Z at y = sqrt2/2).
	 * B: rotated 45 deg about X (bottom edge along X), just touching. */
	top = G_SQRT2 * 0.5;
	make_xf (xa, 0, 0, 0, AX_Z, (gfloat)G_PI_4);
	make_xf (xb, 0, (gfloat)(2.0 * top - 0.02), 0, AX_X, (gfloat)G_PI_4);
	g_assert_true (lrg_collide_shapes (cube, xa, cube, xb, &m, 0.0f));
	g_assert_cmpuint (m.n_points, ==, 1);
	g_assert_true (NEAR (m.normal[1], 1.0, 1e-4));
	g_assert_true (NEAR (m.depths[0], 0.02, 1e-4));
	g_assert_true (NEAR (m.points_a[0], 0.0, 1e-4) && NEAR (m.points_a[2], 0.0, 1e-4));
	g_assert_true (NEAR (m.points_a[1], top, 1e-4));
	g_assert_true (NEAR (m.points_b[1], top - 0.02, 1e-4));
}

static void
test_manifold_capsule_sphere (void)
{
	g_autoptr(LrgPhysicsShape) ground = lrg_physics_shape_new_box (5.0f, 0.5f, 5.0f);
	g_autoptr(LrgPhysicsShape) cap = lrg_physics_shape_new_capsule (0.25f, 1.0f);
	g_autoptr(LrgPhysicsShape) s = lrg_physics_shape_new_sphere (0.5f);
	g_autoptr(LrgPhysicsShape) cyl = lrg_physics_shape_new_cylinder (0.5f, 0.5f);
	LrgContactManifold m;
	gfloat xa[16];
	gfloat xb[16];

	make_pos (xa, 0, -0.5f, 0);

	/* Capsule lying along X on the ground: 2 points at the segment ends. */
	make_xf (xb, 0, 0.24f, 0, AX_Z, (gfloat)G_PI_2);
	g_assert_true (lrg_collide_shapes (ground, xa, cap, xb, &m, 0.0f));
	g_assert_cmpuint (m.n_points, ==, 2);
	g_assert_true (NEAR (fabs (m.points_b[0]), 1.0, 1e-3));
	g_assert_true (NEAR (fabs (m.points_b[3]), 1.0, 1e-3));
	g_assert_true (NEAR (m.depths[0], 0.01, 1e-4));

	/* Two parallel capsules: 2 points over the overlap. */
	{
		gfloat x1[16];
		gfloat x2[16];

		make_xf (x1, 0, 0, 0, AX_Z, (gfloat)G_PI_2);
		make_xf (x2, 0.5f, 0.45f, 0, AX_Z, (gfloat)G_PI_2);
		g_assert_true (lrg_collide_shapes (cap, x1, cap, x2, &m, 0.0f));
		g_assert_cmpuint (m.n_points, ==, 2);
		g_assert_true (NEAR (m.depths[0], 0.05, 1e-4));
		g_assert_true (NEAR (m.normal[1], 1.0, 1e-4));
	}

	/* Sphere on ground: 1 point. */
	make_pos (xb, 0.3f, 0.45f, -0.2f);
	g_assert_true (lrg_collide_shapes (ground, xa, s, xb, &m, 0.0f));
	g_assert_cmpuint (m.n_points, ==, 1);
	g_assert_true (NEAR (m.depths[0], 0.05, 1e-5));
	g_assert_true (NEAR (m.points_b[0], 0.3, 1e-5) && NEAR (m.points_b[1], -0.05, 1e-5));

	/* Cylinder standing: the 8-point cap reduces to 4. */
	make_pos (xb, 0, 0.49f, 0);
	g_assert_true (lrg_collide_shapes (ground, xa, cyl, xb, &m, 0.0f));
	g_assert_cmpuint (m.n_points, ==, 4);
	g_assert_true (NEAR (m.depths[0], 0.01, 1e-4));

	/* Sphere vs sphere. */
	{
		gfloat x1[16];
		gfloat x2[16];

		make_pos (x1, 10, 0, 0);
		make_pos (x2, 10.8f, 0, 0);
		g_assert_true (lrg_collide_shapes (s, x1, s, x2, &m, 0.0f));
		g_assert_cmpuint (m.n_points, ==, 1);
		g_assert_true (NEAR (m.depths[0], 0.2, 1e-5));
		g_assert_true (NEAR (m.normal[0], 1.0, 1e-6));
		g_assert_true (NEAR (m.points_a[0], 10.5, 1e-5));
	}
}

static int
cmp_u32 (const void *a,
         const void *b)
{
	guint32 x = *(const guint32 *)a;
	guint32 y = *(const guint32 *)b;

	return x < y ? -1 : (x > y ? 1 : 0);
}

static void
test_manifold_feature_ids (void)
{
	g_autoptr(LrgPhysicsShape) ground = lrg_physics_shape_new_box (5.0f, 0.5f, 5.0f);
	g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.5f, 0.5f, 0.5f);
	LrgContactManifold m1;
	LrgContactManifold m2;
	gfloat xa[16];
	gfloat xb[16];
	guint32 a[4];
	guint32 b[4];
	guint i;

	/* A small slide keeps the same touching features: same id set. */
	make_pos (xa, 0, -0.5f, 0);
	make_pos (xb, 0.0f, 0.49f, 0.0f);
	g_assert_true (lrg_collide_shapes (ground, xa, cube, xb, &m1, 0.0f));
	make_xf (xb, 0.03f, 0.495f, -0.02f, AX_Y, 0.01f);
	g_assert_true (lrg_collide_shapes (ground, xa, cube, xb, &m2, 0.0f));
	g_assert_cmpuint (m1.n_points, ==, 4);
	g_assert_cmpuint (m2.n_points, ==, 4);
	memcpy (a, m1.feature_ids, sizeof (a));
	memcpy (b, m2.feature_ids, sizeof (b));
	qsort (a, 4, sizeof (guint32), cmp_u32);
	qsort (b, 4, sizeof (guint32), cmp_u32);
	for (i = 0; i < 4; i++)
		g_assert_cmpuint (a[i], ==, b[i]);
	/* Ids are distinct within the manifold. */
	for (i = 0; i < 3; i++)
		g_assert_cmpuint (a[i], !=, a[i + 1]);

	/* Exactly aligned and touching (corners on the clip planes) gives the
	 * same ids as the offset pose. */
	make_pos (xb, 0.0f, 0.5f, 0.0f);
	g_assert_true (lrg_collide_shapes (ground, xa, cube, xb, &m2, 0.01f));
	g_assert_cmpuint (m2.n_points, ==, 4);
	memcpy (b, m2.feature_ids, sizeof (b));
	qsort (b, 4, sizeof (guint32), cmp_u32);
	for (i = 0; i < 4; i++)
		g_assert_cmpuint (a[i], ==, b[i]);
}

/* ==========================================================================
 * Convex vs triangles
 * ========================================================================== */

static LrgPhysicsShape *
make_quad_mesh (void)
{
	/* 4x4 quad at y = 0 split along its diagonal. */
	static const gfloat v[12] = { -2, 0, -2,  2, 0, -2,  2, 0, 2,  -2, 0, 2 };
	static const guint idx[6] = { 0, 2, 1,  0, 3, 2 };
	static const guint mat[2] = { 7, 9 };
	GError *error = NULL;
	LrgPhysicsShape *s;

	s = lrg_physics_shape_new_triangle_mesh_full (v, 12, idx, 6, mat, NULL, 2,
	                                              LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE, &error);
	g_assert_no_error (error);
	return s;
}

static void
test_mesh_active_edges (void)
{
	g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
	gfloat v[9];
	guint act;
	guint mat;
	guint k;
	guint t;

	g_assert_cmpuint (lrg_physics_shape_get_triangle_count (quad), ==, 2);
	for (t = 0; t < 2; t++)
	{
		guint inactive = 0;

		g_assert_true (lrg_physics_shape_get_triangle (quad, t, v, &mat, NULL, &act));
		g_assert_cmpuint (mat, ==, t == 0 ? 7 : 9);
		for (k = 0; k < 3; k++)
			if (!(act & (1u << k)))
				inactive++;
		/* The shared diagonal is the only inactive edge. */
		g_assert_cmpuint (inactive, ==, 1);
	}
}

/* Slides a box across @floor at many positions; every manifold must have
 * an up normal (no ghost collision on internal edges). */
static void
check_no_ghost (LrgPhysicsShape *floor,
                const gfloat    *xf_floor,
                gfloat           y_top)
{
	g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.25f, 0.25f, 0.25f);
	g_autoptr(GArray) all = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	gfloat x;
	guint total;

	total = 0;
	for (x = -1.5f; x <= 1.5f; x += 0.05f)
	{
		gfloat xb[16];
		guint i;
		guint n;

		g_array_set_size (all, 0);
		/* Moving along a line that crosses the diagonal, slightly sunk
		 * and tilted, so edge features near the diagonal are tested. */
		make_xf (xb, x, y_top + 0.25f - 0.02f, x * 0.37f + 0.1f, AX_Y, 0.3f);
		n = lrg_collide_shapes_all (cube, xb, floor, xf_floor, 0.01f, all);
		g_assert_cmpuint (n, >=, 1);
		for (i = 0; i < all->len; i++)
		{
			const LrgContactManifold *m = &g_array_index (all, LrgContactManifold, i);

			/* cube is A, floor B: A->B points down. */
			g_assert_cmpfloat (m->normal[1], <, -0.9999f);
			g_assert_cmpuint (m->sub_shape_b, !=, LRG_PHYSICS_NO_SUB_SHAPE);
			g_assert_cmpuint (m->n_points, <=, 4);
			total++;
		}
		/* Merged: one manifold for the flat quad. */
		g_assert_cmpuint (n, ==, 1);
	}
	g_assert_cmpuint (total, >, 10);
}

static void
test_mesh_no_ghost (void)
{
	g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
	gfloat id[16];

	make_pos (id, 0, 0, 0);
	check_no_ghost (quad, id, 0.0f);
}

static void
test_heightfield (void)
{
	gfloat h[25];
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsShape) hf = NULL;
	gfloat xf[16];
	guint i;
	guint nx;
	guint nz;
	gfloat sc[3];

	for (i = 0; i < 25; i++)
		h[i] = 1.0f;
	hf = lrg_physics_shape_new_heightfield (h, 25, 5, 5, 1.0f, 0.5f, 1.0f, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_physics_shape_get_heightfield_size (hf, &nx, &nz, sc));
	g_assert_cmpuint (nx, ==, 5);
	g_assert_true (NEAR (sc[1], 0.5, 1e-6));
	g_assert_true (NEAR (lrg_physics_shape_get_heightfield_height (hf, 2, 2), 0.5, 1e-6));
	g_assert_cmpuint (lrg_physics_shape_get_triangle_count (hf), ==, 32);

	/* Grid centred under the origin: flat at y = 0.5. */
	make_pos (xf, -2.0f, 0.0f, -2.0f);
	check_no_ghost (hf, xf, 0.5f);

	/* Hole: a box over a hole cell gets no contact. */
	{
		gfloat hh[25];
		g_autoptr(LrgPhysicsShape) holed = NULL;
		g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.2f, 0.2f, 0.2f);
		LrgContactManifold m;
		gfloat xb[16];
		gfloat id[16];
		gfloat v[9];

		for (i = 0; i < 25; i++)
			hh[i] = 0.0f;
		hh[2 * 5 + 2] = LRG_PHYSICS_HEIGHTFIELD_HOLE;
		holed = lrg_physics_shape_new_heightfield (hh, 25, 5, 5, 1.0f, 1.0f, 1.0f, &error);
		g_assert_no_error (error);
		make_pos (id, 0, 0, 0);
		/* Cells (1..2, 1..2) touch the hole sample (2, 2). */
		make_pos (xb, 2.0f, 0.15f, 2.0f);
		g_assert_false (lrg_collide_shapes (cube, xb, holed, id, &m, 0.0f));
		make_pos (xb, 0.5f, 0.15f, 0.5f);
		g_assert_true (lrg_collide_shapes (cube, xb, holed, id, &m, 0.0f));
		g_assert_true (NEAR (m.depths[0], 0.05, 1e-4));
		g_assert_false (lrg_physics_shape_get_triangle (holed, (1 * 4 + 1) * 2, v, NULL, NULL, NULL));
	}

	/* Bad input. */
	{
		LrgPhysicsShape *bad;

		bad = lrg_physics_shape_new_heightfield (h, 24, 5, 5, 1, 1, 1, &error);
		g_assert_null (bad);
		g_assert_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID);
	}
}

static void
test_mesh_wall_keeps_normal (void)
{
	/* Floor quad + a vertical wall at x = 1 sharing an edge: the convex
	 * edge between them is active, so a box pushed into the wall gets a
	 * sideways normal. */
	static const gfloat v[] = {
		-2, 0, -2,  1, 0, -2,  1, 0, 2,  -2, 0, 2,
		1, 2, -2,   1, 2, 2
	};
	static const guint idx[] = { 0, 2, 1,  0, 3, 2,  1, 2, 5,  1, 5, 4 };
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgPhysicsShape) mesh = NULL;
	g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.25f, 0.25f, 0.25f);
	g_autoptr(GArray) all = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	gfloat id[16];
	gfloat xb[16];
	guint i;
	gboolean side;

	mesh = lrg_physics_shape_new_triangle_mesh (v, G_N_ELEMENTS (v), idx, G_N_ELEMENTS (idx), &error);
	g_assert_no_error (error);
	make_pos (id, 0, 0, 0);
	/* Inside corner: a box at the wall's base, sunk into both. */
	make_pos (xb, 1.0f - 0.24f, 0.24f, 0.0f);
	lrg_collide_shapes_all (cube, xb, mesh, id, 0.0f, all);
	side = FALSE;
	for (i = 0; i < all->len; i++)
	{
		const LrgContactManifold *m = &g_array_index (all, LrgContactManifold, i);

		if (m->normal[0] > 0.99f)
			side = TRUE;
	}
	g_assert_true (side);
	g_assert_cmpuint (all->len, ==, 2);
}

/* ==========================================================================
 * Planes and compounds
 * ========================================================================== */

static void
test_plane_and_compound (void)
{
	gfloat up[3] = { 0, 1, 0 };
	g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (up, 1.0f);
	g_autoptr(LrgPhysicsShape) cube = lrg_physics_shape_new_box (0.5f, 0.5f, 0.5f);
	g_autoptr(LrgPhysicsShape) ball = lrg_physics_shape_new_sphere (0.25f);
	g_autoptr(LrgPhysicsShape) comp = lrg_physics_shape_new_compound ();
	g_autoptr(GArray) all = g_array_new (FALSE, FALSE, sizeof (LrgContactManifold));
	LrgContactManifold m;
	gfloat id[16];
	gfloat xb[16];
	gfloat p0[3] = { -1.0f, 0.0f, 0.0f };
	gfloat p1[3] = { 1.0f, 0.0f, 0.0f };

	make_pos (id, 0, 0, 0);
	/* Box on a plane at y = 1: 4 points. */
	make_pos (xb, 3.0f, 1.48f, 0.0f);
	g_assert_true (lrg_collide_shapes (plane, id, cube, xb, &m, 0.0f));
	g_assert_cmpuint (m.n_points, ==, 4);
	g_assert_true (NEAR (m.normal[1], 1.0, 1e-6));
	g_assert_true (NEAR (m.depths[0], 0.02, 1e-5));

	/* Dumbbell: two spheres on a plane -> two manifolds with child ids. */
	lrg_physics_shape_compound_add_child (comp, ball, p0, NULL);
	lrg_physics_shape_compound_add_child (comp, ball, p1, NULL);
	make_pos (xb, 0.0f, 1.2f, 0.0f);
	g_assert_cmpuint (lrg_collide_shapes_all (comp, xb, plane, id, 0.0f, all), ==, 2);
	{
		const LrgContactManifold *a = &g_array_index (all, LrgContactManifold, 0);
		const LrgContactManifold *b = &g_array_index (all, LrgContactManifold, 1);

		g_assert_cmpint (a->child_a + b->child_a, ==, 1);
		g_assert_cmpint (a->child_b, ==, -1);
		g_assert_true (NEAR (a->depths[0], 0.05, 1e-5));
		g_assert_true (NEAR (a->normal[1], -1.0, 1e-6));
	}
	g_assert_true (lrg_collide_overlap (comp, xb, plane, id));
	make_pos (xb, 0.0f, 1.3f, 0.0f);
	g_assert_false (lrg_collide_overlap (comp, xb, plane, id));
	g_assert_true (lrg_collide_overlap (cube, id, ball, id));
}

/* ==========================================================================
 * Ray casts
 * ========================================================================== */

static void
test_rays (void)
{
	g_autoptr(LrgPhysicsShape) sphere = lrg_physics_shape_new_sphere (1.0f);
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (1.0f, 2.0f, 3.0f);
	g_autoptr(LrgPhysicsShape) cap = lrg_physics_shape_new_capsule (0.5f, 1.0f);
	g_autoptr(LrgPhysicsShape) cyl = lrg_physics_shape_new_cylinder (0.5f, 1.0f);
	g_autoptr(LrgPhysicsShape) hull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.0f, NULL);
	g_autoptr(LrgPhysicsShape) rhull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.2f, NULL);
	g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
	gfloat up[3] = { 0, 1, 0 };
	g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (up, -1.0f);
	LrgRayHit hit;
	gfloat o[3];
	gfloat d[3];

	/* Sphere: from x = -5 along +x hits at t = 4 with normal -x. */
	o[0] = -5; o[1] = 0; o[2] = 0;
	d[0] = 1; d[1] = 0; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (sphere, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 4.0, 1e-5));
	g_assert_true (NEAR (hit.normal[0], -1.0, 1e-5));
	g_assert_false (lrg_physics_shape_cast_ray (sphere, o, d, 3.0f, &hit));
	/* Scaled direction: fraction in units of dir. */
	d[0] = 2;
	g_assert_true (lrg_physics_shape_cast_ray (sphere, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 2.0, 1e-5));
	d[0] = 1;
	/* Off-centre: chord at y = 0.6 enters at x = -0.8. */
	o[1] = 0.6f;
	g_assert_true (lrg_physics_shape_cast_ray (sphere, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.point[0], -0.8, 1e-5));
	o[1] = 0.0f;

	/* Inside: t = 0. */
	o[0] = 0.2f;
	g_assert_true (lrg_physics_shape_cast_ray (sphere, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 0.0, 1e-9));

	/* Box: down from above onto y = 2 face. */
	o[0] = 0.3f; o[1] = 10; o[2] = -2.5f;
	d[0] = 0; d[1] = -1; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (box, o, d, 100.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 8.0, 1e-5));
	g_assert_true (NEAR (hit.normal[1], 1.0, 1e-6));
	o[2] = -3.5f;
	g_assert_false (lrg_physics_shape_cast_ray (box, o, d, 100.0f, &hit));

	/* Capsule: side and cap. */
	o[0] = -5; o[1] = 0.5f; o[2] = 0;
	d[0] = 1; d[1] = 0; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (cap, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 4.5, 1e-5));
	o[0] = 0.0f; o[1] = 5; o[2] = 0;
	d[0] = 0; d[1] = -1; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (cap, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 3.5, 1e-5));
	g_assert_true (NEAR (hit.normal[1], 1.0, 1e-5));

	/* Cylinder: flat cap and side. */
	o[0] = 0.4f;
	g_assert_true (lrg_physics_shape_cast_ray (cyl, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 4.0, 1e-5));
	o[0] = -5; o[1] = 0.9f; o[2] = 0;
	d[0] = 1; d[1] = 0; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (cyl, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 4.5, 1e-5));

	/* Hull (cube), sharp and rounded. */
	o[0] = -5; o[1] = 0.1f; o[2] = 0.2f;
	g_assert_true (lrg_physics_shape_cast_ray (hull, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 4.0, 1e-5));
	g_assert_true (NEAR (hit.normal[0], -1.0, 1e-5));
	g_assert_true (lrg_physics_shape_cast_ray (rhull, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 3.8, 1e-4));
	/* Rounded corner: along the diagonal the surface is at sqrt3 + 0.2. */
	{
		gfloat dd[3] = { 1, 1, 1 };
		gfloat oo[3] = { -5, -5, -5 };
		gdouble expect = (5.0 * sqrt (3.0) - sqrt (3.0) - 0.2) / sqrt (3.0);

		g_assert_true (lrg_physics_shape_cast_ray (rhull, oo, dd, 10.0f, &hit));
		g_assert_true (NEAR (hit.fraction, expect, 1e-4));
	}

	/* Mesh: down onto the quad, both triangles, with materials. */
	o[0] = 1.0f; o[1] = 3.0f; o[2] = -1.5f;
	d[0] = 0; d[1] = -1; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (quad, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 3.0, 1e-5));
	g_assert_true (NEAR (hit.normal[1], 1.0, 1e-5));
	g_assert_cmpuint (hit.material, ==, 7);
	o[0] = -1.0f; o[2] = 1.5f;
	g_assert_true (lrg_physics_shape_cast_ray (quad, o, d, 10.0f, &hit));
	g_assert_cmpuint (hit.material, ==, 9);
	g_assert_cmpuint (hit.sub_shape, ==, 1);
	/* From below: two-sided, normal faces the ray. */
	o[1] = -3.0f;
	d[1] = 1.0f;
	g_assert_true (lrg_physics_shape_cast_ray (quad, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.normal[1], -1.0, 1e-5));

	/* Heightfield: slope y = x * 0.5 sampled on a 6x6 grid. */
	{
		gfloat h[36];
		g_autoptr(LrgPhysicsShape) hf = NULL;
		guint i;
		gfloat oo[3] = { -1.0f, 10.0f, 2.5f };
		gfloat dd[3] = { 1.0f, -2.0f, 0.1f };
		gdouble t;

		for (i = 0; i < 36; i++)
			h[i] = (gfloat)(i % 6) * 0.5f;
		hf = lrg_physics_shape_new_heightfield (h, 36, 6, 6, 1.0f, 1.0f, 1.0f, NULL);
		/* Solve 10 - 2t = 0.5 (-1 + t) -> t = 10.5 / 2.5. */
		t = 10.5 / 2.5;
		g_assert_true (lrg_physics_shape_cast_ray (hf, oo, dd, 100.0f, &hit));
		g_assert_true (NEAR (hit.fraction, t, 1e-4));
		g_assert_true (NEAR (hit.normal[0], -0.5 / sqrt (1.25), 1e-4));
		/* Long horizontal ray just under the slope's high side. */
		oo[0] = 6.0f; oo[1] = 2.25f; oo[2] = 1.3f;
		dd[0] = -1.0f; dd[1] = 0.0f; dd[2] = 0.0f;
		g_assert_true (lrg_physics_shape_cast_ray (hf, oo, dd, 100.0f, &hit));
		g_assert_true (NEAR (hit.point[0], 4.5, 1e-4));
	}

	/* Plane at y = -1. */
	o[0] = 0; o[1] = 4; o[2] = 0;
	d[0] = 0; d[1] = -2; d[2] = 0;
	g_assert_true (lrg_physics_shape_cast_ray (plane, o, d, 10.0f, &hit));
	g_assert_true (NEAR (hit.fraction, 2.5, 1e-6));
	d[1] = 2;
	g_assert_false (lrg_physics_shape_cast_ray (plane, o, d, 10.0f, &hit));

	/* World ray through a rotated, translated box. */
	{
		gfloat xf[16];
		gfloat wo[3] = { 10.0f, 0.0f, 0.0f };
		gfloat wd[3] = { -1.0f, 0.0f, 0.0f };

		make_xf (xf, 0, 0, 0, AX_Z, (gfloat)G_PI_2);
		g_assert_true (lrg_collide_ray (box, xf, wo, wd, 100.0f, &hit));
		/* Rotated 90 about Z: the box's Y half extent 2 now spans X. */
		g_assert_true (NEAR (hit.fraction, 8.0, 1e-4));
		g_assert_true (NEAR (hit.normal[0], 1.0, 1e-4));
		g_assert_true (NEAR (hit.point[0], 2.0, 1e-4));
	}

	/* Compound: child index. */
	{
		g_autoptr(LrgPhysicsShape) comp = lrg_physics_shape_new_compound ();
		gfloat p[3] = { 0.0f, 0.0f, 5.0f };
		gfloat oo[3] = { -5.0f, 0.0f, 5.0f };
		gfloat dd[3] = { 1.0f, 0.0f, 0.0f };

		lrg_physics_shape_compound_add_child (comp, sphere, p, NULL);
		g_assert_true (lrg_physics_shape_cast_ray (comp, oo, dd, 10.0f, &hit));
		g_assert_cmpint (hit.child, ==, 0);
		g_assert_true (NEAR (hit.fraction, 4.0, 1e-5));
		g_assert_true (NEAR (hit.point[2], 5.0, 1e-5));
	}
}

/* ==========================================================================
 * Shape casts
 * ========================================================================== */

static void
test_cast (void)
{
	g_autoptr(LrgPhysicsShape) s = lrg_physics_shape_new_sphere (0.5f);
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (0.5f, 0.5f, 0.5f);
	g_autoptr(LrgPhysicsShape) wall = lrg_physics_shape_new_box (0.05f, 2.0f, 2.0f);
	g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
	gfloat up[3] = { 0, 1, 0 };
	g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (up, 0.0f);
	gfloat from[16];
	gfloat to[16];
	gfloat tx[16];
	gfloat toi;
	LrgRayHit hit;

	/* Bullet sphere through a thin wall: first touch at x = 5 - 0.55. */
	make_pos (from, -5, 0, 0);
	make_pos (to, 15, 0, 0);
	make_pos (tx, 5, 0, 0);
	g_assert_true (lrg_collide_cast (s, from, to, wall, tx, &toi, &hit));
	g_assert_true (NEAR (-5.0 + 20.0 * toi, 4.45, 2e-3));
	g_assert_true (NEAR (hit.normal[0], -1.0, 1e-3));
	/* Missing to the side. */
	make_pos (to, 15, 0, 5);
	make_pos (from, -5, 0, 5);
	g_assert_false (lrg_collide_cast (s, from, to, wall, tx, &toi, &hit));

	/* Box falling onto a plane, rotating a little on the way. */
	make_pos (from, 0, 5, 0);
	make_xf (to, 0, -5, 0, AX_Y, 0.4f);
	make_pos (tx, 0, 0, 0);
	g_assert_true (lrg_collide_cast (box, from, to, plane, tx, &toi, &hit));
	g_assert_true (NEAR (5.0 - 10.0 * toi, 0.5, 2e-3));
	g_assert_true (NEAR (hit.normal[1], 1.0, 1e-5));

	/* Sphere onto the triangle mesh. */
	make_pos (from, 0.5f, 3, 0.3f);
	make_pos (to, 0.5f, -3, 0.3f);
	g_assert_true (lrg_collide_cast (s, from, to, quad, tx, &toi, &hit));
	g_assert_true (NEAR (3.0 - 6.0 * toi, 0.5, 2e-3));
	g_assert_cmpuint (hit.sub_shape, !=, LRG_PHYSICS_NO_SUB_SHAPE);

	/* Already overlapping: toi 0. */
	make_pos (from, 5, 0, 0);
	make_pos (to, 6, 0, 0);
	make_pos (tx, 5, 0, 0);
	g_assert_true (lrg_collide_cast (s, from, to, wall, tx, &toi, &hit));
	g_assert_true (NEAR (toi, 0.0, 1e-9));

	/* Rotation only: a long bar spinning into a sphere. */
	{
		g_autoptr(LrgPhysicsShape) bar = lrg_physics_shape_new_box (2.0f, 0.1f, 0.1f);
		gfloat sp[16];

		make_pos (from, 0, 0, 0);
		make_xf (to, 0, 0, 0, AX_Y, (gfloat)G_PI_2);
		make_pos (sp, 0, 0, 1.5f);
		g_assert_true (lrg_collide_cast (bar, from, to, s, sp, &toi, &hit));
		g_assert_cmpfloat (toi, >, 0.1f);
		g_assert_cmpfloat (toi, <, 0.9f);
	}
}

/* ==========================================================================
 * Bounds, debug mesh, determinism, performance
 * ========================================================================== */

static void
test_bounds_debug (void)
{
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (1.0f, 2.0f, 3.0f);
	g_autoptr(LrgPhysicsShape) cap = lrg_physics_shape_new_capsule (0.5f, 1.0f);
	gfloat xf[16];
	gfloat mn[3];
	gfloat mx[3];
	gfloat sup[3];
	gfloat dir[3] = { 1.0f, 1.0f, -1.0f };
	LrgPhysicsShapeKind k;

	make_xf (xf, 1, 2, 3, AX_Z, (gfloat)G_PI_2);
	lrg_physics_shape_get_world_bounds (box, xf, mn, mx);
	g_assert_true (NEAR (mn[0], 1.0 - 2.0, 1e-5) && NEAR (mx[0], 1.0 + 2.0, 1e-5));
	g_assert_true (NEAR (mn[1], 2.0 - 1.0, 1e-5) && NEAR (mx[2], 3.0 + 3.0, 1e-5));
	lrg_physics_shape_get_world_bounds (cap, xf, mn, mx);
	g_assert_true (NEAR (mx[0] - mn[0], 3.0, 1e-5));
	g_assert_true (NEAR (mx[1] - mn[1], 1.0, 1e-5));
	lrg_physics_shape_get_local_bounds (cap, mn, mx);
	g_assert_true (NEAR (mx[1], 1.5, 1e-6));
	g_assert_true (lrg_physics_shape_support (box, dir, sup));
	g_assert_true (sup[0] == 1.0f && sup[1] == 2.0f && sup[2] == -3.0f);
	g_assert_true (NEAR (lrg_physics_shape_get_inner_radius (box), 1.0, 1e-6));
	k = lrg_physics_shape_get_kind (cap);
	g_assert_cmpint (k, ==, LRG_PHYSICS_SHAPE_KIND_CAPSULE);
	g_assert_true (lrg_physics_shape_is_convex (cap));
	g_assert_true (NEAR (lrg_physics_shape_get_radius (cap), 0.5, 1e-6));
	g_assert_true (NEAR (lrg_physics_shape_get_half_height (cap), 1.0, 1e-6));
	g_assert_true (g_type_is_a (LRG_TYPE_PHYSICS_SHAPE_KIND, G_TYPE_ENUM));
	g_assert_true (g_type_is_a (LRG_TYPE_PHYSICS_SHAPE_ERROR, G_TYPE_ENUM));
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_RAY_HIT));
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_CONTACT_MANIFOLD));
	{
		g_autoptr(LrgRayHit) h = lrg_ray_hit_new ();
		g_autoptr(LrgRayHit) h2 = lrg_ray_hit_copy (h);
		g_autoptr(LrgContactManifold) m = lrg_contact_manifold_new ();
		g_autoptr(LrgContactManifold) m2 = lrg_contact_manifold_copy (m);

		g_assert_cmpint (h2->child, ==, -1);
		g_assert_cmpuint (h2->body, ==, LRG_PHYSICS_NO_BODY);
		g_assert_cmpint (m2->child_a, ==, -1);
		g_assert_false (lrg_contact_manifold_get_point (m2, 0, NULL, NULL, NULL, NULL));
	}
	/* Debug meshes: non-empty triangle soups for every kind. */
	{
		gfloat up[3] = { 0, 1, 0 };
		g_autoptr(LrgPhysicsShape) plane = lrg_physics_shape_new_plane (up, 0.0f);
		g_autoptr(LrgPhysicsShape) hull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.1f, NULL);
		g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
		g_autoptr(LrgPhysicsShape) cyl = lrg_physics_shape_new_cylinder (1.0f, 1.0f);
		g_autoptr(LrgPhysicsShape) comp = lrg_physics_shape_new_compound ();
		LrgPhysicsShape *all[7];
		guint i;
		gfloat p[3] = { 1, 0, 0 };
		gfloat n[3];
		gfloat d;

		lrg_physics_shape_compound_add_child (comp, box, p, NULL);
		all[0] = plane; all[1] = hull; all[2] = quad; all[3] = cyl;
		all[4] = comp; all[5] = box; all[6] = cap;
		for (i = 0; i < 7; i++)
		{
			g_autoptr(GArray) tri = lrg_physics_shape_build_debug_mesh (all[i], 12);

			g_assert_cmpuint (tri->len, >, 0);
			g_assert_cmpuint (tri->len % 9, ==, 0);
		}
		g_assert_true (lrg_physics_shape_get_plane (plane, n, &d));
		g_assert_true (NEAR (n[1], 1.0, 1e-6));
		g_assert_false (lrg_physics_shape_get_plane (box, n, &d));
	}
	{
		gfloat he[3];

		lrg_physics_shape_get_half_extents (box, he);
		g_assert_true (he[2] == 3.0f);
	}
}

static void
run_scene (GArray *out)
{
	g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
	g_autoptr(LrgPhysicsShape) hull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.05f, NULL);
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (0.3f, 0.2f, 0.4f);
	gfloat id[16];
	guint i;

	make_pos (id, 0, 0, 0);
	for (i = 0; i < 50; i++)
	{
		gfloat xa[16];
		gfloat xb[16];

		make_xf (xa, (gfloat)i * 0.05f - 1.0f, 0.15f, 0.2f, AX_Y, (gfloat)i * 0.1f);
		lrg_collide_shapes_all (box, xa, quad, id, 0.02f, out);
		make_xf (xb, 1.9f, (gfloat)i * 0.01f, 0.3f, AX_X, (gfloat)i * 0.07f);
		lrg_collide_shapes_all (hull, id, hull, xb, 0.02f, out);
	}
}

static void
test_determinism (void)
{
	g_autoptr(GArray) a = g_array_new (FALSE, TRUE, sizeof (LrgContactManifold));
	g_autoptr(GArray) b = g_array_new (FALSE, TRUE, sizeof (LrgContactManifold));

	run_scene (a);
	run_scene (b);
	g_assert_cmpuint (a->len, ==, b->len);
	g_assert_cmpuint (a->len, >, 50);
	g_assert_cmpint (memcmp (a->data, b->data, a->len * sizeof (LrgContactManifold)), ==, 0);
}

/* Times @n calls of collide on a pair; returns calls per microsecond. */
static gdouble
bench_pair (LrgPhysicsShape *a,
            LrgPhysicsShape *b,
            gfloat           dy,
            guint            n)
{
	gfloat xa[16];
	gfloat xb[16];
	LrgContactManifold m;
	gint64 t0;
	gint64 t1;
	guint i;
	guint hits;

	make_pos (xa, 0, 0, 0);
	hits = 0;
	t0 = g_get_monotonic_time ();
	for (i = 0; i < n; i++)
	{
		make_xf (xb, 0.01f * (gfloat)(i % 7), dy, 0.0f, AX_Y, 0.001f * (gfloat)(i % 13));
		if (lrg_collide_shapes (a, xa, b, xb, &m, 0.02f))
			hits++;
	}
	t1 = g_get_monotonic_time ();
	g_assert_cmpuint (hits, ==, n);
	return (gdouble)n / (gdouble)MAX (t1 - t0, 1);
}

static void
test_perf (void)
{
	g_autoptr(LrgPhysicsShape) s = lrg_physics_shape_new_sphere (0.5f);
	g_autoptr(LrgPhysicsShape) box = lrg_physics_shape_new_box (0.5f, 0.5f, 0.5f);
	g_autoptr(LrgPhysicsShape) cap = lrg_physics_shape_new_capsule (0.3f, 0.5f);
	g_autoptr(LrgPhysicsShape) hull = lrg_physics_shape_new_convex_hull (cube_pts, 24, 0.0f, NULL);
	g_autoptr(LrgPhysicsShape) quad = make_quad_mesh ();
	guint n;

	n = g_test_perf () ? 200000 : 20000;
	g_test_message ("sphere-sphere   %.2f calls/us", bench_pair (s, s, 0.95f, n));
	g_test_message ("box-box         %.2f calls/us", bench_pair (box, box, 0.99f, n));
	g_test_message ("capsule-box     %.2f calls/us", bench_pair (box, cap, 1.29f, n));
	g_test_message ("hull-hull (EPA) %.2f calls/us", bench_pair (hull, hull, 1.95f, n));
	g_test_message ("mesh-box        %.2f calls/us", bench_pair (quad, box, 0.49f, n));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/physics-shape/mass/primitives", test_mass_primitives);
	g_test_add_func ("/physics-shape/mass/hull-cube", test_mass_hull_cube);
	g_test_add_func ("/physics-shape/mass/compound", test_mass_compound);
	g_test_add_func ("/physics-shape/hull/random", test_quickhull_random);
	g_test_add_func ("/physics-shape/hull/degenerate", test_quickhull_degenerate);
	g_test_add_func ("/physics-shape/bounds-debug", test_bounds_debug);
	g_test_add_func ("/physics-shape/rays", test_rays);
	g_test_add_func ("/collide/gjk-distance", test_gjk_distance);
	g_test_add_func ("/collide/epa-depth", test_epa_depth);
	g_test_add_func ("/collide/random-consistency", test_random_consistency);
	g_test_add_func ("/collide/manifold/box-resting", test_manifold_box_resting);
	g_test_add_func ("/collide/manifold/edge-edge", test_manifold_edge_edge);
	g_test_add_func ("/collide/manifold/capsule-sphere", test_manifold_capsule_sphere);
	g_test_add_func ("/collide/manifold/feature-ids", test_manifold_feature_ids);
	g_test_add_func ("/collide/mesh/active-edges", test_mesh_active_edges);
	g_test_add_func ("/collide/mesh/no-ghost", test_mesh_no_ghost);
	g_test_add_func ("/collide/mesh/wall", test_mesh_wall_keeps_normal);
	g_test_add_func ("/collide/heightfield", test_heightfield);
	g_test_add_func ("/collide/plane-compound", test_plane_and_compound);
	g_test_add_func ("/collide/cast", test_cast);
	g_test_add_func ("/collide/determinism", test_determinism);
	g_test_add_func ("/collide/perf", test_perf);

	return g_test_run ();
}
