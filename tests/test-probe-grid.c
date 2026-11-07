/* test-probe-grid.c - baked lighting runtime (0.9.0 M3)
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmap3D (entries, CPU sampling, .rlightmap round trip),
 * LrgProbeGrid (layout, SH sampling, trilinear blend, invalid probe
 * fill, .rprobes round trip), the world accessors, light modes, the mesh
 * renderer's bake properties, the level `lighting:` block (serializer
 * round trip and instantiation onto a world) and, under the isolated X
 * display (xrun.sh), the lit shader's baked hook through
 * LrgWorld3DRenderer: a lightmapped quad shows its baked colours through
 * uv2, a probe-lit sphere shows its SH colour, baked lights skip
 * lightmapped draws, and draws without baked data keep the inactive
 * (ambient) state.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>
#include <math.h>
#include <string.h>

#define GPU_W (128)
#define GPU_H (128)
#define SH0   (0.282095f)

static gboolean gpu_ready = FALSE;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* SH coefficients of a constant irradiance / pi @rgb (only the L0 band) */
static void
sh_constant (gfloat       *sh,
             const gfloat *rgb)
{
	memset (sh, 0, LRG_PROBE_GRID_SH_FLOATS * sizeof (gfloat));
	sh[0] = rgb[0] / SH0;
	sh[1] = rgb[1] / SH0;
	sh[2] = rgb[2] / SH0;
}

/* a @w x @h page filled with @rgb */
static LrgHdrImage *
flat_page (guint         w,
           guint         h,
           const gfloat *rgb)
{
	LrgHdrImage *img = lrg_hdr_image_new (w, h);
	guint        x, y;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			lrg_hdr_image_set_pixel (img, x, y, rgb);
	return img;
}

/* counts signal emissions into @counter */
static void
count_signal (guint *counter)
{
	(*counter)++;
}

/* removes every file of @dir, then @dir */
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

/* ==========================================================================
 * LrgLightmap3D
 * ========================================================================== */

/* Entries: set, look up, replace, remove; sorted GUIDs; settings. */
static void
test_lightmap_entries (void)
{
	g_autoptr(LrgLightmap3D) lm = lrg_lightmap3d_new ();
	g_autoptr(GPtrArray)     guids = NULL;
	g_autoptr(GPtrArray)     keys = NULL;
	const gfloat             so_a[4] = { 0.5f, 0.25f, 0.0f, 0.75f };
	const gfloat             so_b[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
	gfloat                   so[4];
	guint                    page = 99;
	gdouble                  v = 0.0;

	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 0);
	g_assert_false (lrg_lightmap3d_get_entry (lm, "nope", &page, so));
	g_assert_false (lrg_lightmap3d_get_entry (lm, NULL, NULL, NULL));

	lrg_lightmap3d_set_entry (lm, "b-guid", 1, so_b);
	lrg_lightmap3d_set_entry (lm, "a-guid", 0, so_a);
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 2);
	g_assert_true (lrg_lightmap3d_get_entry (lm, "a-guid", &page, so));
	g_assert_cmpuint (page, ==, 0);
	g_assert_cmpmem (so, sizeof so, so_a, sizeof so_a);
	g_assert_true (lrg_lightmap3d_get_entry (lm, "b-guid", NULL, NULL));

	/* replacing keeps the count */
	lrg_lightmap3d_set_entry (lm, "a-guid", 1, so_b);
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 2);
	g_assert_true (lrg_lightmap3d_get_entry (lm, "a-guid", &page, so));
	g_assert_cmpuint (page, ==, 1);

	guids = lrg_lightmap3d_get_guids (lm);
	g_assert_cmpuint (guids->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (guids, 0), ==, "a-guid");
	g_assert_cmpstr (g_ptr_array_index (guids, 1), ==, "b-guid");

	g_assert_true (lrg_lightmap3d_remove_entry (lm, "a-guid"));
	g_assert_false (lrg_lightmap3d_remove_entry (lm, "a-guid"));
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 1);

	lrg_lightmap3d_set_setting (lm, "texels_per_unit", 8.0);
	lrg_lightmap3d_set_setting (lm, "denoise", 1.0);
	g_assert_true (lrg_lightmap3d_get_setting (lm, "texels_per_unit", &v));
	g_assert_cmpfloat (v, ==, 8.0);
	g_assert_false (lrg_lightmap3d_get_setting (lm, "bounces", NULL));
	keys = lrg_lightmap3d_get_setting_keys (lm);
	g_assert_cmpuint (keys->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (keys, 0), ==, "denoise");

	lrg_lightmap3d_clear (lm);
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 0);
	g_assert_false (lrg_lightmap3d_get_setting (lm, "denoise", NULL));
}

/* Bilinear sampling with texel centres and clamped edges, through an
 * entry's scale / offset, and the intensity. */
static void
test_lightmap_sample (void)
{
	g_autoptr(LrgLightmap3D) lm = lrg_lightmap3d_new ();
	g_autoptr(LrgHdrImage)   img = lrg_hdr_image_new (2, 1);
	const gfloat             red[3] = { 1.0f, 0.0f, 0.0f };
	const gfloat             blue[3] = { 0.0f, 0.0f, 1.0f };
	const gfloat             so[4] = { 0.5f, 1.0f, 0.5f, 0.0f };   /* uv2 -> the right half */
	gfloat                   uv[2];
	gfloat                   rgb[3];
	guint                    changed = 0;

	lrg_hdr_image_set_pixel (img, 0, 0, red);
	lrg_hdr_image_set_pixel (img, 1, 0, blue);
	g_signal_connect_swapped (lm, "changed", G_CALLBACK (count_signal), &changed);
	g_assert_cmpuint (lrg_lightmap3d_add_page (lm, img), ==, 0);
	g_assert_cmpuint (changed, ==, 1);
	g_assert_cmpuint (lrg_lightmap3d_get_n_pages (lm), ==, 1);
	g_assert_true (lrg_lightmap3d_get_page (lm, 0) == img);
	g_assert_null (lrg_lightmap3d_get_page (lm, 1));

	/* texel centres: exact; the middle: the average; edges clamp */
	g_assert_true (lrg_lightmap3d_sample_page (lm, 0, 0.25f, 0.5f, rgb));
	g_assert_cmpfloat_with_epsilon (rgb[0], 1.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (rgb[2], 0.0f, 1e-5);
	lrg_lightmap3d_sample_page (lm, 0, 0.5f, 0.5f, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 0.5f, 1e-5);
	g_assert_cmpfloat_with_epsilon (rgb[2], 0.5f, 1e-5);
	lrg_lightmap3d_sample_page (lm, 0, -3.0f, 0.5f, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 1.0f, 1e-5);
	lrg_lightmap3d_sample_page (lm, 0, 1.0f, 0.0f, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[2], 1.0f, 1e-5);
	g_assert_false (lrg_lightmap3d_sample_page (lm, 3, 0.5f, 0.5f, rgb));
	g_assert_cmpfloat (rgb[0], ==, 0.0f);

	/* through an entry: uv2 (0.5, 0.5) lands on the blue texel centre */
	lrg_lightmap3d_set_entry (lm, "g", 0, so);
	uv[0] = 0.5f;
	uv[1] = 0.5f;
	g_assert_true (lrg_lightmap3d_sample (lm, "g", uv, rgb));
	g_assert_cmpfloat_with_epsilon (rgb[2], 1.0f, 1e-5);
	g_assert_false (lrg_lightmap3d_sample (lm, "other", uv, rgb));

	/* intensity scales samples */
	lrg_lightmap3d_set_intensity (lm, 2.0f);
	lrg_lightmap3d_sample (lm, "g", uv, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[2], 2.0f, 1e-5);
	lrg_lightmap3d_set_intensity (lm, -1.0f);
	g_assert_cmpfloat (lrg_lightmap3d_get_intensity (lm), ==, 0.0f);
}

/* .rlightmap index + .hdr pages: save, load, compare. */
static void
test_lightmap_round_trip (void)
{
	g_autoptr(LrgLightmap3D) lm = lrg_lightmap3d_new ();
	g_autoptr(LrgLightmap3D) back = NULL;
	g_autoptr(LrgHdrImage)   p0 = NULL;
	g_autoptr(LrgHdrImage)   p1 = NULL;
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *dir = g_dir_make_tmp ("lrg-lightmap-XXXXXX", NULL);
	g_autofree gchar        *path = g_build_filename (dir, "level.rlightmap", NULL);
	g_autofree gchar        *page1 = g_build_filename (dir, "level-1.hdr", NULL);
	const gfloat             warm[3] = { 0.8f, 0.5f, 0.25f };
	const gfloat             cool[3] = { 0.125f, 0.25f, 1.5f };
	const gfloat             so[4] = { 0.25f, 0.5f, 0.125f, 0.375f };
	gfloat                   so_back[4];
	gfloat                   a[3], b[3];
	guint                    page = 0;
	gdouble                  v = 0.0;

	p0 = flat_page (4, 4, warm);
	p1 = flat_page (8, 2, cool);
	lrg_lightmap3d_add_page (lm, p0);
	lrg_lightmap3d_add_page (lm, p1);
	lrg_lightmap3d_set_entry (lm, "00000000-0000-0000-0000-000000000001", 1, so);
	lrg_lightmap3d_set_setting (lm, "samples", 256.0);
	lrg_lightmap3d_set_intensity (lm, 1.5f);

	g_assert_true (lrg_lightmap3d_save (lm, path, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_lightmap3d_get_path (lm), ==, path);
	g_assert_true (g_file_test (page1, G_FILE_TEST_EXISTS));

	back = lrg_lightmap3d_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (back);
	g_assert_cmpstr (lrg_lightmap3d_get_path (back), ==, path);
	g_assert_cmpuint (lrg_lightmap3d_get_n_pages (back), ==, 2);
	g_assert_cmpuint (lrg_hdr_image_get_width (lrg_lightmap3d_get_page (back, 1)), ==, 8);
	g_assert_cmpuint (lrg_hdr_image_get_height (lrg_lightmap3d_get_page (back, 1)), ==, 2);
	g_assert_true (lrg_lightmap3d_get_entry (back, "00000000-0000-0000-0000-000000000001", &page, so_back));
	g_assert_cmpuint (page, ==, 1);
	g_assert_cmpmem (so_back, sizeof so_back, so, sizeof so);
	g_assert_true (lrg_lightmap3d_get_setting (back, "samples", &v));
	g_assert_cmpfloat (v, ==, 256.0);
	g_assert_cmpfloat_with_epsilon (lrg_lightmap3d_get_intensity (back), 1.5f, 1e-6);

	/* RGBE keeps about 1% of the brightest channel of a texel */
	lrg_lightmap3d_sample_page (lm, 1, 0.3f, 0.6f, a);
	lrg_lightmap3d_sample_page (back, 1, 0.3f, 0.6f, b);
	g_assert_cmpfloat_with_epsilon (a[0], b[0], a[2] * 0.02f);
	g_assert_cmpfloat_with_epsilon (a[2], b[2], a[2] * 0.02f);

	/* errors: a missing file, a newer version, a broken entry */
	g_clear_object (&back);
	back = lrg_lightmap3d_new_from_file ("/nonexistent/x.rlightmap", &error);
	g_assert_null (back);
	g_assert_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_IO);
	g_clear_error (&error);
	g_assert_true (g_file_set_contents (path, "version: 99\n", -1, NULL));
	back = lrg_lightmap3d_new_from_file (path, &error);
	g_assert_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_VERSION);
	g_clear_error (&error);
	g_assert_true (g_file_set_contents (path, "version: 1\nentries:\n  x: { page: 0 }\n", -1, NULL));
	back = lrg_lightmap3d_new_from_file (path, &error);
	g_assert_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_FORMAT);

	remove_dir (dir);
}

/* ==========================================================================
 * LrgProbeGrid
 * ========================================================================== */

/* Box, resolution, indices, probe positions on the lattice. */
static void
test_probe_layout (void)
{
	const gfloat             mn[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat             mx[3] = { 4.0f, 2.0f, 6.0f };
	g_autoptr(LrgProbeGrid)  grid = lrg_probe_grid_new_with_bounds (mx, mn, 3, 2, 4);   /* swapped corners */
	g_autoptr(LrgProbeGrid)  def = lrg_probe_grid_new ();
	gfloat                   bmin[3], bmax[3], pos[3];
	guint                    nx, ny, nz, idx;
	const gfloat             in[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat             out[3] = { 1.0f, 3.0f, 1.0f };

	g_assert_cmpuint (lrg_probe_grid_get_n_probes (def), ==, 8);
	g_assert_cmpuint (lrg_probe_grid_get_n_valid (def), ==, 0);

	lrg_probe_grid_get_bounds (grid, bmin, bmax);
	g_assert_cmpmem (bmin, sizeof bmin, mn, sizeof mn);
	g_assert_cmpmem (bmax, sizeof bmax, mx, sizeof mx);
	lrg_probe_grid_get_resolution (grid, &nx, &ny, &nz);
	g_assert_cmpuint (nx, ==, 3);
	g_assert_cmpuint (ny, ==, 2);
	g_assert_cmpuint (nz, ==, 4);
	g_assert_cmpuint (lrg_probe_grid_get_n_probes (grid), ==, 24);

	idx = lrg_probe_grid_get_index (grid, 2, 1, 3);
	g_assert_cmpuint (idx, ==, 2 + 3 * (1 + 2 * 3));
	g_assert_cmpuint (lrg_probe_grid_get_index (grid, 3, 0, 0), ==, G_MAXUINT);
	g_assert_true (lrg_probe_grid_get_probe_position (grid, idx, pos));
	g_assert_cmpfloat_with_epsilon (pos[0], 4.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (pos[1], 2.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (pos[2], 6.0f, 1e-6);
	lrg_probe_grid_get_probe_position (grid, lrg_probe_grid_get_index (grid, 1, 0, 1), pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 2.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (pos[2], 2.0f, 1e-6);
	g_assert_false (lrg_probe_grid_get_probe_position (grid, 24, pos));

	g_assert_true (lrg_probe_grid_contains (grid, in));
	g_assert_false (lrg_probe_grid_contains (grid, out));

	/* a single probe on an axis sits at the centre; resolution clamps */
	lrg_probe_grid_set_resolution (grid, 1, 0, 1000);
	lrg_probe_grid_get_resolution (grid, &nx, &ny, &nz);
	g_assert_cmpuint (nx, ==, 1);
	g_assert_cmpuint (ny, ==, 1);
	g_assert_cmpuint (nz, ==, LRG_PROBE_GRID_MAX_RESOLUTION);
	lrg_probe_grid_get_probe_position (grid, 0, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 2.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (pos[1], 1.0f, 1e-6);
}

/* SH sampling: a constant grid gives its colour for every normal; the
 * trilinear blend between two probes; an L1 term follows the normal. */
static void
test_probe_sample (void)
{
	const gfloat             mn[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat             mx[3] = { 2.0f, 2.0f, 2.0f };
	g_autoptr(LrgProbeGrid)  grid = lrg_probe_grid_new_with_bounds (mn, mx, 2, 2, 2);
	const gfloat             grey[3] = { 0.5f, 0.5f, 0.5f };
	const gfloat             red[3] = { 1.0f, 0.0f, 0.0f };
	const gfloat             up[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat             side[3] = { 1.0f, 0.0f, 0.0f };
	const gfloat             mid[3] = { 0.5f, 1.0f, 1.0f };
	const gfloat             far[3] = { 50.0f, -5.0f, 1.0f };
	gfloat                   sh[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                   sh_red[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                   rgb[3];
	guint                    i, x, y, z;

	/* nothing valid: FALSE, black */
	g_assert_false (lrg_probe_grid_sample (grid, mid, up, rgb));
	g_assert_cmpfloat (rgb[0], ==, 0.0f);

	sh_constant (sh, grey);
	for (i = 0; i < lrg_probe_grid_get_n_probes (grid); i++)
		g_assert_true (lrg_probe_grid_set_probe (grid, i, sh, TRUE));
	g_assert_cmpuint (lrg_probe_grid_get_n_valid (grid), ==, 8);
	g_assert_true (lrg_probe_grid_sample (grid, mid, up, rgb));
	g_assert_cmpfloat_with_epsilon (rgb[0], 0.5f, 1e-4);
	lrg_probe_grid_sample (grid, mid, side, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[1], 0.5f, 1e-4);

	/* the x = 1 face red: at x = 0.5 (a quarter of the way) red is 1/4 */
	sh_constant (sh_red, red);
	for (z = 0; z < 2; z++)
		for (y = 0; y < 2; y++)
		{
			for (x = 0; x < 2; x++)
				lrg_probe_grid_set_probe (grid, lrg_probe_grid_get_index (grid, x, y, z), x == 1 ? sh_red : sh, TRUE);
		}
	lrg_probe_grid_sample (grid, mid, up, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 0.75f * 0.5f + 0.25f * 1.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (rgb[1], 0.75f * 0.5f, 1e-4);

	/* outside the box: clamped onto it (x = 2 face: all red) */
	lrg_probe_grid_sample (grid, far, up, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 1.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (rgb[1], 0.0f, 1e-4);

	/* the interpolated coefficients themselves, and the intensity */
	g_assert_true (lrg_probe_grid_sample_sh (grid, mid, sh));
	g_assert_cmpfloat_with_epsilon (sh[0] * SH0, 0.75f * 0.5f + 0.25f, 1e-4);
	lrg_probe_grid_set_intensity (grid, 2.0f);
	lrg_probe_grid_sample (grid, far, up, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 2.0f, 1e-4);
	lrg_probe_grid_set_intensity (grid, 1.0f);

	/* an L1 (y) term: brighter facing up than down, as LrgEnvironment */
	{
		gfloat        dir_sh[LRG_PROBE_GRID_SH_FLOATS];
		const gfloat  down[3] = { 0.0f, -1.0f, 0.0f };
		gfloat        up_rgb[3], down_rgb[3], env_rgb[3];

		sh_constant (dir_sh, grey);
		dir_sh[3] = dir_sh[4] = dir_sh[5] = 0.5f;   /* c1 (Y1-1, y) */
		for (i = 0; i < lrg_probe_grid_get_n_probes (grid); i++)
			lrg_probe_grid_set_probe (grid, i, dir_sh, TRUE);
		lrg_probe_grid_sample (grid, mid, up, up_rgb);
		lrg_probe_grid_sample (grid, mid, down, down_rgb);
		g_assert_cmpfloat (up_rgb[0], >, down_rgb[0]);
		lrg_environment_sh_irradiance (dir_sh, up, env_rgb);
		g_assert_cmpfloat_with_epsilon (up_rgb[0], env_rgb[0], 1e-5);
	}
}

/* An invalid probe takes its valid neighbours' average instead of
 * pulling black into the blend; validity is kept per probe. */
static void
test_probe_invalid (void)
{
	const gfloat             mn[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat             mx[3] = { 3.0f, 1.0f, 1.0f };
	g_autoptr(LrgProbeGrid)  grid = lrg_probe_grid_new_with_bounds (mn, mx, 4, 1, 1);
	const gfloat             grey[3] = { 0.5f, 0.5f, 0.5f };
	const gfloat             black[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat             up[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat             p_bad[3] = { 1.0f, 0.5f, 0.5f };
	const gfloat             p_far[3] = { 3.0f, 0.5f, 0.5f };
	gfloat                   sh[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                   sh_black[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                   rgb[3];

	sh_constant (sh, grey);
	sh_constant (sh_black, black);
	/* probe 0 valid grey; 1, 2, 3 invalid (black, "inside a wall") */
	lrg_probe_grid_set_probe (grid, 0, sh, TRUE);
	lrg_probe_grid_set_probe (grid, 1, sh_black, FALSE);
	lrg_probe_grid_set_probe (grid, 2, sh_black, FALSE);
	lrg_probe_grid_set_probe (grid, 3, sh_black, FALSE);
	g_assert_cmpuint (lrg_probe_grid_get_n_valid (grid), ==, 1);
	g_assert_false (lrg_probe_grid_get_probe (grid, 1, NULL));
	g_assert_true (lrg_probe_grid_get_probe (grid, 0, NULL));

	/* flood fill: every probe resolves to the grey */
	lrg_probe_grid_sample (grid, p_bad, up, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 0.5f, 1e-4);
	lrg_probe_grid_sample (grid, p_far, up, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], 0.5f, 1e-4);

	/* the stored data is untouched */
	g_assert_false (lrg_probe_grid_get_probe (grid, 3, sh));
	g_assert_cmpfloat (sh[0], ==, 0.0f);

	/* bulk writes through the storage + notify_changed */
	{
		gsize    nf = 0;
		guint    n = 0;
		gfloat  *data = lrg_probe_grid_get_data (grid, &nf);
		guint8  *valid = lrg_probe_grid_get_validity (grid, &n);

		g_assert_cmpuint (nf, ==, 4 * LRG_PROBE_GRID_SH_FLOATS);
		g_assert_cmpuint (n, ==, 4);
		data[3 * LRG_PROBE_GRID_SH_FLOATS] = 1.0f / SH0;   /* probe 3: red 1 */
		valid[3] = 1;
		lrg_probe_grid_notify_changed (grid);
		g_assert_cmpuint (lrg_probe_grid_get_n_valid (grid), ==, 2);
		lrg_probe_grid_sample (grid, p_far, up, rgb);
		g_assert_cmpfloat_with_epsilon (rgb[0], 1.0f, 1e-4);
	}
}

/* .rprobes: header + base64 coefficients and flags round trip. */
static void
test_probe_round_trip (void)
{
	const gfloat             mn[3] = { -1.0f, 0.0f, -2.0f };
	const gfloat             mx[3] = { 1.0f, 3.0f, 2.0f };
	g_autoptr(LrgProbeGrid)  grid = lrg_probe_grid_new_with_bounds (mn, mx, 3, 2, 2);
	g_autoptr(LrgProbeGrid)  back = NULL;
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *dir = g_dir_make_tmp ("lrg-probes-XXXXXX", NULL);
	g_autofree gchar        *path = g_build_filename (dir, "hall.rprobes", NULL);
	gfloat                   sh[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                   got[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                   bmin[3], bmax[3];
	guint                    i, k, nx, ny, nz;

	for (i = 0; i < lrg_probe_grid_get_n_probes (grid); i++)
	{
		for (k = 0; k < LRG_PROBE_GRID_SH_FLOATS; k++)
			sh[k] = (gfloat)i * 0.37f - (gfloat)k * 0.011f + 0.125f;
		lrg_probe_grid_set_probe (grid, i, sh, (i % 3) != 1);
	}
	lrg_probe_grid_set_intensity (grid, 0.75f);
	g_assert_true (lrg_probe_grid_save (grid, path, &error));
	g_assert_no_error (error);

	back = lrg_probe_grid_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (back);
	g_assert_cmpstr (lrg_probe_grid_get_path (back), ==, path);
	lrg_probe_grid_get_resolution (back, &nx, &ny, &nz);
	g_assert_cmpuint (nx * 100 + ny * 10 + nz, ==, 322);
	lrg_probe_grid_get_bounds (back, bmin, bmax);
	g_assert_cmpmem (bmin, sizeof bmin, mn, sizeof mn);
	g_assert_cmpmem (bmax, sizeof bmax, mx, sizeof mx);
	g_assert_cmpfloat_with_epsilon (lrg_probe_grid_get_intensity (back), 0.75f, 1e-6);
	for (i = 0; i < lrg_probe_grid_get_n_probes (grid); i++)
	{
		gboolean valid = lrg_probe_grid_get_probe (grid, i, sh);

		g_assert_cmpint (lrg_probe_grid_get_probe (back, i, got), ==, valid);
		g_assert_cmpmem (got, sizeof got, sh, sizeof sh);   /* bit exact */
	}

	/* a truncated payload and a newer version fail */
	g_clear_object (&back);
	g_assert_true (g_file_set_contents (path,
		"version: 1\nbounds: { min: [0, 0, 0], max: [1, 1, 1] }\nresolution: [2, 2, 2]\nsh: AAAA\n", -1, NULL));
	back = lrg_probe_grid_new_from_file (path, &error);
	g_assert_null (back);
	g_assert_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_FORMAT);
	g_clear_error (&error);
	g_assert_true (g_file_set_contents (path, "version: 7\n", -1, NULL));
	back = lrg_probe_grid_new_from_file (path, &error);
	g_assert_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_VERSION);
	g_clear_error (&error);
	g_assert_true (g_file_set_contents (path, "version: 1\nresolution: [2, 2]\n", -1, NULL));
	back = lrg_probe_grid_new_from_file (path, &error);
	g_assert_error (error, LRG_PROBE_GRID_ERROR, LRG_PROBE_GRID_ERROR_FORMAT);

	remove_dir (dir);
}

/* ==========================================================================
 * World, lights, components
 * ========================================================================== */

/* The world's lightmap and grids; the smallest containing grid wins. */
static void
test_world_accessors (void)
{
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgLightmap3D) lm = lrg_lightmap3d_new ();
	const gfloat             big_min[3] = { -10.0f, -10.0f, -10.0f };
	const gfloat             big_max[3] = { 10.0f, 10.0f, 10.0f };
	const gfloat             small_min[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat             small_max[3] = { 2.0f, 2.0f, 2.0f };
	g_autoptr(LrgProbeGrid)  big = lrg_probe_grid_new_with_bounds (big_min, big_max, 2, 2, 2);
	g_autoptr(LrgProbeGrid)  small = lrg_probe_grid_new_with_bounds (small_min, small_max, 2, 2, 2);
	const gfloat             p_both[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat             p_big[3] = { -5.0f, 0.0f, 0.0f };
	const gfloat             p_none[3] = { 50.0f, 0.0f, 0.0f };
	const gfloat             grey[3] = { 0.5f, 0.5f, 0.5f };
	gfloat                   sh[LRG_PROBE_GRID_SH_FLOATS];

	g_assert_null (lrg_world3d_get_lightmap (world));
	lrg_world3d_set_lightmap (world, lm);
	g_assert_true (lrg_world3d_get_lightmap (world) == lm);
	lrg_world3d_set_lightmap (world, NULL);
	g_assert_null (lrg_world3d_get_lightmap (world));

	sh_constant (sh, grey);
	lrg_probe_grid_set_probe (big, 0, sh, TRUE);
	lrg_world3d_add_probe_grid (world, big);
	lrg_world3d_add_probe_grid (world, small);
	lrg_world3d_add_probe_grid (world, big);   /* twice: once */
	g_assert_cmpuint (lrg_world3d_get_probe_grids (world)->len, ==, 2);

	/* the small grid has no valid probe yet: the big one lights */
	g_assert_true (lrg_world3d_find_probe_grid (world, p_both) == big);
	lrg_probe_grid_set_probe (small, 0, sh, TRUE);
	g_assert_true (lrg_world3d_find_probe_grid (world, p_both) == small);
	g_assert_true (lrg_world3d_find_probe_grid (world, p_big) == big);
	g_assert_null (lrg_world3d_find_probe_grid (world, p_none));

	g_assert_true (lrg_world3d_remove_probe_grid (world, small));
	g_assert_false (lrg_world3d_remove_probe_grid (world, small));
	lrg_world3d_clear_probe_grids (world);
	g_assert_cmpuint (lrg_world3d_get_probe_grids (world)->len, ==, 0);
}

/* The bake mode: light component -> description -> light set; the
 * mesh renderer's bake properties. */
static void
test_modes_and_properties (void)
{
	g_autoptr(LrgLightComponent)        light = lrg_light_component_new ();
	g_autoptr(LrgLightComponent)        sun = lrg_light_component_new_with_kind (LRG_LIGHT3D_DIRECTIONAL);
	g_autoptr(LrgLightSet)              set = lrg_light_set_new ();
	g_autoptr(LrgMeshRendererComponent) mr = lrg_mesh_renderer_component_new ();
	g_autoptr(GEnumClass)               klass = g_type_class_ref (LRG_TYPE_LIGHT_MODE);
	LrgLight3DDesc                      desc;
	LrgLightMode                        mode = LRG_LIGHT_MODE_REALTIME;
	guint                               id;
	gboolean                            st = FALSE;
	gfloat                              scale = 0.0f;

	g_assert_cmpstr (g_enum_get_value (klass, LRG_LIGHT_MODE_MIXED)->value_nick, ==, "mixed");
	lrg_light3d_desc_init (&desc);
	g_assert_cmpint (desc.bake_mode, ==, LRG_LIGHT_MODE_REALTIME);

	g_assert_cmpint (lrg_light_component_get_bake_mode (light), ==, LRG_LIGHT_MODE_REALTIME);
	g_object_set (light, "bake-mode", LRG_LIGHT_MODE_BAKED, NULL);
	g_object_get (light, "bake-mode", &mode, NULL);
	g_assert_cmpint (mode, ==, LRG_LIGHT_MODE_BAKED);
	lrg_light_component_fill_desc (light, &desc);
	g_assert_cmpint (desc.bake_mode, ==, LRG_LIGHT_MODE_BAKED);

	id = lrg_light_component_add_to_light_set (light, set);
	g_assert_cmpuint (id, !=, LRG_LIGHT_SET_INVALID_LIGHT);
	g_assert_cmpint (lrg_light_set_get_light_mode (set, id), ==, LRG_LIGHT_MODE_BAKED);
	lrg_light_set_set_light_mode (set, id, LRG_LIGHT_MODE_MIXED);
	g_assert_cmpint (lrg_light_set_get_light_mode (set, id), ==, LRG_LIGHT_MODE_MIXED);
	g_assert_cmpint (lrg_light_set_get_light_mode (set, 999), ==, LRG_LIGHT_MODE_REALTIME);

	lrg_light_component_set_bake_mode (sun, LRG_LIGHT_MODE_BAKED);
	lrg_light_component_add_to_light_set (sun, set);
	g_assert_cmpint (lrg_light_set_get_directional_mode (set), ==, LRG_LIGHT_MODE_BAKED);
	lrg_light_set_set_directional_mode (set, LRG_LIGHT_MODE_REALTIME);
	g_assert_cmpint (lrg_light_set_get_directional_mode (set), ==, LRG_LIGHT_MODE_REALTIME);

	g_assert_false (lrg_mesh_renderer_component_get_static_lighting (mr));
	g_assert_cmpfloat (lrg_mesh_renderer_component_get_lightmap_scale (mr), ==, 1.0f);
	g_object_set (mr, "static-lighting", TRUE, "lightmap-scale", 2.5f, NULL);
	g_object_get (mr, "static-lighting", &st, "lightmap-scale", &scale, NULL);
	g_assert_true (st);
	g_assert_cmpfloat (scale, ==, 2.5f);
	lrg_mesh_renderer_component_set_lightmap_scale (mr, 100.0f);
	g_assert_cmpfloat (lrg_mesh_renderer_component_get_lightmap_scale (mr), ==, 16.0f);
	lrg_mesh_renderer_component_set_lightmap_scale (mr, NAN);
	g_assert_cmpfloat (lrg_mesh_renderer_component_get_lightmap_scale (mr), ==, 1.0f);
}

/* ==========================================================================
 * Level `lighting:` block
 * ========================================================================== */

/* The block's API, the serializer round trip, and malformed blocks. */
static void
test_level_round_trip (void)
{
	g_autoptr(LrgLevel)           level = lrg_level_new ("lit");
	g_autoptr(LrgLevel)           back = NULL;
	g_autoptr(LrgLevel)           plain = lrg_level_new ("plain");
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(GError)             error = NULL;
	g_autoptr(GPtrArray)          keys = NULL;
	g_autofree gchar             *data = NULL;
	g_autofree gchar             *plain_data = NULL;
	GPtrArray                    *probes;
	gdouble                       v = 0.0;

	g_assert_false (lrg_level_has_lighting (level));
	lrg_level_set_lightmap_ref (level, "lightmaps/lit.rlightmap");
	lrg_level_add_probe_ref (level, "lightmaps/hall.rprobes");
	lrg_level_add_probe_ref (level, "guid:0123456789abcdef");
	lrg_level_add_probe_ref (level, "lightmaps/hall.rprobes");   /* once */
	lrg_level_set_bake_setting (level, "texels_per_unit", 8.0);
	lrg_level_set_bake_setting (level, "denoise", 1.0);
	lrg_level_set_bake_setting (level, "probe_spacing", 2.5);
	g_assert_true (lrg_level_has_lighting (level));
	g_assert_cmpuint (lrg_level_get_probe_refs (level)->len, ==, 2);

	data = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_nonnull (data);
	g_assert_nonnull (strstr (data, "lighting:"));
	back = lrg_level_serializer_load_from_data (ser, data, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_level_get_lightmap_ref (back), ==, "lightmaps/lit.rlightmap");
	probes = lrg_level_get_probe_refs (back);
	g_assert_cmpuint (probes->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (probes, 0), ==, "lightmaps/hall.rprobes");
	g_assert_cmpstr (g_ptr_array_index (probes, 1), ==, "guid:0123456789abcdef");
	keys = lrg_level_get_bake_setting_keys (back);
	g_assert_cmpuint (keys->len, ==, 3);
	g_assert_true (lrg_level_get_bake_setting (back, "probe_spacing", &v));
	g_assert_cmpfloat (v, ==, 2.5);

	/* levels without the block keep their shape */
	plain_data = lrg_level_serializer_save_to_data (ser, plain, NULL);
	g_assert_null (strstr (plain_data, "lighting:"));

	/* the API: removal and clearing */
	g_assert_true (lrg_level_remove_probe_ref (back, "guid:0123456789abcdef"));
	g_assert_false (lrg_level_remove_probe_ref (back, "guid:0123456789abcdef"));
	lrg_level_set_lightmap_ref (back, "");
	g_assert_null (lrg_level_get_lightmap_ref (back));
	lrg_level_clear_lighting (back);
	g_assert_false (lrg_level_has_lighting (back));

	/* booleans in settings; malformed blocks fail the load */
	g_clear_object (&back);
	back = lrg_level_serializer_load_from_data (ser,
		"type: level\nname: b\nnodes: []\nlighting:\n  settings: { denoise: true, bounces: 3 }\n", -1, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_level_get_bake_setting (back, "denoise", &v));
	g_assert_cmpfloat (v, ==, 1.0);
	g_clear_object (&back);
	back = lrg_level_serializer_load_from_data (ser,
		"type: level\nname: b\nnodes: []\nlighting:\n  probes: hall.rprobes\n", -1, &error);
	g_assert_null (back);
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE);
	g_clear_error (&error);
	back = lrg_level_serializer_load_from_data (ser,
		"type: level\nname: b\nnodes: []\nlighting:\n  settings: { samples: lots }\n", -1, &error);
	g_assert_null (back);
	g_assert_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE);
}

/* A level file with a lighting block and bake params: loading it gives
 * the world the lightmap and grid, the components their properties. */
static void
test_level_instantiate (void)
{
	static const gchar *yaml =
		"type: level\n"
		"name: baked\n"
		"default_view: 3d\n"
		"nodes:\n"
		"  - name: Floor\n"
		"    guid: aaaaaaaa-0000-0000-0000-000000000001\n"
		"    visual:\n"
		"      kind: primitive\n"
		"      primitive: plane\n"
		"      params:\n"
		"        static_lighting: true\n"
		"        lightmap_scale: 2.0\n"
		"  - name: Lamp\n"
		"    guid: aaaaaaaa-0000-0000-0000-000000000002\n"
		"    visual:\n"
		"      kind: light\n"
		"      params:\n"
		"        light_type: point\n"
		"        bake_mode: baked\n"
		"lighting:\n"
		"  lightmap: baked.rlightmap\n"
		"  probes: [baked.rprobes, missing.rprobes]\n"
		"  settings: { texels_per_unit: 4 }\n";
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgLevel)      level = NULL;
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgLightmap3D) lm = lrg_lightmap3d_new ();
	g_autoptr(LrgProbeGrid)  grid = lrg_probe_grid_new ();
	g_autoptr(LrgHdrImage)   page = NULL;
	g_autofree gchar        *dir = g_dir_make_tmp ("lrg-bakelevel-XXXXXX", NULL);
	g_autofree gchar        *path = g_build_filename (dir, "baked.rlevel", NULL);
	g_autofree gchar        *lm_path = g_build_filename (dir, "baked.rlightmap", NULL);
	g_autofree gchar        *pg_path = g_build_filename (dir, "baked.rprobes", NULL);
	const gfloat             rgb[3] = { 0.25f, 0.5f, 1.0f };
	const gfloat             so[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	LrgEntity3D             *floor_e;
	LrgEntity3D             *lamp;
	LrgComponent            *comp;

	page = flat_page (2, 2, rgb);
	lrg_lightmap3d_add_page (lm, page);
	lrg_lightmap3d_set_entry (lm, "aaaaaaaa-0000-0000-0000-000000000001", 0, so);
	g_assert_true (lrg_lightmap3d_save (lm, lm_path, NULL));
	g_assert_true (lrg_probe_grid_save (grid, pg_path, NULL));
	g_assert_true (g_file_set_contents (path, yaml, -1, NULL));

	/* the missing grid is a warning, not a failure */
	g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*probe grid 'missing.rprobes' did not load*");
	level = lrg_level_load_world3d (path, world, &error);
	g_test_assert_expected_messages ();
	g_assert_no_error (error);
	g_assert_nonnull (level);
	g_assert_cmpstr (lrg_level_get_lightmap_ref (level), ==, "baked.rlightmap");

	g_assert_nonnull (lrg_world3d_get_lightmap (world));
	g_assert_true (lrg_lightmap3d_get_entry (lrg_world3d_get_lightmap (world),
	                                         "aaaaaaaa-0000-0000-0000-000000000001", NULL, NULL));
	g_assert_cmpuint (lrg_world3d_get_probe_grids (world)->len, ==, 1);

	floor_e = lrg_world3d_find_by_guid (world, "aaaaaaaa-0000-0000-0000-000000000001");
	g_assert_nonnull (floor_e);
	comp = lrg_entity3d_get_component (floor_e, LRG_TYPE_MESH_RENDERER_COMPONENT);
	g_assert_true (lrg_mesh_renderer_component_get_static_lighting (LRG_MESH_RENDERER_COMPONENT (comp)));
	g_assert_cmpfloat (lrg_mesh_renderer_component_get_lightmap_scale (LRG_MESH_RENDERER_COMPONENT (comp)), ==, 2.0f);
	lamp = lrg_world3d_find_by_guid (world, "aaaaaaaa-0000-0000-0000-000000000002");
	comp = lrg_entity3d_get_component (lamp, LRG_TYPE_LIGHT_COMPONENT);
	g_assert_cmpint (lrg_light_component_get_bake_mode (LRG_LIGHT_COMPONENT (comp)), ==, LRG_LIGHT_MODE_BAKED);

	g_clear_object (&level);
	g_clear_object (&world);
	remove_dir (dir);
}

/* ==========================================================================
 * GPU (isolated X display only)
 * ========================================================================== */

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* Never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-probe-grid");
	return IsWindowReady ();
}

/* Renders @world through @cam into a fresh RGBA image (top row first). */
static Image
gpu_render (LrgWorld3DRenderer *r,
            LrgWorld3D         *world,
            LrgCamera3D        *cam)
{
	g_autoptr(GrlRenderTexture) target = grl_render_texture_new (GPU_W, GPU_H);
	g_autoptr(GrlTexture)       tex = NULL;
	g_autoptr(GrlImage)         grl = NULL;
	const guint8               *pixels;
	gsize                       size = 0;
	Image                       img;

	lrg_world3d_renderer_set_flags (r, LRG_WORLD3D_RENDER_FLAG_CLEAR);
	g_assert_true (lrg_world3d_renderer_render (r, world, cam, target, 0, 0));
	tex = grl_render_texture_get_texture (target);
	grl = grl_texture_to_image (tex);
	pixels = grl_image_get_pixels (grl, &size);
	g_assert_cmpuint (size, ==, (gsize) GPU_W * GPU_H * 4);
	memset (&img, 0, sizeof img);
	img.data = MemAlloc ((guint) size);
	memcpy (img.data, pixels, size);
	img.width = GPU_W;
	img.height = GPU_H;
	img.mipmaps = 1;
	img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	ImageFlipVertical (&img);

	/* LRG_TEST_SHOT_DIR: keep every render as a numbered PNG (visual pass) */
	if (g_getenv ("LRG_TEST_SHOT_DIR") != NULL)
	{
		static guint      shot = 0;
		g_autofree gchar *name = g_strdup_printf ("m34-BAKEDATA-%02u.png", shot++);
		g_autofree gchar *path = g_build_filename (g_getenv ("LRG_TEST_SHOT_DIR"), name, NULL);

		ExportImage (img, path);
	}
	return img;
}

/* the 3x3 mean of @img around (@x, @y) */
static void
pixel_at (Image  img,
          gint   x,
          gint   y,
          gfloat *out)
{
	const guint8 *p = img.data;
	gint          i, j, n = 0;

	out[0] = out[1] = out[2] = 0.0f;
	for (j = y - 1; j <= y + 1; j++)
		for (i = x - 1; i <= x + 1; i++)
		{
			const guint8 *px = p + ((gsize) j * (gsize) img.width + (gsize) i) * 4;

			out[0] += px[0];
			out[1] += px[1];
			out[2] += px[2];
			n++;
		}
	out[0] /= (gfloat) n;
	out[1] /= (gfloat) n;
	out[2] /= (gfloat) n;
}

/* display byte of a linear value */
static gfloat
display_byte (gfloat linear)
{
	return powf (CLAMP (linear, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f;
}

static void
assert_colour (const gfloat *got,
               const gfloat *linear_expected,
               gfloat        tolerance,
               const gchar  *what)
{
	guint c;

	for (c = 0; c < 3; c++)
	{
		gfloat want = display_byte (linear_expected[c]);

		g_test_message ("%s channel %u: got %.1f, expected %.1f", what, c, got[c], want);
		g_assert_cmpfloat (fabsf (got[c] - want), <=, tolerance);
	}
}

/*
 * A 2 x 2 quad in the XZ plane with uv2 = ((x + 1) / 2, (z + 1) / 2)
 * (raylib's TEXCOORD2 attribute, added to the uploaded mesh's vertex
 * array the way a glTF TEXCOORD_1 arrives).
 */
static GrlModel *
uv2_quad (void)
{
	const gfloat              verts[12] = { -1, 0, -1, -1, 0, 1, 1, 0, 1, 1, 0, -1 };
	const gfloat              normals[12] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
	const guint16             idx[6] = { 0, 1, 2, 0, 2, 3 };
	g_autoptr(GrlMesh)        mesh = grl_mesh_new_custom (verts, 4, normals, idx, 6);
	Mesh                     *m;
	guint                     i;

	g_assert_nonnull (mesh);
	m = (Mesh *) grl_mesh_get_handle (mesh);
	m->texcoords2 = MemAlloc (4 * 2 * sizeof (gfloat));
	for (i = 0; i < 4; i++)
	{
		m->texcoords2[i * 2 + 0] = (verts[i * 3 + 0] + 1.0f) * 0.5f;
		m->texcoords2[i * 2 + 1] = (verts[i * 3 + 2] + 1.0f) * 0.5f;
	}
	rlEnableVertexArray (m->vaoId);
	m->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2] =
		rlLoadVertexBuffer (m->texcoords2, 4 * 2 * (gint) sizeof (gfloat), false);
	rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2, 2, RL_FLOAT, 0, 0, 0);
	rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2);
	rlDisableVertexArray ();
	return grl_model_new_from_mesh (mesh);
}

/* an entity with a mesh renderer drawing @model (or a @prim primitive) */
static LrgEntity3D *
add_entity (LrgWorld3D       *world,
            const gchar      *guid,
            GrlModel         *model,
            LrgPrimitiveType  prim,
            gfloat            x,
            gfloat            z)
{
	LrgEntity3D                         *e = lrg_entity3d_new_with_name (guid);
	g_autoptr(LrgMeshRendererComponent)  mr = model != NULL ? lrg_mesh_renderer_component_new ()
	                                                        : lrg_mesh_renderer_component_new_primitive (prim);

	if (model != NULL)
		lrg_mesh_renderer_component_set_model (mr, model);
	lrg_mesh_renderer_component_set_static_lighting (mr, TRUE);
	lrg_mesh_renderer_component_set_cast_shadows (mr, FALSE);
	lrg_entity3d_set_guid (e, guid);
	lrg_entity3d_set_position_xyz (e, x, 0.0f, z);
	lrg_entity3d_add_component (e, LRG_COMPONENT (mr));
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	return e;
}

static LrgCamera3D *
top_camera (gfloat x,
            gfloat height)
{
	LrgCamera3D *cam = lrg_camera3d_new ();

	/* straight down, +Z toward the bottom of the image */
	lrg_camera3d_set_position_xyz (cam, x, height, 0.001f);
	lrg_camera3d_set_target_xyz (cam, x, 0.0f, 0.0f);
	lrg_camera3d_set_up_xyz (cam, 0.0f, 0.0f, -1.0f);
	lrg_camera3d_set_fovy (cam, 60.0f);
	return cam;
}

/*
 * Lightmapped quad: its two halves show the two baked texels (uv2 ->
 * scale / offset -> page); a baked point light above it is skipped on
 * it and still lights the plain quad beside it; without the lightmap the
 * quad falls back to the ambient (inactive hook state).
 */
static void
test_gpu_lightmap (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgCamera3D)        cam = NULL;
	g_autoptr(GrlModel)           quad = NULL;
	g_autoptr(GrlModel)           plain = NULL;
	g_autoptr(LrgLightmap3D)      lm = lrg_lightmap3d_new ();
	g_autoptr(LrgHdrImage)        page = lrg_hdr_image_new (4, 2);
	g_autoptr(GObject)            lamp_src = NULL;
	const gfloat                  left[3] = { 0.25f, 0.5f, 0.1f };
	const gfloat                  right[3] = { 0.6f, 0.15f, 0.35f };
	const gfloat                  junk[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat                  so[4] = { 0.5f, 1.0f, 0.0f, 0.0f };   /* left half of the page */
	GrlColor                      ambient = { 60, 60, 60, 255 };
	gfloat                        amb_lin[3];
	LrgLight3DDesc                lamp;
	Image                         img;
	gfloat                        px[3], plain_px[3], plain_lit[3];
	guint                         x, y;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	/* page: columns 0 (left), 1 (right) hold the quad's colours; 2, 3
	 * junk the scale / offset must keep the quad away from */
	for (y = 0; y < 2; y++)
		for (x = 0; x < 4; x++)
			lrg_hdr_image_set_pixel (page, x, y, x == 0 ? left : x == 1 ? right : junk);
	lrg_lightmap3d_add_page (lm, page);
	lrg_lightmap3d_set_entry (lm, "lit-quad", 0, so);

	r = lrg_world3d_renderer_new ();
	g_object_set (r, "fallback-sun", FALSE, NULL);   /* only the baked light and the ambient */
	lrg_world3d_renderer_set_ambient (r, &ambient);
	world = lrg_world3d_new ();
	quad = uv2_quad ();
	plain = uv2_quad ();
	add_entity (world, "lit-quad", quad, LRG_PRIMITIVE_PLANE, -1.1f, 0.0f);
	add_entity (world, "plain-quad", plain, LRG_PRIMITIVE_PLANE, 1.1f, 0.0f);
	lrg_world3d_set_lightmap (world, lm);
	cam = top_camera (0.0f, 4.0f);

	img = gpu_render (r, world, cam);
	/* lit quad: x -1.6 (uv2.u 0.25: column 0) and -0.6 (0.75: column 1);
	 * the image spans about +-2.3 units at height 4 (fov 60) */
	pixel_at (img, (gint) (GPU_W * (0.5f - 1.6f / 4.62f)), GPU_H / 2, px);
	assert_colour (px, left, 6.0f, "lightmap left");
	pixel_at (img, (gint) (GPU_W * (0.5f - 0.6f / 4.62f)), GPU_H / 2, px);
	assert_colour (px, right, 8.0f, "lightmap right");
	/* plain quad: the flat ambient (hook inactive after the lit draw) */
	amb_lin[0] = amb_lin[1] = amb_lin[2] = powf (60.0f / 255.0f, 2.2f);
	pixel_at (img, (gint) (GPU_W * (0.5f + 1.1f / 4.62f)), GPU_H / 2, plain_px);
	assert_colour (plain_px, amb_lin, 4.0f, "plain quad ambient");
	UnloadImage (img);

	/* a baked point light over both: skipped on the lightmapped quad */
	lamp_src = g_object_new (G_TYPE_OBJECT, NULL);
	lrg_light3d_desc_init (&lamp);
	lamp.kind = LRG_LIGHT3D_POINT;
	lamp.position[1] = 1.5f;
	lamp.range = 6.0f;
	lamp.intensity = 1.0f;
	lamp.bake_mode = LRG_LIGHT_MODE_BAKED;
	lrg_world3d_register_light (world, lamp_src, &lamp);
	img = gpu_render (r, world, cam);
	pixel_at (img, (gint) (GPU_W * (0.5f - 0.6f / 4.62f)), GPU_H / 2, px);
	assert_colour (px, right, 8.0f, "lightmap right under a baked light");
	pixel_at (img, (gint) (GPU_W * (0.5f + 0.6f / 4.62f)), GPU_H / 2, plain_lit);
	g_assert_cmpfloat (plain_lit[0], >, plain_px[0] + 40.0f);
	UnloadImage (img);

	/* the same light in realtime mode lights the lightmapped quad too */
	lamp.bake_mode = LRG_LIGHT_MODE_REALTIME;
	lrg_world3d_register_light (world, lamp_src, &lamp);
	img = gpu_render (r, world, cam);
	pixel_at (img, (gint) (GPU_W * (0.5f - 0.6f / 4.62f)), GPU_H / 2, px);
	g_assert_cmpfloat (px[1], >, display_byte (right[1]) + 30.0f);
	UnloadImage (img);
	lrg_world3d_unregister_light (world, lamp_src);

	/* no lightmap: both quads take the ambient (inactive state) */
	lrg_world3d_set_lightmap (world, NULL);
	img = gpu_render (r, world, cam);
	pixel_at (img, (gint) (GPU_W * (0.5f - 1.6f / 4.62f)), GPU_H / 2, px);
	assert_colour (px, amb_lin, 4.0f, "no lightmap: ambient");
	UnloadImage (img);

	lrg_lightmap3d_unload_gpu (lm);
	lrg_world3d_renderer_release (r);
}

/*
 * Probe-lit sphere: inside a grid of constant SH it shows the grid's
 * colour (white albedo, no lights: colour = display (irradiance / pi));
 * a sphere outside every grid keeps the ambient.
 */
static void
test_gpu_probes (void)
{
	g_autoptr(LrgWorld3DRenderer) r = NULL;
	g_autoptr(LrgWorld3D)         world = NULL;
	g_autoptr(LrgCamera3D)        cam = NULL;
	const gfloat                  mn[3] = { -2.0f, -1.0f, -1.5f };
	const gfloat                  mx[3] = { 0.0f, 1.0f, 1.5f };
	g_autoptr(LrgProbeGrid)       grid = lrg_probe_grid_new_with_bounds (mn, mx, 2, 3, 2);
	const gfloat                  teal[3] = { 0.1f, 0.45f, 0.4f };
	GrlColor                      ambient = { 90, 40, 40, 255 };
	gfloat                        amb_lin[3];
	gfloat                        sh[LRG_PROBE_GRID_SH_FLOATS];
	gfloat                        px[3];
	Image                         img;
	guint                         i;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	sh_constant (sh, teal);
	for (i = 0; i < lrg_probe_grid_get_n_probes (grid); i++)
		lrg_probe_grid_set_probe (grid, i, sh, i != 3);   /* one invalid: filled */

	r = lrg_world3d_renderer_new ();
	g_object_set (r, "fallback-sun", FALSE, NULL);   /* only the baked light and the ambient */
	lrg_world3d_renderer_set_ambient (r, &ambient);
	world = lrg_world3d_new ();
	add_entity (world, "ball-in", NULL, LRG_PRIMITIVE_UV_SPHERE, -1.0f, 0.0f);
	add_entity (world, "ball-out", NULL, LRG_PRIMITIVE_UV_SPHERE, 1.0f, 0.0f);
	lrg_world3d_add_probe_grid (world, grid);
	cam = top_camera (0.0f, 4.0f);

	img = gpu_render (r, world, cam);
	pixel_at (img, (gint) (GPU_W * (0.5f - 1.0f / 4.62f)), GPU_H / 2, px);
	assert_colour (px, teal, 6.0f, "probe-lit sphere");
	amb_lin[0] = powf (90.0f / 255.0f, 2.2f);
	amb_lin[1] = amb_lin[2] = powf (40.0f / 255.0f, 2.2f);
	pixel_at (img, (gint) (GPU_W * (0.5f + 1.0f / 4.62f)), GPU_H / 2, px);
	assert_colour (px, amb_lin, 5.0f, "sphere outside the grid");
	UnloadImage (img);

	g_assert_cmpuint (lrg_probe_grid_get_texture (grid), !=, 0);
	lrg_probe_grid_unload_gpu (grid);
	g_assert_cmpuint (lrg_probe_grid_get_texture (grid), ==, 0);
	lrg_world3d_renderer_release (r);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();

	g_test_add_func ("/probe-grid/lightmap/entries", test_lightmap_entries);
	g_test_add_func ("/probe-grid/lightmap/sample", test_lightmap_sample);
	g_test_add_func ("/probe-grid/lightmap/round-trip", test_lightmap_round_trip);
	g_test_add_func ("/probe-grid/probes/layout", test_probe_layout);
	g_test_add_func ("/probe-grid/probes/sample", test_probe_sample);
	g_test_add_func ("/probe-grid/probes/invalid", test_probe_invalid);
	g_test_add_func ("/probe-grid/probes/round-trip", test_probe_round_trip);
	g_test_add_func ("/probe-grid/world", test_world_accessors);
	g_test_add_func ("/probe-grid/modes", test_modes_and_properties);
	g_test_add_func ("/probe-grid/level/round-trip", test_level_round_trip);
	g_test_add_func ("/probe-grid/level/instantiate", test_level_instantiate);
	g_test_add_func ("/probe-grid/gpu/lightmap", test_gpu_lightmap);
	g_test_add_func ("/probe-grid/gpu/probes", test_gpu_probes);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
