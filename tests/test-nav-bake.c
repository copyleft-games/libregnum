/* test-nav-bake.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavBake: the .rnav format (round trip, unknown keys, strict errors
 * with key paths), bake determinism and the cache, slope and step
 * filtering, off-mesh links, area costs, region filtering, baking from
 * an LrgWorld3D's mesh renderers and colliders (filters, planes,
 * dynamic bodies), boundary segments and debug draw recording.
 */

#include <libregnum.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Geometry helpers                                                     */

static void
quad (GArray       *tris,
      const gfloat *a,
      const gfloat *b,
      const gfloat *c,
      const gfloat *d)
{
	g_array_append_vals (tris, a, 3);
	g_array_append_vals (tris, b, 3);
	g_array_append_vals (tris, c, 3);
	g_array_append_vals (tris, a, 3);
	g_array_append_vals (tris, c, 3);
	g_array_append_vals (tris, d, 3);
}

static void
floor_rect (GArray *tris,
            gfloat  x0,
            gfloat  z0,
            gfloat  x1,
            gfloat  z1,
            gfloat  y)
{
	const gfloat a[3] = { x0, y, z0 };
	const gfloat b[3] = { x1, y, z0 };
	const gfloat c[3] = { x1, y, z1 };
	const gfloat d[3] = { x0, y, z1 };

	quad (tris, a, b, c, d);
}

/* a ramp along +X from (x0, y0) to (x1, y1), z0 .. z1 wide */
static void
ramp (GArray *tris,
      gfloat  x0,
      gfloat  y0,
      gfloat  x1,
      gfloat  y1,
      gfloat  z0,
      gfloat  z1)
{
	const gfloat a[3] = { x0, y0, z0 };
	const gfloat b[3] = { x1, y1, z0 };
	const gfloat c[3] = { x1, y1, z1 };
	const gfloat d[3] = { x0, y0, z1 };

	quad (tris, a, b, c, d);
}

static gfloat
path_length (const gfloat *p,
             guint         n_floats)
{
	gfloat len = 0.0f;
	guint  i;

	for (i = 3; i < n_floats; i += 3)
	{
		gfloat dx = p[i] - p[i - 3];
		gfloat dy = p[i + 1] - p[i - 2];
		gfloat dz = p[i + 2] - p[i - 1];

		len += sqrtf (dx * dx + dy * dy + dz * dz);
	}
	return len;
}

static gfloat *
find (LrgNavPolyMesh *mesh,
      gfloat          sx,
      gfloat          sy,
      gfloat          sz,
      gfloat          gx,
      gfloat          gy,
      gfloat          gz,
      guint          *n_floats)
{
	const gfloat s[3] = { sx, sy, sz };
	const gfloat g[3] = { gx, gy, gz };
	const gfloat ext[3] = { 1.0f, 1.0f, 1.0f };

	return lrg_nav_poly_mesh_find_path (mesh, s, g, ext, 0, NULL, NULL, NULL, n_floats, NULL);
}

/* ------------------------------------------------------------------ */
/* Format                                                               */

static LrgNavBake *
full_settings (void)
{
	LrgNavBake         *bake = lrg_nav_bake_new ();
	const gchar *const  inc[] = { "floor", "ground", NULL };
	const gchar *const  exc[] = { "no-nav", NULL };
	const gchar *const  layers[] = { "static", "default", NULL };
	const gfloat        bmin[3] = { -50.0f, -5.0f, -50.0f };
	const gfloat        bmax[3] = { 50.0f, 20.0f, 50.0f };
	const gfloat        amin[3] = { 1.0f, -1.0f, 2.0f };
	const gfloat        amax[3] = { 3.5f, 2.0f, 4.25f };
	const gfloat        ls[3] = { 0.0f, 0.0f, 0.1f };
	const gfloat        le[3] = { 0.3f, 2.5f, -7.0f };
	gint                mud;

	lrg_nav_bake_set_name (bake, "test level");
	lrg_nav_bake_set_agent (bake, 0.4f, 1.7f, 45.0f, 0.3f);
	g_object_set (bake, "max-drop", 1.5f, "min-region-area", 2.0f, "corner-margin", 0.1f,
	              "static-only", TRUE, "layer-mask", 0x0000ffffu, "output", "nav/level.rnavmesh", NULL);
	lrg_nav_bake_set_cell_size (bake, 0.2f);
	lrg_nav_bake_set_cell_height (bake, 0.05f);
	lrg_nav_bake_set_include_tags (bake, inc);
	lrg_nav_bake_set_exclude_tags (bake, exc);
	lrg_nav_bake_set_physics_layers (bake, layers);
	lrg_nav_bake_set_bounds (bake, bmin, bmax);
	mud = lrg_nav_bake_add_area (bake, "mud", 2, 3.0f);
	g_assert_cmpint (mud, ==, 0);
	g_assert_cmpint (lrg_nav_bake_add_area_box (bake, (guint) mud, amin, amax), ==, 0);
	g_assert_cmpint (lrg_nav_bake_add_area (bake, "door", 7, 1.0f), ==, 1);
	g_assert_cmpint (lrg_nav_bake_add_link (bake, ls, le, FALSE, 2.5f, "door"), ==, 0);
	g_assert_cmpint (lrg_nav_bake_add_link (bake, le, ls, TRUE, 0.0f, NULL), ==, 1);
	return bake;
}

static void
test_round_trip (void)
{
	g_autoptr(LrgNavBake) bake = full_settings ();
	g_autoptr(LrgNavBake) again = NULL;
	g_autoptr(LrgNavBake) copy = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *text = lrg_nav_bake_to_data (bake);
	g_autofree gchar     *text2 = NULL;
	g_autofree gchar     *text3 = NULL;
	const gchar          *area = NULL;
	gfloat                r, h, s, st;
	gfloat                mn[3];
	gfloat                mx[3];
	gboolean              bidi = TRUE;
	gfloat                cost = 0.0f;

	again = lrg_nav_bake_new_from_data (text, -1, &error);
	g_assert_no_error (error);
	text2 = lrg_nav_bake_to_data (again);
	g_assert_cmpstr (text, ==, text2);

	/* values survive */
	lrg_nav_bake_get_agent (again, &r, &h, &s, &st);
	g_assert_cmpfloat (r, ==, 0.4f);
	g_assert_cmpfloat (h, ==, 1.7f);
	g_assert_cmpfloat (s, ==, 45.0f);
	g_assert_cmpfloat (st, ==, 0.3f);
	g_assert_cmpfloat (lrg_nav_bake_get_cell_size (again), ==, 0.2f);
	g_assert_cmpfloat (lrg_nav_bake_get_cell_height (again), ==, 0.05f);
	g_assert_true (lrg_nav_bake_get_bounds (again, mn, mx));
	g_assert_cmpfloat (mx[1], ==, 20.0f);
	g_assert_cmpuint (g_strv_length ((gchar **) lrg_nav_bake_get_include_tags (again)), ==, 2);
	g_assert_cmpstr (lrg_nav_bake_get_exclude_tags (again)[0], ==, "no-nav");
	g_assert_cmpstr (lrg_nav_bake_get_physics_layers (again)[1], ==, "default");
	g_assert_cmpuint (lrg_nav_bake_get_n_areas (again), ==, 2);
	g_assert_cmpuint (lrg_nav_bake_get_n_area_boxes (again, 0), ==, 1);
	g_assert_true (lrg_nav_bake_get_area_box (again, 0, 0, mn, mx));
	g_assert_cmpfloat (mx[2], ==, 4.25f);
	g_assert_cmpuint (lrg_nav_bake_get_n_links (again), ==, 2);
	g_assert_true (lrg_nav_bake_get_link (again, 0, mn, mx, &bidi, &cost, &area));
	g_assert_false (bidi);
	g_assert_cmpfloat (cost, ==, 2.5f);
	g_assert_cmpstr (area, ==, "door");
	g_assert_cmpfloat (mn[2], ==, 0.1f);

	/* a copy writes the same text */
	copy = lrg_nav_bake_copy (again);
	text3 = lrg_nav_bake_to_data (copy);
	g_assert_cmpstr (text, ==, text3);
}

static void
test_unknown_keys (void)
{
	static const gchar *input =
		"version: 1\n"
		"editor_color: '#ff0000'\n"
		"agent:\n"
		"  radius: 0.5\n"
		"  gait: shuffle\n"
		"voxel:\n"
		"  cell_size: 0.3\n"
		"  note: coarse\n"
		"regions:\n"
		"  min_area: 1\n"
		"  keep: [1, 2]\n"
		"sources:\n"
		"  meshes: false\n"
		"  future_source: true\n"
		"areas:\n"
		"  - name: water\n"
		"    tag: 3\n"
		"    cost: 5\n"
		"    paint: blue\n"
		"links:\n"
		"  - start: [0, 0, 0]\n"
		"    end: [1, 0, 0]\n"
		"    anim: climb\n";
	g_autoptr(LrgNavBake) bake = NULL;
	g_autoptr(LrgNavBake) again = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *out = NULL;
	g_autofree gchar     *out2 = NULL;
	gboolean              meshes = TRUE;

	bake = lrg_nav_bake_new_from_data (input, -1, &error);
	g_assert_no_error (error);
	g_object_get (bake, "include-meshes", &meshes, NULL);
	g_assert_false (meshes);
	out = lrg_nav_bake_to_data (bake);
	g_assert_nonnull (strstr (out, "editor_color"));
	g_assert_nonnull (strstr (out, "gait: shuffle"));
	g_assert_nonnull (strstr (out, "note: coarse"));
	g_assert_nonnull (strstr (out, "keep"));
	g_assert_nonnull (strstr (out, "future_source: true"));
	g_assert_nonnull (strstr (out, "paint: blue"));
	g_assert_nonnull (strstr (out, "anim: climb"));

	/* and the saved text is stable */
	again = lrg_nav_bake_new_from_data (out, -1, &error);
	g_assert_no_error (error);
	out2 = lrg_nav_bake_to_data (again);
	g_assert_cmpstr (out, ==, out2);
}

static void
expect_error (const gchar *input,
              gint         code,
              const gchar *path)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(LrgNavBake) bake = lrg_nav_bake_new_from_data (input, -1, &error);

	g_assert_null (bake);
	g_assert_error (error, LRG_NAV_BAKE_ERROR, code);
	if (path != NULL)
		g_assert_nonnull (strstr (error->message, path));
}

static void
test_errors (void)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(LrgNavBake) none = NULL;
	g_autofree gchar     *dir = NULL;
	g_autofree gchar     *path = NULL;

	expect_error ("- a list\n", LRG_NAV_BAKE_ERROR_PARSE, NULL);
	expect_error ("version: 99\n", LRG_NAV_BAKE_ERROR_PARSE, "version");
	expect_error ("agent:\n  radius: wide\n", LRG_NAV_BAKE_ERROR_PARSE, "agent.radius");
	expect_error ("agent:\n  max_slope: 120\n", LRG_NAV_BAKE_ERROR_INVALID, "agent.max_slope");
	expect_error ("voxel: 3\n", LRG_NAV_BAKE_ERROR_PARSE, "voxel");
	expect_error ("sources:\n  layer_mask: -1\n", LRG_NAV_BAKE_ERROR_PARSE, "sources.layer_mask");
	expect_error ("sources:\n  include_tags: [a, '']\n", LRG_NAV_BAKE_ERROR_PARSE, "sources.include_tags[1]");
	expect_error ("sources:\n  bounds: {min: [0, 0], max: [1, 1, 1]}\n", LRG_NAV_BAKE_ERROR_PARSE,
	              "sources.bounds.min");
	expect_error ("areas:\n  - name: mud\n    tag: 2\n    boxes:\n      - {min: [0, 0, 0], max: [1, 1, x]}\n",
	              LRG_NAV_BAKE_ERROR_PARSE, "areas[0].boxes[0].max[2]");
	expect_error ("areas:\n  - name: mud\n    tag: 2\n  - name: mud\n    tag: 3\n",
	              LRG_NAV_BAKE_ERROR_INVALID, "areas[1]");
	expect_error ("areas:\n  - name: mud\n    tag: 0\n", LRG_NAV_BAKE_ERROR_INVALID, "areas[0]");
	expect_error ("links:\n  - start: [0, 0, 0]\n    end: [1, 0, 0]\n    area: lava\n",
	              LRG_NAV_BAKE_ERROR_INVALID, "links[0]");
	expect_error ("links:\n  - start: [0, 0, 0]\n", LRG_NAV_BAKE_ERROR_PARSE, "links[0]");

	/* files: IO errors carry the path */
	dir = g_dir_make_tmp ("nav-bake-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "missing.rnav", NULL);
	none = lrg_nav_bake_new_from_file (path, &error);
	g_assert_null (none);
	g_assert_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_IO);
	g_clear_error (&error);
	{
		g_autoptr(LrgNavBake) bake = full_settings ();
		g_autoptr(LrgNavBake) loaded = NULL;

		g_assert_true (lrg_nav_bake_save_to_file (bake, path, &error));
		g_assert_no_error (error);
		loaded = lrg_nav_bake_new_from_file (path, &error);
		g_assert_no_error (error);
		g_assert_cmpstr (lrg_nav_bake_get_name (loaded), ==, "test level");
		g_unlink (path);
	}
	g_rmdir (dir);

	/* API validation */
	{
		g_autoptr(LrgNavBake) bake = lrg_nav_bake_new ();
		const gfloat          a[3] = { 0.0f, 0.0f, 0.0f };

		g_assert_cmpint (lrg_nav_bake_add_area (bake, "x", 0, 1.0f), ==, -1);
		g_assert_cmpint (lrg_nav_bake_add_area (bake, "x", 1, -1.0f), ==, -1);
		g_assert_cmpint (lrg_nav_bake_add_area (bake, "x", 1, 1.0f), ==, 0);
		g_assert_cmpint (lrg_nav_bake_add_area (bake, "y", 1, 1.0f), ==, -1);
		g_assert_cmpint (lrg_nav_bake_add_area_box (bake, 0, a, a), ==, -1);
		g_assert_cmpint (lrg_nav_bake_add_link (bake, a, a, TRUE, -1.0f, NULL), ==, -1);
		g_assert_cmpint (lrg_nav_bake_add_link (bake, a, a, TRUE, 0.0f, "nope"), ==, -1);
		g_assert_cmpint (lrg_nav_bake_add_link (bake, a, a, TRUE, 0.0f, "x"), ==, 0);
		g_assert_true (lrg_nav_bake_remove_area (bake, 0));
		g_assert_true (lrg_nav_bake_get_link (bake, 0, NULL, NULL, NULL, NULL, NULL));
		{
			const gchar *area = "set";

			lrg_nav_bake_get_link (bake, 0, NULL, NULL, NULL, NULL, &area);
			g_assert_null (area);
		}
		g_assert_true (lrg_nav_bake_remove_link (bake, 0));
		g_assert_false (lrg_nav_bake_remove_link (bake, 0));
	}
}

/* ------------------------------------------------------------------ */
/* Baking                                                               */

static void
test_determinism_and_cache (void)
{
	g_autoptr(GArray)              tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)          bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh)      a = NULL;
	g_autoptr(LrgNavPolyMesh)      b = NULL;
	g_autoptr(LrgNavPolyMesh)      c = NULL;
	g_autoptr(LrgNavPolyMeshCache) cache = lrg_nav_poly_mesh_cache_new (4);
	g_autoptr(GBytes)              ba = NULL;
	g_autoptr(GBytes)              bb = NULL;
	g_autoptr(GBytes)              bc = NULL;
	g_autoptr(GError)              error = NULL;

	floor_rect (tris, 0.0f, 0.0f, 20.0f, 20.0f, 0.0f);
	ramp (tris, 20.0f, 0.0f, 24.0f, 1.0f, 0.0f, 4.0f);
	floor_rect (tris, 24.0f, 0.0f, 30.0f, 4.0f, 1.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);

	a = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	b = lrg_nav_bake_bake (bake, NULL, cache, &error);
	g_assert_no_error (error);
	c = lrg_nav_bake_bake (bake, NULL, cache, &error);   /* a cache hit */
	g_assert_no_error (error);
	ba = lrg_nav_poly_mesh_save (a);
	bb = lrg_nav_poly_mesh_save (b);
	bc = lrg_nav_poly_mesh_save (c);
	g_assert_true (g_bytes_equal (ba, bb));
	g_assert_true (g_bytes_equal (ba, bc));
	g_assert_cmpuint (lrg_nav_bake_get_last_triangle_count (bake), ==, tris->len / 9);
	g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (a), ==, 1);

	/* nothing to bake */
	{
		g_autoptr(LrgNavBake)     empty = lrg_nav_bake_new ();
		g_autoptr(LrgNavPolyMesh) none = lrg_nav_bake_bake (empty, NULL, NULL, &error);

		g_assert_null (none);
		g_assert_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_NO_GEOMETRY);
	}
}

/* the floor, a ramp of @angle degrees up 2 m, a platform; reachable? */
static gboolean
ramp_reachable (gfloat angle,
                gfloat max_slope)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gfloat        *p = NULL;
	gfloat                    run = 2.0f / tanf (angle * (gfloat) G_PI / 180.0f);
	guint                     n = 0;

	floor_rect (tris, -6.0f, 0.0f, 0.0f, 4.0f, 0.0f);
	ramp (tris, 0.0f, 0.0f, run, 2.0f, 0.0f, 4.0f);
	floor_rect (tris, run, 0.0f, run + 6.0f, 4.0f, 2.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	lrg_nav_bake_set_agent (bake, 0.3f, 1.8f, max_slope, 0.45f);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	p = find (mesh, -3.0f, 0.0f, 2.0f, run + 3.0f, 2.0f, 2.0f, &n);
	return p != NULL;
}

/* stairs of @rise per step: reachable with @max_step? */
static gboolean
stairs_reachable (gfloat rise,
                  gfloat max_step)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gfloat        *p = NULL;
	guint                     n = 0;
	guint                     i;

	floor_rect (tris, -6.0f, 0.0f, 0.0f, 4.0f, 0.0f);
	for (i = 0; i < 6; i++)
		floor_rect (tris, (gfloat) i * 0.5f, 0.0f, (gfloat) (i + 1) * 0.5f, 4.0f, rise * (gfloat) (i + 1));
	floor_rect (tris, 3.0f, 0.0f, 9.0f, 4.0f, rise * 6.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	lrg_nav_bake_set_agent (bake, 0.3f, 1.8f, 50.0f, max_step);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	p = find (mesh, -3.0f, 0.0f, 2.0f, 6.0f, rise * 6.0f, 2.0f, &n);
	return p != NULL;
}

static void
test_slope_and_step (void)
{
	g_assert_true (ramp_reachable (30.0f, 50.0f));
	g_assert_false (ramp_reachable (60.0f, 50.0f));
	g_assert_true (ramp_reachable (60.0f, 70.0f));
	g_assert_true (stairs_reachable (0.25f, 0.45f));
	g_assert_false (stairs_reachable (0.25f, 0.15f));
}

static void
test_links (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgNavPolyMesh) linked = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gfloat        *none = NULL;
	g_autofree gfloat        *p = NULL;
	const gfloat              ls[3] = { 4.5f, 0.0f, 2.0f };
	const gfloat              le[3] = { 7.5f, 3.0f, 2.0f };
	guint                     n = 0;
	guint                     i;
	gboolean                  saw_start = FALSE;
	gboolean                  saw_end = FALSE;

	/* two floors, 3 m apart in height, with a gap */
	floor_rect (tris, 0.0f, 0.0f, 5.0f, 4.0f, 0.0f);
	floor_rect (tris, 7.0f, 0.0f, 12.0f, 4.0f, 3.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	none = find (mesh, 1.0f, 0.0f, 2.0f, 10.0f, 3.0f, 2.0f, &n);
	g_assert_null (none);

	/* a ladder link joins them */
	g_assert_cmpint (lrg_nav_bake_add_area (bake, "ladder", 9, 1.0f), ==, 0);
	g_assert_cmpint (lrg_nav_bake_add_link (bake, ls, le, TRUE, 1.0f, "ladder"), ==, 0);
	linked = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_nav_poly_mesh_get_link_count (linked), ==, 1);
	g_assert_true (lrg_nav_poly_mesh_get_link_connected (linked, 0));
	p = find (linked, 1.0f, 0.0f, 2.0f, 10.0f, 3.0f, 2.0f, &n);
	g_assert_nonnull (p);
	for (i = 0; i < n; i += 3)
	{
		if (fabsf (p[i] - ls[0]) < 0.3f && fabsf (p[i + 2] - ls[2]) < 0.3f && p[i + 1] < 0.5f)
			saw_start = TRUE;
		if (fabsf (p[i] - le[0]) < 0.3f && fabsf (p[i + 2] - le[2]) < 0.3f && p[i + 1] > 2.5f)
			saw_end = TRUE;
	}
	g_assert_true (saw_start && saw_end);
	g_clear_pointer (&p, g_free);

	/* the link carries the area's tag: switching it off closes the way */
	lrg_nav_poly_mesh_set_tag_enabled (linked, 9, FALSE);
	p = find (linked, 1.0f, 0.0f, 2.0f, 10.0f, 3.0f, 2.0f, &n);
	g_assert_null (p);

	/* configuring a mesh that already has links does not double them */
	lrg_nav_bake_configure (bake, linked);
	g_assert_cmpuint (lrg_nav_poly_mesh_get_link_count (linked), ==, 1);
}

static void
test_area_costs (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gfloat        *cheap = NULL;
	g_autofree gfloat        *dear = NULL;
	const gfloat              mmin[3] = { 8.0f, -1.0f, 0.0f };
	const gfloat              mmax[3] = { 12.0f, 1.0f, 14.0f };
	guint                     n1 = 0;
	guint                     n2 = 0;
	gint                      mud;

	/* a 20 x 20 floor; a mud strip across the straight line, open at the top */
	floor_rect (tris, 0.0f, 0.0f, 20.0f, 20.0f, 0.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	mud = lrg_nav_bake_add_area (bake, "mud", 2, 1.0f);
	lrg_nav_bake_add_area_box (bake, (guint) mud, mmin, mmax);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	cheap = find (mesh, 2.0f, 0.0f, 5.0f, 18.0f, 0.0f, 5.0f, &n1);
	g_assert_nonnull (cheap);
	g_assert_cmpfloat (path_length (cheap, n1), <, 16.5f);

	/* mud costs 10: the path walks around the strip */
	g_assert_true (lrg_nav_bake_set_area_cost (bake, (guint) mud, 10.0f));
	lrg_nav_bake_apply_costs (bake, mesh);
	g_assert_cmpfloat (lrg_nav_poly_mesh_get_tag_cost (mesh, 2), ==, 10.0f);
	dear = find (mesh, 2.0f, 0.0f, 5.0f, 18.0f, 0.0f, 5.0f, &n2);
	g_assert_nonnull (dear);
	g_assert_cmpfloat (path_length (dear, n2), >, 20.0f);

	/* back to 1: the table is emptied */
	lrg_nav_poly_mesh_set_tag_cost (mesh, 2, 1.0f);
	g_assert_cmpfloat (lrg_nav_poly_mesh_get_tag_cost (mesh, 2), ==, 1.0f);
}

static void
test_min_region_area (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         error = NULL;
	const gfloat              on_table[3] = { 21.0f, 1.0f, 1.0f };
	const gfloat              ext[3] = { 0.5f, 0.5f, 0.5f };
	guint                     i;
	guint                     disabled = 0;

	floor_rect (tris, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f);
	floor_rect (tris, 20.0f, 0.0f, 22.0f, 2.0f, 1.0f);    /* a 2 x 2 table top */
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 2);
	g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, on_table, ext, NULL, NULL));

	g_object_set (bake, "min-region-area", 4.0f, NULL);
	g_assert_true (lrg_nav_bake_bake_into (bake, mesh, NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, on_table, ext, NULL, NULL));
	for (i = 0; i < lrg_nav_poly_mesh_get_polygon_count (mesh); i++)
		if (!lrg_nav_poly_mesh_get_polygon_enabled (mesh, i))
			disabled++;
	g_assert_cmpuint (disabled, >, 0);
}

/* ------------------------------------------------------------------ */
/* Baking from a world                                                  */

static LrgEntity3D *
add_floor (LrgWorld3D  *world,
           const gchar *name,
           gfloat       size,
           gfloat       x,
           gfloat       y,
           gfloat       z)
{
	LrgEntity3D              *e = lrg_entity3d_new_with_name (name);
	LrgMeshRendererComponent *mr = lrg_mesh_renderer_component_new_primitive (LRG_PRIMITIVE_PLANE);

	g_object_set (mr, "size-x", size, "size-z", size, NULL);
	lrg_entity3d_add_component (e, LRG_COMPONENT (mr));
	g_object_unref (mr);
	lrg_entity3d_set_position_xyz (e, x, y, z);
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	return e;
}

static LrgEntity3D *
add_box_collider (LrgWorld3D *world,
                  gfloat      x,
                  gfloat      z,
                  gfloat      hx,
                  gfloat      hz)
{
	LrgEntity3D            *e = lrg_entity3d_new_with_name ("pillar");
	LrgColliderComponent3D *col = lrg_collider_component3d_new_box (hx, 1.5f, hz);

	lrg_entity3d_add_component (e, LRG_COMPONENT (col));
	g_object_unref (col);
	lrg_entity3d_set_position_xyz (e, x, 1.5f, z);
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	return e;
}

static void
test_bake_world (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         error = NULL;
	g_autofree gfloat        *p = NULL;
	const gfloat              in_pillar[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat              ext[3] = { 0.1f, 0.5f, 0.1f };
	LrgEntity3D              *floor_e;
	LrgEntity3D              *pillar;
	guint                     n = 0;

	floor_e = add_floor (world, "floor", 20.0f, 0.0f, 0.0f, 0.0f);
	lrg_entity3d_add_tag (floor_e, "floor");
	pillar = add_box_collider (world, 0.0f, 0.0f, 1.0f, 1.0f);

	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (mesh), >, 1);
	/* the pillar's footprint is not walkable, its top is too small after erosion? it is 2 x 2: walkable */
	g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, in_pillar, ext, NULL, NULL));
	p = find (mesh, -5.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, &n);
	g_assert_nonnull (p);
	g_assert_cmpfloat (path_length (p, n), >, 10.2f);    /* around the pillar */
	g_clear_pointer (&p, g_free);
	g_clear_object (&mesh);

	/* exclude tags: the pillar tagged no-nav is ignored, the way is straight */
	lrg_entity3d_add_tag (pillar, "no-nav");
	{
		const gchar *const exc[] = { "no-nav", NULL };

		lrg_nav_bake_set_exclude_tags (bake, exc);
	}
	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_no_error (error);
	p = find (mesh, -5.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, &n);
	g_assert_nonnull (p);
	g_assert_cmpfloat (path_length (p, n), <, 10.1f);
	g_clear_pointer (&p, g_free);
	g_clear_object (&mesh);
	lrg_nav_bake_set_exclude_tags (bake, NULL);

	/* a dynamic body never counts */
	{
		LrgRigidBodyComponent *rb = lrg_rigid_body_component_new (LRG_DYNAMICS_BODY_DYNAMIC);

		lrg_entity3d_add_component (pillar, LRG_COMPONENT (rb));
		g_object_unref (rb);
	}
	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_no_error (error);
	p = find (mesh, -5.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, &n);
	g_assert_cmpfloat (path_length (p, n), <, 10.1f);
	g_clear_pointer (&p, g_free);
	g_clear_object (&mesh);

	/* include tags: only "floor" entities */
	{
		const gchar *const inc[] = { "ground", NULL };

		lrg_nav_bake_set_include_tags (bake, inc);
		mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
		g_assert_null (mesh);
		g_assert_error (error, LRG_NAV_BAKE_ERROR, LRG_NAV_BAKE_ERROR_NO_GEOMETRY);
		g_clear_error (&error);
	}
	{
		const gchar *const inc[] = { "floor", NULL };

		lrg_nav_bake_set_include_tags (bake, inc);
		mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
		g_assert_no_error (error);
		g_assert_cmpuint (lrg_nav_bake_get_last_triangle_count (bake), ==, 2);
		g_clear_object (&mesh);
	}

	/* static-only: nothing is flagged static yet */
	lrg_nav_bake_set_include_tags (bake, NULL);
	g_object_set (bake, "static-only", TRUE, NULL);
	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_null (mesh);
	g_clear_error (&error);
	lrg_entity3d_set_static (floor_e, TRUE);
	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_no_error (error);
	g_clear_object (&mesh);
}

static void
test_bake_world_plane_and_bounds (void)
{
	g_autoptr(LrgWorld3D)     world = lrg_world3d_new ();
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(GError)         error = NULL;
	LrgEntity3D              *ground = lrg_entity3d_new_with_name ("ground");
	LrgColliderComponent3D   *plane = lrg_collider_component3d_new (LRG_COLLIDER3D_PLANE);
	const gfloat              bmin[3] = { -8.0f, -1.0f, -6.0f };
	const gfloat              bmax[3] = { 8.0f, 1.0f, 6.0f };
	const gfloat              inside[3] = { 7.0f, 0.0f, 5.0f };
	const gfloat              outside[3] = { 9.0f, 0.0f, 0.0f };
	const gfloat              ext[3] = { 0.3f, 0.5f, 0.3f };
	gfloat                    mn[3];
	gfloat                    mx[3];
	guint                     i;

	lrg_entity3d_add_component (ground, LRG_COMPONENT (plane));
	g_object_unref (plane);
	lrg_world3d_add_entity (world, ground);
	g_object_unref (ground);

	/* an infinite plane alone: a 100 m square around the origin */
	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_no_error (error);
	mn[0] = mn[2] = G_MAXFLOAT;
	mx[0] = mx[2] = -G_MAXFLOAT;
	for (i = 0; i < lrg_nav_poly_mesh_get_polygon_count (mesh); i++)
	{
		gfloat a[3];
		gfloat b[3];

		lrg_nav_poly_mesh_get_polygon (mesh, i, a, b, NULL, NULL);
		mn[0] = MIN (mn[0], a[0]);
		mn[2] = MIN (mn[2], a[2]);
		mx[0] = MAX (mx[0], b[0]);
		mx[2] = MAX (mx[2], b[2]);
	}
	g_assert_cmpfloat (mx[0] - mn[0], >, 98.0f);
	g_assert_cmpfloat (mx[0] - mn[0], <, 100.5f);
	g_clear_object (&mesh);

	/* with bounds: clipped to them */
	g_object_set (bake, "cell-size", 0.25f, NULL);
	lrg_nav_bake_set_bounds (bake, bmin, bmax);
	mesh = lrg_nav_bake_bake (bake, world, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, inside, ext, NULL, NULL));
	g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, outside, ext, NULL, NULL));
}

/* ------------------------------------------------------------------ */
/* Boundaries and debug draw                                            */

static void
test_boundaries_and_debug_draw (void)
{
	g_autoptr(GArray)         tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgNavPolyMesh) mesh = NULL;
	g_autoptr(LrgDebugDraw)   draw = lrg_debug_draw_new ();
	g_autoptr(GError)         error = NULL;
	g_autofree gfloat        *all = NULL;
	g_autofree gfloat        *near = NULL;
	const gfloat              corner[3] = { 1.0f, 0.0f, 1.0f };
	const gfloat              ls[3] = { 2.0f, 0.0f, 2.0f };
	const gfloat              le[3] = { 8.0f, 0.0f, 8.0f };
	guint                     n = 0;
	guint                     i;
	gfloat                    perimeter = 0.0f;
	gfloat                    c[12];

	floor_rect (tris, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f);
	lrg_nav_bake_add_triangles (bake, (const gfloat *) (gpointer) tris->data, tris->len, TRUE);
	lrg_nav_bake_add_link (bake, ls, le, TRUE, 0.0f, NULL);
	mesh = lrg_nav_bake_bake (bake, NULL, NULL, &error);
	g_assert_no_error (error);

	/* a square floor: four walls after merging, around the eroded square */
	g_assert_cmpuint (lrg_nav_poly_mesh_get_boundary_count (mesh), ==, 4);
	all = lrg_nav_poly_mesh_get_boundary_segments (mesh, NULL, 0.0f, 0.0f, &n);
	g_assert_cmpuint (n, ==, 24);
	for (i = 0; i < n; i += 6)
	{
		gfloat dx = all[i + 3] - all[i];
		gfloat dz = all[i + 5] - all[i + 2];
		/* walkable on the right: the floor's centre */
		gfloat cx = 5.0f - all[i];
		gfloat cz = 5.0f - all[i + 2];

		perimeter += sqrtf (dx * dx + dz * dz);
		g_assert_cmpfloat (dx * cz - dz * cx, <, 0.0f);
	}
	g_assert_cmpfloat (fabsf (perimeter - 4.0f * 9.0f), <, 0.6f);   /* eroded by 2 cells of 0.25 */

	/* near a corner: two walls */
	near = lrg_nav_poly_mesh_get_boundary_segments (mesh, corner, 1.0f, 1.0f, &n);
	g_assert_cmpuint (n, ==, 12);

	/* corners and serial */
	g_assert_true (lrg_nav_poly_mesh_get_polygon_corners (mesh, 0, c));
	g_assert_false (lrg_nav_poly_mesh_get_polygon_corners (mesh, 9999, c));
	{
		guint serial = lrg_nav_poly_mesh_get_serial (mesh);

		g_assert_true (lrg_nav_bake_bake_into (bake, mesh, NULL, NULL, &error));
		g_assert_cmpuint (lrg_nav_poly_mesh_get_serial (mesh), !=, serial);
	}
	{
		gfloat   s[3];
		gfloat   e[3];
		gboolean bidi = FALSE;
		gboolean on = FALSE;

		g_assert_true (lrg_nav_poly_mesh_get_link (mesh, 0, s, e, &bidi, &on, NULL));
		g_assert_true (bidi && on);
		g_assert_false (lrg_nav_poly_mesh_get_link (mesh, 1, s, e, NULL, NULL, NULL));
	}

	/* debug draw records lines and solid slabs in the navigation category */
	lrg_nav_poly_mesh_debug_draw (mesh, draw, LRG_NAV_DEBUG_ALL);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (draw, TRUE), >, 8);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (draw, TRUE), >, 0);
	lrg_debug_draw_end_frame (draw);
	lrg_debug_draw_set_category_enabled (draw, LRG_DEBUG_CATEGORY_NAVIGATION, FALSE);
	lrg_nav_poly_mesh_debug_draw (mesh, draw, LRG_NAV_DEBUG_ALL);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (draw, TRUE), ==, 0);
	g_assert_true (g_type_is_a (LRG_TYPE_NAV_DEBUG_FLAGS, G_TYPE_FLAGS));
	g_assert_true (g_type_is_a (LRG_TYPE_NAV_BAKE_ERROR, G_TYPE_ENUM));
}

/* ------------------------------------------------------------------ */
/* The level's navigation: block                                        */

static void
test_level_block (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = lrg_level_new ("nav");
	g_autoptr(LrgLevel)           again = NULL;
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *text = NULL;
	g_autofree gchar             *text2 = NULL;

	g_assert_false (lrg_level_has_navigation (level));
	text = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_null (strstr (text, "navigation"));
	g_clear_pointer (&text, g_free);

	lrg_level_set_nav_settings_ref (level, "nav/level.rnav");
	lrg_level_set_nav_data_ref (level, "guid:0123456789abcdef");
	lrg_level_set_nav_bake_on_load (level, TRUE);
	g_assert_true (lrg_level_has_navigation (level));
	text = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_nonnull (strstr (text, "navigation:"));
	again = lrg_level_serializer_load_from_data (ser, text, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_level_get_nav_settings_ref (again), ==, "nav/level.rnav");
	g_assert_cmpstr (lrg_level_get_nav_data_ref (again), ==, "guid:0123456789abcdef");
	g_assert_true (lrg_level_get_nav_bake_on_load (again));
	text2 = lrg_level_serializer_save_to_data (ser, again, NULL);
	g_assert_cmpstr (text, ==, text2);

	lrg_level_clear_navigation (again);
	g_assert_false (lrg_level_has_navigation (again));
	lrg_level_set_nav_settings_ref (again, "");
	g_assert_null (lrg_level_get_nav_settings_ref (again));

	/* strict errors name the key */
	{
		static const gchar *bad[] = {
			"name: x\nnavigation:\n  settings: [a]\n",
			"name: x\nnavigation:\n  bake_on_load: maybe\n",
			"name: x\nnavigation:\n  settngs: a.rnav\n",
			"name: x\nnavigation: 3\n",
		};
		static const gchar *keys[] = { "navigation.settings", "navigation.bake_on_load", "navigation.settngs",
		                               "navigation" };
		guint i;

		for (i = 0; i < G_N_ELEMENTS (bad); i++)
		{
			g_autoptr(LrgLevel) broken = lrg_level_serializer_load_from_data (ser, bad[i], -1, &error);

			g_assert_null (broken);
			g_assert_nonnull (error);
			g_assert_nonnull (strstr (error->message, keys[i]));
			g_clear_error (&error);
		}
	}
}

/* a level with a 20 x 20 floor primitive and a `navigation:` block */
static LrgLevel *
floor_level (void)
{
	LrgLevel                 *level = lrg_level_new ("nav");
	g_autoptr(LrgNode)        node = lrg_node_new ("floor");
	g_autoptr(LrgNodeVisual)  visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);

	lrg_node_visual_set_primitive (visual, LRG_PRIMITIVE_PLANE);
	lrg_node_set_visual (node, visual);
	lrg_node_set_scale_xyz (node, 20.0f, 1.0f, 20.0f);
	lrg_level_add_node (level, node, NULL);
	return level;
}

static void
test_level_instantiate (void)
{
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *dir = g_dir_make_tmp ("nav-level-XXXXXX", &error);
	g_autofree gchar         *rnav = NULL;
	g_autofree gchar         *data = NULL;
	g_autoptr(LrgNavBake)     bake = lrg_nav_bake_new ();
	g_autoptr(LrgLevel)       level = floor_level ();
	gint                      mud;
	const gfloat              mmin[3] = { -2.0f, -1.0f, -2.0f };
	const gfloat              mmax[3] = { 2.0f, 1.0f, 2.0f };

	g_assert_no_error (error);
	rnav = g_build_filename (dir, "level.rnav", NULL);
	data = g_build_filename (dir, "level.rnavmesh", NULL);
	mud = lrg_nav_bake_add_area (bake, "mud", 4, 6.0f);
	lrg_nav_bake_add_area_box (bake, (guint) mud, mmin, mmax);
	g_assert_true (lrg_nav_bake_save_to_file (bake, rnav, &error));

	/* bake on load: the world gets a mesh baked from the floor, with costs */
	lrg_level_set_nav_settings_ref (level, "level.rnav");
	lrg_level_set_nav_bake_on_load (level, TRUE);
	{
		g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
		LrgNavPolyMesh       *mesh;
		g_autoptr(GBytes)     bytes = NULL;

		g_assert_true (lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, &error));
		g_assert_no_error (error);
		mesh = lrg_world3d_get_navigation (world);
		g_assert_nonnull (mesh);
		g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (mesh), >, 1);
		g_assert_cmpfloat (lrg_nav_poly_mesh_get_tag_cost (mesh, 4), ==, 6.0f);
		bytes = lrg_nav_poly_mesh_save (mesh);
		g_assert_true (g_file_set_contents (data, g_bytes_get_data (bytes, NULL),
		                                    (gssize) g_bytes_get_size (bytes), &error));
	}

	/* baked data: loaded (and given the settings' costs) */
	lrg_level_set_nav_bake_on_load (level, FALSE);
	lrg_level_set_nav_data_ref (level, "level.rnavmesh");
	{
		g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
		LrgNavPolyMesh       *mesh;

		g_assert_true (lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, &error));
		mesh = lrg_world3d_get_navigation (world);
		g_assert_nonnull (mesh);
		g_assert_cmpfloat (lrg_nav_poly_mesh_get_bake_seconds (mesh), ==, 0.0);
		g_assert_cmpfloat (lrg_nav_poly_mesh_get_tag_cost (mesh, 4), ==, 6.0f);
	}

	/* a missing data file warns and leaves the world without navigation */
	lrg_level_set_nav_data_ref (level, "missing.rnavmesh");
	{
		g_autoptr(LrgWorld3D) world = lrg_world3d_new ();

		g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*navigation data*missing.rnavmesh*");
		g_assert_true (lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, &error));
		g_test_assert_expected_messages ();
		g_assert_null (lrg_world3d_get_navigation (world));
	}
	g_unlink (rnav);
	g_unlink (data);
	g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/nav-bake/format/round-trip", test_round_trip);
	g_test_add_func ("/nav-bake/format/unknown-keys", test_unknown_keys);
	g_test_add_func ("/nav-bake/format/errors", test_errors);
	g_test_add_func ("/nav-bake/bake/determinism-and-cache", test_determinism_and_cache);
	g_test_add_func ("/nav-bake/bake/slope-and-step", test_slope_and_step);
	g_test_add_func ("/nav-bake/bake/links", test_links);
	g_test_add_func ("/nav-bake/bake/area-costs", test_area_costs);
	g_test_add_func ("/nav-bake/bake/min-region-area", test_min_region_area);
	g_test_add_func ("/nav-bake/world/meshes-colliders-filters", test_bake_world);
	g_test_add_func ("/nav-bake/world/plane-and-bounds", test_bake_world_plane_and_bounds);
	g_test_add_func ("/nav-bake/boundaries-and-debug-draw", test_boundaries_and_debug_draw);
	g_test_add_func ("/nav-bake/level/block", test_level_block);
	g_test_add_func ("/nav-bake/level/instantiate", test_level_instantiate);

	return g_test_run ();
}
