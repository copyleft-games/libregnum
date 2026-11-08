/* test-reflection-probe.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbe: box maths (influence, containment, box
 * projection) headless; capture, prefilter, sky fill and the
 * environment's per-draw selection under a display.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
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
	test_window = grl_window_new (8, 8, "lrg-reflection-probe-test");
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

static void
test_probe_defaults (void)
{
	g_autoptr(LrgReflectionProbe) probe = lrg_reflection_probe_new ();
	gfloat v[3];

	lrg_reflection_probe_get_extents (probe, v);
	g_assert_cmpfloat (v[0], ==, 5.0f);
	g_assert_cmpfloat (lrg_reflection_probe_get_blend_distance (probe), ==, 1.0f);
	g_assert_cmpfloat (lrg_reflection_probe_get_intensity (probe), ==, 1.0f);
	g_assert_true (lrg_reflection_probe_get_box_projection (probe));
	g_assert_cmpuint (lrg_reflection_probe_get_resolution (probe), ==,
	                  LRG_REFLECTION_PROBE_DEFAULT_RESOLUTION);
	lrg_reflection_probe_set_resolution (probe, 100);
	g_assert_cmpuint (lrg_reflection_probe_get_resolution (probe), ==, 64);
	g_assert_false (lrg_reflection_probe_is_ready (probe));
	g_assert_cmpuint (lrg_reflection_probe_get_texture_id (probe), ==, 0);
	g_assert_cmpuint (lrg_reflection_probe_get_mip_count (probe), ==, 0);
	g_assert_null (lrg_reflection_probe_read_face (probe, 0, 0, NULL));
}

static void
test_probe_influence (void)
{
	g_autoptr(LrgReflectionProbe) probe = lrg_reflection_probe_new ();
	const gfloat centre[3] = { 10, 0, 0 };
	const gfloat edge[3] = { 14.5f, 0, 0 };	/* 0.5 inside the +X face */
	const gfloat out[3] = { 16, 0, 0 };

	lrg_reflection_probe_set_position (probe, 10, 0, 0);
	lrg_reflection_probe_set_extents (probe, 5, 5, 5);
	lrg_reflection_probe_set_blend_distance (probe, 1.0f);
	g_assert_true (lrg_reflection_probe_contains (probe, centre));
	g_assert_false (lrg_reflection_probe_contains (probe, out));
	g_assert_cmpfloat (lrg_reflection_probe_get_influence (probe, centre), ==, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_reflection_probe_get_influence (probe, edge), 0.5f, 1e-5);
	g_assert_cmpfloat (lrg_reflection_probe_get_influence (probe, out), ==, 0.0f);
	lrg_reflection_probe_set_blend_distance (probe, 0.0f);
	g_assert_cmpfloat (lrg_reflection_probe_get_influence (probe, edge), ==, 1.0f);
}

static void
test_probe_box_project (void)
{
	g_autoptr(LrgReflectionProbe) probe = lrg_reflection_probe_new ();
	const gfloat p[3] = { 4, 0, 0 };	/* near the +X wall of a 10-wide room */
	const gfloat r[3] = { 0, 0, -1 };
	const gfloat outside[3] = { 40, 0, 0 };
	gfloat d[3];

	/* the ray hits the -Z wall at (4, 0, -5): seen from the centre that
	 * leans toward +X, unlike the raw reflection vector */
	lrg_reflection_probe_box_project (probe, p, r, d);
	g_assert_cmpfloat_with_epsilon (d[0], 4.0f / sqrtf (41.0f), 1e-4);
	g_assert_cmpfloat_with_epsilon (d[2], -5.0f / sqrtf (41.0f), 1e-4);

	/* projection off, or outside the box: the reflection vector */
	lrg_reflection_probe_box_project (probe, outside, r, d);
	g_assert_cmpfloat_with_epsilon (d[2], -1.0f, 1e-5);
	lrg_reflection_probe_set_box_projection (probe, FALSE);
	lrg_reflection_probe_box_project (probe, p, r, d);
	g_assert_cmpfloat_with_epsilon (d[0], 0.0f, 1e-5);
}

static void
test_probe_environment_list (void)
{
	g_autoptr(LrgEnvironment)     env = lrg_environment_new ();
	g_autoptr(LrgReflectionProbe) a = lrg_reflection_probe_new ();
	const gfloat                  pt[3] = { 0, 0, 0 };
	LrgReflectionProbe           *first = NULL;

	lrg_environment_add_probe (env, a);
	lrg_environment_add_probe (env, a);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 1);
	g_assert_true (lrg_environment_get_probe (env, 0) == a);
	g_assert_null (lrg_environment_get_probe (env, 1));
	/* not captured yet: never selected */
	g_assert_cmpuint (lrg_environment_select_probes (env, pt, &first, NULL), ==, 0);
	g_assert_null (first);
	g_assert_true (lrg_environment_remove_probe (env, a));
	g_assert_false (lrg_environment_remove_probe (env, a));
	lrg_environment_add_probe (env, a);
	lrg_environment_clear_probes (env);
	g_assert_cmpuint (lrg_environment_get_n_probes (env), ==, 0);
}

/* ------------------------------------------------------------------------ */

typedef struct
{
	guint faces_seen;
	gboolean draw_red_x;
} DrawCtx;

/* draws a big red quad one unit in front of the +X face */
static void
draw_cb (LrgReflectionProbe *probe,
         guint               face,
         const gfloat       *view,
         const gfloat       *projection,
         gpointer            user_data)
{
	DrawCtx *ctx = user_data;
	gfloat   pos[3];

	g_assert_nonnull (view);
	g_assert_nonnull (projection);
	ctx->faces_seen |= 1u << face;
	if (!ctx->draw_red_x)
		return;
	lrg_reflection_probe_get_position (probe, pos);
	rlDisableBackfaceCulling ();
	rlBegin (RL_QUADS);
	rlColor4ub (255, 0, 0, 255);
	rlVertex3f (pos[0] + 1, pos[1] - 5, pos[2] - 5);
	rlVertex3f (pos[0] + 1, pos[1] + 5, pos[2] - 5);
	rlVertex3f (pos[0] + 1, pos[1] + 5, pos[2] + 5);
	rlVertex3f (pos[0] + 1, pos[1] - 5, pos[2] + 5);
	rlEnd ();
	rlDrawRenderBatchActive ();
	rlEnableBackfaceCulling ();
}

static void
test_gpu_capture (void)
{
	g_autoptr(LrgReflectionProbe) probe = lrg_reflection_probe_new ();
	g_autoptr(GError)             error = NULL;
	const gfloat                  grey[3] = { 0.25f, 0.25f, 0.25f };
	DrawCtx                       ctx = { 0, FALSE };
	g_autofree gfloat            *px = NULL;
	g_autofree gfloat            *xface = NULL;
	g_autofree gfloat            *zface = NULL;
	guint                         size = 0;
	guint                         i;

	if (!gpu_ready ())
		return;

	/* clear colour only: every face of every mip is that colour */
	lrg_reflection_probe_set_resolution (probe, 16);
	lrg_reflection_probe_set_clear_color (probe, grey);
	g_assert_true (lrg_reflection_probe_capture (probe, draw_cb, &ctx, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (ctx.faces_seen, ==, 0x3f);
	g_assert_true (lrg_reflection_probe_is_ready (probe));
	g_assert_cmpuint (lrg_reflection_probe_get_mip_count (probe), ==, 5);
	g_assert_cmpuint (lrg_reflection_probe_get_capture_count (probe), ==, 1);
	px = lrg_reflection_probe_read_face (probe, 2, 3, &size);
	g_assert_cmpuint (size, ==, 4);
	for (i = 0; i < size * size * 3; i++)
		g_assert_cmpfloat_with_epsilon (px[i], 0.25f, 0.02f);

	/* a red wall in front of +X: the mirror mip shows it on +X only */
	ctx.draw_red_x = TRUE;
	lrg_reflection_probe_set_position (probe, 3, 1, -2);
	g_assert_true (lrg_reflection_probe_capture (probe, draw_cb, &ctx, &error));
	xface = lrg_reflection_probe_read_face (probe, 0, 0, &size);
	zface = lrg_reflection_probe_read_face (probe, 0, 4, NULL);
	i = (size / 2 * size + size / 2) * 3;
	g_test_message ("+X centre %f %f %f, +Z centre %f", xface[i], xface[i + 1], xface[i + 2], zface[i]);
	g_assert_cmpfloat (xface[i], >, 0.9f);
	g_assert_cmpfloat (xface[i + 1], <, 0.05f);
	g_assert_cmpfloat_with_epsilon (zface[i], 0.25f, 0.02f);

	lrg_reflection_probe_release_gpu (probe);
	g_assert_false (lrg_reflection_probe_is_ready (probe));
}

static void
test_gpu_fill_and_select (void)
{
	g_autoptr(LrgEnvironment)     env = lrg_environment_new ();
	g_autoptr(LrgReflectionProbe) a = lrg_reflection_probe_new ();
	g_autoptr(LrgReflectionProbe) b = lrg_reflection_probe_new ();
	g_autoptr(LrgReflectionProbe) c = lrg_reflection_probe_new ();
	g_autoptr(GError)             error = NULL;
	const gfloat                  sky[3] = { 0.5f, 0.5f, 0.5f };
	const gfloat                  pt[3] = { 1.0f, 0.0f, 0.0f };
	LrgReflectionProbe           *first = NULL;
	LrgReflectionProbe           *second = NULL;
	g_autofree gfloat            *px = NULL;
	guint                         size = 0;

	if (!gpu_ready ())
		return;
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_COLOR);
	lrg_environment_set_sky_color (env, sky);
	lrg_environment_set_resolution (env, 16);

	/* a probe filled from the sky holds the sky */
	lrg_reflection_probe_set_resolution (a, 16);
	g_assert_true (lrg_environment_fill_probe (env, a, &error));
	g_assert_no_error (error);
	px = lrg_reflection_probe_read_face (a, 0, 2, &size);
	g_assert_cmpfloat_with_epsilon (px[0], powf (0.5f, 2.2f), 0.02f);

	/* selection: the probe whose centre is deeper inside wins, the far
	 * one (point outside its box) is never picked */
	lrg_reflection_probe_set_resolution (b, 16);
	lrg_reflection_probe_set_resolution (c, 16);
	lrg_reflection_probe_set_position (a, 0, 0, 0);
	lrg_reflection_probe_set_position (b, 4, 0, 0);
	lrg_reflection_probe_set_blend_distance (b, 4.0f);
	lrg_reflection_probe_set_position (c, 100, 0, 0);
	g_assert_true (lrg_environment_fill_probe (env, b, &error));
	g_assert_true (lrg_environment_fill_probe (env, c, &error));
	lrg_environment_add_probe (env, b);
	lrg_environment_add_probe (env, c);
	lrg_environment_add_probe (env, a);
	g_assert_cmpuint (lrg_environment_select_probes (env, pt, &first, &second), ==, 2);
	g_assert_true (first == a);
	g_assert_true (second == b);

	lrg_environment_release_gpu (env);
	lrg_reflection_probe_release_gpu (a);
	lrg_reflection_probe_release_gpu (b);
	lrg_reflection_probe_release_gpu (c);
}

int
main (int   argc,
      char *argv[])
{
	int result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/reflection-probe/defaults", test_probe_defaults);
	g_test_add_func ("/reflection-probe/influence", test_probe_influence);
	g_test_add_func ("/reflection-probe/box-project", test_probe_box_project);
	g_test_add_func ("/reflection-probe/environment-list", test_probe_environment_list);
	g_test_add_func ("/reflection-probe/gpu/capture", test_gpu_capture);
	g_test_add_func ("/reflection-probe/gpu/fill-and-select", test_gpu_fill_and_select);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
