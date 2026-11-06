/* test-ui-pickers.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Colour picker, rating, date/time pickers, breadcrumb, markdown label.
 *
 * Everything except the /gpu/ cases is headless: widgets live on an
 * LrgCanvas, input arrives as LrgCanvasInput frames through
 * lrg_canvas_process_input() (exactly like the real input loop) and
 * time advances with fixed 1/60 s frames. The GPU case renders the
 * colour picker's saturation / value square into a texture and reads
 * pixels back; it needs a display (run it under xrun.sh).
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <raylib.h>
#include <libregnum.h>

#define FRAME (1.0f / 60.0f)

/* ==========================================================================
 * Fixture and input helpers
 * ========================================================================== */

typedef struct
{
	LrgCanvas *canvas;
} Fixture;

static void
fixture_set_up (Fixture       *fixture,
                gconstpointer  user_data)
{
	fixture->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (fixture->canvas), 800.0f, 600.0f);
}

static void
fixture_tear_down (Fixture       *fixture,
                   gconstpointer  user_data)
{
	g_clear_object (&fixture->canvas);
}

/* add: put @widget on the canvas at (x, y), sized when w/h > 0, laid out */
static gpointer
add (Fixture  *fixture,
     gpointer  widget,
     gfloat    x,
     gfloat    y,
     gfloat    w,
     gfloat    h)
{
	/* The canvas positions freely: unsized widgets get their preferred size */
	lrg_widget_set_position (LRG_WIDGET (widget), x, y);
	if (w <= 0.0f || h <= 0.0f)
		lrg_widget_get_preferred_size (LRG_WIDGET (widget), &w, &h);
	lrg_widget_set_size (LRG_WIDGET (widget), w, h);
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (widget));
	g_object_unref (widget);
	lrg_canvas_record (fixture->canvas);
	return widget;
}

static void
frame_init (LrgCanvasInput *input,
            gfloat          x,
            gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = FRAME;
}

/* move_to: one frame with the pointer at (x, y) */
static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_init (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

/* press_at / release_at: left button down / up at (x, y) */
static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_init (&input, x, y);
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

	frame_init (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame_init (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

/* click_at: move, press, release */
static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	release_at (canvas, x, y);
}

/* click_rect: click the centre of @r */
static void
click_rect (LrgCanvas          *canvas,
            const GrlRectangle *r)
{
	click_at (canvas, r->x + r->width * 0.5f, r->y + r->height * 0.5f);
}

/* key: one frame with @key pressed (pointer parked off-canvas) */
static void
key_mod (LrgCanvas         *canvas,
         GrlKey             key,
         LrgInputModifiers  modifiers)
{
	LrgCanvasInput input;

	frame_init (&input, -50.0f, -50.0f);
	input.modifiers = modifiers;
	lrg_canvas_input_add_key (&input, key);
	lrg_canvas_process_input (canvas, &input);
}

static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	key_mod (canvas, k, LRG_INPUT_MODIFIER_NONE);
}

/* type_text: one frame of typed characters */
static void
type_text (LrgCanvas   *canvas,
           const gchar *text)
{
	LrgCanvasInput input;

	frame_init (&input, -50.0f, -50.0f);
	lrg_canvas_input_add_text (&input, text);
	lrg_canvas_process_input (canvas, &input);
}

/* wheel_at: one frame of wheel at (x, y) */
static void
wheel_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y,
          gfloat     dy)
{
	LrgCanvasInput input;

	frame_init (&input, x, y);
	input.wheel_y = dy;
	lrg_canvas_process_input (canvas, &input);
}

/* settle: run frames until nothing animates (bounded) */
static void
settle (LrgCanvas *canvas)
{
	guint i;

	for (i = 0; i < 240; i++)
		if (!lrg_canvas_update (canvas, FRAME))
			break;
}

static void
count_signal (gpointer instance,
              gpointer user_data)
{
	(*(gint *) user_data)++;
}

/* ==========================================================================
 * LrgColorPicker
 * ========================================================================== */

static void
test_color_hsv_roundtrip (void)
{
	GrlColor c;
	GrlColor back;
	gdouble  h;
	gdouble  s;
	gdouble  v;
	gint     r;
	gint     g;
	gint     b;

	/* Known values */
	c = (GrlColor) { 255, 0, 0, 255 };
	lrg_color_picker_rgb_to_hsv (&c, &h, &s, &v);
	g_assert_cmpfloat_with_epsilon (h, 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (s, 1.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (v, 1.0, 1e-9);

	c = (GrlColor) { 0, 0, 255, 255 };
	lrg_color_picker_rgb_to_hsv (&c, &h, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (h, 240.0, 1e-9);

	c = (GrlColor) { 128, 128, 128, 255 };
	lrg_color_picker_rgb_to_hsv (&c, &h, &s, &v);
	g_assert_cmpfloat_with_epsilon (s, 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (v, 128.0 / 255.0, 1e-9);

	lrg_color_picker_hsv_to_rgb (120.0, 1.0, 1.0, 0.5, &c);
	g_assert_cmpuint (c.r, ==, 0);
	g_assert_cmpuint (c.g, ==, 255);
	g_assert_cmpuint (c.b, ==, 0);
	g_assert_cmpuint (c.a, ==, 128);

	/* Hue wraps */
	lrg_color_picker_hsv_to_rgb (-120.0, 1.0, 1.0, 1.0, &c);
	g_assert_cmpuint (c.b, ==, 255);

	/* Round trip over a lattice of RGB colours */
	for (r = 0; r <= 255; r += 15)
		for (g = 0; g <= 255; g += 17)
			for (b = 0; b <= 255; b += 51)
			{
				c = (GrlColor) { (guint8) r, (guint8) g, (guint8) b, 200 };
				lrg_color_picker_rgb_to_hsv (&c, &h, &s, &v);
				lrg_color_picker_hsv_to_rgb (h, s, v, 200.0 / 255.0, &back);
				g_assert_cmpuint (back.r, ==, c.r);
				g_assert_cmpuint (back.g, ==, c.g);
				g_assert_cmpuint (back.b, ==, c.b);
				g_assert_cmpuint (back.a, ==, c.a);
			}
}

static void
test_color_sv_click_and_drag (Fixture       *fixture,
                              gconstpointer  user_data)
{
	LrgColorPicker *picker = add (fixture, lrg_color_picker_new (), 20.0f, 20.0f, 0.0f, 0.0f);
	GrlRectangle    sv;
	gdouble         h;
	gdouble         s;
	gdouble         v;
	gint            changes = 0;

	g_signal_connect (picker, "color-changed", G_CALLBACK (count_signal), &changes);
	lrg_color_picker_set_hsv (picker, 120.0, 1.0, 1.0);
	g_assert_cmpint (changes, ==, 1);
	lrg_canvas_record (fixture->canvas);
	g_assert_true (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_SV, &sv));
	g_assert_cmpfloat (sv.width, >, 50.0f);

	/* Click: saturation from x, value from y; the hue stays */
	click_at (fixture->canvas, sv.x + sv.width * 0.75f, sv.y + sv.height * 0.25f);
	lrg_color_picker_get_hsv (picker, &h, &s, &v);
	g_assert_cmpfloat_with_epsilon (h, 120.0, 1e-6);
	g_assert_cmpfloat_with_epsilon (s, 0.75, 0.02);
	g_assert_cmpfloat_with_epsilon (v, 0.75, 0.02);
	g_assert_cmpint (changes, >=, 2);

	/* Drag past the corner clamps; release records a recent colour */
	press_at (fixture->canvas, sv.x + 5.0f, sv.y + 5.0f);
	drag_to (fixture->canvas, sv.x + sv.width * 0.5f, sv.y + sv.height * 0.5f);
	drag_to (fixture->canvas, sv.x + sv.width + 80.0f, sv.y + sv.height + 80.0f);
	lrg_color_picker_get_hsv (picker, NULL, &s, &v);
	g_assert_cmpfloat_with_epsilon (s, 1.0, 1e-6);
	g_assert_cmpfloat_with_epsilon (v, 0.0, 1e-6);
	release_at (fixture->canvas, sv.x + sv.width + 80.0f, sv.y + sv.height + 80.0f);
	{
		guint           n;
		const GrlColor *recent = lrg_color_picker_get_recent (picker, &n);

		g_assert_cmpuint (n, >=, 1);
		g_assert_cmpuint (recent[0].r, ==, 0);
		g_assert_cmpuint (recent[0].g, ==, 0);
	}

	/* Black keeps the hue, so the square does not jump to red */
	lrg_color_picker_get_hsv (picker, &h, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (h, 120.0, 1e-6);
}

static void
test_color_hue_and_alpha (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgColorPicker *picker = add (fixture, lrg_color_picker_new (), 20.0f, 20.0f, 0.0f, 0.0f);
	GrlRectangle    hue;
	GrlRectangle    alpha;
	GrlColor        c;
	gdouble         h;

	lrg_color_picker_set_hsv (picker, 0.0, 1.0, 1.0);
	g_assert_true (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_HUE, &hue));
	g_assert_true (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_ALPHA, &alpha));

	/* Hue strip: the ends are inset by half the strip height */
	press_at (fixture->canvas, hue.x + hue.height * 0.5f, hue.y + hue.height * 0.5f);
	drag_to (fixture->canvas, hue.x + hue.height * 0.5f + (hue.width - hue.height) * 0.5f, hue.y + 2.0f);
	release_at (fixture->canvas, hue.x + hue.height * 0.5f + (hue.width - hue.height) * 0.5f, hue.y + 2.0f);
	lrg_color_picker_get_hsv (picker, &h, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (h, 180.0, 1.0);
	lrg_color_picker_get_color (picker, &c);
	g_assert_cmpuint (c.r, <=, 5);
	g_assert_cmpuint (c.g, >=, 250);
	g_assert_cmpuint (c.b, >=, 250);

	/* Alpha strip: left end transparent, middle half */
	click_at (fixture->canvas, alpha.x + alpha.height * 0.5f, alpha.y + alpha.height * 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_color_picker_get_alpha (picker), 0.0, 1e-6);
	click_at (fixture->canvas, alpha.x + alpha.width * 0.5f, alpha.y + alpha.height * 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_color_picker_get_alpha (picker), 0.5, 0.02);
	lrg_color_picker_get_color (picker, &c);
	g_assert_cmpuint (c.a, >=, 122);
	g_assert_cmpuint (c.a, <=, 133);

	/* Hiding alpha removes the strip and shortens the hex */
	lrg_color_picker_set_show_alpha (picker, FALSE);
	lrg_canvas_record (fixture->canvas);
	g_assert_false (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_ALPHA, &alpha));
	{
		g_autofree gchar *hex = lrg_color_picker_get_hex (picker);

		g_assert_cmpuint (strlen (hex), ==, 7);
	}
}

static void
test_color_hex_entry (Fixture       *fixture,
                      gconstpointer  user_data)
{
	LrgColorPicker   *picker = add (fixture, lrg_color_picker_new (), 20.0f, 20.0f, 0.0f, 0.0f);
	LrgTextInput     *entry;
	GrlRectangle      hex;
	GrlColor          c;
	g_autofree gchar *text = NULL;

	/* API */
	g_assert_true (lrg_color_picker_set_hex (picker, "#3366CC"));
	lrg_color_picker_get_color (picker, &c);
	g_assert_cmpuint (c.r, ==, 0x33);
	g_assert_cmpuint (c.g, ==, 0x66);
	g_assert_cmpuint (c.b, ==, 0xcc);
	g_assert_cmpuint (c.a, ==, 0xff);
	g_assert_false (lrg_color_picker_set_hex (picker, "#nothex"));
	text = lrg_color_picker_get_hex (picker);
	g_assert_cmpstr (text, ==, "#3366CCFF");

	/* Typed into the field: click it, replace the text, Enter */
	g_assert_true (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_HEX, &hex));
	click_rect (fixture->canvas, &hex);
	entry = LRG_TEXT_INPUT (lrg_canvas_get_focused_widget (fixture->canvas));
	g_assert_true (LRG_IS_TEXT_INPUT (entry));
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "#3366CCFF");
	lrg_text_input_select_all (entry);
	type_text (fixture->canvas, "#11223380");
	lrg_color_picker_get_color (picker, &c);
	g_assert_cmpuint (c.r, ==, 0x11);
	g_assert_cmpuint (c.g, ==, 0x22);
	g_assert_cmpuint (c.b, ==, 0x33);
	g_assert_cmpuint (c.a, ==, 0x80);
	key (fixture->canvas, GRL_KEY_ENTER);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "#11223380");

	/* Picker changes rewrite the field */
	lrg_color_picker_set_hsv (picker, 0.0, 1.0, 1.0);
	g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "#FF000080");
}

static void
test_color_keyboard_and_swatches (Fixture       *fixture,
                                  gconstpointer  user_data)
{
	LrgColorPicker *picker = add (fixture, lrg_color_picker_new (), 20.0f, 20.0f, 0.0f, 0.0f);
	LrgWidget      *sv = lrg_container_get_child (LRG_CONTAINER (picker), 0);
	LrgWidget      *hue = lrg_container_get_child (LRG_CONTAINER (picker), 1);
	GrlRectangle    sw;
	GrlColor        c;
	guint           n;
	const GrlColor *palette;
	const GrlColor *recent;
	gdouble         h;
	gdouble         s;
	gdouble         v;

	lrg_color_picker_set_hsv (picker, 200.0, 0.5, 0.5);

	/* Square: Right raises saturation, Up raises value, Shift = x10 */
	lrg_canvas_set_focused_widget (fixture->canvas, sv);
	key (fixture->canvas, GRL_KEY_RIGHT);
	key (fixture->canvas, GRL_KEY_UP);
	key_mod (fixture->canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_SHIFT);
	lrg_color_picker_get_hsv (picker, &h, &s, &v);
	g_assert_cmpfloat_with_epsilon (s, 0.51, 1e-6);
	g_assert_cmpfloat_with_epsilon (v, 0.61, 1e-6);
	/* The arrows were consumed: focus stayed on the square */
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == sv);

	/* Hue strip: degrees */
	lrg_canvas_set_focused_widget (fixture->canvas, hue);
	key (fixture->canvas, GRL_KEY_LEFT);
	lrg_color_picker_get_hsv (picker, &h, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (h, 199.0, 1e-6);
	key (fixture->canvas, GRL_KEY_END);
	lrg_color_picker_get_hsv (picker, &h, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (h, 360.0, 1e-6);

	/* Swatches: the first palette colour; it lands in recent */
	palette = lrg_color_picker_get_palette (picker, &n);
	g_assert_cmpuint (n, ==, 12);
	g_assert_true (lrg_color_picker_get_swatch_bounds (picker, 0, &sw));
	click_rect (fixture->canvas, &sw);
	lrg_color_picker_get_color (picker, &c);
	g_assert_cmpuint (c.r, ==, palette[0].r);
	g_assert_cmpuint (c.g, ==, palette[0].g);
	g_assert_cmpuint (c.b, ==, palette[0].b);
	recent = lrg_color_picker_get_recent (picker, &n);
	g_assert_cmpuint (n, ==, 1);
	g_assert_cmpuint (recent[0].r, ==, palette[0].r);

	/* Recent: deduplicated, newest first, capped */
	{
		guint i;

		for (i = 0; i < 20; i++)
		{
			GrlColor k = { (guint8) i, 0, 0, 255 };

			lrg_color_picker_add_recent (picker, &k);
		}
		lrg_color_picker_add_recent (picker, &(GrlColor) { 15, 0, 0, 255 });
		recent = lrg_color_picker_get_recent (picker, &n);
		g_assert_cmpuint (n, ==, LRG_COLOR_PICKER_MAX_RECENT);
		g_assert_cmpuint (recent[0].r, ==, 15);
		g_assert_cmpuint (recent[1].r, ==, 19);
	}
	lrg_color_picker_clear_recent (picker);
	lrg_color_picker_get_recent (picker, &n);
	g_assert_cmpuint (n, ==, 0);
}

static void
test_color_popup_mode (Fixture       *fixture,
                       gconstpointer  user_data)
{
	LrgColorPicker *button = lrg_color_picker_new ();
	LrgColorPicker *inner;
	gfloat          w;
	gfloat          h;
	GrlColor        c;
	guint           n;

	lrg_color_picker_set_popup_mode (button, TRUE);
	lrg_widget_measure (LRG_WIDGET (button), &w, &h);
	g_assert_cmpfloat (h, <, 60.0f);
	add (fixture, button, 40.0f, 40.0f, 0.0f, 0.0f);
	lrg_widget_get_preferred_size (LRG_WIDGET (button), &w, &h);

	/* Click opens the full picker in a popup, focused on the square */
	click_at (fixture->canvas, 40.0f + w * 0.5f, 40.0f + h * 0.5f);
	g_assert_true (lrg_color_picker_get_popup_open (button));
	inner = lrg_color_picker_get_popup_picker (button);
	g_assert_nonnull (inner);
	g_assert_false (lrg_color_picker_get_popup_mode (inner));
	g_assert_true (lrg_canvas_get_top_popup (fixture->canvas) == LRG_WIDGET (inner));
	g_assert_true (lrg_widget_is_ancestor (lrg_canvas_get_focused_widget (fixture->canvas), LRG_WIDGET (inner)));

	/* Changes in the popup reach the button */
	lrg_color_picker_set_hsv (inner, 240.0, 1.0, 1.0);
	lrg_color_picker_get_color (button, &c);
	g_assert_cmpuint (c.b, ==, 255);
	g_assert_cmpuint (c.r, ==, 0);

	/* Keyboard inside the popup */
	key (fixture->canvas, GRL_KEY_LEFT);
	lrg_color_picker_get_color (button, &c);
	g_assert_cmpuint (c.r, >, 0);

	/* Escape closes it; the final colour is remembered */
	key (fixture->canvas, GRL_KEY_ESCAPE);
	g_assert_false (lrg_color_picker_get_popup_open (button));
	lrg_color_picker_get_recent (button, &n);
	g_assert_cmpuint (n, >=, 1);
	settle (fixture->canvas);
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 0);
}

/* ==========================================================================
 * LrgRating
 * ========================================================================== */

/* star_center: canvas centre of symbol @i (0-based) */
static void
star_center (LrgRating *rating,
             guint      i,
             gfloat     frac,
             gfloat    *x,
             gfloat    *y)
{
	gfloat size = lrg_rating_get_symbol_size (rating);
	gfloat pitch = size + roundf (size * 0.22f);

	*x = lrg_widget_get_world_x (LRG_WIDGET (rating)) + pitch * (gfloat) i + size * frac;
	*y = lrg_widget_get_world_y (LRG_WIDGET (rating)) + lrg_widget_get_height (LRG_WIDGET (rating)) * 0.5f;
}

static LrgRating *
add_rating (Fixture *fixture)
{
	LrgRating *rating = lrg_rating_new ();

	lrg_rating_set_symbol_size (rating, 20.0f);
	return add (fixture, rating, 50.0f, 50.0f, 0.0f, 0.0f);
}

static void
test_rating_hover_and_click (Fixture       *fixture,
                             gconstpointer  user_data)
{
	LrgRating *rating = add_rating (fixture);
	gint       changes = 0;
	gfloat     x;
	gfloat     y;

	g_signal_connect (rating, "value-changed", G_CALLBACK (count_signal), &changes);
	g_assert_cmpuint (lrg_rating_get_max (rating), ==, 5);
	g_assert_cmpfloat (lrg_widget_get_width (LRG_WIDGET (rating)), >, 100.0f);

	/* Hover previews */
	star_center (rating, 2, 0.5f, &x, &y);
	move_to (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_hover_value (rating), ==, 3.0);
	star_center (rating, 0, 0.5f, &x, &y);
	move_to (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_hover_value (rating), ==, 1.0);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 0.0);
	move_to (fixture->canvas, 600.0f, 500.0f);
	g_assert_cmpfloat (lrg_rating_get_hover_value (rating), <, 0.0);

	/* Click sets; the picked star pops (still animating after a frame) */
	settle (fixture->canvas);
	star_center (rating, 3, 0.5f, &x, &y);
	click_at (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 4.0);
	g_assert_cmpint (changes, ==, 1);
	g_assert_true (lrg_canvas_update (fixture->canvas, FRAME));
	settle (fixture->canvas);
	g_assert_false (lrg_canvas_update (fixture->canvas, FRAME));

	/* Paint records shapes for all five stars */
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (fixture->canvas)), >=, 50);

	/* Without halves, the left half of a star still picks the whole */
	star_center (rating, 1, 0.2f, &x, &y);
	click_at (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 2.0);
}

static void
test_rating_half_and_keyboard (Fixture       *fixture,
                               gconstpointer  user_data)
{
	LrgRating *rating = add_rating (fixture);
	gfloat     x;
	gfloat     y;

	lrg_rating_set_allow_half (rating, TRUE);
	star_center (rating, 1, 0.25f, &x, &y);
	click_at (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 1.5);
	star_center (rating, 1, 0.75f, &x, &y);
	click_at (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 2.0);

	/* Keys step by halves; arrows are consumed */
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (rating));
	key (fixture->canvas, GRL_KEY_RIGHT);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 2.5);
	key (fixture->canvas, GRL_KEY_LEFT);
	key (fixture->canvas, GRL_KEY_LEFT);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 1.5);
	key (fixture->canvas, GRL_KEY_END);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 5.0);
	key (fixture->canvas, GRL_KEY_RIGHT);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 5.0);
	key (fixture->canvas, GRL_KEY_HOME);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 0.0);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (rating));

	/* Halves turned off round the value */
	lrg_rating_set_value (rating, 3.5);
	lrg_rating_set_allow_half (rating, FALSE);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 4.0);

	/* Max shrinks: the value is clamped */
	lrg_rating_set_max (rating, 3);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 3.0);
}

static void
test_rating_read_only_and_shapes (Fixture       *fixture,
                                  gconstpointer  user_data)
{
	LrgRating *rating = add_rating (fixture);
	GrlColor   pink = { 255, 0, 128, 255 };
	gfloat     x;
	gfloat     y;
	guint      shape;

	lrg_rating_set_value (rating, 2.0);
	lrg_rating_set_read_only (rating, TRUE);
	g_assert_false (lrg_widget_get_focusable (LRG_WIDGET (rating)));
	star_center (rating, 4, 0.5f, &x, &y);
	move_to (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_hover_value (rating), <, 0.0);
	click_at (fixture->canvas, x, y);
	g_assert_cmpfloat (lrg_rating_get_value (rating), ==, 2.0);

	/* Explicit fill colour wins over the theme in the MARK part */
	lrg_rating_set_fill_color (rating, &pink);
	{
		LrgPartLook look;

		lrg_widget_resolve_look (LRG_WIDGET (rating), LRG_UI_PART_MARK, &look);
		g_assert_cmpuint (look.foreground.r, ==, 255);
		g_assert_cmpuint (look.foreground.g, ==, 0);
		g_assert_cmpuint (look.foreground.b, ==, 128);
	}

	/* Every shape records */
	for (shape = LRG_RATING_SHAPE_STAR; shape <= LRG_RATING_SHAPE_CIRCLE; shape++)
	{
		lrg_rating_set_shape (rating, (LrgRatingShape) shape);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (fixture->canvas)), >=, 5);
	}
}

/* ==========================================================================
 * LrgDatePicker
 * ========================================================================== */

static LrgDatePicker *
add_date_picker (Fixture *fixture)
{
	LrgDatePicker       *picker = lrg_date_picker_new ();
	g_autoptr(GDateTime) today = g_date_time_new_local (2026, 3, 10, 0, 0, 0.0);

	lrg_date_picker_set_today (picker, today);
	return add (fixture, picker, 40.0f, 40.0f, 200.0f, 32.0f);
}

static void
test_date_format (Fixture       *fixture,
                  gconstpointer  user_data)
{
	LrgDatePicker    *picker = add_date_picker (fixture);
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	gint              changes = 0;
	gint              y;
	gint              m;
	gint              d;

	g_signal_connect (picker, "date-changed", G_CALLBACK (count_signal), &changes);
	g_assert_null (lrg_date_picker_get_text (picker));
	g_assert_false (lrg_date_picker_get_date_ymd (picker, NULL, NULL, NULL));

	lrg_date_picker_set_date_ymd (picker, 2026, 3, 14);
	a = lrg_date_picker_get_text (picker);
	g_assert_cmpstr (a, ==, "2026-03-14");
	g_assert_cmpint (changes, ==, 1);

	/* Same day again: no signal; the time of day is dropped */
	{
		g_autoptr(GDateTime) noon = g_date_time_new_local (2026, 3, 14, 12, 30, 0.0);

		lrg_date_picker_set_date (picker, noon);
		g_assert_cmpint (changes, ==, 1);
		g_assert_cmpint (g_date_time_get_hour (lrg_date_picker_get_date (picker)), ==, 0);
	}

	lrg_date_picker_set_format (picker, "%d/%m/%Y");
	b = lrg_date_picker_get_text (picker);
	g_assert_cmpstr (b, ==, "14/03/2026");
	g_assert_true (lrg_date_picker_get_date_ymd (picker, &y, &m, &d));
	g_assert_cmpint (y * 10000 + m * 100 + d, ==, 20260314);

	/* Invalid parts are ignored */
	lrg_date_picker_set_date_ymd (picker, 2026, 2, 30);
	g_assert_cmpint (changes, ==, 1);

	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (fixture->canvas)), >, 0);
}

static void
test_date_popup_navigation (Fixture       *fixture,
                            gconstpointer  user_data)
{
	LrgDatePicker       *picker = add_date_picker (fixture);
	g_autoptr(GDateTime) cursor = NULL;
	gint                 y;
	gint                 m;

	lrg_date_picker_set_date_ymd (picker, 2026, 3, 14);

	/* Click opens the calendar on the date's month */
	click_at (fixture->canvas, 100.0f, 56.0f);
	g_assert_true (lrg_date_picker_get_popup_open (picker));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (picker)) & LRG_WIDGET_STATE_OPEN);
	lrg_date_picker_get_visible_month (picker, &y, &m);
	g_assert_cmpint (y, ==, 2026);
	g_assert_cmpint (m, ==, 3);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (fixture->canvas)), >, 10);

	/* Page Down / Up turn the month (the grid slides) */
	key (fixture->canvas, GRL_KEY_PAGE_DOWN);
	lrg_date_picker_get_visible_month (picker, &y, &m);
	g_assert_cmpint (m, ==, 4);
	g_assert_true (lrg_canvas_update (fixture->canvas, FRAME));
	key (fixture->canvas, GRL_KEY_PAGE_UP);
	key (fixture->canvas, GRL_KEY_PAGE_UP);
	lrg_date_picker_get_visible_month (picker, &y, &m);
	g_assert_cmpint (m, ==, 2);

	/* Shift+Page Down: a year */
	key_mod (fixture->canvas, GRL_KEY_PAGE_DOWN, LRG_INPUT_MODIFIER_SHIFT);
	lrg_date_picker_get_visible_month (picker, &y, &m);
	g_assert_cmpint (y, ==, 2027);
	g_assert_cmpint (m, ==, 2);

	/* Wheel over the calendar turns back */
	{
		LrgWidget *cal = lrg_canvas_get_top_popup (fixture->canvas);

		wheel_at (fixture->canvas, lrg_widget_get_world_x (cal) + 20.0f, lrg_widget_get_world_y (cal) + 60.0f, 1.0f);
	}
	lrg_date_picker_get_visible_month (picker, &y, &m);
	g_assert_cmpint (y * 100 + m, ==, 202701);

	cursor = lrg_date_picker_get_cursor_date (picker);
	g_assert_nonnull (cursor);

	/* Escape closes without changing the date */
	key (fixture->canvas, GRL_KEY_ESCAPE);
	g_assert_false (lrg_date_picker_get_popup_open (picker));
	{
		g_autofree gchar *text = lrg_date_picker_get_text (picker);

		g_assert_cmpstr (text, ==, "2026-03-14");
	}
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (picker));
}

static void
test_date_keyboard_select (Fixture       *fixture,
                           gconstpointer  user_data)
{
	LrgDatePicker    *picker = add_date_picker (fixture);
	g_autofree gchar *text = NULL;
	gint              changes = 0;
	gint              m;

	g_signal_connect (picker, "date-changed", G_CALLBACK (count_signal), &changes);
	lrg_date_picker_set_date_ymd (picker, 2026, 3, 14);
	changes = 0;

	/* Keyboard open (Enter on the focused field) */
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (picker));
	key (fixture->canvas, GRL_KEY_ENTER);
	g_assert_true (lrg_date_picker_get_popup_open (picker));

	key (fixture->canvas, GRL_KEY_RIGHT);    /* 15 */
	key (fixture->canvas, GRL_KEY_DOWN);     /* 22 */
	{
		g_autoptr(GDateTime) c = lrg_date_picker_get_cursor_date (picker);

		g_assert_cmpint (g_date_time_get_day_of_month (c), ==, 22);
	}
	key (fixture->canvas, GRL_KEY_ENTER);
	g_assert_false (lrg_date_picker_get_popup_open (picker));
	text = lrg_date_picker_get_text (picker);
	g_assert_cmpstr (text, ==, "2026-03-22");
	g_assert_cmpint (changes, ==, 1);
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (picker)) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	/* Crossing the month end turns the calendar */
	lrg_date_picker_set_date_ymd (picker, 2026, 3, 31);
	key (fixture->canvas, GRL_KEY_ENTER);
	key (fixture->canvas, GRL_KEY_RIGHT);
	lrg_date_picker_get_visible_month (picker, NULL, &m);
	g_assert_cmpint (m, ==, 4);
	key (fixture->canvas, GRL_KEY_HOME);
	key (fixture->canvas, GRL_KEY_END);
	{
		g_autoptr(GDateTime) c = lrg_date_picker_get_cursor_date (picker);

		g_assert_cmpint (g_date_time_get_day_of_month (c), ==, 30);
	}
	key (fixture->canvas, GRL_KEY_ESCAPE);
}

static void
test_date_click_and_limits (Fixture       *fixture,
                            gconstpointer  user_data)
{
	LrgDatePicker       *picker = add_date_picker (fixture);
	g_autoptr(GDateTime) lo = g_date_time_new_local (2026, 3, 10, 0, 0, 0.0);
	g_autoptr(GDateTime) hi = g_date_time_new_local (2026, 3, 20, 0, 0, 0.0);
	GrlRectangle         cell;
	gint                 d;

	/* Clicking a day picks it and closes */
	lrg_date_picker_set_date_ymd (picker, 2026, 3, 14);
	lrg_date_picker_popup (picker);
	g_assert_true (lrg_date_picker_get_day_bounds (picker, 2026, 3, 18, &cell));
	click_rect (fixture->canvas, &cell);
	g_assert_false (lrg_date_picker_get_popup_open (picker));
	lrg_date_picker_get_date_ymd (picker, NULL, NULL, &d);
	g_assert_cmpint (d, ==, 18);

	/* Limits clamp the date */
	lrg_date_picker_set_min_date (picker, lo);
	lrg_date_picker_set_max_date (picker, hi);
	lrg_date_picker_set_date_ymd (picker, 2026, 3, 1);
	lrg_date_picker_get_date_ymd (picker, NULL, NULL, &d);
	g_assert_cmpint (d, ==, 10);
	lrg_date_picker_set_date_ymd (picker, 2026, 4, 2);
	lrg_date_picker_get_date_ymd (picker, NULL, NULL, &d);
	g_assert_cmpint (d, ==, 20);

	/* Disabled days cannot be clicked; the cursor stops at the limit */
	lrg_date_picker_set_date_ymd (picker, 2026, 3, 12);
	move_to (fixture->canvas, 600.0f, 500.0f);
	lrg_date_picker_popup (picker);
	g_assert_true (lrg_date_picker_get_day_bounds (picker, 2026, 3, 5, &cell));
	click_rect (fixture->canvas, &cell);
	g_assert_true (lrg_date_picker_get_popup_open (picker));
	lrg_date_picker_get_date_ymd (picker, NULL, NULL, &d);
	g_assert_cmpint (d, ==, 12);
	lrg_canvas_set_focused_widget (fixture->canvas, lrg_canvas_get_top_popup (fixture->canvas));
	key (fixture->canvas, GRL_KEY_UP);
	{
		g_autoptr(GDateTime) c = lrg_date_picker_get_cursor_date (picker);

		g_assert_cmpint (g_date_time_get_day_of_month (c), ==, 10);
	}

	/* A tighter limit re-clamps the current date */
	{
		g_autoptr(GDateTime) lo2 = g_date_time_new_local (2026, 3, 15, 0, 0, 0.0);

		lrg_date_picker_popdown (picker);
		lrg_date_picker_set_min_date (picker, lo2);
		lrg_date_picker_get_date_ymd (picker, NULL, NULL, &d);
		g_assert_cmpint (d, ==, 15);
	}
}

/* ==========================================================================
 * LrgTimePicker
 * ========================================================================== */

static void
test_time_wrap_and_modes (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgTimePicker    *picker = add (fixture, lrg_time_picker_new (), 40.0f, 40.0f, 0.0f, 0.0f);
	g_autofree gchar *t1 = NULL;
	g_autofree gchar *t2 = NULL;
	g_autofree gchar *t3 = NULL;
	gint              changes = 0;

	g_signal_connect (picker, "time-changed", G_CALLBACK (count_signal), &changes);

	/* 24h: fields wrap without carrying */
	lrg_time_picker_set_time (picker, 23, 59, 30);
	g_assert_cmpint (changes, ==, 1);
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_HOUR, 1);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 0);
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_MINUTE, 1);
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 0);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 0);
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_MINUTE, -1);
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 59);
	t1 = lrg_time_picker_get_text (picker);
	g_assert_cmpstr (t1, ==, "00:59");

	/* Seconds field */
	lrg_time_picker_set_show_seconds (picker, TRUE);
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_SECOND, 45);
	g_assert_cmpint (lrg_time_picker_get_second (picker), ==, 15);

	/* 12h: 13:05 shows as 01 PM; the period toggles by 12 hours */
	lrg_time_picker_set_show_seconds (picker, FALSE);
	lrg_time_picker_set_use_24h (picker, FALSE);
	lrg_time_picker_set_time (picker, 13, 5, 0);
	t2 = lrg_time_picker_get_text (picker);
	g_assert_cmpstr (t2, ==, "01:05 PM");
	g_assert_true (lrg_time_picker_get_is_pm (picker));
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_PERIOD, 1);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 1);

	/* 12h hour cycles 12, 1 .. 11 inside the half day */
	lrg_time_picker_set_hour (picker, 11);
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_HOUR, 1);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 0);
	t3 = lrg_time_picker_get_text (picker);
	g_assert_cmpstr (t3, ==, "12:05 AM");
	lrg_time_picker_step_segment (picker, LRG_TIME_PICKER_SEGMENT_HOUR, -1);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 11);

	/* Clamping */
	lrg_time_picker_set_minute (picker, 99);
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 59);

	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (fixture->canvas)), >, 0);
}

static void
test_time_input (Fixture       *fixture,
                 gconstpointer  user_data)
{
	LrgTimePicker *picker = add (fixture, lrg_time_picker_new (), 40.0f, 40.0f, 0.0f, 0.0f);
	GrlRectangle   hr;
	GrlRectangle   mr;
	GrlRectangle   pr;

	lrg_time_picker_set_time (picker, 10, 30, 0);
	g_assert_true (lrg_time_picker_get_segment_bounds (picker, LRG_TIME_PICKER_SEGMENT_HOUR, &hr));
	g_assert_true (lrg_time_picker_get_segment_bounds (picker, LRG_TIME_PICKER_SEGMENT_MINUTE, &mr));
	g_assert_false (lrg_time_picker_get_segment_bounds (picker, LRG_TIME_PICKER_SEGMENT_PERIOD, &pr));
	g_assert_cmpfloat (mr.x, >, hr.x + hr.width);

	/* Keyboard: Up / Down on the active field, Left / Right move */
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (picker));
	key (fixture->canvas, GRL_KEY_UP);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 11);
	key (fixture->canvas, GRL_KEY_RIGHT);
	g_assert_cmpint (lrg_time_picker_get_active_segment (picker), ==, LRG_TIME_PICKER_SEGMENT_MINUTE);
	key (fixture->canvas, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 29);
	key (fixture->canvas, GRL_KEY_PAGE_UP);
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 39);

	/* Typed digits fill a field and move on */
	lrg_time_picker_set_active_segment (picker, LRG_TIME_PICKER_SEGMENT_HOUR);
	type_text (fixture->canvas, "09");
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 9);
	g_assert_cmpint (lrg_time_picker_get_active_segment (picker), ==, LRG_TIME_PICKER_SEGMENT_MINUTE);
	type_text (fixture->canvas, "45");
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 45);
	/* A digit that cannot start a pair completes at once */
	lrg_time_picker_set_active_segment (picker, LRG_TIME_PICKER_SEGMENT_HOUR);
	type_text (fixture->canvas, "7");
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 7);
	g_assert_cmpint (lrg_time_picker_get_active_segment (picker), ==, LRG_TIME_PICKER_SEGMENT_MINUTE);

	/* Wheel over the minutes */
	wheel_at (fixture->canvas, mr.x + mr.width * 0.5f, mr.y + mr.height * 0.5f, 1.0f);
	g_assert_cmpint (lrg_time_picker_get_minute (picker), ==, 46);

	/* Drag up on the hours: one step per 10 px; the digits roll */
	press_at (fixture->canvas, hr.x + hr.width * 0.5f, hr.y + hr.height * 0.5f);
	drag_to (fixture->canvas, hr.x + hr.width * 0.5f, hr.y + hr.height * 0.5f - 25.0f);
	release_at (fixture->canvas, hr.x + hr.width * 0.5f, hr.y + hr.height * 0.5f - 25.0f);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 9);

	/* 12h: clicking AM / PM toggles it */
	lrg_time_picker_set_use_24h (picker, FALSE);
	{
		gfloat w;
		gfloat h;

		/* Wider now (free positioning: resize to the new preferred size) */
		lrg_widget_get_preferred_size (LRG_WIDGET (picker), &w, &h);
		g_assert_cmpfloat (w, >, lrg_widget_get_width (LRG_WIDGET (picker)));
		lrg_widget_set_size (LRG_WIDGET (picker), w, h);
	}
	lrg_canvas_record (fixture->canvas);
	g_assert_true (lrg_time_picker_get_segment_bounds (picker, LRG_TIME_PICKER_SEGMENT_PERIOD, &pr));
	click_rect (fixture->canvas, &pr);
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 21);
	type_text (fixture->canvas, "a");
	g_assert_cmpint (lrg_time_picker_get_hour (picker), ==, 9);
}

/* ==========================================================================
 * LrgBreadcrumb
 * ========================================================================== */

static void
on_activated (LrgBreadcrumb *crumb,
              guint          index,
              gpointer       user_data)
{
	*(gint *) user_data = (gint) index;
}

static LrgBreadcrumb *
add_breadcrumb (Fixture *fixture,
                gfloat   width)
{
	LrgBreadcrumb       *crumb = lrg_breadcrumb_new ();
	static const gchar *labels[] = { "Home", "Documents", "Projects", "libregnum", "src", "ui" };
	guint               i;

	for (i = 0; i < G_N_ELEMENTS (labels); i++)
		lrg_breadcrumb_append (crumb, labels[i]);
	return add (fixture, crumb, 20.0f, 20.0f, width, 32.0f);
}

static void
test_breadcrumb_collapse (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgBreadcrumb *crumb = add_breadcrumb (fixture, 760.0f);
	GrlRectangle   r;
	GrlRectangle   last;
	guint          first;
	guint          n;
	gfloat         pw;
	gfloat         ph;

	g_assert_cmpuint (lrg_breadcrumb_get_n_segments (crumb), ==, 6);
	g_assert_false (lrg_breadcrumb_get_collapsed (crumb));
	g_assert_false (lrg_breadcrumb_get_segment_bounds (crumb, LRG_BREADCRUMB_ELLIPSIS, &r));
	lrg_widget_measure (LRG_WIDGET (crumb), &pw, &ph);
	g_assert_cmpfloat (pw, <, 760.0f);

	/* Narrow: the middle folds; first and last stay */
	lrg_widget_set_size (LRG_WIDGET (crumb), pw * 0.6f, 32.0f);
	lrg_canvas_record (fixture->canvas);
	g_assert_true (lrg_breadcrumb_get_collapsed (crumb));
	g_assert_true (lrg_breadcrumb_get_hidden_range (crumb, &first, &n));
	g_assert_cmpuint (first, ==, 1);
	g_assert_cmpuint (n, >=, 1);
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, 0, &r));
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, 5, &last));
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, LRG_BREADCRUMB_ELLIPSIS, &r));
	g_assert_false (lrg_breadcrumb_get_segment_bounds (crumb, 1, &r));
	g_assert_cmpfloat (last.x + last.width, <=, 20.0f + pw * 0.6f + 0.5f);

	/* Very narrow: still sane, the last label is shortened */
	lrg_widget_set_size (LRG_WIDGET (crumb), 60.0f, 32.0f);
	lrg_canvas_record (fixture->canvas);
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, 5, &last));
	g_assert_cmpfloat (last.x + last.width, <=, 80.5f);

	/* Wide again */
	lrg_widget_set_size (LRG_WIDGET (crumb), 760.0f, 32.0f);
	lrg_canvas_record (fixture->canvas);
	g_assert_false (lrg_breadcrumb_get_collapsed (crumb));

	/* Truncate: going up the path */
	lrg_breadcrumb_truncate (crumb, 2);
	g_assert_cmpuint (lrg_breadcrumb_get_n_segments (crumb), ==, 2);
	g_assert_cmpstr (lrg_breadcrumb_get_segment_label (crumb, 1), ==, "Documents");
}

static void
test_breadcrumb_activate (Fixture       *fixture,
                          gconstpointer  user_data)
{
	LrgBreadcrumb *crumb = add_breadcrumb (fixture, 760.0f);
	GrlRectangle   r;
	gint           activated = -1;
	gfloat         pw;
	gfloat         ph;
	guint          first;

	g_signal_connect (crumb, "activated", G_CALLBACK (on_activated), &activated);

	/* Click a segment */
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, 2, &r));
	click_rect (fixture->canvas, &r);
	g_assert_cmpint (activated, ==, 2);

	/* Keyboard: Left from the last segment, Enter activates */
	activated = -1;
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (crumb));
	key (fixture->canvas, GRL_KEY_END);
	key (fixture->canvas, GRL_KEY_LEFT);
	key (fixture->canvas, GRL_KEY_ENTER);
	g_assert_cmpint (activated, ==, 4);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (crumb));

	/* Collapsed: the ellipsis opens a list of the hidden segments */
	lrg_widget_measure (LRG_WIDGET (crumb), &pw, &ph);
	lrg_widget_set_size (LRG_WIDGET (crumb), pw * 0.6f, 32.0f);
	lrg_canvas_record (fixture->canvas);
	g_assert_true (lrg_breadcrumb_get_hidden_range (crumb, &first, NULL));
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, LRG_BREADCRUMB_ELLIPSIS, &r));
	click_rect (fixture->canvas, &r);
	g_assert_true (lrg_breadcrumb_get_popup_open (crumb));
	lrg_canvas_record (fixture->canvas);
	{
		LrgWidget *popup = lrg_canvas_get_top_popup (fixture->canvas);
		LrgWidget *row;

		g_assert_nonnull (popup);
		row = lrg_container_get_child (LRG_CONTAINER (popup), 0);
		activated = -1;
		click_at (fixture->canvas, lrg_widget_get_world_x (row) + 10.0f,
		          lrg_widget_get_world_y (row) + lrg_widget_get_height (row) * 0.5f);
	}
	g_assert_cmpint (activated, ==, (gint) first);
	g_assert_false (lrg_breadcrumb_get_popup_open (crumb));

	/* A second click on the ellipsis while open closes it */
	settle (fixture->canvas);
	click_rect (fixture->canvas, &r);
	g_assert_true (lrg_breadcrumb_get_popup_open (crumb));
	click_rect (fixture->canvas, &r);
	g_assert_false (lrg_breadcrumb_get_popup_open (crumb));
}

/* ==========================================================================
 * LrgMarkdownLabel
 * ========================================================================== */

static const gchar *sample_md =
	"# Title\n"
	"\n"
	"Some **bold** and *it* text with `code` and [a link](https://x.y).\n"
	"A soft break joins snake_case_words.\n"
	"\n"
	"- one\n"
	"- two\n"
	"  - nested\n"
	"1. first\n"
	"2. second\n"
	"\n"
	"> quote line\n"
	"> more\n"
	"\n"
	"```\n"
	"code block\n"
	"  indented\n"
	"```\n"
	"\n"
	"---\n"
	"### Small *heading*\n";

static void
test_markdown_blocks (void)
{
	g_autoptr(LrgMarkdownLabel) md = lrg_markdown_label_new (sample_md);
	static const LrgMarkdownBlockKind kinds[] = {
		LRG_MARKDOWN_BLOCK_HEADING, LRG_MARKDOWN_BLOCK_PARAGRAPH,
		LRG_MARKDOWN_BLOCK_BULLET_ITEM, LRG_MARKDOWN_BLOCK_BULLET_ITEM, LRG_MARKDOWN_BLOCK_BULLET_ITEM,
		LRG_MARKDOWN_BLOCK_NUMBERED_ITEM, LRG_MARKDOWN_BLOCK_NUMBERED_ITEM,
		LRG_MARKDOWN_BLOCK_QUOTE, LRG_MARKDOWN_BLOCK_CODE_BLOCK, LRG_MARKDOWN_BLOCK_RULE,
		LRG_MARKDOWN_BLOCK_HEADING
	};
	guint i;

	g_assert_cmpuint (lrg_markdown_label_get_n_blocks (md), ==, G_N_ELEMENTS (kinds));
	for (i = 0; i < G_N_ELEMENTS (kinds); i++)
		g_assert_cmpint (lrg_markdown_label_get_block_kind (md, i), ==, kinds[i]);

	g_assert_cmpint (lrg_markdown_label_get_block_level (md, 0), ==, 1);
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 0), ==, "Title");
	g_assert_cmpint (lrg_markdown_label_get_block_level (md, 3), ==, 0);
	g_assert_cmpint (lrg_markdown_label_get_block_level (md, 4), ==, 1);
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 4), ==, "nested");
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 6), ==, "second");
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 7), ==, "quote line more");
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 8), ==, "code block\n  indented");
	g_assert_cmpint (lrg_markdown_label_get_block_level (md, 10), ==, 3);
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 10), ==, "Small heading");

	/* The paragraph: plain text, then its spans */
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 1), ==,
	                 "Some bold and it text with code and a link. A soft break joins snake_case_words.");
	{
		static const struct
		{
			const gchar          *text;
			LrgMarkdownSpanStyle  style;
		} spans[] = {
			{ "Some ", LRG_MARKDOWN_SPAN_NONE },
			{ "bold", LRG_MARKDOWN_SPAN_BOLD },
			{ " and ", LRG_MARKDOWN_SPAN_NONE },
			{ "it", LRG_MARKDOWN_SPAN_ITALIC },
			{ " text with ", LRG_MARKDOWN_SPAN_NONE },
			{ "code", LRG_MARKDOWN_SPAN_CODE },
			{ " and ", LRG_MARKDOWN_SPAN_NONE },
			{ "a link", LRG_MARKDOWN_SPAN_LINK },
			{ ". A soft break joins snake_case_words.", LRG_MARKDOWN_SPAN_NONE }
		};

		g_assert_cmpuint (lrg_markdown_label_get_block_n_spans (md, 1), ==, G_N_ELEMENTS (spans));
		for (i = 0; i < G_N_ELEMENTS (spans); i++)
		{
			LrgMarkdownSpanStyle style;

			g_assert_cmpstr (lrg_markdown_label_get_block_span (md, 1, i, &style), ==, spans[i].text);
			g_assert_cmpint (style, ==, spans[i].style);
		}
	}

	g_assert_cmpuint (lrg_markdown_label_get_n_links (md), ==, 1);
	g_assert_cmpstr (lrg_markdown_label_get_link_url (md, 0), ==, "https://x.y");
	g_assert_true (lrg_widget_get_focusable (LRG_WIDGET (md)));

	/* Nesting, escapes and literal markers */
	lrg_markdown_label_set_markdown (md, "***both*** \\*not\\* a * b");
	g_assert_cmpuint (lrg_markdown_label_get_n_blocks (md), ==, 1);
	{
		LrgMarkdownSpanStyle style;

		g_assert_cmpstr (lrg_markdown_label_get_block_span (md, 0, 0, &style), ==, "both");
		g_assert_cmpint (style, ==, LRG_MARKDOWN_SPAN_BOLD | LRG_MARKDOWN_SPAN_ITALIC);
		g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 0), ==, "both *not* a * b");
	}
	g_assert_cmpuint (lrg_markdown_label_get_n_links (md), ==, 0);
	g_assert_false (lrg_widget_get_focusable (LRG_WIDGET (md)));

	lrg_markdown_label_set_markdown (md, NULL);
	g_assert_cmpuint (lrg_markdown_label_get_n_blocks (md), ==, 0);
}

static void
test_markdown_wrapping (Fixture       *fixture,
                        gconstpointer  user_data)
{
	LrgMarkdownLabel *md = lrg_markdown_label_new (
		"A fairly long paragraph of plain words that will certainly need to wrap onto several lines "
		"once the label is made narrow enough, while staying on one or two lines when it is wide.\n\n"
		"- a list item that is also rather long and wraps under its own indentation\n");
	gfloat            w;
	gfloat            h_wide;
	gfloat            h_narrow;
	guint             wide;
	guint             narrow;

	add (fixture, md, 10.0f, 10.0f, 760.0f, 40.0f);
	wide = lrg_markdown_label_get_n_lines (md);
	lrg_widget_measure (LRG_WIDGET (md), &w, &h_wide);
	g_assert_cmpfloat (w, ==, 760.0f);

	/* Re-layout on a width change; the preferred height follows */
	lrg_widget_set_size (LRG_WIDGET (md), 160.0f, 40.0f);
	lrg_canvas_record (fixture->canvas);
	narrow = lrg_markdown_label_get_n_lines (md);
	lrg_widget_measure (LRG_WIDGET (md), &w, &h_narrow);
	g_assert_cmpuint (narrow, >, wide);
	g_assert_cmpuint (narrow, >=, 6);
	g_assert_cmpfloat (h_narrow, >, h_wide);

	/* Unsized labels wrap at a natural width */
	{
		g_autoptr(LrgMarkdownLabel) loose = lrg_markdown_label_new ("short");

		lrg_widget_measure (LRG_WIDGET (loose), &w, &h_wide);
		g_assert_cmpfloat (w, >, 0.0f);
		g_assert_cmpfloat (w, <=, 480.0f);
		g_assert_cmpuint (lrg_markdown_label_get_n_lines (loose), ==, 1);
	}

	/* Paints text, chips, panels */
	lrg_markdown_label_set_markdown (md, sample_md);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (fixture->canvas)), >=, 6);
}

/* find_rich_span: the span of @rich whose text is @text */
static const LrgTextSpan *
find_rich_span (LrgRichText *rich,
                const gchar *text)
{
	guint i;

	for (i = 0; i < lrg_rich_text_get_span_count (rich); i++)
		if (g_strcmp0 (lrg_text_span_get_text (lrg_rich_text_get_span (rich, i)), text) == 0)
			return lrg_rich_text_get_span (rich, i);
	return NULL;
}

static void
test_markdown_rich_text (void)
{
	g_autoptr(LrgMarkdownLabel) md = lrg_markdown_label_new (
		"# Big\n"
		"\n"
		"Some **bold** and *it* with `code`, [a link](lrg://x), [wave]waves[/wave], "
		"[color=#ff0000]red[/color], a[3] and \\[x].\n"
		"\n"
		"```\n"
		"int a[3];\n"
		"```\n");
	LrgRichText       *rich;
	const LrgTextSpan *span;
	const gchar       *markup;
	guint8             r;
	guint8             g;
	guint8             b;
	guint8             a;

	g_assert_cmpuint (lrg_markdown_label_get_n_blocks (md), ==, 3);

	/* Headings are bold rich text */
	rich = lrg_markdown_label_get_block_rich_text (md, 0);
	g_assert_true (LRG_IS_RICH_TEXT (rich));
	span = find_rich_span (rich, "Big");
	g_assert_nonnull (span);
	g_assert_true (lrg_text_span_get_style (span) & LRG_TEXT_STYLE_BOLD);

	/* The paragraph: Markdown became BBCode, rich tags passed through */
	markup = lrg_markdown_label_get_block_markup (md, 1);
	g_assert_nonnull (strstr (markup, "[b]bold[/b]"));
	g_assert_nonnull (strstr (markup, "[i]it[/i]"));
	g_assert_nonnull (strstr (markup, "[u]a link[/u]"));
	g_assert_nonnull (strstr (markup, "[wave]waves[/wave]"));
	rich = lrg_markdown_label_get_block_rich_text (md, 1);
	span = find_rich_span (rich, "bold");
	g_assert_nonnull (span);
	g_assert_cmpint (lrg_text_span_get_style (span), ==, LRG_TEXT_STYLE_BOLD);
	span = find_rich_span (rich, "it");
	g_assert_nonnull (span);
	g_assert_cmpint (lrg_text_span_get_style (span), ==, LRG_TEXT_STYLE_ITALIC);
	span = find_rich_span (rich, "a link");
	g_assert_nonnull (span);
	g_assert_true (lrg_text_span_get_style (span) & LRG_TEXT_STYLE_UNDERLINE);
	span = find_rich_span (rich, "waves");
	g_assert_nonnull (span);
	g_assert_cmpint (lrg_text_span_get_effect_type (span), ==, LRG_TEXT_EFFECT_WAVE);
	span = find_rich_span (rich, "red");
	g_assert_nonnull (span);
	lrg_text_span_get_color (span, &r, &g, &b, &a);
	g_assert_cmpuint (r, ==, 255);
	g_assert_cmpuint (g, ==, 0);
	g_assert_cmpuint (b, ==, 0);

	/* Brackets that are not tags stay text (stored as U+E000 in markup) */
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 1), ==,
	                 "Some bold and it with code, a link, waves, red, a[3] and [x].");
	g_assert_cmpuint (lrg_markdown_label_get_n_links (md), ==, 1);
	g_assert_cmpstr (lrg_markdown_label_get_link_url (md, 0), ==, "lrg://x");

	/* Code blocks keep their brackets */
	g_assert_cmpstr (lrg_markdown_label_get_block_text (md, 2), ==, "int a[3];");
	g_assert_nonnull (strstr (lrg_markdown_label_get_block_markup (md, 2), "\xee\x80\x80"));
	g_assert_null (lrg_markdown_label_get_block_rich_text (md, 3));
}

static void
test_markdown_effects (Fixture       *fixture,
                       gconstpointer  user_data)
{
	LrgMarkdownLabel *md = lrg_markdown_label_new ("[typewriter=20]Hello there[/typewriter]");
	guint             i;

	add (fixture, md, 10.0f, 10.0f, 400.0f, 60.0f);
	g_assert_false (lrg_markdown_label_get_effects_complete (md));

	/* Animated from step(): the canvas stays busy until it has typed */
	g_assert_true (lrg_canvas_update (fixture->canvas, FRAME));
	for (i = 0; i < 600 && !lrg_markdown_label_get_effects_complete (md); i++)
		lrg_canvas_update (fixture->canvas, FRAME);
	g_assert_true (lrg_markdown_label_get_effects_complete (md));
	g_assert_cmpuint (i, >, 30);

	/* Reset starts it over */
	lrg_markdown_label_reset_effects (md);
	g_assert_false (lrg_markdown_label_get_effects_complete (md));

	/* Looping effects keep the canvas animating */
	lrg_markdown_label_set_markdown (md, "Some [wave]wavy[/wave] text");
	for (i = 0; i < 10; i++)
		g_assert_true (lrg_canvas_update (fixture->canvas, FRAME));
	g_assert_false (lrg_markdown_label_get_effects_complete (md));

	/* Plain text settles */
	lrg_markdown_label_set_markdown (md, "Plain **text**");
	settle (fixture->canvas);
	g_assert_false (lrg_canvas_update (fixture->canvas, FRAME));
}

static void
on_link (LrgMarkdownLabel *md,
         const gchar      *url,
         gpointer          user_data)
{
	g_free (*(gchar **) user_data);
	*(gchar **) user_data = g_strdup (url);
}

static void
test_markdown_links (Fixture       *fixture,
                     gconstpointer  user_data)
{
	LrgMarkdownLabel *md = lrg_markdown_label_new ("Go [home](lrg://home) or [away](lrg://away).");
	g_autofree gchar *url = NULL;
	GrlRectangle      r;
	GrlColor          red = { 200, 0, 0, 255 };

	add (fixture, md, 10.0f, 10.0f, 400.0f, 40.0f);
	g_signal_connect (md, "link-activated", G_CALLBACK (on_link), &url);

	g_assert_true (lrg_markdown_label_get_link_bounds (md, 1, &r));
	click_rect (fixture->canvas, &r);
	g_assert_cmpstr (url, ==, "lrg://away");
	g_assert_false (lrg_markdown_label_get_link_bounds (md, 2, &r));

	/* Keyboard: focus, Left to the first link, Enter */
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (md));
	key (fixture->canvas, GRL_KEY_LEFT);
	key (fixture->canvas, GRL_KEY_ENTER);
	g_assert_cmpstr (url, ==, "lrg://home");

	/* Clicking plain text activates nothing */
	g_clear_pointer (&url, g_free);
	click_at (fixture->canvas, 14.0f, r.y + r.height * 0.5f);
	g_assert_null (url);

	/* Explicit text colour beats the theme */
	lrg_markdown_label_set_text_color (md, &red);
	{
		LrgPartLook look;

		lrg_widget_resolve_look (LRG_WIDGET (md), LRG_UI_PART_LABEL, &look);
		g_assert_cmpuint (look.foreground.r, ==, 200);
		g_assert_cmpuint (look.foreground.g, ==, 0);
	}
}

/* ==========================================================================
 * All widgets in all theme presets
 * ========================================================================== */

static void
test_all_presets (Fixture       *fixture,
                  gconstpointer  user_data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	LrgColorPicker      *full = add (fixture, lrg_color_picker_new (), 10.0f, 10.0f, 0.0f, 0.0f);
	LrgColorPicker      *compact = lrg_color_picker_new ();
	LrgRating           *rating = add (fixture, lrg_rating_new (), 300.0f, 10.0f, 0.0f, 0.0f);
	LrgDatePicker       *date = add (fixture, lrg_date_picker_new (), 300.0f, 60.0f, 0.0f, 0.0f);
	LrgTimePicker       *time = add (fixture, lrg_time_picker_new (), 300.0f, 110.0f, 0.0f, 0.0f);
	LrgBreadcrumb       *crumb = add (fixture, lrg_breadcrumb_new (), 300.0f, 160.0f, 120.0f, 32.0f);
	LrgMarkdownLabel    *md = add (fixture, lrg_markdown_label_new (sample_md), 300.0f, 210.0f, 300.0f, 100.0f);
	guint                n = 0;
	guint                i;

	lrg_color_picker_set_popup_mode (compact, TRUE);
	add (fixture, compact, 450.0f, 10.0f, 0.0f, 0.0f);
	lrg_breadcrumb_append (crumb, "One");
	lrg_breadcrumb_append (crumb, "Two");
	lrg_breadcrumb_append (crumb, "Three");
	lrg_breadcrumb_append (crumb, "Four");
	lrg_rating_set_value (rating, 3.5);
	lrg_date_picker_set_date_ymd (date, 2026, 11, 5);
	lrg_time_picker_set_time (time, 13, 37, 0);
	(void) full;
	(void) md;

	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);
		LrgPaintList       *list;
		LrgPartLook         look;

		g_assert_nonnull (theme);
		lrg_widget_set_theme (LRG_WIDGET (fixture->canvas), theme);
		list = lrg_canvas_record (fixture->canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 100);

		/* Looks come from the preset */
		lrg_widget_resolve_look (LRG_WIDGET (date), LRG_UI_PART_BOX, &look);
		g_assert_true (memcmp (&look.background, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT),
		                       sizeof (GrlColor)) == 0);
		lrg_widget_resolve_look (LRG_WIDGET (rating), LRG_UI_PART_MARK, &look);
		g_assert_true (memcmp (&look.foreground, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING),
		                       sizeof (GrlColor)) == 0);
		n++;
	}
	g_assert_cmpuint (n, ==, 5);
}

/* ==========================================================================
 * GPU: the saturation / value square's gradients
 * ========================================================================== */

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
	test_window = grl_window_new (8, 8, "lrg-pickers-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static gboolean
near (guint8 got,
      gint   want,
      gint   tolerance)
{
	return ABS ((gint) got - want) <= tolerance;
}

static void
test_gpu_sv_square (Fixture       *fixture,
                    gconstpointer  user_data)
{
	LrgColorPicker  *picker;
	GrlRectangle     sv;
	RenderTexture2D  target;
	Image            image;
	Color            c;
	gint             x0;
	gint             y0;
	gint             x1;
	gint             y1;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	picker = add (fixture, lrg_color_picker_new (), 10.0f, 10.0f, 0.0f, 0.0f);
	lrg_canvas_set_paint_background (fixture->canvas, FALSE);
	/* Red hue; the knob parked in the bottom-left corner */
	lrg_color_picker_set_hsv (picker, 0.0, 0.0, 0.0);
	settle (fixture->canvas);
	g_assert_true (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_SV, &sv));

	target = LoadRenderTexture (800, 600);
	BeginTextureMode (target);
	ClearBackground ((Color) { 0, 0, 0, 255 });
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (),
	                                          lrg_canvas_record (fixture->canvas)));
	EndTextureMode ();
	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);

	x0 = (gint) sv.x + 6;
	y0 = (gint) sv.y + 6;
	x1 = (gint) (sv.x + sv.width) - 7;
	y1 = (gint) (sv.y + sv.height) - 7;

	/* Top-left: white (no saturation, full value) */
	c = GetImageColor (image, x0, y0);
	g_assert_true (near (c.r, 250, 12) && near (c.g, 250, 12) && near (c.b, 250, 12));

	/* Top-right: the pure hue */
	c = GetImageColor (image, x1, y0);
	g_assert_true (near (c.r, 250, 12));
	g_assert_true (near (c.g, 6, 14) && near (c.b, 6, 14));

	/* Bottom-right: black */
	c = GetImageColor (image, x1, y1);
	g_assert_true (near (c.r, 4, 14) && near (c.g, 4, 14) && near (c.b, 4, 14));

	/* Centre: half saturation, half value = (128, 64, 64) */
	c = GetImageColor (image, (gint) (sv.x + sv.width * 0.5f), (gint) (sv.y + sv.height * 0.5f));
	g_assert_true (near (c.r, 128, 16));
	g_assert_true (near (c.g, 64, 16));
	g_assert_true (near (c.b, 64, 16));

	/* The hue strip's left end is red, its middle cyan */
	{
		GrlRectangle hue;

		g_assert_true (lrg_color_picker_get_area_bounds (picker, LRG_COLOR_PICKER_AREA_HUE, &hue));
		c = GetImageColor (image, (gint) (hue.x + hue.width * 0.5f), (gint) (hue.y + hue.height * 0.5f));
		g_assert_cmpuint (c.r, <, 80);
		g_assert_cmpuint (c.g, >, 180);
		g_assert_cmpuint (c.b, >, 180);
	}

	UnloadImage (image);
}

/* ==========================================================================
 * main
 * ========================================================================== */

#define ADD(path, func) \
	g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/pickers/color/hsv-roundtrip", test_color_hsv_roundtrip);
	ADD ("/ui/pickers/color/sv-click-drag", test_color_sv_click_and_drag);
	ADD ("/ui/pickers/color/hue-alpha", test_color_hue_and_alpha);
	ADD ("/ui/pickers/color/hex-entry", test_color_hex_entry);
	ADD ("/ui/pickers/color/keyboard-swatches", test_color_keyboard_and_swatches);
	ADD ("/ui/pickers/color/popup-mode", test_color_popup_mode);

	ADD ("/ui/pickers/rating/hover-click", test_rating_hover_and_click);
	ADD ("/ui/pickers/rating/half-keyboard", test_rating_half_and_keyboard);
	ADD ("/ui/pickers/rating/read-only-shapes", test_rating_read_only_and_shapes);

	ADD ("/ui/pickers/date/format", test_date_format);
	ADD ("/ui/pickers/date/popup-navigation", test_date_popup_navigation);
	ADD ("/ui/pickers/date/keyboard-select", test_date_keyboard_select);
	ADD ("/ui/pickers/date/click-limits", test_date_click_and_limits);

	ADD ("/ui/pickers/time/wrap-modes", test_time_wrap_and_modes);
	ADD ("/ui/pickers/time/input", test_time_input);

	ADD ("/ui/pickers/breadcrumb/collapse", test_breadcrumb_collapse);
	ADD ("/ui/pickers/breadcrumb/activate", test_breadcrumb_activate);

	g_test_add_func ("/ui/pickers/markdown/blocks", test_markdown_blocks);
	ADD ("/ui/pickers/markdown/wrapping", test_markdown_wrapping);
	ADD ("/ui/pickers/markdown/links", test_markdown_links);
	g_test_add_func ("/ui/pickers/markdown/rich-text", test_markdown_rich_text);
	ADD ("/ui/pickers/markdown/effects", test_markdown_effects);

	ADD ("/ui/pickers/presets", test_all_presets);

	if (!g_test_subprocess ())
		graphics_available = init_graphics_context ();
	ADD ("/ui/pickers/gpu/sv-square", test_gpu_sv_square);

	result = g_test_run ();

	g_clear_object (&test_window);
	return result;
}
