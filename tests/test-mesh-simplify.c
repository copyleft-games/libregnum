/* test-mesh-simplify.c - LrgMeshSimplify (QEM edge collapse)
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests build meshes in memory (planar grid, welded subdivided
 * cube, UV sphere with a seam column and pole fans) and check:
 *  - the triangle target is reached when the error allows it;
 *  - closed meshes stay closed and 2-manifold by position (seams never
 *    open, no T-junctions);
 *  - locked borders and seams keep every one of their vertices;
 *  - the error limit holds and the error grows monotonically with the
 *    reduction;
 *  - raylib Mesh data (unindexed, with UVs) is welded, simplified and
 *    compacted with its attributes intact.
 * The /mesh-simplify/gl/ cases need a graphics context (isolated Xvfb).
 * Run with -m perf for the timing table used in the docs.
 */

#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-mesh-simplify-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Mesh generators
 * ========================================================================== */

typedef struct
{
	GArray *pos;      /* gfloat, 3 per vertex */
	GArray *idx;      /* guint32 */
} TestMesh;

static void
test_mesh_init (TestMesh *m)
{
	m->pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	m->idx = g_array_new (FALSE, FALSE, sizeof (guint32));
}

static void
test_mesh_clear (TestMesh *m)
{
	g_array_unref (m->pos);
	g_array_unref (m->idx);
}

static guint32
add_vertex (TestMesh *m,
            gfloat    x,
            gfloat    y,
            gfloat    z)
{
	gfloat p[3];

	p[0] = x;
	p[1] = y;
	p[2] = z;
	g_array_append_vals (m->pos, p, 3);
	return m->pos->len / 3 - 1;
}

static void
add_tri (TestMesh *m,
         guint32   a,
         guint32   b,
         guint32   c)
{
	guint32 t[3];

	t[0] = a;
	t[1] = b;
	t[2] = c;
	g_array_append_vals (m->idx, t, 3);
}

/* An n x n quad grid in the XY plane (counter-clockwise seen from +Z). */
static void
make_grid (TestMesh *m,
           guint     n)
{
	guint i, j;

	test_mesh_init (m);
	for (j = 0; j <= n; j++)
		for (i = 0; i <= n; i++)
			add_vertex (m, (gfloat) i, (gfloat) j, 0.0f);
	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++)
		{
			guint32 a = j * (n + 1) + i;
			guint32 b = a + 1;
			guint32 c = a + (n + 1) + 1;
			guint32 d = a + (n + 1);

			add_tri (m, a, b, c);
			add_tri (m, a, c, d);
		}
}

/* Vertex at (x, y, z) in a welded mesh (positions de-duplicated). */
static guint32
welded_vertex (TestMesh   *m,
               GHashTable *seen,
               gfloat      x,
               gfloat      y,
               gfloat      z)
{
	g_autofree gchar *key = g_strdup_printf ("%a %a %a", (gdouble) x, (gdouble) y, (gdouble) z);
	gpointer          found;

	if (g_hash_table_lookup_extended (seen, key, NULL, &found))
		return GPOINTER_TO_UINT (found);
	found = GUINT_TO_POINTER (add_vertex (m, x, y, z));
	g_hash_table_insert (seen, g_steal_pointer (&key), found);
	return GPOINTER_TO_UINT (found);
}

/* A [0,n]^3 cube, every face an n x n grid, vertices shared across faces
 * (closed, no seams, outward winding). */
static void
make_cube (TestMesh *m,
           guint     n)
{
	g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	/* origin, u axis, v axis per face (u x v points outward) */
	static const gint faces[6][9] = {
		{ 0, 0, 0,   0, 1, 0,   1, 0, 0 },  /* z = 0, normal -z */
		{ 0, 0, 1,   1, 0, 0,   0, 1, 0 },  /* z = 1, normal +z */
		{ 0, 0, 0,   1, 0, 0,   0, 0, 1 },  /* y = 0, normal -y */
		{ 0, 1, 0,   0, 0, 1,   1, 0, 0 },  /* y = 1, normal +y */
		{ 0, 0, 0,   0, 0, 1,   0, 1, 0 },  /* x = 0, normal -x */
		{ 1, 0, 0,   0, 1, 0,   0, 0, 1 }   /* x = 1, normal +x */
	};
	guint f, i, j;

	test_mesh_init (m);
	for (f = 0; f < 6; f++)
	{
		const gint *o = faces[f];

		for (j = 0; j < n; j++)
			for (i = 0; i < n; i++)
			{
				guint32 q[4];
				guint   k;

				for (k = 0; k < 4; k++)
				{
					guint di = (k == 1 || k == 2) ? 1 : 0;
					guint dj = (k >= 2) ? 1 : 0;
					gfloat u = (gfloat) (i + di);
					gfloat v = (gfloat) (j + dj);

					q[k] = welded_vertex (m, seen,
					                      (gfloat) o[0] * n + u * o[3] + v * o[6],
					                      (gfloat) o[1] * n + u * o[4] + v * o[7],
					                      (gfloat) o[2] * n + u * o[5] + v * o[8]);
				}
				add_tri (m, q[0], q[1], q[2]);
				add_tri (m, q[0], q[2], q[3]);
			}
	}
}

/* A UV sphere: (rings + 1) x (segments + 1) vertices; column 0 and column
 * @segments share positions (the UV seam) and every pole is one position
 * with @segments + 1 wedges. Degenerate pole triangles are skipped. */
static void
make_sphere (TestMesh *m,
             guint     rings,
             guint     segments)
{
	guint i, j;

	test_mesh_init (m);
	for (i = 0; i <= rings; i++)
		for (j = 0; j <= segments; j++)
		{
			gdouble theta = G_PI * (gdouble) i / (gdouble) rings;
			gdouble phi = 2.0 * G_PI * (gdouble) (j % segments) / (gdouble) segments;

			if (i == 0)
				add_vertex (m, 0.0f, 1.0f, 0.0f);
			else if (i == rings)
				add_vertex (m, 0.0f, -1.0f, 0.0f);
			else
				add_vertex (m, (gfloat) (sin (theta) * cos (phi)), (gfloat) cos (theta),
				            (gfloat) (sin (theta) * sin (phi)));
		}
	for (i = 0; i < rings; i++)
		for (j = 0; j < segments; j++)
		{
			guint32 a = i * (segments + 1) + j;
			guint32 b = a + (segments + 1);
			guint32 c = b + 1;
			guint32 d = a + 1;

			if (i != 0)
				add_tri (m, a, d, c);
			if (i != rings - 1)
				add_tri (m, a, c, b);
		}
}

/* ==========================================================================
 * Checks
 * ========================================================================== */

/* Position key of vertex @v. */
static gchar *
pos_key (const gfloat *pos,
         guint32       v)
{
	return g_strdup_printf ("%a %a %a", (gdouble) pos[v * 3], (gdouble) pos[v * 3 + 1],
	                        (gdouble) pos[v * 3 + 2]);
}

/*
 * check_closed_manifold:
 *
 * Every directed edge (by position) appears exactly once and its
 * reverse exactly once: the surface is closed, 2-manifold and
 * consistently oriented, so no seam opened.
 */
static void
check_closed_manifold (const gfloat  *pos,
                       const guint32 *idx,
                       gsize          n_idx)
{
	g_autoptr(GHashTable) edges = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GHashTableIter        it;
	gpointer              key;
	gpointer              value;
	gsize                 t;

	for (t = 0; t < n_idx; t += 3)
	{
		guint k;

		for (k = 0; k < 3; k++)
		{
			g_autofree gchar *a = pos_key (pos, idx[t + k]);
			g_autofree gchar *b = pos_key (pos, idx[t + (k + 1) % 3]);
			gchar            *e = g_strdup_printf ("%s|%s", a, b);
			guint             count = GPOINTER_TO_UINT (g_hash_table_lookup (edges, e));

			g_hash_table_replace (edges, e, GUINT_TO_POINTER (count + 1));
		}
	}

	g_hash_table_iter_init (&it, edges);
	while (g_hash_table_iter_next (&it, &key, &value))
	{
		g_auto(GStrv)     parts = g_strsplit ((const gchar *) key, "|", 2);
		g_autofree gchar *rev = g_strdup_printf ("%s|%s", parts[1], parts[0]);

		g_assert_cmpuint (GPOINTER_TO_UINT (value), ==, 1);
		g_assert_cmpuint (GPOINTER_TO_UINT (g_hash_table_lookup (edges, rev)), ==, 1);
	}
}

/* TRUE if vertex @v is referenced by @idx. */
static gboolean
uses_vertex (const guint32 *idx,
             gsize          n_idx,
             guint32        v)
{
	gsize i;

	for (i = 0; i < n_idx; i++)
		if (idx[i] == v)
			return TRUE;
	return FALSE;
}

static void
run (LrgMeshSimplify *s,
     TestMesh        *m,
     guint32        **out,
     gsize           *n_out)
{
	g_autoptr(GError) error = NULL;
	gboolean          ok;

	ok = lrg_mesh_simplify_indices (s, (const guint32 *) m->idx->data, m->idx->len,
	                                (const gfloat *) m->pos->data, m->pos->len, 3, out, n_out, &error);
	g_assert_no_error (error);
	g_assert_true (ok);
	g_assert_cmpuint (*n_out % 3, ==, 0);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void
test_defaults (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	gfloat                     ratio;

	g_assert_cmpfloat_with_epsilon (lrg_mesh_simplify_get_target_ratio (s), 0.5f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_mesh_simplify_get_target_error (s), 0.01f, 1e-6f);
	g_assert_true (lrg_mesh_simplify_get_lock_border (s));
	g_assert_false (lrg_mesh_simplify_get_lock_seams (s));
	g_assert_cmpfloat (lrg_mesh_simplify_get_result_ratio (s), ==, 1.0f);

	lrg_mesh_simplify_set_target_ratio (s, 2.0f);
	g_assert_cmpfloat (lrg_mesh_simplify_get_target_ratio (s), ==, 1.0f);
	g_object_set (s, "target-ratio", 0.25f, "lock-seams", TRUE, NULL);
	g_object_get (s, "target-ratio", &ratio, NULL);
	g_assert_cmpfloat_with_epsilon (ratio, 0.25f, 1e-6f);
	g_assert_true (lrg_mesh_simplify_get_lock_seams (s));
}

/* A flat grid reaches the ratio at zero error; locked borders keep all
 * of their vertices. */
static void
test_grid_locked_border (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *out = NULL;
	gsize                      n_out = 0;
	TestMesh                   m;
	guint                      n = 32;
	guint                      i;

	make_grid (&m, n);
	lrg_mesh_simplify_set_target_ratio (s, 0.1f);
	lrg_mesh_simplify_set_target_error (s, 1e-4f);
	run (s, &m, &out, &n_out);

	g_assert_cmpuint (n_out / 3, <=, (guint) (0.1 * 2 * n * n + 0.5));
	g_assert_cmpfloat (lrg_mesh_simplify_get_result_error (s), <=, 1e-4f);
	for (i = 0; i <= n; i++)
	{
		g_assert_true (uses_vertex (out, n_out, i));                     /* bottom */
		g_assert_true (uses_vertex (out, n_out, n * (n + 1) + i));       /* top */
		g_assert_true (uses_vertex (out, n_out, i * (n + 1)));           /* left */
		g_assert_true (uses_vertex (out, n_out, i * (n + 1) + n));       /* right */
	}
	test_mesh_clear (&m);
}

/* Unlocked borders slide along straight edges; corners stay. */
static void
test_grid_free_border (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *out = NULL;
	gsize                      n_out = 0;
	TestMesh                   m;
	guint                      n = 16;

	make_grid (&m, n);
	lrg_mesh_simplify_set_lock_border (s, FALSE);
	lrg_mesh_simplify_set_target_ratio (s, 0.0f);
	lrg_mesh_simplify_set_target_error (s, 1e-3f);
	run (s, &m, &out, &n_out);

	g_assert_cmpuint (n_out / 3, <=, 4);
	g_assert_true (uses_vertex (out, n_out, 0));
	g_assert_true (uses_vertex (out, n_out, n));
	g_assert_true (uses_vertex (out, n_out, n * (n + 1)));
	g_assert_true (uses_vertex (out, n_out, n * (n + 1) + n));
	test_mesh_clear (&m);
}

/* A subdivided closed cube collapses to (nearly) its 12 triangles with
 * zero error and stays closed. */
static void
test_cube (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *out = NULL;
	gsize                      n_out = 0;
	TestMesh                   m;

	make_cube (&m, 8);
	g_assert_cmpuint (m.idx->len / 3, ==, 6 * 8 * 8 * 2);
	check_closed_manifold ((const gfloat *) m.pos->data, (const guint32 *) m.idx->data, m.idx->len);

	lrg_mesh_simplify_set_target_ratio (s, 0.0f);
	lrg_mesh_simplify_set_target_error (s, 1e-4f);
	run (s, &m, &out, &n_out);

	g_assert_cmpuint (n_out / 3, <=, 24);
	g_assert_cmpfloat (lrg_mesh_simplify_get_result_error (s), <=, 1e-4f);
	check_closed_manifold ((const gfloat *) m.pos->data, out, n_out);
	test_mesh_clear (&m);
}

/* A UV sphere reaches a 25% ratio and stays closed: the seam column and
 * the pole fans never open. */
static void
test_sphere_ratio (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *out = NULL;
	gsize                      n_out = 0;
	TestMesh                   m;
	gsize                      target;

	make_sphere (&m, 24, 48);
	check_closed_manifold ((const gfloat *) m.pos->data, (const guint32 *) m.idx->data, m.idx->len);

	lrg_mesh_simplify_set_target_ratio (s, 0.25f);
	lrg_mesh_simplify_set_target_error (s, 1.0f);
	run (s, &m, &out, &n_out);

	target = (gsize) (0.25 * (m.idx->len / 3) + 0.5);
	g_assert_cmpuint (n_out / 3, <=, target);
	g_assert_cmpuint (n_out / 3, >, target / 2);   /* stopped near the target */
	check_closed_manifold ((const gfloat *) m.pos->data, out, n_out);
	test_mesh_clear (&m);
}

/* With lock-seams every seam vertex survives; with sliding seams the
 * seam may shorten but both of its sides stay connected. */
static void
test_sphere_seams (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *locked = NULL;
	g_autofree guint32        *sliding = NULL;
	gsize                      n_locked = 0;
	gsize                      n_sliding = 0;
	guint                      rings = 16;
	guint                      segments = 32;
	TestMesh                   m;
	guint                      i;
	guint                      seam_left = 0;
	guint                      seam_right = 0;

	make_sphere (&m, rings, segments);
	lrg_mesh_simplify_set_target_ratio (s, 0.2f);
	lrg_mesh_simplify_set_target_error (s, 1.0f);

	lrg_mesh_simplify_set_lock_seams (s, TRUE);
	run (s, &m, &locked, &n_locked);
	for (i = 1; i < rings; i++)
	{
		g_assert_true (uses_vertex (locked, n_locked, i * (segments + 1)));
		g_assert_true (uses_vertex (locked, n_locked, i * (segments + 1) + segments));
	}
	check_closed_manifold ((const gfloat *) m.pos->data, locked, n_locked);

	lrg_mesh_simplify_set_lock_seams (s, FALSE);
	run (s, &m, &sliding, &n_sliding);
	check_closed_manifold ((const gfloat *) m.pos->data, sliding, n_sliding);
	/* Both wedges of every remaining seam position are still used. */
	for (i = 1; i < rings; i++)
	{
		gboolean left = uses_vertex (sliding, n_sliding, i * (segments + 1));
		gboolean right = uses_vertex (sliding, n_sliding, i * (segments + 1) + segments);

		g_assert_true (left == right);
		seam_left += left ? 1 : 0;
		seam_right += right ? 1 : 0;
	}
	g_assert_cmpuint (seam_left, ==, seam_right);
	g_assert_cmpuint (n_sliding, <=, n_locked);
	test_mesh_clear (&m);
}

/* Error grows (never shrinks) as the ratio drops; triangle counts fall. */
static void
test_error_monotone (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	TestMesh                   m;
	gfloat                     last_error = 0.0f;
	gsize                      last_tris = G_MAXSIZE;
	guint                      step;

	make_sphere (&m, 24, 48);
	lrg_mesh_simplify_set_target_error (s, 1.0f);
	for (step = 9; step >= 1; step--)
	{
		g_autofree guint32 *out = NULL;
		gsize               n_out = 0;
		gfloat              e;

		lrg_mesh_simplify_set_target_ratio (s, (gfloat) step / 10.0f);
		run (s, &m, &out, &n_out);
		e = lrg_mesh_simplify_get_result_error (s);
		g_assert_cmpfloat (e, >=, last_error);
		g_assert_cmpuint (n_out / 3, <=, last_tris);
		last_error = e;
		last_tris = n_out / 3;
	}
	g_assert_cmpfloat (last_error, >, 0.0f);
	test_mesh_clear (&m);
}

/* A tight error budget stops early and is respected. */
static void
test_error_limit (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *out = NULL;
	gsize                      n_out = 0;
	TestMesh                   m;

	make_sphere (&m, 24, 48);
	lrg_mesh_simplify_set_target_ratio (s, 0.0f);
	lrg_mesh_simplify_set_target_error (s, 0.002f);
	run (s, &m, &out, &n_out);

	g_assert_cmpfloat (lrg_mesh_simplify_get_result_error (s), <=, 0.002f);
	g_assert_cmpfloat (lrg_mesh_simplify_get_result_ratio (s), >, 0.25f);
	g_assert_cmpfloat (lrg_mesh_simplify_get_result_ratio (s), <, 1.0f);
	test_mesh_clear (&m);
}

static void
test_classify (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autoptr(GError)          error = NULL;
	g_autofree guint8         *kinds = NULL;
	g_autofree guint8         *sphere_kinds = NULL;
	gsize                      n_kinds = 0;
	TestMesh                   grid;
	TestMesh                   sphere;
	guint                      n = 4;

	make_grid (&grid, n);
	lrg_mesh_simplify_set_lock_border (s, FALSE);
	g_assert_true (lrg_mesh_simplify_classify (s, (const guint32 *) grid.idx->data, grid.idx->len,
	                                           (const gfloat *) grid.pos->data, grid.pos->len, 3,
	                                           &kinds, &n_kinds, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (n_kinds, ==, (n + 1) * (n + 1));
	g_assert_cmpint (kinds[0], ==, LRG_MESH_VERTEX_KIND_BORDER);
	g_assert_cmpint (kinds[2], ==, LRG_MESH_VERTEX_KIND_BORDER);
	g_assert_cmpint (kinds[(n + 1) + 1], ==, LRG_MESH_VERTEX_KIND_MANIFOLD);
	g_clear_pointer (&kinds, g_free);

	lrg_mesh_simplify_set_lock_border (s, TRUE);
	g_assert_true (lrg_mesh_simplify_classify (s, (const guint32 *) grid.idx->data, grid.idx->len,
	                                           (const gfloat *) grid.pos->data, grid.pos->len, 3,
	                                           &kinds, &n_kinds, &error));
	g_assert_cmpint (kinds[2], ==, LRG_MESH_VERTEX_KIND_LOCKED);

	make_sphere (&sphere, 8, 16);
	g_assert_true (lrg_mesh_simplify_classify (s, (const guint32 *) sphere.idx->data, sphere.idx->len,
	                                           (const gfloat *) sphere.pos->data, sphere.pos->len, 3,
	                                           &sphere_kinds, &n_kinds, &error));
	g_assert_cmpint (sphere_kinds[0], ==, LRG_MESH_VERTEX_KIND_LOCKED);         /* pole */
	g_assert_cmpint (sphere_kinds[4 * 17], ==, LRG_MESH_VERTEX_KIND_SEAM);      /* seam left */
	g_assert_cmpint (sphere_kinds[4 * 17 + 16], ==, LRG_MESH_VERTEX_KIND_SEAM); /* seam right */
	g_assert_cmpint (sphere_kinds[4 * 17 + 5], ==, LRG_MESH_VERTEX_KIND_MANIFOLD);

	{
		g_autofree gchar *name = g_enum_to_string (LRG_TYPE_MESH_VERTEX_KIND, LRG_MESH_VERTEX_KIND_SEAM);

		g_assert_cmpstr (name, ==, "LRG_MESH_VERTEX_KIND_SEAM");
	}
	test_mesh_clear (&grid);
	test_mesh_clear (&sphere);
}

static void
test_invalid (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autoptr(GError)          error = NULL;
	guint32                   *out = NULL;
	gsize                      n_out = 0;
	static const gfloat        pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	static const guint32       bad_count[4] = { 0, 1, 2, 0 };
	static const guint32       bad_range[3] = { 0, 1, 7 };

	g_assert_false (lrg_mesh_simplify_indices (s, bad_count, 4, pos, 9, 3, &out, &n_out, &error));
	g_assert_error (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT);
	g_clear_error (&error);
	g_assert_false (lrg_mesh_simplify_indices (s, bad_range, 3, pos, 9, 3, &out, &n_out, &error));
	g_assert_error (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT);
	g_clear_error (&error);
	g_assert_false (lrg_mesh_simplify_indices (s, bad_range, 3, pos, 9, 2, &out, &n_out, &error));
	g_assert_error (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_INVALID_INPUT);
	g_assert_null (out);
}

static void
test_deterministic (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autofree guint32        *a = NULL;
	g_autofree guint32        *b = NULL;
	gsize                      na = 0;
	gsize                      nb = 0;
	TestMesh                   m;

	make_sphere (&m, 16, 32);
	lrg_mesh_simplify_set_target_error (s, 1.0f);
	run (s, &m, &a, &na);
	run (s, &m, &b, &nb);
	g_assert_cmpuint (na, ==, nb);
	g_assert_cmpmem (a, na * sizeof (guint32), b, nb * sizeof (guint32));
	test_mesh_clear (&m);
}

/* An unindexed raylib Mesh with UVs (u = x / n, v = y / n): welded,
 * simplified, compacted, every output vertex keeps its own UV. */
static void
test_raylib_unindexed (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autoptr(GError)          error = NULL;
	TestMesh                   grid;
	Mesh                       src;
	Mesh                       dst;
	guint                      n = 16;
	guint                      i;

	make_grid (&grid, n);
	memset (&src, 0, sizeof src);
	src.vertexCount = (int) grid.idx->len;
	src.triangleCount = (int) grid.idx->len / 3;
	src.vertices = (float *) malloc (sizeof (float) * 3 * grid.idx->len);
	src.texcoords = (float *) malloc (sizeof (float) * 2 * grid.idx->len);
	src.colors = (unsigned char *) malloc (4 * grid.idx->len);
	for (i = 0; i < grid.idx->len; i++)
	{
		guint32       v = g_array_index (grid.idx, guint32, i);
		const gfloat *p = &g_array_index (grid.pos, gfloat, v * 3);

		memcpy (src.vertices + i * 3, p, 3 * sizeof (float));
		src.texcoords[i * 2] = p[0] / (gfloat) n;
		src.texcoords[i * 2 + 1] = p[1] / (gfloat) n;
		src.colors[i * 4] = (unsigned char) p[0];
		src.colors[i * 4 + 1] = (unsigned char) p[1];
		src.colors[i * 4 + 2] = 7;
		src.colors[i * 4 + 3] = 255;
	}

	lrg_mesh_simplify_set_target_ratio (s, 0.25f);
	lrg_mesh_simplify_set_target_error (s, 1e-3f);
	g_assert_true (lrg_mesh_simplify_raylib_mesh (s, &src, &dst, &error));
	g_assert_no_error (error);

	g_assert_nonnull (dst.indices);
	g_assert_nonnull (dst.texcoords);
	g_assert_nonnull (dst.colors);
	g_assert_null (dst.normals);
	g_assert_cmpint (dst.vertexCount, <=, (gint) ((n + 1) * (n + 1)));
	g_assert_cmpint (dst.triangleCount, <=, (gint) (0.25 * 2 * n * n + 0.5));
	for (i = 0; i < (guint) dst.vertexCount; i++)
	{
		g_assert_cmpfloat_with_epsilon (dst.texcoords[i * 2], dst.vertices[i * 3] / (gfloat) n, 1e-6f);
		g_assert_cmpfloat_with_epsilon (dst.texcoords[i * 2 + 1], dst.vertices[i * 3 + 1] / (gfloat) n, 1e-6f);
		g_assert_cmpint (dst.colors[i * 4], ==, (gint) dst.vertices[i * 3]);
		g_assert_cmpint (dst.colors[i * 4 + 2], ==, 7);
	}
	for (i = 0; i < (guint) dst.triangleCount * 3; i++)
		g_assert_cmpint (dst.indices[i], <, dst.vertexCount);

	free (src.vertices);
	free (src.texcoords);
	free (src.colors);
	free (dst.vertices);
	free (dst.texcoords);
	free (dst.colors);
	free (dst.indices);
	test_mesh_clear (&grid);
}

/* Without a graphics context the upload path fails cleanly. */
static void
test_no_gpu (void)
{
	g_autoptr(GError) error = NULL;
	Mesh              m;
	GrlMesh          *mesh;

	if (graphics_available)
	{
		g_test_skip ("A graphics context exists");
		return;
	}
	memset (&m, 0, sizeof m);
	m.vertexCount = 3;
	m.triangleCount = 1;
	m.vertices = (float *) calloc (9, sizeof (float));
	mesh = lrg_mesh_simplify_wrap_raylib_mesh (&m, &error);
	g_assert_null (mesh);
	g_assert_error (error, LRG_MESH_SIMPLIFY_ERROR, LRG_MESH_SIMPLIFY_ERROR_NO_GPU);
	g_assert_null (m.vertices);
}

/* GPU: a raylib sphere (unindexed) simplifies into an uploaded mesh. */
static void
test_gl_mesh (void)
{
	g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
	g_autoptr(GrlMesh)         sphere = NULL;
	g_autoptr(GrlMesh)         low = NULL;
	g_autoptr(GError)          error = NULL;
	Mesh                      *h;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	sphere = grl_mesh_new_sphere (1.0f, 24, 32);
	lrg_mesh_simplify_set_target_ratio (s, 0.25f);
	lrg_mesh_simplify_set_target_error (s, 1.0f);
	low = lrg_mesh_simplify_mesh (s, sphere, &error);
	g_assert_no_error (error);
	g_assert_nonnull (low);
	g_assert_true (grl_mesh_is_valid (low));
	g_assert_cmpint (grl_mesh_get_triangle_count (low), <=,
	                 (gint) (0.25 * grl_mesh_get_triangle_count (sphere) + 1));
	h = (Mesh *) grl_mesh_get_handle (low);
	g_assert_cmpuint (h->vaoId, >, 0);
	g_assert_nonnull (h->texcoords);
	g_assert_nonnull (h->normals);
}

/* -m perf: time vs triangle count (grid with a height bump, sphere). */
static void
test_perf (void)
{
	static const guint sizes[] = { 32, 100, 316, 708 };
	guint              i;

	if (!g_test_perf ())
	{
		g_test_skip ("Run with -m perf");
		return;
	}
	for (i = 0; i < G_N_ELEMENTS (sizes); i++)
	{
		g_autoptr(LrgMeshSimplify) s = lrg_mesh_simplify_new ();
		g_autofree guint32        *out = NULL;
		gsize                      n_out = 0;
		TestMesh                   m;
		gint64                     t0;
		gdouble                    ms;
		guint                      v;

		make_grid (&m, sizes[i]);
		/* Make it non-planar so the error drives the order. */
		for (v = 0; v < m.pos->len / 3; v++)
		{
			gfloat *p = &g_array_index (m.pos, gfloat, v * 3);

			p[2] = (gfloat) (sin (p[0] * 0.05) * cos (p[1] * 0.05) * sizes[i] * 0.05);
		}
		lrg_mesh_simplify_set_target_ratio (s, 0.25f);
		lrg_mesh_simplify_set_target_error (s, 1.0f);
		t0 = g_get_monotonic_time ();
		run (s, &m, &out, &n_out);
		ms = (gdouble) (g_get_monotonic_time () - t0) / 1000.0;
		g_test_message ("grid %u tris -> %" G_GSIZE_FORMAT " tris: %.1f ms (error %.4f)",
		                m.idx->len / 3, n_out / 3, ms, lrg_mesh_simplify_get_result_error (s));
		g_test_minimized_result (ms, "simplify %u tris to 25%%: %.1f ms", m.idx->len / 3, ms);
		test_mesh_clear (&m);
	}
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/mesh-simplify/defaults", test_defaults);
	g_test_add_func ("/mesh-simplify/grid/locked-border", test_grid_locked_border);
	g_test_add_func ("/mesh-simplify/grid/free-border", test_grid_free_border);
	g_test_add_func ("/mesh-simplify/cube", test_cube);
	g_test_add_func ("/mesh-simplify/sphere/ratio", test_sphere_ratio);
	g_test_add_func ("/mesh-simplify/sphere/seams", test_sphere_seams);
	g_test_add_func ("/mesh-simplify/error/monotone", test_error_monotone);
	g_test_add_func ("/mesh-simplify/error/limit", test_error_limit);
	g_test_add_func ("/mesh-simplify/classify", test_classify);
	g_test_add_func ("/mesh-simplify/invalid", test_invalid);
	g_test_add_func ("/mesh-simplify/deterministic", test_deterministic);
	g_test_add_func ("/mesh-simplify/raylib/unindexed", test_raylib_unindexed);
	g_test_add_func ("/mesh-simplify/raylib/no-gpu", test_no_gpu);
	g_test_add_func ("/mesh-simplify/gl/mesh", test_gl_mesh);
	g_test_add_func ("/mesh-simplify/perf", test_perf);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
