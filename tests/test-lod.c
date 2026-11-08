/* test-lod.c - LrgLodGroup, LrgLodState, LrgLodComponent and the .meta
 * LOD import keys
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless: screen-size maths (distance + fovy and view-projection,
 * perspective and orthographic), threshold selection, hysteresis, bias,
 * forced levels, the cross-fade state machine (complementary fades),
 * the component's per-view selection in an LrgWorld3D, and the `lod-*`
 * keys of LrgAssetMeta. GPU (/lod/gl/, isolated Xvfb): chains generated
 * from a model with decreasing triangle counts, generation from a
 * sidecar, the component submitting the right level's models with the
 * packed fade values, and lrg_lod_bind_fade() on a compiled shader.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
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

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-lod-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* A three-level group without models: thresholds 0.5, 0.2, @last. */
static LrgLodGroup *
make_group (gfloat last)
{
	LrgLodGroup *g = lrg_lod_group_new ();

	g_assert_cmpint (lrg_lod_group_add_level (g, NULL, 0.5f), ==, 0);
	g_assert_cmpint (lrg_lod_group_add_level (g, NULL, 0.2f), ==, 1);
	g_assert_cmpint (lrg_lod_group_add_level (g, NULL, last), ==, 2);
	return g;
}

/* ==========================================================================
 * Screen size
 * ========================================================================== */

static void
test_screen_size_compute (void)
{
	g_assert_cmpfloat_with_epsilon (lrg_lod_compute_screen_size (1.0f, 10.0f, 90.0f), 0.1f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_lod_compute_screen_size (1.0f, 10.0f, 60.0f),
	                                (gfloat) (1.0 / (10.0 * tan (G_PI / 6.0))), 1e-5f);
	/* Camera inside the sphere: huge, never infinite. */
	g_assert_cmpfloat (lrg_lod_compute_screen_size (1.0f, 0.0f, 45.0f), >, 100.0f);
	g_assert_true (isfinite (lrg_lod_compute_screen_size (1.0f, 0.0f, 45.0f)));
}

static void
test_screen_size_view_projection (void)
{
	gfloat eye[3] = { 3.0f, 2.0f, 10.0f };
	gfloat target[3] = { 3.0f, 2.0f, 0.0f };
	gfloat up[3] = { 0.0f, 1.0f, 0.0f };
	gfloat center[3] = { 3.0f, 2.0f, 0.0f };
	gfloat view[16];
	gfloat proj[16];
	gfloat vp[16];
	gfloat s;

	/* Perspective: the centre 10 units ahead, fovy 60. */
	lrg_math3d_look_at (eye, target, up, view);
	lrg_math3d_perspective ((gfloat) (G_PI / 3.0), 16.0f / 9.0f, 0.1f, 100.0f, proj);
	lrg_math3d_multiply (proj, view, vp);
	s = lrg_lod_screen_size_from_view_projection (vp, center, 1.0f);
	g_assert_cmpfloat_with_epsilon (s, lrg_lod_compute_screen_size (1.0f, 10.0f, 60.0f), 1e-4f);

	/* Rotated camera: same answer when the distance along the view is
	 * the same. */
	eye[0] = 13.0f;
	eye[2] = 0.0f;
	lrg_math3d_look_at (eye, target, up, view);
	lrg_math3d_multiply (proj, view, vp);
	s = lrg_lod_screen_size_from_view_projection (vp, center, 1.0f);
	g_assert_cmpfloat_with_epsilon (s, lrg_lod_compute_screen_size (1.0f, 10.0f, 60.0f), 1e-4f);

	/* Orthographic, 10 units tall: a radius-1 sphere covers 2/10. */
	lrg_math3d_ortho (-8.0f, 8.0f, -5.0f, 5.0f, 0.1f, 100.0f, proj);
	lrg_math3d_multiply (proj, view, vp);
	s = lrg_lod_screen_size_from_view_projection (vp, center, 1.0f);
	g_assert_cmpfloat_with_epsilon (s, 0.2f, 1e-4f);
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

static void
test_select_thresholds (void)
{
	g_autoptr(LrgLodGroup) g = make_group (0.0f);
	g_autoptr(LrgLodGroup) culling = make_group (0.05f);
	g_autoptr(LrgLodGroup) empty = lrg_lod_group_new ();

	g_assert_cmpuint (lrg_lod_group_get_n_levels (g), ==, 3);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.9f, LRG_LOD_LEVEL_NONE), ==, 0);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.5f, LRG_LOD_LEVEL_NONE), ==, 0);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.3f, LRG_LOD_LEVEL_NONE), ==, 1);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.01f, LRG_LOD_LEVEL_NONE), ==, 2);
	g_assert_cmpint (lrg_lod_group_select_level (culling, 0.01f, LRG_LOD_LEVEL_NONE), ==,
	                 LRG_LOD_LEVEL_CULLED);
	g_assert_cmpint (lrg_lod_group_select_level (culling, 0.06f, LRG_LOD_LEVEL_NONE), ==, 2);
	g_assert_cmpint (lrg_lod_group_select_level (empty, 1.0f, LRG_LOD_LEVEL_NONE), ==, LRG_LOD_LEVEL_CULLED);

	g_assert_null (lrg_lod_group_get_level_model (g, 0));
	g_assert_cmpuint (lrg_lod_group_get_level_models (g, 0)->len, ==, 0);
	g_assert_null (lrg_lod_group_get_level_models (g, 7));
	g_assert_cmpfloat (lrg_lod_group_get_level_screen_size (g, 1), ==, 0.2f);
	lrg_lod_group_set_level_screen_size (g, 1, 0.3f);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.25f, LRG_LOD_LEVEL_NONE), ==, 2);
	g_assert_cmpint (lrg_lod_group_get_level_triangle_count (g, 0), ==, 0);
	g_assert_cmpfloat (lrg_lod_group_get_level_error (g, 0), ==, 0.0f);

	lrg_lod_group_remove_all_levels (g);
	g_assert_cmpuint (lrg_lod_group_get_n_levels (g), ==, 0);
}

static void
test_select_hysteresis (void)
{
	g_autoptr(LrgLodGroup) g = make_group (0.05f);

	lrg_lod_group_set_hysteresis (g, 0.1f);
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_hysteresis (g), 0.1f, 1e-6f);

	/* At level 0: stays until below 0.5 * 0.9 = 0.45. */
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.48f, 0), ==, 0);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.44f, 0), ==, 1);
	/* At level 1: back to 0 only above 0.5 * 1.1 = 0.55. */
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.52f, 1), ==, 1);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.56f, 1), ==, 0);
	/* A big jump goes straight through. */
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.1f, 0), ==, 2);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.9f, 2), ==, 0);
	/* Culled: comes back only above 0.05 * 1.1. */
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.052f, LRG_LOD_LEVEL_CULLED), ==, LRG_LOD_LEVEL_CULLED);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.06f, LRG_LOD_LEVEL_CULLED), ==, 2);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.047f, 2), ==, 2);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.04f, 2), ==, LRG_LOD_LEVEL_CULLED);

	/* No hysteresis: plain thresholds. */
	lrg_lod_group_set_hysteresis (g, 0.0f);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.48f, 0), ==, 1);
}

static void
test_select_bias_force (void)
{
	g_autoptr(LrgLodGroup) g = make_group (0.0f);

	lrg_lod_group_set_bias (g, 2.0f);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.3f, LRG_LOD_LEVEL_NONE), ==, 0);
	lrg_lod_group_set_bias (g, 1.0f);

	lrg_lod_set_global_bias (0.5f);
	g_assert_cmpfloat (lrg_lod_get_global_bias (), ==, 0.5f);
	g_assert_cmpint (lrg_lod_group_select_level (g, 0.6f, LRG_LOD_LEVEL_NONE), ==, 1);
	lrg_lod_set_global_bias (1.0f);

	lrg_lod_group_set_force_level (g, 2);
	g_assert_cmpint (lrg_lod_group_get_force_level (g), ==, 2);
	g_assert_cmpint (lrg_lod_group_select_level (g, 10.0f, 0), ==, 2);
	g_object_set (g, "force-level", 7, NULL);
	g_assert_cmpint (lrg_lod_group_select_level (g, 10.0f, 0), ==, 2);    /* clamped to the chain */
	lrg_lod_group_set_force_level (g, -1);
	g_assert_cmpint (lrg_lod_group_select_level (g, 10.0f, 0), ==, 0);
}

/* ==========================================================================
 * Fade state
 * ========================================================================== */

static void
test_state_fade (void)
{
	g_autoptr(LrgLodGroup) g = make_group (0.0f);
	g_autoptr(LrgLodState) st = lrg_lod_state_new ();
	g_autoptr(LrgLodState) copy = NULL;
	gfloat                 fin = 0.0f;
	gfloat                 fout = 0.0f;

	lrg_lod_group_set_fade_duration (g, 0.5f);
	lrg_lod_group_set_hysteresis (g, 0.0f);

	g_assert_false (st->initialized);
	g_assert_false (lrg_lod_state_get_level_fade (st, 0, NULL));

	/* First update: no fade. */
	g_assert_true (lrg_lod_group_update_state (g, st, 0.8f, 0.016f));
	g_assert_cmpint (st->level, ==, 0);
	g_assert_false (lrg_lod_state_is_fading (st));
	g_assert_true (lrg_lod_state_get_level_fade (st, 0, &fin));
	g_assert_cmpfloat (fin, ==, 0.0f);
	g_assert_false (lrg_lod_state_get_level_fade (st, 1, NULL));

	/* Switch to level 1: the new level starts invisible, the old opaque. */
	g_assert_true (lrg_lod_group_update_state (g, st, 0.3f, 0.016f));
	g_assert_cmpint (st->level, ==, 1);
	g_assert_cmpint (st->previous_level, ==, 0);
	g_assert_true (lrg_lod_state_is_fading (st));
	g_assert_false (lrg_lod_state_get_level_fade (st, 1, NULL));
	g_assert_true (lrg_lod_state_get_level_fade (st, 0, &fout));
	g_assert_cmpfloat (fout, ==, 0.0f);

	/* Halfway: complementary dither thresholds. */
	g_assert_false (lrg_lod_group_update_state (g, st, 0.3f, 0.25f));
	g_assert_true (lrg_lod_state_get_level_fade (st, 1, &fin));
	g_assert_true (lrg_lod_state_get_level_fade (st, 0, &fout));
	g_assert_cmpfloat_with_epsilon (fin, 0.5f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (fout, -0.5f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (fin + fout, 0.0f, 1e-6f);

	copy = lrg_lod_state_copy (st);
	g_assert_cmpint (copy->level, ==, 1);
	g_assert_cmpfloat (copy->fade, ==, st->fade);

	/* Done: only the new level, opaque. */
	g_assert_false (lrg_lod_group_update_state (g, st, 0.3f, 0.3f));
	g_assert_false (lrg_lod_state_is_fading (st));
	g_assert_false (lrg_lod_state_get_level_fade (st, 0, NULL));
	g_assert_true (lrg_lod_state_get_level_fade (st, 1, &fin));
	g_assert_cmpfloat (fin, ==, 0.0f);

	/* Without a fade duration the switch is instant. */
	lrg_lod_group_set_fade_duration (g, 0.0f);
	g_assert_true (lrg_lod_group_update_state (g, st, 0.9f, 0.016f));
	g_assert_cmpint (st->level, ==, 0);
	g_assert_false (lrg_lod_state_is_fading (st));

	lrg_lod_state_init (st);
	g_assert_false (st->initialized);
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_LOD_STATE));
}

static void
test_glsl (void)
{
	const gchar *glsl = lrg_lod_get_glsl ();

	g_assert_nonnull (strstr (glsl, "uniform float " LRG_LOD_FADE_UNIFORM ";"));
	g_assert_nonnull (strstr (glsl, "void lrg_lod_fade_discard ()"));
	g_assert_nonnull (strstr (glsl, "discard;"));
	/* Harmless without a GL context. */
	lrg_lod_bind_fade (0, 0.5f);
}

/* ==========================================================================
 * .meta LOD keys
 * ========================================================================== */

static void
test_meta_keys (void)
{
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(GError)       error = NULL;
	g_autoptr(GArray)       list = NULL;
	g_autoptr(GVariant)     v = NULL;
	g_auto(GStrv)           known = NULL;
	g_autofree gchar       *ratios = NULL;
	g_autofree gchar       *sizes = NULL;
	g_autofree gchar       *saved = NULL;
	static const gchar     *yaml =
		"version: 1\n"
		"guid: 11111111-2222-4333-8444-555555555555\n"
		"importer: model\n"
		"settings:\n"
		"  scale: 1.0\n"
		"  lod-levels: 3\n"
		"  lod-ratios: [0.5, 0.2]\n"
		"  lod-screen-sizes: \"0.6, 0.25, 0.02\"\n"
		"  lod-max-error: 0.02\n";

	/* Defaults on a fresh model sidecar. */
	meta = lrg_asset_meta_new ();
	lrg_asset_meta_set_importer (meta, "model");
	g_assert_cmpuint (lrg_asset_meta_get_model_lod_levels (meta), ==, 1);
	ratios = lrg_asset_meta_get_model_lod_ratios (meta);
	g_assert_cmpstr (ratios, ==, "");
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_max_error (meta), 0.05, 1e-9);
	g_assert_true (lrg_asset_meta_get_model_lod_lock_border (meta));
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_hysteresis (meta), 0.1, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_fade_duration (meta), 0.25, 1e-9);
	known = lrg_asset_meta_list_known_settings ("model");
	g_assert_true (g_strv_contains ((const gchar * const *) known, "lod-levels"));
	g_assert_true (g_strv_contains ((const gchar * const *) known, "lod-screen-sizes"));
	g_clear_pointer (&ratios, g_free);
	g_clear_object (&meta);

	/* A sidecar with a flow sequence and a string list. */
	meta = lrg_asset_meta_new_from_data (yaml, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_asset_meta_get_model_lod_levels (meta), ==, 3);
	ratios = lrg_asset_meta_get_model_lod_ratios (meta);
	g_assert_cmpstr (ratios, ==, "0.5, 0.2");
	sizes = lrg_asset_meta_get_model_lod_screen_sizes (meta);
	g_assert_cmpstr (sizes, ==, "0.6, 0.25, 0.02");
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_max_error (meta), 0.02, 1e-9);
	v = lrg_asset_meta_get_setting_variant (meta, "lod-ratios");
	g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "0.5, 0.2");

	list = lrg_asset_meta_parse_float_list (sizes, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (list->len, ==, 3);
	g_assert_cmpfloat_with_epsilon (g_array_index (list, gfloat, 1), 0.25f, 1e-6f);
	g_clear_pointer (&list, g_array_unref);
	list = lrg_asset_meta_parse_float_list ("[0.5 0.25; 0.125]", &error);
	g_assert_cmpuint (list->len, ==, 3);
	g_clear_pointer (&list, g_array_unref);
	list = lrg_asset_meta_parse_float_list ("", &error);
	g_assert_cmpuint (list->len, ==, 0);
	g_clear_pointer (&list, g_array_unref);
	g_assert_null (lrg_asset_meta_parse_float_list ("0.5, half", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);

	/* Validating setter (the editor's import settings and IPC path). */
	g_assert_false (lrg_asset_meta_set_setting_variant (meta, "lod-ratios", g_variant_new_string ("x"), &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_false (lrg_asset_meta_set_setting_variant (meta, "lod-levels", g_variant_new_int64 (12), &error));
	g_clear_error (&error);
	g_assert_true (lrg_asset_meta_set_setting_variant (meta, "lod-ratios", g_variant_new_string ("0.4, 0.1"),
	                                                   &error));
	g_assert_no_error (error);

	/* Typed setters and a save round trip. */
	lrg_asset_meta_set_model_lod_levels (meta, 20);
	g_assert_cmpuint (lrg_asset_meta_get_model_lod_levels (meta), ==, 8);
	lrg_asset_meta_set_model_lod_levels (meta, 4);
	lrg_asset_meta_set_model_lod_ratios (meta, "0.5, 0.25, 0.1");
	lrg_asset_meta_set_model_lod_screen_sizes (meta, "0.5, 0.2, 0.1, 0");
	lrg_asset_meta_set_model_lod_max_error (meta, 0.03);
	lrg_asset_meta_set_model_lod_lock_border (meta, FALSE);
	lrg_asset_meta_set_model_lod_hysteresis (meta, 0.2);
	lrg_asset_meta_set_model_lod_fade_duration (meta, 0.5);
	saved = lrg_asset_meta_to_data (meta, &error);
	g_assert_no_error (error);
	g_clear_object (&meta);
	meta = lrg_asset_meta_new_from_data (saved, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_asset_meta_get_model_lod_levels (meta), ==, 4);
	g_clear_pointer (&ratios, g_free);
	ratios = lrg_asset_meta_get_model_lod_ratios (meta);
	g_assert_cmpstr (ratios, ==, "0.5, 0.25, 0.1");
	g_assert_false (lrg_asset_meta_get_model_lod_lock_border (meta));
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_hysteresis (meta), 0.2, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_fade_duration (meta), 0.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_asset_meta_get_model_lod_max_error (meta), 0.03, 1e-9);
}

/* lrg_lod_group_apply_meta() retunes thresholds without regenerating. */
static void
test_apply_meta (void)
{
	g_autoptr(LrgLodGroup)  g = make_group (0.0f);
	g_autoptr(LrgAssetMeta) meta = lrg_asset_meta_new ();

	lrg_asset_meta_set_importer (meta, "model");
	lrg_asset_meta_set_model_lod_screen_sizes (meta, "0.7, 0.3");
	lrg_asset_meta_set_model_lod_hysteresis (meta, 0.25);
	lrg_asset_meta_set_model_lod_fade_duration (meta, 1.0);
	lrg_lod_group_apply_meta (g, meta);
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_level_screen_size (g, 0), 0.7f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_level_screen_size (g, 1), 0.3f, 1e-6f);
	g_assert_cmpfloat (lrg_lod_group_get_level_screen_size (g, 2), ==, 0.0f);   /* last: default 0 */
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_hysteresis (g), 0.25f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_fade_duration (g), 1.0f, 1e-6f);
}

/* ==========================================================================
 * Component (headless: levels without models)
 * ========================================================================== */

typedef struct
{
	LrgWorld3D      *world;
	LrgEntity3D     *entity;
	LrgLodComponent *lod;
	LrgCamera3D     *camera;
	LrgRenderList   *list;
	LrgLodGroup     *group;
} Scene;

static void
scene_init (Scene       *s,
            LrgLodGroup *group)
{
	gfloat mn[3] = { -1.0f, -1.0f, -1.0f };
	gfloat mx[3] = { 1.0f, 1.0f, 1.0f };

	s->world = lrg_world3d_new ();
	s->entity = lrg_entity3d_new_with_name ("lod");
	s->group = g_object_ref (group);
	if (!lrg_lod_group_get_bounds (group, NULL, NULL))
		lrg_lod_group_set_bounds (group, mn, mx);
	s->lod = lrg_lod_component_new_with_group (group);
	g_assert_true (lrg_entity3d_add_component (s->entity, LRG_COMPONENT (s->lod)));
	lrg_world3d_add_entity (s->world, s->entity);
	s->camera = lrg_camera3d_new ();
	lrg_camera3d_set_target_xyz (s->camera, 0.0f, 0.0f, 0.0f);
	s->list = lrg_render_list_new ();
}

static void
scene_clear (Scene *s)
{
	lrg_render_list_unref (s->list);
	g_object_unref (s->camera);
	g_object_unref (s->lod);
	g_object_unref (s->entity);
	g_object_unref (s->world);
	g_object_unref (s->group);
}

/* Camera on +Z at @distance, then one collect. */
static gint
scene_level_at (Scene  *s,
                gfloat  distance)
{
	lrg_camera3d_set_position_xyz (s->camera, 0.0f, 0.0f, distance);
	lrg_world3d_collect_render (s->world, LRG_CAMERA (s->camera), s->list);
	return lrg_lod_component_get_current_level (s->lod);
}

static void
test_component_select (void)
{
	g_autoptr(LrgLodGroup) g = make_group (0.0f);
	Scene                  s;
	gfloat                 expect;
	const LrgLodState     *st;

	lrg_lod_group_set_fade_duration (g, 0.0f);
	scene_init (&s, g);
	g_assert_true (lrg_lod_component_get_group (s.lod) == g);
	g_assert_cmpint (lrg_lod_component_get_current_level (s.lod), ==, LRG_LOD_LEVEL_CULLED);

	/* Sphere radius sqrt(3), fovy 45: size = sqrt(3) / (d tan 22.5). */
	g_assert_cmpint (scene_level_at (&s, 5.0f), ==, 0);
	expect = lrg_lod_compute_screen_size (sqrtf (3.0f), 5.0f, 45.0f);
	g_assert_cmpfloat_with_epsilon (lrg_lod_component_get_screen_size (s.lod), expect, 1e-3f);
	g_assert_cmpint (scene_level_at (&s, 15.0f), ==, 1);
	g_assert_cmpint (scene_level_at (&s, 40.0f), ==, 2);
	st = lrg_lod_component_get_state_for_list (s.lod, s.list);
	g_assert_nonnull (st);
	g_assert_cmpint (st->level, ==, 2);

	/* Without frustum culling the list has no view-projection: the
	 * distance + active-camera fovy fallback gives the same size. */
	lrg_world3d_set_frustum_culling (s.world, FALSE);
	g_assert_cmpint (scene_level_at (&s, 5.0f), ==, 0);
	g_assert_cmpfloat_with_epsilon (lrg_lod_component_get_screen_size (s.lod), expect, 1e-3f);
	lrg_world3d_set_frustum_culling (s.world, TRUE);

	/* Entity scale grows the sphere. */
	lrg_entity3d_set_scale_xyz (s.entity, 4.0f, 4.0f, 4.0f);
	g_assert_cmpint (scene_level_at (&s, 15.0f), ==, 0);
	lrg_entity3d_set_scale_xyz (s.entity, 1.0f, 1.0f, 1.0f);

	/* Per-instance bias and forced level. */
	lrg_lod_component_set_bias (s.lod, 4.0f);
	g_assert_cmpint (scene_level_at (&s, 15.0f), ==, 0);
	lrg_lod_component_set_bias (s.lod, 1.0f);
	lrg_lod_component_set_force_level (s.lod, 2);
	g_assert_cmpint (scene_level_at (&s, 1.0f), ==, 2);
	lrg_lod_component_set_force_level (s.lod, -1);

	/* Levels without models submit nothing. */
	g_assert_cmpint (scene_level_at (&s, 5.0f), ==, 0);
	g_assert_cmpuint (lrg_render_list_get_total_items (s.list), ==, 0);

	lrg_lod_component_reset_states (s.lod);
	g_assert_null (lrg_lod_component_get_state_for_list (s.lod, s.list));
	scene_clear (&s);
}

/* The fade advances with world time (update) between collects. */
static void
test_component_fade_clock (void)
{
	g_autoptr(LrgLodGroup) g = make_group (0.0f);
	Scene                  s;
	const LrgLodState     *st;

	lrg_lod_group_set_fade_duration (g, 0.5f);
	lrg_lod_group_set_hysteresis (g, 0.0f);
	scene_init (&s, g);

	g_assert_cmpint (scene_level_at (&s, 5.0f), ==, 0);
	lrg_component_update (LRG_COMPONENT (s.lod), 0.1f);
	g_assert_cmpint (scene_level_at (&s, 15.0f), ==, 1);
	st = lrg_lod_component_get_state_for_list (s.lod, s.list);
	g_assert_true (lrg_lod_state_is_fading (st));
	g_assert_cmpfloat (st->fade, ==, 0.0f);

	lrg_component_update (LRG_COMPONENT (s.lod), 0.25f);
	scene_level_at (&s, 15.0f);
	g_assert_cmpfloat_with_epsilon (st->fade, 0.5f, 1e-5f);
	lrg_component_update (LRG_COMPONENT (s.lod), 0.25f);
	scene_level_at (&s, 15.0f);
	g_assert_false (lrg_lod_state_is_fading (st));
	scene_clear (&s);
}

static void
test_component_properties (void)
{
	g_autoptr(LrgLodComponent) c = lrg_lod_component_new ();
	gfloat                     rgba[4];
	gint                       layer;
	LrgRenderItem              item;

	g_assert_null (lrg_lod_component_get_group (c));
	g_assert_true (lrg_lod_component_get_cast_shadows (c));
	g_assert_true (lrg_lod_component_get_receive_shadows (c));
	g_assert_cmpint (lrg_lod_component_get_layer (c), ==, -1);
	g_assert_cmpint (lrg_lod_component_get_force_level (c), ==, -1);
	g_assert_cmpfloat (lrg_lod_component_get_bias (c), ==, 1.0f);
	g_assert_null (lrg_lod_component_get_material (c));
	g_assert_null (lrg_lod_component_get_mesh_asset (c));
	g_assert_null (lrg_lod_component_get_asset_manager (c));

	g_object_set (c, "color-r", 0.5f, "layer", 3, "cast-shadows", FALSE, "mesh-asset", "rock.glb", NULL);
	lrg_lod_component_get_color (c, rgba);
	g_assert_cmpfloat (rgba[0], ==, 0.5f);
	g_object_get (c, "layer", &layer, NULL);
	g_assert_cmpint (layer, ==, 3);
	g_assert_false (lrg_lod_component_get_cast_shadows (c));
	g_assert_cmpstr (lrg_lod_component_get_mesh_asset (c), ==, "rock.glb");
	lrg_lod_component_set_receive_shadows (c, FALSE);
	lrg_lod_component_set_layer (c, 99);
	g_assert_cmpint (lrg_lod_component_get_layer (c), ==, 31);
	lrg_lod_component_set_color (c, 1.0f, 0.0f, 0.0f, 1.0f);
	lrg_lod_component_set_material (c, NULL);
	lrg_lod_component_set_asset_manager (c, NULL);

	/* Items of other components draw with lodFade 0. */
	lrg_render_item_init (&item);
	item.user_data = GUINT_TO_POINTER (0x3f000000u);
	g_assert_cmpfloat (lrg_lod_component_get_item_fade (&item, &layer), ==, 0.0f);
	g_assert_cmpint (layer, ==, -1);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

/* A sphere model (unindexed raylib mesh with UVs and normals). */
static GrlModel *
sphere_model (void)
{
	g_autoptr(GrlMesh) mesh = grl_mesh_new_sphere (1.0f, 24, 32);

	return grl_model_new_from_mesh (mesh);
}

static void
test_gl_generate (void)
{
	g_autoptr(GrlModel)        model = NULL;
	g_autoptr(LrgLodGroup)     g = NULL;
	g_autoptr(LrgMeshSimplify) s = NULL;
	g_autoptr(GError)          error = NULL;
	static const gfloat        ratios[2] = { 0.5f, 0.1f };
	gfloat                     center[3];
	gfloat                     radius;
	gint                       t0, t1, t2;
	Model                     *part;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	model = sphere_model ();
	s = lrg_mesh_simplify_new ();
	lrg_mesh_simplify_set_target_error (s, 1.0f);
	g = lrg_lod_group_new_from_model (model, s, 3, ratios, 2, NULL, 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (g);
	g_assert_cmpuint (lrg_lod_group_get_n_levels (g), ==, 3);
	g_assert_true (lrg_lod_group_get_level_model (g, 0) == model);

	t0 = lrg_lod_group_get_level_triangle_count (g, 0);
	t1 = lrg_lod_group_get_level_triangle_count (g, 1);
	t2 = lrg_lod_group_get_level_triangle_count (g, 2);
	g_assert_cmpint (t1, <=, (gint) (t0 * 0.5f + 1));
	g_assert_cmpint (t2, <=, (gint) (t0 * 0.1f + 1));
	g_assert_cmpint (t2, <, t1);
	g_assert_cmpfloat (lrg_lod_group_get_level_error (g, 2), >=, lrg_lod_group_get_level_error (g, 1));
	/* Default thresholds: 0.5, 0.25, 0 (last never culls). */
	g_assert_cmpfloat (lrg_lod_group_get_level_screen_size (g, 0), ==, 0.5f);
	g_assert_cmpfloat (lrg_lod_group_get_level_screen_size (g, 1), ==, 0.25f);
	g_assert_cmpfloat (lrg_lod_group_get_level_screen_size (g, 2), ==, 0.0f);

	/* Bounds from level 0: the unit sphere. */
	g_assert_true (lrg_lod_group_get_bounding_sphere (g, center, &radius));
	g_assert_cmpfloat_with_epsilon (center[1], 0.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (radius, sqrtf (3.0f), 1e-2f);

	/* A generated part keeps the attributes and is uploaded. */
	part = (Model *) grl_model_get_handle (lrg_lod_group_get_level_model (g, 1));
	g_assert_cmpint (part->meshCount, ==, 1);
	g_assert_nonnull (part->meshes[0].texcoords);
	g_assert_nonnull (part->meshes[0].normals);
	g_assert_cmpuint (part->meshes[0].vaoId, >, 0);
	g_assert_cmpint (lrg_lod_group_find_level_of_model (g, lrg_lod_group_get_level_model (g, 2)), ==, 2);
}

static void
test_gl_meta (void)
{
	g_autoptr(GrlModel)     model = NULL;
	g_autoptr(LrgLodGroup)  g = NULL;
	g_autoptr(LrgLodGroup)  single = NULL;
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(GError)       error = NULL;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	model = sphere_model ();
	single = lrg_lod_group_new_from_model_with_meta (model, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_lod_group_get_n_levels (single), ==, 1);

	meta = lrg_asset_meta_new ();
	lrg_asset_meta_set_importer (meta, "model");
	lrg_asset_meta_set_model_lod_levels (meta, 4);
	lrg_asset_meta_set_model_lod_ratios (meta, "0.5, 0.25, 0.1");
	lrg_asset_meta_set_model_lod_screen_sizes (meta, "0.6, 0.3, 0.1, 0.01");
	lrg_asset_meta_set_model_lod_max_error (meta, 1.0);
	lrg_asset_meta_set_model_lod_hysteresis (meta, 0.2);
	lrg_asset_meta_set_model_lod_fade_duration (meta, 0.0);
	g = lrg_lod_group_new_from_model_with_meta (model, meta, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_lod_group_get_n_levels (g), ==, 4);
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_level_screen_size (g, 3), 0.01f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_lod_group_get_hysteresis (g), 0.2f, 1e-6f);
	g_assert_cmpfloat (lrg_lod_group_get_fade_duration (g), ==, 0.0f);
	g_assert_cmpint (lrg_lod_group_get_level_triangle_count (g, 3), <,
	                 lrg_lod_group_get_level_triangle_count (g, 2));

	/* A bad list fails cleanly. */
	g_clear_object (&g);
	lrg_asset_meta_set_setting_string (meta, "lod-ratios", "0.5, oops");
	g = lrg_lod_group_new_from_model_with_meta (model, meta, &error);
	g_assert_null (g);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

/* Items of the selected level, then both levels during a fade with
 * complementary packed fades. */
static void
test_gl_component_items (void)
{
	g_autoptr(GrlModel)        model = NULL;
	g_autoptr(LrgLodGroup)     g = NULL;
	g_autoptr(LrgMeshSimplify) simp = NULL;
	g_autoptr(GError)          error = NULL;
	Scene                      s;
	guint                      i;
	guint                      n;
	gfloat                     sum = 0.0f;
	gint                       levels_seen = 0;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	model = sphere_model ();
	simp = lrg_mesh_simplify_new ();
	lrg_mesh_simplify_set_target_error (simp, 1.0f);
	g = lrg_lod_group_new_from_model (model, simp, 3, NULL, 0, NULL, 0, &error);
	g_assert_no_error (error);
	lrg_lod_group_set_hysteresis (g, 0.0f);
	lrg_lod_group_set_fade_duration (g, 1.0f);
	scene_init (&s, g);

	/* Close: level 0, one item, the source model, opaque, caster. */
	g_assert_cmpint (scene_level_at (&s, 3.0f), ==, 0);
	n = lrg_render_list_get_n_items (s.list, LRG_RENDER_CATEGORY_OPAQUE);
	g_assert_cmpuint (n, ==, 1);
	{
		const LrgRenderItem *item = lrg_render_list_get_item (s.list, LRG_RENDER_CATEGORY_OPAQUE, 0);
		gint                 level;

		g_assert_true (item->model == (gpointer) model);
		g_assert_cmpfloat (lrg_lod_component_get_item_fade (item, &level), ==, 0.0f);
		g_assert_cmpint (level, ==, 0);
		g_assert_true ((item->flags & LRG_RENDER_ITEM_FLAG_CAST_SHADOW) != 0);
	}

	/* Step back to level 1 and half a fade: both levels, fades +-0.5. */
	lrg_component_update (LRG_COMPONENT (s.lod), 0.1f);
	g_assert_cmpint (scene_level_at (&s, 10.0f), ==, 1);
	/* (fade steps are clamped to 0.25 s per collect) */
	lrg_component_update (LRG_COMPONENT (s.lod), 0.25f);
	scene_level_at (&s, 10.0f);
	lrg_component_update (LRG_COMPONENT (s.lod), 0.25f);
	scene_level_at (&s, 10.0f);
	n = lrg_render_list_get_n_items (s.list, LRG_RENDER_CATEGORY_OPAQUE);
	g_assert_cmpuint (n, ==, 2);
	for (i = 0; i < n; i++)
	{
		const LrgRenderItem *item = lrg_render_list_get_item (s.list, LRG_RENDER_CATEGORY_OPAQUE, i);
		gint                 level;
		gfloat               fade = lrg_lod_component_get_item_fade (item, &level);

		sum += fade;
		levels_seen |= 1 << level;
		g_assert_cmpfloat_with_epsilon (fabsf (fade), 0.5f, 1e-5f);
		/* Only the incoming level casts shadows. */
		g_assert_true (((item->flags & LRG_RENDER_ITEM_FLAG_CAST_SHADOW) != 0) == (level == 1));
	}
	g_assert_cmpfloat_with_epsilon (sum, 0.0f, 1e-5f);
	g_assert_cmpint (levels_seen, ==, 3);
	scene_clear (&s);
}

/* End to end: an OBJ sphere with a sidecar asking for 3 levels, drawn by
 * two components through an asset manager; both share one chain. */
static void
test_gl_mesh_asset (void)
{
	g_autoptr(GError)          error = NULL;
	g_autoptr(GString)         obj = g_string_new ("# sphere\n");
	g_autoptr(LrgAssetManager) assets = NULL;
	g_autoptr(LrgLodComponent) second = NULL;
	g_autoptr(LrgEntity3D)     other = NULL;
	g_autofree gchar          *dir = NULL;
	g_autofree gchar          *path = NULL;
	g_autofree gchar          *meta_path = NULL;
	g_autoptr(LrgLodGroup)     dummy = lrg_lod_group_new ();
	Scene                      s;
	guint                      rings = 16;
	guint                      segs = 24;
	guint                      i, j;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	/* A closed UV sphere as an OBJ (poles shared). */
	g_string_append (obj, "v 0 1 0\n");
	for (i = 1; i < rings; i++)
		for (j = 0; j < segs; j++)
		{
			gdouble th = G_PI * i / rings;
			gdouble ph = 2.0 * G_PI * j / segs;

			g_string_append_printf (obj, "v %.6f %.6f %.6f\n", sin (th) * cos (ph), cos (th), sin (th) * sin (ph));
		}
	g_string_append (obj, "v 0 -1 0\n");
	for (j = 0; j < segs; j++)
		g_string_append_printf (obj, "f 1 %u %u\n", 2 + (j + 1) % segs, 2 + j);
	for (i = 0; i + 2 < rings; i++)
		for (j = 0; j < segs; j++)
		{
			guint a = 2 + i * segs + j;
			guint b = 2 + i * segs + (j + 1) % segs;
			guint c = a + segs;
			guint d = b + segs;

			g_string_append_printf (obj, "f %u %u %u\nf %u %u %u\n", a, b, d, a, d, c);
		}
	for (j = 0; j < segs; j++)
	{
		guint last = 2 + (rings - 1) * segs;

		g_string_append_printf (obj, "f %u %u %u\n", 2 + (rings - 2) * segs + j,
		                        2 + (rings - 2) * segs + (j + 1) % segs, last);
	}

	dir = g_dir_make_tmp ("lrg-lod-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "ball.obj", NULL);
	meta_path = g_strconcat (path, ".meta", NULL);
	g_assert_true (g_file_set_contents (path, obj->str, -1, &error));
	g_assert_true (g_file_set_contents (meta_path,
	                                    "version: 1\n"
	                                    "guid: 0f0e0d0c-0b0a-4908-8706-050403020100\n"
	                                    "importer: model\n"
	                                    "settings:\n"
	                                    "  lod-levels: 3\n"
	                                    "  lod-ratios: [0.5, 0.2]\n"
	                                    "  lod-screen-sizes: [0.5, 0.2, 0]\n"
	                                    "  lod-max-error: 1.0\n", -1, &error));

	assets = lrg_asset_manager_new ();
	lrg_asset_manager_add_search_path (assets, dir);

	/* The scene helper wants a group: start with an empty one and switch
	 * the component to the asset. */
	scene_init (&s, dummy);
	lrg_lod_component_set_group (s.lod, NULL);
	lrg_lod_component_set_asset_manager (s.lod, assets);
	lrg_lod_component_set_mesh_asset (s.lod, "ball.obj");

	g_assert_cmpint (scene_level_at (&s, 3.0f), ==, 0);
	g_assert_nonnull (lrg_lod_component_get_group (s.lod));
	g_assert_cmpuint (lrg_lod_group_get_n_levels (lrg_lod_component_get_group (s.lod)), ==, 3);
	g_assert_cmpint (lrg_lod_group_get_level_triangle_count (lrg_lod_component_get_group (s.lod), 2), <,
	                 lrg_lod_group_get_level_triangle_count (lrg_lod_component_get_group (s.lod), 0));
	g_assert_cmpuint (lrg_render_list_get_n_items (s.list, LRG_RENDER_CATEGORY_OPAQUE), ==, 1);

	/* A second component on the same asset reuses the chain. */
	second = lrg_lod_component_new ();
	g_object_set (second, "asset-manager", assets, "mesh-asset", "ball.obj", NULL);
	other = lrg_entity3d_new_with_name ("other");
	g_assert_true (lrg_entity3d_add_component (other, LRG_COMPONENT (second)));
	lrg_world3d_add_entity (s.world, other);
	scene_level_at (&s, 3.0f);
	g_assert_true (lrg_lod_component_get_group (second) == lrg_lod_component_get_group (s.lod));

	scene_clear (&s);
	g_remove (meta_path);
	g_remove (path);
	g_rmdir (dir);
}

/* lrg_lod_bind_fade() sets the uniform of a program that declares it,
 * and skips one that does not. */
static void
test_gl_bind_fade (void)
{
	g_autofree gchar *fs = NULL;
	Shader            shader;
	gint              loc;
	gfloat            value = 0.0f;
	static const gchar *vs =
		"#version 330\n"
		"in vec3 vertexPosition;\n"
		"uniform mat4 mvp;\n"
		"void main () { gl_Position = mvp * vec4 (vertexPosition, 1.0); }\n";

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	fs = g_strconcat ("#version 330\n", lrg_lod_get_glsl (),
	                  "out vec4 finalColor;\n"
	                  "void main () { lrg_lod_fade_discard (); finalColor = vec4 (lodFade); }\n", NULL);
	shader = LoadShaderFromMemory (vs, fs);
	g_assert_cmpuint (shader.id, >, 0);
	g_assert_true (shader.id != rlGetShaderIdDefault ());
	loc = rlGetLocationUniform (shader.id, LRG_LOD_FADE_UNIFORM);
	g_assert_cmpint (loc, >=, 0);

	lrg_lod_bind_fade (shader.id, -0.25f);
	{
		/* Read the value back through raylib's GL loader. */
		typedef void (*GetUniformfv) (guint, gint, gfloat *);
		GetUniformfv get = (GetUniformfv) rlGetProcAddress ("glGetUniformfv");

		if (get != NULL)
		{
			get (shader.id, loc, &value);
			g_assert_cmpfloat_with_epsilon (value, -0.25f, 1e-6f);
		}
	}
	/* The default shader has no lodFade: nothing happens. */
	lrg_lod_bind_fade (rlGetShaderIdDefault (), 0.5f);
	rlDisableShader ();
	UnloadShader (shader);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/lod/screen-size/compute", test_screen_size_compute);
	g_test_add_func ("/lod/screen-size/view-projection", test_screen_size_view_projection);
	g_test_add_func ("/lod/select/thresholds", test_select_thresholds);
	g_test_add_func ("/lod/select/hysteresis", test_select_hysteresis);
	g_test_add_func ("/lod/select/bias-force", test_select_bias_force);
	g_test_add_func ("/lod/state/fade", test_state_fade);
	g_test_add_func ("/lod/glsl", test_glsl);
	g_test_add_func ("/lod/meta/keys", test_meta_keys);
	g_test_add_func ("/lod/meta/apply", test_apply_meta);
	g_test_add_func ("/lod/component/select", test_component_select);
	g_test_add_func ("/lod/component/fade-clock", test_component_fade_clock);
	g_test_add_func ("/lod/component/properties", test_component_properties);
	g_test_add_func ("/lod/gl/generate", test_gl_generate);
	g_test_add_func ("/lod/gl/meta", test_gl_meta);
	g_test_add_func ("/lod/gl/component-items", test_gl_component_items);
	g_test_add_func ("/lod/gl/mesh-asset", test_gl_mesh_asset);
	g_test_add_func ("/lod/gl/bind-fade", test_gl_bind_fade);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
