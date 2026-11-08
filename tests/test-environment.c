/* test-environment.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrImage (RGBE read / write), LrgEnvironment (sky evaluation, L2
 * irradiance, BRDF table, YAML block, level `environment:` round trip,
 * GPU bake and the lit-shader hook) and LrgVolumetricFog (CPU mirror,
 * GPU composite).  GPU cases skip without a display.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-environment-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gpu_ready (void)
{
	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return FALSE;
	}
	return TRUE;
}

static gfloat
lin (gfloat c)
{
	return powf (c, 2.2f);
}

/* ==========================================================================
 * HDR images
 * ========================================================================== */

static void
assert_rgbe_close (const gfloat *a,
                   const gfloat *b)
{
	gfloat m = MAX (a[0], MAX (a[1], a[2]));
	gint   c;

	/* 8-bit mantissas against the brightest channel */
	for (c = 0; c < 3; c++)
		g_assert_cmpfloat (fabsf (a[c] - b[c]), <=, m / 128.0f + 1e-6f);
}

static void
test_hdr_rgbe_pixel (void)
{
	const gfloat values[][3] = { { 1.0f, 0.5f, 0.25f }, { 1000.0f, 3.0f, 0.0f },
	                             { 0.001f, 0.002f, 0.003f }, { 0.0f, 0.0f, 0.0f } };
	guint8 rgbe[4];
	gfloat back[3];
	guint  i;

	for (i = 0; i < G_N_ELEMENTS (values); i++)
	{
		lrg_hdr_rgbe_encode (values[i], rgbe);
		lrg_hdr_rgbe_decode (rgbe, back);
		assert_rgbe_close (values[i], back);
	}

	/* negative and NaN become 0 */
	{
		gfloat bad[3] = { -1.0f, NAN, 2.0f };

		lrg_hdr_rgbe_encode (bad, rgbe);
		lrg_hdr_rgbe_decode (rgbe, back);
		g_assert_cmpfloat (back[0], ==, 0.0f);
		g_assert_cmpfloat (back[1], ==, 0.0f);
		g_assert_cmpfloat_with_epsilon (back[2], 2.0f, 2.0f / 128.0f);
	}
}

/* a smooth image with flat runs, so both RLE record kinds appear */
static LrgHdrImage *
make_image (guint w,
            guint h)
{
	LrgHdrImage *img = lrg_hdr_image_new (w, h);
	guint        x;
	guint        y;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			gfloat rgb[3];

			if (x < w / 3)
			{
				rgb[0] = 2.0f;
				rgb[1] = 1.0f;
				rgb[2] = 0.5f;
			}
			else
			{
				rgb[0] = (gfloat)x / (gfloat)w * 10.0f;
				rgb[1] = (gfloat)y / (gfloat)h;
				rgb[2] = 0.01f * (gfloat)(x + y);
			}
			lrg_hdr_image_set_pixel (img, x, y, rgb);
		}
	}
	return img;
}

static void
roundtrip_size (guint w,
                guint h)
{
	g_autoptr(LrgHdrImage) img = make_image (w, h);
	g_autoptr(LrgHdrImage) back = NULL;
	g_autoptr(GBytes)      bytes = NULL;
	g_autoptr(GError)      error = NULL;
	guint                  x;
	guint                  y;

	bytes = lrg_hdr_image_save_to_bytes (img);
	back = lrg_hdr_image_new_from_bytes (bytes, &error);
	g_assert_no_error (error);
	g_assert_nonnull (back);
	g_assert_cmpuint (lrg_hdr_image_get_width (back), ==, w);
	g_assert_cmpuint (lrg_hdr_image_get_height (back), ==, h);
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			gfloat a[3];
			gfloat b[3];

			lrg_hdr_image_get_pixel (img, x, y, a);
			lrg_hdr_image_get_pixel (back, x, y, b);
			assert_rgbe_close (a, b);
		}
	}
}

static void
test_hdr_roundtrip (void)
{
	roundtrip_size (64, 32);	/* run-length scanlines */
	roundtrip_size (5, 3);		/* flat scanlines (width < 8) */
	roundtrip_size (300, 7);	/* long runs split at 127 */
}

static void
test_hdr_file_and_header (void)
{
	g_autoptr(LrgHdrImage) img = make_image (16, 8);
	g_autoptr(LrgHdrImage) back = NULL;
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *dir = g_dir_make_tmp ("lrg-hdr-XXXXXX", NULL);
	g_autofree gchar      *path = g_build_filename (dir, "sky.hdr", NULL);
	gfloat                 px[3];

	g_assert_true (lrg_hdr_image_save_to_file (img, path, &error));
	g_assert_no_error (error);
	back = lrg_hdr_image_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_hdr_image_get_width (back), ==, 16);
	g_remove (path);
	g_rmdir (dir);
	g_clear_object (&back);

	/* hand-written flat file: EXPOSURE divided out, +Y is bottom-up */
	{
		static const guint8 data[] =
			"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=2\n\n+Y 2 +X 1\n"
			"\x80\x80\x80\x81"	/* bottom row: 0.5 * 2 = 1 ... /2 */
			"\x80\x80\x80\x82";	/* top row: 1 * 2 */
		g_autoptr(GBytes) bytes = g_bytes_new_static (data, sizeof (data) - 1);

		back = lrg_hdr_image_new_from_bytes (bytes, &error);
		g_assert_no_error (error);
		g_assert_cmpfloat (lrg_hdr_image_get_exposure (back), ==, 2.0f);
		lrg_hdr_image_get_pixel (back, 0, 0, px);	/* top = last stored row */
		g_assert_cmpfloat_with_epsilon (px[0], (128.5f / 256.0f) * 4.0f / 2.0f, 1e-4);
		lrg_hdr_image_get_pixel (back, 0, 1, px);
		g_assert_cmpfloat_with_epsilon (px[0], (128.5f / 256.0f) * 2.0f / 2.0f, 1e-4);
		g_clear_object (&back);
	}

	/* errors */
	{
		g_autoptr(GBytes) junk = g_bytes_new_static ("hello", 5);
		g_autoptr(GBytes) cut = g_bytes_new_static ("#?RADIANCE\n\n-Y 4 +X 4\n\x01\x02", 25);

		back = lrg_hdr_image_new_from_bytes (junk, &error);
		g_assert_null (back);
		g_assert_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_FORMAT);
		g_clear_error (&error);
		back = lrg_hdr_image_new_from_bytes (cut, &error);
		g_assert_null (back);
		g_assert_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_TRUNCATED);
	}
}

static void
test_hdr_mapping (void)
{
	g_autoptr(LrgHdrImage) img = lrg_hdr_image_new (8, 4);
	g_autoptr(LrgHdrImage) small = NULL;
	const gfloat fwd[3] = { 0.0f, 0.0f, -1.0f };
	const gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat right[3] = { 1.0f, 0.0f, 0.0f };
	gfloat u;
	gfloat v;
	gfloat avg[3];
	gfloat one[3] = { 1.0f, 2.0f, 3.0f };
	guint  x;
	guint  y;

	lrg_hdr_image_direction_to_uv (fwd, &u, &v);
	g_assert_cmpfloat_with_epsilon (u, 0.5f, 1e-5);
	g_assert_cmpfloat_with_epsilon (v, 0.5f, 1e-5);
	lrg_hdr_image_direction_to_uv (up, &u, &v);
	g_assert_cmpfloat_with_epsilon (v, 0.0f, 1e-5);
	lrg_hdr_image_direction_to_uv (right, &u, &v);
	g_assert_cmpfloat_with_epsilon (u, 0.75f, 1e-5);

	for (y = 0; y < 4; y++)
		for (x = 0; x < 8; x++)
			lrg_hdr_image_set_pixel (img, x, y, one);
	lrg_hdr_image_get_average (img, avg);
	g_assert_cmpfloat_with_epsilon (avg[2], 3.0f, 1e-5);
	small = lrg_hdr_image_resize (img, 4, 2);
	lrg_hdr_image_get_average (small, avg);
	g_assert_cmpfloat_with_epsilon (avg[1], 2.0f, 1e-5);
	lrg_hdr_image_scale (small, 0.5f);
	lrg_hdr_image_sample (small, 0.99f, 0.5f, avg);
	g_assert_cmpfloat_with_epsilon (avg[0], 0.5f, 1e-5);
}

/* ==========================================================================
 * Environment: CPU
 * ========================================================================== */

static void
test_env_face_mapping (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	gfloat d[3];
	gfloat rgb[3];
	guint  f;

	/* face centres are the axes */
	lrg_environment_cube_face_direction (0, 0.5f, 0.5f, d);
	g_assert_cmpfloat_with_epsilon (d[0], 1.0f, 1e-5);
	lrg_environment_cube_face_direction (5, 0.5f, 0.5f, d);
	g_assert_cmpfloat_with_epsilon (d[2], -1.0f, 1e-5);
	/* +X face: s grows toward -Z, t toward -Y */
	lrg_environment_cube_face_direction (0, 1.0f, 1.0f, d);
	g_assert_cmpfloat (d[2], <, 0.0f);
	g_assert_cmpfloat (d[1], <, 0.0f);

	/* one colour per face: each axis looks up its own face */
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_CUBEMAP);
	for (f = 0; f < 6; f++)
	{
		g_autoptr(LrgHdrImage) face = lrg_hdr_image_new (4, 4);
		gfloat c[3] = { (gfloat)f, 0.0f, 0.0f };
		guint  x;
		guint  y;

		for (y = 0; y < 4; y++)
			for (x = 0; x < 4; x++)
				lrg_hdr_image_set_pixel (face, x, y, c);
		lrg_environment_set_cubemap_face (env, f, face);
	}
	for (f = 0; f < 6; f++)
	{
		lrg_environment_cube_face_direction (f, 0.3f, 0.7f, d);
		lrg_environment_sample_sky (env, d, rgb);
		g_assert_cmpfloat_with_epsilon (rgb[0], (gfloat)f, 1e-4);
	}
}

static void
test_env_cross (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgHdrImage)    cross = lrg_hdr_image_new (8, 6);
	g_autoptr(LrgHdrImage)    bad = lrg_hdr_image_new (5, 5);
	g_autoptr(GError)         error = NULL;
	/* cell -> face of the horizontal layout */
	const gint cell_face[3][4] = { { -1, 2, -1, -1 }, { 1, 4, 0, 5 }, { -1, 3, -1, -1 } };
	gfloat px[3];
	guint  x;
	guint  y;
	guint  f;

	for (y = 0; y < 6; y++)
	{
		for (x = 0; x < 8; x++)
		{
			gfloat c[3] = { (gfloat)cell_face[y / 2][x / 2], 0.0f, 0.0f };

			lrg_hdr_image_set_pixel (cross, x, y, c);
		}
	}
	g_assert_true (lrg_environment_set_cubemap_cross (env, cross, &error));
	g_assert_no_error (error);
	for (f = 0; f < 6; f++)
	{
		lrg_hdr_image_get_pixel (lrg_environment_get_cubemap_face (env, f), 1, 1, px);
		g_assert_cmpfloat (px[0], ==, (gfloat)f);
	}
	g_assert_false (lrg_environment_set_cubemap_cross (env, bad, &error));
	g_assert_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE);
}

static void
test_env_sh_constant (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	const gfloat grey[3] = { 0.5f, 0.25f, 1.0f };
	const gfloat normals[][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 1, 0, 0 },
	                              { 0.577f, 0.577f, -0.577f }, { 0, 0, 1 } };
	gfloat sh[LRG_ENVIRONMENT_SH_FLOATS];
	gfloat rgb[3];
	guint  i;
	guint  c;

	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_COLOR);
	lrg_environment_set_sky_color (env, grey);
	lrg_environment_compute_sh (env, 16, sh);

	/* a constant sky has only the DC term, and E / pi equals the sky */
	for (i = 3; i < LRG_ENVIRONMENT_SH_FLOATS; i++)
		g_assert_cmpfloat (fabsf (sh[i]), <, 1e-4f);
	for (i = 0; i < G_N_ELEMENTS (normals); i++)
	{
		lrg_environment_sample_diffuse (env, normals[i], rgb);
		for (c = 0; c < 3; c++)
			g_assert_cmpfloat_with_epsilon (rgb[c], lin (grey[c]), 1e-3);
	}

	/* exposure +1 doubles it, intensity scales it, rotation leaves it */
	lrg_environment_set_exposure (env, 1.0f);
	lrg_environment_set_diffuse_intensity (env, 0.5f);
	lrg_environment_set_rotation (env, 77.0f);
	lrg_environment_sample_diffuse (env, normals[0], rgb);
	g_assert_cmpfloat_with_epsilon (rgb[0], lin (grey[0]), 1e-3);
}

static void
test_env_sh_gradient (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	const gfloat zenith[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat horizon[3] = { 0.5f, 0.5f, 0.5f };
	const gfloat ground[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat up[3] = { 0, 1, 0 };
	const gfloat down[3] = { 0, -1, 0 };
	const gfloat side[3] = { 1, 0, 0 };
	gfloat a[3];
	gfloat b[3];
	gfloat c[3];
	gfloat sky[3];

	lrg_environment_set_gradient (env, zenith, horizon, ground);
	lrg_environment_sample_sky (env, up, sky);
	g_assert_cmpfloat_with_epsilon (sky[0], 1.0f, 1e-5);
	lrg_environment_sample_sky (env, side, sky);
	g_assert_cmpfloat_with_epsilon (sky[0], lin (0.5f), 1e-5);
	lrg_environment_compute_sh (env, 16, NULL);
	lrg_environment_sample_diffuse (env, up, a);
	lrg_environment_sample_diffuse (env, side, b);
	lrg_environment_sample_diffuse (env, down, c);
	g_assert_cmpfloat (a[0], >, b[0]);
	g_assert_cmpfloat (b[0], >, c[0]);
	g_assert_cmpfloat (a[0], <, 1.0f);
}

static void
test_env_brdf (void)
{
	gfloat a;
	gfloat b;
	gfloat a2;
	gfloat b2;
	gfloat nv;
	gsize  n = 0;
	g_autofree gfloat *lut = NULL;

	/* roughness 0 is a mirror: A + B = 1 and B is Schlick's (1 - nv)^5 */
	for (nv = 0.1f; nv <= 1.0f; nv += 0.3f)
	{
		lrg_environment_integrate_brdf (nv, 0.0f, 64, &a, &b);
		g_assert_cmpfloat_with_epsilon (a + b, 1.0f, 1e-3);
		g_assert_cmpfloat_with_epsilon (b, powf (1.0f - nv, 5.0f), 1e-3);
	}
	/* rough surfaces lose energy (the split-sum table is below 1) */
	lrg_environment_integrate_brdf (0.5f, 1.0f, 256, &a2, &b2);
	g_assert_cmpfloat (a2 + b2, <, 0.9f);
	g_assert_cmpfloat (a2, >, 0.3f);
	/* deterministic */
	lrg_environment_integrate_brdf (0.5f, 1.0f, 256, &a, &b);
	g_assert_cmpfloat (a, ==, a2);

	lut = lrg_environment_compute_brdf_lut (8, 32, &n);
	g_assert_cmpuint (n, ==, 8 * 8 * 2);
	/* the first row (rough 1/16) at the largest nv is nearly (1, 0) */
	g_assert_cmpfloat (lut[7 * 2], >, 0.9f);
}

static void
test_env_settings (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	gfloat rgba[4] = { 0.1f, 0.2f, 0.3f, 0.8f };
	gfloat out[4];
	gfloat d;
	gfloat s;
	gfloat e;
	guint  steps;
	LrgFogType type;
	g_autoptr(LrgLightSet) lights = lrg_light_set_new ();

	g_assert_true (lrg_environment_needs_bake (env));
	g_assert_false (lrg_environment_is_baked (env));
	lrg_environment_set_resolution (env, 200);
	g_assert_cmpuint (lrg_environment_get_resolution (env), ==, 128);
	lrg_environment_set_resolution (env, 1);
	g_assert_cmpuint (lrg_environment_get_resolution (env), ==, 16);

	lrg_environment_set_fog (env, TRUE, LRG_FOG_TYPE_LINEAR, rgba, 0.1f, 5.0f, 50.0f);
	g_assert_true (lrg_environment_get_fog (env, &type, out, &d, &s, &e));
	g_assert_cmpint (type, ==, LRG_FOG_TYPE_LINEAR);
	g_assert_cmpfloat (out[3], ==, 0.8f);
	g_assert_cmpfloat (e, ==, 50.0f);
	lrg_environment_set_volumetric (env, TRUE, 0.05f, 2.0f, 500, 80.0f, 2.0f);
	g_assert_true (lrg_environment_get_volumetric (env, &d, &s, &steps, &e, NULL));
	g_assert_cmpfloat (s, ==, 0.95f);
	g_assert_cmpuint (steps, ==, 128);

	/* apply: fog on in the light set */
	lrg_environment_apply_to_light_set (env, lights);
	g_assert_true (lrg_light_set_get_fog (lights, &type, NULL, NULL, NULL, NULL));
	g_assert_cmpint (type, ==, LRG_FOG_TYPE_LINEAR);
	lrg_environment_set_fog (env, FALSE, LRG_FOG_TYPE_LINEAR, rgba, 0.1f, 5.0f, 50.0f);
	lrg_environment_apply_to_light_set (env, lights);
	g_assert_false (lrg_light_set_get_fog (lights, NULL, NULL, NULL, NULL, NULL));
}

/* ==========================================================================
 * Environment: YAML and the level block
 * ========================================================================== */

static const gchar *env_yaml =
	"sky:\n"
	"  source: equirect\n"
	"  path: guid:0123-abcd\n"
	"  exposure: 1.5\n"
	"  rotation: 90\n"
	"  zenith: [0.1, 0.2, 0.3]\n"
	"  future_sky_key: kept\n"
	"ibl:\n"
	"  enabled: false\n"
	"  specular_intensity: 0.5\n"
	"  resolution: 64\n"
	"ambient:\n"
	"  color: [0.3, 0.3, 0.4]\n"
	"  intensity: 2\n"
	"fog:\n"
	"  enabled: true\n"
	"  type: height\n"
	"  color: [0.5, 0.6, 0.7, 0.9]\n"
	"  density: 0.03\n"
	"  height_base: 2\n"
	"  height_falloff: 0.5\n"
	"volumetric:\n"
	"  enabled: true\n"
	"  steps: 24\n"
	"  anisotropy: 0.3\n"
	"post_stack: post/night.rpost\n"
	"future_top_key:\n"
	"  nested: [1, 2]\n";

static void
check_env_yaml_fields (LrgEnvironment *env)
{
	gfloat     rgb[3];
	gfloat     k;
	gfloat     base;
	gfloat     falloff;
	guint      steps;
	gfloat     g;
	LrgFogType type;

	g_assert_cmpint (lrg_environment_get_sky_source (env), ==, LRG_ENVIRONMENT_SKY_EQUIRECT);
	g_assert_cmpstr (lrg_environment_get_sky_path (env), ==, "guid:0123-abcd");
	g_assert_cmpfloat (lrg_environment_get_exposure (env), ==, 1.5f);
	g_assert_cmpfloat (lrg_environment_get_rotation (env), ==, 90.0f);
	lrg_environment_get_gradient (env, rgb, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (rgb[2], 0.3f, 1e-6);
	g_assert_false (lrg_environment_get_ibl_enabled (env));
	g_assert_cmpfloat (lrg_environment_get_specular_intensity (env), ==, 0.5f);
	g_assert_cmpuint (lrg_environment_get_resolution (env), ==, 64);
	lrg_environment_get_ambient (env, rgb, &k);
	g_assert_cmpfloat_with_epsilon (rgb[2], 0.4f, 1e-6);
	g_assert_cmpfloat (k, ==, 2.0f);
	g_assert_true (lrg_environment_get_fog (env, &type, NULL, NULL, NULL, NULL));
	g_assert_cmpint (type, ==, LRG_FOG_TYPE_HEIGHT);
	lrg_environment_get_fog_height (env, &base, &falloff);
	g_assert_cmpfloat (base, ==, 2.0f);
	g_assert_cmpfloat (falloff, ==, 0.5f);
	g_assert_true (lrg_environment_get_volumetric (env, NULL, &g, &steps, NULL, NULL));
	g_assert_cmpuint (steps, ==, 24);
	g_assert_cmpfloat_with_epsilon (g, 0.3f, 1e-6);
	g_assert_cmpstr (lrg_environment_get_post_stack (env), ==, "post/night.rpost");
}

static void
test_env_yaml_roundtrip (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgEnvironment) back = lrg_environment_new ();
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *text = NULL;

	g_assert_true (lrg_environment_load_from_data (env, env_yaml, -1, &error));
	g_assert_no_error (error);
	check_env_yaml_fields (env);

	text = lrg_environment_to_data (env);
	g_assert_nonnull (strstr (text, "future_sky_key"));
	g_assert_nonnull (strstr (text, "future_top_key"));
	g_assert_true (lrg_environment_load_from_data (back, text, -1, &error));
	g_assert_no_error (error);
	check_env_yaml_fields (back);

	/* errors */
	g_assert_false (lrg_environment_load_from_data (back, "sky: {source: plaid}\n", -1, &error));
	g_assert_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_false (lrg_environment_load_from_data (back, "- a\n- b\n", -1, &error));
	g_assert_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_PARSE);
}

static void
test_env_level_block (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = lrg_level_new ("env");
	g_autoptr(LrgLevel)           back = NULL;
	g_autoptr(LrgLevel)           plain = lrg_level_new ("plain");
	g_autoptr(LrgLevel)           plain_back = NULL;
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *dir = g_dir_make_tmp ("lrg-env-level-XXXXXX", NULL);
	g_autofree gchar             *path = g_build_filename (dir, "a.rlevel", NULL);
	g_autofree gchar             *plain_path = g_build_filename (dir, "b.rlevel", NULL);
	g_autofree gchar             *contents = NULL;
	LrgEnvironment               *env;

	env = lrg_level_ensure_environment (level);
	g_assert_true (lrg_environment_load_from_data (env, env_yaml, -1, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_level_serializer_save_to_file (ser, level, path, &error));
	g_assert_no_error (error);
	back = lrg_level_serializer_load_from_file (ser, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (lrg_level_get_environment (back));
	check_env_yaml_fields (lrg_level_get_environment (back));
	g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
	g_assert_nonnull (strstr (contents, "future_top_key"));
	g_clear_pointer (&contents, g_free);

	/* a level without the block keeps the 0.8 shape */
	g_assert_true (lrg_level_serializer_save_to_file (ser, plain, plain_path, &error));
	g_assert_true (g_file_get_contents (plain_path, &contents, NULL, NULL));
	g_assert_null (strstr (contents, "environment"));
	plain_back = lrg_level_serializer_load_from_file (ser, plain_path, &error);
	g_assert_no_error (error);
	g_assert_null (lrg_level_get_environment (plain_back));

	/* the property */
	lrg_level_set_environment (back, NULL);
	g_assert_null (lrg_level_get_environment (back));

	g_remove (path);
	g_remove (plain_path);
	g_rmdir (dir);
}

static void
test_env_sources_from_files (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgHdrImage)    pano = lrg_hdr_image_new (16, 8);
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *dir = g_dir_make_tmp ("lrg-env-src-XXXXXX", NULL);
	g_autofree gchar         *path = g_build_filename (dir, "sky.hdr", NULL);
	gfloat                    c[3] = { 4.0f, 2.0f, 1.0f };
	gfloat                    d[3] = { 0.3f, 0.2f, -1.0f };
	gfloat                    out[3];
	guint                     x;
	guint                     y;

	for (y = 0; y < 8; y++)
		for (x = 0; x < 16; x++)
			lrg_hdr_image_set_pixel (pano, x, y, c);
	g_assert_true (lrg_hdr_image_save_to_file (pano, path, &error));

	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_EQUIRECT);
	lrg_environment_set_sky_path (env, "sky.hdr");
	g_assert_true (lrg_environment_load_sources (env, NULL, dir, &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_environment_get_equirect (env));
	lrg_environment_sample_sky (env, d, out);
	g_assert_cmpfloat_with_epsilon (out[0], 4.0f, 4.0f / 128.0f);

	/* a guid without a manager fails */
	lrg_environment_set_sky_path (env, "guid:nope");
	g_assert_false (lrg_environment_load_sources (env, NULL, dir, &error));
	g_assert_error (error, LRG_ENVIRONMENT_ERROR, LRG_ENVIRONMENT_ERROR_SOURCE);

	g_remove (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Volumetric fog: CPU
 * ========================================================================== */

static void
test_fog_cpu (void)
{
	g_autoptr(LrgVolumetricFog) fog = lrg_volumetric_fog_new ();
	g_autoptr(LrgEnvironment)   env = lrg_environment_new ();
	const gfloat from[3] = { 0, 0, 0 };
	const gfloat to[3] = { 0, 0, -50 };
	const gfloat sun_dir[3] = { 0, 0, 1 };	/* travelling toward the eye */
	const gfloat sun_rgb[3] = { 1, 1, 1 };
	const gfloat amb[3] = { 0.5f, 0.5f, 0.5f };
	gfloat rgb[3];
	gfloat rgb_back[3];
	gfloat t;

	/* uniform fog (no falloff): T = exp (-density x distance) */
	lrg_volumetric_fog_set_density (fog, 0.02f);
	lrg_volumetric_fog_set_height (fog, 0.0f, 0.0f);
	t = lrg_volumetric_fog_sample (fog, from, to, NULL, NULL, rgb);
	g_assert_cmpfloat_with_epsilon (t, expf (-1.0f), 1e-4);
	g_assert_cmpfloat (rgb[0], ==, 0.0f);

	/* forward scattering: looking into the sun is brighter than away */
	lrg_volumetric_fog_set_anisotropy (fog, 0.7f);
	lrg_volumetric_fog_sample (fog, from, to, sun_dir, sun_rgb, rgb);
	lrg_volumetric_fog_sample (fog, to, from, sun_dir, sun_rgb, rgb_back);
	g_assert_cmpfloat (rgb[0], >, rgb_back[0] * 5.0f);

	/* height falloff: fog high above the base is thinner */
	lrg_volumetric_fog_set_height (fog, 0.0f, 1.0f);
	{
		const gfloat hf[3] = { 0, 10, 0 };
		const gfloat ht[3] = { 0, 10, -50 };

		t = lrg_volumetric_fog_sample (fog, hf, ht, NULL, NULL, rgb);
		g_assert_cmpfloat (t, >, 0.999f);
	}

	/* the march is cut at the max distance */
	lrg_volumetric_fog_set_height (fog, 0.0f, 0.0f);
	lrg_volumetric_fog_set_max_distance (fog, 10.0f);
	t = lrg_volumetric_fog_sample (fog, from, to, NULL, NULL, rgb);
	g_assert_cmpfloat_with_epsilon (t, expf (-0.2f), 1e-4);

	/* the environment's parameters and ambient */
	lrg_environment_set_volumetric (env, TRUE, 0.1f, 0.2f, 16, 30.0f, 3.0f);
	lrg_environment_set_ambient (env, amb, 1.0f);
	g_assert_true (lrg_volumetric_fog_set_from_environment (fog, env));
	g_assert_cmpfloat (lrg_volumetric_fog_get_density (fog), ==, 0.1f);
	g_assert_cmpuint (lrg_volumetric_fog_get_steps (fog), ==, 16);
	g_assert_cmpfloat (lrg_volumetric_fog_get_intensity (fog), ==, 3.0f);
	lrg_volumetric_fog_sample (fog, from, to, NULL, NULL, rgb);
	g_assert_cmpfloat (rgb[0], >, 0.0f);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

static void
assert_face_close (gfloat       *px,
                   guint         size,
                   const gfloat *expect,
                   gfloat        tol)
{
	guint i;
	guint c;

	g_assert_nonnull (px);
	for (i = 0; i < size * size; i++)
		for (c = 0; c < 3; c++)
			g_assert_cmpfloat_with_epsilon (px[i * 3 + c], expect[c], tol);
}

static void
test_gpu_bake_constant (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(GError)         error = NULL;
	const gfloat              grey[3] = { 0.5f, 0.6f, 0.7f };
	gfloat                    expect[3];
	guint                     mips;
	guint                     m;
	guint                     f;

	if (!gpu_ready ())
		return;
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_COLOR);
	lrg_environment_set_sky_color (env, grey);
	lrg_environment_set_resolution (env, 32);
	g_assert_true (lrg_environment_bake (env, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_environment_is_baked (env));
	g_assert_false (lrg_environment_needs_bake (env));
	mips = lrg_environment_get_specular_mip_count (env);
	g_assert_cmpuint (mips, ==, 6);	/* 32 .. 1 */
	g_assert_cmpuint (lrg_environment_get_brdf_texture_id (env), !=, 0);

	expect[0] = lin (grey[0]);
	expect[1] = lin (grey[1]);
	expect[2] = lin (grey[2]);
	/* every mip of every face of a constant sky is that constant */
	for (m = 0; m < mips; m++)
	{
		for (f = 0; f < 6; f++)
		{
			guint size = 0;
			g_autofree gfloat *px = lrg_environment_read_specular_face (env, m, f, &size);

			g_assert_cmpuint (size, ==, MAX (32u >> m, 1u));
			assert_face_close (px, size, expect, 0.01f);
		}
	}
	g_assert_null (lrg_environment_read_specular_face (env, mips, 0, NULL));
	lrg_environment_release_gpu (env);
	g_assert_false (lrg_environment_is_baked (env));
}

/* A bright +Y hemisphere: the mirror mip keeps the edge sharp, rough mips
 * blur light toward the sides, and the up face stays brightest. */
static void
test_gpu_bake_prefilter_blurs (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(GError)         error = NULL;
	const gfloat              zenith[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat              black[3] = { 0.0f, 0.0f, 0.0f };
	g_autofree gfloat        *side0 = NULL;
	g_autofree gfloat        *side_rough = NULL;
	g_autofree gfloat        *up_rough = NULL;
	guint                     s0 = 0;
	guint                     sr = 0;
	guint                     mips;

	if (!gpu_ready ())
		return;
	lrg_environment_set_gradient (env, zenith, black, black);
	lrg_environment_set_resolution (env, 32);
	g_assert_true (lrg_environment_bake (env, &error));
	mips = lrg_environment_get_specular_mip_count (env);
	/* +X face, bottom row (t = 1, looking down): black at mip 0 */
	side0 = lrg_environment_read_specular_face (env, 0, 0, &s0);
	g_assert_cmpfloat (side0[((s0 - 1) * s0 + s0 / 2) * 3], <, 0.01f);
	side_rough = lrg_environment_read_specular_face (env, mips - 2, 0, &sr);
	up_rough = lrg_environment_read_specular_face (env, mips - 2, 2, NULL);
	g_assert_cmpfloat (up_rough[0], >, side_rough[((sr - 1) * sr) * 3]);
	g_assert_cmpfloat (side_rough[((sr - 1) * sr) * 3], >, 0.0f);
	lrg_environment_release_gpu (env);
}

static void
test_gpu_sky_dome_and_draw (void)
{
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(LrgSkyDome)     dome = lrg_sky_dome_new ();
	g_autoptr(GError)         error = NULL;
	GrlColor                  blue = { 0, 0, 255, 255 };
	gfloat                    sh[LRG_ENVIRONMENT_SH_FLOATS];
	gfloat                    up[3] = { 0, 1, 0 };
	gfloat                    rgb[3];
	const gfloat              red[3] = { 1.0f, 0.0f, 0.0f };
	RenderTexture2D           rt;
	Camera3D                  camera;
	Image                     shot;
	Color                     c;
	GrlVector3                eye = { 0, 0, 0 };

	if (!gpu_ready ())
		return;

	/* a dome is captured on the GPU and its irradiance read back */
	lrg_sky_dome_set_zenith_color (dome, &blue);
	lrg_sky_dome_set_horizon_color (dome, &blue);
	lrg_sky_dome_set_ground_color (dome, &blue);
	lrg_sky_dome_set_sun_size (dome, 0.0f);
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_DOME);
	lrg_environment_set_sky_dome (env, dome);
	lrg_environment_set_resolution (env, 16);
	g_assert_true (lrg_environment_bake (env, &error));
	g_assert_no_error (error);
	lrg_environment_get_sh (env, sh);
	lrg_environment_sample_diffuse (env, up, rgb);
	g_assert_cmpfloat_with_epsilon (rgb[2], 1.0f, 0.05f);
	g_assert_cmpfloat (rgb[0], <, 0.05f);

	/* a colour sky drawn behind the scene shows its colour */
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_COLOR);
	lrg_environment_set_sky_color (env, red);
	g_assert_true (lrg_environment_bake (env, &error));
	rt = LoadRenderTexture (8, 8);
	memset (&camera, 0, sizeof camera);
	camera.position = (Vector3){ 0, 0, 0 };
	camera.target = (Vector3){ 0, 0, -1 };
	camera.up = (Vector3){ 0, 1, 0 };
	camera.fovy = 60.0f;
	camera.projection = CAMERA_PERSPECTIVE;
	BeginTextureMode (rt);
	ClearBackground ((Color){ 0, 255, 0, 255 });
	BeginMode3D (camera);
	lrg_environment_draw_sky (env, &eye);
	EndMode3D ();
	EndTextureMode ();
	shot = LoadImageFromTexture (rt.texture);
	c = GetImageColor (shot, 4, 4);
	UnloadImage (shot);
	UnloadRenderTexture (rt);
	g_assert_cmpuint (c.r, >, 240);
	g_assert_cmpuint (c.g, <, 10);
	lrg_environment_release_gpu (env);
}

/* The lit shader's indirect light comes from the bound environment. */
static void
test_gpu_lit_hook (void)
{
	g_autoptr(LrgLightSet)    lights = lrg_light_set_new ();
	g_autoptr(LrgEnvironment) env = lrg_environment_new ();
	g_autoptr(GError)         error = NULL;
	const gfloat              grey[3] = { 0.5f, 0.5f, 0.5f };
	const GrlColor            black = { 0, 0, 0, 255 };
	GrlShader                *shader;
	Model                     floor_model;
	RenderTexture2D           rt;
	Camera3D                  camera;
	Image                     shot;
	Color                     on;
	Color                     off;
	guint                     pass;

	if (!gpu_ready ())
		return;
	shader = lrg_light_set_get_shader (lights);
	if (shader == NULL)
	{
		g_test_skip ("GLSL 330 unavailable");
		return;
	}
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_COLOR);
	lrg_environment_set_sky_color (env, grey);
	lrg_environment_set_resolution (env, 16);
	g_assert_true (lrg_environment_bake (env, &error));
	lrg_light_set_set_ambient (lights, &black);
	{
		gfloat n[3] = { 0, 1, 0 };
		gfloat d[3];

		lrg_environment_sample_diffuse (env, n, d);
		g_test_message ("cpu diffuse %f %f %f", d[0], d[1], d[2]);
	}

	floor_model = LoadModelFromMesh (GenMeshPlane (20, 20, 1, 1));
	floor_model.materials[0].shader = *(Shader *)grl_shader_get_handle (shader);
	memset (&camera, 0, sizeof camera);
	camera.position = (Vector3){ 0, 10, 0.01f };
	camera.target = (Vector3){ 0, 0, 0 };
	camera.up = (Vector3){ 0, 0, -1 };
	camera.fovy = 30.0f;
	camera.projection = CAMERA_PERSPECTIVE;

	for (pass = 0; pass < 2; pass++)
	{
		lrg_light_set_set_environment (lights, pass == 0 ? env : NULL);
		lrg_light_set_apply (lights, 0, 10, 0.01);
		rt = LoadRenderTexture (16, 16);
		BeginTextureMode (rt);
		ClearBackground ((Color){ 0, 0, 255, 255 });
		BeginMode3D (camera);
		rlDisableBackfaceCulling ();
		DrawModel (floor_model, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
		rlDrawRenderBatchActive ();
		rlEnableBackfaceCulling ();
		EndMode3D ();
		EndTextureMode ();
		shot = LoadImageFromTexture (rt.texture);
		if (pass == 0)
			on = GetImageColor (shot, 8, 8);
		else
			off = GetImageColor (shot, 8, 8);
		UnloadImage (shot);
		UnloadRenderTexture (rt);
	}
	g_test_message ("IBL on %u %u %u, off %u %u %u", on.r, on.g, on.b, off.r, off.g, off.b);
	/* white floor under a mid-grey sky: about the sky's own colour */
	g_assert_cmpuint (on.r, >, 90);
	g_assert_cmpuint (on.r, <, 170);
	g_assert_cmpuint (ABS ((gint)on.r - (gint)on.b), <, 6);
	g_assert_cmpuint (off.r, <, 10);

	floor_model.materials[0].shader.id = rlGetShaderIdDefault ();
	floor_model.materials[0].shader.locs = rlGetShaderLocsDefault ();
	UnloadModel (floor_model);
	lrg_light_set_set_environment (lights, NULL);
	lrg_environment_release_gpu (env);
}

/* Fog composited over a cleared target: the far plane (sky) gets the
 * fog's ambient glow and is dimmed by its transmittance. */
static void
test_gpu_volumetric_fog (void)
{
	g_autoptr(LrgVolumetricFog) fog = lrg_volumetric_fog_new ();
	g_autoptr(GError)           error = NULL;
	const gfloat                amb[3] = { 4.0f, 4.0f, 4.0f };
	RenderTexture2D             rt;
	guint                       depth;
	guint                       fbo;
	gfloat                      view[16];
	gfloat                      proj[16];
	const gfloat                eye[3] = { 0, 0, 0 };
	const gfloat                at[3] = { 0, 0, -1 };
	const gfloat                up[3] = { 0, 1, 0 };
	Image                       shot;
	Color                       c;

	if (!gpu_ready ())
		return;

	/* a depth texture cleared to the far plane */
	depth = rlLoadTextureDepth (16, 16, false);
	fbo = rlLoadFramebuffer ();
	rlFramebufferAttach (fbo, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
	rlEnableFramebuffer (fbo);
	rlViewport (0, 0, 16, 16);
	rlClearScreenBuffers ();
	rlDisableFramebuffer ();

	lrg_math3d_look_at (eye, at, up, view);
	lrg_math3d_perspective (1.0f, 1.0f, 0.1f, 100.0f, proj);
	lrg_volumetric_fog_set_density (fog, 0.05f);
	lrg_volumetric_fog_set_height (fog, 0.0f, 0.0f);
	lrg_volumetric_fog_set_max_distance (fog, 40.0f);
	lrg_volumetric_fog_set_ambient (fog, amb);

	rt = LoadRenderTexture (16, 16);
	BeginTextureMode (rt);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	g_assert_true (lrg_volumetric_fog_render (fog, depth, 16, 16, view, proj, NULL, &error));
	g_assert_no_error (error);
	EndTextureMode ();
	g_assert_cmpuint (lrg_volumetric_fog_get_scatter_texture_id (fog), !=, 0);
	shot = LoadImageFromTexture (rt.texture);
	c = GetImageColor (shot, 8, 8);
	UnloadImage (shot);
	UnloadRenderTexture (rt);
	/* scattering = amb / 4pi x (1 - exp (-2)) ~ 0.275 -> display ~ 0.56 */
	g_test_message ("fog pixel %u %u %u", c.r, c.g, c.b);
	g_assert_cmpuint (c.r, >, 110);
	g_assert_cmpuint (c.r, <, 175);

	lrg_volumetric_fog_release_gpu (fog);
	rlUnloadFramebuffer (fbo);	/* frees the attached depth texture too */
}

int
main (int   argc,
      char *argv[])
{
	int result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/hdr-image/rgbe-pixel", test_hdr_rgbe_pixel);
	g_test_add_func ("/hdr-image/roundtrip", test_hdr_roundtrip);
	g_test_add_func ("/hdr-image/file-and-header", test_hdr_file_and_header);
	g_test_add_func ("/hdr-image/mapping", test_hdr_mapping);
	g_test_add_func ("/environment/face-mapping", test_env_face_mapping);
	g_test_add_func ("/environment/cross", test_env_cross);
	g_test_add_func ("/environment/sh-constant", test_env_sh_constant);
	g_test_add_func ("/environment/sh-gradient", test_env_sh_gradient);
	g_test_add_func ("/environment/brdf", test_env_brdf);
	g_test_add_func ("/environment/settings", test_env_settings);
	g_test_add_func ("/environment/yaml-roundtrip", test_env_yaml_roundtrip);
	g_test_add_func ("/environment/level-block", test_env_level_block);
	g_test_add_func ("/environment/sources-from-files", test_env_sources_from_files);
	g_test_add_func ("/volumetric-fog/cpu", test_fog_cpu);
	g_test_add_func ("/environment/gpu/bake-constant", test_gpu_bake_constant);
	g_test_add_func ("/environment/gpu/prefilter-blurs", test_gpu_bake_prefilter_blurs);
	g_test_add_func ("/environment/gpu/sky-dome-and-draw", test_gpu_sky_dome_and_draw);
	g_test_add_func ("/environment/gpu/lit-hook", test_gpu_lit_hook);
	g_test_add_func ("/volumetric-fog/gpu/composite", test_gpu_volumetric_fog);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
