/* test-post-stack.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the post-processing pipeline and the `.rpost` post stack.
 *
 * Headless: the effect type registry; loading (order, parameters, vector
 * expansion, unknown keys kept), errors (unknown effect, unknown parameter,
 * bad value, newer version, not a stack); the text and file round trip;
 * building into an LrgPostProcessor (order, values, reuse, removal,
 * defaults restored); blending; capturing a processor; the tonemap curves
 * on the CPU.
 *
 * GPU (only under the isolated X display, tests/xrun.sh style: DISPLAY set
 * and no WAYLAND_DISPLAY): capture then render passes the scene through
 * untouched with no effect; a vignette darkens the corners and keeps the
 * centre; the tonemap result matches lrg_tonemap_apply_rgb(); bloom
 * spreads light around a bright square; every built-in effect runs in one
 * chain; HDR targets; process() on a foreign texture; auto-resize.
 *
 * LRG_POST_BENCH=1 also prints the cost of each effect at 1920x1080.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define GPU_W (64)
#define GPU_H (64)

static gboolean gpu_ready = FALSE;

static const gchar sample_rpost[] =
	"version: 1\n"
	"name: Dusk\n"
	"quality: medium\n"
	"hdr: true\n"
	"author: someone\n"                      /* unknown: kept */
	"effects:\n"
	"  - type: bloom\n"
	"    weight: 0.8\n"
	"    params:\n"
	"      threshold: 0.9\n"
	"      soft_knee: 0.25\n"               /* snake case accepted */
	"      tint: [1.0, 0.5, 0.25]\n"
	"  - type: tonemap\n"
	"    params:\n"
	"      operator: reinhard\n"
	"      exposure: 0.5\n"
	"  - type: vignette\n"
	"    enabled: false\n"
	"    priority: 500\n"
	"    editor_note: keep me\n"            /* unknown: kept */
	"    params:\n"
	"      intensity: 0.4\n"
	"      color: [0.1, 0.0, 0.2]\n";

static LrgPostStack *
load_sample (void)
{
	g_autoptr(GError) error = NULL;
	LrgPostStack     *stack = lrg_post_stack_new_from_data (sample_rpost, -1, NULL, &error);

	g_assert_no_error (error);
	g_assert_nonnull (stack);
	return stack;
}

static gfloat
param_float (LrgPostStack *stack,
             guint         index,
             const gchar  *name)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_assert_true (lrg_post_stack_get_param (stack, index, name, &v));
	if (G_VALUE_HOLDS_FLOAT (&v))
		return g_value_get_float (&v);
	if (G_VALUE_HOLDS_DOUBLE (&v))
		return (gfloat) g_value_get_double (&v);
	g_assert_not_reached ();
	return 0.0f;
}

/* ==========================================================================
 * Headless
 * ========================================================================== */

/* A custom effect type for the registry test */
#define TEST_TYPE_SEPIA (test_sepia_get_type ())
G_DECLARE_FINAL_TYPE (TestSepia, test_sepia, TEST, SEPIA, LrgPostEffect)
struct _TestSepia
{
	LrgPostEffect parent_instance;
	gfloat        amount;
};
G_DEFINE_FINAL_TYPE (TestSepia, test_sepia, LRG_TYPE_POST_EFFECT)

static void
test_sepia_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	(void) prop_id;
	(void) pspec;
	g_value_set_float (value, TEST_SEPIA (object)->amount);
}

static void
test_sepia_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	(void) prop_id;
	(void) pspec;
	TEST_SEPIA (object)->amount = g_value_get_float (value);
}

static void
test_sepia_class_init (TestSepiaClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);

	oc->get_property = test_sepia_get_property;
	oc->set_property = test_sepia_set_property;
	g_object_class_install_property (oc, 1,
		g_param_spec_float ("amount", NULL, NULL, 0.0f, 1.0f, 0.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
test_sepia_init (TestSepia *self)
{
	self->amount = 0.5f;
}

static void
test_registry (void)
{
	g_auto(GStrv) names = NULL;

	g_assert_true (lrg_post_stack_lookup_effect_type ("bloom") == LRG_TYPE_BLOOM);
	g_assert_true (lrg_post_stack_lookup_effect_type ("tonemap") == LRG_TYPE_TONEMAP);
	g_assert_true (lrg_post_stack_lookup_effect_type ("color-grade") == LRG_TYPE_COLOR_GRADE);
	g_assert_true (lrg_post_stack_lookup_effect_type ("LrgVignette") == LRG_TYPE_VIGNETTE);
	g_assert_true (lrg_post_stack_lookup_effect_type ("no-such-effect") == G_TYPE_INVALID);
	g_assert_true (lrg_post_stack_lookup_effect_type ("GObject") == G_TYPE_INVALID);

	lrg_post_stack_register_effect_type ("sepia", TEST_TYPE_SEPIA);
	g_assert_true (lrg_post_stack_lookup_effect_type ("sepia") == TEST_TYPE_SEPIA);
	names = lrg_post_stack_list_effect_types ();
	g_assert_true (g_strv_contains ((const gchar * const *) names, "sepia"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "heat-haze"));
	g_assert_cmpstr (names[0], ==, "bloom");   /* sorted */
}

static void
test_load (void)
{
	g_autoptr(LrgPostStack) stack = load_sample ();
	g_auto(GValue)          v = G_VALUE_INIT;
	g_auto(GStrv)           names = NULL;

	g_assert_cmpstr (lrg_post_stack_get_name (stack), ==, "Dusk");
	g_assert_cmpint (lrg_post_stack_get_quality (stack), ==, LRG_POST_QUALITY_MEDIUM);
	g_assert_true (lrg_post_stack_get_hdr (stack));
	g_assert_cmpuint (lrg_post_stack_get_effect_count (stack), ==, 3);

	/* order as written */
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 0), ==, "bloom");
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 1), ==, "tonemap");
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 2), ==, "vignette");
	g_assert_cmpint (lrg_post_stack_find_effect (stack, "vignette"), ==, 2);
	g_assert_cmpint (lrg_post_stack_find_effect (stack, "fxaa"), ==, -1);

	g_assert_cmpfloat_with_epsilon (lrg_post_stack_get_effect_weight (stack, 0), 0.8f, 1e-6f);
	g_assert_true (lrg_post_stack_get_effect_enabled (stack, 0));
	g_assert_false (lrg_post_stack_get_effect_enabled (stack, 2));

	/* typed parameters, snake case read as kebab, vectors expanded */
	g_assert_cmpfloat_with_epsilon (param_float (stack, 0, "threshold"), 0.9f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (param_float (stack, 0, "soft-knee"), 0.25f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (param_float (stack, 0, "tint-g"), 0.5f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (param_float (stack, 2, "color-b"), 0.2f, 1e-6f);
	g_assert_true (lrg_post_stack_get_param (stack, 1, "operator", &v));
	g_assert_true (G_VALUE_HOLDS (&v, LRG_TYPE_TONEMAP_OPERATOR));
	g_assert_cmpint (g_value_get_enum (&v), ==, LRG_TONEMAP_OPERATOR_REINHARD);

	names = lrg_post_stack_get_param_names (stack, 0);
	g_assert_cmpuint (g_strv_length (names), ==, 5);
	g_assert_cmpstr (names[0], ==, "threshold");
	g_assert_cmpstr (names[2], ==, "tint-r");
}

static void
test_round_trip (void)
{
	g_autoptr(LrgPostStack) stack = load_sample ();
	g_autoptr(LrgPostStack) again = NULL;
	g_autoptr(LrgPostStack) from_file = NULL;
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *text = NULL;
	g_autofree gchar       *text2 = NULL;
	g_autofree gchar       *text3 = NULL;
	g_autofree gchar       *dir = NULL;
	g_autofree gchar       *path = NULL;

	text = lrg_post_stack_to_data (stack, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (text, "author: someone"));
	g_assert_nonnull (strstr (text, "editor_note: keep me"));
	g_assert_nonnull (strstr (text, "operator: reinhard"));
	g_assert_nonnull (strstr (text, "quality: medium"));

	again = lrg_post_stack_new_from_data (text, -1, NULL, &error);
	g_assert_no_error (error);
	text2 = lrg_post_stack_to_data (again, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, text2);
	g_assert_cmpint (lrg_post_stack_get_effect_count (again), ==, 3);
	g_assert_cmpfloat_with_epsilon (param_float (again, 0, "tint-b"), 0.25f, 1e-6f);

	/* through a file */
	dir = g_dir_make_tmp ("lrg-post-stack-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "dusk.rpost", NULL);
	g_assert_true (lrg_post_stack_save_to_file (stack, path, &error));
	g_assert_no_error (error);
	from_file = lrg_post_stack_new_from_file (path, &error);
	g_assert_no_error (error);
	text3 = lrg_post_stack_to_data (from_file, &error);
	g_assert_cmpstr (text, ==, text3);
	g_unlink (path);
	g_rmdir (dir);
}

static void
expect_error (const gchar *yaml,
              gint         code)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgPostStack) stack = lrg_post_stack_new_from_data (yaml, -1, NULL, &error);

	g_assert_null (stack);
	g_assert_error (error, LRG_POST_STACK_ERROR, code);
}

static void
test_errors (void)
{
	g_autoptr(LrgPostStack) stack = lrg_post_stack_new ();
	g_autoptr(GError)       error = NULL;
	g_auto(GValue)          v = G_VALUE_INIT;

	expect_error ("effects:\n  - type: lens-flare\n", LRG_POST_STACK_ERROR_UNKNOWN_EFFECT);
	expect_error ("effects:\n  - type: bloom\n    params:\n      radius: 3\n",
	              LRG_POST_STACK_ERROR_UNKNOWN_PARAM);
	expect_error ("effects:\n  - type: bloom\n    params:\n      threshold: bright\n",
	              LRG_POST_STACK_ERROR_INVALID_VALUE);
	expect_error ("effects:\n  - type: tonemap\n    params:\n      operator: hable2\n",
	              LRG_POST_STACK_ERROR_INVALID_VALUE);
	expect_error ("effects:\n  - type: bloom\n    params:\n      tint: [1, 2]\n",
	              LRG_POST_STACK_ERROR_INVALID_VALUE);
	expect_error ("effects:\n  - enabled: true\n", LRG_POST_STACK_ERROR_PARSE);
	expect_error ("version: 99\neffects: []\n", LRG_POST_STACK_ERROR_PARSE);
	expect_error ("name: no effects\n", LRG_POST_STACK_ERROR_PARSE);
	expect_error ("effects:\n  - type: palettize\n    params:\n      palette: ['#12345g']\n",
	              LRG_POST_STACK_ERROR_INVALID_VALUE);

	/* the API checks the same way */
	g_assert_cmpint (lrg_post_stack_add_effect (stack, "nope", &error), ==, -1);
	g_assert_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_EFFECT);
	g_clear_error (&error);
	g_assert_cmpint (lrg_post_stack_add_effect (stack, "vignette", &error), ==, 0);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "0.75");
	g_assert_true (lrg_post_stack_set_param (stack, 0, "radius", &v, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (param_float (stack, 0, "radius"), 0.75f, 1e-6f);
	g_assert_false (lrg_post_stack_set_param (stack, 0, "wobble", &v, &error));
	g_assert_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_PARAM);
	g_clear_error (&error);

	/* an empty file-level stack is fine */
	g_clear_object (&stack);
	stack = lrg_post_stack_new_from_data ("effects: []\n", -1, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_post_stack_get_effect_count (stack), ==, 0);
}

static void
test_build (void)
{
	g_autoptr(LrgPostStack)     stack = load_sample ();
	g_autoptr(LrgPostStack)     smaller = NULL;
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (320, 200);
	g_autoptr(GError)           error = NULL;
	LrgPostEffect              *bloom, *vignette, *tonemap;
	GList                      *effects;
	gfloat                      r, g, b;

	g_assert_true (lrg_post_stack_build (stack, proc, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_post_processor_get_effect_count (proc), ==, 3);
	g_assert_cmpint (lrg_post_processor_get_quality (proc), ==, LRG_POST_QUALITY_MEDIUM);

	/* chain order: bloom (0), tonemap (10), vignette (explicit 500) */
	effects = lrg_post_processor_get_effects (proc);
	g_assert_true (LRG_IS_BLOOM (effects->data));
	g_assert_true (LRG_IS_TONEMAP (effects->next->data));
	g_assert_true (LRG_IS_VIGNETTE (effects->next->next->data));
	bloom = lrg_post_processor_get_effect (proc, "bloom");
	tonemap = lrg_post_processor_get_effect (proc, "tonemap");
	vignette = lrg_post_processor_get_effect (proc, "vignette");
	g_assert_cmpint (lrg_post_effect_get_priority (vignette), ==, 500);
	g_assert_cmpint (lrg_post_effect_get_quality_tier (bloom), ==, LRG_POST_QUALITY_MEDIUM);

	/* values */
	g_assert_cmpfloat_with_epsilon (lrg_bloom_get_threshold (LRG_BLOOM (bloom)), 0.9f, 1e-6f);
	g_assert_cmpfloat_with_epsilon (lrg_post_effect_get_intensity (bloom), 0.8f, 1e-6f);
	lrg_bloom_get_tint (LRG_BLOOM (bloom), &r, &g, &b);
	g_assert_cmpfloat_with_epsilon (g, 0.5f, 1e-6f);
	g_assert_cmpint (lrg_tonemap_get_operator (LRG_TONEMAP (tonemap)), ==, LRG_TONEMAP_OPERATOR_REINHARD);
	g_assert_false (lrg_post_effect_is_enabled (vignette));
	g_assert_cmpfloat_with_epsilon (lrg_vignette_get_intensity (LRG_VIGNETTE (vignette)), 0.4f, 1e-6f);

	/* touched by hand, then rebuilt: the same objects, values restored */
	lrg_bloom_set_iterations (LRG_BLOOM (bloom), 7);
	lrg_bloom_set_threshold (LRG_BLOOM (bloom), 0.1f);
	g_assert_true (lrg_post_stack_build (stack, proc, &error));
	g_assert_true (lrg_post_processor_get_effect (proc, "bloom") == bloom);
	g_assert_true (lrg_post_processor_get_effect (proc, "vignette") == vignette);
	g_assert_cmpuint (lrg_bloom_get_iterations (LRG_BLOOM (bloom)), ==, 3);   /* default */
	g_assert_cmpfloat_with_epsilon (lrg_bloom_get_threshold (LRG_BLOOM (bloom)), 0.9f, 1e-6f);

	/* a smaller stack removes what it lacks and keeps what it shares */
	smaller = lrg_post_stack_new ();
	lrg_post_stack_add_effect (smaller, "vignette", NULL);
	lrg_post_stack_add_effect (smaller, "fxaa", NULL);
	g_assert_true (lrg_post_stack_build (smaller, proc, &error));
	g_assert_cmpuint (lrg_post_processor_get_effect_count (proc), ==, 2);
	g_assert_true (lrg_post_processor_get_effect (proc, "vignette") == vignette);
	g_assert_null (lrg_post_processor_get_effect (proc, "bloom"));
	g_assert_nonnull (lrg_post_processor_get_effect (proc, "fxaa"));
	g_assert_true (lrg_post_effect_is_enabled (vignette));
	g_assert_cmpfloat_with_epsilon (lrg_vignette_get_intensity (LRG_VIGNETTE (vignette)), 0.5f, 1e-6f);
	g_assert_true (LRG_IS_VIGNETTE (lrg_post_processor_get_effects (proc)->data));
}

/* runtime state (screen-shake trauma) survives rebuilds */
static void
test_build_keeps_trauma (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (64, 64);
	g_autoptr(LrgPostStack)     stack = lrg_post_stack_new ();
	LrgPostEffect              *shake;

	lrg_post_stack_add_effect (stack, "screen-shake", NULL);
	g_assert_true (lrg_post_stack_build (stack, proc, NULL));
	shake = lrg_post_processor_get_effect (proc, "screen-shake");
	lrg_screen_shake_add_trauma (LRG_SCREEN_SHAKE (shake), 0.6f);
	g_assert_true (lrg_post_stack_build (stack, proc, NULL));
	g_assert_true (lrg_post_processor_get_effect (proc, "screen-shake") == shake);
	g_assert_cmpfloat_with_epsilon (lrg_screen_shake_get_trauma (LRG_SCREEN_SHAKE (shake)), 0.6f, 1e-6f);
}

static void
test_build_special (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (64, 64);
	g_autoptr(LrgPostStack)     stack = NULL;
	g_autoptr(LrgPostStack)     bad = NULL;
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *dir = NULL;
	g_autofree gchar           *cube = NULL;
	g_autofree gchar           *yaml = NULL;
	LrgPostEffect              *pal, *grade;
	gfloat                      strength = 0.0f;
	guint8                      r = 0, g = 0, b = 0;

	dir = g_dir_make_tmp ("lrg-post-stack-XXXXXX", &error);
	g_assert_no_error (error);
	cube = g_build_filename (dir, "warm.cube", NULL);
	g_assert_true (g_file_set_contents (cube,
	                                    "LUT_3D_SIZE 2\n"
	                                    "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
	                                    "0 0 1\n1 0 1\n0 1 1\n1 1 1\n", -1, &error));

	/* lut relative to the .rpost directory */
	yaml = g_strdup ("effects:\n"
	                 "  - type: palettize\n"
	                 "    params:\n"
	                 "      palette: ['#000000', '#ff8000', '#ffffff']\n"
	                 "  - type: color-grade\n"
	                 "    params:\n"
	                 "      lut: warm.cube\n"
	                 "      lut_strength: 0.5\n"
	                 "      lift: [0.1, 0.0, -0.1]\n");
	stack = lrg_post_stack_new_from_data (yaml, -1, dir, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_post_stack_build (stack, proc, &error));
	g_assert_no_error (error);
	pal = lrg_post_processor_get_effect (proc, "palettize");
	grade = lrg_post_processor_get_effect (proc, "color-grade");
	g_assert_cmpuint (lrg_palettize_get_palette_size (LRG_PALETTIZE (pal)), ==, 3);
	g_assert_true (lrg_palettize_get_color (LRG_PALETTIZE (pal), 1, &r, &g, &b));
	g_assert_cmpuint (r, ==, 255);
	g_assert_cmpuint (g, ==, 128);
	g_assert_nonnull (lrg_color_grade_get_lut (LRG_COLOR_GRADE (grade), &strength));
	g_assert_cmpfloat_with_epsilon (strength, 0.5f, 1e-6f);

	/* a missing LUT fails and leaves the processor as it was */
	bad = lrg_post_stack_new_from_data ("effects:\n  - type: color-grade\n    params:\n      lut: missing.cube\n",
	                                    -1, dir, &error);
	g_assert_no_error (error);
	g_assert_false (lrg_post_stack_build (bad, proc, &error));
	g_assert_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_IO);
	g_assert_cmpuint (lrg_post_processor_get_effect_count (proc), ==, 2);

	g_unlink (cube);
	g_rmdir (dir);
}

static void
test_blend (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgPostStack) a = NULL;
	g_autoptr(LrgPostStack) b = NULL;
	g_autoptr(LrgPostStack) mid = NULL;
	g_autoptr(LrgPostStack) end = NULL;
	g_auto(GValue)          v = G_VALUE_INIT;

	a = lrg_post_stack_new_from_data ("quality: low\n"
	                                  "effects:\n"
	                                  "  - type: tonemap\n"
	                                  "    params: { exposure: 0.0, operator: aces }\n"
	                                  "  - type: vignette\n"
	                                  "    params: { intensity: 0.2 }\n",
	                                  -1, NULL, &error);
	g_assert_no_error (error);
	b = lrg_post_stack_new_from_data ("quality: ultra\n"
	                                  "effects:\n"
	                                  "  - type: tonemap\n"
	                                  "    params: { exposure: 2.0, operator: filmic }\n"
	                                  "  - type: film-grain\n"
	                                  "    weight: 0.5\n",
	                                  -1, NULL, &error);
	g_assert_no_error (error);

	mid = lrg_post_stack_blend (a, b, 0.25f);
	g_assert_cmpuint (lrg_post_stack_get_effect_count (mid), ==, 3);
	g_assert_cmpint (lrg_post_stack_get_quality (mid), ==, LRG_POST_QUALITY_LOW);
	g_assert_cmpstr (lrg_post_stack_get_effect_type (mid, 0), ==, "tonemap");
	g_assert_cmpfloat_with_epsilon (param_float (mid, 0, "exposure"), 0.5f, 1e-5f);
	g_assert_true (lrg_post_stack_get_param (mid, 0, "operator", &v));
	g_assert_cmpint (g_value_get_enum (&v), ==, LRG_TONEMAP_OPERATOR_ACES);   /* < 0.5 */
	g_value_unset (&v);
	/* a-only fades out, b-only fades in (appended) */
	g_assert_cmpstr (lrg_post_stack_get_effect_type (mid, 1), ==, "vignette");
	g_assert_cmpfloat_with_epsilon (lrg_post_stack_get_effect_weight (mid, 1), 0.75f, 1e-5f);
	g_assert_cmpstr (lrg_post_stack_get_effect_type (mid, 2), ==, "film-grain");
	g_assert_cmpfloat_with_epsilon (lrg_post_stack_get_effect_weight (mid, 2), 0.125f, 1e-5f);

	end = lrg_post_stack_blend (a, b, 1.0f);
	g_assert_cmpint (lrg_post_stack_get_quality (end), ==, LRG_POST_QUALITY_ULTRA);
	g_assert_cmpfloat_with_epsilon (param_float (end, 0, "exposure"), 2.0f, 1e-5f);
	g_assert_true (lrg_post_stack_get_param (end, 0, "operator", &v));
	g_assert_cmpint (g_value_get_enum (&v), ==, LRG_TONEMAP_OPERATOR_FILMIC);
	g_assert_false (lrg_post_stack_get_effect_enabled (end, 1));   /* faded out */
	g_assert_cmpfloat_with_epsilon (lrg_post_stack_get_effect_weight (end, 1), 0.0f, 1e-6f);
}

static void
test_from_processor (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (64, 64);
	g_autoptr(LrgPostProcessor) proc2 = lrg_post_processor_new (64, 64);
	g_autoptr(LrgVignette)      vignette = lrg_vignette_new ();
	g_autoptr(LrgFxaa)          fxaa = lrg_fxaa_new ();
	g_autoptr(LrgPostStack)     stack = NULL;
	g_autoptr(GError)           error = NULL;
	g_auto(GStrv)               names = NULL;

	lrg_vignette_set_radius (vignette, 0.7f);
	lrg_vignette_set_color (vignette, 0.0f, 0.0f, 0.5f);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (vignette), 5);
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (fxaa), 0.5f);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (fxaa));
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (vignette));

	stack = lrg_post_stack_new_from_processor (proc);
	g_assert_cmpuint (lrg_post_stack_get_effect_count (stack), ==, 2);
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 0), ==, "fxaa");
	g_assert_cmpfloat_with_epsilon (lrg_post_stack_get_effect_weight (stack, 0), 0.5f, 1e-6f);
	names = lrg_post_stack_get_param_names (stack, 1);
	g_assert_true (g_strv_contains ((const gchar * const *) names, "radius"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "color-b"));
	g_assert_false (g_strv_contains ((const gchar * const *) names, "roundness"));   /* default */

	/* and back */
	g_assert_true (lrg_post_stack_build (stack, proc2, &error));
	g_assert_cmpfloat_with_epsilon (lrg_vignette_get_radius (LRG_VIGNETTE (lrg_post_processor_get_effect (proc2, "vignette"))),
	                                0.7f, 1e-6f);
}

static void
test_tonemap_cpu (void)
{
	g_autoptr(LrgTonemap) tm = lrg_tonemap_new ();
	const gfloat          one[3] = { 1.0f, 1.0f, 1.0f };
	const gfloat          black[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                out[3];

	g_assert_cmpint (lrg_tonemap_get_operator (tm), ==, LRG_TONEMAP_OPERATOR_ACES);

	/* none: identity below white */
	lrg_tonemap_set_operator (tm, LRG_TONEMAP_OPERATOR_NONE);
	lrg_tonemap_apply_rgb (tm, (const gfloat[3]) { 0.5f, 0.25f, 1.0f }, out);
	g_assert_cmpfloat_with_epsilon (out[0], 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (out[1], 0.25f, 1e-4f);

	/* reinhard of linear 1 with a huge white point: 0.5 linear */
	lrg_tonemap_set_operator (tm, LRG_TONEMAP_OPERATOR_REINHARD);
	lrg_tonemap_set_white_point (tm, 100.0f);
	lrg_tonemap_apply_rgb (tm, one, out);
	g_assert_cmpfloat_with_epsilon (out[0], powf (0.5f, 1.0f / 2.2f), 1e-3f);
	/* at the white point: white */
	lrg_tonemap_set_white_point (tm, 1.0f);
	lrg_tonemap_apply_rgb (tm, one, out);
	g_assert_cmpfloat_with_epsilon (out[0], 1.0f, 1e-4f);

	/* filmic maps the white point to 1 */
	lrg_tonemap_set_operator (tm, LRG_TONEMAP_OPERATOR_FILMIC);
	lrg_tonemap_set_white_point (tm, 2.0f);
	lrg_tonemap_set_linear_input (tm, TRUE);
	lrg_tonemap_apply_rgb (tm, (const gfloat[3]) { 1.0f, 1.0f, 1.0f }, out);
	g_assert_cmpfloat_with_epsilon (out[0], 1.0f, 1e-3f);

	/* aces: black stays black, exposure brightens, monotone */
	lrg_tonemap_set_operator (tm, LRG_TONEMAP_OPERATOR_ACES);
	lrg_tonemap_apply_rgb (tm, black, out);
	g_assert_cmpfloat_with_epsilon (out[0], 0.0f, 1e-3f);
	{
		gfloat lo[3], hi[3];

		lrg_tonemap_set_exposure (tm, 0.0f);
		lrg_tonemap_apply_rgb (tm, (const gfloat[3]) { 0.3f, 0.3f, 0.3f }, lo);
		lrg_tonemap_set_exposure (tm, 1.0f);
		lrg_tonemap_apply_rgb (tm, (const gfloat[3]) { 0.3f, 0.3f, 0.3f }, hi);
		g_assert_cmpfloat (hi[0], >, lo[0]);
	}

	/* intensity 0 is the identity */
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (tm), 0.0f);
	lrg_tonemap_apply_rgb (tm, (const gfloat[3]) { 0.3f, 0.6f, 0.9f }, out);
	g_assert_cmpfloat_with_epsilon (out[2], 0.9f, 1e-6f);
}

static void
test_processor_headless (void)
{
	g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (100, 50);
	g_autoptr(LrgBloom)         bloom = lrg_bloom_new ();

	/* no window: state only, nothing drawn */
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (bloom));
	g_assert_true (lrg_post_effect_is_initialized (LRG_POST_EFFECT (bloom)));
	lrg_post_processor_set_quality (proc, LRG_POST_QUALITY_LOW);
	g_assert_cmpint (lrg_post_effect_get_quality_tier (LRG_POST_EFFECT (bloom)), ==, LRG_POST_QUALITY_LOW);
	if (!gpu_ready)
	{
		lrg_post_processor_begin_capture (proc);
		g_assert_true (lrg_post_processor_is_capturing (proc));
		lrg_post_processor_end_capture (proc);
		g_assert_false (lrg_post_processor_is_capturing (proc));
		lrg_post_processor_render (proc, 0.016f);
		g_assert_cmpuint (lrg_post_processor_get_capture_texture (proc), ==, 0);
		g_assert_cmpuint (lrg_post_processor_process (proc, 7, 0.0f), ==, 7);
	}

	/* disabled: capture does nothing */
	lrg_post_processor_set_enabled (proc, FALSE);
	lrg_post_processor_begin_capture (proc);
	g_assert_false (lrg_post_processor_is_capturing (proc));
	g_assert_true (lrg_post_processor_get_hdr (proc));
}

/* ==========================================================================
 * GPU
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
	InitWindow (GPU_W, GPU_H, "test-post-stack");
	return IsWindowReady ();
}

typedef void (*SceneFunc) (gint w, gint h);

/* Captures @scene through @proc into an outer render texture and reads
 * it back top-down. */
static Image
run_frame (LrgPostProcessor *proc,
           SceneFunc         scene,
           gint              w,
           gint              h)
{
	RenderTexture2D outer = LoadRenderTexture (w, h);
	Image           img;

	BeginDrawing ();
	BeginTextureMode (outer);
	ClearBackground (MAGENTA);   /* must be fully covered by the composite */
	lrg_post_processor_begin_capture (proc);
	scene (w, h);
	lrg_post_processor_end_capture (proc);
	lrg_post_processor_render (proc, 1.0f / 60.0f);
	EndTextureMode ();
	EndDrawing ();
	img = LoadImageFromTexture (outer.texture);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	ImageFlipVertical (&img);
	UnloadRenderTexture (outer);
	return img;
}

static const guint8 *
px (Image img,
    gint  x,
    gint  y)
{
	return (const guint8 *) img.data + ((gsize) y * (gsize) img.width + (gsize) x) * 4;
}

static void
scene_white (gint w,
             gint h)
{
	ClearBackground (WHITE);
}

static void
scene_grey (gint w,
            gint h)
{
	ClearBackground ((Color) { 128, 128, 128, 255 });
}

/* left half red, right half blue, a green square top-left */
static void
scene_pattern (gint w,
               gint h)
{
	ClearBackground (BLACK);
	DrawRectangle (0, 0, w / 2, h, RED);
	DrawRectangle (w / 2, 0, w - w / 2, h, BLUE);
	DrawRectangle (4, 4, 8, 8, GREEN);
}

static void
scene_spot (gint w,
            gint h)
{
	ClearBackground (BLACK);
	DrawRectangle (w / 2 - 4, h / 2 - 4, 8, 8, WHITE);
}

static void
test_gpu_passthrough (void)
{
	g_autoptr(LrgPostProcessor) proc = NULL;
	Image                       img;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	proc = lrg_post_processor_new (GPU_W, GPU_H);
	img = run_frame (proc, scene_pattern, GPU_W, GPU_H);
	g_assert_cmpuint (lrg_post_processor_get_capture_texture (proc), !=, 0);
	g_assert_true (lrg_post_processor_is_hdr_active (proc));   /* llvmpipe renders to RGBA16F */
	/* the scene, unchanged, in the right orientation */
	g_assert_cmpuint (px (img, 6, 6)[1], ==, 228);     /* raylib GREEN = 0,228,48 */
	g_assert_cmpuint (px (img, 6, 6)[0], ==, 0);
	g_assert_cmpuint (px (img, 20, 40)[0], ==, 230);   /* RED = 230,41,55 */
	g_assert_cmpuint (px (img, 50, 40)[2], ==, 241);   /* BLUE = 0,121,241 */
	g_assert_cmpuint (px (img, 50, 40)[3], ==, 255);
	g_assert_cmpuint (lrg_post_processor_get_pass_count (proc), ==, 0);
	UnloadImage (img);

	/* RGBA8 when HDR is off */
	lrg_post_processor_set_hdr (proc, FALSE);
	img = run_frame (proc, scene_pattern, GPU_W, GPU_H);
	g_assert_false (lrg_post_processor_is_hdr_active (proc));
	g_assert_cmpuint (px (img, 20, 40)[0], ==, 230);
	UnloadImage (img);
	lrg_post_processor_release (proc);
}

static void
test_gpu_vignette (void)
{
	g_autoptr(LrgPostProcessor) proc = NULL;
	g_autoptr(LrgVignette)      vignette = lrg_vignette_new ();
	Image                       img;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	proc = lrg_post_processor_new (GPU_W, GPU_H);
	lrg_vignette_set_intensity (vignette, 1.0f);
	lrg_vignette_set_radius (vignette, 0.3f);
	lrg_vignette_set_smoothness (vignette, 0.3f);
	lrg_vignette_set_color (vignette, 0.0f, 0.0f, 0.0f);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (vignette));

	img = run_frame (proc, scene_white, GPU_W, GPU_H);
	g_assert_cmpuint (lrg_post_processor_get_pass_count (proc), ==, 1);
	g_assert_cmpuint (px (img, GPU_W / 2, GPU_H / 2)[0], >=, 250);   /* centre untouched */
	g_assert_cmpuint (px (img, 0, 0)[0], <=, 10);                    /* corners black */
	g_assert_cmpuint (px (img, GPU_W - 1, GPU_H - 1)[0], <=, 10);
	g_assert_cmpuint (px (img, 0, 0)[3], ==, 255);
	UnloadImage (img);

	/* the base intensity (stack weight) scales it */
	lrg_post_effect_set_intensity (LRG_POST_EFFECT (vignette), 0.5f);
	img = run_frame (proc, scene_white, GPU_W, GPU_H);
	g_assert_cmpuint (px (img, 0, 0)[0], >=, 115);
	g_assert_cmpuint (px (img, 0, 0)[0], <=, 140);
	UnloadImage (img);
	lrg_post_processor_release (proc);
}

static void
test_gpu_tonemap (void)
{
	g_autoptr(LrgPostProcessor) proc = NULL;
	g_autoptr(LrgTonemap)       tm = lrg_tonemap_new ();
	const LrgTonemapOperator    ops[] = { LRG_TONEMAP_OPERATOR_REINHARD, LRG_TONEMAP_OPERATOR_FILMIC,
	                                      LRG_TONEMAP_OPERATOR_ACES };
	guint                       i;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	proc = lrg_post_processor_new (GPU_W, GPU_H);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (tm));
	lrg_tonemap_set_exposure (tm, 1.0f);
	for (i = 0; i < G_N_ELEMENTS (ops); i++)
	{
		const gfloat grey[3] = { 128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f };
		gfloat       want[3];
		Image        img;

		lrg_tonemap_set_operator (tm, ops[i]);
		lrg_tonemap_apply_rgb (tm, grey, want);
		img = run_frame (proc, scene_grey, GPU_W, GPU_H);
		g_assert_cmpint (ABS ((gint) px (img, 10, 10)[0] - (gint) lroundf (want[0] * 255.0f)), <=, 2);
		g_assert_cmpint (ABS ((gint) px (img, 50, 50)[2] - (gint) lroundf (want[2] * 255.0f)), <=, 2);
		/* the curve changes the image */
		g_assert_cmpint (ABS ((gint) px (img, 10, 10)[0] - 128), >, 4);
		UnloadImage (img);
	}
	lrg_post_processor_release (proc);
}

static void
test_gpu_bloom (void)
{
	g_autoptr(LrgPostProcessor) proc = NULL;
	g_autoptr(LrgBloom)         bloom = lrg_bloom_new ();
	Image                       plain, glow;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	proc = lrg_post_processor_new (GPU_W, GPU_H);
	plain = run_frame (proc, scene_spot, GPU_W, GPU_H);
	lrg_bloom_set_threshold (bloom, 0.5f);
	lrg_bloom_set_intensity (bloom, 3.0f);
	lrg_bloom_set_iterations (bloom, 4);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (bloom));
	glow = run_frame (proc, scene_spot, GPU_W, GPU_H);

	/* just outside the square: black without bloom, lit with it */
	g_assert_cmpuint (px (plain, GPU_W / 2 + 7, GPU_H / 2)[0], ==, 0);
	g_assert_cmpuint (px (glow, GPU_W / 2 + 7, GPU_H / 2)[0], >, 20);
	g_assert_cmpuint (px (glow, GPU_W / 2 - 8, GPU_H / 2)[0], >, 20);   /* symmetric */
	/* far corner stays dark */
	g_assert_cmpuint (px (glow, 0, 0)[0], <, 20);
	UnloadImage (plain);
	UnloadImage (glow);
	lrg_post_processor_release (proc);
}

/* Every built-in effect through one chain, built from a stack */
static void
test_gpu_all_effects (void)
{
	g_autoptr(LrgPostProcessor) proc = NULL;
	g_autoptr(LrgPostStack)     stack = NULL;
	g_autoptr(GError)           error = NULL;
	Image                       img;
	gint                        frame;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	stack = lrg_post_stack_new_from_data ("effects:\n"
	                                      "  - type: screen-shake\n"
	                                      "    params: { trauma: 1.0 }\n"
	                                      "  - type: heat-haze\n"
	                                      "  - type: bloom\n"
	                                      "  - type: tonemap\n"
	                                      "  - type: color-grade\n"
	                                      "    params: { saturation: 1.3 }\n"
	                                      "  - type: colorblind-filter\n"
	                                      "    params: { filter-type: deuteranopia }\n"
	                                      "  - type: sharpen\n"
	                                      "  - type: fxaa\n"
	                                      "  - type: palettize\n"
	                                      "    params: { palette: ['#000000', '#ffffff', '#ff0000', '#0000ff'] }\n"
	                                      "  - type: film-grain\n"
	                                      "  - type: vignette\n",
	                                      -1, NULL, &error);
	g_assert_no_error (error);
	proc = lrg_post_processor_new (GPU_W, GPU_H);
	g_assert_true (lrg_post_stack_build (stack, proc, &error));
	g_assert_no_error (error);
	for (frame = 0; frame < 3; frame++)
	{
		guint  colours = 0;
		gint   i;

		img = run_frame (proc, scene_pattern, GPU_W, GPU_H);
		g_assert_cmpuint (lrg_post_processor_get_pass_count (proc), ==, 11);
		/* the composite covered the target and the palette quantised it:
		 * every pixel is one of the four palette colours (maybe dimmed
		 * by the vignette, never the magenta under the composite) */
		for (i = 0; i < img.width * img.height; i++)
		{
			const guint8 *p = (const guint8 *) img.data + i * 4;

			g_assert_false (p[0] > 200 && p[1] < 50 && p[2] > 200);
			g_assert_cmpuint (p[3], ==, 255);
			if (p[0] > 0 || p[1] > 0 || p[2] > 0)
				colours++;
		}
		g_assert_cmpuint (colours, >, 0);
		UnloadImage (img);
	}
	lrg_post_processor_release (proc);
}

static void
test_gpu_process_and_resize (void)
{
	g_autoptr(LrgPostProcessor) proc = NULL;
	g_autoptr(LrgVignette)      vignette = lrg_vignette_new ();
	RenderTexture2D             src, outer;
	Image                       img;
	guint                       result;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	/* process(): a renderer's own scene texture, the binding kept */
	proc = lrg_post_processor_new (GPU_W, GPU_H);
	lrg_vignette_set_intensity (vignette, 1.0f);
	lrg_vignette_set_radius (vignette, 0.2f);
	lrg_vignette_set_smoothness (vignette, 0.2f);
	lrg_post_processor_add_effect (proc, LRG_POST_EFFECT (vignette));
	src = LoadRenderTexture (GPU_W, GPU_H);
	outer = LoadRenderTexture (GPU_W, GPU_H);
	BeginDrawing ();
	BeginTextureMode (src);
	ClearBackground (WHITE);
	EndTextureMode ();
	BeginTextureMode (outer);
	result = lrg_post_processor_process (proc, src.texture.id, 0.0f);
	g_assert_cmpuint (rlGetActiveFramebuffer (), ==, outer.id);
	g_assert_cmpuint (result, !=, src.texture.id);
	lrg_post_effect_draw_fullscreen (result, GPU_W, GPU_H);
	EndTextureMode ();
	EndDrawing ();
	img = LoadImageFromTexture (outer.texture);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	g_assert_cmpuint (px (img, GPU_W / 2, GPU_H / 2)[0], >=, 250);
	g_assert_cmpuint (px (img, 0, 0)[0], <=, 10);
	UnloadImage (img);
	UnloadRenderTexture (src);
	UnloadRenderTexture (outer);

	/* auto-resize follows the bound target; render scales to it */
	lrg_post_processor_set_auto_resize (proc, TRUE);
	img = run_frame (proc, scene_white, 32, 48);
	g_assert_cmpuint (lrg_post_processor_get_width (proc), ==, 32);
	g_assert_cmpuint (lrg_post_processor_get_height (proc), ==, 48);
	g_assert_cmpuint (px (img, 16, 24)[0], >=, 250);
	g_assert_cmpuint (px (img, 0, 0)[0], <=, 10);
	UnloadImage (img);
	lrg_post_processor_release (proc);
}

/* LRG_POST_BENCH=1: milliseconds per effect at 1920x1080 */
static void
bench_scene (gint w,
             gint h)
{
	ClearBackground (DARKBLUE);
	DrawRectangle (w / 4, h / 4, w / 2, h / 2, ORANGE);
	DrawCircle (w / 2, h / 2, (gfloat) h / 8.0f, WHITE);
}

static void
test_gpu_bench (void)
{
	const gchar *types[] = { NULL, "bloom", "tonemap", "color-grade", "colorblind-filter", "film-grain",
	                         "fxaa", "heat-haze", "palettize", "screen-shake", "sharpen", "vignette" };
	RenderTexture2D outer;
	guint           i;

	if (!gpu_ready || g_getenv ("LRG_POST_BENCH") == NULL)
	{
		g_test_skip ("set LRG_POST_BENCH=1 under xrun.sh");
		return;
	}
	outer = LoadRenderTexture (1920, 1080);
	for (i = 0; i < G_N_ELEMENTS (types); i++)
	{
		g_autoptr(LrgPostProcessor) proc = lrg_post_processor_new (1920, 1080);
		g_autoptr(LrgPostStack)     stack = lrg_post_stack_new ();
		Image                       sync;
		gint64                      t0 = 0;
		gint                        f;
		const gint                  frames = 20;

		if (types[i] != NULL)
		{
			gint k = lrg_post_stack_add_effect (stack, types[i], NULL);

			if (g_strcmp0 (types[i], "palettize") == 0)
			{
				g_auto(GValue) v = G_VALUE_INIT;
				const gchar   *pal[] = { "#000000", "#ffffff", "#ff8000", "#0040ff", NULL };

				g_value_init (&v, G_TYPE_STRV);
				g_value_set_boxed (&v, pal);
				lrg_post_stack_set_param (stack, (guint) k, "palette", &v, NULL);
			}
			if (g_strcmp0 (types[i], "screen-shake") == 0)
			{
				g_auto(GValue) v = G_VALUE_INIT;

				g_value_init (&v, G_TYPE_FLOAT);
				g_value_set_float (&v, 1.0f);
				lrg_post_stack_set_param (stack, (guint) k, "trauma", &v, NULL);
			}
		}
		lrg_post_stack_build (stack, proc, NULL);
		for (f = -2; f < frames; f++)
		{
			if (f == 0)
			{
				sync = LoadImageFromTexture (outer.texture);   /* drain the queue */
				UnloadImage (sync);
				t0 = g_get_monotonic_time ();
			}
			BeginDrawing ();
			BeginTextureMode (outer);
			lrg_post_processor_begin_capture (proc);
			bench_scene (1920, 1080);
			lrg_post_processor_end_capture (proc);
			lrg_post_processor_render (proc, 1.0f / 60.0f);
			EndTextureMode ();
			EndDrawing ();
		}
		sync = LoadImageFromTexture (outer.texture);
		UnloadImage (sync);
		g_printerr ("bench 1920x1080 %-18s %7.2f ms/frame\n", types[i] != NULL ? types[i] : "(capture only)",
		            (gdouble) (g_get_monotonic_time () - t0) / 1000.0 / frames);
		lrg_post_processor_release (proc);
	}
	UnloadRenderTexture (outer);
}

int
main (int   argc,
      char *argv[])
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();

	g_test_add_func ("/post-stack/registry", test_registry);
	g_test_add_func ("/post-stack/load", test_load);
	g_test_add_func ("/post-stack/round-trip", test_round_trip);
	g_test_add_func ("/post-stack/errors", test_errors);
	g_test_add_func ("/post-stack/build", test_build);
	g_test_add_func ("/post-stack/build-keeps-trauma", test_build_keeps_trauma);
	g_test_add_func ("/post-stack/build-special", test_build_special);
	g_test_add_func ("/post-stack/blend", test_blend);
	g_test_add_func ("/post-stack/from-processor", test_from_processor);
	g_test_add_func ("/post-stack/tonemap-cpu", test_tonemap_cpu);
	g_test_add_func ("/post-stack/processor-headless", test_processor_headless);
	g_test_add_func ("/post-stack/gpu/passthrough", test_gpu_passthrough);
	g_test_add_func ("/post-stack/gpu/vignette", test_gpu_vignette);
	g_test_add_func ("/post-stack/gpu/tonemap", test_gpu_tonemap);
	g_test_add_func ("/post-stack/gpu/bloom", test_gpu_bloom);
	g_test_add_func ("/post-stack/gpu/all-effects", test_gpu_all_effects);
	g_test_add_func ("/post-stack/gpu/process-and-resize", test_gpu_process_and_resize);
	g_test_add_func ("/post-stack/gpu/bench", test_gpu_bench);

	ret = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return ret;
}
