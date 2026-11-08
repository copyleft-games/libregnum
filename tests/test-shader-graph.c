/* test-shader-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgShaderGraph: the node library, golden GLSL for small
 * graphs (and that the text does not depend on insertion order),
 * validation (cycles, type mismatches, missing inputs, output count,
 * parameters) with node ids, the `.rshadergraph` format (round trip,
 * unknown keys, errors), the generated LrgShaderAsset, and, with a
 * display, compiling the generated unlit and PBR shaders and rendering
 * an unlit graph.  Nothing here needs src/ui.
 */

#include <glib.h>
#include <glib/gstdio.h>
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

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (16, 16, "lrg-shader-graph-test");
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

/* adds a node, asserting success */
static void
add (LrgShaderGraph *graph,
     const gchar    *type,
     const gchar    *id)
{
	g_autoptr(GError) error = NULL;

	g_assert_cmpstr (lrg_shader_graph_add_node (graph, type, id, &error), ==, id);
	g_assert_no_error (error);
}

/* links from.port -> to.port, asserting success */
static void
link_ports (LrgShaderGraph *graph,
            const gchar    *from,
            const gchar    *from_port,
            const gchar    *to,
            const gchar    *to_port)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_shader_graph_connect (graph, from, from_port, to, to_port, &error));
	g_assert_no_error (error);
}

static void
test_library (void)
{
	g_auto(GStrv) types = lrg_shader_graph_list_node_types ();
	g_auto(GStrv) inputs = lrg_shader_node_type_get_inputs ("texture_sample");
	g_auto(GStrv) outputs = lrg_shader_node_type_get_outputs ("texture_sample");
	g_auto(GStrv) pbr = lrg_shader_node_type_get_inputs ("pbr_output");
	const gchar * const required[] = {
		"uv", "position", "normal", "view_dir", "time", "vertex_color", "parameter",
		"add", "sub", "mul", "div", "lerp", "dot", "cross", "normalize", "pow", "clamp",
		"saturate", "one_minus", "fresnel", "step", "smoothstep", "sin", "cos", "split",
		"combine", "swizzle", "texture_sample", "noise", "pbr_output", "unlit_output", NULL
	};
	guint i;

	for (i = 0; required[i] != NULL; i++)
	{
		g_assert_true (g_strv_contains ((const gchar * const *) types, required[i]));
		g_assert_true (lrg_shader_node_type_exists (required[i]));
		g_assert_nonnull (lrg_shader_node_type_get_label (required[i]));
	}
	g_assert_false (lrg_shader_node_type_exists ("teapot"));
	g_assert_null (lrg_shader_node_type_get_inputs ("teapot"));
	g_assert_cmpstr (lrg_shader_node_type_get_category ("noise"), ==, "procedural");
	g_assert_cmpstr (lrg_shader_node_type_get_category ("pbr_output"), ==, "output");

	g_assert_cmpuint (g_strv_length (inputs), ==, 2);
	g_assert_cmpstr (inputs[0], ==, "texture");
	g_assert_cmpuint (g_strv_length (outputs), ==, 6);
	g_assert_cmpint (lrg_shader_node_type_get_port_type ("texture_sample", "texture", FALSE), ==,
	                 LRG_SHADER_PORT_SAMPLER2D);
	g_assert_cmpint (lrg_shader_node_type_get_port_type ("texture_sample", "rgb", TRUE), ==,
	                 LRG_SHADER_PORT_VEC3);
	g_assert_cmpint (lrg_shader_node_type_get_port_type ("add", "a", FALSE), ==,
	                 LRG_SHADER_PORT_DYNAMIC);
	g_assert_cmpstr (pbr[0], ==, "albedo");
	g_assert_true (g_strv_contains ((const gchar * const *) pbr, "emission"));
	g_assert_true (g_strv_contains ((const gchar * const *) pbr, "alpha"));
}

/* albedo_map sampled at the UV, times a tint colour, unlit */
static LrgShaderGraph *
build_textured (gboolean reversed)
{
	LrgShaderGraph *graph = lrg_shader_graph_new ();
	gfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	lrg_shader_graph_set_name (graph, "textured");
	if (reversed)
	{
		add (graph, "unlit_output", "out");
		add (graph, "mul", "mul");
		add (graph, "parameter", "tint");
		add (graph, "texture_sample", "sample");
		add (graph, "parameter", "tex");
	}
	else
	{
		add (graph, "parameter", "tex");
		add (graph, "texture_sample", "sample");
		add (graph, "parameter", "tint");
		add (graph, "mul", "mul");
		add (graph, "unlit_output", "out");
	}
	lrg_shader_graph_set_node_param (graph, "tex", "name", "albedo_map");
	lrg_shader_graph_set_node_param (graph, "tex", "type", "sampler2D");
	lrg_shader_graph_set_node_param (graph, "tint", "name", "tint");
	lrg_shader_graph_set_node_param (graph, "tint", "type", "color");
	lrg_shader_graph_set_node_param (graph, "tint", "hint", "color");
	lrg_shader_graph_set_node_values (graph, "tint", "default", white, 4);

	if (reversed)
	{
		link_ports (graph, "sample", "a", "out", "alpha");
		link_ports (graph, "mul", "out", "out", "color");
		link_ports (graph, "tint", "value", "mul", "b");
		link_ports (graph, "sample", "rgba", "mul", "a");
		link_ports (graph, "tex", "value", "sample", "texture");
	}
	else
	{
		link_ports (graph, "tex", "value", "sample", "texture");
		link_ports (graph, "sample", "rgba", "mul", "a");
		link_ports (graph, "tint", "value", "mul", "b");
		link_ports (graph, "mul", "out", "out", "color");
		link_ports (graph, "sample", "a", "out", "alpha");
	}
	return graph;
}

static const gchar textured_golden[] =
	"// generated by LrgShaderGraph from 'textured': edit the graph, not this text\n"
	"void main()\n"
	"{\n"
	"    vec4 sg_sample = texture(albedo_map, fragTexCoord);\n"
	"    vec4 sg_mul = (sg_sample * tint);\n"
	"    finalColor = vec4(sg_mul.xyz, sg_sample.a);\n"
	"}\n";

static void
test_golden_unlit (void)
{
	g_autoptr(LrgShaderGraph) graph = build_textured (FALSE);
	g_autoptr(LrgShaderGraph) reversed = build_textured (TRUE);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *glsl = NULL;
	g_autofree gchar *glsl_reversed = NULL;

	glsl = lrg_shader_graph_generate_fragment (graph, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (glsl, ==, textured_golden);

	/* the same graph built in another order gives the same text */
	glsl_reversed = lrg_shader_graph_generate_fragment (reversed, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (glsl_reversed, ==, textured_golden);

	/* resolved types: mul is vec4 (widest input), its output feeds a vec3 */
	g_assert_cmpint (lrg_shader_graph_get_resolved_port_type (graph, "mul", "out", TRUE), ==,
	                 LRG_SHADER_PORT_VEC4);
	g_assert_cmpint (lrg_shader_graph_get_resolved_port_type (graph, "mul", "a", FALSE), ==,
	                 LRG_SHADER_PORT_VEC4);
	g_assert_cmpint (lrg_shader_graph_get_resolved_port_type (graph, "tex", "value", TRUE), ==,
	                 LRG_SHADER_PORT_SAMPLER2D);
	g_assert_cmpint (lrg_shader_graph_get_resolved_port_type (graph, "nope", "out", TRUE), ==,
	                 LRG_SHADER_PORT_DYNAMIC);
}

static const gchar pbr_golden[] =
	"// generated by LrgShaderGraph from 'rim': edit the graph, not this text\n"
	"float lrg_sg_hash(vec2 p)\n"
	"{\n"
	"    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);\n"
	"}\n"
	"\n"
	"float lrg_sg_noise(vec2 p)\n"
	"{\n"
	"    vec2 i = floor(p);\n"
	"    vec2 f = fract(p);\n"
	"    vec2 u = f * f * (3.0 - 2.0 * f);\n"
	"    return mix(mix(lrg_sg_hash(i), lrg_sg_hash(i + vec2(1.0, 0.0)), u.x),\n"
	"               mix(lrg_sg_hash(i + vec2(0.0, 1.0)), lrg_sg_hash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
	"}\n"
	"\n"
	"void main()\n"
	"{\n"
	"    float sg_grain = lrg_sg_noise(fragTexCoord * 4.0);\n"
	"    vec3 sg_base = mix(vec3(0.2, 0.3, 0.8), vec3(0.9, 0.9, 1.0), vec3(sg_grain));\n"
	"    float sg_rim = pow(1.0 - clamp(dot(normalize(normalize(fragNormal)), normalize(normalize(lrg_view_position() - fragPosition))), 0.0, 1.0), rim_power);\n"
	"    vec3 sg_glow = (vec3(sg_rim) * vec3(0.2, 0.6, 1.0));\n"
	"    vec3 lrg_n = normalize(normalize(fragNormal));\n"
	"    vec3 lrg_v = normalize(lrg_view_position() - fragPosition);\n"
	"    vec4 lrg_albedo = vec4(lrg_to_linear(sg_base), 1.0);\n"
	"    float lrg_metal = 0.0;\n"
	"    float lrg_rough = 0.35;\n"
	"#ifdef LRG_HAS_DECALS\n"
	"    lrg_decals_apply(fragPosition, normalize(fragNormal), lrg_albedo, lrg_n, lrg_rough, lrg_metal);\n"
	"#endif\n"
	"    vec3 lrg_lit = lrg_light_pbr(fragPosition, lrg_n, lrg_v, lrg_albedo, lrg_metal, lrg_rough, 1.0)\n"
	"        + lrg_to_linear(sg_glow);\n"
	"    vec3 lrg_col = lrg_to_display(lrg_lit);\n"
	"#ifdef LRG_HAS_OVERLAY\n"
	"    if (ovOn != 0) lrg_col = lrg_overlay_apply(fragPosition, lrg_n, lrg_col);\n"
	"#endif\n"
	"#ifdef LRG_HAS_AIR\n"
	"    if (airParams.x > 0.0)\n"
	"    {\n"
	"        vec3 lrg_ins;\n"
	"        float lrg_tr = lrg_air(fragPosition, lrg_ins);\n"
	"        lrg_col = lrg_col * lrg_tr + lrg_ins;\n"
	"    }\n"
	"#endif\n"
	"#ifdef LRG_HAS_FOG\n"
	"    lrg_col = mix(lrg_col, fogColor.rgb, lrg_fog_amount(fragPosition));\n"
	"#endif\n"
	"#ifdef LRG_HAS_OUTPUT_LINEAR\n"
	"    if (outputLinear != 0) lrg_col = lrg_to_linear(lrg_col);\n"
	"#endif\n"
	"    finalColor = vec4(lrg_col, lrg_albedo.a);\n"
	"}\n";

/* noise-blended base colour plus a fresnel rim glow, lit */
static LrgShaderGraph *
build_rim (void)
{
	LrgShaderGraph *graph = lrg_shader_graph_new ();
	gfloat four = 4.0f;
	gfloat dark[3] = { 0.2f, 0.3f, 0.8f };
	gfloat light[3] = { 0.9f, 0.9f, 1.0f };
	gfloat glow[3] = { 0.2f, 0.6f, 1.0f };
	gfloat rough = 0.35f;
	gfloat power_default = 3.0f;
	gfloat range[2] = { 0.5f, 8.0f };

	lrg_shader_graph_set_name (graph, "rim");
	add (graph, "noise", "grain");
	add (graph, "constant", "dark");
	add (graph, "lerp", "base");
	add (graph, "parameter", "power");
	add (graph, "fresnel", "rim");
	add (graph, "mul", "glow");
	add (graph, "pbr_output", "out");

	lrg_shader_graph_set_input_value (graph, "grain", "scale", &four, 1);
	lrg_shader_graph_set_node_values (graph, "dark", "value", dark, 3);
	lrg_shader_graph_set_input_value (graph, "base", "b", light, 3);
	lrg_shader_graph_set_node_param (graph, "power", "name", "rim_power");
	lrg_shader_graph_set_node_param (graph, "power", "type", "float");
	lrg_shader_graph_set_node_param (graph, "power", "hint", "slider");
	lrg_shader_graph_set_node_values (graph, "power", "default", &power_default, 1);
	lrg_shader_graph_set_node_values (graph, "power", "range", range, 2);
	lrg_shader_graph_set_input_value (graph, "glow", "b", glow, 3);
	lrg_shader_graph_set_input_value (graph, "out", "roughness", &rough, 1);

	link_ports (graph, "dark", "value", "base", "a");
	link_ports (graph, "grain", "out", "base", "t");
	link_ports (graph, "power", "value", "rim", "power");
	link_ports (graph, "rim", "out", "glow", "a");
	link_ports (graph, "base", "out", "out", "albedo");
	link_ports (graph, "glow", "out", "out", "emission");
	return graph;
}

static void
test_golden_pbr (void)
{
	g_autoptr(LrgShaderGraph) graph = build_rim ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *glsl = lrg_shader_graph_generate_fragment (graph, &error);

	g_assert_no_error (error);
	g_assert_cmpstr (glsl, ==, pbr_golden);
}

static void
test_golden_split_swizzle (void)
{
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *glsl = NULL;

	lrg_shader_graph_set_name (graph, "channels");
	add (graph, "uv", "uv");
	add (graph, "split", "parts");
	add (graph, "swizzle", "flip");
	add (graph, "combine", "rebuilt");
	add (graph, "time", "t");
	add (graph, "sin", "wave");
	add (graph, "unlit_output", "out");
	lrg_shader_graph_set_node_param (graph, "flip", "mask", "yx");
	link_ports (graph, "uv", "uv", "parts", "in");
	link_ports (graph, "uv", "uv", "flip", "in");
	link_ports (graph, "t", "time", "wave", "in");
	link_ports (graph, "flip", "out", "rebuilt", "x");
	link_ports (graph, "parts", "y", "rebuilt", "y");
	link_ports (graph, "wave", "out", "rebuilt", "z");
	link_ports (graph, "rebuilt", "xyz", "out", "color");
	glsl = lrg_shader_graph_generate_fragment (graph, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (glsl, ==,
	                 "// generated by LrgShaderGraph from 'channels': edit the graph, not this text\n"
	                 "void main()\n"
	                 "{\n"
	                 "    vec2 sg_flip = fragTexCoord.yx;\n"
	                 "    vec4 sg_parts = vec4(fragTexCoord, 0.0, 0.0);\n"
	                 "    float sg_wave = sin(lrgTime);\n"
	                 "    vec4 sg_rebuilt = vec4(sg_flip.x, sg_parts.y, sg_wave, 1.0);\n"
	                 "    finalColor = vec4(sg_rebuilt.xyz, 1.0);\n"
	                 "}\n");
}

/* asserts a problem with @code on @node_id exists */
static void
assert_problem (LrgShaderGraph      *graph,
                LrgShaderGraphError  code,
                const gchar         *node_id,
                const gchar         *port)
{
	GPtrArray *problems = lrg_shader_graph_get_problems (graph);
	guint i;

	for (i = 0; i < problems->len; i++)
	{
		LrgShaderGraphProblem *p = g_ptr_array_index (problems, i);

		if (lrg_shader_graph_problem_get_code (p) == code &&
		    g_strcmp0 (lrg_shader_graph_problem_get_node_id (p), node_id) == 0 &&
		    g_strcmp0 (lrg_shader_graph_problem_get_port (p), port) == 0)
		{
			if (node_id != NULL)
				g_assert_nonnull (strstr (lrg_shader_graph_problem_get_message (p), node_id));
			return;
		}
	}
	g_error ("no problem %d on node '%s' port '%s'", code, node_id, port);
}

static void
test_validation (void)
{
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *glsl = NULL;

	/* no output at all */
	g_assert_false (lrg_shader_graph_validate (graph, &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_INVALID);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_NO_OUTPUT, NULL, NULL);
	g_clear_error (&error);

	add (graph, "unlit_output", "out");
	add (graph, "pbr_output", "out2");
	/* a cycle a -> b -> a */
	add (graph, "add", "a");
	add (graph, "add", "b");
	link_ports (graph, "a", "out", "b", "a");
	link_ports (graph, "b", "out", "a", "a");
	/* vec2 into cross's vec3: widening is a mismatch */
	add (graph, "uv", "uv");
	add (graph, "cross", "cr");
	link_ports (graph, "uv", "uv", "cr", "a");
	/* a texture sample without a texture */
	add (graph, "texture_sample", "smp");
	/* a parameter without a name, a bad swizzle mask */
	add (graph, "parameter", "p");
	lrg_shader_graph_set_node_param (graph, "p", "type", "vec3");
	add (graph, "swizzle", "sw");
	lrg_shader_graph_set_node_param (graph, "sw", "mask", "xz");
	link_ports (graph, "uv", "uv", "sw", "in");
	/* a sampler into a maths node */
	add (graph, "parameter", "tex");
	lrg_shader_graph_set_node_param (graph, "tex", "name", "tex");
	lrg_shader_graph_set_node_param (graph, "tex", "type", "sampler2D");
	add (graph, "mul", "m");
	link_ports (graph, "tex", "value", "m", "a");

	g_assert_false (lrg_shader_graph_validate (graph, &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_INVALID);
	g_assert_nonnull (strstr (error->message, "node 'a': is part of a cycle"));
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_MULTIPLE_OUTPUTS, "out", NULL);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_MULTIPLE_OUTPUTS, "out2", NULL);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_CYCLE, "a", NULL);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_CYCLE, "b", NULL);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_TYPE_MISMATCH, "cr", "a");
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_MISSING_INPUT, "smp", "texture");
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_INVALID_PARAM, "p", NULL);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_INVALID_PARAM, "sw", NULL);
	assert_problem (graph, LRG_SHADER_GRAPH_ERROR_TYPE_MISMATCH, "m", "a");
	g_clear_error (&error);

	/* generation refuses an invalid graph */
	glsl = lrg_shader_graph_generate_fragment (graph, &error);
	g_assert_null (glsl);
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_INVALID);
}

/* Two added nodes do not share a point. The first stays at the origin. */
static void
test_default_layout (void)
{
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new ();
	g_autoptr(GError) error = NULL;
	gfloat x = -1.0f;
	gfloat y = -1.0f;

	g_assert_nonnull (lrg_shader_graph_add_node (graph, "add", "a", &error));
	g_assert_nonnull (lrg_shader_graph_add_node (graph, "mul", "b", &error));
	g_assert_no_error (error);
	g_assert_true (lrg_shader_graph_get_node_position (graph, "a", &x, &y));
	g_assert_cmpfloat (x, ==, 0.0f);
	g_assert_cmpfloat (y, ==, 0.0f);
	g_assert_true (lrg_shader_graph_get_node_position (graph, "b", &x, &y));
	g_assert_cmpfloat (x, ==, 220.0f);
	g_assert_cmpfloat (y, ==, 0.0f);
}

static void
test_editing (void)
{
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new ();
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) ids = NULL;
	const gchar *id;
	const gchar *from;
	const gchar *to_port;
	gfloat v[4];
	gfloat x, y;

	/* generated ids, bad ids, duplicates, unknown types */
	id = lrg_shader_graph_add_node (graph, "add", NULL, &error);
	g_assert_cmpstr (id, ==, "add1");
	g_assert_null (lrg_shader_graph_add_node (graph, "add", "add1", &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_DUPLICATE_ID);
	g_clear_error (&error);
	g_assert_null (lrg_shader_graph_add_node (graph, "add", "lrg_x", &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_null (lrg_shader_graph_add_node (graph, "teapot", "t", &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE);
	g_clear_error (&error);

	add (graph, "uv", "uv");
	add (graph, "unlit_output", "out");
	g_assert_true (lrg_shader_graph_set_node_position (graph, "uv", 10.0f, -20.0f));
	g_assert_true (lrg_shader_graph_get_node_position (graph, "uv", &x, &y));
	g_assert_cmpfloat (y, ==, -20.0f);

	/* ports must exist */
	g_assert_false (lrg_shader_graph_connect (graph, "uv", "nope", "add1", "a", &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_UNKNOWN_PORT);
	g_clear_error (&error);
	g_assert_false (lrg_shader_graph_connect (graph, "ghost", "out", "add1", "a", &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE);
	g_clear_error (&error);

	/* an input has one link: connecting again replaces it in place */
	link_ports (graph, "uv", "uv", "add1", "a");
	link_ports (graph, "add1", "out", "out", "color");
	link_ports (graph, "add1", "out", "add1", "b");
	link_ports (graph, "uv", "uv", "add1", "b");
	g_assert_cmpuint (lrg_shader_graph_get_n_links (graph), ==, 3);
	g_assert_true (lrg_shader_graph_get_link (graph, 2, &from, NULL, NULL, &to_port));
	g_assert_cmpstr (from, ==, "uv");
	g_assert_cmpstr (to_port, ==, "b");
	g_assert_false (lrg_shader_graph_get_link (graph, 3, NULL, NULL, NULL, NULL));
	g_assert_true (lrg_shader_graph_disconnect (graph, "add1", "b"));
	g_assert_false (lrg_shader_graph_disconnect (graph, "add1", "b"));

	/* input values: fixed ports take 1 or their width */
	v[0] = 0.5f;
	v[1] = 0.25f;
	g_assert_true (lrg_shader_graph_set_input_value (graph, "add1", "b", v, 2));
	g_assert_cmpuint (lrg_shader_graph_get_input_value (graph, "add1", "b", v), ==, 2);
	g_assert_false (lrg_shader_graph_set_input_value (graph, "out", "color", v, 2));
	g_assert_true (lrg_shader_graph_set_input_value (graph, "out", "color", v, 1));
	g_assert_false (lrg_shader_graph_set_node_param (graph, "add1", "mask", "x"));
	g_assert_false (lrg_shader_graph_set_node_values (graph, "add1", "value", v, 1));

	/* removing a node drops its links */
	g_assert_true (lrg_shader_graph_remove_node (graph, "uv"));
	g_assert_cmpuint (lrg_shader_graph_get_n_links (graph), ==, 1);
	g_assert_false (lrg_shader_graph_has_node (graph, "uv"));
	ids = lrg_shader_graph_get_node_ids (graph);
	g_assert_cmpuint (g_strv_length (ids), ==, 2);
	g_assert_cmpstr (lrg_shader_graph_get_node_type (graph, "add1"), ==, "add");
	g_assert_cmpuint (lrg_shader_graph_get_n_nodes (graph), ==, 2);
}

static const gchar graph_fixture[] =
	"version: 1\n"
	"name: textured\n"
	"nodes:\n"
	"- id: tex\n"
	"  type: parameter\n"
	"  position:\n"
	"  - -200\n"
	"  - 40\n"
	"  params:\n"
	"    name: albedo_map\n"
	"    type: sampler2D\n"
	"    texture: textures/rock.png\n"
	"- id: sample\n"
	"  type: texture_sample\n"
	"- id: tint\n"
	"  type: parameter\n"
	"  params:\n"
	"    name: tint\n"
	"    type: color\n"
	"    default:\n"
	"    - 1\n"
	"    - 0.8\n"
	"    - 0.6\n"
	"    - 1\n"
	"    range:\n"
	"    - 0\n"
	"    - 1\n"
	"    hint: color\n"
	"- id: mul\n"
	"  type: mul\n"
	"- id: out\n"
	"  type: unlit_output\n"
	"  params:\n"
	"    blend: alpha\n"
	"  inputs:\n"
	"    alpha_clip: 0.1\n"
	"links:\n"
	"- from: tex.value\n"
	"  to: sample.texture\n"
	"- from: sample.rgba\n"
	"  to: mul.a\n"
	"- from: tint.value\n"
	"  to: mul.b\n"
	"- from: mul.out\n"
	"  to: out.color\n"
	"- from: sample.a\n"
	"  to: out.alpha\n"
	"editor:\n"
	"  zoom: 1.5\n";

static void
test_format_round_trip (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderGraph) graph = NULL;
	g_autoptr(LrgShaderGraph) again = NULL;
	g_autofree gchar *saved = NULL;
	g_autofree gchar *saved_again = NULL;
	gfloat v[4];

	graph = lrg_shader_graph_new_from_data (graph_fixture, -1, &error);
	g_assert_no_error (error);
	{
		gfloat x = 0.0f;
		gfloat y = 0.0f;

		/* Authored position wins over the default grid. */
		g_assert_true (lrg_shader_graph_get_node_position (graph, "tex", &x, &y));
		g_assert_cmpfloat (x, ==, -200.0f);
		g_assert_cmpfloat (y, ==, 40.0f);
		/* sample is the second node and has no position in the file. */
		g_assert_true (lrg_shader_graph_get_node_position (graph, "sample", &x, &y));
		g_assert_cmpfloat (x, ==, 220.0f);
		g_assert_cmpfloat (y, ==, 0.0f);
	}
	g_assert_cmpstr (lrg_shader_graph_get_name (graph), ==, "textured");
	g_assert_cmpuint (lrg_shader_graph_get_n_nodes (graph), ==, 5);
	g_assert_cmpuint (lrg_shader_graph_get_n_links (graph), ==, 5);
	g_assert_cmpstr (lrg_shader_graph_get_node_param (graph, "tex", "texture"), ==, "textures/rock.png");
	g_assert_cmpuint (lrg_shader_graph_get_node_values (graph, "tint", "default", v), ==, 4);
	g_assert_cmpfloat (v[1], ==, 0.8f);
	g_assert_cmpuint (lrg_shader_graph_get_input_value (graph, "out", "alpha_clip", v), ==, 1);

	saved = lrg_shader_graph_save_to_data (graph, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (saved, ==, graph_fixture);
	again = lrg_shader_graph_new_from_data (saved, -1, &error);
	g_assert_no_error (error);
	saved_again = lrg_shader_graph_save_to_data (again, NULL, &error);
	g_assert_cmpstr (saved_again, ==, graph_fixture);
}

static void
test_format_file (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-shadergraph-XXXXXX", &error);
	g_autofree gchar *path = NULL;
	g_autoptr(LrgShaderGraph) graph = build_rim ();
	g_autoptr(LrgShaderGraph) loaded = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;

	g_assert_no_error (error);
	path = g_build_filename (dir, "rim.rshadergraph", NULL);
	g_assert_true (lrg_shader_graph_save_to_file (graph, path, &error));
	g_assert_no_error (error);
	loaded = lrg_shader_graph_new_from_file (path, &error);
	g_assert_no_error (error);
	a = lrg_shader_graph_generate_fragment (graph, &error);
	b = lrg_shader_graph_generate_fragment (loaded, &error);
	g_assert_cmpstr (a, ==, b);
	g_unlink (path);
	g_rmdir (dir);

	g_assert_null (lrg_shader_graph_new_from_file ("/nonexistent/x.rshadergraph", &error));
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_IO);
}

/* loading @text fails with @code and a message containing @needle */
static void
expect_load_error (const gchar *text,
                   gint         code,
                   const gchar *needle)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new_from_data (text, -1, &error);

	g_assert_null (graph);
	g_assert_error (error, LRG_SHADER_GRAPH_ERROR, code);
	if (strstr (error->message, needle) == NULL)
		g_error ("expected '%s' in: %s", needle, error->message);
}

static void
test_format_errors (void)
{
	expect_load_error ("nodes:\n- id: a\n  type: teapot\n", LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE,
	                   "/nodes/0");
	expect_load_error ("nodes:\n- id: a\n  type: add\n- id: a\n  type: add\n",
	                   LRG_SHADER_GRAPH_ERROR_DUPLICATE_ID, "/nodes/1");
	expect_load_error ("nodes:\n- id: a\n  type: add\nlinks:\n- from: a.out\n  to: a.zz\n",
	                   LRG_SHADER_GRAPH_ERROR_UNKNOWN_PORT, "/links/0");
	expect_load_error ("nodes:\n- id: a\n  type: add\nlinks:\n- from: b.out\n  to: a.a\n",
	                   LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE, "/links/0");
	expect_load_error ("nodes:\n- id: a\n  type: add\n  colour: red\n", LRG_SHADER_GRAPH_ERROR_PARSE,
	                   "/nodes/0/colour");
	expect_load_error ("nodes:\n- id: a\n  type: add\n  params:\n    mask: x\n", LRG_SHADER_GRAPH_ERROR_PARSE,
	                   "take no parameter 'mask'");
	expect_load_error ("nodes:\n- id: a\n  type: cross\n  inputs:\n    a: [1, 2]\n",
	                   LRG_SHADER_GRAPH_ERROR_PARSE, "/nodes/0/inputs/a");
	expect_load_error ("links:\n- from: nodot\n  to: a.b\n", LRG_SHADER_GRAPH_ERROR_PARSE, "node.port");
	expect_load_error ("version: 3\n", LRG_SHADER_GRAPH_ERROR_PARSE, "/version");
}

static void
test_generate_asset (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new_from_data (graph_fixture, -1, &error);
	g_autoptr(LrgShaderGraph) rim = build_rim ();
	g_autoptr(LrgShaderAsset) asset = NULL;
	g_autoptr(LrgShaderAsset) lit = NULL;
	g_autofree gchar *fragment = NULL;
	const LrgShaderUniform *u;
	gfloat v[4];
	gfloat lo, hi;

	g_assert_no_error (error);
	asset = lrg_shader_graph_generate (graph, &error);
	g_assert_no_error (error);
	fragment = lrg_shader_graph_generate_fragment (graph, &error);
	g_assert_cmpstr (lrg_shader_asset_get_source (asset, LRG_SHADER_STAGE_FRAGMENT), ==, fragment);
	g_assert_cmpstr (lrg_shader_asset_get_name (asset), ==, "textured");
	g_assert_false (lrg_shader_asset_get_lit (asset));
	g_assert_cmpstr (lrg_shader_asset_get_blend_mode (asset), ==, "alpha");
	g_assert_null (lrg_shader_asset_get_source (asset, LRG_SHADER_STAGE_VERTEX));
	g_assert_nonnull (strstr (fragment, "if (sg_sample.a < 0.1) discard;"));

	/* uniforms in generation order: the texture is reached first */
	g_assert_cmpuint (lrg_shader_asset_get_n_uniforms (asset), ==, 2);
	u = lrg_shader_asset_get_uniform_at (asset, 0);
	g_assert_cmpstr (lrg_shader_uniform_get_name (u), ==, "albedo_map");
	g_assert_cmpint (lrg_shader_uniform_get_uniform_type (u), ==, LRG_SHADER_UNIFORM_SAMPLER2D);
	g_assert_cmpstr (lrg_shader_uniform_get_default_texture (u), ==, "textures/rock.png");
	u = lrg_shader_asset_get_uniform_at (asset, 1);
	g_assert_cmpint (lrg_shader_uniform_get_uniform_type (u), ==, LRG_SHADER_UNIFORM_COLOR);
	lrg_shader_uniform_get_default (u, v);
	g_assert_cmpfloat (v[2], ==, 0.6f);
	g_assert_true (lrg_shader_uniform_get_range (u, &lo, &hi));
	g_assert_cmpstr (lrg_shader_uniform_get_hint (u), ==, "color");

	lit = lrg_shader_graph_generate (rim, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_shader_asset_get_lit (lit));
	g_assert_nonnull (lrg_shader_asset_get_uniform (lit, "rim_power"));
}

/* ---- GPU ---------------------------------------------------------------- */

static void
test_gpu_compile (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderGraph) textured = lrg_shader_graph_new_from_data (graph_fixture, -1, &error);
	g_autoptr(LrgShaderGraph) rim = build_rim ();
	g_autoptr(LrgShaderAsset) a = NULL;
	g_autoptr(LrgShaderAsset) b = NULL;

	SKIP_WITHOUT_GRAPHICS ();

	a = lrg_shader_graph_generate (textured, &error);
	g_assert_no_error (error);
	if (!lrg_shader_asset_compile (a, &error))
		g_error ("unlit graph failed to compile:\n%s", error->message);
	g_assert_cmpint (lrg_shader_asset_get_uniform_location (a, "tint"), >=, 0);

	/* the PBR master node against the real light-set library */
	b = lrg_shader_graph_generate (rim, &error);
	g_assert_no_error (error);
	if (!lrg_shader_asset_compile (b, &error))
		g_error ("pbr graph failed to compile:\n%s", error->message);
	g_assert_cmpint (lrg_shader_asset_get_uniform_location (b, "rim_power"), >=, 0);
}

static void
test_gpu_render (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new ();
	g_autoptr(LrgShaderAsset) asset = NULL;
	gfloat orange[3] = { 1.0f, 0.5f, 0.0f };
	RenderTexture2D target;
	Shader *shader;
	Image image;
	Color c;

	SKIP_WITHOUT_GRAPHICS ();

	add (graph, "parameter", "col");
	lrg_shader_graph_set_node_param (graph, "col", "name", "base_colour");
	lrg_shader_graph_set_node_param (graph, "col", "type", "vec3");
	lrg_shader_graph_set_node_values (graph, "col", "default", orange, 3);
	add (graph, "unlit_output", "out");
	link_ports (graph, "col", "value", "out", "color");

	asset = lrg_shader_graph_generate (graph, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_shader_asset_compile (asset, &error));
	g_assert_no_error (error);

	target = LoadRenderTexture (16, 16);
	shader = grl_shader_get_handle (lrg_shader_asset_get_shader (asset));
	BeginTextureMode (target);
	ClearBackground (BLACK);
	lrg_shader_asset_apply (asset, NULL);
	BeginShaderMode (*shader);
	DrawRectangle (0, 0, 16, 16, WHITE);
	EndShaderMode ();
	EndTextureMode ();
	image = LoadImageFromTexture (target.texture);
	c = GetImageColor (image, 8, 8);
	UnloadImage (image);
	UnloadRenderTexture (target);
	g_assert_cmpuint (c.r, >, 250);
	g_assert_cmpuint (c.g, >, 120);
	g_assert_cmpuint (c.g, <, 135);
	g_assert_cmpuint (c.b, <, 5);
	lrg_shader_asset_release (asset);
}

static void
test_gpu_render_lit (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgShaderGraph) graph = lrg_shader_graph_new ();
	g_autoptr(LrgShaderAsset) asset = NULL;
	g_autoptr(LrgLightSet) lights = NULL;
	g_autoptr(GrlVector3) down = grl_vector3_new (0.0f, -1.0f, 0.0f);
	g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
	g_autoptr(GrlColor) dim = grl_color_new (40, 40, 40, 255);
	gfloat red[3] = { 1.0f, 0.0f, 0.0f };
	gfloat one = 1.0f;
	RenderTexture2D target;
	Camera3D camera = { 0 };
	Mesh mesh;
	Model model;
	Image image;
	Color c;

	SKIP_WITHOUT_GRAPHICS ();

	/* a rough red dielectric lit by a white sun from straight above */
	add (graph, "pbr_output", "out");
	lrg_shader_graph_set_input_value (graph, "out", "albedo", red, 3);
	lrg_shader_graph_set_input_value (graph, "out", "roughness", &one, 1);
	asset = lrg_shader_graph_generate (graph, &error);
	g_assert_no_error (error);
	if (!lrg_shader_asset_compile (asset, &error))
		g_error ("lit graph failed to compile:\n%s", error->message);

	lights = lrg_light_set_new ();
	lrg_light_set_set_ambient (lights, dim);
	lrg_light_set_set_directional (lights, down, white);
	lrg_light_set_apply (lights, 0.0, 3.0, 0.01);
	g_assert_true (lrg_shader_asset_bind_lights (asset, lights));
	lrg_shader_asset_apply (asset, NULL);

	mesh = GenMeshPlane (4.0f, 4.0f, 1, 1);
	model = LoadModelFromMesh (mesh);
	model.materials[0].shader = *(Shader *) grl_shader_get_handle (lrg_shader_asset_get_shader (asset));

	camera.position = (Vector3) { 0.0f, 3.0f, 0.01f };
	camera.target = (Vector3) { 0.0f, 0.0f, 0.0f };
	camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	camera.fovy = 45.0f;
	camera.projection = CAMERA_PERSPECTIVE;

	target = LoadRenderTexture (16, 16);
	BeginTextureMode (target);
	ClearBackground (BLACK);
	BeginMode3D (camera);
	DrawModel (model, (Vector3) { 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
	EndMode3D ();
	EndTextureMode ();
	image = LoadImageFromTexture (target.texture);
	c = GetImageColor (image, 8, 8);
	UnloadImage (image);
	UnloadRenderTexture (target);

	/* the program must not keep the model's copy alive: restore the
	 * default shader before unloading the model */
	model.materials[0].shader.id = rlGetShaderIdDefault ();
	model.materials[0].shader.locs = rlGetShaderLocsDefault ();
	UnloadModel (model);

	g_test_message ("lit centre: %u %u %u", c.r, c.g, c.b);
	g_assert_cmpuint (c.r, >, 100);
	g_assert_cmpuint (c.g, <, c.r / 4 + 1);
	g_assert_cmpuint (c.b, <, c.r / 4 + 1);
	lrg_shader_asset_release (asset);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/shader-graph/library", test_library);
	g_test_add_func ("/shader-graph/golden/unlit", test_golden_unlit);
	g_test_add_func ("/shader-graph/golden/pbr", test_golden_pbr);
	g_test_add_func ("/shader-graph/golden/split-swizzle", test_golden_split_swizzle);
	g_test_add_func ("/shader-graph/validation", test_validation);
	g_test_add_func ("/shader-graph/editing", test_editing);
	g_test_add_func ("/shader-graph/layout", test_default_layout);
	g_test_add_func ("/shader-graph/format/round-trip", test_format_round_trip);
	g_test_add_func ("/shader-graph/format/file", test_format_file);
	g_test_add_func ("/shader-graph/format/errors", test_format_errors);
	g_test_add_func ("/shader-graph/generate/asset", test_generate_asset);
	g_test_add_func ("/shader-graph/gpu/compile", test_gpu_compile);
	g_test_add_func ("/shader-graph/gpu/render", test_gpu_render);
	g_test_add_func ("/shader-graph/gpu/render-lit", test_gpu_render_lit);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
