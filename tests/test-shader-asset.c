/* test-shader-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgShaderAsset (`.rshader`): the format (round trip, stage
 * paths, unknown keys, errors), uniform values, the generated preamble
 * (contract declarations, declarations the user or library already has,
 * the lit library through the test seam and the real light-set library),
 * info-log parsing with line mapping, and, with a display, compiling,
 * compile errors with user line numbers, keep-last-good, uniform upload
 * through a render, and hot reload from a temporary directory.
 */

#include <glib.h>
#include <glib/gstdio.h>
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

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (16, 16, "lrg-shader-asset-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

#define SKIP_WITHOUT_GRAPHICS() \
	do { \
		if (!graphics_available) \
		{ \
			g_test_skip ("No display available (headless environment)"); \
			return; \
		} \
	} while (0)

/* the writer's own layout: load + save must give back these bytes */
static const gchar fixture[] =
	"version: 1\n"
	"name: dissolve\n"
	"lit: true\n"
	"fragment: |\n"
	"  void main()\n"
	"  {\n"
	"      finalColor = vec4(tint.rgb * edge, 1.0);\n"
	"  }\n"
	"render_state:\n"
	"  blend: cutout\n"
	"  cull: none\n"
	"  depth_write: false\n"
	"  alpha_cutoff: 0.25\n"
	"uniforms:\n"
	"  tint:\n"
	"    type: color\n"
	"    default:\n"
	"    - 1\n"
	"    - 0.5\n"
	"    - 0.25\n"
	"    - 1\n"
	"    hint: color\n"
	"  edge:\n"
	"    type: float\n"
	"    default: 0.1\n"
	"    range:\n"
	"    - 0\n"
	"    - 1\n"
	"    hint: slider\n"
	"  steps:\n"
	"    type: int\n"
	"    default: 3\n"
	"  wobble:\n"
	"    type: bool\n"
	"    default: true\n"
	"  noise_tex:\n"
	"    type: sampler2D\n"
	"    default: textures/noise.png\n"
	"editor_notes: keep me\n";

static void
test_format_round_trip (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = NULL;
	g_autoptr(LrgShaderAsset) again = NULL;
	g_autofree gchar *saved = NULL;
	g_autofree gchar *saved_again = NULL;
	const LrgShaderUniform *u;
	gfloat v[4];
	gfloat lo, hi;

	asset = lrg_shader_asset_new_from_data (fixture, -1, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_shader_asset_get_name (asset), ==, "dissolve");
	g_assert_true (lrg_shader_asset_get_lit (asset));
	g_assert_cmpstr (lrg_shader_asset_get_blend_mode (asset), ==, "cutout");
	g_assert_cmpstr (lrg_shader_asset_get_cull_mode (asset), ==, "none");
	g_assert_true (lrg_shader_asset_get_depth_test (asset));
	g_assert_false (lrg_shader_asset_get_depth_write (asset));
	g_assert_cmpfloat (lrg_shader_asset_get_alpha_cutoff (asset), ==, 0.25f);
	g_assert_null (lrg_shader_asset_get_source (asset, LRG_SHADER_STAGE_VERTEX));
	g_assert_nonnull (strstr (lrg_shader_asset_get_source (asset, LRG_SHADER_STAGE_FRAGMENT),
	                          "tint.rgb * edge"));

	/* uniforms in document order with their descriptions */
	g_assert_cmpuint (lrg_shader_asset_get_n_uniforms (asset), ==, 5);
	u = lrg_shader_asset_get_uniform_at (asset, 0);
	g_assert_cmpstr (lrg_shader_uniform_get_name (u), ==, "tint");
	g_assert_cmpint (lrg_shader_uniform_get_uniform_type (u), ==, LRG_SHADER_UNIFORM_COLOR);
	lrg_shader_uniform_get_default (u, v);
	g_assert_cmpfloat (v[1], ==, 0.5f);
	g_assert_cmpstr (lrg_shader_uniform_get_hint (u), ==, "color");
	u = lrg_shader_asset_get_uniform (asset, "edge");
	g_assert_true (lrg_shader_uniform_get_range (u, &lo, &hi));
	g_assert_cmpfloat (hi, ==, 1.0f);
	u = lrg_shader_asset_get_uniform (asset, "noise_tex");
	g_assert_cmpstr (lrg_shader_uniform_get_default_texture (u), ==, "textures/noise.png");
	g_assert_true (lrg_shader_asset_get_uniform_value (asset, "wobble", v));
	g_assert_cmpfloat (v[0], ==, 1.0f);

	/* byte-stable */
	saved = lrg_shader_asset_save_to_data (asset, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (saved, ==, fixture);
	again = lrg_shader_asset_new_from_data (saved, -1, NULL, &error);
	g_assert_no_error (error);
	saved_again = lrg_shader_asset_save_to_data (again, NULL, &error);
	g_assert_cmpstr (saved_again, ==, fixture);
}

static void
test_format_built_in_code (void)
{
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new ();
	g_autoptr(LrgShaderUniform) u = lrg_shader_uniform_new ("speed", LRG_SHADER_UNIFORM_FLOAT);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	gfloat two = 2.0f;

	lrg_shader_asset_set_name (asset, "wave");
	lrg_shader_asset_set_source (asset, LRG_SHADER_STAGE_FRAGMENT,
	                             "void main()\n{\n    finalColor = vec4(speed);\n}\n");
	lrg_shader_uniform_set_default (u, &two, 1);
	lrg_shader_asset_add_uniform (asset, u);
	g_assert_true (lrg_shader_asset_set_blend_mode (asset, "additive"));
	g_assert_false (lrg_shader_asset_set_blend_mode (asset, "screen"));
	text = lrg_shader_asset_save_to_data (asset, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==,
	                 "version: 1\n"
	                 "name: wave\n"
	                 "fragment: |\n"
	                 "  void main()\n"
	                 "  {\n"
	                 "      finalColor = vec4(speed);\n"
	                 "  }\n"
	                 "render_state:\n"
	                 "  blend: additive\n"
	                 "uniforms:\n"
	                 "  speed:\n"
	                 "    type: float\n"
	                 "    default: 2\n");
}

static void
test_format_stage_paths (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-shader-XXXXXX", &error);
	g_autofree gchar *doc = NULL;
	g_autofree gchar *frag = NULL;
	g_autofree gchar *saved = NULL;
	g_autoptr(LrgShaderAsset) asset = NULL;
	g_autoptr(LrgShaderAsset) missing = NULL;

	g_assert_no_error (error);
	doc = g_build_filename (dir, "glow.rshader", NULL);
	frag = g_build_filename (dir, "glow.frag", NULL);
	g_assert_true (g_file_set_contents (frag, "void main() { finalColor = vec4(1.0); }\n", -1, NULL));
	g_assert_true (g_file_set_contents (doc, "version: 1\nname: glow\nfragment: glow.frag\n", -1, NULL));

	asset = lrg_shader_asset_new_from_file (doc, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_shader_asset_get_path (asset), ==, doc);
	g_assert_cmpstr (lrg_shader_asset_get_source_path (asset, LRG_SHADER_STAGE_FRAGMENT), ==, "glow.frag");
	g_assert_cmpstr (lrg_shader_asset_get_source (asset, LRG_SHADER_STAGE_FRAGMENT), ==,
	                 "void main() { finalColor = vec4(1.0); }\n");
	saved = lrg_shader_asset_save_to_data (asset, NULL, &error);
	g_assert_cmpstr (saved, ==, "version: 1\nname: glow\nfragment: glow.frag\n");

	/* a missing stage file is an I/O error naming the file */
	g_assert_true (g_file_set_contents (doc, "version: 1\nname: glow\nfragment: nope.frag\n", -1, NULL));
	missing = lrg_shader_asset_new_from_file (doc, &error);
	g_assert_null (missing);
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_IO);
	g_assert_nonnull (strstr (error->message, "nope.frag"));

	g_unlink (frag);
	g_unlink (doc);
	g_rmdir (dir);
}

/* loads @text, expecting a parse error mentioning @needle */
static void
expect_parse_error (const gchar *text,
                    const gchar *needle)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new_from_data (text, -1, NULL, &error);

	g_assert_null (asset);
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_PARSE);
	if (strstr (error->message, needle) == NULL)
		g_error ("expected '%s' in: %s", needle, error->message);
}

static void
test_format_errors (void)
{
	expect_parse_error ("version: 1\nname: x\n", "/fragment");
	expect_parse_error ("version: 2\nfragment: |\n  x\n  y\n", "/version");
	expect_parse_error ("fragment: |\n  a\n  b\nuniforms:\n  t:\n    type: colour\n",
	                    "/uniforms/t");
	expect_parse_error ("fragment: |\n  a\n  b\nuniforms:\n  t:\n    type: vec3\n    default: [1, 2]\n",
	                    "'default' needs 3 numbers");
	expect_parse_error ("fragment: |\n  a\n  b\nuniforms:\n  t:\n    type: float\n    defualt: 1\n",
	                    "unknown key 'defualt'");
	expect_parse_error ("fragment: |\n  a\n  b\nuniforms:\n  2bad:\n    type: float\n",
	                    "not a valid GLSL identifier");
	expect_parse_error ("fragment: |\n  a\n  b\nrender_state:\n  blend: screen\n", "/render_state/blend");
	expect_parse_error ("fragment: |\n  a\n  b\nuniforms:\n  t:\n    type: float\n    range: [1, 0]\n",
	                    "'range'");
}

static void
test_uniform_values (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new_from_data (fixture, -1, NULL, &error);
	gfloat v[4];

	g_assert_no_error (error);
	g_assert_true (lrg_shader_asset_set_uniform_vec4 (asset, "tint", 0.0f, 1.0f, 0.0f, 1.0f));
	g_assert_true (lrg_shader_asset_set_uniform_float (asset, "edge", 0.75f));
	g_assert_true (lrg_shader_asset_set_uniform_int (asset, "steps", 7));
	/* wrong arity or type */
	g_assert_false (lrg_shader_asset_set_uniform_float (asset, "tint", 1.0f));
	g_assert_false (lrg_shader_asset_set_uniform_vec3 (asset, "tint", 1.0f, 1.0f, 1.0f));
	g_assert_false (lrg_shader_asset_set_uniform_float (asset, "nope", 1.0f));
	g_assert_false (lrg_shader_asset_set_uniform_float (asset, "noise_tex", 1.0f));
	g_assert_false (lrg_shader_asset_set_uniform_texture (asset, "edge", NULL));
	g_assert_true (lrg_shader_asset_set_uniform_texture (asset, "noise_tex", NULL));

	g_assert_true (lrg_shader_asset_get_uniform_value (asset, "tint", v));
	g_assert_cmpfloat (v[1], ==, 1.0f);
	g_assert_true (lrg_shader_asset_get_uniform_value (asset, "steps", v));
	g_assert_cmpfloat (v[0], ==, 7.0f);
	g_assert_false (lrg_shader_asset_get_uniform_value (asset, "nope", v));

	lrg_shader_asset_reset_uniforms (asset);
	lrg_shader_asset_get_uniform_value (asset, "edge", v);
	g_assert_cmpfloat (v[0], ==, 0.1f);

	/* removing a uniform drops its value too */
	g_assert_true (lrg_shader_asset_remove_uniform (asset, "edge"));
	g_assert_false (lrg_shader_asset_get_uniform_value (asset, "edge", v));
	g_assert_cmpuint (lrg_shader_asset_get_n_uniforms (asset), ==, 4);

	/* uploading needs a program */
	g_assert_false (lrg_shader_asset_apply (asset, NULL));
}

/* counts the lines of @text before @needle */
static guint
line_of (const gchar *text,
         const gchar *needle)
{
	const gchar *at = strstr (text, needle);
	const gchar *p;
	guint line = 1;

	g_assert_nonnull (at);
	for (p = text; p < at; p++)
		if (*p == '\n')
			line++;
	return line;
}

static void
test_preamble (void)
{
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new ();
	g_autoptr(LrgShaderUniform) u = lrg_shader_uniform_new ("tint", LRG_SHADER_UNIFORM_COLOR);
	g_autofree gchar *fs = NULL;
	g_autofree gchar *vs = NULL;
	guint preamble = 0;

	lrg_shader_asset_add_uniform (asset, u);
	/* the user declares fragNormal and finalColor themselves, and keeps a
	 * #version line: neither may be repeated */
	lrg_shader_asset_set_source (asset, LRG_SHADER_STAGE_FRAGMENT,
	                             "#version 330\n"
	                             "in vec3 fragNormal;\n"
	                             "out vec4 finalColor;\n"
	                             "void main() { finalColor = tint; }\n");
	fs = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_FRAGMENT, &preamble);
	g_assert_true (g_str_has_prefix (fs, "#version 330\n"));
	g_assert_nonnull (strstr (fs, "#define LRG_FRAGMENT_STAGE 1\n"));
	g_assert_nonnull (strstr (fs, "in vec3 fragPosition;\n"));
	g_assert_nonnull (strstr (fs, "uniform float lrgTime;\n"));
	g_assert_nonnull (strstr (fs, "uniform vec4 tint;\n"));
	g_assert_nonnull (strstr (fs, "vec3 lrg_view_position()"));
	g_assert_null (strstr (fs, "LRG_LIT"));
	/* declared once (the user's) */
	g_assert_true (strstr (fs, "in vec3 fragNormal;") == g_strrstr (fs, "in vec3 fragNormal;"));
	g_assert_true (strstr (fs, "out vec4 finalColor;") == g_strrstr (fs, "out vec4 finalColor;"));
	/* the user's #version is blanked; their line 2 is preamble + 2 */
	g_assert_cmpuint (line_of (fs, "in vec3 fragNormal;"), ==, preamble + 2);
	g_assert_null (strstr (fs + 1, "#version"));

	/* without a vertex source: the complete default stage, no preamble */
	vs = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_VERTEX, &preamble);
	g_assert_cmpstr (vs, ==, lrg_shader_asset_get_default_vertex_source ());
	g_assert_cmpuint (preamble, ==, 0);

	/* a user vertex stage gets the attribute / matrix / varying contract */
	g_clear_pointer (&vs, g_free);
	lrg_shader_asset_set_source (asset, LRG_SHADER_STAGE_VERTEX,
	                             "void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }\n");
	vs = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_VERTEX, &preamble);
	g_assert_nonnull (strstr (vs, "in vec3 vertexPosition;\n"));
	g_assert_nonnull (strstr (vs, "uniform mat4 mvp;\n"));
	g_assert_nonnull (strstr (vs, "out vec3 fragNormal;\n"));
	g_assert_nonnull (strstr (vs, "uniform vec4 tint;\n"));
	g_assert_cmpuint (line_of (vs, "void main()"), ==, preamble + 1);
}

/* a stand-in lit library (the documented test seam) */
static const gchar fake_library[] =
	"uniform vec3 fakeSun;\n"
	"uniform sampler2D texture0;\n"
	"vec3 lrg_light_pbr(vec3 p, vec3 n, vec3 v, vec4 albedo, float metal, float rough, float ao)\n"
	"{\n"
	"    return albedo.rgb * fakeSun;\n"
	"}\n";

static void
test_preamble_lit (void)
{
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new ();
	g_autofree gchar *fs = NULL;
	g_autofree gchar *real = NULL;
	g_autofree gchar *fallback = NULL;
	g_autofree gchar *vs = NULL;
	guint preamble = 0;

	lrg_shader_asset_set_lit (asset, TRUE);
	lrg_shader_asset_set_source (asset, LRG_SHADER_STAGE_FRAGMENT,
	                             "void main() { finalColor = vec4(lrg_light_pbr(fragPosition, "
	                             "fragNormal, vec3(0.0), vec4(1.0), 0.0, 0.5, 1.0), 1.0); }\n");

	/* the seam: the fake library is included, texture0 not redeclared,
	 * no fallback since it defines lrg_light_pbr */
	lrg_shader_asset_set_library_source (asset, fake_library);
	g_assert_cmpstr (lrg_shader_asset_get_library_source (asset), ==, fake_library);
	fs = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_FRAGMENT, &preamble);
	g_assert_nonnull (strstr (fs, "#define LRG_LIT 1\n"));
	g_assert_nonnull (strstr (fs, fake_library));
	g_assert_true (strstr (fs, "uniform sampler2D texture0;") == g_strrstr (fs, "uniform sampler2D texture0;"));
	g_assert_null (strstr (fs, "LRG_LIGHT_FALLBACK"));
	g_assert_nonnull (strstr (fs, "vec3 lrg_to_linear("));
	g_assert_cmpuint (line_of (fs, "void main()"), ==, preamble + 1);

	/* a library without lrg_light_pbr gets the fallback model */
	lrg_shader_asset_set_library_source (asset, "uniform float unused;\n");
	fallback = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_FRAGMENT, NULL);
	g_assert_nonnull (strstr (fallback, "#define LRG_LIGHT_FALLBACK 1\n"));

	/* the real light-set library: its pbr function, colour helpers, fog
	 * and eye are used, nothing it declares is repeated */
	lrg_shader_asset_set_library_source (asset, NULL);
	g_assert_cmpstr (lrg_shader_asset_get_library_source (asset), ==, lrg_light_set_get_glsl_library ());
	real = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_FRAGMENT, NULL);
	if (lrg_light_set_get_glsl_library ()[0] != '\0')
	{
		g_assert_null (strstr (real, "LRG_LIGHT_FALLBACK"));
		g_assert_true (strstr (real, "uniform sampler2D texture0;") ==
		               g_strrstr (real, "uniform sampler2D texture0;"));
		g_assert_true (strstr (real, "vec3 lrg_to_linear(") == g_strrstr (real, "vec3 lrg_to_linear("));
		g_assert_nonnull (strstr (real, "return mapCamera;"));
		g_assert_nonnull (strstr (real, "#define LRG_HAS_FOG 1\n"));
		g_assert_nonnull (strstr (real, "#define LRG_HAS_OUTPUT_LINEAR 1\n"));
		g_assert_nonnull (strstr (real, "#define LRG_HAS_DECALS 1\n"));
		g_assert_nonnull (strstr (real, "#define LRG_HAS_AIR 1\n"));
		g_assert_nonnull (strstr (real, "#define LRG_HAS_OVERLAY 1\n"));
	}

	/* a lit asset without a vertex stage uses the light set's */
	vs = lrg_shader_asset_get_generated_source (asset, LRG_SHADER_STAGE_VERTEX, NULL);
	if (lrg_light_set_get_vertex_source ()[0] != '\0')
		g_assert_cmpstr (vs, ==, lrg_light_set_get_vertex_source ());
}

static void
test_library_redeclaration (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new ();
	LrgShaderDiagnostic *d;

	/* redeclaring a library uniform is caught before the driver sees it,
	 * with the user's line */
	lrg_shader_asset_set_lit (asset, TRUE);
	lrg_shader_asset_set_library_source (asset, fake_library);
	lrg_shader_asset_set_source (asset, LRG_SHADER_STAGE_FRAGMENT,
	                             "// my shader\n"
	                             "uniform vec3 fakeSun;\n"
	                             "void main() { finalColor = vec4(fakeSun, 1.0); }\n");
	g_assert_false (lrg_shader_asset_compile (asset, &error));
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_COMPILE);
	g_assert_nonnull (strstr (error->message, "'fakeSun' is declared by the lit library"));
	g_assert_cmpuint (lrg_shader_asset_get_diagnostics (asset)->len, ==, 1);
	d = g_ptr_array_index (lrg_shader_asset_get_diagnostics (asset), 0);
	g_assert_cmpuint (lrg_shader_diagnostic_get_line (d), ==, 2);
	g_assert_false (lrg_shader_asset_is_compiled (asset));
}

static void
test_log_parse (void)
{
	g_autoptr(GPtrArray) mesa = NULL;
	g_autoptr(GPtrArray) nvidia = NULL;
	g_autoptr(GPtrArray) amd = NULL;
	LrgShaderDiagnostic *d;
	g_autofree gchar *text = NULL;

	mesa = lrg_shader_parse_info_log ("0:22(5): error: `foo' undeclared\n"
	                                  "0:3(1): warning: extension not supported\n"
	                                  "something odd\n",
	                                  LRG_SHADER_STAGE_FRAGMENT, 20, "glow.frag");
	g_assert_cmpuint (mesa->len, ==, 3);
	d = g_ptr_array_index (mesa, 0);
	g_assert_cmpuint (lrg_shader_diagnostic_get_line (d), ==, 2);
	g_assert_cmpuint (lrg_shader_diagnostic_get_column (d), ==, 5);
	g_assert_true (lrg_shader_diagnostic_is_error (d));
	g_assert_false (lrg_shader_diagnostic_is_in_preamble (d));
	g_assert_cmpstr (lrg_shader_diagnostic_get_source (d), ==, "glow.frag");
	g_assert_cmpstr (lrg_shader_diagnostic_get_message (d), ==, "`foo' undeclared");
	g_assert_cmpint (lrg_shader_diagnostic_get_stage (d), ==, LRG_SHADER_STAGE_FRAGMENT);
	text = lrg_shader_diagnostic_to_string (d);
	g_assert_cmpstr (text, ==, "glow.frag:2:5: error: `foo' undeclared");
	d = g_ptr_array_index (mesa, 1);
	g_assert_false (lrg_shader_diagnostic_is_error (d));
	g_assert_true (lrg_shader_diagnostic_is_in_preamble (d));
	g_assert_cmpstr (lrg_shader_diagnostic_get_source (d), ==, "<preamble>");
	g_assert_cmpuint (lrg_shader_diagnostic_get_line (d), ==, 3);
	d = g_ptr_array_index (mesa, 2);
	g_assert_cmpuint (lrg_shader_diagnostic_get_line (d), ==, 0);
	g_assert_cmpstr (lrg_shader_diagnostic_get_message (d), ==, "something odd");

	nvidia = lrg_shader_parse_info_log ("0(14) : error C1008: undefined variable \"foo\"\n",
	                                    LRG_SHADER_STAGE_VERTEX, 10, "x.vert");
	d = g_ptr_array_index (nvidia, 0);
	g_assert_cmpuint (lrg_shader_diagnostic_get_line (d), ==, 4);
	g_assert_cmpuint (lrg_shader_diagnostic_get_column (d), ==, 0);
	g_assert_cmpstr (lrg_shader_diagnostic_get_message (d), ==, "undefined variable \"foo\"");

	amd = lrg_shader_parse_info_log ("ERROR: 0:31: 'foo' : undeclared identifier\n",
	                                 LRG_SHADER_STAGE_FRAGMENT, 30, "y.frag");
	d = g_ptr_array_index (amd, 0);
	g_assert_cmpuint (lrg_shader_diagnostic_get_line (d), ==, 1);
	g_assert_nonnull (strstr (lrg_shader_diagnostic_get_message (d), "undeclared identifier"));
}

static void
test_compile_no_context (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new_from_data (fixture, -1, NULL, &error);

	if (graphics_available)
	{
		g_test_skip ("a graphics context is open");
		return;
	}
	g_assert_false (lrg_shader_asset_compile (asset, &error));
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_NO_CONTEXT);
	g_assert_false (lrg_shader_asset_is_compiled (asset));
}

/* ---- GPU ---------------------------------------------------------------- */

static const gchar unlit_tint[] =
	"version: 1\n"
	"name: tint\n"
	"fragment: |\n"
	"  void main()\n"
	"  {\n"
	"      finalColor = tint;\n"
	"  }\n"
	"uniforms:\n"
	"  tint:\n"
	"    type: color\n";

/* draws a full-target rectangle with @asset into a 16 x 16 target and
 * reads its centre */
static Color
render_centre (LrgShaderAsset *asset)
{
	RenderTexture2D target = LoadRenderTexture (16, 16);
	Shader *shader = grl_shader_get_handle (lrg_shader_asset_get_shader (asset));
	Image image;
	Color colour;

	BeginTextureMode (target);
	ClearBackground (BLACK);
	g_assert_true (lrg_shader_asset_apply (asset, NULL));
	BeginShaderMode (*shader);
	DrawRectangle (0, 0, 16, 16, WHITE);
	EndShaderMode ();
	EndTextureMode ();
	image = LoadImageFromTexture (target.texture);
	colour = GetImageColor (image, 8, 8);
	UnloadImage (image);
	UnloadRenderTexture (target);
	return colour;
}

static void
test_gpu_compile_and_render (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = NULL;
	Color colour;
	gfloat green[3] = { 0.0f, 1.0f, 0.0f };

	SKIP_WITHOUT_GRAPHICS ();

	asset = lrg_shader_asset_new_from_data (unlit_tint, -1, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_shader_asset_compile (asset, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_shader_asset_is_compiled (asset));
	g_assert_cmpuint (lrg_shader_asset_get_shader_id (asset), !=, 0);
	g_assert_cmpint (lrg_shader_asset_get_uniform_location (asset, "tint"), >=, 0);
	g_assert_cmpint (lrg_shader_asset_get_uniform_location (asset, "nothing"), ==, -1);

	/* the stored value reaches the screen */
	lrg_shader_asset_set_uniform_vec4 (asset, "tint", 1.0f, 0.0f, 0.0f, 1.0f);
	colour = render_centre (asset);
	g_assert_cmpuint (colour.r, >, 250);
	g_assert_cmpuint (colour.g, <, 5);

	/* and a per-draw override without storing */
	{
		gfloat v[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		RenderTexture2D target = LoadRenderTexture (16, 16);
		Shader *shader = grl_shader_get_handle (lrg_shader_asset_get_shader (asset));
		Image image;
		Color c;

		BeginTextureMode (target);
		ClearBackground (BLACK);
		lrg_shader_asset_apply (asset, NULL);
		g_assert_true (lrg_shader_asset_apply_uniform (asset, "tint", v, 4, &error));
		BeginShaderMode (*shader);
		DrawRectangle (0, 0, 16, 16, WHITE);
		EndShaderMode ();
		EndTextureMode ();
		image = LoadImageFromTexture (target.texture);
		c = GetImageColor (image, 8, 8);
		UnloadImage (image);
		UnloadRenderTexture (target);
		g_assert_cmpuint (c.b, >, 250);
		g_assert_cmpuint (c.r, <, 5);
	}
	g_assert_false (lrg_shader_asset_apply_uniform (asset, "tint", green, 3, &error));
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_UNIFORM);

	lrg_shader_asset_release (asset);
	g_assert_false (lrg_shader_asset_is_compiled (asset));
}

static void
test_gpu_compile_error_and_keep_last_good (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = NULL;
	GPtrArray *diagnostics;
	LrgShaderDiagnostic *first;
	guint good_id;
	guint i;
	gboolean found = FALSE;

	SKIP_WITHOUT_GRAPHICS ();

	asset = lrg_shader_asset_new_from_data (unlit_tint, -1, NULL, &error);
	g_assert_true (lrg_shader_asset_compile (asset, &error));
	good_id = lrg_shader_asset_get_shader_id (asset);

	/* an undeclared identifier on the user's line 3 */
	lrg_shader_asset_set_source (asset, LRG_SHADER_STAGE_FRAGMENT,
	                             "void main()\n{\n    finalColor = vec4(undeclared_thing);\n}\n");
	g_assert_false (lrg_shader_asset_compile (asset, &error));
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_COMPILE);
	g_assert_nonnull (strstr (error->message, "#fragment:3"));

	diagnostics = lrg_shader_asset_get_diagnostics (asset);
	g_assert_cmpuint (diagnostics->len, >, 0);
	for (i = 0; i < diagnostics->len; i++)
	{
		first = g_ptr_array_index (diagnostics, i);
		if (lrg_shader_diagnostic_is_error (first) && lrg_shader_diagnostic_get_line (first) == 3)
		{
			g_assert_false (lrg_shader_diagnostic_is_in_preamble (first));
			g_assert_true (g_str_has_suffix (lrg_shader_diagnostic_get_source (first), "#fragment"));
			found = TRUE;
		}
	}
	g_assert_true (found);

	/* keep-last-good: the old program is still in use */
	g_assert_true (lrg_shader_asset_is_compiled (asset));
	g_assert_cmpuint (lrg_shader_asset_get_shader_id (asset), ==, good_id);
}

static void
test_gpu_compile_lit (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) fake = lrg_shader_asset_new ();
	g_autoptr(LrgShaderAsset) real = lrg_shader_asset_new ();
	const gchar *body =
		"void main()\n"
		"{\n"
		"    vec3 n = normalize(fragNormal);\n"
		"    vec3 v = normalize(lrg_view_position() - fragPosition);\n"
		"    vec3 lit = lrg_light_pbr(fragPosition, n, v, vec4(lrg_to_linear(vec3(0.8)), 1.0), 0.0, 0.5, 1.0);\n"
		"    finalColor = vec4(lrg_to_display(lit), 1.0);\n"
		"}\n";

	SKIP_WITHOUT_GRAPHICS ();

	/* through the test seam */
	lrg_shader_asset_set_lit (fake, TRUE);
	lrg_shader_asset_set_library_source (fake, fake_library);
	lrg_shader_asset_set_source (fake, LRG_SHADER_STAGE_FRAGMENT, body);
	g_assert_true (lrg_shader_asset_compile (fake, &error));
	g_assert_no_error (error);
	g_assert_cmpint (lrg_shader_asset_get_uniform_location (fake, "fakeSun"), >=, 0);

	/* against the real light-set library and vertex stage */
	lrg_shader_asset_set_lit (real, TRUE);
	lrg_shader_asset_set_source (real, LRG_SHADER_STAGE_FRAGMENT, body);
	if (!lrg_shader_asset_compile (real, &error))
		g_error ("lit compile against the light-set library failed:\n%s", error->message);
	if (lrg_light_set_get_glsl_library ()[0] != '\0')
		g_assert_cmpint (lrg_shader_asset_get_uniform_location (real, "sunDirection"), >=, 0);

	/* the light set uploads its state to the user program */
	{
		g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
		g_autoptr(GrlVector3) down = grl_vector3_new (0.0f, -1.0f, 0.0f);
		g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);

		lrg_light_set_set_directional (lights, down, white);
		lrg_light_set_apply (lights, 0.0, 2.0, 5.0);
		g_assert_true (lrg_shader_asset_bind_lights (real, lights));
		g_assert_true (lrg_shader_asset_bind_lights (real, lights));
		/* a recompile forgets the old program in the light set */
		g_assert_true (lrg_shader_asset_compile (real, &error));
		g_assert_true (lrg_shader_asset_bind_lights (real, lights));
		lrg_shader_asset_release (real);
		g_assert_false (lrg_shader_asset_bind_lights (real, lights));
	}
}

/* iterates the default context until *flag or about @seconds pass */
static void
wait_for (gboolean *flag,
          gdouble   seconds)
{
	gint64 end = g_get_monotonic_time () + (gint64) (seconds * G_USEC_PER_SEC);

	while (!*flag && g_get_monotonic_time () < end)
	{
		g_main_context_iteration (NULL, FALSE);
		g_usleep (5000);
	}
}

static void
on_reloaded (LrgShaderAsset *asset,
             gpointer        data)
{
	*(gboolean *) data = TRUE;
}

static void
on_reload_failed (LrgShaderAsset *asset,
                  GError         *error,
                  gpointer        data)
{
	g_assert_nonnull (error);
	*(gboolean *) data = TRUE;
}

static void
test_hot_reload (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-shader-reload-XXXXXX", &error);
	g_autofree gchar *doc = NULL;
	g_autofree gchar *frag = NULL;
	g_autoptr(LrgShaderAsset) asset = NULL;
	gboolean reloaded = FALSE;
	gboolean failed = FALSE;
	guint first_id;
	Color colour;

	g_assert_no_error (error);
	doc = g_build_filename (dir, "pulse.rshader", NULL);
	frag = g_build_filename (dir, "pulse.frag", NULL);
	g_assert_true (g_file_set_contents (frag, "void main() { finalColor = vec4(1.0, 0.0, 0.0, 1.0); }\n",
	                                    -1, NULL));
	g_assert_true (g_file_set_contents (doc, "version: 1\nname: pulse\nfragment: pulse.frag\n", -1, NULL));

	asset = lrg_shader_asset_new_from_file (doc, &error);
	g_assert_no_error (error);
	g_signal_connect (asset, "reloaded", G_CALLBACK (on_reloaded), &reloaded);
	g_signal_connect (asset, "reload-failed", G_CALLBACK (on_reload_failed), &failed);

	/* headless: a reload re-reads the files */
	g_assert_true (g_file_set_contents (doc, "version: 1\nname: pulse2\nfragment: pulse.frag\n", -1, NULL));
	g_assert_true (lrg_shader_asset_reload (asset, &error));
	g_assert_true (reloaded);
	g_assert_cmpstr (lrg_shader_asset_get_name (asset), ==, "pulse2");
	reloaded = FALSE;

	if (!graphics_available)
	{
		g_unlink (frag);
		g_unlink (doc);
		g_rmdir (dir);
		return;
	}

	g_assert_true (lrg_shader_asset_compile (asset, &error));
	first_id = lrg_shader_asset_get_shader_id (asset);
	g_assert_true (lrg_shader_asset_set_watch (asset, TRUE, &error));
	g_assert_true (lrg_shader_asset_get_watch (asset));

	/* a good edit of the stage file swaps the program */
	g_assert_true (g_file_set_contents (frag, "void main() { finalColor = vec4(0.0, 1.0, 0.0, 1.0); }\n",
	                                    -1, NULL));
	wait_for (&reloaded, 5.0);
	g_assert_true (reloaded);
	g_assert_false (failed);
	g_assert_cmpuint (lrg_shader_asset_get_shader_id (asset), !=, 0);
	colour = render_centre (asset);
	g_assert_cmpuint (colour.g, >, 250);
	g_assert_cmpuint (colour.r, <, 5);
	(void) first_id;

	/* a broken edit keeps the last good program */
	first_id = lrg_shader_asset_get_shader_id (asset);
	reloaded = FALSE;
	g_assert_true (g_file_set_contents (frag, "void main() { finalColor = nope; }\n", -1, NULL));
	wait_for (&failed, 5.0);
	g_assert_true (failed);
	g_assert_false (reloaded);
	g_assert_cmpuint (lrg_shader_asset_get_shader_id (asset), ==, first_id);
	g_assert_cmpuint (lrg_shader_asset_get_diagnostics (asset)->len, >, 0);
	colour = render_centre (asset);
	g_assert_cmpuint (colour.g, >, 250);

	g_assert_true (lrg_shader_asset_set_watch (asset, FALSE, &error));
	lrg_shader_asset_release (asset);
	g_unlink (frag);
	g_unlink (doc);
	g_rmdir (dir);
}

static void
test_watch_needs_path (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderAsset) asset = lrg_shader_asset_new ();

	g_assert_false (lrg_shader_asset_set_watch (asset, TRUE, &error));
	g_assert_error (error, LRG_SHADER_ASSET_ERROR, LRG_SHADER_ASSET_ERROR_IO);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/shader-asset/format/round-trip", test_format_round_trip);
	g_test_add_func ("/shader-asset/format/built-in-code", test_format_built_in_code);
	g_test_add_func ("/shader-asset/format/stage-paths", test_format_stage_paths);
	g_test_add_func ("/shader-asset/format/errors", test_format_errors);
	g_test_add_func ("/shader-asset/uniforms/values", test_uniform_values);
	g_test_add_func ("/shader-asset/preamble/contract", test_preamble);
	g_test_add_func ("/shader-asset/preamble/lit", test_preamble_lit);
	g_test_add_func ("/shader-asset/diagnostics/log-parse", test_log_parse);
	g_test_add_func ("/shader-asset/diagnostics/library-redeclaration", test_library_redeclaration);
	g_test_add_func ("/shader-asset/compile/no-context", test_compile_no_context);
	g_test_add_func ("/shader-asset/gpu/compile-and-render", test_gpu_compile_and_render);
	g_test_add_func ("/shader-asset/gpu/error-keep-last-good", test_gpu_compile_error_and_keep_last_good);
	g_test_add_func ("/shader-asset/gpu/lit", test_gpu_compile_lit);
	g_test_add_func ("/shader-asset/reload/hot-reload", test_hot_reload);
	g_test_add_func ("/shader-asset/reload/watch-needs-path", test_watch_needs_path);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
