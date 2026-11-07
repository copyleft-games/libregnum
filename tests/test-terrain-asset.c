/* test-terrain-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests of the 0.9.0 terrain family: LrgTerrainAsset (.rterrain format,
 * sidecars, sampling, chunk meshes, splat bake, heightfield shape, ray
 * pick, edits), LrgTerrainBrush (every op against expected values,
 * splat normalisation, strokes and undo), LrgFoliage (determinism,
 * density and placement rules, dirty patches), LrgWaterBody (.rwater
 * format, outlines, surface building) and the terrain / foliage / water
 * components (headless render items, LOD, physics body).
 *
 * GPU (only under an isolated X display, build-aux/xrun.sh): the whole
 * family drawn by LrgWorld3DRenderer.  With LRG_TERRAIN_SHOT=<file.png>
 * the frame is rendered at 1920x1080 and written there.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <libregnum.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A 2 x 2 chunk terrain of 8 cells per chunk, 16 units per chunk (2-unit
 * cells, 17 x 17 samples), three layers. */
static LrgTerrainAsset *
small_terrain (void)
{
	LrgTerrainAsset *t = lrg_terrain_asset_new (8, 16.0f, 2, 2);
	LrgTerrainLayer *l;

	lrg_terrain_asset_set_name (t, "small");
	l = lrg_terrain_asset_get_layer (t, 0);
	lrg_terrain_layer_set_name (l, "grass");
	lrg_terrain_layer_set_color (l, 0.25f, 0.5f, 0.2f, 1.0f);
	l = lrg_terrain_asset_add_layer (t, "rock");
	lrg_terrain_layer_set_color (l, 0.5f, 0.5f, 0.5f, 1.0f);
	lrg_terrain_layer_set_tiling (l, 8.0f);
	l = lrg_terrain_asset_add_layer (t, "sand");
	lrg_terrain_layer_set_color (l, 0.8f, 0.7f, 0.5f, 1.0f);
	return t;
}

/* Fills the heights with a gentle analytic slope h = 0.25 x + 0.1 z. */
static void
fill_slope (LrgTerrainAsset *t)
{
	g_autofree gfloat *h = NULL;
	guint              nx, nz, i, j;
	gfloat             cell = lrg_terrain_asset_get_cell_size (t);

	lrg_terrain_asset_get_samples (t, &nx, &nz);
	h = g_new (gfloat, nx * nz);
	for (j = 0; j < nz; j++)
		for (i = 0; i < nx; i++)
			h[j * nx + i] = 0.25f * (gfloat) i * cell + 0.1f * (gfloat) j * cell;
	g_assert_true (lrg_terrain_asset_set_heights (t, h, nx * nz, NULL));
}

static gchar *
make_tmp_dir (void)
{
	g_autoptr(GError) error = NULL;
	gchar            *dir = g_dir_make_tmp ("lrg-terrain-XXXXXX", &error);

	g_assert_no_error (error);
	return dir;
}

/* Removes every file in @dir, then @dir. */
static void
remove_dir (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		g_unlink (p);
	}
	g_rmdir (dir);
}

static gchar *
read_file (const gchar *path)
{
	gchar *contents = NULL;

	g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
	return contents;
}

static gfloat
weight_sum (LrgTerrainAsset *t,
            guint            x,
            guint            z)
{
	guint  l;
	gfloat s = 0.0f;

	for (l = 0; l < lrg_terrain_asset_get_n_layers (t); l++)
		s += lrg_terrain_asset_get_weight (t, x, z, l);
	return s;
}

/* ==========================================================================
 * LrgTerrainAsset
 * ========================================================================== */

static void
test_asset_new (void)
{
	g_autoptr(LrgTerrainAsset) t = lrg_terrain_asset_new (8, 16.0f, 2, 3);
	guint                      nx, nz, cx, cz;
	gfloat                     w, d;

	lrg_terrain_asset_get_samples (t, &nx, &nz);
	lrg_terrain_asset_get_cells (t, &cx, &cz);
	lrg_terrain_asset_get_size (t, &w, &d);
	g_assert_cmpuint (nx, ==, 17);
	g_assert_cmpuint (nz, ==, 25);
	g_assert_cmpuint (cx, ==, 16);
	g_assert_cmpuint (cz, ==, 24);
	g_assert_cmpfloat_with_epsilon (w, 32.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (d, 48.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_cell_size (t), 2.0f, 1e-6f);
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (t), ==, 1);
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (t, 3, 3, 0), ==, 1.0f);
	g_assert_cmpuint (lrg_terrain_asset_get_lod_levels (t), ==, 3);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_lod_distance (t, 1), 32.0f, 1e-6f);
	g_assert_cmpuint (lrg_terrain_asset_select_lod (t, 10.0f), ==, 0);
	g_assert_cmpuint (lrg_terrain_asset_select_lod (t, 40.0f), ==, 1);
	g_assert_cmpuint (lrg_terrain_asset_select_lod (t, 1000.0f), ==, 2);

	/* clamping: resolution and chunk counts */
	{
		g_autoptr(LrgTerrainAsset) big = lrg_terrain_asset_new (4096, -1.0f, 0, 9999);

		g_assert_cmpuint (lrg_terrain_asset_get_resolution (big), ==, LRG_TERRAIN_ASSET_MAX_RESOLUTION);
		g_assert_cmpfloat (lrg_terrain_asset_get_chunk_size (big), >, 0.0f);
		lrg_terrain_asset_get_chunks (big, &cx, &cz);
		g_assert_cmpuint (cx, ==, 1);
		g_assert_cmpuint (cz, ==, LRG_TERRAIN_ASSET_MAX_CHUNKS);
	}
}

static void
test_asset_sample (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	gfloat                     h, n[3];

	fill_slope (t);
	g_assert_true (lrg_terrain_asset_sample_height (t, 5.0f, 7.0f, &h));
	g_assert_cmpfloat_with_epsilon (h, 0.25f * 5.0f + 0.1f * 7.0f, 1e-4f);
	g_assert_false (lrg_terrain_asset_sample_height (t, -1.0f, 3.0f, &h));
	g_assert_false (lrg_terrain_asset_sample_height (t, 3.0f, 33.0f, &h));

	/* the normal of a plane h = a x + b z is (-a, 1, -b) / |...| */
	lrg_terrain_asset_sample_normal (t, 9.0f, 9.0f, n);
	g_assert_cmpfloat_with_epsilon (n[0], -0.25f / sqrtf (1.0725f), 1e-4f);
	g_assert_cmpfloat_with_epsilon (n[1], 1.0f / sqrtf (1.0725f), 1e-4f);
	g_assert_cmpfloat_with_epsilon (n[2], -0.1f / sqrtf (1.0725f), 1e-4f);

	/* a hole cell is not ground */
	lrg_terrain_asset_set_hole (t, 2, 3, TRUE);
	g_assert_false (lrg_terrain_asset_sample_height (t, 5.0f, 7.0f, NULL));
	g_assert_cmpuint (lrg_terrain_asset_get_hole_count (t), ==, 1);
}

/* Builds the asset the round-trip tests save: slope, painted weights,
 * holes, ids, LOD, a foliage layer with a painted density. */
static LrgTerrainAsset *
rich_terrain (void)
{
	LrgTerrainAsset *t = small_terrain ();
	LrgFoliageLayer *fl;
	gfloat           w[3];
	guint            i;

	fill_slope (t);
	for (i = 0; i < 17; i++)
	{
		w[0] = 1.0f;
		w[1] = (gfloat) i / 16.0f;
		w[2] = i == 3 ? 2.0f : 0.0f;
		lrg_terrain_asset_set_weights (t, i, 4, w, 3);
	}
	lrg_terrain_asset_set_hole (t, 5, 5, TRUE);
	lrg_terrain_asset_set_hole (t, 6, 5, TRUE);
	lrg_terrain_asset_set_cell_material (t, 1, 1, 2);
	lrg_terrain_asset_set_lod_levels (t, 2);
	lrg_terrain_asset_set_lod_distance (t, 1, 40.0f);
	lrg_terrain_asset_set_skirt_depth (t, 1.5f);
	lrg_terrain_layer_set_texture (lrg_terrain_asset_get_layer (t, 1), "textures/rock.png");
	lrg_terrain_layer_set_material (lrg_terrain_asset_get_layer (t, 1), "materials/rock.rmat");

	fl = lrg_foliage_layer_new ("pines", LRG_FOLIAGE_KIND_MESH);
	lrg_foliage_layer_set_mesh (fl, "builtin:conifer");
	lrg_foliage_layer_set_lod_mesh (fl, "builtin:shrub", 50.0f);
	lrg_foliage_layer_set_density (fl, 0.05f);
	lrg_foliage_layer_set_material_density (fl, 2, 0.0f);
	lrg_foliage_layer_set_height_range (fl, 1.0f, 100.0f);
	g_assert_cmpint (lrg_foliage_add_layer (lrg_terrain_asset_get_foliage (t), fl), ==, 0);
	g_object_unref (fl);
	lrg_foliage_set_seed (lrg_terrain_asset_get_foliage (t), 42);
	lrg_foliage_set_density (lrg_terrain_asset_get_foliage (t), 0, 3, 3, 0.25f);
	return t;
}

static void
test_asset_round_trip (void)
{
	g_autofree gchar           *dir = make_tmp_dir ();
	g_autofree gchar           *path = g_build_filename (dir, "isle.rterrain", NULL);
	g_autofree gchar           *heights1 = NULL, *heights2 = NULL, *splat1 = NULL, *splat2 = NULL;
	g_autofree gchar           *text1 = NULL, *text2 = NULL, *text3 = NULL;
	g_autofree gchar           *hp = g_build_filename (dir, "isle.heights.png", NULL);
	g_autofree gchar           *sp = g_build_filename (dir, "isle.splat0.png", NULL);
	g_autofree gchar           *hop = g_build_filename (dir, "isle.holes.png", NULL);
	g_autofree gchar           *fp = g_build_filename (dir, "isle.foliage0.png", NULL);
	g_autoptr(LrgTerrainAsset)  t = rich_terrain ();
	g_autoptr(LrgTerrainAsset)  back = NULL;
	g_autoptr(GError)           error = NULL;
	gsize                       l1, l2;
	guint                       i, j, l;
	gfloat                      lo, hi;

	g_assert_true (lrg_terrain_asset_save_to_file (t, path, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (hp, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (sp, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (hop, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (fp, G_FILE_TEST_EXISTS));
	text1 = read_file (path);
	g_assert_nonnull (strstr (text1, "file: isle.heights.png"));
	g_assert_nonnull (strstr (text1, "mesh: builtin:conifer"));

	back = lrg_terrain_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (back);

	/* fields */
	g_assert_cmpstr (lrg_terrain_asset_get_name (back), ==, "small");
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (back), ==, 3);
	g_assert_cmpstr (lrg_terrain_layer_get_texture (lrg_terrain_asset_get_layer (back, 1)), ==, "textures/rock.png");
	g_assert_cmpstr (lrg_terrain_layer_get_material (lrg_terrain_asset_get_layer (back, 1)), ==,
	                 "materials/rock.rmat");
	g_assert_cmpfloat (lrg_terrain_layer_get_tiling (lrg_terrain_asset_get_layer (back, 1)), ==, 8.0f);
	g_assert_cmpuint (lrg_terrain_asset_get_lod_levels (back), ==, 2);
	g_assert_cmpfloat (lrg_terrain_asset_get_lod_distance (back, 1), ==, 40.0f);
	g_assert_cmpfloat (lrg_terrain_asset_get_skirt_depth (back), ==, 1.5f);
	g_assert_true (lrg_terrain_asset_get_hole (back, 5, 5));
	g_assert_true (lrg_terrain_asset_get_hole (back, 6, 5));
	g_assert_cmpuint (lrg_terrain_asset_get_hole_count (back), ==, 2);

	/* heights within one 16-bit step of the range */
	lrg_terrain_asset_get_height_range (back, &lo, &hi);
	for (j = 0; j < 17; j++)
		for (i = 0; i < 17; i++)
			g_assert_cmpfloat (fabsf (lrg_terrain_asset_get_height (back, i, j) -
			                          lrg_terrain_asset_get_height (t, i, j)), <=, (hi - lo) / 65535.0f + 1e-5f);

	/* weights exactly (bytes) */
	for (i = 0; i < 17; i++)
		for (l = 0; l < 3; l++)
			g_assert_cmpfloat (lrg_terrain_asset_get_weight (back, i, 4, l), ==,
			                   lrg_terrain_asset_get_weight (t, i, 4, l));

	/* foliage */
	g_assert_cmpuint (lrg_foliage_get_n_layers (lrg_terrain_asset_get_foliage (back)), ==, 1);
	g_assert_cmpuint (lrg_foliage_get_seed (lrg_terrain_asset_get_foliage (back)), ==, 42);
	g_assert_cmpfloat_with_epsilon (lrg_foliage_get_density (lrg_terrain_asset_get_foliage (back), 0, 3, 3), 0.25f,
	                                0.003f);
	g_assert_cmpfloat (lrg_foliage_layer_get_material_density (
	                       lrg_foliage_get_layer (lrg_terrain_asset_get_foliage (back), 0), 2), ==, 0.0f);

	/* byte-stable: to_data equals the file, saving again changes nothing */
	text2 = lrg_terrain_asset_to_data (back, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text1, ==, text2);
	g_assert_true (g_file_get_contents (hp, &heights1, &l1, NULL));
	g_assert_true (g_file_get_contents (sp, &splat1, NULL, NULL));
	g_assert_true (lrg_terrain_asset_save_to_file (back, path, &error));
	g_assert_no_error (error);
	text3 = read_file (path);
	g_assert_cmpstr (text1, ==, text3);
	g_assert_true (g_file_get_contents (hp, &heights2, &l2, NULL));
	g_assert_true (g_file_get_contents (sp, &splat2, NULL, NULL));
	g_assert_cmpuint (l1, ==, l2);
	g_assert_cmpint (memcmp (heights1, heights2, l1), ==, 0);
	g_assert_cmpstr (splat1, ==, splat2);

	remove_dir (dir);
}

static void
test_asset_round_trip_r32 (void)
{
	g_autofree gchar           *dir = make_tmp_dir ();
	g_autofree gchar           *path = g_build_filename (dir, "exact.rterrain", NULL);
	g_autoptr(LrgTerrainAsset)  t = small_terrain ();
	g_autoptr(LrgTerrainAsset)  back = NULL;
	g_autoptr(GError)           error = NULL;
	guint                       i, j;

	fill_slope (t);
	lrg_terrain_asset_set_height (t, 7, 7, 1234.56789f);
	lrg_terrain_asset_set_height_format (t, LRG_TERRAIN_HEIGHT_FORMAT_R32);
	lrg_terrain_asset_set_splat_mode (t, LRG_TERRAIN_SPLAT_MODE_IDS);
	lrg_terrain_asset_set_cell_material (t, 2, 2, 1);
	lrg_terrain_asset_set_cell_material (t, 3, 2, 2);
	g_assert_true (lrg_terrain_asset_save_to_file (t, path, &error));
	g_assert_no_error (error);
	back = lrg_terrain_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	for (j = 0; j < 17; j++)
		for (i = 0; i < 17; i++)
			g_assert_cmpfloat (lrg_terrain_asset_get_height (back, i, j), ==, lrg_terrain_asset_get_height (t, i, j));
	g_assert_cmpint (lrg_terrain_asset_get_splat_mode (back), ==, LRG_TERRAIN_SPLAT_MODE_IDS);
	g_assert_cmpuint (lrg_terrain_asset_get_cell_material (back, 2, 2), ==, 1);
	g_assert_cmpuint (lrg_terrain_asset_get_cell_material (back, 3, 2), ==, 2);
	g_assert_cmpuint (lrg_terrain_asset_get_cell_material (back, 4, 2), ==, 0);
	remove_dir (dir);
}

static const gchar *unknown_doc =
	"version: 1\n"
	"name: field\n"
	"grid:\n"
	"  resolution: 4\n"
	"  chunk_size: 8\n"
	"  chunks:\n"
	"  - 1\n"
	"  - 1\n"
	"  origin_hint: north\n"
	"heights:\n"
	"  file: field.heights.png\n"
	"  format: png16\n"
	"  range:\n"
	"  - 0\n"
	"  - 1\n"
	"layers:\n"
	"- name: base\n"
	"  tiling: 4\n"
	"  color:\n"
	"  - 1\n"
	"  - 1\n"
	"  - 1\n"
	"  - 1\n"
	"  roughness_hint: 0.8\n"
	"lod:\n"
	"  levels: 3\n"
	"  distances:\n"
	"  - 16\n"
	"  - 32\n"
	"  skirt: 0.8\n"
	"editor:\n"
	"  last_brush: raise\n";

static void
test_asset_unknown_keys (void)
{
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgTerrainAsset) t = lrg_terrain_asset_new_from_data (unknown_doc, -1, NULL, &error);
	g_autofree gchar          *out = NULL;
	g_autoptr(LrgTerrainAsset) again = NULL;
	g_autofree gchar          *out2 = NULL;

	g_assert_no_error (error);
	g_assert_nonnull (t);
	out = lrg_terrain_asset_to_data (t, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (out, "origin_hint: north"));
	g_assert_nonnull (strstr (out, "roughness_hint: 0.8"));
	g_assert_nonnull (strstr (out, "last_brush: raise"));
	g_assert_nonnull (strstr (out, "file: field.heights.png"));

	/* the writer's output is a fixed point */
	g_assert_cmpstr (out, ==, unknown_doc);
	again = lrg_terrain_asset_new_from_data (out, -1, NULL, &error);
	g_assert_no_error (error);
	out2 = lrg_terrain_asset_to_data (again, NULL, &error);
	g_assert_cmpstr (out, ==, out2);
}

static void
expect_error (const gchar *doc,
              const gchar *needle)
{
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgTerrainAsset) t = lrg_terrain_asset_new_from_data (doc, -1, NULL, &error);

	g_assert_null (t);
	g_assert_nonnull (error);
	g_assert_true (error->domain == LRG_TERRAIN_ASSET_ERROR);
	if (strstr (error->message, needle) == NULL)
		g_error ("expected \"%s\" in \"%s\"", needle, error->message);
}

static void
test_asset_errors (void)
{
	expect_error ("name: x\n", "<data>: /grid: missing");
	expect_error ("grid:\n  resolution: 0\n  chunk_size: 8\n  chunks: [1, 1]\n",
	              "/grid/resolution: expected a whole number from 1 to 128");
	expect_error ("grid:\n  resolution: 4\n  chunk_size: big\n  chunks: [1, 1]\n",
	              "/grid/chunk_size: expected a number, got \"big\"");
	expect_error ("grid:\n  resolution: 4\n  chunk_size: 8\n  chunks: [1]\n",
	              "/grid/chunks: expected a list of 2 numbers, got 1");
	expect_error ("grid: {resolution: 4, chunk_size: 8, chunks: [1, 1]}\nheights: {format: tiff}\n",
	              "/heights/format: expected one of png16, r32");
	expect_error ("grid: {resolution: 4, chunk_size: 8, chunks: [1, 1]}\n"
	              "layers:\n- name: a\n- name: b\n  tiling: -2\n",
	              "/layers[1]/tiling: expected a number from 0.001 to 100000");
	expect_error ("grid: {resolution: 4, chunk_size: 8, chunks: [1, 1]}\nlod: {distances: [10, 5]}\n",
	              "/lod/distances: distances must be positive and increasing");
	expect_error ("grid: {resolution: 4, chunk_size: 8, chunks: [1, 1]}\n"
	              "foliage:\n  layers:\n  - kind: tree\n",
	              "/foliage/layers[0]/kind: expected one of mesh, billboard, grass");
	expect_error ("version: 9\ngrid: {resolution: 4, chunk_size: 8, chunks: [1, 1]}\n",
	              "/version: version 9 is newer");
}

static void
test_asset_sidecar_errors (void)
{
	g_autofree gchar           *dir = make_tmp_dir ();
	g_autofree gchar           *path = g_build_filename (dir, "a.rterrain", NULL);
	g_autofree gchar           *hp = g_build_filename (dir, "a.heights.png", NULL);
	g_autoptr(LrgTerrainAsset)  t = small_terrain ();
	g_autoptr(LrgTerrainAsset)  small = lrg_terrain_asset_new (4, 8.0f, 1, 1);
	g_autoptr(LrgTerrainAsset)  back = NULL;
	g_autofree gchar           *other = g_build_filename (dir, "b.rterrain", NULL);
	g_autofree gchar           *bp = g_build_filename (dir, "b.heights.png", NULL);
	g_autoptr(GError)           error = NULL;

	g_assert_true (lrg_terrain_asset_save_to_file (t, path, &error));
	g_assert_true (lrg_terrain_asset_save_to_file (small, other, &error));

	/* a heights image of the wrong size */
	g_assert_cmpint (g_rename (bp, hp), ==, 0);
	back = lrg_terrain_asset_new_from_file (path, &error);
	g_assert_null (back);
	g_assert_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID);
	if (strstr (error->message, "/heights/file: a.heights.png: expected a 17x17 image, got 5x5") == NULL) g_error ("%s", error->message);
	g_clear_error (&error);

	/* a missing one */
	g_unlink (hp);
	back = lrg_terrain_asset_new_from_file (path, &error);
	g_assert_null (back);
	g_assert_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_IO);
	remove_dir (dir);
}

static void
test_asset_mesh (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	guint                      nv, nt, nv1, nt1, nt_hole;

	g_assert_true (lrg_terrain_asset_get_chunk_mesh_counts (t, 1, 1, 0, &nv, &nt));
	/* 9 x 9 grid + 4 skirts of 9, 8 x 8 x 2 triangles + 4 x 8 x 4 skirt */
	g_assert_cmpuint (nv, ==, 81 + 36);
	g_assert_cmpuint (nt, ==, 128 + 128);
	g_assert_true (lrg_terrain_asset_get_chunk_mesh_counts (t, 1, 1, 1, &nv1, &nt1));
	g_assert_cmpuint (nv1, ==, 25 + 20);
	g_assert_cmpuint (nt1, ==, 32 + 64);
	g_assert_false (lrg_terrain_asset_get_chunk_mesh_counts (t, 2, 0, 0, NULL, NULL));

	/* a hole removes its cell's two triangles at LOD 0 and its 2 x 2
	 * block at LOD 1 */
	lrg_terrain_asset_set_hole (t, 9, 9, TRUE);
	lrg_terrain_asset_get_chunk_mesh_counts (t, 1, 1, 0, NULL, &nt_hole);
	g_assert_cmpuint (nt_hole, ==, nt - 2);
	lrg_terrain_asset_get_chunk_mesh_counts (t, 1, 1, 1, NULL, &nt_hole);
	g_assert_cmpuint (nt_hole, ==, nt1 - 2);

	/* no skirts at depth 0 */
	lrg_terrain_asset_set_skirt_depth (t, 0.0f);
	lrg_terrain_asset_get_chunk_mesh_counts (t, 0, 0, 0, &nv, &nt);
	g_assert_cmpuint (nv, ==, 81);
	g_assert_cmpuint (nt, ==, 128);
}

static void
test_asset_bake (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	guint                      B, x, z;
	g_autofree guint8         *px = NULL;
	gfloat                     w[3] = { 1.0f, 1.0f, 0.0f };
	guint8                     checker[4 * 4] = { 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255 };

	lrg_terrain_asset_set_bake_resolution (t, 16);
	B = lrg_terrain_asset_get_bake_resolution (t);
	g_assert_cmpuint (B, ==, 16);
	px = g_malloc (B * B * 4);

	/* layer 0 everywhere: its colour */
	g_assert_true (lrg_terrain_asset_bake_chunk (t, 0, 0, px));
	g_assert_cmpuint (px[0], ==, 64);
	g_assert_cmpuint (px[1], ==, 128);
	g_assert_cmpuint (px[2], ==, 51);

	/* half grass, half rock on every sample: the mean colour */
	for (z = 0; z < 17; z++)
		for (x = 0; x < 17; x++)
			lrg_terrain_asset_set_weights (t, x, z, w, 3);
	lrg_terrain_asset_bake_chunk (t, 1, 0, px);
	g_assert_cmpint (ABS ((gint) px[(5 * B + 5) * 4] - 96), <=, 1);
	g_assert_cmpint (ABS ((gint) px[(5 * B + 5) * 4 + 1] - 128), <=, 1);

	/* pixels multiply the colour */
	lrg_terrain_layer_set_pixels (lrg_terrain_asset_get_layer (t, 1), 2, 2, checker);
	g_assert_true (lrg_terrain_layer_has_pixels (lrg_terrain_asset_get_layer (t, 1)));
	lrg_terrain_asset_bake_chunk (t, 1, 0, px);
	g_assert_cmpint (ABS ((gint) px[(5 * B + 5) * 4 + 1] - 64), <=, 1);
	g_assert_false (lrg_terrain_asset_bake_chunk (t, 5, 0, px));
}

static void
test_asset_physics (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	g_autoptr(LrgPhysicsShape) shape = NULL;
	g_autoptr(GError)          error = NULL;
	guint                      nx, nz, i, j;
	gfloat                     scale[3];

	fill_slope (t);
	/* a 2 x 2 hole block (cells 4..5, 4..5): sample (5, 5) is the only one
	 * all of whose cells are holes */
	lrg_terrain_asset_set_hole (t, 4, 4, TRUE);
	lrg_terrain_asset_set_hole (t, 5, 4, TRUE);
	lrg_terrain_asset_set_hole (t, 4, 5, TRUE);
	lrg_terrain_asset_set_hole (t, 5, 5, TRUE);
	shape = lrg_terrain_asset_build_physics_shape (t, 1.0f, 2.0f, 1.0f, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_physics_shape_get_kind (shape), ==, LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD);
	g_assert_true (lrg_physics_shape_get_heightfield_size (shape, &nx, &nz, scale));
	g_assert_cmpuint (nx, ==, 17);
	g_assert_cmpuint (nz, ==, 17);
	g_assert_cmpfloat_with_epsilon (scale[0], 2.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (scale[1], 2.0f, 1e-6f);
	for (j = 0; j < nz; j++)
		for (i = 0; i < nx; i++)
		{
			gfloat h = lrg_physics_shape_get_heightfield_height (shape, i, j);

			if (i == 5 && j == 5)
				g_assert_cmpfloat (h, ==, LRG_PHYSICS_HEIGHTFIELD_HOLE);
			else
				g_assert_cmpfloat_with_epsilon (h, lrg_terrain_asset_get_height (t, i, j) * 2.0f, 1e-4f);
		}
	g_assert_null (lrg_terrain_asset_build_physics_shape (t, 0.0f, 1.0f, 1.0f, NULL));
}

static void
test_asset_raycast (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	gfloat                     o[3] = { 10.0f, 50.0f, 6.0f }, d[3] = { 0.0f, -1.0f, 0.0f }, hit[3], h;
	gfloat                     o2[3] = { -5.0f, 20.0f, -5.0f }, d2[3] = { 1.0f, -0.5f, 1.0f };

	fill_slope (t);
	g_assert_true (lrg_terrain_asset_raycast (t, o, d, 100.0f, hit));
	lrg_terrain_asset_sample_height (t, 10.0f, 6.0f, &h);
	g_assert_cmpfloat_with_epsilon (hit[1], h, 1e-3f);
	g_assert_false (lrg_terrain_asset_raycast (t, o, d, 10.0f, hit));
	g_assert_true (lrg_terrain_asset_raycast (t, o2, d2, 200.0f, hit));
	lrg_terrain_asset_sample_height (t, hit[0], hit[2], &h);
	g_assert_cmpfloat_with_epsilon (hit[1], h, 1e-3f);
}

static void
test_asset_resize (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	guint                      nx, nz;

	fill_slope (t);
	lrg_terrain_asset_set_hole (t, 1, 1, TRUE);
	lrg_terrain_asset_resize (t, 3, 1);
	lrg_terrain_asset_get_samples (t, &nx, &nz);
	g_assert_cmpuint (nx, ==, 25);
	g_assert_cmpuint (nz, ==, 9);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, 4, 4), 0.25f * 8.0f + 0.1f * 8.0f, 1e-5f);
	/* new samples copy the old +X edge */
	g_assert_cmpfloat (lrg_terrain_asset_get_height (t, 20, 4), ==, lrg_terrain_asset_get_height (t, 16, 4));
	g_assert_true (lrg_terrain_asset_get_hole (t, 1, 1));
}

/* ==========================================================================
 * LrgTerrainBrush
 * ========================================================================== */

static void
test_brush_raise_lower (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	g_autoptr(LrgTerrainBrush) b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_RAISE);
	g_autoptr(LrgTerrainEdit)  e = NULL;
	guint                      x, z, w, h, i, j;

	lrg_terrain_brush_set_radius (b, 5.0f);
	lrg_terrain_brush_set_strength (b, 2.0f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_LINEAR);
	e = lrg_terrain_brush_apply (b, t, 16.0f, 16.0f, 0.5f);
	g_assert_nonnull (e);
	g_assert_cmpint (lrg_terrain_edit_get_kind (e), ==, LRG_TERRAIN_EDIT_HEIGHTS);
	lrg_terrain_edit_get_region (e, &x, &z, &w, &h);
	g_assert_cmpuint (x, ==, 6);
	g_assert_cmpuint (z, ==, 6);
	g_assert_cmpuint (w, ==, 5);
	g_assert_cmpuint (h, ==, 5);

	/* every sample: 2 x 0.5 x (1 - d / 5) */
	for (j = 0; j < 17; j++)
		for (i = 0; i < 17; i++)
		{
			gfloat d = sqrtf (powf ((gfloat) i * 2.0f - 16.0f, 2.0f) + powf ((gfloat) j * 2.0f - 16.0f, 2.0f));
			gfloat want = d < 5.0f ? 1.0f * (1.0f - d / 5.0f) : 0.0f;

			g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, i, j), want, 1e-5f);
		}
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, 8, 9), 0.6f, 1e-5f);

	/* lower undoes it exactly at the same spot and amount */
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_LOWER);
	g_clear_pointer (&e, lrg_terrain_edit_free);
	e = lrg_terrain_brush_apply (b, t, 16.0f, 16.0f, 0.5f);
	for (j = 0; j < 17; j++)
		for (i = 0; i < 17; i++)
			g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, i, j), 0.0f, 1e-5f);

	/* a dab off the grid changes nothing */
	g_assert_null (lrg_terrain_brush_apply (b, t, -100.0f, 0.0f, 1.0f));
}

static void
test_brush_flatten_smooth_noise (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	g_autoptr(LrgTerrainAsset) t2 = small_terrain ();
	g_autoptr(LrgTerrainBrush) b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_FLATTEN);
	g_autoptr(LrgTerrainEdit)  e = NULL;
	gfloat                     before;
	guint                      i, j;

	fill_slope (t);

	/* flatten with sampled target, constant falloff, full strength: every
	 * sample in the radius takes the height under the stroke start */
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT);
	lrg_terrain_brush_set_radius (b, 3.0f);
	lrg_terrain_brush_set_sample_target (b, TRUE);
	lrg_terrain_brush_begin_stroke (b, t, 8.0f, 8.0f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_target_height (b), 2.8f, 1e-5f);
	e = lrg_terrain_brush_apply (b, t, 8.0f, 8.0f, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, 4, 4), 2.8f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, 5, 4), 2.8f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t, 6, 4), 0.25f * 12.0f + 0.8f, 1e-5f);
	g_clear_pointer (&e, lrg_terrain_edit_free);
	e = lrg_terrain_brush_end_stroke (b);
	g_assert_nonnull (e);
	g_clear_pointer (&e, lrg_terrain_edit_free);

	/* smooth: a single spike moves halfway to its 3 x 3 mean */
	lrg_terrain_asset_set_height (t2, 8, 8, 9.0f);
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_SMOOTH);
	lrg_terrain_brush_set_strength (b, 0.5f);
	lrg_terrain_brush_set_radius (b, 1.0f);
	e = lrg_terrain_brush_apply (b, t2, 16.0f, 16.0f, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_height (t2, 8, 8), 9.0f + (1.0f - 9.0f) * 0.5f, 1e-5f);
	g_clear_pointer (&e, lrg_terrain_edit_free);

	/* smoothing a plane changes nothing (no edit) */
	{
		g_autoptr(LrgTerrainAsset) flat = small_terrain ();

		lrg_terrain_brush_set_radius (b, 6.0f);
		g_assert_null (lrg_terrain_brush_apply (b, flat, 16.0f, 16.0f, 1.0f));
	}

	/* noise: deterministic for a seed, bounded by the strength */
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_NOISE);
	lrg_terrain_brush_set_strength (b, 1.0f);
	lrg_terrain_brush_set_radius (b, 12.0f);
	lrg_terrain_brush_set_noise_scale (b, 4.0f);
	lrg_terrain_brush_set_seed (b, 7);
	{
		g_autoptr(LrgTerrainAsset) a1 = small_terrain ();
		g_autoptr(LrgTerrainAsset) a2 = small_terrain ();
		g_autoptr(LrgTerrainEdit)  e1 = lrg_terrain_brush_apply (b, a1, 16.0f, 16.0f, 1.0f);
		g_autoptr(LrgTerrainEdit)  e2 = lrg_terrain_brush_apply (b, a2, 16.0f, 16.0f, 1.0f);
		gboolean                   any = FALSE;

		for (j = 0; j < 17; j++)
			for (i = 0; i < 17; i++)
			{
				before = lrg_terrain_asset_get_height (a1, i, j);
				g_assert_cmpfloat (before, ==, lrg_terrain_asset_get_height (a2, i, j));
				g_assert_cmpfloat (fabsf (before), <=, 1.0f);
				if (before != 0.0f)
					any = TRUE;
			}
		g_assert_true (any);
		g_assert_true (g_bytes_equal (lrg_terrain_edit_get_after (e1), lrg_terrain_edit_get_after (e2)));
	}
}

static void
test_brush_paint (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	g_autoptr(LrgTerrainBrush) b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_PAINT);
	guint                      k, i, j;
	gfloat                     prev = 0.0f;

	lrg_terrain_brush_set_layer (b, 1);
	lrg_terrain_brush_set_radius (b, 6.0f);
	lrg_terrain_brush_set_strength (b, 0.3f);

	/* repeated dabs: the painted layer rises monotonically to 1, every
	 * sample's weights stay normalised */
	for (k = 0; k < 20; k++)
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_brush_apply (b, t, 10.0f, 10.0f, 1.0f);
		gfloat                    w = lrg_terrain_asset_get_weight (t, 5, 5, 1);

		if (k == 0)
			g_assert_cmpint (lrg_terrain_edit_get_kind (e), ==, LRG_TERRAIN_EDIT_SPLAT);
		g_assert_cmpfloat (w, >=, prev);
		prev = w;
		for (j = 0; j < 17; j++)
			for (i = 0; i < 17; i++)
				g_assert_cmpfloat_with_epsilon (weight_sum (t, i, j), 1.0f, 1e-6f);
	}
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (t, 5, 5, 1), ==, 1.0f);
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (t, 5, 5, 0), ==, 0.0f);
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (t, 0, 0, 0), ==, 1.0f);

	/* a third layer painted over the second: the remainder is shared
	 * proportionally (still normalised) */
	lrg_terrain_brush_set_layer (b, 2);
	lrg_terrain_brush_set_strength (b, 0.5f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT);
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_brush_apply (b, t, 10.0f, 10.0f, 1.0f);
	}
	g_assert_cmpfloat_with_epsilon (lrg_terrain_asset_get_weight (t, 5, 5, 2), 128.0f / 255.0f, 1.5f / 255.0f);
	g_assert_cmpfloat_with_epsilon (weight_sum (t, 5, 5), 1.0f, 1e-6f);

	/* ids mode: the brush sets the cell layer */
	lrg_terrain_asset_set_splat_mode (t, LRG_TERRAIN_SPLAT_MODE_IDS);
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_brush_apply (b, t, 9.0f, 9.0f, 1.0f);

		g_assert_cmpint (lrg_terrain_edit_get_kind (e), ==, LRG_TERRAIN_EDIT_MATERIAL_IDS);
	}
	g_assert_cmpuint (lrg_terrain_asset_get_cell_material (t, 4, 4), ==, 2);
	g_assert_cmpuint (lrg_terrain_asset_get_cell_material (t, 0, 0), ==, 0);
}

static void
test_brush_undo (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	g_autoptr(LrgTerrainAsset) ref = small_terrain ();
	g_autoptr(LrgTerrainBrush) b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_RAISE);
	g_autoptr(LrgTerrainEdit)  stroke = NULL;
	g_autofree gfloat         *after = NULL;
	guint                      k, i, n, x, z, w, h;

	fill_slope (t);
	fill_slope (ref);
	lrg_terrain_brush_begin_stroke (b, t, 4.0f, 4.0f);
	g_assert_true (lrg_terrain_brush_is_stroking (b));
	for (k = 0; k < 8; k++)
	{
		g_autoptr(LrgTerrainEdit) dab = lrg_terrain_brush_apply (b, t, 4.0f + (gfloat) k * 3.0f, 4.0f, 0.25f);

		g_assert_nonnull (dab);
	}
	stroke = lrg_terrain_brush_end_stroke (b);
	g_assert_false (lrg_terrain_brush_is_stroking (b));
	g_assert_nonnull (stroke);
	lrg_terrain_edit_get_region (stroke, &x, &z, &w, &h);
	g_assert_cmpuint (x, ==, 0);
	g_assert_cmpuint (w, >=, 14);
	after = g_memdup2 (lrg_terrain_asset_get_heights (t, &n), sizeof (gfloat) * n);

	/* undo restores the slope exactly, redo the stroke */
	g_assert_true (lrg_terrain_edit_apply (stroke, t, FALSE));
	for (i = 0; i < n; i++)
		g_assert_cmpfloat (lrg_terrain_asset_get_heights (t, NULL)[i], ==, lrg_terrain_asset_get_heights (ref, NULL)[i]);
	g_assert_true (lrg_terrain_edit_apply (stroke, t, TRUE));
	for (i = 0; i < n; i++)
		g_assert_cmpfloat (lrg_terrain_asset_get_heights (t, NULL)[i], ==, after[i]);

	/* capture / capture_after for tools that edit by themselves */
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_asset_capture_edit (t, LRG_TERRAIN_EDIT_HOLES, 0, 2, 2, 3, 3);

		g_assert_true (lrg_terrain_edit_is_empty (e));
		lrg_terrain_asset_set_hole (t, 3, 3, TRUE);
		lrg_terrain_edit_capture_after (e, t);
		g_assert_false (lrg_terrain_edit_is_empty (e));
		lrg_terrain_edit_apply (e, t, FALSE);
		g_assert_false (lrg_terrain_asset_get_hole (t, 3, 3));
		g_assert_null (lrg_terrain_asset_capture_edit (t, LRG_TERRAIN_EDIT_HOLES, 0, 99, 0, 1, 1));
	}
}

static void
on_changed (LrgTerrainAsset    *asset,
            LrgTerrainEditKind  kind,
            guint               layer,
            guint               x,
            guint               z,
            guint               w,
            guint               h,
            gpointer            user_data)
{
	guint *count = user_data;

	(void) asset;
	(void) kind;
	(void) layer;
	(void) x;
	(void) z;
	(void) w;
	(void) h;
	(*count)++;
}

static void
test_brush_holes_foliage (void)
{
	g_autoptr(LrgTerrainAsset) t = small_terrain ();
	g_autoptr(LrgTerrainBrush) b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_HOLE);
	g_autoptr(LrgFoliageLayer) fl = lrg_foliage_layer_new ("bush", LRG_FOLIAGE_KIND_MESH);
	LrgFoliage                *f = lrg_terrain_asset_get_foliage (t);
	guint                      changes = 0;

	g_signal_connect (t, "region-changed", G_CALLBACK (on_changed), &changes);
	lrg_terrain_brush_set_radius (b, 2.5f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT);
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_brush_apply (b, t, 16.0f, 16.0f, 1.0f);

		g_assert_cmpint (lrg_terrain_edit_get_kind (e), ==, LRG_TERRAIN_EDIT_HOLES);
	}
	g_assert_cmpuint (changes, ==, 1);
	/* cell centres within 2.5 of (16, 16): (7..8, 7..8) */
	g_assert_cmpuint (lrg_terrain_asset_get_hole_count (t), ==, 4);
	g_assert_true (lrg_terrain_asset_get_hole (t, 7, 7));
	g_assert_true (lrg_terrain_asset_get_hole (t, 8, 8));
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_FILL);
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_brush_apply (b, t, 16.0f, 16.0f, 1.0f);
	}
	g_assert_cmpuint (lrg_terrain_asset_get_hole_count (t), ==, 0);

	/* foliage density: erase then paint back */
	lrg_foliage_add_layer (f, fl);
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_FOLIAGE_ERASE);
	lrg_terrain_brush_set_layer (b, 0);
	{
		g_autoptr(LrgTerrainEdit) e = lrg_terrain_brush_apply (b, t, 16.0f, 16.0f, 1.0f);

		g_assert_cmpint (lrg_terrain_edit_get_kind (e), ==, LRG_TERRAIN_EDIT_FOLIAGE);
		g_assert_cmpfloat (lrg_foliage_get_density (f, 0, 7, 7), ==, 0.0f);
		g_assert_cmpfloat (lrg_foliage_get_density (f, 0, 0, 0), ==, 1.0f);
		lrg_terrain_edit_apply (e, t, FALSE);
		g_assert_cmpfloat (lrg_foliage_get_density (f, 0, 7, 7), ==, 1.0f);
	}
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_FOLIAGE_PAINT);
	g_assert_null (lrg_terrain_brush_apply (b, t, 16.0f, 16.0f, 1.0f));   /* already full */
}

static void
test_brush_falloff (void)
{
	g_autoptr(LrgTerrainBrush)  b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_RAISE);
	g_autoptr(LrgKeyframeCurve) c = lrg_keyframe_curve_new ();

	lrg_terrain_brush_set_radius (b, 2.0f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_weight (b, 0.0f), 1.0f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_weight (b, 1.0f), 0.5f, 1e-6f);
	g_assert_cmpfloat (lrg_terrain_brush_get_weight (b, 2.0f), ==, 0.0f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_SPHERE);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_weight (b, 1.0f), sqrtf (0.75f), 1e-6f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_SHARP);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_weight (b, 1.0f), 0.25f, 1e-6f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_CURVE);
	lrg_keyframe_curve_add_key (c, 0.0f, 0.2f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (c, 1.0f, 0.6f, LRG_EASING_LINEAR);
	lrg_terrain_brush_set_falloff_curve (b, c);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_weight (b, 1.0f), 0.4f, 1e-5f);
}

/* ==========================================================================
 * LrgFoliage
 * ========================================================================== */

/* A 4 x 4 chunk terrain whose west half is a 60-degree slope. */
static LrgTerrainAsset *
foliage_terrain (guint32 seed)
{
	LrgTerrainAsset   *t = lrg_terrain_asset_new (8, 16.0f, 4, 4);
	g_autoptr(LrgFoliageLayer) fl = lrg_foliage_layer_new ("trees", LRG_FOLIAGE_KIND_MESH);
	g_autofree gfloat *h = g_new (gfloat, 33 * 33);
	guint              i, j;

	lrg_terrain_asset_add_layer (t, "rock");
	for (j = 0; j < 33; j++)
		for (i = 0; i < 33; i++)
			h[j * 33 + i] = i < 16 ? (16.0f - (gfloat) i) * 2.0f * tanf (60.0f * (gfloat) G_PI / 180.0f) : 0.0f;
	lrg_terrain_asset_set_heights (t, h, 33 * 33, NULL);
	lrg_foliage_layer_set_mesh (fl, "builtin:conifer");
	lrg_foliage_layer_set_density (fl, 0.2f);
	lrg_foliage_layer_set_slope_range (fl, 0.0f, 30.0f);
	lrg_foliage_add_layer (lrg_terrain_asset_get_foliage (t), fl);
	lrg_foliage_set_seed (lrg_terrain_asset_get_foliage (t), seed);
	return t;
}

static void
test_foliage_determinism (void)
{
	g_autoptr(LrgTerrainAsset) a = foliage_terrain (11);
	g_autoptr(LrgTerrainAsset) b = foliage_terrain (11);
	g_autoptr(LrgTerrainAsset) c = foliage_terrain (12);
	LrgFoliage                *fa = lrg_terrain_asset_get_foliage (a);
	LrgFoliage                *fb = lrg_terrain_asset_get_foliage (b);
	LrgFoliage                *fc = lrg_terrain_asset_get_foliage (c);
	guint                      na, nb, nc, i;
	gboolean                   differs = FALSE;

	na = lrg_terrain_asset_scatter_foliage (a);
	nb = lrg_terrain_asset_scatter_foliage (b);
	nc = lrg_terrain_asset_scatter_foliage (c);
	g_assert_cmpuint (na, >, 0);
	g_assert_cmpuint (na, ==, nb);
	g_assert_cmpuint (lrg_foliage_get_dirty_patches (fa), ==, 0);
	for (i = 0; i < na; i++)
	{
		LrgFoliageInstance ia, ib;

		g_assert_true (lrg_foliage_get_instance (fa, 0, i, &ia));
		g_assert_true (lrg_foliage_get_instance (fb, 0, i, &ib));
		g_assert_cmpint (memcmp (&ia, &ib, sizeof ia), ==, 0);
	}
	/* another seed, another forest */
	for (i = 0; i < MIN (na, nc) && !differs; i++)
	{
		LrgFoliageInstance ia, ic;

		lrg_foliage_get_instance (fa, 0, i, &ia);
		lrg_foliage_get_instance (fc, 0, i, &ic);
		differs = memcmp (&ia, &ic, sizeof ia) != 0;
	}
	g_assert_true (differs || na != nc);

	/* expected count: 0.2 per square unit over the flat half (32 x 64
	 * units minus the slope's edge), within 15 % */
	g_assert_cmpfloat (fabs ((gdouble) na - 0.2 * 32.0 * 64.0), <, 0.15 * 0.2 * 32.0 * 64.0);
	(void) fc;
}

static void
test_foliage_rules (void)
{
	g_autoptr(LrgTerrainAsset) t = foliage_terrain (3);
	LrgFoliage                *f = lrg_terrain_asset_get_foliage (t);
	LrgFoliageLayer           *fl = lrg_foliage_get_layer (f, 0);
	guint                      n, i, n_full, n_half;
	gfloat                     w[2] = { 0.0f, 1.0f };

	/* slope rule: nothing on the 60-degree west half */
	n = lrg_terrain_asset_scatter_foliage (t);
	for (i = 0; i < n; i++)
	{
		LrgFoliageInstance inst;
		gfloat             nrm[3], s;

		lrg_foliage_get_instance (f, 0, i, &inst);
		g_assert_cmpfloat (inst.position[0], >=, 30.0f);
		lrg_terrain_asset_sample_normal (t, inst.position[0], inst.position[2], nrm);
		g_assert_cmpfloat (nrm[1], >=, cosf (30.0f * (gfloat) G_PI / 180.0f) - 1e-4f);
		lrg_foliage_layer_get_scale_range (fl, NULL, &s);
		g_assert_cmpfloat (inst.scale, <=, s);
	}
	n_full = n;

	/* half painted density: about half, and a subset of the full set
	 * (higher density only adds instances) */
	lrg_foliage_fill_density (f, 0, 0.5f);
	n_half = lrg_terrain_asset_scatter_foliage (t);
	g_assert_cmpuint (n_half, <, n_full);
	g_assert_cmpfloat (fabs ((gdouble) n_half - (gdouble) n_full * 0.5), <, 0.2 * (gdouble) n_full);

	/* density 0 painted under a brush: nothing there */
	lrg_foliage_fill_density (f, 0, 1.0f);
	lrg_foliage_set_density (f, 0, 20, 20, 0.0f);
	lrg_terrain_asset_scatter_foliage (t);
	n = lrg_foliage_get_instance_count (f, 0);
	for (i = 0; i < n; i++)
	{
		LrgFoliageInstance inst;

		lrg_foliage_get_instance (f, 0, i, &inst);
		g_assert_false (inst.position[0] >= 40.0f && inst.position[0] < 42.0f && inst.position[2] >= 40.0f &&
		                inst.position[2] < 42.0f);
	}

	/* material density: trees only on layer 0; paint the whole terrain
	 * rock -> none */
	lrg_foliage_layer_set_material_density (fl, 1, 0.0f);
	for (i = 0; i < 33 * 33; i++)
		lrg_terrain_asset_set_weights (t, i % 33, i / 33, w, 2);
	g_assert_cmpuint (lrg_terrain_asset_scatter_foliage (t), ==, 0);

	/* height rule */
	lrg_foliage_layer_set_material_density (fl, 1, 1.0f);
	lrg_foliage_layer_set_height_range (fl, 5.0f, 100.0f);
	g_assert_cmpuint (lrg_terrain_asset_scatter_foliage (t), ==, 0);
}

static void
test_foliage_dirty (void)
{
	g_autoptr(LrgTerrainAsset) t = foliage_terrain (5);
	LrgFoliage                *f = lrg_terrain_asset_get_foliage (t);
	guint                      cx, cz;
	gfloat                     cs;

	lrg_foliage_get_grid (f, &cx, &cz, &cs);
	g_assert_cmpuint (cx, ==, 32);
	g_assert_cmpuint (cz, ==, 32);
	g_assert_cmpfloat (cs, ==, 2.0f);
	g_assert_cmpuint (lrg_foliage_get_patch_cells (f), ==, 16);
	lrg_terrain_asset_scatter_foliage (t);
	g_assert_cmpuint (lrg_foliage_get_dirty_patches (f), ==, 0);

	/* one height change dirties the patches around it only (cells 19..20
	 * in one patch) */
	lrg_terrain_asset_set_height (t, 20, 20, 3.0f);
	g_assert_cmpuint (lrg_foliage_get_dirty_patches (f), ==, 1);

	/* manual instances */
	{
		g_autoptr(LrgFoliage) free_set = lrg_foliage_new ();
		g_autoptr(LrgFoliageLayer) l = lrg_foliage_layer_new ("rocks", LRG_FOLIAGE_KIND_MESH);
		LrgFoliageInstance          inst = { { 3.0f, 0.0f, 5.0f }, 0.0f, 1.0f, { 0.0f, 1.0f, 0.0f } };
		gfloat                      m[16];

		lrg_foliage_add_layer (free_set, l);
		g_assert_false (lrg_foliage_add_instance (free_set, 0, &inst));   /* no grid yet */
		lrg_foliage_set_grid (free_set, 8, 8, 1.0f);
		g_assert_true (lrg_foliage_add_instance (free_set, 0, &inst));
		g_assert_cmpuint (lrg_foliage_get_instance_count (free_set, 0), ==, 1);
		lrg_foliage_instance_get_matrix (&inst, m);
		g_assert_cmpfloat (m[12], ==, 3.0f);
		g_assert_cmpfloat (m[14], ==, 5.0f);
		g_assert_cmpfloat_with_epsilon (m[5], 1.0f, 1e-6f);
		lrg_foliage_clear_instances (free_set, 0);
		g_assert_cmpuint (lrg_foliage_get_instance_count (free_set, 0), ==, 0);
	}
}

/* ==========================================================================
 * LrgWaterBody
 * ========================================================================== */

static const gchar *lake_doc =
	"version: 1\n"
	"name: pond\n"
	"shape: lake\n"
	"level: 2.5\n"
	"points:\n"
	"- - 0\n"
	"  - 0\n"
	"- - 20\n"
	"  - 0\n"
	"- - 20\n"
	"  - 10\n"
	"- - 0\n"
	"  - 10\n"
	"cell_size: 1\n"
	"foam:\n"
	"- 2\n"
	"- 0.5\n"
	"waves:\n"
	"- direction:\n"
	"  - 1\n"
	"  - 0\n"
	"  amplitude: 0.1\n"
	"  wavelength: 6\n"
	"  speed: 1.5\n"
	"  steepness: 0.4\n"
	"wave_scale: 1\n"
	"colors:\n"
	"  shallow:\n"
	"  - 0.2\n"
	"  - 0.4\n"
	"  - 0.4\n"
	"  - 1\n"
	"  deep:\n"
	"  - 0\n"
	"  - 0.2\n"
	"  - 0.6\n"
	"  - 1\n"
	"  foam:\n"
	"  - 1\n"
	"  - 1\n"
	"  - 1\n"
	"  - 1\n"
	"optics:\n"
	"  transparency: 0.4\n"
	"  reflection: 0.7\n"
	"  refraction: 0.03\n"
	"  reflect_distance: 20\n"
	"editor_color: blue\n";

static void
test_water_round_trip (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgWaterBody) w = lrg_water_body_new_from_data (lake_doc, -1, &error);
	g_autoptr(LrgWaterBody) back = NULL;
	g_autofree gchar       *out = NULL, *out2 = NULL;
	LrgWaterWave            wave;
	gfloat                  p[4], level;

	g_assert_no_error (error);
	g_assert_cmpint (lrg_water_body_get_shape (w), ==, LRG_WATER_BODY_LAKE);
	g_assert_cmpuint (lrg_water_body_get_n_points (w), ==, 4);
	g_assert_true (lrg_water_body_get_point (w, 2, p));
	g_assert_cmpfloat (p[0], ==, 20.0f);
	g_assert_cmpfloat (p[2], ==, 10.0f);
	g_assert_cmpuint (lrg_water_body_get_wave_count (w), ==, 1);
	g_assert_true (lrg_water_body_get_wave (w, 0, &wave));
	g_assert_cmpfloat (wave.wavelength, ==, 6.0f);
	out = lrg_water_body_to_data (w, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (out, ==, lake_doc);
	back = lrg_water_body_new_from_data (out, -1, &error);
	out2 = lrg_water_body_to_data (back, NULL, &error);
	g_assert_cmpstr (out, ==, out2);

	g_assert_true (lrg_water_body_contains (w, 5.0f, 5.0f, &level));
	g_assert_cmpfloat (level, ==, 2.5f);
	g_assert_false (lrg_water_body_contains (w, 25.0f, 5.0f, NULL));
}

static void
test_water_errors (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgWaterBody) w = NULL;

	w = lrg_water_body_new_from_data ("level: 1\n", -1, &error);
	g_assert_null (w);
	g_assert_nonnull (strstr (error->message, "<data>: /shape: missing"));
	g_clear_error (&error);
	w = lrg_water_body_new_from_data ("shape: ocean\n", -1, &error);
	g_assert_nonnull (strstr (error->message, "/shape: expected one of plane, lake, river"));
	g_clear_error (&error);
	w = lrg_water_body_new_from_data ("shape: river\npoints:\n- [0, 0, 0]\n", -1, &error);
	g_assert_nonnull (strstr (error->message, "/points[0]: expected [x, z] or [x, y, z, width]"));
	g_clear_error (&error);
	w = lrg_water_body_new_from_data ("shape: plane\nwaves:\n- {height: 2}\n", -1, &error);
	g_assert_nonnull (strstr (error->message, "/waves[0]/height: unknown wave key"));
	g_clear_error (&error);
	w = lrg_water_body_new_from_data ("shape: plane\noptics: {reflection: 3}\n", -1, &error);
	g_assert_nonnull (strstr (error->message, "/optics/reflection: expected a number from 0 to 1"));
}

static gboolean
ground_bowl (gfloat    x,
             gfloat    z,
             gfloat   *out,
             gpointer  user_data)
{
	(void) user_data;
	*out = 2.5f - 0.2f * MIN (MIN (x, 20.0f - x), MIN (z, 10.0f - z));
	return TRUE;
}

static void
test_water_build (void)
{
	g_autoptr(LrgWaterBody)    lake = lrg_water_body_new_from_data (lake_doc, -1, NULL);
	g_autoptr(LrgWaterBody)    river = lrg_water_body_new (LRG_WATER_BODY_RIVER);
	g_autoptr(LrgWaterBody)    plane = lrg_water_body_new (LRG_WATER_BODY_PLANE);
	g_autoptr(LrgWaterSurface) s = lrg_water_surface_new ();
	g_autoptr(GError)          error = NULL;
	const gfloat              *foam;
	guint                      n;
	gfloat                     level, mn[3], mx[3];

	/* the 20 x 10 lake: 200 wet cells in one patch */
	g_assert_true (lrg_water_body_build_surface (lake, s, 100.0f, 0.0f, 50.0f, ground_bowl, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_water_surface_get_patch_count (s), ==, 1);
	g_assert_cmpuint (lrg_water_surface_get_triangle_count (s), ==, 400);
	foam = lrg_water_surface_get_patch_foam (s, 0, &n);
	g_assert_cmpuint (n, ==, 21 * 11);
	g_assert_cmpfloat (foam[0], ==, 1.0f);              /* corner: on the shore */
	g_assert_cmpfloat (foam[5 * 21 + 10], <, 0.5f);     /* middle: open water */

	/* a big plane is cut into tiles */
	lrg_water_body_set_size (plane, 600.0f, 10.0f);
	g_assert_true (lrg_water_body_build_surface (plane, s, 0.0f, 0.0f, 0.0f, NULL, NULL, &error));
	g_assert_cmpuint (lrg_water_surface_get_patch_count (s), ==, 3);

	/* a river: two points 40 apart, width 6, sloping down */
	lrg_water_body_add_point (river, 0.0f, 5.0f, 0.0f, 6.0f);
	lrg_water_body_add_point (river, 40.0f, 1.0f, 0.0f, 6.0f);
	g_assert_true (lrg_water_body_contains (river, 20.0f, 2.0f, &level));
	g_assert_cmpfloat_with_epsilon (level, 3.0f, 0.05f);
	g_assert_false (lrg_water_body_contains (river, 20.0f, 4.0f, NULL));
	g_assert_true (lrg_water_body_get_bounds (river, mn, mx));
	g_assert_cmpfloat (mn[1], ==, 1.0f);
	g_assert_cmpfloat (mx[1], ==, 5.0f);
	g_assert_true (lrg_water_body_build_surface (river, s, 0.0f, 0.0f, 0.0f, NULL, NULL, &error));
	g_assert_cmpuint (lrg_water_surface_get_triangle_count (s), >, 400);

	/* a lake needs three points */
	lrg_water_body_set_shape (river, LRG_WATER_BODY_LAKE);
	g_assert_false (lrg_water_body_build_surface (river, s, 0.0f, 0.0f, 0.0f, NULL, NULL, &error));
	g_assert_error (error, LRG_WATER_BODY_ERROR, LRG_WATER_BODY_ERROR_INVALID);
}

/* ==========================================================================
 * Components (headless)
 * ========================================================================== */

static void
test_components_headless (void)
{
	g_autoptr(LrgWorld3D)          world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)         ground = lrg_entity3d_new_with_name ("ground");
	g_autoptr(LrgEntity3D)         lake = lrg_entity3d_new_with_name ("lake");
	g_autoptr(LrgTerrainAsset)     t = foliage_terrain (9);
	g_autoptr(LrgTerrainComponent) tc = lrg_terrain_component_new_with_asset (t);
	g_autoptr(LrgWaterComponent)   wc = lrg_water_component_new ();
	g_autoptr(LrgCamera3D)         cam = lrg_camera3d_new ();
	g_autoptr(LrgRenderList)       list = lrg_render_list_new ();
	guint                          n_mesh = 0, n_custom = 0, n_trans = 0, i;
	gfloat                         h, pos[3] = { 60.0f, 30.0f, 0.0f }, dir[3] = { 0.0f, -1.0f, 0.4f }, hit[3];
	LrgRayHit                      ray;
	gfloat                         ro[3] = { 50.0f, 50.0f, 50.0f }, rd[3] = { 0.0f, -1.0f, 0.0f };

	lrg_world3d_ensure_physics (world);
	lrg_entity3d_add_component (ground, LRG_COMPONENT (tc));
	lrg_world3d_add_entity (world, ground);
	lrg_entity3d_set_position_xyz (lake, 40.0f, 0.5f, 40.0f);
	lrg_entity3d_add_component (lake, LRG_COMPONENT (wc));
	lrg_world3d_add_entity (world, lake);
	lrg_camera3d_set_position_xyz (cam, 48.0f, 20.0f, 120.0f);
	lrg_camera3d_set_target_xyz (cam, 32.0f, 0.0f, 32.0f);

	lrg_world3d_collect_render (world, LRG_CAMERA (cam), list);
	for (i = 0; i < lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE); i++)
	{
		const LrgRenderItem *it = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_OPAQUE, i);

		if (it->kind == LRG_RENDER_ITEM_MESH && it->component == LRG_COMPONENT (tc))
			n_mesh++;
	}
	for (i = 0; i < lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_TRANSPARENT); i++)
	{
		const LrgRenderItem *it = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_TRANSPARENT, i);

		n_trans++;
		if (it->kind == LRG_RENDER_ITEM_CUSTOM)
			n_custom++;
	}
	g_assert_cmpuint (n_mesh, >, 0);
	g_assert_cmpuint (n_mesh, <=, 16);
	g_assert_cmpuint (n_custom, ==, 2);   /* the trees and the water */
	g_assert_cmpuint (n_trans, ==, 2);

	/* LOD grows with distance: the near row is finer than the far one */
	g_assert_cmpint (lrg_terrain_component_get_chunk_lod (tc, 2, 3), >=, 0);
	g_assert_cmpint (lrg_terrain_component_get_chunk_lod (tc, 2, 0), >=,
	                 lrg_terrain_component_get_chunk_lod (tc, 2, 3));

	/* queries through the entity transform */
	lrg_entity3d_set_position_xyz (ground, 10.0f, 1.0f, 0.0f);
	g_assert_true (lrg_terrain_component_height_at (tc, 50.0f, 20.0f, &h));
	g_assert_cmpfloat_with_epsilon (h, 1.0f, 1e-3f);
	g_assert_true (lrg_terrain_component_raycast (tc, pos, dir, 200.0f, hit, NULL));
	g_assert_cmpfloat_with_epsilon (hit[1], 1.0f, 1e-2f);
	g_assert_true (lrg_water_component_level_at (wc, 41.0f, 41.0f, &h));
	g_assert_cmpfloat (fabsf (h - 0.5f), <, 0.5f);

	/* physics: one static heightfield body that rays hit */
	lrg_world3d_step (world, 1.0f / 60.0f);
	g_assert_cmpuint (lrg_terrain_component_get_body_id (tc), !=, LRG_PHYSICS_NO_BODY);
	g_assert_true (lrg_dynamics_world_raycast (lrg_world3d_get_physics (world), ro, rd, 100.0f, G_MAXUINT32,
	                                           G_MAXUINT, &ray));
	g_assert_cmpuint (ray.body, ==, lrg_terrain_component_get_body_id (tc));
	g_assert_cmpfloat_with_epsilon (ray.point[1], 1.0f, 1e-3f);

	/* a sculpt rebuilds the body before the next step */
	lrg_terrain_asset_set_height (t, 20, 25, 7.0f);
	lrg_world3d_step (world, 1.0f / 60.0f);
	g_assert_true (lrg_dynamics_world_raycast (lrg_world3d_get_physics (world), ro, rd, 100.0f, G_MAXUINT32,
	                                           G_MAXUINT, &ray));
	g_assert_cmpfloat_with_epsilon (ray.point[1], 8.0f, 1e-3f);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

/* A procedural RGBA texture: @a and @b mixed by hashed noise. */
static guint8 *
noise_texture (guint         size,
               const guint8 *a,
               const guint8 *b,
               guint32       seed)
{
	guint8 *px = g_malloc ((gsize) size * size * 4);
	guint   x, y;

	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
		{
			guint32 h = (x * 73856093u) ^ (y * 19349663u) ^ seed;
			gfloat  t;
			guint   c;

			h ^= h >> 13;
			h *= 0x5bd1e995u;
			h ^= h >> 15;
			t = (gfloat) (h & 0xffffu) / 65535.0f;
			t = 0.5f * t + 0.25f * (0.5f + 0.5f * sinf ((gfloat) x * 0.4f) * cosf ((gfloat) y * 0.3f));
			for (c = 0; c < 3; c++)
				px[(y * size + x) * 4 + c] = (guint8) ((gfloat) a[c] * (1.0f - t) + (gfloat) b[c] * t);
			px[(y * size + x) * 4 + 3] = 255;
		}
	return px;
}

/* The showcase: a sculpted island valley, splat painted, with a forest,
 * grass and a lake. */
static LrgTerrainAsset *
showcase_terrain (void)
{
	LrgTerrainAsset           *t = lrg_terrain_asset_new (32, 32.0f, 8, 8);
	g_autoptr(LrgTerrainBrush) b = lrg_terrain_brush_new (LRG_TERRAIN_BRUSH_NOISE);
	g_autoptr(LrgFoliageLayer) trees = lrg_foliage_layer_new ("pines", LRG_FOLIAGE_KIND_MESH);
	g_autoptr(LrgFoliageLayer) oaks = lrg_foliage_layer_new ("oaks", LRG_FOLIAGE_KIND_MESH);
	g_autoptr(LrgFoliageLayer) grass = lrg_foliage_layer_new ("grass", LRG_FOLIAGE_KIND_GRASS);
	static const guint8        g1[3] = { 70, 110, 40 }, g2[3] = { 110, 150, 60 };
	static const guint8        r1[3] = { 95, 90, 85 }, r2[3] = { 150, 145, 135 };
	static const guint8        s1[3] = { 190, 170, 120 }, s2[3] = { 220, 200, 150 };
	static const guint8        d1[3] = { 90, 70, 45 }, d2[3] = { 120, 95, 60 };
	g_autofree guint8         *tex = NULL;
	LrgTerrainLayer           *l;
	LrgFoliage                *f;
	guint                      k, i, j, nx;
	gfloat                     x, z;

	/* 1. sculpt: rolling noise, a ring of hills, a lake basin, smoothing */
	lrg_terrain_brush_set_radius (b, 400.0f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT);
	lrg_terrain_brush_set_noise_scale (b, 60.0f);
	lrg_terrain_brush_set_strength (b, 5.0f);
	lrg_terrain_brush_set_seed (b, 3);
	lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, 128.0f, 128.0f, 1.0f));
	lrg_terrain_brush_set_noise_scale (b, 18.0f);
	lrg_terrain_brush_set_strength (b, 2.5f);
	lrg_terrain_brush_set_seed (b, 8);
	lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, 128.0f, 128.0f, 1.0f));
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_RAISE);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH);
	for (k = 0; k < 9; k++)
	{
		gfloat a = (gfloat) k / 9.0f * 2.0f * (gfloat) G_PI + 0.4f;

		lrg_terrain_brush_set_radius (b, 40.0f + 10.0f * (gfloat) (k % 3));
		lrg_terrain_brush_set_strength (b, 10.0f + 4.0f * (gfloat) (k % 4));
		lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, 128.0f + cosf (a) * 95.0f, 128.0f + sinf (a) * 95.0f,
		                                                1.0f));
	}
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_FLATTEN);
	lrg_terrain_brush_set_target_height (b, -4.0f);
	lrg_terrain_brush_set_radius (b, 42.0f);
	lrg_terrain_brush_set_strength (b, 1.0f);
	lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, 140.0f, 150.0f, 1.0f));
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_SMOOTH);
	lrg_terrain_brush_set_radius (b, 400.0f);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT);
	for (k = 0; k < 4; k++)
		lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, 128.0f, 128.0f, 1.0f));

	/* 2. layers with procedural textures */
	l = lrg_terrain_asset_get_layer (t, 0);
	lrg_terrain_layer_set_name (l, "grass");
	lrg_terrain_layer_set_tiling (l, 6.0f);
	tex = noise_texture (64, g1, g2, 1);
	lrg_terrain_layer_set_pixels (l, 64, 64, tex);
	g_clear_pointer (&tex, g_free);
	l = lrg_terrain_asset_add_layer (t, "rock");
	lrg_terrain_layer_set_tiling (l, 9.0f);
	tex = noise_texture (64, r1, r2, 2);
	lrg_terrain_layer_set_pixels (l, 64, 64, tex);
	g_clear_pointer (&tex, g_free);
	l = lrg_terrain_asset_add_layer (t, "sand");
	lrg_terrain_layer_set_tiling (l, 5.0f);
	tex = noise_texture (64, s1, s2, 3);
	lrg_terrain_layer_set_pixels (l, 64, 64, tex);
	g_clear_pointer (&tex, g_free);
	l = lrg_terrain_asset_add_layer (t, "dirt");
	lrg_terrain_layer_set_tiling (l, 5.0f);
	tex = noise_texture (64, d1, d2, 4);
	lrg_terrain_layer_set_pixels (l, 64, 64, tex);
	g_clear_pointer (&tex, g_free);

	/* 3. splat painting: rock on slopes and peaks, sand at the shore, a
	 * dirt path, by brush dabs over the samples that need them */
	lrg_terrain_asset_get_samples (t, &nx, NULL);
	lrg_terrain_brush_set_op (b, LRG_TERRAIN_BRUSH_PAINT);
	lrg_terrain_brush_set_falloff (b, LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH);
	lrg_terrain_brush_set_radius (b, 3.0f);
	lrg_terrain_brush_set_strength (b, 0.9f);
	for (j = 0; j < nx; j += 2)
		for (i = 0; i < nx; i += 2)
		{
			gfloat n[3], h;

			x = (gfloat) i;
			z = (gfloat) j;
			lrg_terrain_asset_sample_normal (t, x, z, n);
			lrg_terrain_asset_sample_height (t, x, z, &h);
			if (n[1] < 0.82f || h > 17.0f)
				lrg_terrain_brush_set_layer (b, 1);
			else if (h < 0.6f)
				lrg_terrain_brush_set_layer (b, 2);
			else
				continue;
			lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, x, z, 1.0f));
		}
	lrg_terrain_brush_set_layer (b, 3);
	lrg_terrain_brush_set_radius (b, 2.5f);
	for (k = 0; k < 60; k++)
	{
		x = 60.0f + (gfloat) k * 1.6f;
		z = 220.0f - (gfloat) k * 0.9f + sinf ((gfloat) k * 0.2f) * 6.0f;
		lrg_terrain_edit_free (lrg_terrain_brush_apply (b, t, x, z, 1.0f));
	}

	/* 4. foliage: pines on the hills, oaks in the valley, grass near */
	f = lrg_terrain_asset_get_foliage (t);
	lrg_foliage_set_seed (f, 2026);
	lrg_foliage_layer_set_mesh (trees, "builtin:conifer");
	lrg_foliage_layer_set_size (trees, 1.0f, 9.0f);
	lrg_foliage_layer_set_density (trees, 0.006f);
	lrg_foliage_layer_set_height_range (trees, 2.0f, 30.0f);
	lrg_foliage_layer_set_slope_range (trees, 0.0f, 32.0f);
	lrg_foliage_layer_set_material_density (trees, 2, 0.0f);
	lrg_foliage_layer_set_material_density (trees, 3, 0.0f);
	lrg_foliage_layer_set_fade (trees, 260.0f, 320.0f);
	lrg_foliage_add_layer (f, trees);
	lrg_foliage_layer_set_mesh (oaks, "builtin:broadleaf");
	lrg_foliage_layer_set_size (oaks, 1.0f, 7.0f);
	lrg_foliage_layer_set_density (oaks, 0.006f);
	lrg_foliage_layer_set_height_range (oaks, 1.0f, 12.0f);
	lrg_foliage_layer_set_slope_range (oaks, 0.0f, 20.0f);
	lrg_foliage_layer_set_material_density (oaks, 1, 0.0f);
	lrg_foliage_layer_set_material_density (oaks, 2, 0.0f);
	lrg_foliage_layer_set_material_density (oaks, 3, 0.0f);
	lrg_foliage_layer_set_seed (oaks, 5);
	lrg_foliage_layer_set_fade (oaks, 260.0f, 320.0f);
	lrg_foliage_add_layer (f, oaks);
	lrg_foliage_layer_set_density (grass, 3.0f);
	lrg_foliage_layer_set_height_range (grass, 0.8f, 20.0f);
	lrg_foliage_layer_set_material_density (grass, 1, 0.0f);
	lrg_foliage_layer_set_material_density (grass, 2, 0.0f);
	lrg_foliage_layer_set_material_density (grass, 3, 0.0f);
	lrg_foliage_layer_set_size (grass, 0.35f, 0.6f);
	lrg_foliage_layer_set_fade (grass, 40.0f, 60.0f);
	lrg_foliage_add_layer (f, grass);
	return t;
}

static void
test_gpu_render (void)
{
	const gchar                    *display = g_getenv ("DISPLAY");
	const gchar                    *shot = g_getenv ("LRG_TERRAIN_SHOT");
	gint                            W = shot != NULL ? 1920 : 320, H = shot != NULL ? 1080 : 180;
	g_autoptr(LrgWorld3D)           world = NULL;
	g_autoptr(LrgEntity3D)          ground = NULL;
	g_autoptr(LrgEntity3D)          lake = NULL;
	g_autoptr(LrgEntity3D)          sun = NULL;
	g_autoptr(LrgTerrainAsset)      t = NULL;
	g_autoptr(LrgTerrainComponent)  tc = NULL;
	g_autoptr(LrgWaterComponent)    wc = NULL;
	g_autoptr(LrgLightComponent)    lc = NULL;
	g_autoptr(LrgWaterBody)         body = NULL;
	g_autoptr(LrgCamera3D)          cam = NULL;
	g_autoptr(LrgWorld3DRenderer)   r = NULL;
	g_autoptr(GrlRenderTexture)     target = NULL;
	g_autoptr(GrlTexture)           tex = NULL;
	g_autoptr(GrlImage)             grl = NULL;
	const guint8                   *pixels;
	gsize                           size = 0;
	Image                           img;
	guint                           k, green = 0, blue = 0;
	gint64                          t0;
	gint                            i;

	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
	{
		g_test_skip ("needs an isolated X display (build-aux/xrun.sh)");
		return;
	}
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
	InitWindow (W, H, "test-terrain-asset");
	if (!IsWindowReady ())
	{
		g_test_skip ("no GL context");
		return;
	}

	world = lrg_world3d_new ();
	t0 = g_get_monotonic_time ();
	t = showcase_terrain ();
	g_test_message ("showcase build + sculpt + paint: %.1f ms", (gdouble) (g_get_monotonic_time () - t0) / 1000.0);
	{
		guint              n_inst;
		g_autofree guint8 *bake = g_malloc (256 * 256 * 4);

		t0 = g_get_monotonic_time ();
		n_inst = lrg_terrain_asset_scatter_foliage (t);
		g_test_message ("foliage: %u instances, scatter %.1f ms", n_inst,
		                (gdouble) (g_get_monotonic_time () - t0) / 1000.0);
		t0 = g_get_monotonic_time ();
		for (k = 0; k < 64; k++)
			lrg_terrain_asset_bake_chunk (t, k % 8, k / 8, bake);
		g_test_message ("splat bake: %.2f ms per 256-texel chunk (4 layers)",
		                (gdouble) (g_get_monotonic_time () - t0) / 64000.0);
	}
	tc = lrg_terrain_component_new_with_asset (t);
	ground = lrg_entity3d_new_with_name ("terrain");
	lrg_entity3d_set_position_xyz (ground, -128.0f, 0.0f, -128.0f);
	lrg_entity3d_add_component (ground, LRG_COMPONENT (tc));
	lrg_world3d_add_entity (world, ground);

	body = lrg_water_body_new (LRG_WATER_BODY_LAKE);
	for (k = 0; k < 24; k++)
	{
		gfloat a = (gfloat) k / 24.0f * 2.0f * (gfloat) G_PI;

		lrg_water_body_add_point (body, cosf (a) * (46.0f + 6.0f * sinf (a * 3.0f)), 0.0f,
		                          sinf (a) * (40.0f + 5.0f * cosf (a * 2.0f)), 1.0f);
	}
	lrg_water_body_set_level (body, 0.0f);
	lrg_water_body_set_cell_size (body, 1.0f);
	wc = lrg_water_component_new ();
	lrg_water_component_set_body (wc, body);
	lake = lrg_entity3d_new_with_name ("lake");
	lrg_entity3d_set_position_xyz (lake, 12.0f, 0.0f, 22.0f);
	lrg_entity3d_add_component (lake, LRG_COMPONENT (wc));
	lrg_world3d_add_entity (world, lake);

	lc = lrg_light_component_new_with_kind (LRG_LIGHT3D_DIRECTIONAL);
	sun = lrg_entity3d_new_with_name ("sun");
	{
		gfloat euler[3] = { -0.75f, 0.9f, 0.0f };

		lrg_entity3d_set_rotation_euler (sun, euler);
	}
	lrg_entity3d_add_component (sun, LRG_COMPONENT (lc));
	lrg_world3d_add_entity (world, sun);

	cam = lrg_camera3d_new ();
	lrg_camera3d_set_position_xyz (cam, -20.0f, 38.0f, 115.0f);
	lrg_camera3d_set_target_xyz (cam, 14.0f, 0.0f, 15.0f);
	lrg_camera3d_set_fovy (cam, 55.0f);

	r = lrg_world3d_renderer_new ();
	target = grl_render_texture_new (W, H);
	for (k = 0; k < 8; k++)
	{
		lrg_world3d_update (world, 0.4f);
		t0 = g_get_monotonic_time ();
		g_assert_true (lrg_world3d_renderer_render (r, world, cam, target, 0, 0));
		g_test_message ("frame %u: %.2f ms", k, (gdouble) (g_get_monotonic_time () - t0) / 1000.0);
	}
	{
		g_autoptr(LrgRenderList) list = lrg_render_list_new ();

		t0 = g_get_monotonic_time ();
		for (k = 0; k < 100; k++)
			lrg_world3d_collect_render (world, LRG_CAMERA (cam), list);
		g_test_message ("collect_render: %.3f ms", (gdouble) (g_get_monotonic_time () - t0) / 100000.0);
	}

	tex = grl_render_texture_get_texture (target);
	grl = grl_texture_to_image (tex);
	pixels = grl_image_get_pixels (grl, &size);
	g_assert_cmpuint (size, ==, (gsize) W * H * 4);
	memset (&img, 0, sizeof img);
	img.data = MemAlloc ((guint) size);
	memcpy (img.data, pixels, size);
	img.width = W;
	img.height = H;
	img.mipmaps = 1;
	img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	ImageFlipVertical (&img);

	if (shot != NULL)
		g_assert_true (ExportImage (img, shot));

	/* terrain (green-dominant) and water (blue-dominant) both show */
	for (i = 0; i < W * H; i++)
	{
		const guint8 *p = (const guint8 *) img.data + (gsize) i * 4;

		if (p[1] > p[0] + 8 && p[1] > p[2] + 8)
			green++;
		if (p[2] > p[0] + 10 && p[2] >= p[1] && i > W * H / 2)
			blue++;
	}
	g_assert_cmpuint (green, >, (guint) (W * H / 20));
	g_assert_cmpuint (blue, >, (guint) (W * H / 200));
	g_assert_cmpuint (lrg_foliage_get_instance_count (lrg_terrain_asset_get_foliage (t), 0), >, 50);
	UnloadImage (img);

	lrg_terrain_component_release_gpu (tc);
	lrg_world3d_renderer_release (r);
	g_clear_object (&target);
	CloseWindow ();
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/terrain-asset/new", test_asset_new);
	g_test_add_func ("/terrain-asset/sample", test_asset_sample);
	g_test_add_func ("/terrain-asset/round-trip", test_asset_round_trip);
	g_test_add_func ("/terrain-asset/round-trip-r32-ids", test_asset_round_trip_r32);
	g_test_add_func ("/terrain-asset/unknown-keys", test_asset_unknown_keys);
	g_test_add_func ("/terrain-asset/errors", test_asset_errors);
	g_test_add_func ("/terrain-asset/sidecar-errors", test_asset_sidecar_errors);
	g_test_add_func ("/terrain-asset/mesh", test_asset_mesh);
	g_test_add_func ("/terrain-asset/bake", test_asset_bake);
	g_test_add_func ("/terrain-asset/physics", test_asset_physics);
	g_test_add_func ("/terrain-asset/raycast", test_asset_raycast);
	g_test_add_func ("/terrain-asset/resize", test_asset_resize);
	g_test_add_func ("/terrain-brush/raise-lower", test_brush_raise_lower);
	g_test_add_func ("/terrain-brush/flatten-smooth-noise", test_brush_flatten_smooth_noise);
	g_test_add_func ("/terrain-brush/paint", test_brush_paint);
	g_test_add_func ("/terrain-brush/undo", test_brush_undo);
	g_test_add_func ("/terrain-brush/holes-foliage", test_brush_holes_foliage);
	g_test_add_func ("/terrain-brush/falloff", test_brush_falloff);
	g_test_add_func ("/foliage/determinism", test_foliage_determinism);
	g_test_add_func ("/foliage/rules", test_foliage_rules);
	g_test_add_func ("/foliage/dirty", test_foliage_dirty);
	g_test_add_func ("/water-body/round-trip", test_water_round_trip);
	g_test_add_func ("/water-body/errors", test_water_errors);
	g_test_add_func ("/water-body/build", test_water_build);
	g_test_add_func ("/terrain-components/headless", test_components_headless);
	g_test_add_func ("/terrain-components/gpu", test_gpu_render);
	return g_test_run ();
}
