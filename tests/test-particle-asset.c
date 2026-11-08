/* test-particle-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the `.rparticles` format and its runtime: LrgColorGradient,
 * LrgParticleRng, the module stack (analytic checks for gravity, drag,
 * curves and gradients; bursts, rates, sub-emitters, collisions,
 * shapes), seeded determinism, the YAML round trip with unknown keys and
 * validation messages, effect reload, the ribbon and mesh renderers'
 * headless geometry, the particle component, and GPU draws under a
 * display (skipped headless).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define STEP (1.0f / 60.0f)

/* A fixture in the writer's own layout: load + save must give it back */
static const gchar *FIXTURE =
	"version: 1\n"
	"name: Sparks\n"
	"seed: 42\n"
	"duration: 2\n"
	"loop: false\n"
	"emitters:\n"
	"- name: sparks\n"
	"  max-particles: 200\n"
	"  renderer:\n"
	"    kind: ribbon\n"
	"    blend: additive\n"
	"    width: 0.05\n"
	"    width-curve: 0 1; 1 0\n"
	"    color-gradient: '0 #ffffffff; 1 #ff400000'\n"
	"  modules:\n"
	"  - type: burst\n"
	"    count:\n"
	"    - 20\n"
	"    - 30\n"
	"  - type: shape\n"
	"    shape: cone\n"
	"    angle: 30\n"
	"  - type: velocity\n"
	"    speed:\n"
	"    - 4\n"
	"    - 8\n"
	"  - type: lifetime\n"
	"    lifetime:\n"
	"    - 0.4\n"
	"    - 0.8\n"
	"  - type: color\n"
	"    color: '#ffc040'\n"
	"  - type: gravity\n"
	"  - type: collision\n"
	"    bounce: 0.3\n"
	"    colour: red\n"
	"  - type: sub-emitter\n"
	"    emitter: smoke\n"
	"    count: 2\n"
	"- name: smoke\n"
	"  enabled: false\n"
	"  modules:\n"
	"  - type: size-over-life\n"
	"    curve: 0 0.5; 1 2 ease-out-quad\n"
	"  x-editor:\n"
	"    folded: true\n";

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* An asset with one emitter "main" and the given modules */
static LrgParticleAsset *
one_emitter (LrgParticleAssetEmitter **out_emitter)
{
	LrgParticleAsset                  *asset = lrg_particle_asset_new ();
	g_autoptr(LrgParticleAssetEmitter) e = lrg_particle_asset_emitter_new ("main");

	lrg_particle_asset_add_emitter (asset, e);
	if (out_emitter != NULL)
		*out_emitter = e;
	return asset;
}

typedef struct
{
	const LrgParticle *last;
	guint              count;
	gfloat             min_y;
	gfloat             max_dist;
} Scan;

static void
scan_particle (const LrgParticle *p,
               gpointer           user_data)
{
	Scan  *s = user_data;
	gfloat d = sqrtf (p->position_x * p->position_x + p->position_y * p->position_y +
	                  p->position_z * p->position_z);

	if (s->count == 0 || p->position_y < s->min_y)
		s->min_y = p->position_y;
	if (d > s->max_dist)
		s->max_dist = d;
	s->last = p;
	s->count++;
}

static Scan
scan (LrgParticleSystem *sys)
{
	Scan s;

	memset (&s, 0, sizeof s);
	lrg_particle_system_foreach_active (sys, scan_particle, &s);
	return s;
}

static void
range (LrgParticleModule *m,
       const gchar       *name,
       gfloat             lo,
       gfloat             hi)
{
	g_autoptr(GrlVector2) v = grl_vector2_new (lo, hi);

	g_object_set (m, name, v, NULL);
}

static void
vec3 (LrgParticleModule *m,
      const gchar       *name,
      gfloat             x,
      gfloat             y,
      gfloat             z)
{
	g_autoptr(GrlVector3) v = grl_vector3_new (x, y, z);

	g_object_set (m, name, v, NULL);
}

/* One particle at the origin, at rest, living @life seconds */
static LrgParticleAsset *
single_particle (LrgParticleAssetEmitter **out_emitter,
                 gfloat                    life)
{
	LrgParticleAssetEmitter *e;
	LrgParticleAsset        *asset = one_emitter (&e);
	LrgParticleModule       *m;

	m = lrg_particle_asset_emitter_add_module (e, "burst");
	range (m, "count", 1, 1);
	m = lrg_particle_asset_emitter_add_module (e, "shape");
	g_object_set (m, "shape", LRG_PARTICLE_SHAPE_POINT, NULL);
	m = lrg_particle_asset_emitter_add_module (e, "velocity");
	range (m, "speed", 0, 0);
	m = lrg_particle_asset_emitter_add_module (e, "lifetime");
	range (m, "lifetime", life, life);
	if (out_emitter != NULL)
		*out_emitter = e;
	return asset;
}

/* ==========================================================================
 * LrgColorGradient
 * ========================================================================== */

static void
test_gradient_evaluate (void)
{
	g_autoptr(LrgColorGradient) g = lrg_color_gradient_new ();
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgColorGradient) parsed = NULL;
	g_autoptr(LrgGradientStops) stops = NULL;
	g_autoptr(LrgColorGradient) back = NULL;
	g_autofree gchar           *text = NULL;
	static const gfloat         red[4] = { 1, 0, 0, 1 };
	static const gfloat         blue[4] = { 0, 0, 1, 0 };
	gfloat                      c[4];

	/* empty: the neutral multiplier */
	lrg_color_gradient_evaluate (g, 0.3f, c);
	g_assert_cmpfloat (c[0], ==, 1.0f);
	g_assert_cmpfloat (c[3], ==, 1.0f);

	lrg_color_gradient_add_stop (g, 1.0f, blue);
	lrg_color_gradient_add_stop (g, 0.0f, red);
	g_assert_cmpuint (lrg_color_gradient_get_n_stops (g), ==, 2);

	/* linear: halfway is the average; clamped outside */
	lrg_color_gradient_evaluate (g, 0.5f, c);
	g_assert_cmpfloat_with_epsilon (c[0], 0.5f, 1e-6);
	g_assert_cmpfloat_with_epsilon (c[2], 0.5f, 1e-6);
	g_assert_cmpfloat_with_epsilon (c[3], 0.5f, 1e-6);
	lrg_color_gradient_evaluate (g, 2.0f, c);
	g_assert_cmpfloat (c[2], ==, 1.0f);

	/* the table agrees within a step */
	lrg_color_gradient_evaluate_fast (g, 0.37f, c);
	g_assert_cmpfloat_with_epsilon (c[0], 0.63f, 1.0f / 255.0f);

	/* step holds the left stop, smooth is smoothstep */
	lrg_color_gradient_set_mode (g, LRG_COLOR_GRADIENT_STEP);
	lrg_color_gradient_evaluate (g, 0.9f, c);
	g_assert_cmpfloat (c[0], ==, 1.0f);
	lrg_color_gradient_set_mode (g, LRG_COLOR_GRADIENT_SMOOTH);
	lrg_color_gradient_evaluate (g, 0.25f, c);
	g_assert_cmpfloat_with_epsilon (c[2], 0.25f * 0.25f * (3.0f - 0.5f), 1e-6);

	/* text form round trip, with the mode */
	text = lrg_color_gradient_to_string (g);
	g_assert_cmpstr (text, ==, "smooth: 0 #ff0000ff; 1 #0000ff00");
	parsed = lrg_color_gradient_new_from_string (text, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_color_gradient_equal (g, parsed));

	/* editor stops round trip */
	stops = lrg_color_gradient_to_stops (g);
	g_assert_cmpuint (lrg_gradient_stops_get_n_stops (stops), ==, 2);
	back = lrg_color_gradient_new_from_stops (stops);
	lrg_color_gradient_set_mode (back, LRG_COLOR_GRADIENT_SMOOTH);
	g_assert_true (lrg_color_gradient_equal (g, back));

	/* errors */
	g_assert_null (lrg_color_gradient_new_from_string ("0 red", &error));
	g_assert_error (error, LRG_COLOR_GRADIENT_ERROR, LRG_COLOR_GRADIENT_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_null (lrg_color_gradient_new_from_string ("wobbly: 0 #fff", &error));
	g_assert_error (error, LRG_COLOR_GRADIENT_ERROR, LRG_COLOR_GRADIENT_ERROR_PARSE);
}

/* ==========================================================================
 * LrgParticleRng
 * ========================================================================== */

static void
test_rng (void)
{
	LrgParticleRng a, b;
	gdouble        sum = 0.0;
	guint          i, same = 0;

	lrg_particle_rng_seed (&a, 7);
	lrg_particle_rng_seed (&b, 7);
	for (i = 0; i < 1000; i++)
		g_assert_cmpuint (lrg_particle_rng_next_u32 (&a), ==, lrg_particle_rng_next_u32 (&b));

	/* another seed, another sequence */
	lrg_particle_rng_seed (&a, 7);
	lrg_particle_rng_seed (&b, 8);
	for (i = 0; i < 100; i++)
		same += lrg_particle_rng_next_u32 (&a) == lrg_particle_rng_next_u32 (&b);
	g_assert_cmpuint (same, <, 3);

	/* floats in [0, 1), mean near 0.5; a constant range draws nothing */
	for (i = 0; i < 10000; i++)
	{
		gfloat f = lrg_particle_rng_next_float (&a);

		g_assert_cmpfloat (f, >=, 0.0f);
		g_assert_cmpfloat (f, <, 1.0f);
		sum += f;
	}
	g_assert_cmpfloat (fabs (sum / 10000.0 - 0.5), <, 0.02);
	b = a;
	g_assert_cmpfloat (lrg_particle_rng_range (&a, 3.0f, 3.0f), ==, 3.0f);
	g_assert_cmpuint (lrg_particle_rng_next_u32 (&a), ==, lrg_particle_rng_next_u32 (&b));

	/* unit vectors are unit */
	for (i = 0; i < 100; i++)
	{
		gfloat v[3];

		lrg_particle_rng_unit_vector (&a, v);
		g_assert_cmpfloat_with_epsilon (v[0] * v[0] + v[1] * v[1] + v[2] * v[2], 1.0f, 1e-5);
	}
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static void
test_round_trip (void)
{
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgParticleAsset) asset = NULL;
	g_autoptr(LrgParticleAsset) again = NULL;
	g_autofree gchar           *text = NULL;
	g_autofree gchar           *text2 = NULL;
	g_auto(GStrv)               unknown = NULL;
	LrgParticleAssetEmitter    *e;
	LrgParticleModule          *m;
	g_autoptr(GrlVector2)       count = NULL;
	g_autoptr(GrlColor)         color = NULL;

	asset = lrg_particle_asset_new_from_data (FIXTURE, -1, "sparks.rparticles", &error);
	g_assert_no_error (error);

	/* the fields */
	g_assert_cmpstr (lrg_particle_asset_get_name (asset), ==, "Sparks");
	g_assert_cmpuint (lrg_particle_asset_get_seed (asset), ==, 42);
	g_assert_cmpfloat (lrg_particle_asset_get_duration (asset), ==, 2.0f);
	g_assert_false (lrg_particle_asset_get_loop (asset));
	g_assert_cmpuint (lrg_particle_asset_get_n_emitters (asset), ==, 2);
	e = lrg_particle_asset_get_emitter (asset, 0);
	g_assert_cmpstr (lrg_particle_asset_emitter_get_name (e), ==, "sparks");
	g_assert_cmpuint (lrg_particle_asset_emitter_get_max_particles (e), ==, 200);
	g_assert_cmpint (lrg_particle_render_settings_get_kind (lrg_particle_asset_emitter_get_renderer (e)), ==,
	                 LRG_PARTICLE_RENDERER_RIBBON);
	g_assert_cmpint (lrg_particle_render_settings_get_blend (lrg_particle_asset_emitter_get_renderer (e)), ==,
	                 LRG_PARTICLE_BLEND_ADDITIVE);
	g_assert_cmpuint (lrg_particle_asset_emitter_get_n_modules (e), ==, 8);
	m = lrg_particle_asset_emitter_get_module (e, 0);
	g_assert_cmpstr (lrg_particle_module_get_kind (m), ==, "burst");
	g_object_get (m, "count", &count, NULL);
	g_assert_cmpfloat (count->x, ==, 20.0f);
	g_assert_cmpfloat (count->y, ==, 30.0f);
	m = lrg_particle_asset_emitter_find_module (e, "color");
	g_object_get (m, "color", &color, NULL);
	g_assert_cmpuint (color->r, ==, 0xff);
	g_assert_cmpuint (color->g, ==, 0xc0);
	g_assert_cmpuint (color->b, ==, 0x40);
	g_assert_false (lrg_particle_asset_emitter_get_enabled (lrg_particle_asset_get_emitter (asset, 1)));

	/* unknown keys are kept and reported */
	unknown = lrg_particle_asset_get_unknown_keys (asset);
	g_assert_cmpuint (g_strv_length (unknown), ==, 2);
	g_assert_cmpstr (unknown[0], ==, "/emitters/0/modules/6/colour");
	g_assert_cmpstr (unknown[1], ==, "/emitters/1/x-editor");

	/* save gives the fixture back, byte for byte, and again */
	text = lrg_particle_asset_save_to_data (asset, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, FIXTURE);
	again = lrg_particle_asset_new_from_data (text, -1, NULL, &error);
	g_assert_no_error (error);
	text2 = lrg_particle_asset_save_to_data (again, NULL, &error);
	g_assert_cmpstr (text2, ==, text);
}

static void
test_file_io (void)
{
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgParticleAsset) asset = NULL;
	g_autoptr(LrgParticleAsset) loaded = NULL;
	g_autofree gchar           *dir = g_dir_make_tmp ("lrg-rparticles-XXXXXX", &error);
	g_autofree gchar           *path = NULL;
	g_autofree gchar           *a = NULL;
	g_autofree gchar           *b = NULL;

	g_assert_no_error (error);
	path = g_build_filename (dir, "fx.rparticles", NULL);
	asset = lrg_particle_asset_new_from_data (FIXTURE, -1, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_particle_asset_save_to_file (asset, path, &error));
	g_assert_no_error (error);
	loaded = lrg_particle_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	a = lrg_particle_asset_save_to_data (asset, NULL, NULL);
	b = lrg_particle_asset_save_to_data (loaded, NULL, NULL);
	g_assert_cmpstr (a, ==, b);
	g_remove (path);
	g_rmdir (dir);

	g_assert_null (lrg_particle_asset_new_from_file ("/nonexistent/x.rparticles", &error));
	g_assert_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_IO);
}

static void
test_edit_and_save (void)
{
	g_autoptr(LrgParticleAsset)        asset = lrg_particle_asset_new ();
	g_autoptr(LrgParticleAssetEmitter) e = lrg_particle_asset_emitter_new ("fire");
	g_autoptr(LrgKeyframeCurve)        curve = lrg_keyframe_curve_new ();
	g_autoptr(GError)                  error = NULL;
	g_autofree gchar                   *text = NULL;
	LrgParticleModule                  *m;
	guint                               changes = 0;

	g_signal_connect_swapped (asset, "changed", G_CALLBACK (g_atomic_int_inc), &changes);
	lrg_particle_asset_set_seed (asset, 9);
	lrg_particle_asset_add_emitter (asset, e);
	m = lrg_particle_asset_emitter_add_module (e, "spawn-rate");
	g_object_set (m, "rate", 40.0f, NULL);
	m = lrg_particle_asset_emitter_add_module (e, "size-over-life");
	lrg_keyframe_curve_add_key (curve, 0.0f, 1.0f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (curve, 1.0f, 0.0f, LRG_EASING_LINEAR);
	g_object_set (m, "curve", curve, NULL);
	lrg_particle_module_set_enabled (m, FALSE);
	lrg_particle_render_settings_set_texture (lrg_particle_asset_emitter_get_renderer (e), "fx/flame.png");
	g_assert_cmpuint (changes, >=, 6);

	g_assert_null (lrg_particle_asset_emitter_add_module (e, "no-such-module"));
	g_assert_true (lrg_particle_asset_emitter_move_module (e, 1, 0));

	text = lrg_particle_asset_save_to_data (asset, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==,
	                 "version: 1\n"
	                 "seed: 9\n"
	                 "emitters:\n"
	                 "- name: fire\n"
	                 "  renderer:\n"
	                 "    texture: fx/flame.png\n"
	                 "  modules:\n"
	                 "  - type: size-over-life\n"
	                 "    curve: 0 1; 1 0\n"
	                 "    enabled: false\n"
	                 "  - type: spawn-rate\n"
	                 "    rate: 40\n");
	g_assert_true (lrg_particle_asset_emitter_remove_module (e, 0));
	g_assert_cmpuint (lrg_particle_asset_emitter_get_n_modules (e), ==, 1);
}

static void
check_error (const gchar *yaml,
             gint         code,
             const gchar *fragment)
{
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgParticleAsset) asset = lrg_particle_asset_new_from_data (yaml, -1, "bad.rparticles", &error);

	g_assert_null (asset);
	g_assert_error (error, LRG_PARTICLE_ASSET_ERROR, code);
	if (strstr (error->message, fragment) == NULL)
		g_error ("expected '%s' in '%s'", fragment, error->message);
}

static void
test_validation (void)
{
	check_error ("version: 1\n", LRG_PARTICLE_ASSET_ERROR_PARSE, "bad.rparticles: /: not a particle effect");
	check_error ("version: 9\nemitters: []\n", LRG_PARTICLE_ASSET_ERROR_VERSION, "/version: version 9 is newer");
	check_error ("emitters:\n- modules: []\n", LRG_PARTICLE_ASSET_ERROR_PARSE, "/emitters/0: an emitter needs");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: sparkle\n", LRG_PARTICLE_ASSET_ERROR_UNKNOWN_MODULE,
	             "/emitters/0/modules/0/type: unknown module 'sparkle' (known: spawn-rate, burst");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: spawn-rate\n    rate: lots\n",
	             LRG_PARTICLE_ASSET_ERROR_PARSE, "/emitters/0/modules/0/rate: expected a number");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: burst\n    probability: 2\n",
	             LRG_PARTICLE_ASSET_ERROR_VALUE, "/emitters/0/modules/0/probability: expected a number from 0 to 1");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: velocity\n    speed: [5, 1]\n",
	             LRG_PARTICLE_ASSET_ERROR_VALUE, "/emitters/0/modules/0/speed: the minimum 5 is above the maximum 1");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: shape\n    shape: donut\n",
	             LRG_PARTICLE_ASSET_ERROR_VALUE, "expected one of point, sphere, hemisphere, cone, box, circle, mesh");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: gravity\n    gravity: [0, 1]\n",
	             LRG_PARTICLE_ASSET_ERROR_PARSE, "/emitters/0/modules/0/gravity: expected a list of 3 numbers");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: color-over-life\n    gradient: 0 green\n",
	             LRG_PARTICLE_ASSET_ERROR_VALUE, "/emitters/0/modules/0/gradient: stop 0");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: alpha-over-life\n    curve: 0 1 bouncy\n",
	             LRG_PARTICLE_ASSET_ERROR_VALUE, "unknown easing 'bouncy'");
	check_error ("emitters:\n- name: a\n  renderer:\n    kind: sprite\n", LRG_PARTICLE_ASSET_ERROR_VALUE,
	             "/emitters/0/renderer/kind: expected one of none, billboard, ribbon, mesh");
	check_error ("emitters:\n- name: a\n- name: a\n", LRG_PARTICLE_ASSET_ERROR_REFERENCE,
	             "/emitters/1/name: 'a' is already the name of emitter 0");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: sub-emitter\n    emitter: b\n",
	             LRG_PARTICLE_ASSET_ERROR_REFERENCE, "/emitters/0/modules/0/emitter: no emitter called 'b'");
	check_error ("emitters:\n- name: a\n  modules:\n  - type: sub-emitter\n    emitter: a\n",
	             LRG_PARTICLE_ASSET_ERROR_REFERENCE, "cannot be its own sub-emitter");
	check_error ("[1, 2]\n", LRG_PARTICLE_ASSET_ERROR_PARSE, "must be a YAML mapping");
}

/* ==========================================================================
 * Simulation
 * ========================================================================== */

static void
test_determinism (void)
{
	g_autoptr(GError)            error = NULL;
	g_autoptr(LrgParticleAsset)  asset = lrg_particle_asset_new_from_data (FIXTURE, -1, NULL, &error);
	g_autoptr(LrgParticleEffect) a = NULL;
	g_autoptr(LrgParticleEffect) b = NULL;
	g_autoptr(LrgParticleEffect) c = NULL;
	Scan                         sa, sb, sc;

	g_assert_no_error (error);
	/* enable the smoke too: sub-emitters are part of the check */
	lrg_particle_asset_emitter_set_enabled (lrg_particle_asset_get_emitter (asset, 1), TRUE);
	a = lrg_particle_effect_new (asset);
	b = lrg_particle_effect_new (asset);
	c = lrg_particle_effect_new (asset);
	lrg_particle_effect_set_seed (c, 43);
	lrg_particle_effect_simulate (a, 0.5f, STEP);
	lrg_particle_effect_simulate (b, 0.5f, STEP);
	lrg_particle_effect_simulate (c, 0.5f, STEP);

	sa = scan (lrg_particle_effect_get_system (a, 0));
	sb = scan (lrg_particle_effect_get_system (b, 0));
	sc = scan (lrg_particle_effect_get_system (c, 0));
	g_assert_cmpuint (sa.count, >, 0);
	g_assert_cmpuint (sa.count, ==, sb.count);
	g_assert_cmpfloat (sa.last->position_x, ==, sb.last->position_x);
	g_assert_cmpfloat (sa.last->position_y, ==, sb.last->position_y);
	g_assert_cmpfloat (sa.last->velocity_z, ==, sb.last->velocity_z);
	g_assert_true (sa.count != sc.count || sa.last->position_x != sc.last->position_x);

	/* seeking replays exactly */
	lrg_particle_effect_seek (a, 0.5f, STEP);
	sa = scan (lrg_particle_effect_get_system (a, 0));
	g_assert_cmpuint (sa.count, ==, sb.count);
	g_assert_cmpfloat (sa.last->position_x, ==, sb.last->position_x);
	g_assert_cmpuint (lrg_particle_system_get_active_count (lrg_particle_effect_get_system (a, 1)), ==,
	                  lrg_particle_system_get_active_count (lrg_particle_effect_get_system (b, 1)));
}

static void
test_gravity_analytic (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = single_particle (&e, 10.0f);
	g_autoptr(LrgParticleEffect) fx = NULL;
	LrgParticleModule           *m = lrg_particle_asset_emitter_add_module (e, "gravity");
	Scan                         s;
	guint                        n;
	gdouble                      expect;

	vec3 (m, "gravity", 0.0f, -10.0f, 0.0f);
	fx = lrg_particle_effect_new (asset);
	lrg_particle_effect_play (fx);
	/* the spawn step already integrates (force, then move), so after
	 * N = 101 steps of semi-implicit Euler: y_N = g dt^2 N (N + 1) / 2 */
	lrg_particle_effect_update (fx, 0.01f);
	for (n = 0; n < 100; n++)
		lrg_particle_effect_update (fx, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpuint (s.count, ==, 1);
	expect = -10.0 * 0.0001 * 101.0 * 102.0 / 2.0;
	g_assert_cmpfloat_with_epsilon (s.last->position_y, (gfloat) expect, 1e-3);
	/* and the continuous answer within the integrator's O(dt) error */
	g_assert_cmpfloat_with_epsilon (s.last->position_y, -0.5f * 10.0f * 1.01f * 1.01f, 0.06f);
	g_assert_cmpfloat_with_epsilon (s.last->velocity_y, -10.0f * 1.01f, 1e-3);
}

static void
test_drag_analytic (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = single_particle (&e, 10.0f);
	g_autoptr(LrgParticleEffect) fx = NULL;
	LrgParticleModule           *m;
	Scan                         s;

	m = lrg_particle_asset_emitter_find_module (e, "velocity");
	range (m, "speed", 4, 4);
	vec3 (m, "direction", 1, 0, 0);
	m = lrg_particle_asset_emitter_add_module (e, "drag");
	g_object_set (m, "drag", 2.0f, NULL);
	fx = lrg_particle_effect_new (asset);
	lrg_particle_effect_simulate (fx, 0.5f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpuint (s.count, ==, 1);
	/* exact per step: v = v0 exp (-k t) */
	g_assert_cmpfloat_with_epsilon (s.last->velocity_x, 4.0f * expf (-2.0f * 0.5f), 1e-3);
}

static void
test_curves_and_gradients (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = single_particle (&e, 2.0f);
	g_autoptr(LrgParticleEffect) fx = NULL;
	g_autoptr(LrgKeyframeCurve)  curve = lrg_keyframe_curve_new ();
	g_autoptr(LrgColorGradient)  grad = NULL;
	g_autoptr(LrgGradientStops)  stops = NULL;
	LrgParticleModule           *m;
	Scan                         s;
	gfloat                       t;

	m = lrg_particle_asset_emitter_add_module (e, "size");
	range (m, "size", 2, 2);
	m = lrg_particle_asset_emitter_add_module (e, "size-over-life");
	lrg_keyframe_curve_add_key (curve, 0.0f, 1.0f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (curve, 1.0f, 3.0f, LRG_EASING_LINEAR);
	g_object_set (m, "curve", curve, "multiplier", 0.5f, NULL);
	m = lrg_particle_asset_emitter_add_module (e, "color-over-life");
	grad = lrg_color_gradient_new_from_string ("0 #ffffffff; 1 #00000000", NULL);
	stops = lrg_color_gradient_to_stops (grad);
	g_object_set (m, "gradient", stops, NULL);

	fx = lrg_particle_effect_new (asset);
	lrg_particle_effect_simulate (fx, 0.5f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpuint (s.count, ==, 1);
	t = s.last->age / s.last->max_life;
	/* size = start (2) x curve (1 + 2t) x multiplier (0.5) */
	g_assert_cmpfloat_with_epsilon (s.last->size, 2.0f * (1.0f + 2.0f * t) * 0.5f, 1e-4);
	g_assert_cmpfloat (s.last->start_size, ==, 2.0f);
	/* colour = white x (1 - t), within the table's step */
	g_assert_cmpfloat_with_epsilon (s.last->color_r, 1.0f - t, 1.0f / 255.0f);
	g_assert_cmpfloat_with_epsilon (s.last->color_a, 1.0f - t, 1.0f / 255.0f);
}

static void
test_bursts_and_rate (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = one_emitter (&e);
	g_autoptr(LrgParticleEffect) fx = NULL;
	LrgParticleModule           *m;
	LrgParticleSystem           *sys;

	m = lrg_particle_asset_emitter_add_module (e, "burst");
	g_object_set (m, "time", 0.5f, "cycles", 3u, "interval", 0.2f, NULL);
	range (m, "count", 25, 25);
	m = lrg_particle_asset_emitter_add_module (e, "lifetime");
	range (m, "lifetime", 100, 100);
	lrg_particle_asset_set_duration (asset, 2.0f);

	fx = lrg_particle_effect_new (asset);
	sys = lrg_particle_effect_get_system (fx, 0);
	lrg_particle_effect_simulate (fx, 0.49f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 0);
	lrg_particle_effect_simulate (fx, 0.02f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 25);
	lrg_particle_effect_simulate (fx, 0.5f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 75);
	/* the next cycle (looping, 2 s) re-arms the burst */
	lrg_particle_effect_simulate (fx, 1.5f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 100);

	/* a rate of 10/s for 1 s at 100 Hz: 10 particles */
	lrg_particle_asset_emitter_remove_module (e, 0);
	m = lrg_particle_asset_emitter_add_module (e, "spawn-rate");
	g_object_set (m, "rate", 10.0f, NULL);
	lrg_particle_effect_reload (fx);
	lrg_particle_effect_seek (fx, 1.0f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (lrg_particle_effect_get_system (fx, 0)), ==, 10);
}

static void
test_sub_emitters (void)
{
	LrgParticleAssetEmitter           *e;
	g_autoptr(LrgParticleAsset)        asset = single_particle (&e, 0.5f);
	g_autoptr(LrgParticleAssetEmitter) child = lrg_particle_asset_emitter_new ("child");
	g_autoptr(LrgParticleEffect)       fx = NULL;
	g_autoptr(GError)                  error = NULL;
	LrgParticleModule                 *m;
	Scan                               s;

	range (lrg_particle_asset_emitter_find_module (e, "burst"), "count", 4, 4);
	m = lrg_particle_asset_emitter_find_module (e, "velocity");
	range (m, "speed", 2, 2);
	vec3 (m, "direction", 1, 0, 0);
	m = lrg_particle_asset_emitter_add_module (e, "sub-emitter");
	g_object_set (m, "emitter", "child", "inherit-velocity", 1.0f, NULL);
	range (m, "count", 5, 5);
	lrg_particle_asset_add_emitter (asset, child);
	m = lrg_particle_asset_emitter_add_module (child, "lifetime");
	range (m, "lifetime", 10, 10);
	g_assert_true (lrg_particle_asset_validate (asset, &error));

	fx = lrg_particle_effect_new (asset);
	lrg_particle_effect_simulate (fx, 0.45f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (lrg_particle_effect_get_system (fx, 1)), ==, 0);
	lrg_particle_effect_simulate (fx, 0.1f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (lrg_particle_effect_get_system (fx, 0)), ==, 0);
	g_assert_cmpuint (lrg_particle_system_get_active_count (lrg_particle_effect_get_system (fx, 1)), ==, 20);

	/* children start where the parents died (x ~ 1) moving on with them */
	s = scan (lrg_particle_effect_get_system (fx, 1));
	g_assert_cmpfloat (s.last->position_x, >, 0.9f);
	g_assert_cmpfloat_with_epsilon (s.last->velocity_x, 2.0f, 1e-4);
}

static void
test_collision (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = single_particle (&e, 10.0f);
	g_autoptr(LrgParticleEffect) fx = NULL;
	LrgParticleModule           *m;
	LrgParticleSystem           *sys;
	gfloat                       min_y = 100.0f;
	gboolean                     bounced = FALSE;
	guint                        i;

	m = lrg_particle_asset_emitter_find_module (e, "shape");
	vec3 (m, "offset", 0, 2, 0);
	lrg_particle_asset_emitter_add_module (e, "gravity");
	m = lrg_particle_asset_emitter_add_module (e, "collision");
	g_object_set (m, "bounce", 0.5f, "offset", 0.0f, NULL);

	fx = lrg_particle_effect_new (asset);
	sys = lrg_particle_effect_get_system (fx, 0);
	lrg_particle_effect_play (fx);
	for (i = 0; i < 200; i++)
	{
		Scan s;

		lrg_particle_effect_update (fx, 0.01f);
		s = scan (sys);
		g_assert_cmpuint (s.count, ==, 1);
		min_y = MIN (min_y, s.last->position_y);
		if (s.last->collisions > 0 && s.last->velocity_y > 0.0f)
			bounced = TRUE;
	}
	g_assert_cmpfloat (min_y, >=, 0.0f);
	g_assert_true (bounced);

	/* kill on hit */
	g_object_set (m, "kill", TRUE, NULL);
	lrg_particle_effect_reload (fx);
	lrg_particle_effect_seek (fx, 1.0f, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 0);
}

static void
test_shapes (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = one_emitter (&e);
	g_autoptr(LrgParticleEffect) fx = NULL;
	LrgParticleModule           *shape;
	LrgParticleModule           *m;
	Scan                         s;
	static const gfloat          tri[9] = { 0, 0, 0, 2, 0, 0, 0, 0, 2 };

	m = lrg_particle_asset_emitter_add_module (e, "burst");
	range (m, "count", 200, 200);
	shape = lrg_particle_asset_emitter_add_module (e, "shape");
	g_object_set (shape, "shape", LRG_PARTICLE_SHAPE_SPHERE, "radius", 2.0f, NULL);
	m = lrg_particle_asset_emitter_add_module (e, "velocity");
	range (m, "speed", 0, 0);
	m = lrg_particle_asset_emitter_add_module (e, "lifetime");
	range (m, "lifetime", 5, 5);

	/* sphere: inside the radius */
	fx = lrg_particle_effect_new (asset);
	lrg_particle_effect_simulate (fx, 0.02f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpuint (s.count, ==, 200);
	g_assert_cmpfloat (s.max_dist, <=, 2.0001f);

	/* sphere surface (thickness 0): on the radius */
	g_object_set (shape, "thickness", 0.0f, NULL);
	lrg_particle_effect_reload (fx);
	lrg_particle_effect_seek (fx, 0.02f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpfloat_with_epsilon (s.max_dist, 2.0f, 1e-4);

	/* hemisphere: nothing below y = 0 */
	g_object_set (shape, "shape", LRG_PARTICLE_SHAPE_HEMISPHERE, NULL);
	lrg_particle_effect_reload (fx);
	lrg_particle_effect_seek (fx, 0.02f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpfloat (s.min_y, >=, -1e-5f);

	/* mesh surface: every point on the triangle (y = 0, x + z <= 2) */
	g_object_set (shape, "shape", LRG_PARTICLE_SHAPE_MESH, NULL);
	g_assert_true (lrg_particle_module_set_surface (shape, tri, 9, NULL, 0));
	g_assert_false (lrg_particle_module_set_surface (m, tri, 9, NULL, 0));
	g_assert_false (lrg_particle_module_get_gpu_capable (shape));
	lrg_particle_effect_reload (fx);
	lrg_particle_effect_seek (fx, 0.02f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpuint (s.count, ==, 200);
	g_assert_cmpfloat_with_epsilon (s.last->position_y, 0.0f, 1e-5);
	g_assert_cmpfloat (s.last->position_x + s.last->position_z, <=, 2.0001f);
	g_assert_cmpfloat (s.last->position_x, >=, -1e-5f);
}

static void
test_module_kinds (void)
{
	g_auto(GStrv) kinds = lrg_particle_module_list_kinds ();
	guint         i;

	g_assert_cmpuint (g_strv_length (kinds), ==, 20);
	g_assert_cmpstr (kinds[0], ==, "spawn-rate");
	for (i = 0; kinds[i] != NULL; i++)
	{
		g_autoptr(LrgParticleModule) m = lrg_particle_module_new (kinds[i]);
		g_autoptr(LrgParticleModule) copy = NULL;

		g_assert_nonnull (m);
		g_assert_cmpstr (lrg_particle_module_get_kind (m), ==, kinds[i]);
		copy = lrg_particle_module_copy (m);
		g_assert_true (G_OBJECT_TYPE (copy) == G_OBJECT_TYPE (m));
		g_assert_cmpint (lrg_particle_module_get_stage (copy), ==, lrg_particle_module_get_stage (m));
		if (g_strcmp0 (kinds[i], "sub-emitter") == 0)
			g_assert_false (lrg_particle_module_get_gpu_capable (m));
		else
			g_assert_true (lrg_particle_module_get_gpu_capable (m));
	}
	g_assert_null (lrg_particle_module_new ("nope"));
	g_assert_true (lrg_particle_module_lookup_kind ("nope") == G_TYPE_INVALID);
}

static void
test_reload (void)
{
	LrgParticleAssetEmitter     *e;
	g_autoptr(LrgParticleAsset)  asset = single_particle (&e, 10.0f);
	g_autoptr(LrgParticleEffect) fx = lrg_particle_effect_new (asset);
	LrgParticleModule           *gravity;
	Scan                         s;

	lrg_particle_effect_set_auto_reload (fx, TRUE);
	lrg_particle_effect_simulate (fx, 0.1f, 0.01f);
	g_assert_cmpuint (lrg_particle_effect_get_active_count (fx), ==, 1);

	/* an edit reloads live: the particle stays, the burst does not refire */
	gravity = lrg_particle_asset_emitter_add_module (e, "gravity");
	lrg_particle_effect_simulate (fx, 0.1f, 0.01f);
	s = scan (lrg_particle_effect_get_system (fx, 0));
	g_assert_cmpuint (s.count, ==, 1);
	g_assert_cmpfloat (s.last->velocity_y, <, 0.0f);
	vec3 (gravity, "gravity", 0, 0, 0);
	g_assert_cmpuint (lrg_particle_effect_get_active_count (fx), ==, 1);

	/* a structural change rebuilds and restarts */
	{
		g_autoptr(LrgParticleAssetEmitter) other = lrg_particle_asset_emitter_new ("other");

		lrg_particle_asset_add_emitter (asset, other);
	}
	g_assert_cmpuint (lrg_particle_effect_get_n_systems (fx), ==, 2);
	g_assert_true (lrg_particle_effect_is_playing (fx));
}

static void
on_finished (LrgParticleEffect *fx,
             gpointer           user_data)
{
	(void) fx;
	(*(guint *) user_data)++;
}

static void
test_finished (void)
{
	g_autoptr(LrgParticleAsset)  asset = single_particle (NULL, 0.2f);
	g_autoptr(LrgParticleEffect) fx = NULL;
	guint                        finished = 0;

	lrg_particle_asset_set_loop (asset, FALSE);
	lrg_particle_asset_set_duration (asset, 0.1f);
	fx = lrg_particle_effect_new (asset);
	g_signal_connect (fx, "finished", G_CALLBACK (on_finished), &finished);
	lrg_particle_effect_simulate (fx, 1.0f, 0.01f);
	g_assert_cmpuint (finished, ==, 1);
	g_assert_false (lrg_particle_effect_is_playing (fx));
	g_assert_false (lrg_particle_effect_is_alive (fx));
}

/* ==========================================================================
 * Renderers (headless geometry)
 * ========================================================================== */

static void
test_ribbon_trail (void)
{
	g_autoptr(LrgParticleSystem)         sys = lrg_particle_system_new (8);
	g_autoptr(LrgParticleRibbonRenderer) r = lrg_particle_ribbon_renderer_new ();
	g_autoptr(GArray)                    v = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgParticleModule)         burst = lrg_particle_module_new ("burst");
	g_autoptr(LrgParticleModule)         shape = lrg_particle_module_new ("shape");
	g_autoptr(LrgParticleModule)         vel = lrg_particle_module_new ("velocity");
	g_autoptr(LrgParticleModule)         life = lrg_particle_module_new ("lifetime");
	static const gfloat                  eye[3] = { 0, 10, 0 };
	guint                                quads, i;
	const gfloat                        *q;

	range (burst, "count", 1, 1);
	g_object_set (shape, "shape", LRG_PARTICLE_SHAPE_POINT, NULL);
	range (vel, "speed", 1, 1);
	vec3 (vel, "direction", 1, 0, 0);
	range (life, "lifetime", 0.5f, 0.5f);
	lrg_particle_system_add_module (sys, burst);
	lrg_particle_system_add_module (sys, shape);
	lrg_particle_system_add_module (sys, vel);
	lrg_particle_system_add_module (sys, life);
	lrg_particle_ribbon_renderer_set_min_distance (r, 0.1f);
	lrg_particle_ribbon_renderer_set_trail_lifetime (r, 0.3f);
	lrg_particle_ribbon_renderer_set_width (r, 0.5f);
	lrg_particle_system_play (sys);

	/* x moves 0.05 per step: a point every second step (0.4 s here) */
	for (i = 0; i < 8; i++)
	{
		lrg_particle_system_update (sys, 0.05f);
		lrg_particle_ribbon_renderer_update (r, sys, 0.05f);
	}
	g_assert_cmpuint (lrg_particle_ribbon_renderer_get_n_trails (r), ==, 1);
	/* points live 0.3 s, spaced 0.1 s: at most 3, plus the live head */
	g_assert_cmpuint (lrg_particle_ribbon_renderer_get_n_points (r), <=, 3);
	quads = lrg_particle_ribbon_renderer_build (r, sys, eye, v);
	g_assert_cmpuint (quads, >=, 2);
	g_assert_cmpuint (v->len, ==, quads * LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD);

	/* seen from above, a strip along x is 0.5 wide in z; stretched u
	 * runs 0 at the head to 1 at the tail */
	q = (const gfloat *) (const void *) v->data;
	g_assert_cmpfloat_with_epsilon (fabsf (q[2] - q[9 + 2]), 0.5f, 1e-4);
	g_assert_cmpfloat (q[3], ==, 0.0f);
	q = &g_array_index (v, gfloat, (quads - 1) * LRG_PARTICLE_RIBBON_FLOATS_PER_QUAD);
	g_assert_cmpfloat_with_epsilon (q[18 + 3], 1.0f, 1e-5);

	/* the particle dies (0.5 s): the trail fades within 0.3 s more */
	for (i = 0; i < 12; i++)
	{
		lrg_particle_system_update (sys, 0.05f);
		lrg_particle_ribbon_renderer_update (r, sys, 0.05f);
	}
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 0);
	g_assert_cmpuint (lrg_particle_ribbon_renderer_get_n_trails (r), ==, 0);
}

static void
test_ribbon_connect (void)
{
	g_autoptr(LrgParticleSystem)         sys = lrg_particle_system_new (8);
	g_autoptr(LrgParticleRibbonRenderer) r = lrg_particle_ribbon_renderer_new ();
	g_autoptr(GArray)                    v = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autoptr(LrgParticleModule)         rate = lrg_particle_module_new ("spawn-rate");
	g_autoptr(LrgParticleModule)         shape = lrg_particle_module_new ("shape");
	g_autoptr(LrgParticleModule)         vel = lrg_particle_module_new ("velocity");
	static const gfloat                  eye[3] = { 0, 0, 10 };
	guint                                quads, i;
	const gfloat                        *q;

	g_object_set (rate, "rate", 10.0f, NULL);
	g_object_set (shape, "shape", LRG_PARTICLE_SHAPE_POINT, NULL);
	vec3 (vel, "direction", 0, 1, 0);
	lrg_particle_system_add_module (sys, rate);
	lrg_particle_system_add_module (sys, shape);
	lrg_particle_system_add_module (sys, vel);
	lrg_particle_ribbon_renderer_set_mode (r, LRG_PARTICLE_RIBBON_CONNECT);
	lrg_particle_ribbon_renderer_set_uv_mode (r, LRG_PARTICLE_RIBBON_UV_PER_SEGMENT);
	lrg_particle_system_play (sys);
	for (i = 0; i < 30; i++)
		lrg_particle_system_update (sys, 0.01f);
	g_assert_cmpuint (lrg_particle_system_get_active_count (sys), ==, 3);
	quads = lrg_particle_ribbon_renderer_build (r, sys, eye, v);
	g_assert_cmpuint (quads, ==, 2);

	/* head = the newest particle (lowest y); per-segment u is 0 .. 1 */
	q = (const gfloat *) (const void *) v->data;
	g_assert_cmpfloat (q[1], <, q[18 + 1]);
	g_assert_cmpfloat (q[3], ==, 0.0f);
	g_assert_cmpfloat (q[18 + 3], ==, 1.0f);
}

static void
test_mesh_build (void)
{
	g_autoptr(LrgParticleSystem)       sys = lrg_particle_system_new (8);
	g_autoptr(LrgParticleMeshRenderer) r = lrg_particle_mesh_renderer_new ();
	g_autoptr(LrgParticleModule)       burst = lrg_particle_module_new ("burst");
	g_autoptr(LrgParticleModule)       shape = lrg_particle_module_new ("shape");
	g_autoptr(LrgParticleModule)       size = lrg_particle_module_new ("size");
	g_autoptr(LrgParticleModule)       vel = lrg_particle_module_new ("velocity");
	gfloat                             inst[8 * LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE];
	guint                              n;
	gfloat                             len;

	range (burst, "count", 3, 3);
	g_object_set (shape, "shape", LRG_PARTICLE_SHAPE_POINT, NULL);
	vec3 (shape, "offset", 1, 2, 3);
	range (size, "size", 2, 2);
	range (vel, "speed", 0, 0);
	lrg_particle_system_add_module (sys, burst);
	lrg_particle_system_add_module (sys, shape);
	lrg_particle_system_add_module (sys, size);
	lrg_particle_system_add_module (sys, vel);
	lrg_particle_system_play (sys);
	lrg_particle_system_update (sys, 0.01f);
	lrg_particle_mesh_renderer_set_scale (r, 0.5f, 1.0f, 0.25f);

	n = lrg_particle_mesh_renderer_build (r, sys, inst, 8);
	g_assert_cmpuint (n, ==, 3);
	/* translation, per-axis scale (size x renderer scale), colour */
	g_assert_cmpfloat (inst[12], ==, 1.0f);
	g_assert_cmpfloat (inst[13], ==, 2.0f);
	g_assert_cmpfloat (inst[14], ==, 3.0f);
	g_assert_cmpfloat (inst[15], ==, 1.0f);
	len = sqrtf (inst[0] * inst[0] + inst[1] * inst[1] + inst[2] * inst[2]);
	g_assert_cmpfloat_with_epsilon (len, 1.0f, 1e-5);
	len = sqrtf (inst[8] * inst[8] + inst[9] * inst[9] + inst[10] * inst[10]);
	g_assert_cmpfloat_with_epsilon (len, 0.5f, 1e-5);
	g_assert_cmpfloat (inst[16], ==, 1.0f);
	g_assert_cmpfloat (inst[19], ==, 1.0f);
	g_assert_cmpuint (lrg_particle_mesh_renderer_build (r, sys, inst, 2), ==, 2);
}

/* ==========================================================================
 * Component
 * ========================================================================== */

static void
test_component (void)
{
	g_autoptr(LrgParticleAsset)     asset = single_particle (NULL, 10.0f);
	g_autoptr(LrgWorld3D)           world = lrg_world3d_new ();
	g_autoptr(LrgEntity3D)          entity = lrg_entity3d_new ();
	g_autoptr(LrgParticleComponent) comp = lrg_particle_component_new_with_asset (asset);
	g_autoptr(LrgRenderList)        list = lrg_render_list_new ();
	LrgParticleEffect              *fx;
	gfloat                          pos[3] = { 5, 1, -2 };
	gfloat                          lo[3], hi[3];

	lrg_entity3d_set_position (entity, pos);
	lrg_entity3d_add_component (entity, LRG_COMPONENT (comp));
	lrg_world3d_add_entity (world, entity);
	lrg_world3d_update (world, 0.02f);

	fx = lrg_particle_component_get_effect (comp);
	g_assert_nonnull (fx);
	g_assert_true (lrg_particle_effect_is_playing (fx));
	g_assert_cmpuint (lrg_particle_effect_get_active_count (fx), ==, 1);
	/* the particle was born at the entity */
	g_assert_true (lrg_particle_effect_get_bounds (fx, lo, hi));
	g_assert_cmpfloat_with_epsilon ((lo[0] + hi[0]) * 0.5f, 5.0f, 1e-4);

	/* one transparent custom item per drawing emitter */
	lrg_render_list_clear (list);
	lrg_component_collect_render (LRG_COMPONENT (comp), list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_TRANSPARENT), ==, 1);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_OPAQUE), ==, 0);

	lrg_particle_component_stop (comp);
	g_assert_cmpuint (lrg_particle_effect_get_active_count (fx), ==, 0);
	lrg_render_list_clear (list);
	lrg_component_collect_render (LRG_COMPONENT (comp), list);
	g_assert_cmpuint (lrg_render_list_get_n_items (list, LRG_RENDER_CATEGORY_TRANSPARENT), ==, 0);
}

/* ==========================================================================
 * GPU draws (skipped without a display)
 * ========================================================================== */

static gboolean
have_display (void)
{
	return g_getenv ("DISPLAY") != NULL || g_getenv ("WAYLAND_DISPLAY") != NULL;
}

static const gchar *GPU_FX =
	"version: 1\n"
	"seed: 3\n"
	"emitters:\n"
	"- name: dots\n"
	"  modules:\n"
	"  - type: burst\n"
	"    count: 50\n"
	"  - type: shape\n"
	"    radius: 0.5\n"
	"  - type: velocity\n"
	"    speed: 0\n"
	"  - type: lifetime\n"
	"    lifetime: 10\n"
	"  - type: size\n"
	"    size: 0.6\n"
	"  - type: color\n"
	"    color: '#ff0000'\n"
	"- name: trail\n"
	"  renderer:\n"
	"    kind: ribbon\n"
	"    width: 0.4\n"
	"  modules:\n"
	"  - type: spawn-rate\n"
	"    rate: 30\n"
	"  - type: shape\n"
	"    shape: point\n"
	"  - type: velocity\n"
	"    direction:\n"
	"    - 1\n"
	"    - 0.2\n"
	"    - 0\n"
	"    speed: 2\n"
	"  - type: color\n"
	"    color: '#00ff00'\n"
	"- name: debris\n"
	"  renderer:\n"
	"    kind: mesh\n"
	"  modules:\n"
	"  - type: burst\n"
	"    count: 10\n"
	"  - type: shape\n"
	"    radius: 1\n"
	"    offset:\n"
	"    - -1.5\n"
	"    - 0\n"
	"    - 0\n"
	"  - type: velocity\n"
	"    speed: 0\n"
	"  - type: size\n"
	"    size: 0.3\n"
	"  - type: lifetime\n"
	"    lifetime: 10\n"
	"  - type: color\n"
	"    color: '#0000ff'\n";

static void
test_gpu_draw (void)
{
	g_autoptr(GError)            error = NULL;
	g_autoptr(LrgParticleAsset)  asset = NULL;
	g_autoptr(LrgParticleEffect) fx = NULL;
	RenderTexture2D              rt;
	Camera3D                     cam;
	Image                        img;
	gint                         x, y;
	guint                        red = 0, green = 0, blue = 0;

	if (!have_display ())
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (320, 240, "test-particle-asset");
	if (!IsWindowReady ())
	{
		g_test_skip ("No GL context");
		return;
	}

	asset = lrg_particle_asset_new_from_data (GPU_FX, -1, "gpu.rparticles", &error);
	g_assert_no_error (error);
	fx = lrg_particle_effect_new (asset);
	lrg_particle_effect_simulate (fx, 0.5f, STEP);

	rt = LoadRenderTexture (320, 240);
	memset (&cam, 0, sizeof cam);
	cam.position = (Vector3) { 0.0f, 0.5f, 6.0f };
	cam.target = (Vector3) { 0.0f, 0.0f, 0.0f };
	cam.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	cam.fovy = 50.0f;
	cam.projection = CAMERA_PERSPECTIVE;
	BeginTextureMode (rt);
	ClearBackground (BLACK);
	BeginMode3D (cam);
	{
		gfloat eye[3] = { 0.0f, 0.5f, 6.0f };
		gfloat up[3] = { 0.0f, 1.0f, 0.0f };

		lrg_particle_effect_draw (fx, eye, up);
	}
	EndMode3D ();
	EndTextureMode ();

	img = LoadImageFromTexture (rt.texture);
	for (y = 0; y < img.height; y++)
		for (x = 0; x < img.width; x++)
		{
			Color c = GetImageColor (img, x, y);

			red += c.r > 120 && c.g < 60 && c.b < 60;
			green += c.g > 120 && c.r < 60;
			blue += c.b > 60 && c.r < 60 && c.g < 60;
		}
	UnloadImage (img);
	/* billboards, the ribbon and the instanced meshes all reached pixels */
	g_assert_cmpuint (red, >, 50);
	g_assert_cmpuint (green, >, 50);
	g_assert_cmpuint (blue, >, 50);

	lrg_particle_effect_release_gpu (fx);
	UnloadRenderTexture (rt);
	CloseWindow ();
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/particle-asset/gradient", test_gradient_evaluate);
	g_test_add_func ("/particle-asset/rng", test_rng);
	g_test_add_func ("/particle-asset/round-trip", test_round_trip);
	g_test_add_func ("/particle-asset/file-io", test_file_io);
	g_test_add_func ("/particle-asset/edit-and-save", test_edit_and_save);
	g_test_add_func ("/particle-asset/validation", test_validation);
	g_test_add_func ("/particle-asset/module-kinds", test_module_kinds);
	g_test_add_func ("/particle-asset/determinism", test_determinism);
	g_test_add_func ("/particle-asset/gravity-analytic", test_gravity_analytic);
	g_test_add_func ("/particle-asset/drag-analytic", test_drag_analytic);
	g_test_add_func ("/particle-asset/curves-gradients", test_curves_and_gradients);
	g_test_add_func ("/particle-asset/bursts-rate", test_bursts_and_rate);
	g_test_add_func ("/particle-asset/sub-emitters", test_sub_emitters);
	g_test_add_func ("/particle-asset/collision", test_collision);
	g_test_add_func ("/particle-asset/shapes", test_shapes);
	g_test_add_func ("/particle-asset/reload", test_reload);
	g_test_add_func ("/particle-asset/finished", test_finished);
	g_test_add_func ("/particle-asset/ribbon-trail", test_ribbon_trail);
	g_test_add_func ("/particle-asset/ribbon-connect", test_ribbon_connect);
	g_test_add_func ("/particle-asset/mesh-build", test_mesh_build);
	g_test_add_func ("/particle-asset/component", test_component);
	g_test_add_func ("/particle-asset/gpu-draw", test_gpu_draw);

	return g_test_run ();
}
