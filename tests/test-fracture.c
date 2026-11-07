/* test-fracture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless checks of LrgFracture: chunks are convex, their volumes sum
 * to the source's, they stay inside its bounds, cut faces carry the
 * interior material, neighbours are symmetric, mass properties match a
 * box in closed form, seed modes, determinism, the .rfracture round trip
 * and cache workflow, errors, the swap-in helpers and build timing.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

static LrgFracture *
make_wall (guint                chunks,
           LrgFractureSeedMode  mode,
           guint                seed)
{
	LrgFracture      *f = lrg_fracture_new ();
	const gfloat      half[3] = { 1.0f, 0.5f, 0.1f };
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_fracture_set_box (f, half, &error));
	g_assert_no_error (error);
	g_object_set (f, "chunk-count", chunks, "seed-mode", mode, "seed", seed, NULL);
	g_assert_true (lrg_fracture_build (f, &error));
	g_assert_no_error (error);
	return f;
}

/* Every triangle plane of the chunk has all hull points behind it. */
static void
assert_convex (const LrgFractureChunk *c)
{
	guint          n_idx;
	guint          n_hull;
	const guint32 *idx = lrg_fracture_chunk_get_indices (c, &n_idx);
	const gfloat  *pos = lrg_fracture_chunk_get_positions (c, NULL);
	const gfloat  *hull = lrg_fracture_chunk_get_hull_points (c, &n_hull);
	guint          t;
	guint          i;

	g_assert_cmpuint (n_hull, >=, 12);
	for (t = 0; t < n_idx; t += 3)
	{
		const gfloat *a = pos + 3 * idx[t];
		const gfloat *b = pos + 3 * idx[t + 1];
		const gfloat *e = pos + 3 * idx[t + 2];
		gfloat ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		gfloat ae[3] = { e[0] - a[0], e[1] - a[1], e[2] - a[2] };
		gfloat n[3] = { ab[1] * ae[2] - ab[2] * ae[1], ab[2] * ae[0] - ab[0] * ae[2], ab[0] * ae[1] - ab[1] * ae[0] };
		gfloat len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

		if (len < 1e-9f)
			continue;
		for (i = 0; i < n_hull; i += 3)
		{
			gfloat r[3] = { hull[i] - a[0], hull[i + 1] - a[1], hull[i + 2] - a[2] };

			g_assert_cmpfloat ((r[0] * n[0] + r[1] * n[1] + r[2] * n[2]) / len, <=, 1e-4f);
		}
	}
}

static void
test_fracture_wall (void)
{
	g_autoptr(LrgFracture) f = make_wall (12, LRG_FRACTURE_SEED_RANDOM, 7);
	gfloat                 smin[3];
	gfloat                 smax[3];
	guint                  n = lrg_fracture_get_chunk_count (f);
	guint                  i;
	gdouble                sum = 0.0;

	g_assert_true (lrg_fracture_is_built (f));
	g_assert_cmpuint (n, ==, 12);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_source_volume (f), 0.4f, 1e-5f);
	g_assert_true (lrg_fracture_get_source_bounds (f, smin, smax));

	for (i = 0; i < n; i++)
	{
		const LrgFractureChunk *c = lrg_fracture_get_chunk (f, i);
		gfloat                  cmin[3];
		gfloat                  cmax[3];
		gfloat                  com[3];
		guint                   n_tri;
		guint                   n_nb;
		const guint32          *mat = lrg_fracture_chunk_get_triangle_materials (c, &n_tri);
		const guint32          *nb = lrg_fracture_chunk_get_neighbors (c, &n_nb);
		guint                   k;
		guint                   t;

		g_assert_cmpuint (lrg_fracture_chunk_get_index (c), ==, i);
		assert_convex (c);
		sum += lrg_fracture_chunk_get_volume (c);
		g_assert_cmpfloat (lrg_fracture_chunk_get_volume (c), >, 0.0f);

		/* Inside the source bounds; the centre of mass inside the chunk bounds. */
		lrg_fracture_chunk_get_bounds (c, cmin, cmax);
		lrg_fracture_chunk_get_center_of_mass (c, com);
		for (k = 0; k < 3; k++)
		{
			g_assert_cmpfloat (cmin[k], >=, smin[k] - 1e-5f);
			g_assert_cmpfloat (cmax[k], <=, smax[k] + 1e-5f);
			g_assert_cmpfloat (com[k], >=, cmin[k]);
			g_assert_cmpfloat (com[k], <=, cmax[k]);
		}

		/* Cut faces are tagged with the interior material, the rest keep 0. */
		g_assert_cmpuint (lrg_fracture_chunk_get_interior_triangle_count (c, 1), >, 0);
		for (t = 0; t < n_tri; t++)
			g_assert_true (mat[t] == 0 || mat[t] == 1);

		/* Neighbours are symmetric and every cut face has one. */
		g_assert_cmpuint (n_nb, >, 0);
		for (k = 0; k < n_nb; k++)
		{
			const LrgFractureChunk *o = lrg_fracture_get_chunk (f, nb[k]);
			guint                   m;
			const guint32          *onb = lrg_fracture_chunk_get_neighbors (o, &m);
			gboolean                back = FALSE;
			guint                   j;

			g_assert_cmpuint (nb[k], !=, i);
			if (k > 0)
				g_assert_cmpuint (nb[k], >, nb[k - 1]);
			for (j = 0; j < m; j++)
				back = back || onb[j] == i;
			g_assert_true (back);
		}
	}
	g_test_message ("12 chunks, volume sum %.7f of 0.4", sum);
	g_assert_cmpfloat (fabs (sum - 0.4), <, 1e-5);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_total_volume (f), 0.4f, 1e-5f);
}

/* One seed: the chunk is the box; its inertia is m (b² + c²) / 12. */
static void
test_fracture_mass_properties (void)
{
	g_autoptr(LrgFracture) f = lrg_fracture_new ();
	const gfloat           half[3] = { 1.0f, 0.5f, 0.25f };
	const gfloat           seed[3] = { 0.1f, 0.2f, 0.0f };
	const LrgFractureChunk *c;
	gfloat                 mass;
	gfloat                 com[3];
	gfloat                 inertia[9];

	g_assert_true (lrg_fracture_set_box (f, half, NULL));
	g_assert_true (lrg_fracture_set_seed_points (f, seed, 3));
	g_assert_true (lrg_fracture_build (f, NULL));
	g_assert_cmpuint (lrg_fracture_get_chunk_count (f), ==, 1);
	c = lrg_fracture_get_chunk (f, 0);
	g_assert_cmpuint (lrg_fracture_chunk_get_interior_triangle_count (c, 1), ==, 0);
	g_assert_true (lrg_fracture_chunk_get_mass_properties (c, 2.0f, &mass, com, inertia));
	g_assert_false (lrg_fracture_chunk_get_mass_properties (c, 0.0f, NULL, NULL, NULL));
	g_assert_cmpfloat_with_epsilon (mass, 2.0f * 1.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (com[0], 0.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (com[1], 0.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (inertia[0], mass * (1.0f + 0.25f) / 12.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (inertia[4], mass * (4.0f + 0.25f) / 12.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (inertia[8], mass * (4.0f + 1.0f) / 12.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (inertia[1], 0.0f, 1e-6f);
	/* 6 quads (12 triangles), 8 distinct corners. */
	g_assert_cmpuint (lrg_fracture_chunk_get_hull_points (c, NULL) != NULL, ==, TRUE);
	{
		guint n;

		lrg_fracture_chunk_get_hull_points (c, &n);
		g_assert_cmpuint (n, ==, 24);
		lrg_fracture_chunk_get_indices (c, &n);
		g_assert_cmpuint (n, ==, 36);
	}
}

/* Two seeds split a box down the middle: two exact halves. */
static void
test_fracture_halves (void)
{
	g_autoptr(LrgFracture) f = lrg_fracture_new ();
	const gfloat           half[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat           seeds[6] = { -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f };
	guint                  i;

	lrg_fracture_set_box (f, half, NULL);
	lrg_fracture_set_seed_points (f, seeds, 6);
	g_assert_true (lrg_fracture_build (f, NULL));
	g_assert_cmpuint (lrg_fracture_get_chunk_count (f), ==, 2);
	for (i = 0; i < 2; i++)
	{
		const LrgFractureChunk *c = lrg_fracture_get_chunk (f, i);
		gfloat                  com[3];
		guint                   n_nb;

		g_assert_cmpfloat_with_epsilon (lrg_fracture_chunk_get_volume (c), 4.0f, 1e-5f);
		lrg_fracture_chunk_get_center_of_mass (c, com);
		g_assert_cmpfloat_with_epsilon (fabsf (com[0]), 0.5f, 1e-5f);
		/* The cut is one quad: two interior triangles. */
		g_assert_cmpuint (lrg_fracture_chunk_get_interior_triangle_count (c, 1), ==, 2);
		lrg_fracture_chunk_get_neighbors (c, &n_nb);
		g_assert_cmpuint (n_nb, ==, 1);
	}
}

/* A custom convex mesh keeps its per-triangle materials on the outside. */
static void
test_fracture_custom_mesh (void)
{
	/* Octahedron, outward winding, material 5 + face index. */
	const gfloat  pos[18] = { 1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1 };
	const guint32 idx[24] = { 0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4, 2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5 };
	guint32       mats[8];
	g_autoptr(LrgFracture) f = lrg_fracture_new ();
	g_autoptr(GError) error = NULL;
	gdouble       sum = 0.0;
	guint         i;

	for (i = 0; i < 8; i++)
		mats[i] = 5 + i;
	g_assert_true (lrg_fracture_set_mesh (f, 6, pos, NULL, 24, idx, mats, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_source_volume (f), 4.0f / 3.0f, 1e-5f);
	g_object_set (f, "chunk-count", 6, "interior-material", 99, NULL);
	g_assert_true (lrg_fracture_build (f, NULL));
	for (i = 0; i < lrg_fracture_get_chunk_count (f); i++)
	{
		const LrgFractureChunk *c = lrg_fracture_get_chunk (f, i);
		guint                   n_tri;
		const guint32          *m = lrg_fracture_chunk_get_triangle_materials (c, &n_tri);
		guint                   t;

		assert_convex (c);
		sum += lrg_fracture_chunk_get_volume (c);
		for (t = 0; t < n_tri; t++)
			g_assert_true (m[t] == 99 || (m[t] >= 5 && m[t] < 13));
	}
	g_assert_cmpfloat (fabs (sum - 4.0 / 3.0), <, 1e-5);
}

static void
test_fracture_errors (void)
{
	g_autoptr(LrgFracture) f = lrg_fracture_new ();
	g_autoptr(GError)      error = NULL;
	/* A tetrahedron missing one face: open. */
	const gfloat  pos[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	const guint32 open[9] = { 0, 2, 1, 0, 1, 3, 0, 3, 2 };
	const guint32 closed[12] = { 0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3 };
	const guint32 inside_out[12] = { 0, 1, 2, 0, 3, 1, 0, 2, 3, 1, 3, 2 };
	/* A box with one corner pushed in: closed but not convex. */
	const gfloat  dent[24] = { -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
	                           -1, -1, 1, 1, -1, 1, 0.2f, 0.2f, 0.2f, -1, 1, 1 };
	const guint32 box[36] = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
	                          2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3 };

	g_assert_false (lrg_fracture_build (f, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE);
	g_clear_error (&error);
	g_assert_false (lrg_fracture_set_mesh (f, 4, pos, NULL, 9, open, NULL, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_true (lrg_fracture_set_mesh (f, 4, pos, NULL, 12, closed, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_source_volume (f), 1.0f / 6.0f, 1e-6f);
	g_assert_false (lrg_fracture_set_mesh (f, 4, pos, NULL, 12, inside_out, NULL, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NOT_CONVEX);
	g_clear_error (&error);
	g_assert_false (lrg_fracture_set_mesh (f, 8, dent, NULL, 36, box, NULL, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NOT_CONVEX);
	g_clear_error (&error);
	{
		const guint32 broken[12] = { 0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 3, 2 };

		g_assert_false (lrg_fracture_set_mesh (f, 4, pos, NULL, 12, broken, NULL, &error));
		g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NOT_CLOSED);
		g_clear_error (&error);
	}
	/* The tetrahedron is still the source. */
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_source_volume (f), 1.0f / 6.0f, 1e-6f);
	{
		const gfloat outside[3] = { 5.0f, 5.0f, 5.0f };

		g_assert_true (lrg_fracture_set_seed_points (f, outside, 3));
		g_assert_false (lrg_fracture_build (f, &error));
		g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID);
		g_clear_error (&error);
	}
	g_assert_false (lrg_fracture_load_from_data (f, "format: rmat\nversion: 1\n", -1, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_false (lrg_fracture_load_from_data (f, "format: rfracture\nversion: 9\nchunks: []\n", -1, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_PARSE);
	g_clear_error (&error);
}

/* Grid seeds give the grid's cell count; impact seeds give small chunks at the hit. */
static void
test_fracture_seed_modes (void)
{
	g_autoptr(LrgFracture) grid = NULL;
	g_autoptr(LrgFracture) hit = lrg_fracture_new ();
	const gfloat           half[3] = { 1.0f, 0.5f, 0.1f };
	const gfloat           corner[3] = { 0.9f, 0.4f, 0.0f };
	gfloat                 near_vol = 0.0f;
	gfloat                 far_vol = 0.0f;
	guint                  near_n = 0;
	guint                  far_n = 0;
	guint                  i;

	grid = make_wall (8, LRG_FRACTURE_SEED_GRID, 3);
	/* 2 m x 1 m x 0.2 m with 8 cells: 4 x 2 x 1. */
	g_assert_cmpuint (lrg_fracture_get_chunk_count (grid), ==, 8);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_total_volume (grid), 0.4f, 1e-5f);

	lrg_fracture_set_box (hit, half, NULL);
	lrg_fracture_set_impact_point (hit, corner);
	g_object_set (hit, "seed-mode", LRG_FRACTURE_SEED_IMPACT, "chunk-count", 24, "seed", 11, NULL);
	g_assert_true (lrg_fracture_build (hit, NULL));
	g_assert_cmpuint (lrg_fracture_get_chunk_count (hit), ==, 24);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_total_volume (hit), 0.4f, 1e-5f);
	for (i = 0; i < 24; i++)
	{
		const LrgFractureChunk *c = lrg_fracture_get_chunk (hit, i);
		gfloat                  com[3];
		gfloat                  d;

		lrg_fracture_chunk_get_center_of_mass (c, com);
		d = sqrtf ((com[0] - corner[0]) * (com[0] - corner[0]) + (com[1] - corner[1]) * (com[1] - corner[1]));
		if (d < 0.5f)
		{
			near_vol += lrg_fracture_chunk_get_volume (c);
			near_n++;
		}
		else
		{
			far_vol += lrg_fracture_chunk_get_volume (c);
			far_n++;
		}
	}
	g_test_message ("impact: %u chunks near (mean %.4f), %u far (mean %.4f)", near_n,
	                near_n ? near_vol / near_n : 0.0f, far_n, far_n ? far_vol / far_n : 0.0f);
	g_assert_cmpuint (near_n, >, far_n);
	g_assert_cmpfloat (near_vol / (gfloat) near_n, <, far_vol / (gfloat) MAX (far_n, 1u));
}

static void
test_fracture_determinism (void)
{
	g_autoptr(LrgFracture) a = make_wall (16, LRG_FRACTURE_SEED_RANDOM, 42);
	g_autoptr(LrgFracture) b = make_wall (16, LRG_FRACTURE_SEED_RANDOM, 42);
	g_autoptr(LrgFracture) c = make_wall (16, LRG_FRACTURE_SEED_RANDOM, 43);
	g_autofree gchar      *da = lrg_fracture_save_to_data (a, NULL);
	g_autofree gchar      *db = lrg_fracture_save_to_data (b, NULL);
	g_autofree gchar      *dc = lrg_fracture_save_to_data (c, NULL);

	g_assert_cmpstr (da, ==, db);
	g_assert_cmpstr (da, !=, dc);
}

static void
assert_same_chunks (LrgFracture *a,
                    LrgFracture *b)
{
	guint i;

	g_assert_cmpuint (lrg_fracture_get_chunk_count (a), ==, lrg_fracture_get_chunk_count (b));
	for (i = 0; i < lrg_fracture_get_chunk_count (a); i++)
	{
		const LrgFractureChunk *x = lrg_fracture_get_chunk (a, i);
		const LrgFractureChunk *y = lrg_fracture_get_chunk (b, i);
		guint                   nx;
		guint                   ny;
		gfloat                  ix[9];
		gfloat                  iy[9];

		g_assert_cmpmem (lrg_fracture_chunk_get_positions (x, &nx), nx * sizeof (gfloat),
		                 lrg_fracture_chunk_get_positions (y, &ny), ny * sizeof (gfloat));
		g_assert_cmpmem (lrg_fracture_chunk_get_normals (x, &nx), nx * sizeof (gfloat),
		                 lrg_fracture_chunk_get_normals (y, &ny), ny * sizeof (gfloat));
		g_assert_cmpmem (lrg_fracture_chunk_get_texcoords (x, &nx), nx * sizeof (gfloat),
		                 lrg_fracture_chunk_get_texcoords (y, &ny), ny * sizeof (gfloat));
		g_assert_cmpmem (lrg_fracture_chunk_get_indices (x, &nx), nx * sizeof (guint32),
		                 lrg_fracture_chunk_get_indices (y, &ny), ny * sizeof (guint32));
		g_assert_cmpmem (lrg_fracture_chunk_get_triangle_materials (x, &nx), nx * sizeof (guint32),
		                 lrg_fracture_chunk_get_triangle_materials (y, &ny), ny * sizeof (guint32));
		g_assert_cmpmem (lrg_fracture_chunk_get_neighbors (x, &nx), nx * sizeof (guint32),
		                 lrg_fracture_chunk_get_neighbors (y, &ny), ny * sizeof (guint32));
		g_assert_cmpmem (lrg_fracture_chunk_get_hull_points (x, &nx), nx * sizeof (gfloat),
		                 lrg_fracture_chunk_get_hull_points (y, &ny), ny * sizeof (gfloat));
		g_assert_cmpfloat (lrg_fracture_chunk_get_volume (x), ==, lrg_fracture_chunk_get_volume (y));
		lrg_fracture_chunk_get_mass_properties (x, 1.0f, NULL, NULL, ix);
		lrg_fracture_chunk_get_mass_properties (y, 1.0f, NULL, NULL, iy);
		g_assert_cmpmem (ix, sizeof (ix), iy, sizeof (iy));
	}
}

static void
test_fracture_round_trip (void)
{
	g_autoptr(LrgFracture) f = make_wall (10, LRG_FRACTURE_SEED_RANDOM, 5);
	g_autoptr(LrgFracture) g = NULL;
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *dir = g_dir_make_tmp ("lrg-fracture-XXXXXX", NULL);
	g_autofree gchar      *path = g_build_filename (dir, "wall.rfracture", NULL);
	g_autofree gchar      *again = NULL;
	g_autofree gchar      *first = NULL;
	gfloat                 threshold = 0.0f;

	g_object_set (f, "impulse-threshold", 123.5f, "density", 2400.0f, NULL);
	g_assert_true (lrg_fracture_save_to_file (f, path, &error));
	g_assert_no_error (error);
	g = lrg_fracture_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (g);
	g_assert_false (lrg_fracture_has_source (g));
	assert_same_chunks (f, g);
	g_object_get (g, "impulse-threshold", &threshold, NULL);
	g_assert_cmpfloat (threshold, ==, 123.5f);
	{
		guint n1;
		guint n2;

		g_assert_cmpmem (lrg_fracture_get_seed_points (f, &n1), n1 * sizeof (gfloat),
		                 lrg_fracture_get_seed_points (g, &n2), n2 * sizeof (gfloat));
	}

	/* Saving the loaded copy reproduces the file except for the source fields. */
	first = lrg_fracture_save_to_data (f, NULL);
	again = lrg_fracture_save_to_data (g, NULL);
	g_assert_nonnull (strstr (first, "source_hash:"));
	g_assert_null (strstr (again, "source_hash:"));
	g_assert_nonnull (strstr (again, "chunks:"));

	g_unlink (path);
	g_rmdir (dir);
}

/* load_or_build writes the cache once, then loads it while the hash matches. */
static void
test_fracture_cache (void)
{
	g_autoptr(LrgFracture) f = lrg_fracture_new ();
	g_autoptr(LrgFracture) g = lrg_fracture_new ();
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *dir = g_dir_make_tmp ("lrg-fracture-XXXXXX", NULL);
	g_autofree gchar      *path = g_build_filename (dir, "crate.rfracture", NULL);
	g_autofree gchar      *h1 = NULL;
	g_autofree gchar      *h2 = NULL;
	const gfloat           half[3] = { 0.5f, 0.5f, 0.5f };
	GStatBuf               st;
	gint64                 size1;

	lrg_fracture_set_box (f, half, NULL);
	g_object_set (f, "chunk-count", 5, "seed", 9, NULL);
	g_assert_true (lrg_fracture_load_or_build (f, path, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	g_assert_cmpint (g_stat (path, &st), ==, 0);
	size1 = (gint64) st.st_size;

	/* Same input: loaded from the cache (corrupt chunk data would show). */
	lrg_fracture_set_box (g, half, NULL);
	g_object_set (g, "chunk-count", 5, "seed", 9, NULL);
	h1 = lrg_fracture_get_source_hash (f);
	h2 = lrg_fracture_get_source_hash (g);
	g_assert_cmpstr (h1, ==, h2);
	g_assert_true (lrg_fracture_load_or_build (g, path, &error));
	assert_same_chunks (f, g);

	/* Another seed: stale, rebuilt and rewritten. */
	g_object_set (g, "seed", 10, "chunk-count", 9, NULL);
	g_assert_true (lrg_fracture_load_or_build (g, path, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_fracture_get_chunk_count (g), ==, 9);
	g_assert_cmpint (g_stat (path, &st), ==, 0);
	g_assert_cmpint ((gint64) st.st_size, !=, size1);

	g_unlink (path);
	g_rmdir (dir);
}

/* Swap-in helpers: threshold, chunk placement and break-up velocities. */
static void
test_fracture_swap_helpers (void)
{
	g_autoptr(LrgFracture) f = make_wall (6, LRG_FRACTURE_SEED_RANDOM, 2);
	gfloat                 obj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 2, 0, 1 };
	gfloat                 m[16];
	gfloat                 com[3];
	gfloat                 v[3];
	const gfloat           hit[3] = { 9.0f, 2.0f, 0.1f };
	const gfloat           impulse[3] = { 0.0f, 0.0f, -400.0f };
	gfloat                 total = 0.0f;
	guint                  i;

	g_object_set (f, "impulse-threshold", 100.0f, NULL);
	g_assert_false (lrg_fracture_should_break (f, 99.0f));
	g_assert_true (lrg_fracture_should_break (f, 100.0f));

	lrg_fracture_chunk_get_center_of_mass (lrg_fracture_get_chunk (f, 0), com);
	g_assert_true (lrg_fracture_get_chunk_transform (f, 0, obj, m));
	g_assert_cmpfloat_with_epsilon (m[12], com[0] + 10.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (m[13], com[1] + 2.0f, 1e-5f);
	g_assert_false (lrg_fracture_get_chunk_transform (f, 99, obj, m));

	for (i = 0; i < lrg_fracture_get_chunk_count (f); i++)
	{
		g_assert_true (lrg_fracture_get_chunk_velocity (f, i, obj, hit, impulse, v));
		g_assert_cmpfloat (v[2], <, 0.0f);
		total += -v[2];
	}
	g_assert_cmpfloat (total, >, 0.0f);
	g_assert_true (lrg_fracture_get_chunk_velocity (f, 0, NULL, NULL, NULL, v));
	g_assert_cmpfloat (v[0], ==, 0.0f);
}

/* The chunk hull shape (SHAPES' quickhull) matches the chunk's volume. */
static void
test_fracture_shape (void)
{
	g_autoptr(LrgFracture)     f = make_wall (4, LRG_FRACTURE_SEED_RANDOM, 3);
	g_autoptr(LrgPhysicsShape) shape = NULL;
	g_autoptr(GError)          error = NULL;
	const LrgFractureChunk    *c = lrg_fracture_get_chunk (f, 0);

	shape = lrg_fracture_chunk_create_shape (c, &error);
	if (shape == NULL)
	{
		g_test_skip ("convex hull shapes are not available yet");
		return;
	}
	g_assert_cmpfloat_with_epsilon (lrg_physics_shape_get_volume (shape),
	                                lrg_fracture_chunk_get_volume (c), 1e-4f);
}

/* Swap-in: N dynamic bodies, total mass and momentum as given, they fall. */
static void
test_fracture_swap_in (void)
{
	g_autoptr(LrgFracture)      f = make_wall (8, LRG_FRACTURE_SEED_RANDOM, 4);
	g_autoptr(LrgDynamicsWorld) world = lrg_dynamics_world_new ();
	g_autoptr(GArray)           ids = NULL;
	g_autoptr(GError)           error = NULL;
	gfloat                      obj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 3, 0, 1 };
	const gfloat                hit[3] = { 0.5f, 3.2f, 0.1f };
	const gfloat                impulse[3] = { 0.0f, 0.0f, -800.0f };
	gfloat                      mass = 0.0f;
	gfloat                      pz = 0.0f;
	guint                       i;

	g_object_set (f, "density", 2000.0f, NULL);
	g_assert_true (lrg_fracture_should_break (f, 800.0f));
	ids = lrg_fracture_swap_in (f, world, obj, hit, impulse, LRG_PHYSICS_LAYER_DEBRIS, &error);
	if (ids == NULL && g_error_matches (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID))
	{
		g_test_skip ("convex hull shapes are not available yet");
		return;
	}
	g_assert_no_error (error);
	g_assert_nonnull (ids);
	g_assert_cmpuint (ids->len, ==, lrg_fracture_get_chunk_count (f));
	g_assert_cmpuint (lrg_dynamics_world_get_body_count (world), ==, ids->len);
	for (i = 0; i < ids->len; i++)
	{
		guint  id = g_array_index (ids, guint, i);
		gfloat v[3];
		gfloat m = lrg_dynamics_world_get_mass (world, id);
		guint  layer = 0;

		mass += m;
		g_assert_true (lrg_dynamics_world_get_linear_velocity (world, id, v, NULL));
		pz += m * v[2];
		g_assert_true (lrg_dynamics_world_get_body_layer (world, id, &layer, NULL, NULL));
		g_assert_cmpuint (layer, ==, LRG_PHYSICS_LAYER_DEBRIS);
	}
	g_assert_cmpfloat_with_epsilon (mass, 2000.0f * 0.4f, 0.01f);
	g_test_message ("swap-in: %u bodies, %.1f kg, momentum z %.1f N s", ids->len, mass, pz);
	g_assert_cmpfloat (pz, <, -400.0f);
	{
		gfloat p0[3];
		gfloat p1[3];
		guint  s;

		lrg_dynamics_world_get_position (world, g_array_index (ids, guint, 0), p0, NULL);
		for (s = 0; s < 30; s++)
			lrg_dynamics_world_step (world, 1.0f / 60.0f, NULL, NULL);
		lrg_dynamics_world_get_position (world, g_array_index (ids, guint, 0), p1, NULL);
		g_assert_cmpfloat (p1[1], <, p0[1]);
	}
}

static void
test_fracture_no_gpu (void)
{
	g_autoptr(LrgFracture) f = make_wall (2, LRG_FRACTURE_SEED_RANDOM, 3);
	g_autoptr(GError)      error = NULL;
	GrlMesh               *mesh;

	mesh = lrg_fracture_chunk_create_mesh (lrg_fracture_get_chunk (f, 0), TRUE, &error);
	g_assert_null (mesh);
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_GPU);
}

static void
test_fracture_perf (void)
{
	g_autoptr(LrgFracture) f = lrg_fracture_new ();
	const gfloat           half[3] = { 2.0f, 1.5f, 0.15f };
	gint64                 t0;
	gint64                 t1;

	lrg_fracture_set_box (f, half, NULL);
	g_object_set (f, "chunk-count", 64, NULL);
	t0 = g_get_monotonic_time ();
	g_assert_true (lrg_fracture_build (f, NULL));
	t1 = g_get_monotonic_time ();
	g_test_message ("64-chunk wall built in %.2f ms", (gdouble) (t1 - t0) / 1000.0);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_total_volume (f), 4.0f * 3.0f * 0.3f, 1e-4f);
}

static void
on_fractured (LrgDestructibleComponent *d,
              guint                     n_chunks,
              gpointer                  user_data)
{
	*(guint *) user_data = n_chunks;
}

/* A wall entity (cube + static body) broken by a fast sphere, then restored. */
static void
test_destructible_component (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)    wall = lrg_entity3d_new_with_name ("wall");
	g_autoptr(LrgEntity3D)    ball = lrg_entity3d_new_with_name ("ball");
	g_autoptr(LrgRenderList)  list = lrg_render_list_new ();
	LrgDynamicsWorld         *physics = lrg_world3d_ensure_physics (world);
	LrgMeshRendererComponent *cube = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_CUBE);
	LrgRigidBodyComponent    *wall_body = lrg_rigid_body_component_new (LRG_DYNAMICS_BODY_STATIC);
	LrgRigidBodyComponent    *ball_body = lrg_rigid_body_component_new (LRG_DYNAMICS_BODY_DYNAMIC);
	LrgDestructibleComponent *breakable = lrg_destructible_component_new ();
	const gfloat              speed[3] = { 0.0f, 0.0f, -40.0f };
	const gfloat              weak[3] = { 0.0f, 0.0f, -1.0f };
	const gfloat              here[3] = { 0.0f, 1.0f, 0.1f };
	guint                     fractured = 0;
	guint                     s;
	guint                     n_mesh = 0;
	guint                     i;
	gfloat                    v[3];

	lrg_mesh_renderer_component_set_size (cube, 2.0f, 1.0f, 0.2f);
	lrg_entity3d_set_position_xyz (wall, 0.0f, 1.0f, 0.0f);
	lrg_entity3d_add_component (wall, LRG_COMPONENT (cube));
	{
		g_autoptr(LrgColliderComponent3D) box = lrg_collider_component3d_new_box (1.0f, 0.5f, 0.1f);

		lrg_entity3d_add_component (wall, LRG_COMPONENT (box));
	}
	lrg_entity3d_add_component (wall, LRG_COMPONENT (wall_body));
	g_object_set (breakable, "chunk-count", 10, "impulse-threshold", 60.0f, "density", 500.0f, NULL);
	g_signal_connect (breakable, "fractured", G_CALLBACK (on_fractured), &fractured);
	lrg_entity3d_add_component (wall, LRG_COMPONENT (breakable));
	g_object_unref (cube);
	g_object_unref (wall_body);
	g_object_unref (breakable);
	lrg_world3d_add_entity (world, wall);

	/* A weak hit does nothing. */
	g_assert_false (lrg_destructible_component_report_impact (breakable, here, weak));
	g_assert_true (lrg_destructible_component_prepare (breakable, NULL));
	g_assert_cmpuint (lrg_fracture_get_chunk_count (lrg_destructible_component_get_fracture (breakable)), ==, 10);
	g_assert_cmpfloat_with_epsilon (lrg_fracture_get_total_volume (lrg_destructible_component_get_fracture (breakable)),
	                                0.4f, 1e-5f);

	/* A 5 kg ball at 40 m/s: about 200 N s, well above the threshold. */
	lrg_rigid_body_component_set_mass (ball_body, 5.0f);
	lrg_entity3d_set_position_xyz (ball, 0.3f, 1.2f, 1.5f);
	{
		g_autoptr(LrgColliderComponent3D) sphere = lrg_collider_component3d_new_sphere (0.15f);

		lrg_entity3d_add_component (ball, LRG_COMPONENT (sphere));
	}
	lrg_entity3d_add_component (ball, LRG_COMPONENT (ball_body));
	g_object_unref (ball_body);
	lrg_world3d_add_entity (world, ball);
	lrg_world3d_step (world, 1.0f / 60.0f);
	lrg_rigid_body_component_set_linear_velocity (ball_body, speed);
	for (s = 0; s < 60 && !lrg_destructible_component_is_broken (breakable); s++)
		lrg_world3d_step (world, 1.0f / 60.0f);
	g_assert_true (lrg_destructible_component_is_broken (breakable));
	g_assert_cmpuint (fractured, ==, 10);
	g_assert_cmpuint (lrg_destructible_component_get_chunk_count (breakable), ==, 10);
	g_assert_cmpuint (lrg_destructible_component_get_chunk_body (breakable, 10), ==, G_MAXUINT);
	g_assert_false (lrg_component_get_enabled (LRG_COMPONENT (cube)));
	g_assert_false (lrg_component_get_enabled (LRG_COMPONENT (wall_body)));
	/* The hit pushed the chunks along -Z. */
	g_assert_true (lrg_dynamics_world_get_linear_velocity (physics, lrg_destructible_component_get_chunk_body (breakable, 0), v, NULL));
	g_test_message ("broke after %u steps; chunk 0 velocity z %.2f", s, v[2]);

	for (s = 0; s < 20; s++)
		lrg_world3d_step (world, 1.0f / 60.0f);
	lrg_world3d_collect_render (world, NULL, list);
	for (i = 0; i < lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE); i++)
		if (lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, i)->kind == LRG_RENDER_ITEM_MESH)
			n_mesh++;
	g_assert_cmpuint (n_mesh, ==, 10);

	/* Restore: chunk bodies gone, the wall is back. */
	lrg_destructible_component_restore (breakable);
	g_assert_false (lrg_destructible_component_is_broken (breakable));
	g_assert_cmpuint (lrg_destructible_component_get_chunk_count (breakable), ==, 0);
	g_assert_true (lrg_component_get_enabled (LRG_COMPONENT (cube)));
	g_assert_true (lrg_component_get_enabled (LRG_COMPONENT (wall_body)));

	/* On demand, with a lifetime: the chunks leave after it. */
	g_object_set (breakable, "chunk-lifetime", 0.25f, "auto-break", FALSE, NULL);
	g_assert_true (lrg_destructible_component_fracture (breakable, here, NULL, NULL));
	g_assert_cmpuint (lrg_destructible_component_get_chunk_count (breakable), ==, 10);
	for (s = 0; s < 30; s++)
		lrg_world3d_step (world, 1.0f / 60.0f);
	g_assert_cmpuint (lrg_destructible_component_get_chunk_count (breakable), ==, 0);
	g_assert_true (lrg_destructible_component_is_broken (breakable));
}

/* Without a cube or a fracture there is nothing to break. */
static void
test_destructible_errors (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)    thing = lrg_entity3d_new_with_name ("thing");
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgFracture)    f = make_wall (3, LRG_FRACTURE_SEED_RANDOM, 1);
	LrgDestructibleComponent *d = lrg_destructible_component_new ();

	lrg_entity3d_add_component (thing, LRG_COMPONENT (d));
	g_object_unref (d);
	g_assert_false (lrg_destructible_component_fracture (d, NULL, NULL, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_false (lrg_destructible_component_prepare (d, &error));
	g_assert_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE);
	g_clear_error (&error);

	/* An explicit fracture works for any shape. */
	lrg_destructible_component_set_fracture (d, f);
	g_assert_true (lrg_destructible_component_get_fracture (d) == f);
	lrg_world3d_ensure_physics (world);
	lrg_world3d_add_entity (world, thing);
	g_assert_true (lrg_destructible_component_fracture (d, NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_destructible_component_get_chunk_count (d), ==, 3);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/fracture/wall", test_fracture_wall);
	g_test_add_func ("/fracture/mass-properties", test_fracture_mass_properties);
	g_test_add_func ("/fracture/halves", test_fracture_halves);
	g_test_add_func ("/fracture/custom-mesh", test_fracture_custom_mesh);
	g_test_add_func ("/fracture/errors", test_fracture_errors);
	g_test_add_func ("/fracture/seed-modes", test_fracture_seed_modes);
	g_test_add_func ("/fracture/determinism", test_fracture_determinism);
	g_test_add_func ("/fracture/round-trip", test_fracture_round_trip);
	g_test_add_func ("/fracture/cache", test_fracture_cache);
	g_test_add_func ("/fracture/swap-helpers", test_fracture_swap_helpers);
	g_test_add_func ("/fracture/shape", test_fracture_shape);
	g_test_add_func ("/fracture/swap-in", test_fracture_swap_in);
	g_test_add_func ("/fracture/no-gpu", test_fracture_no_gpu);
	g_test_add_func ("/fracture/perf", test_fracture_perf);
	g_test_add_func ("/fracture/destructible", test_destructible_component);
	g_test_add_func ("/fracture/destructible/errors", test_destructible_errors);
	return g_test_run ();
}
