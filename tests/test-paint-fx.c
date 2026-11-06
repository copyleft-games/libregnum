/* test-paint-fx.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Glass, backdrop blur, effect shaders, curves and icons.
 *
 * Headless tests record lists and inspect the instance and command
 * streams. GPU tests (run under a display, e.g. xvfb) render into a
 * texture or the window and read pixels back.
 */

#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include <libregnum.h>

#define TARGET_W (128)
#define TARGET_H (128)
#define WINDOW_W (128)
#define WINDOW_H (128)

static const GrlColor C_RED = { 255, 0, 0, 255 };
static const GrlColor C_GREEN = { 0, 255, 0, 255 };
static const GrlColor C_BLUE = { 0, 0, 255, 255 };
static const GrlColor C_WHITE = { 255, 255, 255, 255 };
static const GrlColor C_BLACK = { 0, 0, 0, 255 };
static const GrlColor C_CLEAR = { 0, 0, 0, 0 };

static gint
prim_of (const LrgPaintInstance *inst)
{
	return (gint) lrg_paint_instance_get_prim (inst);
}

/* ==========================================================================
 * Headless: glass and blur recording
 * ========================================================================== */

static void
test_glass_recording (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;
	LrgPaintGlass           glass;
	LrgCornerRadii          radii;
	guint                   first;
	guint                   count;

	lrg_paint_glass_init_default (&glass);
	glass.frost = 0.5f;
	glass.refraction = 6.0f;
	glass.blur_radius = 20.0f;
	glass.tint = C_RED;
	lrg_corner_radii_init_uniform (&radii, 12.0f);

	lrg_paint_list_glass (list, 10.0f, 20.0f, 100.0f, 50.0f, &radii, &glass);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 1);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpint (prim_of (inst), ==, LRG_PAINT_PRIM_GLASS);
	g_assert_cmpfloat (inst->rect[2], ==, 110.0f);
	g_assert_cmpfloat (inst->radii[0], ==, 12.0f);
	g_assert_cmpfloat (inst->params[1], ==, 20.0f);
	g_assert_cmpfloat (inst->extra[0], ==, 0.5f);
	g_assert_cmpfloat (inst->extra[1], ==, 6.0f);
	g_assert_cmpfloat (inst->data[2], ==, 1.0f);
	g_assert_cmpuint (inst->color[0], ==, 255);

	/* Its own command kind: the renderer captures the target first */
	g_assert_cmpint (lrg_paint_list_get_command (list, 0, &first, &count, NULL), ==,
	                 LRG_PAINT_COMMAND_BACKDROP);
	g_assert_cmpuint (first, ==, 0);
	g_assert_cmpuint (count, ==, 1);

	/* NULL params: the default glass; NULL radii: square */
	lrg_paint_list_glass (list, 200.0f, 20.0f, 40.0f, 40.0f, NULL, NULL);
	inst = lrg_paint_list_get_instance (list, 1);
	g_assert_cmpfloat (inst->radii[0], ==, 0.0f);
	g_assert_cmpfloat (inst->extra[0], ==, 0.35f);

	/* Empty or fully transparent: nothing */
	lrg_paint_list_glass (list, 0.0f, 0.0f, 0.0f, 10.0f, NULL, NULL);
	lrg_paint_list_push_opacity (list, 0.0f);
	lrg_paint_list_glass (list, 0.0f, 0.0f, 10.0f, 10.0f, NULL, NULL);
	lrg_paint_list_pop_opacity (list);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 2);
}

static void
test_blur_recording (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;

	lrg_paint_list_push_opacity (list, 0.5f);
	lrg_paint_list_blur (list, 0.0f, 0.0f, 64.0f, 32.0f, 8.0f, 10.0f, &C_BLUE);
	lrg_paint_list_pop_opacity (list);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpint (prim_of (inst), ==, LRG_PAINT_PRIM_BLUR);
	g_assert_cmpfloat (inst->params[1], ==, 10.0f);
	g_assert_cmpfloat (inst->extra[0], ==, 1.0f);   /* full frost */
	g_assert_cmpfloat (inst->extra[1], ==, 0.0f);   /* no refraction */
	g_assert_cmpfloat (inst->extra[3], ==, 0.0f);   /* no rim */
	g_assert_cmpfloat (inst->radii[2], ==, 8.0f);
	/* Opacity rides in data.z; the tint keeps its own alpha */
	g_assert_cmpfloat (inst->data[2], ==, 0.5f);
	g_assert_cmpuint (inst->color[3], ==, 255);
}

static void
test_backdrop_batching (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgPaintGlass           glass;
	guint                   count;

	lrg_paint_glass_init_default (&glass);
	glass.blur_radius = 4.0f;
	glass.refraction = 2.0f;

	/* Background, then a glass panel with its label */
	lrg_paint_list_rect (list, 0.0f, 0.0f, 400.0f, 200.0f, 0.0f, &C_BLUE);
	lrg_paint_list_glass (list, 10.0f, 10.0f, 80.0f, 80.0f, NULL, &glass);
	lrg_paint_list_rect (list, 20.0f, 20.0f, 20.0f, 10.0f, 0.0f, &C_WHITE);

	/* A second panel far away: shares the capture and the draw */
	lrg_paint_list_glass (list, 200.0f, 10.0f, 80.0f, 80.0f, NULL, &glass);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 2);
	g_assert_cmpint (lrg_paint_list_get_command (list, 0, NULL, NULL, NULL), ==,
	                 LRG_PAINT_COMMAND_INSTANCES);
	g_assert_cmpint (lrg_paint_list_get_command (list, 1, NULL, &count, NULL), ==,
	                 LRG_PAINT_COMMAND_BACKDROP);
	g_assert_cmpuint (count, ==, 3);

	/* A third panel over the first one's label must see the label:
	 * a new capture */
	lrg_paint_list_glass (list, 30.0f, 15.0f, 40.0f, 40.0f, NULL, &glass);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 3);
	g_assert_cmpint (lrg_paint_list_get_command (list, 2, NULL, &count, NULL), ==,
	                 LRG_PAINT_COMMAND_BACKDROP);
	g_assert_cmpuint (count, ==, 1);

	/* Plain shapes after a glass join its batch */
	lrg_paint_list_rect (list, 35.0f, 20.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 3);
}

static void
test_glass_group_transform (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;
	LrgPaintGlass           glass;
	guint                   mark;

	lrg_paint_glass_init_default (&glass);
	glass.refraction = 10.0f;
	glass.blur_radius = 8.0f;
	glass.tint = C_WHITE;

	mark = lrg_paint_list_begin_group (list);
	lrg_paint_list_glass (list, 0.0f, 0.0f, 100.0f, 100.0f, NULL, &glass);
	lrg_paint_list_transform_group (list, mark, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpfloat (inst->rect[2], ==, 50.0f);
	g_assert_cmpfloat (inst->params[1], ==, 4.0f);
	g_assert_cmpfloat (inst->extra[1], ==, 5.0f);
	/* Fades through data.z, not the tint */
	g_assert_cmpfloat (inst->data[2], ==, 0.5f);
	g_assert_cmpuint (inst->color[3], ==, 255);
}

static void
test_glass_lerp (void)
{
	LrgPaintGlass a;
	LrgPaintGlass b;
	LrgPaintGlass out;
	g_autoptr(LrgPaintGlass) copy = NULL;

	lrg_paint_glass_init_default (&a);
	b = a;
	a.frost = 0.0f;
	b.frost = 1.0f;
	a.tint = C_BLACK;
	b.tint = C_WHITE;
	lrg_paint_glass_lerp (&a, &b, 0.5f, &out);
	g_assert_cmpfloat_with_epsilon (out.frost, 0.5f, 1e-5f);
	g_assert_cmpuint (out.tint.r, >=, 126);
	g_assert_cmpuint (out.tint.r, <=, 129);

	copy = lrg_paint_glass_copy (&out);
	g_assert_cmpfloat (copy->frost, ==, out.frost);
}


/* ==========================================================================
 * Headless: effects
 * ========================================================================== */

static void
test_effect_registry (void)
{
	g_auto(GStrv) names = NULL;

	g_assert_cmpint (lrg_paint_effect_lookup ("shimmer"), ==, 1);
	g_assert_cmpint (lrg_paint_effect_lookup ("mesh-gradient"), ==, 8);
	g_assert_cmpint (lrg_paint_effect_lookup ("nope"), ==, -1);
	g_assert_cmpint (lrg_paint_effect_lookup (NULL), ==, -1);
	g_assert_cmpstr (lrg_paint_effect_get_name (2), ==, "aurora");
	g_assert_null (lrg_paint_effect_get_name (LRG_PAINT_EFFECT_NONE));
	g_assert_null (lrg_paint_effect_get_name (63));

	names = lrg_paint_effect_list_names ();
	g_assert_true (g_strv_length (names) >= 8);
	g_assert_true (g_strv_contains ((const gchar * const *) names, "neon"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "spotlight"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "ripple"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "grain"));
	g_assert_true (g_strv_contains ((const gchar * const *) names, "plasma"));
}

static const gchar *green_effect =
	"vec4 lrg_effect (LrgEffect fx)\n"
	"{\n"
	"    return vec4 (0.0, 1.0, 0.0, 1.0);\n"
	"}\n";

/* user effects are process-wide: register once for every test */
static gint
green_effect_id (void)
{
	static gint id = -1;

	if (id < 0)
	{
		g_autoptr(GError) error = NULL;

		id = lrg_paint_effect_register ("test-green", green_effect, &error);
		g_assert_no_error (error);
	}
	return id;
}

static void
test_effect_register (void)
{
	g_autoptr(GError) error = NULL;
	gint              id;

	id = green_effect_id ();
	g_assert_cmpint (id, >=, LRG_PAINT_EFFECT_FIRST_USER);
	g_assert_cmpint (lrg_paint_effect_lookup ("test-green"), ==, id);
	g_assert_cmpstr (lrg_paint_effect_get_name (id), ==, "test-green");

	/* Bad names, bad snippets, duplicates */
	g_assert_cmpint (lrg_paint_effect_register ("Bad Name", green_effect, &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_cmpint (lrg_paint_effect_register ("no-func", "vec4 f () { return vec4 (1.0); }",
	                                            &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_cmpint (lrg_paint_effect_register ("with-version",
	                                            "#version 330\nvec4 lrg_effect (LrgEffect fx) { return fx.color1; }",
	                                            &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_cmpint (lrg_paint_effect_register ("test-green", green_effect, &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
	g_clear_error (&error);
	g_assert_cmpint (lrg_paint_effect_register ("aurora", green_effect, &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
}

static void
test_effect_recording (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;
	LrgPaintEffectParams    params;
	g_autoptr(LrgPaintEffectParams) copy = NULL;

	lrg_paint_effect_params_init_default (&params);
	params.color1 = C_RED;
	params.color2 = C_GREEN;
	params.color3 = C_BLUE;
	params.hover = 0.25f;
	params.press = 2.0f;
	params.intensity = 0.75f;
	params.speed = 2.0f;

	lrg_paint_list_push_opacity (list, 0.5f);
	lrg_paint_list_effect (list, 0.0f, 0.0f, 40.0f, 20.0f, NULL,
	                       lrg_paint_effect_lookup ("neon"), &params);
	lrg_paint_list_pop_opacity (list);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpint (prim_of (inst), ==, LRG_PAINT_PRIM_EFFECT);
	g_assert_cmpfloat (inst->extra[3], ==, 3.0f);
	g_assert_cmpfloat (inst->extra[0], ==, 0.25f);
	g_assert_cmpfloat (inst->extra[1], ==, 1.0f);    /* clamped */
	g_assert_cmpfloat (inst->extra[2], ==, 0.75f);
	g_assert_cmpfloat (inst->data[0], ==, 2.0f);
	g_assert_cmpfloat (inst->data[2], ==, 0.5f);
	/* palette unfaded: the shader fades the result by data.z */
	g_assert_cmpuint (inst->color[3], ==, 255);
	g_assert_cmpuint (inst->color2[1], ==, 255);
	g_assert_cmpuint (inst->border[2], ==, 255);

	/* Effects batch with plain shapes */
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 1);

	/* Unknown ids draw nothing; NULL params use the defaults */
	lrg_paint_list_effect (list, 0.0f, 0.0f, 40.0f, 20.0f, NULL, 999, NULL);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 2);
	lrg_paint_list_effect (list, 0.0f, 0.0f, 40.0f, 20.0f, NULL, 1, NULL);
	g_assert_cmpfloat (lrg_paint_list_get_instance (list, 2)->extra[2], ==, 1.0f);

	copy = lrg_paint_effect_params_copy (&params);
	g_assert_cmpfloat (copy->speed, ==, 2.0f);
}

static void
test_time_and_pointer (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	gfloat                  x;
	gfloat                  y;

	g_assert_cmpfloat (lrg_paint_list_get_time (list), ==, 0.0);
	g_assert_false (lrg_paint_list_get_pointer (list, &x, &y));

	lrg_paint_list_set_time (list, 12.5);
	lrg_paint_list_set_pointer (list, 3.0f, 4.0f);
	lrg_paint_list_clear (list);

	/* Frame inputs survive clear */
	g_assert_cmpfloat (lrg_paint_list_get_time (list), ==, 12.5);
	g_assert_true (lrg_paint_list_get_pointer (list, &x, &y));
	g_assert_cmpfloat (x, ==, 3.0f);
	g_assert_cmpfloat (y, ==, 4.0f);

	lrg_paint_list_unset_pointer (list);
	g_assert_false (lrg_paint_list_get_pointer (list, NULL, NULL));
}

/* ==========================================================================
 * Headless: curves
 * ========================================================================== */

static void
test_curve_recording (void)
{
	g_autoptr(LrgPaintList)       list = lrg_paint_list_new ();
	const LrgPaintInstance       *inst;
	LrgPaintCurveStyle            style;
	g_autoptr(LrgPaintCurveStyle) copy = NULL;
	guint                         mark;

	lrg_paint_curve_style_init (&style, &C_RED);
	style.color_end = C_BLUE;
	style.glow = C_WHITE;
	style.glow_size = 6.0f;
	style.pulse_count = 12;
	style.pulse_color = C_GREEN;

	mark = lrg_paint_list_begin_group (list);
	lrg_paint_list_curve (list, 0.0f, 0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f,
	                      3.0f, &style);
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpint (prim_of (inst), ==, LRG_PAINT_PRIM_CURVE);
	g_assert_cmpfloat (inst->rect[2], ==, 50.0f);
	g_assert_cmpfloat (inst->extra[1], ==, 20.0f);
	g_assert_cmpfloat (inst->extra[2], ==, 30.0f);
	g_assert_cmpfloat (inst->params[1], ==, 3.0f);
	g_assert_cmpfloat (inst->data[0], ==, 6.0f);
	g_assert_cmpfloat (inst->data[1], ==, 8.0f);   /* at most 8 pulses */
	g_assert_cmpfloat (inst->radii[1], ==, 1.0f);  /* pulse colour, green */
	g_assert_cmpuint (inst->color2[2], ==, 255);

	/* Group transforms move every control point; the pulse colour is
	 * not a size */
	lrg_paint_list_transform_group (list, mark, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	g_assert_cmpfloat (inst->rect[2], ==, 101.0f);
	g_assert_cmpfloat (inst->extra[2], ==, 61.0f);
	g_assert_cmpfloat (inst->extra[3], ==, 80.0f);
	g_assert_cmpfloat (inst->params[1], ==, 6.0f);
	g_assert_cmpfloat (inst->data[0], ==, 12.0f);
	g_assert_cmpfloat (inst->radii[1], ==, 1.0f);

	/* Nothing to draw: skipped */
	lrg_paint_list_curve (list, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 3.0f, 0.0f, &style);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 1);

	copy = lrg_paint_curve_style_copy (&style);
	g_assert_cmpuint (copy->pulse_count, ==, 12);
}


/* ==========================================================================
 * Headless: icons
 * ========================================================================== */

static void
test_icon_names (void)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_ICON);
	guint                 i;

	g_assert_cmpint (lrg_paint_icon_from_name ("check"), ==, LRG_ICON_CHECK);
	g_assert_cmpint (lrg_paint_icon_from_name ("chevron-down"), ==, LRG_ICON_CHEVRON_DOWN);
	g_assert_cmpint (lrg_paint_icon_from_name ("star-half"), ==, LRG_ICON_STAR_HALF);
	g_assert_cmpint (lrg_paint_icon_from_name ("nonsense"), ==, LRG_ICON_NONE);
	g_assert_cmpint (lrg_paint_icon_from_name (NULL), ==, LRG_ICON_NONE);

	for (i = 0; i < klass->n_values; i++)
		g_assert_cmpint (lrg_paint_icon_from_name (lrg_paint_icon_get_name ((LrgIcon) klass->values[i].value)),
		                 ==, klass->values[i].value);
}

static void
test_icon_recording (void)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_ICON);
	guint                 i;

	/* Every icon draws something, and only inside its box */
	for (i = 1; i < klass->n_values; i++)
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
		guint                   n;
		guint                   k;

		lrg_paint_list_icon (list, (LrgIcon) klass->values[i].value, 100.0f, 200.0f, 32.0f,
		                     0.0f, &C_WHITE);
		n = lrg_paint_list_get_n_instances (list);
		if (n == 0)
			g_error ("icon %s draws nothing", klass->values[i].value_nick);

		for (k = 0; k < n; k++)
		{
			const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, k);
			gfloat                  slack = 3.0f;

			g_assert_cmpfloat (MIN (inst->rect[0], inst->rect[2]), >=, 100.0f - slack);
			g_assert_cmpfloat (MAX (inst->rect[0], inst->rect[2]), <=, 132.0f + slack);
			g_assert_cmpfloat (MIN (inst->rect[1], inst->rect[3]), >=, 200.0f - slack);
			g_assert_cmpfloat (MAX (inst->rect[1], inst->rect[3]), <=, 232.0f + slack);
		}
	}

	/* NONE and transparent draw nothing */
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

		lrg_paint_list_icon (list, LRG_ICON_NONE, 0.0f, 0.0f, 32.0f, 0.0f, &C_WHITE);
		lrg_paint_list_icon (list, LRG_ICON_CHECK, 0.0f, 0.0f, 32.0f, 0.0f, &C_CLEAR);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 0);
	}
}

static const gchar *svg_circle =
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
	"<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#00ff00\"/></svg>";

static void
test_svg_icon_rasterize (void)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(LrgSvgIcon) icon = NULL;
	g_autoptr(GrlImage)   image = NULL;
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	GrlColor             *c;

	icon = lrg_svg_icon_new_from_data (svg_circle, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_svg_icon_get_n_shapes (icon), ==, 1);

	/* Fitted into the square: full circle, transparent corners */
	image = lrg_svg_icon_rasterize (icon, 32);
	g_assert_cmpint (grl_image_get_width (image), ==, 32);
	c = grl_image_get_pixel (image, 16, 16);
	g_assert_cmpuint (c->g, >=, 250);
	g_assert_cmpuint (c->a, >=, 250);
	grl_color_free (c);
	c = grl_image_get_pixel (image, 1, 1);
	g_assert_cmpuint (c->a, ==, 0);
	grl_color_free (c);

	g_assert_null (lrg_svg_icon_new_from_data ("not svg", -1, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);
	g_assert_null (lrg_svg_icon_new_from_file ("/nonexistent/icon.svg", &error));
	g_assert_nonnull (error);
}

/* ==========================================================================
 * GPU harness
 * ========================================================================== */

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

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (WINDOW_W, WINDOW_H, "lrg-paint-fx-test");
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

/* render_list: draw @list over black into a texture, return pixels */
static Image
render_list (LrgPaintList *list)
{
	RenderTexture2D target = LoadRenderTexture (TARGET_W, TARGET_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (), list));
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

/* render_list_to_screen: draw @list into the window's back buffer */
static Image
render_list_to_screen (LrgPaintList *list)
{
	Image image;

	BeginDrawing ();
	ClearBackground ((Color){ 0, 0, 0, 255 });
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (), list));
	rlDrawRenderBatchActive ();
	/* read the back buffer before the swap */
	image = LoadImageFromScreen ();
	EndDrawing ();
	return image;
}

static Color
pixel (Image *image,
       gint   x,
       gint   y)
{
	return GetImageColor (*image, x, y);
}

/* sharp_edge: white on the left half, black on the right */
static void
record_sharp_edge (LrgPaintList *list)
{
	lrg_paint_list_rect (list, 0.0f, 0.0f, 64.0f, (gfloat) TARGET_H, 0.0f, &C_WHITE);
}

/* ==========================================================================
 * GPU: glass and blur
 * ========================================================================== */

/* check_edge_blurred: around x = 64 the step became a ramp */
static void
check_edge_blurred (Image *img,
                    gint   y)
{
	Color far_left = pixel (img, 30, y);
	Color near_left = pixel (img, 61, y);
	Color near_right = pixel (img, 66, y);
	Color far_right = pixel (img, 100, y);
	gint  x;

	for (x = 52; x < 78; x += 2)
		g_test_message ("x=%d r=%u", x, pixel (img, x, y).r);

	g_assert_cmpuint (far_left.r, >=, 235);
	g_assert_cmpuint (far_right.r, <=, 20);
	/* Inside the blur radius both sides are mid grey */
	g_assert_cmpuint (near_left.r, <, 235);
	g_assert_cmpuint (near_left.r, >, 100);
	g_assert_cmpuint (near_right.r, >, 20);
	g_assert_cmpuint (near_right.r, <, 160);
	g_assert_cmpuint (near_left.r, >, near_right.r);
}

static void
test_gpu_blur_softens_edge (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintRenderer       *renderer;
	Image                   img;

	SKIP_WITHOUT_GL ();
	renderer = lrg_paint_renderer_get_default ();
	list = lrg_paint_list_new ();
	record_sharp_edge (list);

	/* Without a blur the edge is a hard step */
	img = render_list (list);
	g_assert_cmpuint (pixel (&img, 63, 64).r, >=, 250);
	g_assert_cmpuint (pixel (&img, 64, 64).r, <=, 5);
	g_assert_cmpuint (lrg_paint_renderer_get_backdrop_passes (renderer), ==, 0);
	UnloadImage (img);

	/* A blur over it turns the step into a gradient */
	lrg_paint_list_blur (list, 8.0f, 8.0f, 112.0f, 112.0f, 0.0f, 16.0f, NULL);
	img = render_list (list);
	g_assert_cmpuint (lrg_paint_renderer_get_backdrop_passes (renderer), ==, 1);
	check_edge_blurred (&img, 64);

	/* Outside the blur shape the frame is untouched */
	g_assert_cmpuint (pixel (&img, 63, 3).r, >=, 250);
	g_assert_cmpuint (pixel (&img, 64, 3).r, <=, 5);
	UnloadImage (img);
}

static void
test_gpu_glass_on_screen (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintGlass           glass;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	record_sharp_edge (list);

	/* Fully frosted, untinted glass with no rim: a pure blur */
	lrg_paint_glass_init_default (&glass);
	glass.frost = 1.0f;
	glass.refraction = 0.0f;
	glass.rim_light = 0.0f;
	glass.dispersion = 0.0f;
	glass.saturation = 1.0f;
	glass.tint = C_CLEAR;
	glass.blur_radius = 16.0f;
	lrg_paint_list_glass (list, 8.0f, 8.0f, 112.0f, 112.0f, NULL, &glass);

	img = render_list_to_screen (list);
	ImageFlipVertical (&img);
	g_assert_cmpint (img.width, ==, WINDOW_W);
	check_edge_blurred (&img, 64);
	UnloadImage (img);
}

static void
test_gpu_glass_in_texture_with_transform (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintGlass           glass;
	RenderTexture2D         target;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();

	/* Half-size canvas drawn at 2x into a render texture, as a game
	 * rendering its UI into a scaled target */
	lrg_paint_list_rect (list, 0.0f, 0.0f, 32.0f, 64.0f, 0.0f, &C_WHITE);
	lrg_paint_glass_init_default (&glass);
	glass.frost = 1.0f;
	glass.refraction = 0.0f;
	glass.rim_light = 0.0f;
	glass.dispersion = 0.0f;
	glass.saturation = 1.0f;
	glass.tint = C_CLEAR;
	glass.blur_radius = 8.0f;
	lrg_paint_list_glass (list, 4.0f, 4.0f, 56.0f, 56.0f, NULL, &glass);

	target = LoadRenderTexture (TARGET_W, TARGET_H);
	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	rlPushMatrix ();
	rlScalef (2.0f, 2.0f, 1.0f);
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (), list));
	rlPopMatrix ();
	EndTextureMode ();
	img = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&img);
	UnloadRenderTexture (target);

	check_edge_blurred (&img, 64);
	UnloadImage (img);
}

static void
test_gpu_glass_tint_and_rim (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintGlass           glass;
	LrgCornerRadii          radii;
	Image                   img;
	Color                   middle;
	Color                   top_left;
	Color                   bottom_right;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &(GrlColor){ 40, 40, 40, 255 });

	lrg_paint_glass_init_default (&glass);
	glass.tint = (GrlColor){ 0, 0, 255, 128 };
	glass.rim_light = 1.0f;
	lrg_corner_radii_init_uniform (&radii, 16.0f);
	lrg_paint_list_glass (list, 16.0f, 16.0f, 96.0f, 96.0f, &radii, &glass);

	img = render_list (list);
	middle = pixel (&img, 64, 64);
	top_left = pixel (&img, 40, 16);
	bottom_right = pixel (&img, 88, 111);

	/* The tint shows; the backdrop grey still shows through it */
	g_assert_cmpuint (middle.b, >, 100);
	g_assert_cmpuint (middle.r, >, 5);
	g_assert_cmpuint (middle.r, <, 40);
	/* The rim facing the light (top-left) is brighter than the far one */
	g_assert_cmpuint (top_left.r, >, middle.r + 20);
	g_assert_cmpuint (top_left.r, >, bottom_right.r);
	/* Outside the rounded corner: the backdrop, unchanged */
	g_assert_cmpuint (pixel (&img, 17, 17).r, ==, 40);
	UnloadImage (img);
}

static void
test_gpu_glass_refraction (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintGlass           glass;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();

	/* A thin red bar just inside the glass's left rim */
	lrg_paint_list_rect (list, 30.0f, 0.0f, 4.0f, 128.0f, 0.0f, &C_RED);

	lrg_paint_glass_init_default (&glass);
	glass.frost = 0.0f;
	glass.refraction = 12.0f;
	glass.rim_light = 0.0f;
	glass.dispersion = 0.0f;
	glass.saturation = 1.0f;
	glass.tint = C_CLEAR;
	glass.blur_radius = 0.0f;
	lrg_paint_list_glass (list, 24.0f, 16.0f, 80.0f, 96.0f, NULL, &glass);

	img = render_list (list);
	/* Above the glass the bar is where it was drawn */
	g_assert_cmpuint (pixel (&img, 31, 8).r, >=, 250);
	/* Inside the rim the backdrop is pulled in from further inside:
	 * the bar no longer shows at its true place */
	g_assert_cmpuint (pixel (&img, 31, 64).r, <, 128);
	g_assert_cmpuint (pixel (&img, 64, 64).r, <=, 5);
	UnloadImage (img);

	/* Same glass without refraction: the bar shows where it is */
	lrg_paint_list_clear (list);
	lrg_paint_list_rect (list, 30.0f, 0.0f, 4.0f, 128.0f, 0.0f, &C_RED);
	glass.refraction = 0.0f;
	lrg_paint_list_glass (list, 24.0f, 16.0f, 80.0f, 96.0f, NULL, &glass);
	img = render_list (list);
	g_assert_cmpuint (pixel (&img, 31, 64).r, >=, 240);
	UnloadImage (img);
}

static void
test_gpu_backdrop_pass_count (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintRenderer       *renderer;
	LrgPaintGlass           glass;
	Image                   img;

	SKIP_WITHOUT_GL ();
	renderer = lrg_paint_renderer_get_default ();
	list = lrg_paint_list_new ();
	lrg_paint_glass_init_default (&glass);
	glass.blur_radius = 4.0f;
	glass.refraction = 2.0f;

	/* Plain shapes, effects and curves: no offscreen work at all */
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &C_BLUE);
	lrg_paint_list_effect (list, 0.0f, 0.0f, 64.0f, 64.0f, NULL,
	                       lrg_paint_effect_lookup ("aurora"), NULL);
	img = render_list (list);
	UnloadImage (img);
	g_assert_cmpuint (lrg_paint_renderer_get_backdrop_passes (renderer), ==, 0);
	g_assert_cmpuint (lrg_paint_renderer_get_draw_calls (renderer), ==, 1);

	/* Two disjoint panels: one capture, one draw */
	lrg_paint_list_glass (list, 4.0f, 4.0f, 40.0f, 40.0f, NULL, &glass);
	lrg_paint_list_glass (list, 80.0f, 80.0f, 40.0f, 40.0f, NULL, &glass);
	img = render_list (list);
	UnloadImage (img);
	g_assert_cmpuint (lrg_paint_renderer_get_backdrop_passes (renderer), ==, 1);
	g_assert_cmpuint (lrg_paint_renderer_get_draw_calls (renderer), ==, 2);

	/* Content on the first panel, then a panel over it: a second capture */
	lrg_paint_list_rect (list, 10.0f, 10.0f, 10.0f, 10.0f, 0.0f, &C_RED);
	lrg_paint_list_glass (list, 8.0f, 8.0f, 30.0f, 30.0f, NULL, &glass);
	img = render_list (list);
	UnloadImage (img);
	g_assert_cmpuint (lrg_paint_renderer_get_backdrop_passes (renderer), ==, 2);

	/* And back to nothing */
	lrg_paint_list_clear (list);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 8.0f, 8.0f, 0.0f, &C_GREEN);
	img = render_list (list);
	UnloadImage (img);
	g_assert_cmpuint (lrg_paint_renderer_get_backdrop_passes (renderer), ==, 0);
}


/* ==========================================================================
 * GPU: effects
 * ========================================================================== */

static gint
color_distance (Color a,
                Color b)
{
	return ABS ((gint) a.r - (gint) b.r) + ABS ((gint) a.g - (gint) b.g) +
	       ABS ((gint) a.b - (gint) b.b);
}

/* bright_params: a palette where every effect leaves visible marks */
static void
bright_params (LrgPaintEffectParams *params)
{
	lrg_paint_effect_params_init_default (params);
	params->color1 = (GrlColor){ 60, 20, 90, 255 };
	params->color2 = (GrlColor){ 255, 160, 0, 255 };
	params->color3 = (GrlColor){ 0, 220, 255, 255 };
	params->hover = 1.0f;
	params->press = 1.0f;
}

static void
test_gpu_every_effect_draws (void)
{
	static const gchar * const names[] = {
		"shimmer", "aurora", "neon", "spotlight", "ripple", "grain", "plasma",
		"mesh-gradient", NULL
	};
	guint i;

	SKIP_WITHOUT_GL ();

	for (i = 0; names[i] != NULL; i++)
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
		LrgPaintEffectParams    params;
		LrgCornerRadii          radii;
		Image                   img;
		Color                   inside;

		bright_params (&params);
		lrg_corner_radii_init_uniform (&radii, 20.0f);
		lrg_paint_list_set_time (list, 1.0);
		lrg_paint_list_set_pointer (list, 64.0f, 64.0f);
		lrg_paint_list_effect (list, 16.0f, 16.0f, 96.0f, 96.0f, &radii,
		                       lrg_paint_effect_lookup (names[i]), &params);
		img = render_list (list);

		/* Something non-black inside, nothing in the rounded corner */
		inside = pixel (&img, 64, 64);
		g_test_message ("%s: %u %u %u", names[i], inside.r, inside.g, inside.b);
		g_assert_cmpint (inside.r + inside.g + inside.b, >, 30);
		g_assert_cmpint (color_distance (pixel (&img, 17, 17), (Color){ 0, 0, 0, 255 }), ==, 0);
		g_assert_cmpint (color_distance (pixel (&img, 4, 64), (Color){ 0, 0, 0, 255 }), ==, 0);
		UnloadImage (img);
	}
}

/* frame_diff: summed colour change between two times of one effect */
static gint
frame_diff (const gchar *name,
            gdouble      t0,
            gdouble      t1)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgPaintEffectParams    params;
	Image                   a;
	Image                   b;
	gint                    diff = 0;
	gint                    x;
	gint                    y;

	bright_params (&params);
	lrg_paint_list_set_pointer (list, 64.0f, 64.0f);
	lrg_paint_list_effect (list, 0.0f, 0.0f, 128.0f, 128.0f, NULL,
	                       lrg_paint_effect_lookup (name), &params);
	lrg_paint_list_set_time (list, t0);
	a = render_list (list);
	lrg_paint_list_set_time (list, t1);
	b = render_list (list);

	for (y = 4; y < 124; y += 8)
		for (x = 4; x < 124; x += 8)
			diff += color_distance (pixel (&a, x, y), pixel (&b, x, y));

	UnloadImage (a);
	UnloadImage (b);
	return diff;
}

static void
test_gpu_effects_animate_with_time (void)
{
	SKIP_WITHOUT_GL ();

	/* The list's time drives the animation ... */
	g_assert_cmpint (frame_diff ("plasma", 0.0, 1.3), >, 500);
	g_assert_cmpint (frame_diff ("aurora", 0.0, 2.0), >, 200);
	g_assert_cmpint (frame_diff ("shimmer", 0.2, 1.0), >, 200);
	g_assert_cmpint (frame_diff ("ripple", 0.0, 0.2), >, 200);
	/* ... and nothing else: the same time gives the same frame */
	g_assert_cmpint (frame_diff ("plasma", 0.7, 0.7), ==, 0);
	g_assert_cmpint (frame_diff ("grain", 0.5, 0.5), ==, 0);
}

static void
test_gpu_spotlight_follows_pointer (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintEffectParams    params;
	Image                   img;
	Color                   left;
	Color                   right;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	bright_params (&params);
	params.press = 0.0f;
	lrg_paint_list_effect (list, 0.0f, 0.0f, 128.0f, 128.0f, NULL,
	                       lrg_paint_effect_lookup ("spotlight"), &params);

	lrg_paint_list_set_pointer (list, 20.0f, 64.0f);
	img = render_list (list);
	left = pixel (&img, 20, 64);
	right = pixel (&img, 108, 64);
	UnloadImage (img);
	g_assert_cmpint (left.r, >, right.r + 40);

	lrg_paint_list_set_pointer (list, 108.0f, 64.0f);
	img = render_list (list);
	left = pixel (&img, 20, 64);
	right = pixel (&img, 108, 64);
	UnloadImage (img);
	g_assert_cmpint (right.r, >, left.r + 40);

	/* No pointer: no light */
	lrg_paint_list_unset_pointer (list);
	img = render_list (list);
	g_assert_cmpint (pixel (&img, 64, 64).r, <=, 62);
	UnloadImage (img);
}

static void
test_gpu_ripple_stronger_on_press (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintEffectParams    params;
	Image                   img;
	gint                    hover_only = 0;
	gint                    pressed = 0;
	gint                    x;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_set_pointer (list, 64.0f, 64.0f);
	lrg_paint_list_set_time (list, 0.3);

	bright_params (&params);
	params.press = 0.0f;
	lrg_paint_list_effect (list, 0.0f, 0.0f, 128.0f, 128.0f, NULL,
	                       lrg_paint_effect_lookup ("ripple"), &params);
	img = render_list (list);
	for (x = 0; x < 128; x += 2)
		hover_only += color_distance (pixel (&img, x, 64), (Color){ 60, 20, 90, 255 });
	UnloadImage (img);

	lrg_paint_list_clear (list);
	params.press = 1.0f;
	lrg_paint_list_effect (list, 0.0f, 0.0f, 128.0f, 128.0f, NULL,
	                       lrg_paint_effect_lookup ("ripple"), &params);
	img = render_list (list);
	for (x = 0; x < 128; x += 2)
		pressed += color_distance (pixel (&img, x, 64), (Color){ 60, 20, 90, 255 });
	UnloadImage (img);

	g_assert_cmpint (pressed, >, hover_only * 2);
}

static void
test_gpu_user_effect (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintRenderer       *renderer;
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();
	renderer = lrg_paint_renderer_get_default ();
	list = lrg_paint_list_new ();
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &C_BLUE);
	lrg_paint_list_effect (list, 32.0f, 32.0f, 64.0f, 64.0f, NULL, green_effect_id (), NULL);
	lrg_paint_list_effect (list, 0.0f, 0.0f, 16.0f, 16.0f, NULL,
	                       lrg_paint_effect_lookup ("plasma"), NULL);
	img = render_list (list);

	c = pixel (&img, 64, 64);
	g_assert_cmpuint (c.g, >=, 250);
	g_assert_cmpuint (c.r, <=, 5);
	g_assert_cmpuint (pixel (&img, 20, 64).b, >=, 250);
	/* user and built-in effects share the batch */
	g_assert_cmpuint (lrg_paint_renderer_get_draw_calls (renderer), ==, 1);
	UnloadImage (img);
}

/* ==========================================================================
 * GPU: curves
 * ========================================================================== */

static void
bezier_point (gfloat  t,
              gfloat *x,
              gfloat *y)
{
	/* the S curve used below */
	static const gfloat px[4] = { 10.0f, 50.0f, 78.0f, 118.0f };
	static const gfloat py[4] = { 64.0f, 10.0f, 118.0f, 64.0f };
	gfloat u = 1.0f - t;

	*x = u * u * u * px[0] + 3.0f * u * u * t * px[1] + 3.0f * u * t * t * px[2] + t * t * t * px[3];
	*y = u * u * u * py[0] + 3.0f * u * u * t * py[1] + 3.0f * u * t * t * py[2] + t * t * t * py[3];
}

static void
record_s_curve (LrgPaintList             *list,
                const LrgPaintCurveStyle *style)
{
	lrg_paint_list_curve (list, 10.0f, 64.0f, 50.0f, 10.0f, 78.0f, 118.0f, 118.0f, 64.0f,
	                      4.0f, style);
}

static void
test_gpu_curve_follows_path (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintCurveStyle      style;
	Image                   img;
	gfloat                  t;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_curve_style_init (&style, &C_RED);
	style.color_end = C_BLUE;
	record_s_curve (list, &style);
	img = render_list (list);

	/* Every sampled point on the analytic curve is covered */
	for (t = 0.05f; t < 0.96f; t += 0.05f)
	{
		gfloat x;
		gfloat y;
		Color  c;

		bezier_point (t, &x, &y);
		c = pixel (&img, (gint) floorf (x), (gint) floorf (y));
		g_assert_cmpint (c.r + c.b, >, 200);
	}

	/* The colour runs from red to blue along it */
	{
		gfloat x;
		gfloat y;
		Color  c;

		bezier_point (0.1f, &x, &y);
		c = pixel (&img, (gint) floorf (x), (gint) floorf (y));
		g_assert_cmpuint (c.r, >, c.b);
		bezier_point (0.9f, &x, &y);
		c = pixel (&img, (gint) floorf (x), (gint) floorf (y));
		g_assert_cmpuint (c.b, >, c.r);
	}

	/* Off the curve: nothing (the hull corner and the straight chord) */
	g_assert_cmpint (color_distance (pixel (&img, 64, 20), (Color){ 0, 0, 0, 255 }), ==, 0);
	g_assert_cmpint (color_distance (pixel (&img, 30, 64), (Color){ 0, 0, 0, 255 }), <=, 3);
	UnloadImage (img);
}

static void
test_gpu_curve_glow_and_pulses (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintCurveStyle      style;
	Image                   img;
	Color                   mid_t0;
	Color                   mid_t1;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();

	/* Glow: light beyond the stroke edge */
	lrg_paint_curve_style_init (&style, &C_RED);
	style.glow = C_WHITE;
	style.glow_size = 10.0f;
	lrg_paint_list_curve (list, 10.0f, 64.0f, 40.0f, 64.0f, 88.0f, 64.0f, 118.0f, 64.0f,
	                      4.0f, &style);
	img = render_list (list);
	g_assert_cmpuint (pixel (&img, 64, 69).g, >, 20);
	g_assert_cmpuint (pixel (&img, 64, 80).g, ==, 0);
	UnloadImage (img);

	/* One green pulse at the middle at t = 0, moved on at t = 1 */
	lrg_paint_list_clear (list);
	lrg_paint_curve_style_init (&style, &C_RED);
	style.pulse_count = 1;
	style.pulse_color = C_GREEN;
	style.pulse_speed = 0.25f;
	style.pulse_phase = 0.5f;
	lrg_paint_list_curve (list, 10.0f, 64.0f, 40.0f, 64.0f, 88.0f, 64.0f, 118.0f, 64.0f,
	                      4.0f, &style);
	lrg_paint_list_set_time (list, 0.0);
	img = render_list (list);
	mid_t0 = pixel (&img, 64, 64);
	UnloadImage (img);
	lrg_paint_list_set_time (list, 1.0);
	img = render_list (list);
	mid_t1 = pixel (&img, 64, 64);
	g_assert_cmpuint (pixel (&img, 91, 64).g, >, 200);
	UnloadImage (img);

	g_assert_cmpuint (mid_t0.g, >, 200);
	g_assert_cmpuint (mid_t1.g, <, 40);
	g_assert_cmpuint (mid_t1.r, >, 200);
}


/* ==========================================================================
 * GPU: icons
 * ========================================================================== */

/* icon_image: one icon, white on black, filling most of the target */
static Image
icon_image (LrgIcon icon)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

	lrg_paint_list_icon (list, icon, 16.0f, 16.0f, 96.0f, 0.0f, &C_WHITE);
	return render_list (list);
}

/* lit: the pixel at unit position (u, v) of the icon box is drawn */
static gboolean
lit (Image  *img,
     gfloat  u,
     gfloat  v)
{
	return pixel (img, (gint) (16.0f + u * 96.0f), (gint) (16.0f + v * 96.0f)).r > 128;
}

static void
test_gpu_icons_render (void)
{
	g_autoptr(GEnumClass) klass = NULL;
	Image                 img;
	guint                 i;

	SKIP_WITHOUT_GL ();
	klass = g_type_class_ref (LRG_TYPE_ICON);

	/* Every icon puts ink in its box and none outside it */
	for (i = 1; i < klass->n_values; i++)
	{
		gint x;
		gint y;
		gint ink = 0;
		gint outside = 0;

		img = icon_image ((LrgIcon) klass->values[i].value);
		for (y = 0; y < TARGET_H; y += 2)
			for (x = 0; x < TARGET_W; x += 2)
			{
				gboolean on = pixel (&img, x, y).r > 40;
				gboolean in_box = x >= 14 && x < 114 && y >= 14 && y < 114;

				if (on && in_box)
					ink++;
				else if (on)
					outside++;
			}
		UnloadImage (img);
		if (ink < 20 || outside > 0)
			g_error ("icon %s: %d ink pixels, %d outside", klass->values[i].value_nick, ink, outside);
	}

	/* Spot checks of a few shapes */
	img = icon_image (LRG_ICON_PLUS);
	g_assert_true (lit (&img, 0.5f, 0.5f));
	g_assert_true (lit (&img, 0.5f, 0.25f));
	g_assert_false (lit (&img, 0.25f, 0.25f));
	UnloadImage (img);

	img = icon_image (LRG_ICON_PLAY);
	g_assert_true (lit (&img, 0.45f, 0.5f));
	g_assert_false (lit (&img, 0.75f, 0.25f));
	UnloadImage (img);

	img = icon_image (LRG_ICON_CHECK);
	g_assert_true (lit (&img, 0.42f, 0.72f));
	g_assert_false (lit (&img, 0.5f, 0.5f));
	UnloadImage (img);

	/* Star variants: filled, outline (hollow), half (left filled) */
	img = icon_image (LRG_ICON_STAR);
	g_assert_true (lit (&img, 0.5f, 0.54f));
	g_assert_true (lit (&img, 0.6f, 0.54f));
	UnloadImage (img);
	img = icon_image (LRG_ICON_STAR_OUTLINE);
	g_assert_false (lit (&img, 0.5f, 0.54f));
	UnloadImage (img);
	img = icon_image (LRG_ICON_STAR_HALF);
	g_assert_true (lit (&img, 0.42f, 0.56f));
	g_assert_false (lit (&img, 0.58f, 0.56f));
	UnloadImage (img);

	/* Gear: a hole in the middle, a solid ring around it */
	img = icon_image (LRG_ICON_GEAR);
	g_assert_false (lit (&img, 0.5f, 0.5f));
	g_assert_true (lit (&img, 0.5f, 0.23f));
	UnloadImage (img);
}

static void
test_gpu_svg_icon (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgSvgIcon)   icon = NULL;
	g_autoptr(LrgPaintList) list = NULL;
	GrlTexture             *tex;
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();
	icon = lrg_svg_icon_new_from_data (svg_circle, -1, &error);
	g_assert_no_error (error);

	/* One texture per pixel size, reused */
	tex = lrg_svg_icon_get_texture (icon, 48);
	g_assert_nonnull (tex);
	g_assert_true (tex == lrg_svg_icon_get_texture (icon, 48));
	g_assert_true (tex != lrg_svg_icon_get_texture (icon, 24));

	list = lrg_paint_list_new ();
	lrg_paint_list_svg_icon (list, icon, 40.0f, 40.0f, 48.0f, NULL);
	lrg_paint_list_svg_icon (list, icon, 0.0f, 0.0f, 24.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 2);
	img = render_list (list);

	c = pixel (&img, 64, 64);
	g_assert_cmpuint (c.g, >=, 240);
	g_assert_cmpuint (c.r, <=, 10);
	/* tinted red: green x red = black, but drawn (covers the corner box) */
	g_assert_cmpuint (pixel (&img, 12, 12).g, <=, 10);
	g_assert_cmpuint (pixel (&img, 41, 41).g, <=, 10);
	UnloadImage (img);

	lrg_svg_icon_clear_cache (icon);
	g_assert_true (lrg_svg_icon_get_texture (icon, 48) != NULL);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/paint-fx/glass/recording", test_glass_recording);
	g_test_add_func ("/paint-fx/glass/blur-recording", test_blur_recording);
	g_test_add_func ("/paint-fx/glass/batching", test_backdrop_batching);
	g_test_add_func ("/paint-fx/glass/group-transform", test_glass_group_transform);
	g_test_add_func ("/paint-fx/glass/lerp", test_glass_lerp);
	g_test_add_func ("/paint-fx/effect/registry", test_effect_registry);
	g_test_add_func ("/paint-fx/effect/register", test_effect_register);
	g_test_add_func ("/paint-fx/effect/recording", test_effect_recording);
	g_test_add_func ("/paint-fx/effect/time-and-pointer", test_time_and_pointer);
	g_test_add_func ("/paint-fx/curve/recording", test_curve_recording);
	g_test_add_func ("/paint-fx/icon/names", test_icon_names);
	g_test_add_func ("/paint-fx/icon/recording", test_icon_recording);
	g_test_add_func ("/paint-fx/icon/svg-rasterize", test_svg_icon_rasterize);

	graphics_available = init_graphics_context ();

	g_test_add_func ("/paint-fx/gpu/blur-softens-edge", test_gpu_blur_softens_edge);
	g_test_add_func ("/paint-fx/gpu/glass-on-screen", test_gpu_glass_on_screen);
	g_test_add_func ("/paint-fx/gpu/glass-in-texture-transformed", test_gpu_glass_in_texture_with_transform);
	g_test_add_func ("/paint-fx/gpu/glass-tint-and-rim", test_gpu_glass_tint_and_rim);
	g_test_add_func ("/paint-fx/gpu/glass-refraction", test_gpu_glass_refraction);
	g_test_add_func ("/paint-fx/gpu/backdrop-pass-count", test_gpu_backdrop_pass_count);
	g_test_add_func ("/paint-fx/gpu/every-effect-draws", test_gpu_every_effect_draws);
	g_test_add_func ("/paint-fx/gpu/effects-animate", test_gpu_effects_animate_with_time);
	g_test_add_func ("/paint-fx/gpu/spotlight-follows-pointer", test_gpu_spotlight_follows_pointer);
	g_test_add_func ("/paint-fx/gpu/ripple-press", test_gpu_ripple_stronger_on_press);
	g_test_add_func ("/paint-fx/gpu/user-effect", test_gpu_user_effect);
	g_test_add_func ("/paint-fx/gpu/curve-follows-path", test_gpu_curve_follows_path);
	g_test_add_func ("/paint-fx/gpu/curve-glow-and-pulses", test_gpu_curve_glow_and_pulses);
	g_test_add_func ("/paint-fx/gpu/icons", test_gpu_icons_render);
	g_test_add_func ("/paint-fx/gpu/svg-icon", test_gpu_svg_icon);

	result = g_test_run ();

	g_clear_object (&test_window);
	return result;
}
