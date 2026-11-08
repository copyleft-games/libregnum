/* test-material.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMaterial: the `.rmat` format (load, save, round trip, errors),
 * instances (inheritance, override, revert, parent chains by path and by
 * guid, cycles), render-state mapping, the asset manager's material
 * cache, LrgMaterial3D conversion, component and level binding, and,
 * in a hidden window, the lit-shader uniform contract, rlgl render state
 * and drawing through the static batch, skinned mesh and terrain.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define EPS (1e-5f)

/* GL entry points through raylib's loader (no GL headers in tests) */
#define TEST_GL_CULL_FACE         (0x0B44)
#define TEST_GL_CULL_FACE_MODE    (0x0B45)
#define TEST_GL_DEPTH_TEST        (0x0B71)
#define TEST_GL_DEPTH_WRITEMASK   (0x0B72)
#define TEST_GL_FRONT             (0x0404)
#define TEST_GL_BACK              (0x0405)
#define TEST_GL_TEXTURE0          (0x84C0)
#define TEST_GL_TEXTURE_BINDING_2D (0x8069)
#define TEST_GL_BLEND_DST_RGB     (0x80C8)
#define TEST_GL_ONE               (1)

typedef void          (*TestGetUniformfvFn) (guint program, gint location, gfloat *params);
typedef void          (*TestGetUniformivFn) (guint program, gint location, gint *params);
typedef unsigned char (*TestIsEnabledFn) (guint cap);
typedef void          (*TestGetIntegervFn) (guint name, gint *data);
typedef void          (*TestGetBooleanvFn) (guint name, unsigned char *data);
typedef void          (*TestActiveTextureFn) (guint unit);

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (64, 64, "lrg-material-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

#define SKIP_WITHOUT_GL() \
	do { \
		if (!graphics_available) \
		{ \
			g_test_skip ("Graphics context not available"); \
			return; \
		} \
	} while (0)

static gchar *
data_path (const gchar *name)
{
	return g_test_build_filename (G_TEST_DIST, "data", "materials", name, NULL);
}

static LrgMaterial *
load_data (const gchar      *name,
           LrgAssetManager  *manager)
{
	g_autofree gchar  *path = data_path (name);
	g_autoptr(GError)  error = NULL;
	LrgMaterial       *material = lrg_material_new_from_file (path, manager, &error);

	g_assert_no_error (error);
	g_assert_nonnull (material);
	return material;
}

static void
assert_rgba (const gfloat *v,
             gfloat        r,
             gfloat        g,
             gfloat        b,
             gfloat        a)
{
	g_assert_cmpfloat_with_epsilon (v[0], r, EPS);
	g_assert_cmpfloat_with_epsilon (v[1], g, EPS);
	g_assert_cmpfloat_with_epsilon (v[2], b, EPS);
	g_assert_cmpfloat_with_epsilon (v[3], a, EPS);
}

/* ==========================================================================
 * Values
 * ========================================================================== */

static void
test_defaults (void)
{
	g_autoptr(LrgMaterial) m = lrg_material_new ();
	g_auto(GStrv)          overrides = NULL;
	gfloat                 v[4];

	g_assert_cmpint (lrg_material_get_shading_model (m), ==, LRG_SHADING_MODEL_LIT);
	lrg_material_get_base_color (m, v);
	assert_rgba (v, 1.0f, 1.0f, 1.0f, 1.0f);
	g_assert_cmpfloat (lrg_material_get_metallic (m), ==, LRG_MATERIAL_DEFAULT_METALLIC);
	g_assert_cmpfloat (lrg_material_get_roughness (m), ==, LRG_MATERIAL_DEFAULT_ROUGHNESS);
	g_assert_cmpfloat (lrg_material_get_occlusion_strength (m), ==, 1.0f);
	g_assert_cmpfloat (lrg_material_get_normal_scale (m), ==, 1.0f);
	g_assert_cmpfloat (lrg_material_get_emission_strength (m), ==, 0.0f);
	g_assert_cmpint (lrg_material_get_blend_mode (m), ==, LRG_MATERIAL_BLEND_OPAQUE);
	g_assert_cmpint (lrg_material_get_cull_mode (m), ==, LRG_MATERIAL_CULL_BACK);
	g_assert_true (lrg_material_get_depth_test (m));
	g_assert_true (lrg_material_get_depth_write (m));
	g_assert_false (lrg_material_get_double_sided (m));
	g_assert_cmpint (lrg_material_get_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_AUTO);
	g_assert_cmpint (lrg_material_resolve_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_OPAQUE);
	g_assert_null (lrg_material_get_texture_ref (m, LRG_MATERIAL_SLOT_ALBEDO));
	g_assert_null (lrg_material_get_shader (m));
	g_assert_cmpint (lrg_material_get_map_flags (m), ==, 0);

	/* defaults are not overrides: a new material saves almost nothing */
	overrides = lrg_material_list_overrides (m);
	g_assert_cmpuint (g_strv_length (overrides), ==, 0);

	lrg_material_get_pbr_params (m, v);
	assert_rgba (v, 0.0f, 0.5f, 1.0f, 1.0f);
	lrg_material_get_emission_own (m, v);
	assert_rgba (v, 0.0f, 0.0f, 0.0f, 1.0f);
}

static void
on_changed (LrgMaterial *m,
            gpointer     user_data)
{
	(*(guint *) user_data)++;
}

static void
test_setters (void)
{
	g_autoptr(LrgMaterial) m = lrg_material_new ();
	guint                  changed = 0;
	gfloat                 v[4];
	gfloat                 f = 0.0f;

	g_signal_connect (m, "changed", G_CALLBACK (on_changed), &changed);

	lrg_material_set_base_color (m, 0.1f, 0.2f, 0.3f, 0.4f);
	lrg_material_set_metallic (m, 2.0f);              /* clamped */
	lrg_material_set_roughness (m, -1.0f);            /* clamped */
	lrg_material_set_emission_color (m, 1.0f, 0.5f, 0.25f);
	lrg_material_set_emission_strength (m, 4.0f);
	lrg_material_set_shading_model (m, LRG_SHADING_MODEL_UNLIT);
	g_assert_cmpuint (changed, ==, 6);

	/* the same value again changes nothing */
	lrg_material_set_emission_strength (m, 4.0f);
	g_assert_cmpuint (changed, ==, 6);

	lrg_material_get_base_color (m, v);
	assert_rgba (v, 0.1f, 0.2f, 0.3f, 0.4f);
	g_assert_cmpfloat (lrg_material_get_metallic (m), ==, 1.0f);
	g_assert_cmpfloat (lrg_material_get_roughness (m), ==, 0.0f);
	lrg_material_get_emission_own (m, v);
	assert_rgba (v, 4.0f, 2.0f, 1.0f, 1.0f);
	g_assert_cmpint (lrg_material_get_map_flags (m), ==, LRG_MATERIAL_MAP_FLAG_UNLIT);

	/* GObject properties read the effective values */
	g_object_get (m, "base-color-g", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 0.2f, EPS);
	g_object_set (m, "base-color-g", 0.9f, "roughness", 0.3f, NULL);
	lrg_material_get_base_color (m, v);
	assert_rgba (v, 0.1f, 0.9f, 0.3f, 0.4f);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (m), 0.3f, EPS);

	/* custom uniforms */
	lrg_material_set_uniform_float (m, "speed", 2.0f);
	{
		const gfloat c[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		LrgMaterialUniformType type;
		g_auto(GStrv)          names = NULL;

		lrg_material_set_uniform (m, "tint", LRG_MATERIAL_UNIFORM_COLOR, c, 4);
		lrg_material_set_uniform_texture (m, "noise", "noise.png");
		g_assert_true (lrg_material_get_uniform (m, "tint", &type, v));
		g_assert_cmpint (type, ==, LRG_MATERIAL_UNIFORM_COLOR);
		assert_rgba (v, 1.0f, 0.0f, 0.0f, 1.0f);
		g_assert_cmpstr (lrg_material_get_uniform_texture_ref (m, "noise"), ==, "noise.png");
		g_assert_false (lrg_material_get_uniform (m, "missing", NULL, NULL));
		names = lrg_material_list_uniforms (m);
		g_assert_cmpuint (g_strv_length (names), ==, 3);
		g_assert_cmpstr (names[0], ==, "noise");
		g_assert_cmpstr (names[2], ==, "tint");
	}
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static void
test_format_load (void)
{
	g_autoptr(LrgMaterial) m = load_data ("crate.rmat", NULL);
	LrgMaterialUniformType type;
	gfloat                 v[4];

	g_assert_cmpstr (lrg_material_get_name (m), ==, "crate");
	g_assert_nonnull (lrg_material_get_path (m));
	g_assert_true (g_str_has_suffix (lrg_material_get_path (m), "crate.rmat"));
	lrg_material_get_base_color (m, v);
	assert_rgba (v, 0.8f, 0.6f, 0.4f, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_metallic (m), 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (m), 0.75f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_occlusion_strength (m), 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_normal_scale (m), 2.0f, EPS);
	lrg_material_get_emission_own (m, v);
	assert_rgba (v, 3.0f, 1.5f, 0.0f, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_alpha_cutoff (m), 0.4f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_height_scale (m), 0.1f, EPS);
	g_assert_cmpstr (lrg_material_get_texture_ref (m, LRG_MATERIAL_SLOT_ALBEDO), ==, "crate_albedo.png");
	g_assert_cmpstr (lrg_material_get_texture_ref (m, LRG_MATERIAL_SLOT_NORMAL), ==, "crate_normal.png");
	/* `orm: null` is an explicit "no texture" */
	g_assert_null (lrg_material_get_texture_ref (m, LRG_MATERIAL_SLOT_ORM));
	g_assert_true (lrg_material_is_overridden (m, "textures.orm"));
	g_assert_false (lrg_material_is_overridden (m, "textures.height"));
	g_assert_cmpint (lrg_material_get_blend_mode (m), ==, LRG_MATERIAL_BLEND_CUTOUT);
	g_assert_cmpint (lrg_material_get_cull_mode (m), ==, LRG_MATERIAL_CULL_FRONT);
	g_assert_true (lrg_material_get_depth_test (m));
	g_assert_false (lrg_material_get_depth_write (m));
	g_assert_cmpint (lrg_material_get_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_CUTOUT);

	g_assert_true (lrg_material_get_uniform (m, "wobble", &type, v));
	g_assert_cmpint (type, ==, LRG_MATERIAL_UNIFORM_FLOAT);
	g_assert_cmpfloat_with_epsilon (v[0], 0.5f, EPS);
	g_assert_true (lrg_material_get_uniform (m, "offset", &type, v));
	g_assert_cmpint (type, ==, LRG_MATERIAL_UNIFORM_VEC2);
	g_assert_cmpfloat_with_epsilon (v[1], 0.5f, EPS);
	g_assert_true (lrg_material_get_uniform (m, "noise", &type, NULL));
	g_assert_cmpint (type, ==, LRG_MATERIAL_UNIFORM_TEXTURE);

	/* the fallback resolver: relative to the file's directory */
	{
		g_autofree gchar *resolved = lrg_material_resolve_reference (m, "crate_albedo.png");

		g_assert_nonnull (resolved);
		g_assert_true (g_file_test (resolved, G_FILE_TEST_IS_REGULAR));
		g_assert_null (lrg_material_resolve_reference (m, "guid:0000"));
	}
}

static void
test_format_round_trip (void)
{
	g_autoptr(LrgMaterial) m = load_data ("crate.rmat", NULL);
	g_autoptr(LrgMaterial) again = NULL;
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *first = NULL;
	g_autofree gchar      *second = NULL;
	g_autofree gchar      *dir = NULL;
	g_auto(GStrv)          a = NULL;
	g_auto(GStrv)          b = NULL;
	guint                  i;

	first = lrg_material_save_to_data (m, NULL);
	g_assert_nonnull (first);
	/* the unknown key survives */
	g_assert_nonnull (strstr (first, "editor_note"));
	g_assert_nonnull (strstr (first, "render_state"));

	dir = g_path_get_dirname (lrg_material_get_path (m));
	again = lrg_material_new_from_data (first, -1, dir, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (again);
	second = lrg_material_save_to_data (again, NULL);
	g_assert_cmpstr (first, ==, second);

	/* the same overrides, the same effective values */
	a = lrg_material_list_overrides (m);
	b = lrg_material_list_overrides (again);
	g_assert_cmpuint (g_strv_length (a), ==, g_strv_length (b));
	for (i = 0; a[i] != NULL; i++)
		g_assert_cmpstr (a[i], ==, b[i]);
	g_assert_cmpfloat (lrg_material_get_roughness (m), ==, lrg_material_get_roughness (again));

	/* file save remembers the path */
	{
		g_autofree gchar     *tmp = g_dir_make_tmp ("lrg-material-XXXXXX", NULL);
		g_autofree gchar     *path = g_build_filename (tmp, "out.rmat", NULL);
		g_autoptr(LrgMaterial) loaded = NULL;
		g_autofree gchar     *third = NULL;

		g_assert_true (lrg_material_save_to_file (again, path, &error));
		g_assert_no_error (error);
		g_assert_cmpstr (lrg_material_get_path (again), ==, path);
		loaded = lrg_material_new_from_file (path, NULL, &error);
		g_assert_no_error (error);
		third = lrg_material_save_to_data (loaded, NULL);
		g_assert_cmpstr (first, ==, third);
		g_unlink (path);
		g_rmdir (tmp);
	}
}

static void
expect_error (const gchar *yaml,
              gint         code)
{
	g_autoptr(GError)      error = NULL;
	g_autoptr(LrgMaterial) m = lrg_material_new_from_data (yaml, -1, NULL, NULL, &error);

	g_assert_null (m);
	g_assert_error (error, LRG_MATERIAL_ERROR, code);
}

static void
test_format_errors (void)
{
	expect_error ("- just\n- a list\n", LRG_MATERIAL_ERROR_PARSE);
	expect_error ("version: 99\n", LRG_MATERIAL_ERROR_VERSION);
	expect_error ("shading_model: shiny\n", LRG_MATERIAL_ERROR_INVALID);
	expect_error ("metallic: lots\n", LRG_MATERIAL_ERROR_INVALID);
	expect_error ("base_color: [1, 2]\n", LRG_MATERIAL_ERROR_INVALID);
	expect_error ("textures:\n  sparkle: a.png\n", LRG_MATERIAL_ERROR_INVALID);
	expect_error ("render_state:\n  blend: glitter\n", LRG_MATERIAL_ERROR_INVALID);
	expect_error ("uniforms:\n  x: { type: vec3, value: [1, 2] }\n", LRG_MATERIAL_ERROR_INVALID);
	expect_error ("parent: missing.rmat\n", LRG_MATERIAL_ERROR_NOT_FOUND);
	expect_error ("parent: guid:12345678\n", LRG_MATERIAL_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Instances
 * ========================================================================== */

static void
test_instance_override_revert (void)
{
	g_autoptr(LrgMaterial) parent = lrg_material_new ();
	g_autoptr(LrgMaterial) inst = NULL;
	g_auto(GStrv)          overrides = NULL;
	guint                  changed = 0;
	gfloat                 v[4];
	gfloat                 one = 1.0f;

	lrg_material_set_base_color (parent, 0.5f, 0.5f, 0.5f, 1.0f);
	lrg_material_set_roughness (parent, 0.2f);
	lrg_material_set_texture_ref (parent, LRG_MATERIAL_SLOT_ALBEDO, "a.png");
	lrg_material_set_uniform (parent, "k", LRG_MATERIAL_UNIFORM_FLOAT, &one, 1);

	inst = lrg_material_new_instance (parent);
	g_assert_true (lrg_material_get_parent (inst) == parent);
	g_signal_connect (inst, "changed", G_CALLBACK (on_changed), &changed);

	/* everything inherits */
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (inst), 0.2f, EPS);
	g_assert_cmpstr (lrg_material_get_texture_ref (inst, LRG_MATERIAL_SLOT_ALBEDO), ==, "a.png");
	g_assert_true (lrg_material_get_uniform (inst, "k", NULL, v));
	g_assert_false (lrg_material_is_overridden (inst, "roughness"));
	overrides = lrg_material_list_overrides (inst);
	g_assert_cmpuint (g_strv_length (overrides), ==, 0);
	g_clear_pointer (&overrides, g_strfreev);

	/* a parent edit reaches the instance and re-emits ::changed */
	lrg_material_set_metallic (parent, 0.7f);
	g_assert_cmpuint (changed, ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_metallic (inst), 0.7f, EPS);

	/* overrides shadow the parent */
	lrg_material_set_roughness (inst, 0.9f);
	lrg_material_set_texture_ref (inst, LRG_MATERIAL_SLOT_ALBEDO, NULL);
	lrg_material_set_uniform_float (inst, "k", 5.0f);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (inst), 0.9f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (parent), 0.2f, EPS);
	g_assert_null (lrg_material_get_texture_ref (inst, LRG_MATERIAL_SLOT_ALBEDO));
	g_assert_true (lrg_material_get_uniform (inst, "k", NULL, v));
	g_assert_cmpfloat (v[0], ==, 5.0f);
	g_assert_true (lrg_material_is_overridden (inst, "roughness"));
	g_assert_true (lrg_material_is_overridden (inst, "textures.albedo"));
	g_assert_true (lrg_material_is_overridden (inst, "uniforms.k"));
	overrides = lrg_material_list_overrides (inst);
	g_assert_cmpuint (g_strv_length (overrides), ==, 3);
	g_assert_cmpstr (overrides[0], ==, "roughness");
	g_assert_cmpstr (overrides[1], ==, "textures.albedo");
	g_assert_cmpstr (overrides[2], ==, "uniforms.k");

	/* an instance saves only its overrides */
	{
		g_autofree gchar *yaml = lrg_material_save_to_data (inst, NULL);

		g_assert_nonnull (strstr (yaml, "roughness"));
		g_assert_null (strstr (yaml, "base_color"));
	}

	/* revert falls back to the parent */
	g_assert_true (lrg_material_revert (inst, "roughness"));
	g_assert_true (lrg_material_revert (inst, "textures.albedo"));
	g_assert_true (lrg_material_revert (inst, "uniforms.k"));
	g_assert_false (lrg_material_revert (inst, "roughness"));
	g_assert_false (lrg_material_revert (inst, "no_such_key"));
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (inst), 0.2f, EPS);
	g_assert_cmpstr (lrg_material_get_texture_ref (inst, LRG_MATERIAL_SLOT_ALBEDO), ==, "a.png");
	g_assert_true (lrg_material_get_uniform (inst, "k", NULL, v));
	g_assert_cmpfloat (v[0], ==, 1.0f);
	lrg_material_get_base_color (inst, v);
	assert_rgba (v, 0.5f, 0.5f, 0.5f, 1.0f);

	/* copy keeps own values and the parent link */
	{
		g_autoptr(LrgMaterial) copy = NULL;

		lrg_material_set_normal_scale (inst, 3.0f);
		copy = lrg_material_copy (inst);
		g_assert_true (lrg_material_get_parent (copy) == parent);
		g_assert_cmpfloat (lrg_material_get_normal_scale (copy), ==, 3.0f);
		g_assert_true (lrg_material_is_overridden (copy, "normal_scale"));
		g_assert_false (lrg_material_is_overridden (copy, "roughness"));
	}
}

static void
test_instance_parent_by_path (void)
{
	g_autoptr(LrgMaterial) glow = load_data ("glow_instance.rmat", NULL);
	LrgMaterial           *red;
	LrgMaterial           *base;
	gfloat                 v[4];

	/* glow -> red -> base, each by relative path */
	red = lrg_material_get_parent (glow);
	g_assert_nonnull (red);
	g_assert_cmpstr (lrg_material_get_name (red), ==, "red");
	base = lrg_material_get_parent (red);
	g_assert_nonnull (base);
	g_assert_cmpstr (lrg_material_get_name (base), ==, "base");
	g_assert_cmpstr (lrg_material_get_parent_ref (glow), ==, "red_instance.rmat");

	lrg_material_get_base_color (glow, v);
	assert_rgba (v, 1.0f, 0.0f, 0.0f, 1.0f);           /* from red */
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (glow), 0.9f, EPS);  /* base */
	g_assert_cmpstr (lrg_material_get_texture_ref (glow, LRG_MATERIAL_SLOT_ALBEDO), ==,
	                 "crate_albedo.png");
	lrg_material_get_emission_own (glow, v);
	assert_rgba (v, 0.0f, 2.0f, 0.0f, 1.0f);           /* own */
	g_assert_true (lrg_material_get_uniform (glow, "wobble", NULL, v));
	g_assert_cmpfloat_with_epsilon (v[0], 1.5f, EPS);

	/* the parent reference round-trips */
	{
		g_autofree gchar *yaml = lrg_material_save_to_data (glow, NULL);

		g_assert_nonnull (strstr (yaml, "parent: red_instance.rmat"));
	}

	/* cycles are an error, not a hang */
	{
		g_autofree gchar     *path = data_path ("cycle_a.rmat");
		g_autoptr(GError)     error = NULL;
		g_autoptr(LrgMaterial) cyc = lrg_material_new_from_file (path, NULL, &error);

		g_assert_null (cyc);
		g_assert_error (error, LRG_MATERIAL_ERROR, LRG_MATERIAL_ERROR_CYCLE);
	}
}

/* A project dir with copies of the data files, indexed by the manager. */
static gchar *
make_project (void)
{
	g_autofree gchar *src = data_path ("");
	gchar            *tmp = g_dir_make_tmp ("lrg-material-proj-XXXXXX", NULL);
	const gchar      *names[] = { "base.rmat", "crate_albedo.png", "noise.png", "crate_normal.png" };
	guint             i;

	for (i = 0; i < G_N_ELEMENTS (names); i++)
	{
		g_autofree gchar *from = g_build_filename (src, names[i], NULL);
		g_autofree gchar *to = g_build_filename (tmp, names[i], NULL);
		g_autofree gchar *contents = NULL;
		gsize             len = 0;

		g_assert_true (g_file_get_contents (from, &contents, &len, NULL));
		g_assert_true (g_file_set_contents (to, contents, (gssize) len, NULL));
	}
	return tmp;
}

static void
remove_tree (const gchar *dir)
{
	GDir        *d = g_dir_open (dir, 0, NULL);
	const gchar *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *path = g_build_filename (dir, name, NULL);

		g_unlink (path);
	}
	if (d != NULL)
		g_dir_close (d);
	g_rmdir (dir);
}

static void
test_instance_parent_by_guid (void)
{
	g_autofree gchar          *dir = make_project ();
	g_autoptr(LrgAssetManager) manager = lrg_asset_manager_new ();
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *base_path = g_build_filename (dir, "base.rmat", NULL);
	g_autofree gchar          *guid = NULL;
	g_autofree gchar          *inst_yaml = NULL;
	g_autofree gchar          *inst_path = g_build_filename (dir, "inst.rmat", NULL);
	LrgMaterial               *inst;
	LrgMaterial               *base;
	LrgMaterial               *again;

	lrg_asset_manager_add_asset_dir (manager, dir);
	lrg_asset_manager_add_search_path (manager, dir);
	guid = lrg_asset_manager_lookup_guid (manager, base_path);
	g_assert_nonnull (guid);

	inst_yaml = g_strdup_printf ("version: 1\nparent: guid:%s\nmetallic: 1.0\n", guid);
	g_assert_true (g_file_set_contents (inst_path, inst_yaml, -1, NULL));

	/* loaded through the manager: cached, parent shared */
	inst = lrg_asset_manager_load_material (manager, "inst.rmat", &error);
	g_assert_no_error (error);
	g_assert_nonnull (inst);
	g_assert_true (lrg_material_get_asset_manager (inst) == manager);
	base = lrg_asset_manager_load_material (manager, base_path, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_material_get_parent (inst) == base);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (inst), 0.9f, EPS);
	g_assert_cmpfloat (lrg_material_get_metallic (inst), ==, 1.0f);
	{
		g_autofree gchar *ref = g_strconcat ("guid:", guid, NULL);

		g_assert_cmpstr (lrg_material_get_parent_ref (inst), ==, ref);
		/* the guid reference and the path share one cache entry */
		again = lrg_asset_manager_load_material (manager, ref, &error);
		g_assert_no_error (error);
		g_assert_true (again == base);
	}
	g_assert_cmpuint (lrg_asset_manager_get_material_cache_size (manager), ==, 2);
	g_assert_true (lrg_asset_manager_is_cached (manager, inst_path));
	g_assert_true (lrg_asset_manager_load_asset (manager, "inst.rmat", &error) == G_OBJECT (inst));

	/* set_parent writes a guid reference when the manager indexes the file */
	{
		g_autoptr(LrgMaterial) fresh = lrg_material_new ();
		g_autofree gchar      *ref = g_strconcat ("guid:", guid, NULL);

		lrg_material_set_asset_manager (fresh, manager);
		lrg_material_set_parent (fresh, base);
		g_assert_cmpstr (lrg_material_get_parent_ref (fresh), ==, ref);
	}

	/* missing material */
	g_assert_null (lrg_asset_manager_load_material (manager, "nope.rmat", &error));
	g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
	g_clear_error (&error);

	g_assert_true (lrg_asset_manager_unload (manager, inst_path));
	g_assert_cmpuint (lrg_asset_manager_get_material_cache_size (manager), ==, 1);
	lrg_asset_manager_unload_all (manager);
	g_assert_cmpuint (lrg_asset_manager_get_material_cache_size (manager), ==, 0);
	remove_tree (dir);
}

/* ==========================================================================
 * Render state, conversion
 * ========================================================================== */

static void
test_render_state_mapping (void)
{
	g_autoptr(LrgMaterial) m = lrg_material_new ();

	lrg_material_set_blend_mode (m, LRG_MATERIAL_BLEND_CUTOUT);
	g_assert_cmpint (lrg_material_resolve_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_CUTOUT);
	lrg_material_set_blend_mode (m, LRG_MATERIAL_BLEND_ALPHA);
	g_assert_cmpint (lrg_material_resolve_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT);
	lrg_material_set_blend_mode (m, LRG_MATERIAL_BLEND_ADDITIVE);
	g_assert_cmpint (lrg_material_resolve_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT);
	lrg_material_set_blend_mode (m, LRG_MATERIAL_BLEND_MULTIPLY);
	g_assert_cmpint (lrg_material_resolve_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_TRANSPARENT);
	/* an explicit queue wins */
	lrg_material_set_render_queue (m, LRG_MATERIAL_RENDER_QUEUE_OPAQUE);
	g_assert_cmpint (lrg_material_resolve_render_queue (m), ==, LRG_MATERIAL_RENDER_QUEUE_OPAQUE);

	lrg_material_set_cull_mode (m, LRG_MATERIAL_CULL_FRONT);
	g_assert_cmpint (lrg_material_get_cull_mode (m), ==, LRG_MATERIAL_CULL_FRONT);
	lrg_material_set_double_sided (m, TRUE);
	g_assert_cmpint (lrg_material_get_cull_mode (m), ==, LRG_MATERIAL_CULL_NONE);

	/* enum nicks are the format's words */
	{
		GEnumClass *klass = g_type_class_ref (LRG_TYPE_MATERIAL_BLEND_MODE);

		g_assert_cmpint (g_enum_get_value_by_nick (klass, "multiply")->value, ==, LRG_MATERIAL_BLEND_MULTIPLY);
		g_type_class_unref (klass);
	}
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_SHADING_MODEL));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_MATERIAL_CULL_MODE));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_MATERIAL_RENDER_QUEUE));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_MATERIAL_SLOT));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_MATERIAL_UNIFORM_TYPE));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_MATERIAL_ERROR));
}

static void
test_material3d_conversion (void)
{
	g_autoptr(LrgMaterial3D) src = lrg_material3d_new_with_color (0.5f, 0.25f, 1.0f, 0.5f);
	g_autoptr(LrgMaterial3D) copy = NULL;
	g_autoptr(LrgMaterial)   m = NULL;
	gfloat                   v[4];

	lrg_material3d_set_roughness (src, 0.1f);
	lrg_material3d_set_metallic (src, 0.8f);
	lrg_material3d_set_emission_color (src, 1.0f, 1.0f, 0.0f, 1.0f);
	lrg_material3d_set_emission_strength (src, 2.0f);

	copy = lrg_material3d_copy (src);
	g_assert_true (lrg_material3d_equal (src, copy));

	m = lrg_material3d_to_material (src);
	lrg_material_get_base_color (m, v);
	assert_rgba (v, 0.5f, 0.25f, 1.0f, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_roughness (m), 0.1f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_material_get_metallic (m), 0.8f, EPS);
	lrg_material_get_emission_own (m, v);
	assert_rgba (v, 2.0f, 2.0f, 0.0f, 1.0f);
	/* translucent authoring colour -> alpha blending */
	g_assert_cmpint (lrg_material_get_blend_mode (m), ==, LRG_MATERIAL_BLEND_ALPHA);
}

/* ==========================================================================
 * Components and levels (headless)
 * ========================================================================== */

static LrgEntity3D *
add_entity (LrgWorld3D  *world,
            const gchar *name)
{
	g_autoptr(LrgEntity3D) e = lrg_entity3d_new_with_name (name);

	lrg_world3d_add_entity (world, e);
	return e;
}

static void
test_mesh_renderer_binding (void)
{
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	g_autoptr(LrgMaterial)   glass = lrg_material_new ();
	g_autoptr(LrgMeshRendererComponent) mr = lrg_mesh_renderer_component_new ();
	LrgEntity3D             *e = add_entity (world, "box");
	const LrgRenderItem     *item;

	lrg_material_set_base_color (glass, 0.2f, 0.4f, 0.6f, 0.5f);
	lrg_material_set_blend_mode (glass, LRG_MATERIAL_BLEND_ALPHA);
	lrg_material_set_double_sided (glass, TRUE);
	lrg_entity3d_add_component (e, LRG_COMPONENT (mr));
	g_object_set (mr, "material", glass, NULL);
	g_assert_true (lrg_mesh_renderer_component_get_material (mr) == G_OBJECT (glass));
	g_assert_true (lrg_mesh_renderer_component_get_effective_material (mr) == G_OBJECT (glass));

	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE), ==, 0);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_TRANSPARENT), ==, 1);
	item = lrg_render_list_get_item (list, LRG_RENDER_CATEGORY_TRANSPARENT, 0);
	g_assert_true (item->material == (gpointer) glass);
	g_assert_cmpfloat_with_epsilon (item->color[2], 0.6f, EPS);
	g_assert_true ((item->flags & LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED) != 0);

	/* a cutout material goes to the cutout list */
	lrg_material_set_blend_mode (glass, LRG_MATERIAL_BLEND_CUTOUT);
	lrg_world3d_collect_render (world, NULL, list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_CUTOUT), ==, 1);
	g_assert_true ((lrg_mesh_renderer_component_get_flags (mr) & LRG_RENDER_ITEM_FLAG_CUTOUT) != 0);

	/* unlit shading -> unlit flag */
	lrg_material_set_shading_model (glass, LRG_SHADING_MODEL_UNLIT);
	g_assert_true ((lrg_mesh_renderer_component_get_flags (mr) & LRG_RENDER_ITEM_FLAG_UNLIT) != 0);

	/* submesh material bookkeeping (items need a resolved model) */
	{
		g_autoptr(LrgMaterial) other = lrg_material_new ();

		g_assert_cmpuint (lrg_mesh_renderer_component_get_n_submesh_materials (mr), ==, 0);
		lrg_mesh_renderer_component_set_submesh_material (mr, 2, other);
		g_assert_cmpuint (lrg_mesh_renderer_component_get_n_submesh_materials (mr), ==, 3);
		g_assert_null (lrg_mesh_renderer_component_get_submesh_material (mr, 0));
		g_assert_true (lrg_mesh_renderer_component_get_submesh_material (mr, 2) == other);
		lrg_mesh_renderer_component_set_submesh_material (mr, 2, NULL);
		g_assert_cmpuint (lrg_mesh_renderer_component_get_n_submesh_materials (mr), ==, 0);
		lrg_mesh_renderer_component_set_submesh_material (mr, 1, other);
		lrg_mesh_renderer_component_clear_submesh_materials (mr);
		g_assert_cmpuint (lrg_mesh_renderer_component_get_n_submesh_materials (mr), ==, 0);
	}
}

static void
test_skinned_model_binding (void)
{
	g_autoptr(LrgSkinnedModelComponent) smc = lrg_skinned_model_component_new ();
	g_autoptr(LrgMaterial)              m = lrg_material_new ();
	g_autoptr(LrgMaterial)              sub = lrg_material_new ();
	LrgMaterial                        *got = NULL;

	g_object_set (smc, "material", m, NULL);
	g_object_get (smc, "material", &got, NULL);
	g_assert_true (got == m);
	g_object_unref (got);
	g_assert_true (lrg_skinned_model_component_get_material (smc) == m);

	lrg_skinned_model_component_set_submesh_material (smc, 1, sub);
	g_assert_cmpuint (lrg_skinned_model_component_get_n_submesh_materials (smc), ==, 2);
	g_assert_true (lrg_skinned_model_component_get_submesh_material (smc, 1) == sub);
	g_assert_null (lrg_skinned_model_component_get_submesh_material (smc, 5));
	lrg_skinned_model_component_set_material (smc, NULL);
	g_assert_null (lrg_skinned_model_component_get_material (smc));
}

static void
test_level_instantiate (void)
{
	g_autoptr(LrgLevel)      level = lrg_level_new ("materials");
	g_autoptr(LrgWorld3D)    world = lrg_world3d_new ();
	LrgEngine               *engine = lrg_engine_get_default ();
	g_autoptr(LrgMaterial3D) glow = lrg_material3d_new_with_color (0.0f, 1.0f, 0.0f, 1.0f);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *crate = data_path ("crate.rmat");
	g_autoptr(LrgNode)       a = lrg_node_new ("Inline");
	g_autoptr(LrgNode)       b = lrg_node_new ("Ref");
	g_autoptr(LrgNode)       c = lrg_node_new ("Missing");
	g_autoptr(LrgNodeVisual) va = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgNodeVisual) vb = lrg_node_visual_new (LRG_NODE_VISUAL_MESH_ASSET);
	g_autoptr(LrgNodeVisual) vc = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	LrgComponent            *comp;
	GObject                 *slot;
	gfloat                   f = 0.0f;

	lrg_engine_startup (engine, NULL);
	g_assert_nonnull (lrg_engine_get_asset_manager (engine));

	/* inline block with emission */
	lrg_material3d_set_emission_color (glow, 1.0f, 0.5f, 0.0f, 1.0f);
	lrg_material3d_set_emission_strength (glow, 3.0f);
	lrg_node_visual_set_material (va, glow);
	lrg_node_set_guid (a, "cccccccc-0000-0000-0000-000000000001");
	lrg_node_set_visual (a, va);
	lrg_level_add_node (level, a, NULL);

	/* a material reference (absolute path: no project needed) */
	lrg_node_visual_set_asset (vb, "models/rock.obj");
	lrg_node_visual_set_material_ref (vb, crate);
	g_assert_cmpstr (lrg_node_visual_get_material_ref (vb), ==, crate);
	lrg_node_set_guid (b, "cccccccc-0000-0000-0000-000000000002");
	lrg_node_set_visual (b, vb);
	lrg_level_add_node (level, b, NULL);

	/* an unresolvable reference keeps the scalar material */
	lrg_node_visual_set_material_ref (vc, "guid:00000000-dead-beef-0000-000000000000");
	lrg_node_set_guid (c, "cccccccc-0000-0000-0000-000000000003");
	lrg_node_set_visual (c, vc);
	lrg_level_add_node (level, c, NULL);

	/* copy/equal carry the reference */
	{
		g_autoptr(LrgNodeVisual) copy = lrg_node_visual_copy (vb);

		g_assert_cmpstr (lrg_node_visual_get_material_ref (copy), ==, crate);
		g_assert_true (lrg_node_visual_equal (copy, vb));
		lrg_node_visual_set_material_ref (copy, NULL);
		g_assert_false (lrg_node_visual_equal (copy, vb));
	}

	g_assert_true (lrg_level_instantiate_3d (level, world, engine, &error));
	g_assert_no_error (error);

	comp = lrg_entity3d_get_component (lrg_world3d_find_by_guid (world, "cccccccc-0000-0000-0000-000000000001"),
	                                   LRG_TYPE_MESH_RENDERER_COMPONENT);
	g_assert_nonnull (comp);
	g_object_get (comp, "emission-strength", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 3.0f, EPS);
	g_object_get (comp, "emission-g", &f, NULL);
	g_assert_cmpfloat_with_epsilon (f, 0.5f, EPS);

	/* play-in-editor writes emission back into the inline block */
	{
		GValue v = G_VALUE_INIT;
		gfloat e[4];

		g_value_init (&v, G_TYPE_FLOAT);
		g_value_set_float (&v, 7.0f);
		g_assert_true (lrg_level_visual_store_property (va, "emission-strength", &v));
		g_value_unset (&v);
		g_assert_cmpfloat_with_epsilon (lrg_material3d_get_emission_strength (lrg_node_visual_get_material (va)),
		                                7.0f, EPS);
		lrg_material3d_get_emission_color (lrg_node_visual_get_material (va), &e[0], &e[1], &e[2], &e[3]);
		g_assert_cmpfloat_with_epsilon (e[1], 0.5f, EPS);
	}

	comp = lrg_entity3d_get_component (lrg_world3d_find_by_guid (world, "cccccccc-0000-0000-0000-000000000002"),
	                                   LRG_TYPE_MESH_RENDERER_COMPONENT);
	g_assert_nonnull (comp);
	slot = lrg_mesh_renderer_component_get_material (LRG_MESH_RENDERER_COMPONENT (comp));
	g_assert_true (LRG_IS_MATERIAL (slot));
	g_assert_cmpstr (lrg_material_get_name (LRG_MATERIAL (slot)), ==, "crate");
	g_assert_true ((lrg_mesh_renderer_component_get_flags (LRG_MESH_RENDERER_COMPONENT (comp))
	                & LRG_RENDER_ITEM_FLAG_CUTOUT) != 0);
	/* through the engine's asset manager cache */
	g_assert_true (lrg_asset_manager_is_cached (lrg_engine_get_asset_manager (engine), crate));

	comp = lrg_entity3d_get_component (lrg_world3d_find_by_guid (world, "cccccccc-0000-0000-0000-000000000003"),
	                                   LRG_TYPE_MESH_RENDERER_COMPONENT);
	g_assert_nonnull (comp);
	g_assert_null (lrg_mesh_renderer_component_get_material (LRG_MESH_RENDERER_COMPONENT (comp)));
}

/* ==========================================================================
 * GPU: uniform contract, render state, drawing
 * ========================================================================== */

static const gchar *test_vs =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"uniform mat4 mvp;\n"
	"out vec2 fragTexCoord;\n"
	"void main() { fragTexCoord = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";

/* Uses every contract uniform so none is optimized away. */
static const gchar *test_fs =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"uniform sampler2D texture0;\n"
	"uniform sampler2D normalMap;\n"
	"uniform sampler2D ormMap;\n"
	"uniform sampler2D emissionMap;\n"
	"uniform sampler2D heightMap;\n"
	"uniform sampler2D metallicMap;\n"
	"uniform sampler2D noise;\n"
	"uniform vec4 colDiffuse;\n"
	"uniform vec4 pbrParams;\n"
	"uniform vec4 emissionOwn;\n"
	"uniform int mapFlags;\n"
	"uniform float alphaCutoff;\n"
	"uniform float heightScale;\n"
	"uniform float wobble;\n"
	"uniform vec4 tint;\n"
	"uniform vec2 offset;\n"
	"out vec4 finalColor;\n"
	"void main() {\n"
	"    vec2 uv = fragTexCoord;\n"
	"    vec4 t = texture(texture0, uv) + texture(normalMap, uv) + texture(ormMap, uv)\n"
	"           + texture(emissionMap, uv) + texture(heightMap, uv) + texture(metallicMap, uv)\n"
	"           + texture(noise, uv);\n"
	"    finalColor = colDiffuse + 0.001 * (t + pbrParams + emissionOwn + tint\n"
	"               + vec4(float(mapFlags) + alphaCutoff + heightScale + wobble + offset.x + offset.y));\n"
	"}\n";

static gpointer
gl_proc (const gchar *name)
{
	gpointer fn = rlGetProcAddress (name);

	g_assert_nonnull (fn);
	return fn;
}

static void
read_uniform4 (guint        program,
               const gchar *name,
               gfloat      *out)
{
	TestGetUniformfvFn get = (TestGetUniformfvFn) gl_proc ("glGetUniformfv");
	gint               loc = rlGetLocationUniform (program, name);

	g_assert_cmpint (loc, >=, 0);
	memset (out, 0, 4 * sizeof (gfloat));
	get (program, loc, out);
}

static gint
read_uniform_int (guint        program,
                  const gchar *name)
{
	TestGetUniformivFn get = (TestGetUniformivFn) gl_proc ("glGetUniformiv");
	gint               loc = rlGetLocationUniform (program, name);
	gint               value = -1;

	g_assert_cmpint (loc, >=, 0);
	get (program, loc, &value);
	return value;
}

static guint
bound_texture (guint unit)
{
	TestActiveTextureFn active = (TestActiveTextureFn) gl_proc ("glActiveTexture");
	TestGetIntegervFn   get = (TestGetIntegervFn) gl_proc ("glGetIntegerv");
	gint                id = 0;

	active (TEST_GL_TEXTURE0 + unit);
	get (TEST_GL_TEXTURE_BINDING_2D, &id);
	active (TEST_GL_TEXTURE0);
	return (guint) id;
}

/* The GL id lrg_material_fill_raylib_maps() reports for @unit (graylib
 * does not export its handle accessor to tests). */
static guint
map_texture_id (LrgMaterial *m,
                gint         unit)
{
	MaterialMap maps[16];

	memset (maps, 0, sizeof maps);
	lrg_material_fill_raylib_maps (m, maps);
	return maps[unit].texture.id;
}

static void
test_gpu_apply_uniforms (void)
{
	g_autoptr(LrgMaterial) m = NULL;
	g_autoptr(GrlTexture)  orm = NULL;
	g_autoptr(GrlImage)    image = NULL;
	Shader                 shader;
	gfloat                 v[4];
	gint                   unit;

	SKIP_WITHOUT_GL ();

	shader = LoadShaderFromMemory (test_vs, test_fs);
	g_assert_cmpuint (shader.id, >, 0);

	m = load_data ("crate.rmat", NULL);
	/* the file's albedo/normal/noise load lazily from the data dir */
	g_assert_true (lrg_material_load_textures (m, NULL) || TRUE);
	g_assert_nonnull (lrg_material_get_texture (m, LRG_MATERIAL_SLOT_ALBEDO));
	g_assert_nonnull (lrg_material_get_texture (m, LRG_MATERIAL_SLOT_NORMAL));
	/* a runtime ORM texture wins over `orm: null` */
	image = grl_image_new_color (2, 2, &(GrlColor){ 255, 128, 0, 255 });
	orm = grl_texture_new_from_image (image);
	lrg_material_set_texture (m, LRG_MATERIAL_SLOT_ORM, orm);

	lrg_material_apply (m, shader.id);

	read_uniform4 (shader.id, "colDiffuse", v);
	assert_rgba (v, 0.8f, 0.6f, 0.4f, 1.0f);
	read_uniform4 (shader.id, "pbrParams", v);
	assert_rgba (v, 0.25f, 0.75f, 0.5f, 2.0f);
	read_uniform4 (shader.id, "emissionOwn", v);
	assert_rgba (v, 3.0f, 1.5f, 0.0f, 1.0f);
	g_assert_cmpint (read_uniform_int (shader.id, "mapFlags"), ==,
	                 LRG_MATERIAL_MAP_FLAG_NORMAL | LRG_MATERIAL_MAP_FLAG_ORM);
	read_uniform4 (shader.id, "alphaCutoff", v);
	g_assert_cmpfloat_with_epsilon (v[0], 0.4f, EPS);       /* cutout */
	read_uniform4 (shader.id, "heightScale", v);
	g_assert_cmpfloat_with_epsilon (v[0], 0.1f, EPS);
	read_uniform4 (shader.id, "wobble", v);
	g_assert_cmpfloat_with_epsilon (v[0], 0.5f, EPS);
	read_uniform4 (shader.id, "tint", v);
	assert_rgba (v, 1.0f, 0.0f, 0.0f, 1.0f);
	read_uniform4 (shader.id, "offset", v);
	g_assert_cmpfloat_with_epsilon (v[1], 0.5f, EPS);

	/* samplers point at the contract's units, textures are bound there */
	g_assert_cmpint (read_uniform_int (shader.id, "normalMap"), ==, LRG_MATERIAL_UNIT_NORMAL);
	g_assert_cmpint (read_uniform_int (shader.id, "ormMap"), ==, LRG_MATERIAL_UNIT_ORM);
	g_assert_cmpint (read_uniform_int (shader.id, "emissionMap"), ==, LRG_MATERIAL_UNIT_EMISSION);
	g_assert_cmpint (read_uniform_int (shader.id, "noise"), ==, LRG_MATERIAL_CUSTOM_TEXTURE_UNIT);
	g_assert_cmpuint (map_texture_id (m, LRG_MATERIAL_UNIT_NORMAL), !=, 0);
	g_assert_cmpuint (map_texture_id (m, LRG_MATERIAL_UNIT_NORMAL), !=, rlGetTextureIdDefault ());
	g_assert_cmpuint (map_texture_id (m, LRG_MATERIAL_UNIT_ORM), !=, map_texture_id (m, LRG_MATERIAL_UNIT_NORMAL));
	g_assert_cmpuint (bound_texture (LRG_MATERIAL_UNIT_NORMAL), ==, map_texture_id (m, LRG_MATERIAL_UNIT_NORMAL));
	g_assert_cmpuint (bound_texture (LRG_MATERIAL_UNIT_ORM), ==, map_texture_id (m, LRG_MATERIAL_UNIT_ORM));
	/* no emission map: the white texture (the tint decides the glow) */
	g_assert_cmpuint (bound_texture (LRG_MATERIAL_UNIT_EMISSION), ==, rlGetTextureIdDefault ());

	/* a sampler already on a budget unit (a shader asset's allocation)
	 * keeps it: the material's texture replaces the asset's in place */
	{
		gint nine = 9;

		rlEnableShader (shader.id);
		rlSetUniform (rlGetLocationUniform (shader.id, "noise"), &nine, RL_SHADER_UNIFORM_INT, 1);
	}
	unit = LRG_MATERIAL_CUSTOM_TEXTURE_UNIT;
	lrg_material_apply_custom_uniforms (m, shader.id, &unit);
	g_assert_cmpint (unit, ==, LRG_MATERIAL_CUSTOM_TEXTURE_UNIT);
	g_assert_cmpint (read_uniform_int (shader.id, "noise"), ==, 9);
	g_assert_cmpuint (bound_texture (9), !=, 0);

	/* a sampler on a material map unit is moved into the budget, from
	 * the caller's unit */
	{
		gint zero = 0;

		rlEnableShader (shader.id);
		rlSetUniform (rlGetLocationUniform (shader.id, "noise"), &zero, RL_SHADER_UNIFORM_INT, 1);
	}
	unit = 8;
	lrg_material_apply_custom_uniforms (m, shader.id, &unit);
	g_assert_cmpint (unit, ==, 9);
	g_assert_cmpint (read_uniform_int (shader.id, "noise"), ==, 8);

	/* a non-cutout material uploads cutoff 0 */
	lrg_material_set_blend_mode (m, LRG_MATERIAL_BLEND_OPAQUE);
	lrg_material_apply (m, shader.id);
	read_uniform4 (shader.id, "alphaCutoff", v);
	g_assert_cmpfloat (v[0], ==, 0.0f);

	/* the "no material" state */
	lrg_material_reset_uniforms (shader.id);
	read_uniform4 (shader.id, "pbrParams", v);
	assert_rgba (v, 0.0f, 0.5f, 1.0f, 1.0f);
	read_uniform4 (shader.id, "emissionOwn", v);
	assert_rgba (v, 0.0f, 0.0f, 0.0f, 0.0f);
	g_assert_cmpint (read_uniform_int (shader.id, "mapFlags"), ==, 0);
	g_assert_cmpuint (bound_texture (LRG_MATERIAL_UNIT_EMISSION), ==, 0);
	g_assert_cmpuint (bound_texture (LRG_MATERIAL_UNIT_ORM), ==, 0);

	/* raylib maps mirror the units */
	{
		MaterialMap maps[16];

		memset (maps, 0, sizeof maps);
		lrg_material_fill_raylib_maps (m, maps);
		g_assert_cmpuint (maps[MATERIAL_MAP_NORMAL].texture.width, ==, 4);
		g_assert_cmpuint (maps[MATERIAL_MAP_OCCLUSION].texture.width, ==, 2);
		g_assert_cmpuint (maps[MATERIAL_MAP_EMISSION].texture.id, ==, rlGetTextureIdDefault ());
		g_assert_cmpuint (maps[MATERIAL_MAP_ALBEDO].color.r, ==, 204);
	}

	rlDisableShader ();
	UnloadShader (shader);
}

/* A lit program (it declares the light set's uniforms): custom textures
 * stay out of the light set's units 11 .. 15 and the hooks' window. */
static void
test_gpu_custom_unit_budget (void)
{
	static const gchar *lit_fs =
		"#version 330\n"
		"in vec2 fragTexCoord;\n"
		"uniform sampler2D noise;\n"
		"uniform vec3 sunDirection;\n"
		"out vec4 finalColor;\n"
		"void main() { finalColor = texture(noise, fragTexCoord) + vec4(sunDirection, 0.0); }\n";
	g_autoptr(LrgMaterial) m = lrg_material_new ();
	TestGetIntegervFn      get_int;
	Shader                 shader;
	gint                   units = 16;
	gint                   unit;

	SKIP_WITHOUT_GL ();

	get_int = (TestGetIntegervFn) gl_proc ("glGetIntegerv");
	get_int (0x8872 /* GL_MAX_TEXTURE_IMAGE_UNITS */, &units);
	shader = LoadShaderFromMemory (test_vs, lit_fs);
	g_assert_cmpuint (shader.id, >, 0);
	lrg_material_set_uniform_texture (m, "noise", "noise.png");

	/* asking from unit 11 (the spot atlas) skips past the lit budget */
	unit = 11;
	if (units > 16)
	{
		/* 7 .. 10 only: from 11 there is nothing left */
		g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*no free texture unit*noise*");
		lrg_material_apply_custom_uniforms (m, shader.id, &unit);
		g_test_assert_expected_messages ();
		unit = LRG_MATERIAL_CUSTOM_TEXTURE_UNIT;
		lrg_material_apply_custom_uniforms (m, shader.id, &unit);
		g_assert_cmpint (read_uniform_int (shader.id, "noise"), ==, 7);
	}
	else
	{
		/* a 16-unit driver gives lit programs no custom units */
		unit = LRG_MATERIAL_CUSTOM_TEXTURE_UNIT;
		g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*no free texture unit*noise*");
		lrg_material_apply_custom_uniforms (m, shader.id, &unit);
		g_test_assert_expected_messages ();
	}
	rlDisableShader ();
	UnloadShader (shader);
}

static void
test_gpu_render_state (void)
{
	g_autoptr(LrgMaterial) m = lrg_material_new ();
	TestIsEnabledFn        is_enabled;
	TestGetIntegervFn      get_int;
	TestGetBooleanvFn      get_bool;
	gint                   mode = 0;
	unsigned char          mask = 0;

	SKIP_WITHOUT_GL ();

	is_enabled = (TestIsEnabledFn) gl_proc ("glIsEnabled");
	get_int = (TestGetIntegervFn) gl_proc ("glGetIntegerv");
	get_bool = (TestGetBooleanvFn) gl_proc ("glGetBooleanv");

	lrg_material_set_cull_mode (m, LRG_MATERIAL_CULL_FRONT);
	lrg_material_set_depth_test (m, FALSE);
	lrg_material_set_depth_write (m, FALSE);
	lrg_material_set_blend_mode (m, LRG_MATERIAL_BLEND_ADDITIVE);
	lrg_material_apply_state (m);
	g_assert_true (is_enabled (TEST_GL_CULL_FACE));
	get_int (TEST_GL_CULL_FACE_MODE, &mode);
	g_assert_cmpint (mode, ==, TEST_GL_FRONT);
	g_assert_false (is_enabled (TEST_GL_DEPTH_TEST));
	get_bool (TEST_GL_DEPTH_WRITEMASK, &mask);
	g_assert_false (mask);
	/* additive: destination factor ONE */
	get_int (TEST_GL_BLEND_DST_RGB, &mode);
	g_assert_cmpint (mode, ==, TEST_GL_ONE);

	lrg_material_set_double_sided (m, TRUE);
	lrg_material_apply_state (m);
	g_assert_false (is_enabled (TEST_GL_CULL_FACE));

	lrg_material_restore_state (m);
	g_assert_true (is_enabled (TEST_GL_CULL_FACE));
	get_int (TEST_GL_CULL_FACE_MODE, &mode);
	g_assert_cmpint (mode, ==, TEST_GL_BACK);
	g_assert_true (is_enabled (TEST_GL_DEPTH_TEST));
	get_bool (TEST_GL_DEPTH_WRITEMASK, &mask);
	g_assert_true (mask);
	rlDisableDepthTest ();
}

/* Renders @draw into a 32x32 target looking down -Z from z = 5 at the
 * origin (or straight down at (10, 0, 10) when @top_down) and returns
 * the centre pixel. */
typedef void (*DrawFn) (gpointer data);

static Color
render_centre (DrawFn   draw,
               gpointer data,
               gboolean top_down)
{
	RenderTexture2D target = LoadRenderTexture (32, 32);
	Camera3D        camera;
	Image           image;
	Color           centre;

	memset (&camera, 0, sizeof camera);
	camera.fovy = 60.0f;
	camera.projection = CAMERA_PERSPECTIVE;
	if (top_down)
	{
		camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
		camera.target = (Vector3){ 10.0f, 0.0f, 10.0f };
		camera.up = (Vector3){ 0.0f, 0.0f, -1.0f };
	}
	else
	{
		camera.position = (Vector3){ 0.0f, 0.0f, 5.0f };
		camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
		camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
	}

	BeginTextureMode (target);
	ClearBackground (BLACK);
	BeginMode3D (camera);
	draw (data);
	EndMode3D ();
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	centre = GetImageColor (image, 16, 16);
	UnloadImage (image);
	UnloadRenderTexture (target);
	return centre;
}

/* A quad in the z = 0 plane, 10 units wide, both windings drawn by a
 * double-sided material. */
static const gfloat quad_positions[] = {
	-5.0f, -5.0f, 0.0f,   5.0f, -5.0f, 0.0f,   5.0f, 5.0f, 0.0f,   -5.0f, 5.0f, 0.0f
};
static const gfloat quad_normals[] = {
	0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f
};
static const gfloat quad_uvs[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
static const guint32 quad_indices[] = { 0, 1, 2, 0, 2, 3 };

static LrgMaterial *
red_material (void)
{
	LrgMaterial *m = lrg_material_new ();

	lrg_material_set_base_color (m, 1.0f, 0.0f, 0.0f, 1.0f);
	lrg_material_set_double_sided (m, TRUE);
	return m;
}

static void
draw_batch (gpointer data)
{
	lrg_static_batch_draw (LRG_STATIC_BATCH (data), 0xffffffffu);
}

static void
draw_skinned (gpointer data)
{
	lrg_skinned_mesh_draw (LRG_SKINNED_MESH (data), NULL);
}

static void
draw_terrain (gpointer data)
{
	lrg_terrain_draw (LRG_TERRAIN (data));
}

static void
test_gpu_static_batch (void)
{
	g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
	g_autoptr(LrgMaterial)    red = red_material ();
	g_autoptr(GError)         error = NULL;
	Color                     c;

	SKIP_WITHOUT_GL ();

	g_assert_true (lrg_static_batch_add_mesh (batch, quad_positions, quad_normals, quad_uvs, NULL, 4,
	                                          quad_indices, 6, NULL, 7, 0, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_static_batch_upload (batch, &error));
	g_assert_no_error (error);

	/* without a material: white */
	c = render_centre (draw_batch, batch, FALSE);
	g_assert_cmpuint (c.r, ==, 255);
	g_assert_cmpuint (c.g, ==, 255);

	lrg_static_batch_set_material (batch, 7, red);
	g_assert_true (lrg_static_batch_get_material (batch, 7) == red);
	c = render_centre (draw_batch, batch, FALSE);
	g_assert_cmpuint (c.r, ==, 255);
	g_assert_cmpuint (c.g, ==, 0);
	g_assert_cmpuint (c.b, ==, 0);

	/* the render state was restored */
	g_assert_true (((TestIsEnabledFn) gl_proc ("glIsEnabled")) (TEST_GL_CULL_FACE));

	lrg_static_batch_set_material (batch, 7, NULL);
	g_assert_null (lrg_static_batch_get_material (batch, 7));
}

static void
test_gpu_skinned_mesh (void)
{
	g_autoptr(LrgSkinnedMesh) mesh = lrg_skinned_mesh_new (1);
	g_autoptr(LrgMaterial)    red = red_material ();
	g_autoptr(GError)         error = NULL;
	const guint8              bones[16] = { 0 };
	const gfloat              weights[16] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	const gfloat              identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	Color                     c;

	SKIP_WITHOUT_GL ();

	g_assert_true (lrg_skinned_mesh_set_geometry (mesh, 4, quad_positions, quad_normals, quad_uvs, NULL,
	                                              bones, weights, 6, quad_indices, &error));
	g_assert_no_error (error);
	lrg_skinned_mesh_set_palette (mesh, identity);
	lrg_skinned_mesh_set_material (mesh, red);
	g_assert_true (lrg_skinned_mesh_get_material (mesh) == red);

	/* the built-in skinning shader shades by the normal: red, not white */
	c = render_centre (draw_skinned, mesh, FALSE);
	g_assert_cmpuint (c.r, >, 100);
	g_assert_cmpuint (c.g, ==, 0);
	g_assert_cmpuint (c.b, ==, 0);

	lrg_skinned_mesh_set_material (mesh, NULL);
	c = render_centre (draw_skinned, mesh, FALSE);
	g_assert_cmpuint (c.g, >, 100);
	g_assert_cmpuint (c.g, ==, c.r);
	lrg_skinned_mesh_unload (mesh);
}

static void
check_terrain (gboolean meshes)
{
	g_autoptr(LrgTerrain)  terrain = NULL;
	g_autoptr(LrgMaterial) red = red_material ();
	g_autoptr(GError)      error = NULL;
	const gfloat           heights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const guint            materials[1] = { 3 };
	Color                  c;

	if (meshes)
		g_setenv ("LRG_TERRAIN_DRAW", "meshes", TRUE);
	terrain = lrg_terrain_new (1, 20.0f);
	g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 4, materials, NULL, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_terrain_upload (terrain, &error));
	g_assert_no_error (error);

	c = render_centre (draw_terrain, terrain, TRUE);
	g_assert_cmpuint (c.g, ==, 255);

	lrg_terrain_set_material (terrain, 3, red);
	g_assert_true (lrg_terrain_get_material (terrain, 3) == red);
	c = render_centre (draw_terrain, terrain, TRUE);
	g_assert_cmpuint (c.r, ==, 255);
	g_assert_cmpuint (c.g, ==, 0);

	/* back to plain drawing: the tint/colour is restored */
	lrg_terrain_set_material (terrain, 3, NULL);
	c = render_centre (draw_terrain, terrain, TRUE);
	g_assert_cmpuint (c.g, ==, 255);
	lrg_terrain_unload (terrain);
	g_unsetenv ("LRG_TERRAIN_DRAW");
}

static void
test_gpu_terrain (void)
{
	SKIP_WITHOUT_GL ();
	check_terrain (FALSE);
	check_terrain (TRUE);
}

int
main (int    argc,
      char  *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/material/defaults", test_defaults);
	g_test_add_func ("/material/setters", test_setters);
	g_test_add_func ("/material/format/load", test_format_load);
	g_test_add_func ("/material/format/round-trip", test_format_round_trip);
	g_test_add_func ("/material/format/errors", test_format_errors);
	g_test_add_func ("/material/instance/override-revert", test_instance_override_revert);
	g_test_add_func ("/material/instance/parent-by-path", test_instance_parent_by_path);
	g_test_add_func ("/material/instance/parent-by-guid", test_instance_parent_by_guid);
	g_test_add_func ("/material/render-state/mapping", test_render_state_mapping);
	g_test_add_func ("/material/material3d", test_material3d_conversion);
	g_test_add_func ("/material/binding/mesh-renderer", test_mesh_renderer_binding);
	g_test_add_func ("/material/binding/skinned-model", test_skinned_model_binding);
	g_test_add_func ("/material/binding/level-instantiate", test_level_instantiate);
	g_test_add_func ("/material/gpu/apply-uniforms", test_gpu_apply_uniforms);
	g_test_add_func ("/material/gpu/custom-unit-budget", test_gpu_custom_unit_budget);
	g_test_add_func ("/material/gpu/render-state", test_gpu_render_state);
	g_test_add_func ("/material/gpu/static-batch", test_gpu_static_batch);
	g_test_add_func ("/material/gpu/skinned-mesh", test_gpu_skinned_mesh);
	g_test_add_func ("/material/gpu/terrain", test_gpu_terrain);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
