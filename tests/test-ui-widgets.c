/* test-ui-widgets.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the built-in widgets after the move to paint() and part looks.
 *
 * Most cases are headless: the widget is put on a canvas, driven through
 * lrg_canvas_process_input() exactly like the real input loop, recorded
 * with lrg_canvas_record() and the resulting paint list inspected. A few
 * GPU cases render the canvas into a render texture under a hidden
 * window and probe pixels; they skip without a display.
 */

#include <glib.h>
#include <libregnum.h>
#include <raylib.h>

/* ==========================================================================
 * Fixture and input helpers
 * ========================================================================== */

typedef struct
{
	LrgCanvas *canvas;
	LrgTheme  *theme;
} Fixture;

static void
fixture_set_up (Fixture       *fixture,
                gconstpointer  user_data)
{
	/* Each test gets its own theme so presets / rules never leak */
	fixture->theme = lrg_theme_new_preset ("midnight");
	fixture->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (fixture->canvas), 800.0f, 600.0f);
	lrg_widget_set_theme (LRG_WIDGET (fixture->canvas), fixture->theme);
}

static void
fixture_tear_down (Fixture       *fixture,
                   gconstpointer  user_data)
{
	g_clear_object (&fixture->canvas);
	g_clear_object (&fixture->theme);
	lrg_motion_set_reduced (FALSE);
}

/* add_at: put @widget on the canvas at a fixed rectangle */
static gpointer
add_at (Fixture   *fixture,
        gpointer   widget,
        gfloat     x,
        gfloat     y,
        gfloat     w,
        gfloat     h)
{
	lrg_widget_set_position (LRG_WIDGET (widget), x, y);
	lrg_widget_set_size (LRG_WIDGET (widget), w, h);
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (widget));
	g_object_unref (widget);
	return widget;
}

/* frame_pointer: an input frame with the pointer at (@x, @y) only */
static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = 1.0f / 60.0f;
}

/* move_to: one frame with the pointer at (@x, @y) */
static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

/* press_at / release_at: half a click */
static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
drag_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

/* click_at: a full left click (move, press, release) */
static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	release_at (canvas, x, y);
}

/* key: one frame with a key press and modifiers */
static void
key (LrgCanvas         *canvas,
     GrlKey             k,
     LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	lrg_canvas_input_add_key (&input, k);
	input.modifiers = mods;
	lrg_canvas_process_input (canvas, &input);
}

/* activate: one frame with the gamepad / Enter activation */
static void
activate (LrgCanvas *canvas)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	input.activate = TRUE;
	lrg_canvas_process_input (canvas, &input);
}

/* settle: advance every spring by @seconds in 60 Hz steps */
static void
settle (LrgCanvas *canvas,
        gfloat     seconds)
{
	gint i;
	gint n = (gint) (seconds * 60.0f);

	for (i = 0; i < n; i++)
		lrg_canvas_update (canvas, 1.0f / 60.0f);
}

/*
 * run_until_rest:
 *
 * Steps 60 Hz frames until lrg_canvas_update() reports nothing is
 * animating any more (at most @max_frames).
 *
 * Returns: the number of frames that still reported motion
 */
static guint
run_until_rest (LrgCanvas *canvas,
                guint      max_frames)
{
	guint n = 0;

	while (n < max_frames && lrg_canvas_update (canvas, 1.0f / 60.0f))
		n++;
	g_assert_cmpuint (n, <, max_frames);
	return n;
}

/* ==========================================================================
 * Paint-list inspection helpers
 * ========================================================================== */

/* color_is: does an instance colour equal @c (within @tol per channel)? */
static gboolean
color_is (const guint8   *bytes,
          const GrlColor *c,
          gint            tol)
{
	return ABS ((gint) bytes[0] - (gint) c->r) <= tol &&
	       ABS ((gint) bytes[1] - (gint) c->g) <= tol &&
	       ABS ((gint) bytes[2] - (gint) c->b) <= tol &&
	       ABS ((gint) bytes[3] - (gint) c->a) <= tol;
}

/*
 * find_rect:
 * Index of the first instance of @prim whose rectangle is (@x, @y,
 * @w, @h) within half a unit, or -1.
 */
static gint
find_rect (LrgPaintList *list,
           LrgPaintPrim  prim,
           gfloat        x,
           gfloat        y,
           gfloat        w,
           gfloat        h)
{
	guint i;
	guint n = lrg_paint_list_get_n_instances (list);

	for (i = 0; i < n; i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (lrg_paint_instance_get_prim (inst) != prim)
			continue;
		if (fabsf (inst->rect[0] - x) <= 0.5f && fabsf (inst->rect[1] - y) <= 0.5f &&
		    fabsf (inst->rect[2] - (x + w)) <= 0.5f && fabsf (inst->rect[3] - (y + h)) <= 0.5f)
			return (gint) i;
	}
	return -1;
}

/* find_color: index of the first @prim instance filled with @c, or -1 */
static gint
find_color (LrgPaintList   *list,
            LrgPaintPrim    prim,
            const GrlColor *c)
{
	guint i;
	guint n = lrg_paint_list_get_n_instances (list);

	for (i = 0; i < n; i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (lrg_paint_instance_get_prim (inst) == prim && color_is (inst->color, c, 1))
			return (gint) i;
	}
	return -1;
}

/* find_prim: index of the first @prim instance, or -1 */
static gint
find_prim (LrgPaintList *list,
           LrgPaintPrim  prim)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, i)) == prim)
			return (gint) i;
	return -1;
}

static const LrgPaintInstance *
instance (LrgPaintList *list,
          gint          index)
{
	g_assert_cmpint (index, >=, 0);
	return lrg_paint_list_get_instance (list, (guint) index);
}

/* ==========================================================================
 * GPU helpers
 * ========================================================================== */

#define GPU_W (256)
#define GPU_H (128)

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
	test_window = grl_window_new (8, 8, "lrg-ui-widgets-test");
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

/* render_canvas: draw @canvas over black into a texture, return pixels */
static Image
render_canvas (LrgCanvas *canvas)
{
	RenderTexture2D target = LoadRenderTexture (GPU_W, GPU_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_canvas_render (canvas);
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

/* pixel_near: does the pixel at (@x, @y) match @c within @tol? */
static gboolean
pixel_near (Image          *image,
            gint            x,
            gint            y,
            const GrlColor *c,
            gint            tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) c->r) <= tol &&
	       ABS ((gint) p.g - (gint) c->g) <= tol &&
	       ABS ((gint) p.b - (gint) c->b) <= tol;
}

/* ==========================================================================
 * Button
 * ========================================================================== */

static void
on_count (gpointer instance,
          gpointer user_data)
{
	gint *count = user_data;

	(*count)++;
}

/* A primary button rests on the theme's ACCENT colour */
static void
test_button_primary_rest (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgButton    *button;
	LrgPaintList *list;
	gint          idx;

	button = add_at (fixture, lrg_button_new ("Play"), 100.0f, 100.0f, 120.0f, 40.0f);
	lrg_widget_add_class (LRG_WIDGET (button), "primary");

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 120.0f, 40.0f);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 1));

	/* Rounded with the theme radius, and a soft shadow underneath */
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->radii[0],
	                                lrg_theme_get_metric (fixture->theme,
	                                                      LRG_THEME_METRIC_CORNER_RADIUS), 0.01);
	g_assert_cmpint (find_prim (list, LRG_PAINT_PRIM_SHADOW), >=, 0);
	g_assert_cmpint (find_prim (list, LRG_PAINT_PRIM_SHADOW), <, idx);
}

/* Hovering blends towards ACCENT_HOVER as the springs move */
static void
test_button_hover_blends (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgButton      *button;
	LrgPaintList   *list;
	const GrlColor *rest = lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor *hover = lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT_HOVER);
	gint            idx;

	button = add_at (fixture, lrg_button_new ("Play"), 100.0f, 100.0f, 120.0f, 40.0f);
	lrg_widget_add_class (LRG_WIDGET (button), "primary");

	move_to (fixture->canvas, 150.0f, 120.0f);
	g_assert_true (lrg_button_get_is_hovered (button));

	/* One frame in: neither the resting nor the hovered colour */
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 120.0f, 40.0f);
	g_assert_false (color_is (instance (list, idx)->color, rest, 0));
	g_assert_false (color_is (instance (list, idx)->color, hover, 0));

	/* Settled: exactly the hover colour, with an accent glow */
	settle (fixture->canvas, 1.5f);
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 120.0f, 40.0f);
	g_assert_true (color_is (instance (list, idx)->color, hover, 1));
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >=, 3);

	/* Leaving clears the mirror property */
	move_to (fixture->canvas, 10.0f, 10.0f);
	g_assert_false (lrg_button_get_is_hovered (button));
}

/* An explicitly set legacy colour still wins over the theme */
static void
test_button_explicit_color_wins (Fixture       *fixture,
                                 gconstpointer  user_data)
{
	static const GrlColor red = { 200, 30, 40, 255 };
	LrgButton            *button;
	LrgPaintList         *list;
	gint                  idx;

	button = add_at (fixture, lrg_button_new ("Quit"), 10.0f, 10.0f, 100.0f, 36.0f);
	lrg_widget_add_class (LRG_WIDGET (button), "primary");
	lrg_button_set_normal_color (button, &red);

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 36.0f);
	g_assert_true (color_is (instance (list, idx)->color, &red, 1));

	/* Explicit radius too */
	lrg_button_set_corner_radius (button, 3.0f);
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 36.0f);
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->radii[0], 3.0f, 0.01);
}

/* Flat buttons have no visible surface until hovered */
static void
test_button_flat (Fixture       *fixture,
                  gconstpointer  user_data)
{
	LrgButton    *button;
	LrgPaintList *list;

	button = add_at (fixture, lrg_button_new ("More"), 10.0f, 10.0f, 100.0f, 36.0f);
	lrg_widget_add_class (LRG_WIDGET (button), "flat");

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 36.0f), ==, -1);

	move_to (fixture->canvas, 50.0f, 20.0f);
	settle (fixture->canvas, 1.0f);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 36.0f), >=, 0);
}

/* Pressing squashes the button about its centre and mirrors is-pressed */
static void
test_button_press_squash (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgButton    *button;
	LrgPaintList *list;
	gint          clicks = 0;
	guint         i;
	gboolean      found = FALSE;

	button = add_at (fixture, lrg_button_new ("Go"), 100.0f, 100.0f, 100.0f, 40.0f);
	g_signal_connect (button, "clicked", G_CALLBACK (on_count), &clicks);

	move_to (fixture->canvas, 150.0f, 120.0f);
	press_at (fixture->canvas, 150.0f, 120.0f);
	g_assert_true (lrg_button_get_is_pressed (button));
	settle (fixture->canvas, 0.5f);

	/* The body is now smaller than the allocation, centred on it */
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 100.0f, 40.0f), ==, -1);
	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (lrg_paint_instance_get_prim (inst) == LRG_PAINT_PRIM_RECT &&
		    fabsf ((inst->rect[2] - inst->rect[0]) - 96.0f) < 0.5f)
		{
			g_assert_cmpfloat_with_epsilon ((inst->rect[0] + inst->rect[2]) * 0.5f, 150.0f, 0.5);
			found = TRUE;
		}
	}
	g_assert_true (found);

	release_at (fixture->canvas, 150.0f, 120.0f);
	g_assert_false (lrg_button_get_is_pressed (button));
	g_assert_cmpint (clicks, ==, 1);
}

/* Enter / Space / gamepad A on the focused button clicks it */
static void
test_button_activate (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgButton *button;
	gint       clicks = 0;

	button = add_at (fixture, lrg_button_new ("OK"), 10.0f, 10.0f, 80.0f, 36.0f);
	g_signal_connect (button, "clicked", G_CALLBACK (on_count), &clicks);

	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (button));
	activate (fixture->canvas);
	g_assert_cmpint (clicks, ==, 1);

	key (fixture->canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpint (clicks, ==, 2);
}

/* Preferred size follows the text and the theme metrics */
static void
test_button_measure (Fixture       *fixture,
                     gconstpointer  user_data)
{
	LrgButton *button;
	gfloat     w1;
	gfloat     h1;
	gfloat     w2;
	gfloat     h2;

	button = add_at (fixture, lrg_button_new ("A"), 0.0f, 0.0f, 10.0f, 10.0f);
	lrg_widget_measure (LRG_WIDGET (button), &w1, &h1);
	g_assert_cmpfloat (h1, >=, lrg_theme_get_metric (fixture->theme,
	                                                 LRG_THEME_METRIC_CONTROL_HEIGHT));
	g_assert_cmpfloat (w1, >=, 2.0f * lrg_theme_get_metric (fixture->theme,
	                                                        LRG_THEME_METRIC_PADDING_X));

	lrg_button_set_text (button, "A much longer label");
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (button)));
	lrg_widget_measure (LRG_WIDGET (button), &w2, &h2);
	g_assert_cmpfloat (w2, >, w1);
	g_assert_cmpfloat_with_epsilon (h2, h1, 0.01);
}

/* Disabled buttons fade and ignore clicks */
static void
test_button_disabled (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgButton *button;
	gint       clicks = 0;

	button = add_at (fixture, lrg_button_new ("No"), 10.0f, 10.0f, 80.0f, 36.0f);
	g_signal_connect (button, "clicked", G_CALLBACK (on_count), &clicks);
	lrg_widget_set_enabled (LRG_WIDGET (button), FALSE);

	click_at (fixture->canvas, 40.0f, 20.0f);
	g_assert_cmpint (clicks, ==, 0);
}

/* GPU: a primary button fills with ACCENT in every preset */
static void
test_gpu_button (Fixture       *fixture,
                 gconstpointer  user_data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	LrgButton           *button;
	guint                i;

	SKIP_WITHOUT_GL ();

	button = add_at (fixture, lrg_button_new ("Play"), 40.0f, 40.0f, 140.0f, 44.0f);
	lrg_widget_add_class (LRG_WIDGET (button), "primary");

	for (i = 0; presets[i] != NULL; i++)
	{
		Image img;

		lrg_theme_apply_preset (fixture->theme, presets[i]);
		img = render_canvas (fixture->canvas);

		/* Body colour away from the text, black well outside */
		g_assert_true (pixel_near (&img, 46, 62,
		                           lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 3));
		g_assert_true (pixel_near (&img, 240, 120, &(GrlColor){ 0, 0, 0, 255 }, 0));
		UnloadImage (img);
	}
}

/* ==========================================================================
 * Label
 * ========================================================================== */

/* Default, muted and explicit text colours */
static void
test_label_colors (Fixture       *fixture,
                   gconstpointer  user_data)
{
	static const GrlColor gold = { 250, 200, 40, 255 };
	LrgLabel             *label;
	LrgPartLook           look;

	label = add_at (fixture, lrg_label_new ("Hi"), 10.0f, 10.0f, 100.0f, 20.0f);
	g_assert_false (lrg_widget_get_focusable (LRG_WIDGET (label)));

	lrg_widget_resolve_look (LRG_WIDGET (label), LRG_UI_PART_LABEL, &look);
	g_assert_true (lrg_paint_color_contrast (&look.foreground,
	                                         lrg_theme_get_color (fixture->theme,
	                                                              LRG_THEME_COLOR_TEXT)) < 1.01f);

	lrg_widget_add_class (LRG_WIDGET (label), "muted");
	lrg_widget_resolve_look (LRG_WIDGET (label), LRG_UI_PART_LABEL, &look);
	g_assert_cmpuint (look.foreground.r, ==,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TEXT_MUTED)->r);
	g_assert_cmpuint (look.foreground.b, ==,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TEXT_MUTED)->b);

	/* Explicit colour wins over the class */
	lrg_label_set_color (label, &gold);
	lrg_widget_resolve_look (LRG_WIDGET (label), LRG_UI_PART_LABEL, &look);
	g_assert_cmpuint (look.foreground.r, ==, 250);
	g_assert_cmpuint (look.foreground.g, ==, 200);
}

/* Style classes pick the theme's font sizes; an explicit size wins */
static void
test_label_class_sizes (Fixture       *fixture,
                        gconstpointer  user_data)
{
	LrgLabel *label;
	gfloat    h;

	label = add_at (fixture, lrg_label_new ("Title"), 0.0f, 0.0f, 10.0f, 10.0f);

	lrg_widget_measure (LRG_WIDGET (label), NULL, &h);
	g_assert_cmpfloat_with_epsilon (h, lrg_theme_get_metric (fixture->theme,
	                                                         LRG_THEME_METRIC_FONT_SIZE), 0.01);

	lrg_widget_add_class (LRG_WIDGET (label), "title");
	lrg_widget_measure (LRG_WIDGET (label), NULL, &h);
	g_assert_cmpfloat_with_epsilon (h, lrg_theme_get_metric (fixture->theme,
	                                                         LRG_THEME_METRIC_FONT_SIZE_TITLE), 0.01);
	lrg_widget_remove_class (LRG_WIDGET (label), "title");

	lrg_widget_add_class (LRG_WIDGET (label), "heading");
	lrg_widget_measure (LRG_WIDGET (label), NULL, &h);
	g_assert_cmpfloat_with_epsilon (h, lrg_theme_get_metric (fixture->theme,
	                                                         LRG_THEME_METRIC_FONT_SIZE_LARGE), 0.01);
	lrg_widget_remove_class (LRG_WIDGET (label), "heading");

	lrg_widget_add_class (LRG_WIDGET (label), "caption");
	lrg_widget_measure (LRG_WIDGET (label), NULL, &h);
	g_assert_cmpfloat_with_epsilon (h, lrg_theme_get_metric (fixture->theme,
	                                                         LRG_THEME_METRIC_FONT_SIZE_SMALL), 0.01);

	lrg_label_set_font_size (label, 40.0f);
	lrg_widget_measure (LRG_WIDGET (label), NULL, &h);
	g_assert_cmpfloat_with_epsilon (h, 40.0f, 0.01);
}

/* Several lines measure as one size plus a line height per extra line,
 * and text changes queue a layout */
static void
test_label_multiline (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgLabel *label;
	gfloat    w1;
	gfloat    w2;
	gfloat    h;

	label = add_at (fixture, lrg_label_new ("one line"), 0.0f, 0.0f, 10.0f, 10.0f);
	lrg_label_set_font_size (label, 10.0f);
	lrg_widget_measure (LRG_WIDGET (label), &w1, NULL);

	lrg_canvas_record (fixture->canvas);
	g_assert_false (lrg_widget_get_needs_layout (LRG_WIDGET (label)));
	lrg_label_set_text (label, "one line\ntwo\nthree");
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (label)));

	lrg_widget_measure (LRG_WIDGET (label), &w2, &h);
	g_assert_cmpfloat_with_epsilon (w2, w1, 0.01);
	g_assert_cmpfloat_with_epsilon (h, 10.0f + 2.0f * 12.0f, 0.01);

	/* Alignment is kept */
	lrg_label_set_alignment (label, LRG_TEXT_ALIGN_RIGHT);
	g_assert_cmpint (lrg_label_get_alignment (label), ==, LRG_TEXT_ALIGN_RIGHT);
	lrg_canvas_record (fixture->canvas);
}


/* ==========================================================================
 * Checkbox
 * ========================================================================== */

static void
on_toggled (LrgCheckbox *box,
            gboolean     checked,
            gpointer     user_data)
{
	gint *count = user_data;

	(*count)++;
}

/* Clicks and ACTIVATE toggle; CHECKED mirrors the property */
static void
test_checkbox_toggle (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgCheckbox *box;
	gint         toggles = 0;

	box = add_at (fixture, lrg_checkbox_new ("Sound"), 10.0f, 10.0f, 150.0f, 24.0f);
	g_signal_connect (box, "toggled", G_CALLBACK (on_toggled), &toggles);

	click_at (fixture->canvas, 20.0f, 20.0f);
	g_assert_true (lrg_checkbox_get_checked (box));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (box)) & LRG_WIDGET_STATE_CHECKED);
	g_assert_cmpint (toggles, ==, 1);

	/* Clicked, so it has focus: gamepad A toggles back */
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (box));
	activate (fixture->canvas);
	g_assert_false (lrg_checkbox_get_checked (box));
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (box)) & LRG_WIDGET_STATE_CHECKED);

	/* Space too (via the canvas' activate fallback) */
	key (fixture->canvas, GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_checkbox_get_checked (box));
	g_assert_cmpint (toggles, ==, 3);
}

/* Checked CHECK style: ACCENT box and a two-stroke tick */
static void
test_checkbox_check_mark (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgCheckbox  *box;
	LrgPaintList *list;
	gfloat        s = lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_CHECK_SIZE);
	guint         i;
	guint         lines = 0;
	gint          idx;

	lrg_motion_set_reduced (TRUE);
	box = add_at (fixture, lrg_checkbox_new (NULL), 10.0f, 10.0f, s, s);

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, s, s);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_CONTROL), 1));

	lrg_checkbox_set_checked (box, TRUE);
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, s, s);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 1));
	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, i)) == LRG_PAINT_PRIM_LINE)
			lines++;
	g_assert_cmpuint (lines, ==, 2);
	g_assert_cmpfloat (lrg_checkbox_get_check_progress (box), ==, 1.0f);
}

/* Explicit legacy colours still win */
static void
test_checkbox_explicit_colors (Fixture       *fixture,
                               gconstpointer  user_data)
{
	static const GrlColor green = { 50, 200, 50, 255 };
	LrgCheckbox          *box;
	LrgPaintList         *list;

	lrg_motion_set_reduced (TRUE);
	box = add_at (fixture, lrg_checkbox_new (NULL), 10.0f, 10.0f, 20.0f, 20.0f);
	lrg_checkbox_set_box_size (box, 20.0f);
	lrg_checkbox_set_check_color (box, &green);
	lrg_checkbox_set_checked (box, TRUE);

	list = lrg_canvas_record (fixture->canvas);
	g_assert_true (color_is (instance (list, find_rect (list, LRG_PAINT_PRIM_RECT,
	                                                    10.0f, 10.0f, 20.0f, 20.0f))->color,
	                         &green, 1));
}

/* index of the switch knob: the HANDLE-coloured rect */
static gint
find_knob (Fixture      *fixture,
           LrgPaintList *list)
{
	return find_color (list, LRG_PAINT_PRIM_RECT,
	                   lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_HANDLE));
}

/* The switch knob slides on a spring and the track turns ACCENT */
static void
test_checkbox_switch (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgCheckbox  *sw;
	LrgPaintList *list;
	gfloat        s = lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_CHECK_SIZE);
	gfloat        x_off;
	gfloat        x_mid;
	gfloat        x_on;
	gfloat        w;
	gfloat        h;

	sw = add_at (fixture, lrg_checkbox_new_with_style ("Wi-Fi", LRG_CHECKBOX_STYLE_SWITCH),
	             10.0f, 10.0f, 200.0f, s);
	g_assert_cmpint (lrg_checkbox_get_style (sw), ==, LRG_CHECKBOX_STYLE_SWITCH);

	/* Wider than a plain check box */
	lrg_widget_measure (LRG_WIDGET (sw), &w, &h);
	g_assert_cmpfloat (h, >=, s);

	list = lrg_canvas_record (fixture->canvas);
	x_off = instance (list, find_knob (fixture, list))->rect[0];
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, s * 1.75f, s), >=, 0);

	/* The switch's first frame: nothing to animate yet */
	run_until_rest (fixture->canvas, 240);

	/* Toggle: a few frames later the knob is on its way, and the
	 * canvas reports the animation */
	lrg_checkbox_set_checked (sw, TRUE);
	g_assert_true (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	g_assert_true (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (fixture->canvas);
	x_mid = instance (list, find_knob (fixture, list))->rect[0];
	g_assert_cmpfloat (x_mid, >, x_off);
	g_assert_cmpfloat (lrg_checkbox_get_check_progress (sw), <, 1.0f);

	/* Settled (update returns FALSE): one track length minus the knob
	 * along, track is ACCENT */
	g_assert_cmpuint (run_until_rest (fixture->canvas, 240), >, 0);
	g_assert_false (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (fixture->canvas);
	x_on = instance (list, find_knob (fixture, list))->rect[0];
	g_assert_cmpfloat_with_epsilon (x_on - x_off, s * 0.75f, 0.5);
	g_assert_cmpfloat (x_on, >=, x_mid);
	g_assert_true (color_is (instance (list, find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f,
	                                                    s * 1.75f, s))->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 1));
}

/* Radio: a dot appears inside the circle once checked */
static void
test_checkbox_radio (Fixture       *fixture,
                     gconstpointer  user_data)
{
	LrgCheckbox  *radio;
	LrgPaintList *list;
	const GrlColor *dot = lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TEXT_ON_ACCENT);

	lrg_motion_set_reduced (TRUE);
	radio = add_at (fixture, lrg_checkbox_new_with_style (NULL, LRG_CHECKBOX_STYLE_RADIO),
	                10.0f, 10.0f, 20.0f, 20.0f);
	lrg_checkbox_set_box_size (radio, 20.0f);

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT, dot), ==, -1);

	lrg_checkbox_set_checked (radio, TRUE);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 16.0f, 16.0f, 8.0f, 8.0f), >=, 0);
	/* the circle is round */
	g_assert_cmpfloat_with_epsilon (instance (list, find_rect (list, LRG_PAINT_PRIM_RECT,
	                                                           10.0f, 10.0f, 20.0f, 20.0f))->radii[0],
	                                10.0f, 0.01);
}

/* GPU: the switch knob is drawn where the paint list says, in every preset */
static void
test_gpu_switch (Fixture       *fixture,
                 gconstpointer  user_data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	LrgCheckbox         *sw;
	guint                i;

	SKIP_WITHOUT_GL ();
	lrg_motion_set_reduced (TRUE);

	sw = add_at (fixture, lrg_checkbox_new_with_style (NULL, LRG_CHECKBOX_STYLE_SWITCH),
	             40.0f, 40.0f, 70.0f, 40.0f);
	lrg_checkbox_set_box_size (sw, 40.0f);

	for (i = 0; presets[i] != NULL; i++)
	{
		const GrlColor *accent;
		const GrlColor *handle;
		Image           img;

		lrg_theme_apply_preset (fixture->theme, presets[i]);
		accent = lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT);
		handle = lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_HANDLE);

		/* Off: knob on the left (centre x = 60), track to the right */
		lrg_checkbox_set_checked (sw, FALSE);
		img = render_canvas (fixture->canvas);
		g_assert_true (pixel_near (&img, 60, 60, handle, 4));
		g_assert_false (pixel_near (&img, 90, 60, handle, 4));
		UnloadImage (img);

		/* On: knob slid right (centre x = 90), accent on the left */
		lrg_checkbox_set_checked (sw, TRUE);
		img = render_canvas (fixture->canvas);
		g_assert_true (pixel_near (&img, 90, 60, handle, 4));
		g_assert_true (pixel_near (&img, 50, 60, accent, 4));
		UnloadImage (img);
	}
}


/* ==========================================================================
 * Slider
 * ========================================================================== */

/* Track, accent fill and handle-coloured thumb straight from the theme */
static void
test_slider_parts (Fixture       *fixture,
                   gconstpointer  user_data)
{
	LrgSlider    *slider;
	LrgPaintList *list;
	gfloat        handle = lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_HANDLE_SIZE);
	gfloat        cx;
	gint          idx;

	slider = add_at (fixture, lrg_slider_new (), 100.0f, 100.0f, 200.0f, 30.0f);
	lrg_slider_set_value (slider, 50.0);

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT,
	                             lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TRACK)), >=, 0);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT,
	                             lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT)), >=, 0);

	/* Resting thumb: HANDLE_SIZE across, centred on the value */
	cx = 100.0f + handle / 2.0f + 0.5f * (200.0f - handle);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, cx - handle / 2.0f, 115.0f - handle / 2.0f,
	                 handle, handle);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_HANDLE), 1));

	/* Hovered: the thumb swells */
	move_to (fixture->canvas, 150.0f, 115.0f);
	settle (fixture->canvas, 1.5f);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, cx - handle / 2.0f,
	                            115.0f - handle / 2.0f, handle, handle), ==, -1);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT,
	                             lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_HANDLE)), >=, 0);
	idx = find_color (list, LRG_PAINT_PRIM_RECT,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_HANDLE));
	g_assert_cmpfloat (instance (list, idx)->rect[2] - instance (list, idx)->rect[0], >, handle);
}

/* Explicit legacy colours win over the theme */
static void
test_slider_explicit_colors (Fixture       *fixture,
                             gconstpointer  user_data)
{
	static const GrlColor green = { 20, 200, 60, 255 };
	static const GrlColor grey = { 90, 90, 90, 255 };
	LrgSlider            *slider;
	LrgPaintList         *list;

	slider = add_at (fixture, lrg_slider_new (), 10.0f, 10.0f, 200.0f, 30.0f);
	lrg_slider_set_value (slider, 40.0);
	lrg_slider_set_fill_color (slider, &green);
	lrg_slider_set_track_color (slider, &grey);

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT, &green), >=, 0);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT, &grey), >=, 0);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT,
	                             lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT)), ==, -1);
}

/* Focused: arrows along the axis step, Home / End jump, focus stays */
static void
test_slider_keyboard (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgSlider *slider;
	LrgSlider *other;
	gint       changes = 0;

	slider = add_at (fixture, lrg_slider_new_with_range (0.0, 10.0, 2.0), 10.0f, 10.0f,
	                 200.0f, 30.0f);
	other = add_at (fixture, lrg_slider_new (), 300.0f, 10.0f, 200.0f, 30.0f);
	g_signal_connect (slider, "value-changed", G_CALLBACK (on_count), &changes);
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (slider));

	key (fixture->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 4.0, 0.0001);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (slider));

	key (fixture->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 2.0, 0.0001);

	key (fixture->canvas, GRL_KEY_END, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 10.0, 0.0001);
	key (fixture->canvas, GRL_KEY_HOME, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 0.0, 0.0001);
	g_assert_cmpint (changes, ==, 5);

	/* Up / Down are not this slider's axis: they move focus on */
	key (fixture->canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 0.0, 0.0001);
	(void) other;
}

/* Vertical sliders step with Up / Down and leave Left / Right alone */
static void
test_slider_vertical (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgSlider *slider;
	gfloat     w;
	gfloat     h;

	slider = add_at (fixture, lrg_slider_new (), 10.0f, 10.0f, 30.0f, 200.0f);
	lrg_slider_set_orientation (slider, LRG_ORIENTATION_VERTICAL);
	lrg_widget_measure (LRG_WIDGET (slider), &w, &h);
	g_assert_cmpfloat (h, >, w);

	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (slider));
	key (fixture->canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 1.0, 0.0001);

	/* Pressing near the top picks a high value (top is max) */
	click_at (fixture->canvas, 25.0f, 12.0f);
	g_assert_cmpfloat (lrg_slider_get_value (slider), >=, 99.0);
}

/* Dragging keeps tracking outside the widget thanks to pointer capture */
static void
test_slider_drag (Fixture       *fixture,
                  gconstpointer  user_data)
{
	LrgSlider *slider;
	gfloat     handle = lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_HANDLE_SIZE);
	gfloat     mid = 100.0f + handle / 2.0f + 0.5f * (200.0f - handle);

	slider = add_at (fixture, lrg_slider_new (), 100.0f, 100.0f, 200.0f, 30.0f);

	move_to (fixture->canvas, mid, 115.0f);
	press_at (fixture->canvas, mid, 115.0f);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 50.0, 0.0001);

	drag_to (fixture->canvas, 700.0f, 400.0f);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 100.0, 0.0001);
	g_assert_cmpint (find_rect (lrg_canvas_record (fixture->canvas), LRG_PAINT_PRIM_RECT,
	                            300.0f - handle, 115.0f - handle / 2.0f, handle, handle), ==, -1);

	drag_to (fixture->canvas, 0.0f, 400.0f);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 0.0, 0.0001);

	/* After release, moves no longer change the value */
	release_at (fixture->canvas, 0.0f, 400.0f);
	move_to (fixture->canvas, mid, 115.0f);
	g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 0.0, 0.0001);
}


/* ==========================================================================
 * Progress bar
 * ========================================================================== */

/* The fill is a gradient from ACCENT to ACCENT_HOVER over a TRACK pill */
static void
test_progress_bar_gradient (Fixture       *fixture,
                            gconstpointer  user_data)
{
	LrgProgressBar         *bar;
	LrgPaintList           *list;
	const LrgPaintInstance *fill;
	gint                    idx;

	bar = add_at (fixture, lrg_progress_bar_new (), 10.0f, 10.0f, 200.0f, 12.0f);
	lrg_progress_bar_set_value (bar, 50.0);

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 200.0f, 12.0f);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TRACK), 1));
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->radii[0], 6.0f, 0.01);

	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 12.0f);
	fill = instance (list, idx);
	g_assert_cmpfloat_with_epsilon (fill->params[3], (gfloat) LRG_PAINT_GRADIENT_LINEAR, 0.01);
	g_assert_true (color_is (fill->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 1));
	g_assert_true (color_is (fill->color2,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT_HOVER), 1));
}

/* A value change eases in instead of jumping */
static void
test_progress_bar_eases (Fixture       *fixture,
                         gconstpointer  user_data)
{
	LrgProgressBar *bar;
	LrgPaintList   *list;
	gint            idx;

	bar = add_at (fixture, lrg_progress_bar_new (), 10.0f, 10.0f, 200.0f, 12.0f);
	lrg_progress_bar_set_value (bar, 20.0);

	/* Before and at the first frame it shows the value, no animation */
	lrg_canvas_record (fixture->canvas);
	g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_display_fraction (bar), 0.2, 0.0001);
	g_assert_false (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_display_fraction (bar), 0.2, 0.0001);

	/* Right after a jump the drawn fill has barely moved, and the canvas
	 * is busy */
	lrg_progress_bar_set_value (bar, 100.0);
	g_assert_true (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	lrg_canvas_record (fixture->canvas);
	g_assert_cmpfloat (lrg_progress_bar_get_display_fraction (bar), >, 0.2);
	g_assert_cmpfloat (lrg_progress_bar_get_display_fraction (bar), <, 0.95);
	g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_fraction (bar), 1.0, 0.0001);

	/* It comes to rest, then the canvas is idle */
	g_assert_cmpuint (run_until_rest (fixture->canvas, 240), >, 0);
	g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_display_fraction (bar), 1.0, 0.0001);

	/* With reduced motion a change lands on the next frame */
	lrg_progress_bar_set_value (bar, 50.0);
	lrg_motion_set_reduced (TRUE);
	g_assert_false (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_display_fraction (bar), 0.5, 0.0001);
	lrg_progress_bar_set_value (bar, 100.0);
	g_assert_false (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_display_fraction (bar), 1.0, 0.0001);
	idx = find_color (list, LRG_PAINT_PRIM_RECT,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT));
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->rect[2], 210.0f, 0.5);
}

/* Indeterminate: a segment a third of the track long, clipped to it */
static void
test_progress_bar_indeterminate (Fixture       *fixture,
                                 gconstpointer  user_data)
{
	LrgProgressBar         *bar;
	LrgPaintList           *list;
	const LrgPaintInstance *seg;
	gint                    idx;

	bar = add_at (fixture, lrg_progress_bar_new (), 10.0f, 10.0f, 200.0f, 12.0f);
	lrg_progress_bar_set_value (bar, 50.0);
	lrg_progress_bar_set_indeterminate (bar, TRUE);
	g_assert_true (lrg_progress_bar_get_indeterminate (bar));

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 12.0f), ==, -1);

	/* The sweep runs on the canvas clock and keeps the canvas busy */
	for (idx = 0; idx < 18; idx++)
		g_assert_true (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (fixture->canvas);
	idx = find_color (list, LRG_PAINT_PRIM_RECT,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT));
	seg = instance (list, idx);
	g_assert_cmpfloat_with_epsilon (seg->rect[2] - seg->rect[0], 70.0f, 0.5);
	g_assert_cmpfloat (seg->clip[0], >=, 9.5f);
	g_assert_cmpfloat (seg->clip[2], <=, 210.5f);

	/* Back to determinate: the value fill returns */
	lrg_progress_bar_set_indeterminate (bar, FALSE);
	lrg_motion_set_reduced (TRUE);
	g_assert_false (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 12.0f), >=, 0);
}

/* Explicit colours and radius win */
static void
test_progress_bar_explicit (Fixture       *fixture,
                            gconstpointer  user_data)
{
	static const GrlColor bg = { 30, 30, 30, 255 };
	static const GrlColor fg = { 40, 180, 40, 255 };
	LrgProgressBar       *bar;
	LrgPaintList         *list;
	gint                  idx;

	bar = add_at (fixture, lrg_progress_bar_new (), 10.0f, 10.0f, 200.0f, 20.0f);
	lrg_progress_bar_set_value (bar, 100.0);
	lrg_progress_bar_set_background_color (bar, &bg);
	lrg_progress_bar_set_fill_color (bar, &fg);
	lrg_progress_bar_set_corner_radius (bar, 2.0f);

	list = lrg_canvas_record (fixture->canvas);
	idx = find_color (list, LRG_PAINT_PRIM_RECT, &bg);
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->radii[0], 2.0f, 0.01);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT, &fg), >=, 0);
}


/* ==========================================================================
 * Panel
 * ========================================================================== */

/* Theme surface, border, radius and elevation shadow; children on top */
static void
test_panel_theme (Fixture       *fixture,
                  gconstpointer  user_data)
{
	LrgPanel     *panel;
	LrgButton    *child;
	LrgPaintList *list;
	gint          bg;
	gint          sh1;
	gfloat        blur1;

	panel = add_at (fixture, lrg_panel_new (), 50.0f, 50.0f, 300.0f, 200.0f);
	child = lrg_button_new ("Inside");
	lrg_widget_set_position (LRG_WIDGET (child), 10.0f, 10.0f);
	lrg_widget_set_size (LRG_WIDGET (child), 100.0f, 36.0f);
	lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (child));
	g_object_unref (child);

	g_assert_cmpuint (lrg_panel_get_elevation (panel), ==, 1);

	list = lrg_canvas_record (fixture->canvas);
	bg = find_rect (list, LRG_PAINT_PRIM_RECT, 50.0f, 50.0f, 300.0f, 200.0f);
	g_assert_true (color_is (instance (list, bg)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_PANEL), 1));
	g_assert_true (color_is (instance (list, bg)->border,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_BORDER), 1));
	g_assert_cmpfloat_with_epsilon (instance (list, bg)->radii[0],
	                                lrg_theme_get_metric (fixture->theme,
	                                                      LRG_THEME_METRIC_CORNER_RADIUS), 0.01);
	sh1 = find_prim (list, LRG_PAINT_PRIM_SHADOW);
	g_assert_cmpint (sh1, >=, 0);
	blur1 = instance (list, sh1)->params[1];

	/* The child is painted after the panel */
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 60.0f, 60.0f, 100.0f, 36.0f), >, bg);

	/* Higher elevation: softer, bigger shadow; zero: none */
	lrg_panel_set_elevation (panel, 3);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpfloat (instance (list, find_prim (list, LRG_PAINT_PRIM_SHADOW))->params[1], >, blur1);
	lrg_panel_set_elevation (panel, 0);
	list = lrg_canvas_record (fixture->canvas);
	bg = find_rect (list, LRG_PAINT_PRIM_RECT, 50.0f, 50.0f, 300.0f, 200.0f);
	g_assert_cmpint (find_prim (list, LRG_PAINT_PRIM_SHADOW), >, bg);  /* only the child's */

	lrg_panel_set_elevation (panel, 9);
	g_assert_cmpuint (lrg_panel_get_elevation (panel), ==, 3);
}

/* Explicit legacy values win; a NULL border colour removes the border */
static void
test_panel_explicit (Fixture       *fixture,
                     gconstpointer  user_data)
{
	static const GrlColor navy = { 10, 20, 80, 255 };
	LrgPanel             *panel;
	LrgPaintList         *list;
	gint                  bg;

	panel = add_at (fixture, lrg_panel_new (), 0.0f, 0.0f, 100.0f, 100.0f);
	lrg_panel_set_background_color (panel, &navy);
	lrg_panel_set_corner_radius (panel, 0.0f);
	lrg_panel_set_border_color (panel, NULL);
	g_assert_null (lrg_panel_get_border_color (panel));

	list = lrg_canvas_record (fixture->canvas);
	bg = find_rect (list, LRG_PAINT_PRIM_RECT, 0.0f, 0.0f, 100.0f, 100.0f);
	g_assert_true (color_is (instance (list, bg)->color, &navy, 1));
	g_assert_cmpfloat (instance (list, bg)->radii[0], ==, 0.0f);
	g_assert_cmpfloat (instance (list, bg)->params[0], ==, 0.0f);
}


/* ==========================================================================
 * Tab view
 * ========================================================================== */

/* tab_view_with_tabs: a 300x200 view at (0, 0) with three label tabs */
static LrgTabView *
tab_view_with_tabs (Fixture *fixture)
{
	LrgTabView *view = add_at (fixture, lrg_tab_view_new (), 0.0f, 0.0f, 300.0f, 200.0f);

	lrg_tab_view_add_tab (view, "One", LRG_WIDGET (lrg_button_new ("a")));
	lrg_tab_view_add_tab (view, "Two", LRG_WIDGET (lrg_button_new ("b")));
	lrg_tab_view_add_tab (view, "Three", LRG_WIDGET (lrg_button_new ("c")));
	return view;
}

static void
on_tab_changed (LrgTabView *view,
                guint       index,
                gpointer    user_data)
{
	gint *last = user_data;

	*last = (gint) index;
}

/* The indicator heads for the active tab and slides there on springs */
static void
test_tab_view_indicator (Fixture       *fixture,
                         gconstpointer  user_data)
{
	LrgTabView   *view = tab_view_with_tabs (fixture);
	LrgPaintList *list;
	gfloat        x;
	gfloat        y;
	gfloat        w;
	gfloat        h;
	gfloat        x2;
	gint          last = -1;
	gint          idx;

	g_signal_connect (view, "tab-changed", G_CALLBACK (on_tab_changed), &last);

	/* Under the first header, accent coloured */
	g_assert_true (lrg_tab_view_get_indicator_rect (view, &x, &y, &w, &h));
	g_assert_cmpfloat (x, >=, 0.0f);
	g_assert_cmpfloat (x + w, <=, 100.0f);
	g_assert_cmpfloat_with_epsilon (y + h, lrg_tab_view_get_tab_height (view), 0.01);
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, x, y, w, h);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 1));

	/* Click the third header */
	click_at (fixture->canvas, 250.0f, 10.0f);
	g_assert_cmpuint (lrg_tab_view_get_active_tab (view), ==, 2);
	g_assert_cmpint (last, ==, 2);
	lrg_tab_view_get_indicator_rect (view, &x2, NULL, NULL, NULL);
	g_assert_cmpfloat (x2, >=, 200.0f);

	/* Still on its way right after the change ... */
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, x2, y, w, h), ==, -1);

	/* ... the canvas reports it moving, and it arrives */
	g_assert_true (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	g_assert_cmpuint (run_until_rest (fixture->canvas, 240), >, 0);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, x2, y, w, h), >=, 0);

	/* Only the active content is shown */
	g_assert_false (lrg_widget_get_visible (lrg_tab_view_get_tab_content (view, 0)));
	g_assert_true (lrg_widget_get_visible (lrg_tab_view_get_tab_content (view, 2)));
}

/* Left / Right switch tabs while the view itself is focused */
static void
test_tab_view_keyboard (Fixture       *fixture,
                        gconstpointer  user_data)
{
	LrgTabView *view = tab_view_with_tabs (fixture);

	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (view));
	key (fixture->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpuint (lrg_tab_view_get_active_tab (view), ==, 2);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (view));

	key (fixture->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpuint (lrg_tab_view_get_active_tab (view), ==, 1);

	/* With focus inside a tab, arrows belong to that content */
	lrg_canvas_set_focused_widget (fixture->canvas, lrg_tab_view_get_tab_content (view, 1));
	key (fixture->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpuint (lrg_tab_view_get_active_tab (view), ==, 1);
}

/* Hovering a header blends it towards the hover look */
static void
test_tab_view_hover (Fixture       *fixture,
                     gconstpointer  user_data)
{
	LrgTabView   *view = tab_view_with_tabs (fixture);
	LrgPaintList *list;
	gfloat        th = lrg_tab_view_get_tab_height (view);

	(void) view;
	lrg_motion_set_reduced (TRUE);

	/* Resting inactive headers are invisible */
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 102.0f, 2.0f, 96.0f, th - 4.0f), ==, -1);

	move_to (fixture->canvas, 150.0f, 10.0f);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_true (color_is (instance (list, find_rect (list, LRG_PAINT_PRIM_RECT, 102.0f, 2.0f,
	                                                    96.0f, th - 4.0f))->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_CONTROL_HOVER), 1));

	move_to (fixture->canvas, 150.0f, 150.0f);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_rect (list, LRG_PAINT_PRIM_RECT, 102.0f, 2.0f, 96.0f, th - 4.0f), ==, -1);
}


/* ==========================================================================
 * TextInput
 * ========================================================================== */

/* type_text: one frame typing @utf8 into the focused widget */
static void
type_text (LrgCanvas   *canvas,
           const gchar *utf8)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	lrg_canvas_input_add_text (&input, utf8);
	lrg_canvas_process_input (canvas, &input);
}

/* focused_entry: an entry at (100, 100, 300x36), clicked to focus */
static LrgTextInput *
focused_entry (Fixture     *fixture,
               const gchar *text)
{
	LrgTextInput *entry;

	entry = add_at (fixture, lrg_text_input_new (), 100.0f, 100.0f, 300.0f, 36.0f);
	click_at (fixture->canvas, 390.0f, 118.0f);
	g_assert_true (lrg_text_input_get_focused (entry));
	if (text != NULL)
		type_text (fixture->canvas, text);
	return entry;
}

/* The border glides from BORDER to ACCENT when focused */
static void
test_text_input_focus_border (Fixture       *fixture,
                              gconstpointer  user_data)
{
	LrgTextInput *entry;
	LrgPaintList *list;
	gint          idx;

	lrg_motion_set_reduced (TRUE);
	entry = add_at (fixture, lrg_text_input_new (), 100.0f, 100.0f, 300.0f, 36.0f);

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 300.0f, 36.0f);
	g_assert_true (color_is (instance (list, idx)->color,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_INPUT), 1));
	g_assert_true (color_is (instance (list, idx)->border,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_BORDER), 1));

	click_at (fixture->canvas, 150.0f, 118.0f);
	g_assert_true (lrg_text_input_get_focused (entry));
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 300.0f, 36.0f);
	g_assert_true (color_is (instance (list, idx)->border,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT), 1));

	/* The caret is drawn in the accent colour */
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT,
	                             lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT)), >=, 0);
}

/* Placeholder uses TEXT_MUTED, typed text uses TEXT */
static void
test_text_input_placeholder_look (Fixture       *fixture,
                                  gconstpointer  user_data)
{
	LrgTextInput *entry;
	LrgPartLook   look;

	entry = add_at (fixture, lrg_text_input_new_with_placeholder ("Name"),
	                100.0f, 100.0f, 300.0f, 36.0f);
	lrg_widget_resolve_look (LRG_WIDGET (entry), LRG_UI_PART_LABEL, &look);
	g_assert_true (color_is ((const guint8 *) &look.foreground,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TEXT_MUTED), 0));

	lrg_text_input_set_text (entry, "Zach");
	lrg_widget_resolve_look (LRG_WIDGET (entry), LRG_UI_PART_LABEL, &look);
	g_assert_true (color_is ((const guint8 *) &look.foreground,
	                         lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_TEXT), 0));
}

/* Shift+arrows select; the selection is painted with SELECTION */
static void
test_text_input_shift_select (Fixture       *fixture,
                              gconstpointer  user_data)
{
	LrgTextInput *entry;
	LrgPaintList *list;
	gint          s0;
	gint          s1;

	entry = focused_entry (fixture, "hello");
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "hello");
	g_assert_false (lrg_text_input_get_selection (entry, NULL, NULL));

	key (fixture->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_SHIFT);
	key (fixture->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_true (lrg_text_input_get_selection (entry, &s0, &s1));
	g_assert_cmpint (s0, ==, 3);
	g_assert_cmpint (s1, ==, 5);

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpint (find_color (list, LRG_PAINT_PRIM_RECT,
	                             lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_SELECTION)),
	                 >=, 0);

	/* Shift+Home extends to the start; plain Right collapses to the end */
	key (fixture->canvas, GRL_KEY_HOME, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_true (lrg_text_input_get_selection (entry, &s0, &s1));
	g_assert_cmpint (s0, ==, 0);
	g_assert_cmpint (s1, ==, 5);
	key (fixture->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	g_assert_false (lrg_text_input_get_selection (entry, NULL, NULL));
	g_assert_cmpint (lrg_text_input_get_cursor_position (entry), ==, 5);

	/* Focus did not move away on the arrows */
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (entry));
}

/* Typing replaces the selection; Backspace / Delete remove it */
static void
test_text_input_replace_selection (Fixture       *fixture,
                                   gconstpointer  user_data)
{
	LrgTextInput *entry;
	gchar        *sel;

	entry = focused_entry (fixture, "hello world");

	key (fixture->canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	sel = lrg_text_input_get_selected_text (entry);
	g_assert_cmpstr (sel, ==, "hello world");
	g_free (sel);

	type_text (fixture->canvas, "x");
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "x");

	lrg_text_input_set_text (entry, "abcdef");
	lrg_text_input_set_selection (entry, 1, 3);
	key (fixture->canvas, GRL_KEY_BACKSPACE, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "adef");
	g_assert_cmpint (lrg_text_input_get_cursor_position (entry), ==, 1);

	lrg_text_input_set_selection (entry, 4, 2);
	key (fixture->canvas, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "ad");

	/* Ctrl+Backspace removes a word */
	lrg_text_input_set_text (entry, "one two");
	key (fixture->canvas, GRL_KEY_END, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_BACKSPACE, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "one ");
}

/* Ctrl+C / Ctrl+X / Ctrl+V (headless fallback clipboard) */
static void
test_text_input_clipboard (Fixture       *fixture,
                           gconstpointer  user_data)
{
	LrgTextInput *entry;

	entry = focused_entry (fixture, "hello");

	key (fixture->canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	key (fixture->canvas, GRL_KEY_C, LRG_INPUT_MODIFIER_CTRL);
	key (fixture->canvas, GRL_KEY_END, LRG_INPUT_MODIFIER_NONE);
	key (fixture->canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "hellohello");

	key (fixture->canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	key (fixture->canvas, GRL_KEY_X, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "");
	key (fixture->canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "hellohello");

	/* Pasting respects max-length */
	lrg_text_input_set_text (entry, "");
	lrg_text_input_set_max_length (entry, 4);
	key (fixture->canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "hell");
}

/* Space is text, not activation; Enter submits */
static void
test_text_input_keys_consumed (Fixture       *fixture,
                               gconstpointer  user_data)
{
	LrgTextInput *entry;
	gint          submitted = 0;

	entry = focused_entry (fixture, NULL);
	g_signal_connect (entry, "submitted", G_CALLBACK (on_count), &submitted);

	key (fixture->canvas, GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
	type_text (fixture->canvas, " a");
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, " a");
	g_assert_cmpint (submitted, ==, 0);

	key (fixture->canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpint (submitted, ==, 1);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (entry));
}

/* Mouse drag selects (pointer captured); double click selects a word */
static void
test_text_input_mouse_select (Fixture       *fixture,
                              gconstpointer  user_data)
{
	LrgTextInput *entry;
	gfloat        pad = lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_PADDING_X);
	gint          s0;
	gint          s1;

	entry = focused_entry (fixture, "hello world");

	/* Drag from before the first character to past the box */
	move_to (fixture->canvas, 100.0f + pad + 1.0f, 118.0f);
	press_at (fixture->canvas, 100.0f + pad + 1.0f, 118.0f);
	g_assert_cmpint (lrg_text_input_get_cursor_position (entry), ==, 0);
	drag_to (fixture->canvas, 500.0f, 118.0f);
	release_at (fixture->canvas, 500.0f, 118.0f);
	g_assert_true (lrg_text_input_get_selection (entry, &s0, &s1));
	g_assert_cmpint (s0, ==, 0);
	g_assert_cmpint (s1, ==, 11);

	/* Double click inside "world" */
	{
		gfloat w;
		gfloat x;

		lrg_paint_measure_text (lrg_theme_get_ui_font (fixture->theme), "hello wo",
		                        lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_FONT_SIZE),
		                        1.0f, &w, NULL);
		x = 100.0f + pad + w - 2.0f;
		click_at (fixture->canvas, x, 118.0f);
		click_at (fixture->canvas, x, 118.0f);
	}
	g_assert_true (lrg_text_input_get_selection (entry, &s0, &s1));
	g_assert_cmpint (s0, ==, 6);
	g_assert_cmpint (s1, ==, 11);
}

/*
 * The focus cross-fade runs on the canvas clock: lrg_canvas_update()
 * reports it until it settles, and the caret blink alone never keeps
 * the canvas busy. Double clicks are timed on the same clock.
 */
static void
test_text_input_frame_clock (Fixture       *fixture,
                             gconstpointer  user_data)
{
	LrgTextInput *entry;
	gfloat        pad = lrg_theme_get_metric (fixture->theme, LRG_THEME_METRIC_PADDING_X);
	gint          s0;
	gint          s1;

	entry = add_at (fixture, lrg_text_input_new (), 100.0f, 100.0f, 300.0f, 36.0f);
	lrg_text_input_set_text (entry, "hello world");
	run_until_rest (fixture->canvas, 240);

	/* Focusing starts the fade; it settles and the canvas goes idle
	 * although the caret keeps blinking */
	lrg_text_input_set_focused (entry, TRUE);
	g_assert_true (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));
	g_assert_cmpuint (run_until_rest (fixture->canvas, 240), >, 0);
	settle (fixture->canvas, 2.0f);
	g_assert_false (lrg_canvas_update (fixture->canvas, 1.0f / 60.0f));

	/* Two clicks half a second apart (canvas time) are two single
	 * clicks: no word gets selected */
	click_at (fixture->canvas, 100.0f + pad + 2.0f, 118.0f);
	settle (fixture->canvas, 0.5f);
	click_at (fixture->canvas, 100.0f + pad + 2.0f, 118.0f);
	g_assert_false (lrg_text_input_get_selection (entry, &s0, &s1));

	/* ... while two clicks in consecutive frames are a double click */
	click_at (fixture->canvas, 100.0f + pad + 2.0f, 118.0f);
	g_assert_true (lrg_text_input_get_selection (entry, &s0, &s1));
	g_assert_cmpint (s0, ==, 0);
	g_assert_cmpint (s1, ==, 5);
}

/* Long text scrolls so the caret stays inside the clipped box */
static void
test_text_input_scrolls (Fixture       *fixture,
                         gconstpointer  user_data)
{
	LrgTextInput           *entry;
	LrgPaintList           *list;
	const LrgPaintInstance *inst;
	gint                    idx;

	lrg_motion_set_reduced (TRUE);
	entry = add_at (fixture, lrg_text_input_new (), 10.0f, 10.0f, 120.0f, 36.0f);
	click_at (fixture->canvas, 60.0f, 28.0f);
	type_text (fixture->canvas, "the quick brown ");
	type_text (fixture->canvas, "fox jumps over");
	g_assert_cmpint (lrg_text_input_get_cursor_position (entry), ==, 30);

	list = lrg_canvas_record (fixture->canvas);
	idx = find_color (list, LRG_PAINT_PRIM_RECT,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_ACCENT));
	inst = instance (list, idx);
	g_assert_cmpfloat (inst->rect[0], >=, 10.0f);
	g_assert_cmpfloat (inst->rect[2], <=, 130.0f);

	/* The selection starts far left of the box and is clipped to it */
	key (fixture->canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	list = lrg_canvas_record (fixture->canvas);
	idx = find_color (list, LRG_PAINT_PRIM_RECT,
	                  lrg_theme_get_color (fixture->theme, LRG_THEME_COLOR_SELECTION));
	inst = instance (list, idx);
	g_assert_cmpfloat (inst->rect[0], <, 10.0f);
	g_assert_cmpfloat (inst->clip[0], >=, 10.0f);
	g_assert_cmpfloat (inst->clip[2], <=, 130.0f);
}

/* Explicit legacy colours still win; password inputs never copy */
static void
test_text_input_explicit_and_password (Fixture       *fixture,
                                       gconstpointer  user_data)
{
	static const GrlColor bg = { 12, 34, 56, 255 };
	LrgTextInput         *entry;
	LrgPaintList         *list;
	gint                  idx;

	entry = focused_entry (fixture, "secret");
	lrg_text_input_set_background_color (entry, &bg);
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 100.0f, 100.0f, 300.0f, 36.0f);
	g_assert_true (color_is (instance (list, idx)->color, &bg, 1));

	/* Copy "visible" first, then a password copy must not replace it */
	lrg_text_input_set_text (entry, "visible");
	lrg_text_input_select_all (entry);
	lrg_text_input_copy_clipboard (entry);
	lrg_text_input_set_password_mode (entry, TRUE);
	lrg_text_input_set_text (entry, "hunter2");
	lrg_text_input_select_all (entry);
	key (fixture->canvas, GRL_KEY_C, LRG_INPUT_MODIFIER_CTRL);
	lrg_text_input_set_password_mode (entry, FALSE);
	lrg_text_input_set_text (entry, "");
	lrg_text_input_paste_clipboard (entry);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "visible");
}


/* ==========================================================================
 * Image
 * ========================================================================== */

/* Without a texture: nothing painted, default preferred size */
static void
test_image_empty (Fixture       *fixture,
                  gconstpointer  user_data)
{
	LrgImage     *image;
	LrgPaintList *list;
	gfloat        w;
	gfloat        h;

	image = add_at (fixture, lrg_image_new (), 0.0f, 0.0f, 64.0f, 64.0f);
	g_assert_cmpfloat (lrg_image_get_corner_radius (image), ==, 0.0f);
	lrg_image_set_corner_radius (image, 12.0f);
	g_assert_cmpfloat (lrg_image_get_corner_radius (image), ==, 12.0f);

	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 0);
	lrg_widget_measure (LRG_WIDGET (image), &w, &h);
	g_assert_cmpfloat (w, ==, 100.0f);
	g_assert_cmpfloat (h, ==, 100.0f);
}

/* GPU: FIT keeps the aspect ratio, and corner-radius rounds the image */
static void
test_gpu_image (Fixture       *fixture,
                gconstpointer  user_data)
{
	static const GrlColor     red = { 255, 0, 0, 255 };
	static const GrlColor     black = { 0, 0, 0, 255 };
	g_autoptr(GrlImage)       pixels = NULL;
	g_autoptr(GrlTexture)     tex = NULL;
	LrgImage                 *image;
	LrgPaintList             *list;
	gint                      idx;
	Image                     img;

	SKIP_WITHOUT_GL ();

	pixels = grl_image_new_color (32, 16, &red);
	tex = grl_texture_new_from_image (pixels);
	image = add_at (fixture, lrg_image_new_with_texture (tex), 20.0f, 20.0f, 100.0f, 100.0f);
	lrg_image_set_corner_radius (image, 20.0f);

	/* 2:1 texture fitted into a square: 100 x 50, centred */
	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_IMAGE, 20.0f, 45.0f, 100.0f, 50.0f);
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->radii[0], 20.0f, 0.01);

	img = render_canvas (fixture->canvas);
	g_assert_true (pixel_near (&img, 70, 70, &red, 2));
	g_assert_true (pixel_near (&img, 21, 46, &black, 8));  /* rounded corner */
	g_assert_true (pixel_near (&img, 70, 30, &black, 0));  /* letterbox */
	UnloadImage (img);
}


/* ==========================================================================
 * Precedence: widget defaults < theme style rules < explicit setters
 * ========================================================================== */

/* Distinct colours that no preset uses for any role */
static const GrlColor RULE_COLOR     = { 11, 222, 33, 255 };
static const GrlColor EXPLICIT_COLOR = { 201, 17, 123, 255 };

/* same_color: exact channel equality */
static gboolean
same_color (const GrlColor *a,
            const GrlColor *b)
{
	return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

/*
 * add_rule:
 *
 * Adds a theme style rule for @type / @part / @state that sets the
 * background and foreground to RULE_COLOR, the radius to @radius (when
 * >= 0) and the size to @size (when >= 0).
 */
static void
add_rule (LrgTheme       *theme,
          const gchar    *type,
          LrgUIPart       part,
          LrgWidgetState  state,
          gfloat          radius,
          gfloat          size)
{
	g_autoptr(LrgStyleRule) rule = lrg_style_rule_new (type, NULL, part, state);
	LrgPartLook            *look = lrg_style_rule_get_look (rule);

	lrg_part_look_set_background (look, &RULE_COLOR);
	lrg_part_look_set_foreground (look, &RULE_COLOR);
	if (radius >= 0.0f)
		lrg_part_look_set_radius (look, radius);
	if (size >= 0.0f)
		lrg_part_look_set_size (look, size);
	lrg_theme_add_style_rule (theme, rule);
}

/* resolve: the look of @part in exactly @state */
static LrgPartLook
resolve (gpointer        widget,
         LrgUIPart       part,
         LrgWidgetState  state)
{
	LrgPartLook look;

	lrg_widget_resolve_look_for_state (LRG_WIDGET (widget), part, state, &look);
	return look;
}

/* Button: rule beats the default; explicit colours / radius beat the
 * rule, at rest and hovered (hover derived from the normal colour) */
static void
test_precedence_button (Fixture       *fixture,
                        gconstpointer  user_data)
{
	LrgButton    *button;
	LrgPaintList *list;
	LrgPartLook   look;
	GrlColor      shade;
	gint          idx;

	button = add_at (fixture, lrg_button_new ("Go"), 10.0f, 10.0f, 100.0f, 36.0f);
	add_rule (fixture->theme, "LrgButton", LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE, 9.0f, -1.0f);
	add_rule (fixture->theme, "LrgButton", LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);

	/* (a) the rule beats the theme default, at rest and hovered */
	look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 9.0f, 0.01);
	look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	look = resolve (button, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &RULE_COLOR));

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 36.0f);
	g_assert_true (color_is (instance (list, idx)->color, &RULE_COLOR, 1));

	/* (b) explicit values beat the rule */
	lrg_button_set_normal_color (button, &EXPLICIT_COLOR);
	lrg_button_set_text_color (button, &EXPLICIT_COLOR);
	lrg_button_set_corner_radius (button, 2.0f);

	look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 2.0f, 0.01);
	g_assert_cmpfloat_with_epsilon (look.border_width, 0.0f, 0.01);

	/* Hover / press still derive from the explicit normal colour */
	look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	lrg_paint_color_shade (&EXPLICIT_COLOR, 0.12f, &shade);
	g_assert_true (same_color (&look.background, &shade));
	look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_PRESSED);
	lrg_paint_color_shade (&EXPLICIT_COLOR, -0.15f, &shade);
	g_assert_true (same_color (&look.background, &shade));

	look = resolve (button, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.foreground, &EXPLICIT_COLOR));

	/* An explicit hover colour beats the derived one */
	lrg_button_set_hover_color (button, &RULE_COLOR);
	look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.background, &RULE_COLOR));

	list = lrg_canvas_record (fixture->canvas);
	idx = find_rect (list, LRG_PAINT_PRIM_RECT, 10.0f, 10.0f, 100.0f, 36.0f);
	g_assert_true (color_is (instance (list, idx)->color, &EXPLICIT_COLOR, 1));
	g_assert_cmpfloat_with_epsilon (instance (list, idx)->radii[0], 2.0f, 0.01);

	/* (c) per-instance part looks still beat the explicit values */
	{
		LrgPartLook over;

		lrg_part_look_init (&over);
		lrg_part_look_set_background (&over, &shade);
		lrg_widget_set_part_look (LRG_WIDGET (button), LRG_UI_PART_BOX,
		                          LRG_WIDGET_STATE_NONE, &over);
		look = resolve (button, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
		g_assert_true (same_color (&look.background, &shade));
	}
}

/* Label: rule beats TEXT; explicit colour beats the rule (not disabled) */
static void
test_precedence_label (Fixture       *fixture,
                       gconstpointer  user_data)
{
	LrgLabel    *label;
	LrgPartLook  look;

	label = add_at (fixture, lrg_label_new ("Hi"), 10.0f, 10.0f, 100.0f, 20.0f);
	add_rule (fixture->theme, "LrgLabel", LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);

	look = resolve (label, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &RULE_COLOR));

	lrg_label_set_color (label, &EXPLICIT_COLOR);
	look = resolve (label, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &EXPLICIT_COLOR));
	look = resolve (label, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_DISABLED);
	g_assert_false (same_color (&look.foreground, &EXPLICIT_COLOR));
}

/* Checkbox: rules beat the BOX / LABEL defaults; explicit box, check
 * and text colours beat them, at rest and hovered */
static void
test_precedence_checkbox (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgCheckbox *box;
	LrgPartLook  look;
	GrlColor     shade;

	box = add_at (fixture, lrg_checkbox_new ("Sound"), 10.0f, 10.0f, 160.0f, 24.0f);
	add_rule (fixture->theme, "LrgCheckbox", LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);
	add_rule (fixture->theme, "LrgCheckbox", LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);

	look = resolve (box, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	look = resolve (box, LRG_UI_PART_BOX, LRG_WIDGET_STATE_CHECKED | LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	look = resolve (box, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &RULE_COLOR));

	lrg_checkbox_set_box_color (box, &EXPLICIT_COLOR);
	lrg_checkbox_set_check_color (box, &EXPLICIT_COLOR);
	lrg_checkbox_set_text_color (box, &EXPLICIT_COLOR);

	look = resolve (box, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	look = resolve (box, LRG_UI_PART_BOX, LRG_WIDGET_STATE_CHECKED);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_true (same_color (&look.border, &EXPLICIT_COLOR));
	look = resolve (box, LRG_UI_PART_BOX, LRG_WIDGET_STATE_CHECKED | LRG_WIDGET_STATE_HOVERED);
	lrg_paint_color_shade (&EXPLICIT_COLOR, 0.1f, &shade);
	g_assert_true (same_color (&look.background, &shade));
	look = resolve (box, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.foreground, &EXPLICIT_COLOR));
}

/* Slider: rules beat the TRACK / FILL / THUMB defaults; explicit
 * colours and sizes beat them */
static void
test_precedence_slider (Fixture       *fixture,
                        gconstpointer  user_data)
{
	LrgSlider   *slider;
	LrgPartLook  look;
	GrlColor     shade;

	slider = add_at (fixture, lrg_slider_new (), 10.0f, 10.0f, 200.0f, 24.0f);
	add_rule (fixture->theme, "LrgSlider", LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE, -1.0f, 13.0f);
	add_rule (fixture->theme, "LrgSlider", LRG_UI_PART_FILL, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);
	add_rule (fixture->theme, "LrgSlider", LRG_UI_PART_THUMB, LRG_WIDGET_STATE_NONE, -1.0f, 31.0f);

	look = resolve (slider, LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.size, 13.0f, 0.01);
	look = resolve (slider, LRG_UI_PART_FILL, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	look = resolve (slider, LRG_UI_PART_THUMB, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.size, 31.0f, 0.01);

	lrg_slider_set_track_color (slider, &EXPLICIT_COLOR);
	lrg_slider_set_fill_color (slider, &EXPLICIT_COLOR);
	lrg_slider_set_handle_color (slider, &EXPLICIT_COLOR);
	lrg_slider_set_handle_size (slider, 20.0f);
	lrg_slider_set_track_thickness (slider, 5.0f);

	look = resolve (slider, LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_cmpfloat_with_epsilon (look.size, 5.0f, 0.01);
	look = resolve (slider, LRG_UI_PART_FILL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	look = resolve (slider, LRG_UI_PART_FILL, LRG_WIDGET_STATE_HOVERED);
	lrg_paint_color_shade (&EXPLICIT_COLOR, 0.10f, &shade);
	g_assert_true (same_color (&look.background, &shade));
	look = resolve (slider, LRG_UI_PART_THUMB, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_cmpfloat_with_epsilon (look.size, 20.0f, 0.01);
	look = resolve (slider, LRG_UI_PART_THUMB, LRG_WIDGET_STATE_HOVERED);
	g_assert_cmpfloat (look.size, >, 20.0f);
}

/* Progress bar: rules beat the TRACK / FILL / LABEL defaults; explicit
 * colours and radius beat them */
static void
test_precedence_progress_bar (Fixture       *fixture,
                              gconstpointer  user_data)
{
	LrgProgressBar *bar;
	LrgPartLook     look;

	bar = add_at (fixture, lrg_progress_bar_new (), 10.0f, 10.0f, 200.0f, 16.0f);
	add_rule (fixture->theme, "LrgProgressBar", LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE, 1.0f, -1.0f);
	add_rule (fixture->theme, "LrgProgressBar", LRG_UI_PART_FILL, LRG_WIDGET_STATE_NONE, 1.0f, -1.0f);
	add_rule (fixture->theme, "LrgProgressBar", LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);

	look = resolve (bar, LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 1.0f, 0.01);
	look = resolve (bar, LRG_UI_PART_FILL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	look = resolve (bar, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &RULE_COLOR));

	lrg_progress_bar_set_background_color (bar, &EXPLICIT_COLOR);
	lrg_progress_bar_set_fill_color (bar, &EXPLICIT_COLOR);
	lrg_progress_bar_set_text_color (bar, &EXPLICIT_COLOR);
	lrg_progress_bar_set_corner_radius (bar, 3.0f);

	look = resolve (bar, LRG_UI_PART_TRACK, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 3.0f, 0.01);
	look = resolve (bar, LRG_UI_PART_FILL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_true (same_color (&look.gradient.start, &EXPLICIT_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 3.0f, 0.01);
	look = resolve (bar, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &EXPLICIT_COLOR));
}

/* Panel: a rule beats the BOX default; explicit background, border and
 * radius beat it (and an explicit "no border" removes the rule's) */
static void
test_precedence_panel (Fixture       *fixture,
                       gconstpointer  user_data)
{
	LrgPanel                *panel;
	LrgPartLook              look;
	static const GrlColor    blue = { 10, 20, 230, 255 };
	g_autoptr(LrgStyleRule)  rule = NULL;

	panel = add_at (fixture, lrg_panel_new (), 10.0f, 10.0f, 200.0f, 100.0f);
	rule = lrg_style_rule_new ("LrgPanel", NULL, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	lrg_part_look_set_background (lrg_style_rule_get_look (rule), &RULE_COLOR);
	lrg_part_look_set_border (lrg_style_rule_get_look (rule), &RULE_COLOR, 4.0f);
	lrg_part_look_set_radius (lrg_style_rule_get_look (rule), 12.0f);
	lrg_theme_add_style_rule (fixture->theme, rule);

	look = resolve (panel, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	g_assert_true (same_color (&look.border, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.border_width, 4.0f, 0.01);
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 12.0f, 0.01);

	/* An explicit width keeps the rule's colour */
	lrg_panel_set_border_width (panel, 2.0f);
	look = resolve (panel, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.border, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.border_width, 2.0f, 0.01);

	lrg_panel_set_background_color (panel, &EXPLICIT_COLOR);
	lrg_panel_set_border_color (panel, &blue);
	lrg_panel_set_corner_radius (panel, 5.0f);
	look = resolve (panel, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_true (same_color (&look.border, &blue));
	g_assert_cmpfloat_with_epsilon (look.border_width, 2.0f, 0.01);
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 5.0f, 0.01);

	/* Explicitly no border beats the rule's border */
	lrg_panel_set_border_color (panel, NULL);
	look = resolve (panel, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	g_assert_cmpfloat_with_epsilon (look.border_width, 0.0f, 0.01);
}

/* Text input: rules beat the defaults; explicit colours and radius beat
 * them, but focus still shows the accent border */
static void
test_precedence_text_input (Fixture       *fixture,
                            gconstpointer  user_data)
{
	LrgTextInput            *input;
	LrgPartLook              look;
	g_autoptr(LrgStyleRule)  rule = NULL;

	input = add_at (fixture, lrg_text_input_new_with_placeholder ("Name"),
	                10.0f, 10.0f, 200.0f, 32.0f);
	rule = lrg_style_rule_new ("LrgTextInput", NULL, LRG_UI_PART_BOX, LRG_WIDGET_STATE_NONE);
	lrg_part_look_set_background (lrg_style_rule_get_look (rule), &RULE_COLOR);
	lrg_part_look_set_border (lrg_style_rule_get_look (rule), &RULE_COLOR, 1.0f);
	lrg_part_look_set_radius (lrg_style_rule_get_look (rule), 11.0f);
	lrg_theme_add_style_rule (fixture->theme, rule);
	add_rule (fixture->theme, "LrgTextInput", LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, -1.0f, -1.0f);

	look = resolve (input, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.background, &RULE_COLOR));
	g_assert_true (same_color (&look.border, &RULE_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 11.0f, 0.01);
	look = resolve (input, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &RULE_COLOR));

	lrg_text_input_set_background_color (input, &EXPLICIT_COLOR);
	lrg_text_input_set_border_color (input, &EXPLICIT_COLOR);
	lrg_text_input_set_placeholder_color (input, &EXPLICIT_COLOR);
	lrg_text_input_set_corner_radius (input, 4.0f);

	look = resolve (input, LRG_UI_PART_BOX, LRG_WIDGET_STATE_HOVERED);
	g_assert_true (same_color (&look.background, &EXPLICIT_COLOR));
	g_assert_true (same_color (&look.border, &EXPLICIT_COLOR));
	g_assert_cmpfloat_with_epsilon (look.radii.top_left, 4.0f, 0.01);

	/* Placeholder colour while empty, text colour once typed into */
	look = resolve (input, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &EXPLICIT_COLOR));
	lrg_text_input_set_text (input, "x");
	look = resolve (input, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &RULE_COLOR));
	lrg_text_input_set_text_color (input, &EXPLICIT_COLOR);
	look = resolve (input, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE);
	g_assert_true (same_color (&look.foreground, &EXPLICIT_COLOR));

	/* Focused: the explicit border colour gives way to the rule /
	 * accent, it only applies while unfocused */
	look = resolve (input, LRG_UI_PART_BOX, LRG_WIDGET_STATE_FOCUSED);
	g_assert_false (same_color (&look.border, &EXPLICIT_COLOR));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

#define ADD(path, func) \
	g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	ADD ("/ui/widgets/button/primary-rest", test_button_primary_rest);
	ADD ("/ui/widgets/button/hover-blends", test_button_hover_blends);
	ADD ("/ui/widgets/button/explicit-color-wins", test_button_explicit_color_wins);
	ADD ("/ui/widgets/button/flat", test_button_flat);
	ADD ("/ui/widgets/button/press-squash", test_button_press_squash);
	ADD ("/ui/widgets/button/activate", test_button_activate);
	ADD ("/ui/widgets/button/measure", test_button_measure);
	ADD ("/ui/widgets/button/disabled", test_button_disabled);
	ADD ("/ui/widgets/label/colors", test_label_colors);
	ADD ("/ui/widgets/label/class-sizes", test_label_class_sizes);
	ADD ("/ui/widgets/label/multiline", test_label_multiline);
	ADD ("/ui/widgets/checkbox/toggle", test_checkbox_toggle);
	ADD ("/ui/widgets/checkbox/check-mark", test_checkbox_check_mark);
	ADD ("/ui/widgets/checkbox/explicit-colors", test_checkbox_explicit_colors);
	ADD ("/ui/widgets/checkbox/switch", test_checkbox_switch);
	ADD ("/ui/widgets/checkbox/radio", test_checkbox_radio);
	ADD ("/ui/widgets/slider/parts", test_slider_parts);
	ADD ("/ui/widgets/slider/explicit-colors", test_slider_explicit_colors);
	ADD ("/ui/widgets/slider/keyboard", test_slider_keyboard);
	ADD ("/ui/widgets/slider/vertical", test_slider_vertical);
	ADD ("/ui/widgets/slider/drag", test_slider_drag);
	ADD ("/ui/widgets/progress-bar/gradient", test_progress_bar_gradient);
	ADD ("/ui/widgets/progress-bar/eases", test_progress_bar_eases);
	ADD ("/ui/widgets/progress-bar/indeterminate", test_progress_bar_indeterminate);
	ADD ("/ui/widgets/progress-bar/explicit", test_progress_bar_explicit);
	ADD ("/ui/widgets/panel/theme", test_panel_theme);
	ADD ("/ui/widgets/panel/explicit", test_panel_explicit);
	ADD ("/ui/widgets/tab-view/indicator", test_tab_view_indicator);
	ADD ("/ui/widgets/tab-view/keyboard", test_tab_view_keyboard);
	ADD ("/ui/widgets/tab-view/hover", test_tab_view_hover);
	ADD ("/ui/widgets/text-input/focus-border", test_text_input_focus_border);
	ADD ("/ui/widgets/text-input/placeholder-look", test_text_input_placeholder_look);
	ADD ("/ui/widgets/text-input/shift-select", test_text_input_shift_select);
	ADD ("/ui/widgets/text-input/replace-selection", test_text_input_replace_selection);
	ADD ("/ui/widgets/text-input/clipboard", test_text_input_clipboard);
	ADD ("/ui/widgets/text-input/keys-consumed", test_text_input_keys_consumed);
	ADD ("/ui/widgets/text-input/mouse-select", test_text_input_mouse_select);
	ADD ("/ui/widgets/text-input/scrolls", test_text_input_scrolls);
	ADD ("/ui/widgets/text-input/frame-clock", test_text_input_frame_clock);
	ADD ("/ui/widgets/text-input/explicit-and-password", test_text_input_explicit_and_password);
	ADD ("/ui/widgets/image/empty", test_image_empty);
	ADD ("/ui/widgets/precedence/button", test_precedence_button);
	ADD ("/ui/widgets/precedence/label", test_precedence_label);
	ADD ("/ui/widgets/precedence/checkbox", test_precedence_checkbox);
	ADD ("/ui/widgets/precedence/slider", test_precedence_slider);
	ADD ("/ui/widgets/precedence/progress-bar", test_precedence_progress_bar);
	ADD ("/ui/widgets/precedence/panel", test_precedence_panel);
	ADD ("/ui/widgets/precedence/text-input", test_precedence_text_input);

	if (!g_test_subprocess ())
		graphics_available = init_graphics_context ();

	ADD ("/ui/widgets/gpu/button", test_gpu_button);
	ADD ("/ui/widgets/gpu/switch", test_gpu_switch);
	ADD ("/ui/widgets/gpu/image", test_gpu_image);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
