/* test-ui-popups.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for combo box, dialog, toast and toast queue (popup-layer widgets).
 *
 * Everything is driven through lrg_canvas_process_input() /
 * lrg_canvas_update() / lrg_canvas_record() the way a real frame loop
 * does, so focus, capture, bubbling and popup dismissal are exercised
 * end to end. Toast timing mostly uses lrg_toast_queue_advance() with
 * auto-advance off; the frame-clock cases drive auto-advance through
 * lrg_canvas_update() with fixed frame times. Both are deterministic.
 * One GPU test (skipped
 * without a display) checks the modal veil in pixels.
 */

#include <glib.h>
#include <math.h>
#include <raylib.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Input helpers
 * ========================================================================== */

/* frame_pointer: a frame with the pointer at (@x, @y) and nothing else */
static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = DT;
}

/* click_at: move, press, release at (@x, @y) */
static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_process_input (canvas, &input);

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

/* click_widget: click the centre of @widget */
static void
click_widget (LrgCanvas *canvas,
              LrgWidget *widget)
{
	click_at (canvas,
	          lrg_widget_get_world_x (widget) + lrg_widget_get_width (widget) * 0.5f,
	          lrg_widget_get_world_y (widget) + lrg_widget_get_height (widget) * 0.5f);
}

/* press_key: one frame with @key pressed, pointer parked far away */
static void
press_key (LrgCanvas *canvas,
           GrlKey     key)
{
	LrgCanvasInput input;

	frame_pointer (&input, -50.0f, -50.0f);
	lrg_canvas_input_add_key (&input, key);
	lrg_canvas_process_input (canvas, &input);
}

/* send_nav: one frame of Tab / gamepad navigation */
static void
send_nav (LrgCanvas       *canvas,
          LrgNavDirection  dir)
{
	LrgCanvasInput input;

	frame_pointer (&input, -50.0f, -50.0f);
	input.nav = dir;
	lrg_canvas_process_input (canvas, &input);
}

/* send_activate: one frame of gamepad A */
static void
send_activate (LrgCanvas *canvas)
{
	LrgCanvasInput input;

	frame_pointer (&input, -50.0f, -50.0f);
	input.activate = TRUE;
	lrg_canvas_process_input (canvas, &input);
}

/* wheel_at: one frame of wheel movement at (@x, @y) */
static void
wheel_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y,
          gfloat     wheel)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.wheel_y = wheel;
	lrg_canvas_process_input (canvas, &input);
}

/* settle: run @frames frames of animation */
static void
settle (LrgCanvas *canvas,
        guint      frames)
{
	guint i;

	for (i = 0; i < frames; i++)
		lrg_canvas_update (canvas, DT);
}

static void
count_cb (GObject  *object,
          gpointer  user_data)
{
	(*(gint *) user_data)++;
}

/* ==========================================================================
 * Combo box
 * ========================================================================== */

typedef struct
{
	LrgCanvas   *canvas;
	LrgComboBox *combo;
	gint         changed;
} ComboFixture;

static void
combo_set_up (ComboFixture  *f,
              gconstpointer  data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 800.0f, 600.0f);

	f->combo = lrg_combo_box_new ();
	lrg_combo_box_append (f->combo, "Alpha");
	lrg_combo_box_append (f->combo, "Beta");
	lrg_combo_box_append (f->combo, "Gamma");
	lrg_combo_box_set_placeholder (f->combo, "Pick one");
	lrg_widget_set_position (LRG_WIDGET (f->combo), 100.0f, 100.0f);
	lrg_widget_set_size (LRG_WIDGET (f->combo), 200.0f, 32.0f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->combo));
	g_signal_connect (f->combo, "changed", G_CALLBACK (count_cb), &f->changed);
	lrg_canvas_record (f->canvas);
}

static void
combo_tear_down (ComboFixture  *f,
                 gconstpointer  data)
{
	g_clear_object (&f->combo);
	g_clear_object (&f->canvas);
}

/* popup_row: row @i of the open combo popup */
static LrgWidget *
popup_row (LrgCanvas *canvas,
           guint      i)
{
	LrgWidget *popup = lrg_canvas_get_top_popup (canvas);

	g_assert_nonnull (popup);
	g_assert_true (LRG_IS_CONTAINER (popup));
	return lrg_container_get_child (LRG_CONTAINER (popup), i);
}

/*
 * The chevron flips on the canvas clock: with only the OPEN state
 * changing (no popup), lrg_canvas_update() reports TRUE while it turns
 * and FALSE once it rests, in both directions.
 */
static void
test_combo_chevron_frame_clock (ComboFixture  *f,
                                gconstpointer  data)
{
	guint frames = 0;

	/* First frame: a fresh box does not animate */
	g_assert_false (lrg_canvas_update (f->canvas, DT));

	lrg_widget_set_state_flags (LRG_WIDGET (f->combo), LRG_WIDGET_STATE_OPEN);
	g_assert_true (lrg_canvas_update (f->canvas, DT));
	while (lrg_canvas_update (f->canvas, DT))
		g_assert_cmpuint (++frames, <, 240);
	g_assert_false (lrg_canvas_update (f->canvas, DT));

	lrg_widget_unset_state_flags (LRG_WIDGET (f->combo), LRG_WIDGET_STATE_OPEN);
	g_assert_true (lrg_canvas_update (f->canvas, DT));
	frames = 0;
	while (lrg_canvas_update (f->canvas, DT))
		g_assert_cmpuint (++frames, <, 240);
	lrg_canvas_record (f->canvas);
}

static void
test_combo_defaults (void)
{
	g_autoptr(LrgComboBox) combo = lrg_combo_box_new ();
	gfloat                 w;
	gfloat                 h;

	g_assert_cmpint (lrg_combo_box_get_selected (combo), ==, -1);
	g_assert_cmpuint (lrg_combo_box_get_n_items (combo), ==, 0);
	g_assert_null (lrg_combo_box_get_selected_text (combo));
	g_assert_true (lrg_widget_get_focusable (LRG_WIDGET (combo)));

	lrg_combo_box_append (combo, "One");
	lrg_combo_box_append (combo, "A much longer item");
	lrg_combo_box_set_selected (combo, 5);
	g_assert_cmpint (lrg_combo_box_get_selected (combo), ==, -1);
	lrg_combo_box_set_selected (combo, 1);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (combo), ==, "A much longer item");
	g_assert_cmpstr (lrg_combo_box_get_item_text (combo, 0), ==, "One");
	g_assert_null (lrg_combo_box_get_item_text (combo, 2));

	/* Sized for the longest item and the control height */
	lrg_widget_get_preferred_size (LRG_WIDGET (combo), &w, &h);
	g_assert_cmpfloat (h, ==, lrg_theme_get_metric (lrg_theme_get_default (),
	                                                LRG_THEME_METRIC_CONTROL_HEIGHT));
	g_assert_cmpfloat (w, >, 40.0f);

	lrg_combo_box_remove_all (combo);
	g_assert_cmpuint (lrg_combo_box_get_n_items (combo), ==, 0);
	g_assert_cmpint (lrg_combo_box_get_selected (combo), ==, -1);
}

static void
test_combo_open_click (ComboFixture  *f,
                       gconstpointer  data)
{
	LrgWidget *combo = LRG_WIDGET (f->combo);
	LrgWidget *popup;
	LrgWidget *focused;

	click_widget (f->canvas, combo);

	/* The list is the top popup, below the combo, at least as wide */
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);
	g_assert_true (lrg_combo_box_get_popup_open (f->combo));
	popup = lrg_canvas_get_top_popup (f->canvas);
	g_assert_cmpfloat (lrg_widget_get_world_y (popup), >=, 100.0f + 32.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (popup), <=, 100.0f + 32.0f + 12.0f);
	g_assert_cmpfloat (lrg_widget_get_world_x (popup), ==, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_width (popup), >=, 200.0f);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (popup)), ==, 3);
	g_assert_true (lrg_widget_get_state_flags (combo) & LRG_WIDGET_STATE_OPEN);

	/* Rows are focusable; the first one has focus, without a ring */
	focused = lrg_canvas_get_focused_widget (f->canvas);
	g_assert_true (focused == popup_row (f->canvas, 0));
	g_assert_false (lrg_widget_get_state_flags (focused) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	/* Clicking the combo again closes it, and once more reopens it */
	click_widget (f->canvas, combo);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_false (lrg_widget_get_state_flags (combo) & LRG_WIDGET_STATE_OPEN);
	click_widget (f->canvas, combo);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);
	g_assert_cmpint (f->changed, ==, 0);
}

static void
test_combo_open_activate (ComboFixture  *f,
                          gconstpointer  data)
{
	LrgWidget *focused;

	lrg_combo_box_set_selected (f->combo, 2);
	f->changed = 0;
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->combo));
	send_activate (f->canvas);

	/* Opened by gamepad: the selected row is focused with a ring */
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);
	focused = lrg_canvas_get_focused_widget (f->canvas);
	g_assert_true (focused == popup_row (f->canvas, 2));
	g_assert_true (lrg_widget_get_state_flags (focused) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	/* Spatial navigation reaches the other rows */
	send_nav (f->canvas, LRG_NAV_DIRECTION_UP);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == popup_row (f->canvas, 1));

	/* Gamepad A picks it */
	send_activate (f->canvas);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 1);
	g_assert_cmpint (f->changed, ==, 1);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->combo));
}

static void
test_combo_select_click (ComboFixture  *f,
                         gconstpointer  data)
{
	click_widget (f->canvas, LRG_WIDGET (f->combo));
	lrg_canvas_record (f->canvas);
	click_widget (f->canvas, popup_row (f->canvas, 1));

	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 1);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (f->combo), ==, "Beta");
	g_assert_cmpint (f->changed, ==, 1);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_false (lrg_combo_box_get_popup_open (f->combo));
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (f->combo)) & LRG_WIDGET_STATE_OPEN);
}

static void
test_combo_select_keyboard (ComboFixture  *f,
                            gconstpointer  data)
{
	LrgWidget *row;

	lrg_combo_box_set_selected (f->combo, 0);
	f->changed = 0;
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->combo));
	press_key (f->canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);

	/* Down moves focus to the next row with the ring */
	press_key (f->canvas, GRL_KEY_DOWN);
	row = lrg_canvas_get_focused_widget (f->canvas);
	g_assert_true (row == popup_row (f->canvas, 1));
	g_assert_true (lrg_widget_get_state_flags (row) & LRG_WIDGET_STATE_FOCUS_VISIBLE);
	press_key (f->canvas, GRL_KEY_DOWN);
	press_key (f->canvas, GRL_KEY_DOWN);   /* clamps at the last row */
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == popup_row (f->canvas, 2));
	g_assert_cmpint (f->changed, ==, 0);

	/* Enter selects and closes; focus returns to the combo with its ring */
	press_key (f->canvas, GRL_KEY_ENTER);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 2);
	g_assert_cmpint (f->changed, ==, 1);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->combo));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (f->combo)) & LRG_WIDGET_STATE_FOCUS_VISIBLE);
}

static void
test_combo_escape (ComboFixture  *f,
                   gconstpointer  data)
{
	gint cancels = 0;

	g_signal_connect (f->canvas, "cancel", G_CALLBACK (count_cb), &cancels);
	lrg_combo_box_set_selected (f->combo, 1);
	f->changed = 0;
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->combo));
	press_key (f->canvas, GRL_KEY_ENTER);
	press_key (f->canvas, GRL_KEY_DOWN);
	press_key (f->canvas, GRL_KEY_ESCAPE);

	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 1);
	g_assert_cmpint (f->changed, ==, 0);
	g_assert_cmpint (cancels, ==, 0);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->combo));
}

static void
test_combo_outside_click (ComboFixture  *f,
                          gconstpointer  data)
{
	click_widget (f->canvas, LRG_WIDGET (f->combo));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);

	click_at (f->canvas, 650.0f, 500.0f);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, -1);
	g_assert_cmpint (f->changed, ==, 0);

	/* The closed list fades out as a ghost and is then released */
	settle (f->canvas, 60);
	g_assert_false (lrg_combo_box_get_popup_open (f->combo));
}

static void
test_combo_arrows_closed (ComboFixture  *f,
                          gconstpointer  data)
{
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->combo));

	press_key (f->canvas, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 0);
	press_key (f->canvas, GRL_KEY_DOWN);
	press_key (f->canvas, GRL_KEY_DOWN);
	press_key (f->canvas, GRL_KEY_DOWN);   /* stays on the last */
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 2);
	press_key (f->canvas, GRL_KEY_UP);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 1);
	g_assert_cmpint (f->changed, ==, 4);

	/* Arrows never opened it and never moved focus away */
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->combo));
}

static void
test_combo_long_list_scrolls (ComboFixture  *f,
                              gconstpointer  data)
{
	LrgTheme  *theme = lrg_theme_get_default ();
	gfloat     row_h = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
	LrgWidget *popup;
	LrgWidget *last;
	gfloat     y0;
	guint      i;

	for (i = 0; i < 17; i++)
	{
		g_autofree gchar *text = g_strdup_printf ("Item %u", i);

		lrg_combo_box_append (f->combo, text);
	}
	lrg_canvas_record (f->canvas);
	click_widget (f->canvas, LRG_WIDGET (f->combo));
	popup = lrg_canvas_get_top_popup (f->canvas);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (popup)), ==, 20);

	/* Capped near eight rows */
	g_assert_cmpfloat (lrg_widget_get_height (popup), <=, row_h * 8.0f + 16.0f);
	g_assert_cmpfloat (lrg_widget_get_height (popup), >=, row_h * 8.0f);

	/* The wheel scrolls the rows up */
	y0 = lrg_widget_get_world_y (popup_row (f->canvas, 0));
	wheel_at (f->canvas, lrg_widget_get_world_x (popup) + 20.0f, lrg_widget_get_world_y (popup) + 20.0f,
	          -3.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (popup_row (f->canvas, 0)), <, y0);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);

	/* End focuses the last row and scrolls it fully into view */
	press_key (f->canvas, GRL_KEY_END);
	last = popup_row (f->canvas, 19);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == last);
	g_assert_cmpfloat (lrg_widget_get_world_y (last), >=, lrg_widget_get_world_y (popup));
	g_assert_cmpfloat (lrg_widget_get_world_y (last) + lrg_widget_get_height (last), <=,
	                   lrg_widget_get_world_y (popup) + lrg_widget_get_height (popup));

	press_key (f->canvas, GRL_KEY_ENTER);
	g_assert_cmpint (lrg_combo_box_get_selected (f->combo), ==, 19);
}

static void
test_combo_paints (ComboFixture  *f,
                   gconstpointer  data)
{
	LrgPaintList *list;
	guint         closed;

	list = lrg_canvas_record (f->canvas);
	closed = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (closed, >, 0);

	/* Every preset: open list adds a surface and rows */
	click_widget (f->canvas, LRG_WIDGET (f->combo));
	list = lrg_canvas_record (f->canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, closed);
}

static void
test_combo_presets (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgCanvas)   canvas = lrg_canvas_new ();
		g_autoptr(LrgTheme)    theme = lrg_theme_new_preset (presets[i]);
		g_autoptr(LrgComboBox) combo = lrg_combo_box_new ();

		g_assert_nonnull (theme);
		lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
		lrg_widget_set_size (LRG_WIDGET (canvas), 640.0f, 480.0f);
		lrg_combo_box_append (combo, "One");
		lrg_combo_box_append (combo, "Two");
		lrg_widget_set_position (LRG_WIDGET (combo), 20.0f, 20.0f);
		lrg_widget_set_size (LRG_WIDGET (combo), 160.0f, 30.0f);
		lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (combo));
		lrg_combo_box_popup (combo);
		g_assert_true (lrg_combo_box_get_popup_open (combo));
		g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (canvas)), >, 0);
		lrg_combo_box_popdown (combo);
		g_assert_false (lrg_combo_box_get_popup_open (combo));
	}
}

/* ==========================================================================
 * Dialog
 * ========================================================================== */

typedef struct
{
	LrgCanvas *canvas;
	LrgButton *background;
	LrgDialog *dialog;
	LrgButton *cancel;
	LrgButton *ok;
	gint       background_clicks;
	gint       responses;
	gint       last_response;
} DialogFixture;

static void
on_response (LrgDialog *dialog,
             gint       response_id,
             gpointer   user_data)
{
	DialogFixture *f = user_data;

	f->responses++;
	f->last_response = response_id;
}

static void
dialog_set_up (DialogFixture *f,
               gconstpointer  data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 800.0f, 600.0f);

	f->background = lrg_button_new ("Behind");
	lrg_widget_set_position (LRG_WIDGET (f->background), 10.0f, 10.0f);
	lrg_widget_set_size (LRG_WIDGET (f->background), 100.0f, 30.0f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->background));
	g_signal_connect (f->background, "clicked", G_CALLBACK (count_cb), &f->background_clicks);

	f->dialog = lrg_dialog_new ("Save changes?", "Your level has unsaved edits.");
	f->cancel = lrg_dialog_add_button (f->dialog, "Cancel", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	f->ok = lrg_dialog_add_button (f->dialog, "Save", 1, TRUE);
	g_signal_connect (f->dialog, "response", G_CALLBACK (on_response), f);
	f->last_response = -100;
	lrg_canvas_record (f->canvas);
}

static void
dialog_tear_down (DialogFixture *f,
                  gconstpointer  data)
{
	g_clear_object (&f->dialog);
	g_clear_object (&f->background);
	g_clear_object (&f->canvas);
}

static void
test_dialog_layout (DialogFixture *f,
                    gconstpointer  data)
{
	LrgWidget *d = LRG_WIDGET (f->dialog);
	gfloat     w;
	gfloat     h;
	gfloat     short_h;

	g_assert_true (lrg_dialog_get_primary_button (f->dialog) == f->ok);
	g_assert_true (lrg_widget_has_class (LRG_WIDGET (f->ok), "primary"));
	g_assert_false (lrg_widget_has_class (LRG_WIDGET (f->cancel), "primary"));

	lrg_widget_get_preferred_size (d, &w, &short_h);
	g_assert_cmpfloat (w, <=, 520.0f);

	/* Long body text wraps: taller, not wider than the cap */
	lrg_dialog_set_body (f->dialog,
	                     "This is a much longer message that certainly needs more than one line "
	                     "of text once it is wrapped at the maximum width of the dialog body, "
	                     "so the dialog grows taller instead of wider.\nAnd a second paragraph.");
	lrg_widget_get_preferred_size (d, &w, &h);
	g_assert_cmpfloat (h, >, short_h);
	g_assert_cmpfloat (w, <=, 520.0f);

	/* Buttons right-aligned in order, primary last */
	lrg_dialog_present (f->dialog, f->canvas);
	lrg_canvas_record (f->canvas);
	g_assert_cmpfloat (lrg_widget_get_x (LRG_WIDGET (f->ok)), >, lrg_widget_get_x (LRG_WIDGET (f->cancel)));
	g_assert_cmpfloat (lrg_widget_get_x (LRG_WIDGET (f->ok)) + lrg_widget_get_width (LRG_WIDGET (f->ok)), >,
	                   lrg_widget_get_width (d) - 40.0f);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (f->ok)) + lrg_widget_get_height (LRG_WIDGET (f->ok)), <,
	                   lrg_widget_get_height (d));

	/* Centred on the canvas */
	g_assert_cmpfloat (fabsf (lrg_widget_get_world_x (d) + lrg_widget_get_width (d) * 0.5f - 400.0f), <=, 1.0f);
}

static void
test_dialog_present_modal (DialogFixture *f,
                           gconstpointer  data)
{
	lrg_dialog_present (f->dialog, f->canvas);

	g_assert_true (lrg_dialog_get_presented (f->dialog));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 1);
	g_assert_true (lrg_canvas_get_top_popup (f->canvas) == LRG_WIDGET (f->dialog));
	g_assert_cmpint (lrg_widget_get_appear (LRG_WIDGET (f->dialog)), ==, LRG_APPEAR_FLY_UP);

	/* Initial focus on the primary button */
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->ok));

	/* The veil swallows clicks outside */
	settle (f->canvas, 30);
	click_widget (f->canvas, LRG_WIDGET (f->background));
	g_assert_cmpint (f->background_clicks, ==, 0);
	g_assert_true (lrg_dialog_get_presented (f->dialog));
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->ok));
	g_assert_cmpint (f->responses, ==, 0);
}

static void
test_dialog_tab_stays_inside (DialogFixture *f,
                              gconstpointer  data)
{
	guint i;

	lrg_dialog_present (f->dialog, f->canvas);
	lrg_canvas_record (f->canvas);

	for (i = 0; i < 5; i++)
	{
		LrgWidget *focused;

		send_nav (f->canvas, LRG_NAV_DIRECTION_NEXT);
		focused = lrg_canvas_get_focused_widget (f->canvas);
		g_assert_true (focused == LRG_WIDGET (f->ok) || focused == LRG_WIDGET (f->cancel));
		g_assert_true (lrg_widget_get_state_flags (focused) & LRG_WIDGET_STATE_FOCUS_VISIBLE);
	}
	g_assert_cmpint (f->responses, ==, 0);
}

static void
test_dialog_button_click (DialogFixture *f,
                          gconstpointer  data)
{
	lrg_widget_set_focusable (LRG_WIDGET (f->background), TRUE);
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->background));
	lrg_dialog_present (f->dialog, f->canvas);
	lrg_canvas_record (f->canvas);
	settle (f->canvas, 30);

	click_widget (f->canvas, LRG_WIDGET (f->ok));
	g_assert_cmpint (f->responses, ==, 1);
	g_assert_cmpint (f->last_response, ==, 1);
	g_assert_false (lrg_dialog_get_presented (f->dialog));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);

	/* Focus goes back to where it was */
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->background));

	/* The ghost fades and lets go of the dialog; it can be shown again */
	settle (f->canvas, 90);
	g_assert_null (lrg_widget_get_parent (LRG_WIDGET (f->dialog)));
	lrg_dialog_present (f->dialog, f->canvas);
	g_assert_true (lrg_dialog_get_presented (f->dialog));
	click_widget (f->canvas, LRG_WIDGET (f->cancel));
	g_assert_cmpint (f->last_response, ==, LRG_DIALOG_RESPONSE_CANCEL);
	g_assert_cmpint (f->responses, ==, 2);
}

static void
test_dialog_escape_cancels (DialogFixture *f,
                            gconstpointer  data)
{
	gint canvas_cancels = 0;

	g_signal_connect (f->canvas, "cancel", G_CALLBACK (count_cb), &canvas_cancels);
	lrg_dialog_present (f->dialog, f->canvas);
	press_key (f->canvas, GRL_KEY_ESCAPE);

	g_assert_cmpint (f->responses, ==, 1);
	g_assert_cmpint (f->last_response, ==, LRG_DIALOG_RESPONSE_CANCEL);
	g_assert_false (lrg_dialog_get_presented (f->dialog));
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	g_assert_cmpint (canvas_cancels, ==, 0);
}

static void
test_dialog_enter_triggers_primary (DialogFixture *f,
                                    gconstpointer  data)
{
	lrg_dialog_present (f->dialog, f->canvas);
	lrg_canvas_record (f->canvas);

	/* Click the sheet background: focus leaves the buttons */
	click_at (f->canvas, lrg_widget_get_world_x (LRG_WIDGET (f->dialog)) + 8.0f,
	          lrg_widget_get_world_y (LRG_WIDGET (f->dialog)) + 8.0f);
	g_assert_null (lrg_canvas_get_focused_widget (f->canvas));
	g_assert_cmpint (f->responses, ==, 0);

	press_key (f->canvas, GRL_KEY_ENTER);
	g_assert_cmpint (f->responses, ==, 1);
	g_assert_cmpint (f->last_response, ==, 1);
}

static void
test_dialog_activate_focused_button (DialogFixture *f,
                                     gconstpointer  data)
{
	lrg_dialog_present (f->dialog, f->canvas);
	lrg_canvas_record (f->canvas);
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->cancel));

	/* Gamepad A on the cancel button answers with ITS id */
	send_activate (f->canvas);
	g_assert_cmpint (f->responses, ==, 1);
	g_assert_cmpint (f->last_response, ==, LRG_DIALOG_RESPONSE_CANCEL);
}

static void
test_dialog_content (DialogFixture *f,
                     gconstpointer  data)
{
	g_autoptr(LrgCheckbox) check = lrg_checkbox_new ("Don't ask again");
	LrgWidget             *content = LRG_WIDGET (check);
	GList                 *children;

	lrg_dialog_set_content (f->dialog, content);
	g_assert_true (lrg_dialog_get_content (f->dialog) == content);

	/* Content first in Tab order */
	children = lrg_container_get_children (LRG_CONTAINER (f->dialog));
	g_assert_true (children->data == content);

	lrg_dialog_present (f->dialog, f->canvas);
	lrg_canvas_record (f->canvas);
	g_assert_cmpfloat (lrg_widget_get_y (content) + lrg_widget_get_height (content), <=,
	                   lrg_widget_get_y (LRG_WIDGET (f->ok)));
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas)), >, 0);

	lrg_dialog_close (f->dialog);
	g_assert_cmpint (f->responses, ==, 0);
	lrg_dialog_set_content (f->dialog, NULL);
	g_assert_null (lrg_widget_get_parent (content));
}

/* ==========================================================================
 * Toasts
 * ========================================================================== */

typedef struct
{
	LrgCanvas     *canvas;
	LrgButton     *background;
	LrgToastQueue *queue;
	gint           background_clicks;
} ToastFixture;

static void
toast_set_up (ToastFixture  *f,
              gconstpointer  data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 800.0f, 600.0f);

	/* Something under the queue, in the bottom-right corner area */
	f->background = lrg_button_new ("Under");
	lrg_widget_set_position (LRG_WIDGET (f->background), 300.0f, 300.0f);
	lrg_widget_set_size (LRG_WIDGET (f->background), 100.0f, 30.0f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->background));
	g_signal_connect (f->background, "clicked", G_CALLBACK (count_cb), &f->background_clicks);

	f->queue = lrg_toast_queue_new ();
	lrg_toast_queue_set_auto_advance (f->queue, FALSE);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->queue));
}

static void
toast_tear_down (ToastFixture  *f,
                 gconstpointer  data)
{
	g_clear_object (&f->queue);
	g_clear_object (&f->background);
	g_clear_object (&f->canvas);
}

/* run: advance the queue by @seconds in frame-sized steps */
static void
run (LrgToastQueue *queue,
     gdouble        seconds)
{
	gdouble t;

	for (t = 0.0; t < seconds - 1e-6; t += DT)
		lrg_toast_queue_advance (queue, DT);
}

/* show_new: show a fresh toast lasting @duration seconds */
static LrgToast *
show_new (LrgToastQueue *queue,
          const gchar   *title,
          gdouble        duration)
{
	g_autoptr(LrgToast) toast = lrg_toast_new (title, "Message text");

	lrg_toast_set_duration (toast, duration);
	lrg_toast_queue_show (queue, toast);
	return toast;   /* the queue holds a reference */
}

static void
test_toast_basics (void)
{
	g_autoptr(LrgToast) toast = lrg_toast_new ("Saved", "All good");
	gint                dismissed = 0;
	gfloat              w;
	gfloat              h;

	g_assert_cmpfloat (lrg_toast_get_duration (toast), ==, LRG_TOAST_DEFAULT_DURATION);
	g_assert_cmpint (lrg_toast_get_kind (toast), ==, LRG_TOAST_KIND_INFO);
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_TOAST_KIND));
	lrg_toast_set_kind (toast, LRG_TOAST_KIND_DANGER);
	g_object_set (toast, "kind", LRG_TOAST_KIND_SUCCESS, NULL);
	g_assert_cmpint (lrg_toast_get_kind (toast), ==, LRG_TOAST_KIND_SUCCESS);

	g_signal_connect (toast, "dismissed", G_CALLBACK (count_cb), &dismissed);
	lrg_toast_set_duration (toast, 2.0);
	g_assert_false (lrg_toast_advance (toast, 1.0));
	g_assert_cmpfloat (lrg_toast_get_remaining_fraction (toast), ==, 0.5f);
	g_assert_true (lrg_toast_advance (toast, 1.5));
	g_assert_true (lrg_toast_get_dismissed (toast));
	lrg_toast_dismiss (toast);
	g_assert_cmpint (dismissed, ==, 1);

	lrg_widget_get_preferred_size (LRG_WIDGET (toast), &w, &h);
	g_assert_cmpfloat (w, ==, 340.0f);
	g_assert_cmpfloat (h, >, 20.0f);
}

static void
test_toast_stack_mode (ToastFixture  *f,
                       gconstpointer  data)
{
	g_assert_cmpint (lrg_toast_queue_get_mode (f->queue), ==, LRG_TOAST_MODE_STACK);
	lrg_toast_queue_set_max_visible (f->queue, 2);

	show_new (f->queue, "A", 1.0);
	show_new (f->queue, "B", 2.0);
	show_new (f->queue, "C", 1.0);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 2);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 1);

	/* The queue covers the canvas */
	g_assert_cmpfloat (lrg_widget_get_width (LRG_WIDGET (f->queue)), ==, 800.0f);

	/* A expires: C takes its place at once */
	run (f->queue, 1.1);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 2);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 0);
	g_assert_cmpstr (lrg_toast_get_title (lrg_toast_queue_get_visible_toast (f->queue, 1)), ==, "C");

	/* Everything expires and fades away */
	run (f->queue, 3.0);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 0);
}

static void
test_toast_sequence_mode (ToastFixture  *f,
                          gconstpointer  data)
{
	lrg_toast_queue_set_mode (f->queue, LRG_TOAST_MODE_SEQUENCE);

	show_new (f->queue, "First", 1.0);
	show_new (f->queue, "Second", 1.0);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 1);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 1);

	/* First expired and is fading: Second still waits */
	run (f->queue, 1.05);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 0);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 1);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 1);

	/* Once it has left, Second comes in */
	run (f->queue, 0.9);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 1);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 0);
	g_assert_cmpstr (lrg_toast_get_title (lrg_toast_queue_get_visible_toast (f->queue, 0)), ==, "Second");
}

static void
test_toast_replace_mode (ToastFixture  *f,
                         gconstpointer  data)
{
	LrgToast *a;
	LrgToast *b;

	lrg_toast_queue_set_mode (f->queue, LRG_TOAST_MODE_REPLACE);
	a = show_new (f->queue, "A", 5.0);
	run (f->queue, 0.5);
	b = show_new (f->queue, "B", 5.0);

	/* A is dismissed and cross-fades out where B comes in */
	g_assert_true (lrg_toast_get_dismissed (a));
	g_assert_false (lrg_toast_get_dismissed (b));
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 1);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 2);

	run (f->queue, 0.05);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (a)) + lrg_widget_get_height (LRG_WIDGET (a)), ==,
	                   lrg_widget_get_y (LRG_WIDGET (b)) + lrg_widget_get_height (LRG_WIDGET (b)));
	run (f->queue, 1.0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 1);
	g_assert_true (lrg_toast_queue_get_visible_toast (f->queue, 0) == b);
}

static void
test_toast_click_dismiss (ToastFixture  *f,
                          gconstpointer  data)
{
	LrgToast *toast;
	gint      dismissed = 0;

	toast = show_new (f->queue, "Click me", 0.0);
	g_signal_connect (toast, "dismissed", G_CALLBACK (count_cb), &dismissed);
	run (f->queue, 0.5);
	lrg_canvas_record (f->canvas);

	/* Bottom-right corner */
	g_assert_cmpfloat (lrg_widget_get_world_x (LRG_WIDGET (toast)) + lrg_widget_get_width (LRG_WIDGET (toast)),
	                   ==, 800.0f - 16.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (LRG_WIDGET (toast)) + lrg_widget_get_height (LRG_WIDGET (toast)),
	                   ==, 600.0f - 16.0f);

	/* Duration 0: it stays */
	run (f->queue, 10.0);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 1);

	/* Empty areas of the queue let clicks through */
	click_widget (f->canvas, LRG_WIDGET (f->background));
	g_assert_cmpint (f->background_clicks, ==, 1);

	click_widget (f->canvas, LRG_WIDGET (toast));
	g_assert_cmpint (dismissed, ==, 1);
	g_assert_true (lrg_toast_get_dismissed (toast));
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 0);
	run (f->queue, 1.0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 0);
}

static void
test_toast_reflow (ToastFixture  *f,
                   gconstpointer  data)
{
	LrgToast *older;
	LrgToast *newer;
	gfloat    start;
	gfloat    mid;
	gfloat    bottom;

	older = show_new (f->queue, "Older", 0.0);
	newer = show_new (f->queue, "Newer", 0.0);
	run (f->queue, 0.6);

	/* Newest nearest the bottom edge, the older one stacked above */
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (newer)), >, lrg_widget_get_y (LRG_WIDGET (older)));
	bottom = 600.0f - 16.0f - lrg_widget_get_height (LRG_WIDGET (older));
	start = lrg_widget_get_y (LRG_WIDGET (older));
	g_assert_cmpfloat (start, <, bottom);

	/* The newer one leaves: it fades in place, then the older glides down */
	lrg_toast_dismiss (newer);
	run (f->queue, 0.05);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (older)), ==, start);

	while (lrg_container_get_child_count (LRG_CONTAINER (f->queue)) > 1)
		lrg_toast_queue_advance (f->queue, DT);
	lrg_toast_queue_advance (f->queue, DT);
	lrg_toast_queue_advance (f->queue, DT);
	mid = lrg_widget_get_y (LRG_WIDGET (older));
	g_assert_cmpfloat (mid, >, start);
	g_assert_cmpfloat (mid, <, bottom);

	run (f->queue, 1.5);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (older)), ==, bottom);
}

/*
 * Auto-advance runs on the canvas clock: lrg_canvas_update() advances
 * the toast and reports TRUE while it enters, counts down and leaves,
 * then FALSE once it is gone. An explicit advance adds to the clock.
 */
static void
test_toast_frame_clock (ToastFixture  *f,
                        gconstpointer  data)
{
	g_autoptr(LrgToast) toast = NULL;
	LrgToast           *sticky;
	guint               i;
	guint               frames = 0;

	/* Auto-advance off: frames do not move time and nothing is busy.
	 * Keep our own reference: the queue drops the toast once gone. */
	toast = g_object_ref (show_new (f->queue, "Timed", 1.0));
	for (i = 0; i < 30; i++)
		g_assert_false (lrg_canvas_update (f->canvas, DT));
	g_assert_cmpfloat (lrg_toast_get_elapsed (toast), ==, 0.0);

	/* On: half a second of frames is half a second on the toast */
	lrg_toast_queue_set_auto_advance (f->queue, TRUE);
	for (i = 0; i < 30; i++)
		g_assert_true (lrg_canvas_update (f->canvas, DT));
	g_assert_cmpfloat_with_epsilon (lrg_toast_get_elapsed (toast), 0.5, 1e-4);

	/* An explicit advance skips ahead on top of the frame clock */
	lrg_toast_queue_advance (f->queue, 0.25);
	g_assert_cmpfloat_with_epsilon (lrg_toast_get_elapsed (toast), 0.75, 1e-4);

	/* It expires, fades out and leaves; then the canvas is idle */
	while (lrg_canvas_update (f->canvas, DT))
		g_assert_cmpuint (++frames, <, 600);
	g_assert_true (lrg_toast_get_dismissed (toast));
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 0);

	/* A toast without a duration is busy only while it enters */
	sticky = show_new (f->queue, "Sticky", 0.0);
	g_assert_true (lrg_canvas_update (f->canvas, DT));
	frames = 0;
	while (lrg_canvas_update (f->canvas, DT))
		g_assert_cmpuint (++frames, <, 600);
	g_assert_false (lrg_canvas_update (f->canvas, DT));
	g_assert_false (lrg_toast_get_dismissed (sticky));
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 1);
}

static void
test_toast_top_left_and_clear (ToastFixture  *f,
                               gconstpointer  data)
{
	LrgToast *a;

	lrg_toast_queue_set_toast_halign (f->queue, LRG_ALIGN_START);
	lrg_toast_queue_set_toast_valign (f->queue, LRG_ALIGN_START);
	lrg_toast_queue_set_max_visible (f->queue, 1);
	a = show_new (f->queue, "A", 0.0);
	show_new (f->queue, "B", 0.0);
	run (f->queue, 0.6);
	g_assert_cmpfloat (lrg_widget_get_x (LRG_WIDGET (a)), ==, 16.0f);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (a)), ==, 16.0f);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 1);

	lrg_toast_queue_clear (f->queue);
	g_assert_cmpuint (lrg_toast_queue_get_n_visible (f->queue), ==, 0);
	g_assert_cmpuint (lrg_toast_queue_get_n_pending (f->queue), ==, 0);
	run (f->queue, 1.0);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (f->queue)), ==, 0);
}

static void
test_toast_paints (ToastFixture  *f,
                   gconstpointer  data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	LrgToast            *t;
	guint                i;

	t = show_new (f->queue, "Painted", 3.0);
	lrg_toast_set_kind (t, LRG_TOAST_KIND_WARNING);
	run (f->queue, 0.5);
	for (i = 0; presets[i] != NULL; i++)
	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (presets[i]);

		lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas)), >, 3);
	}
}

/* ==========================================================================
 * GPU: the modal veil darkens what is behind a dialog
 * ========================================================================== */

#define GPU_W (240)
#define GPU_H (180)

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
	test_window = grl_window_new (8, 8, "lrg-ui-popups-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* render_canvas: record @canvas and render it into an image */
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

static void
test_gpu_modal_veil (void)
{
	g_autoptr(LrgCanvas) canvas = NULL;
	g_autoptr(LrgDialog) dialog = NULL;
	Image                before;
	Image                after;
	Color                a;
	Color                b;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (canvas), (gfloat) GPU_W, (gfloat) GPU_H);
	lrg_canvas_set_paint_background (canvas, TRUE);

	before = render_canvas (canvas);

	dialog = lrg_dialog_new ("Quit?", NULL);
	lrg_dialog_add_button (dialog, "OK", LRG_DIALOG_RESPONSE_CLOSE, TRUE);
	lrg_dialog_present (dialog, canvas);
	settle (canvas, 120);
	after = render_canvas (canvas);

	/* A corner far from the sheet got darker behind the veil */
	a = GetImageColor (before, 2, 2);
	b = GetImageColor (after, 2, 2);
	g_assert_cmpint ((gint) b.r + b.g + b.b, <, (gint) a.r + a.g + a.b);

	/* The centre shows the (opaque) sheet, not the veiled window */
	b = GetImageColor (after, GPU_W / 2, GPU_H / 2);
	g_assert_cmpuint (b.a, ==, 255);

	UnloadImage (before);
	UnloadImage (after);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

#define COMBO_TEST(path, func) \
	g_test_add ("/ui/popups/combo/" path, ComboFixture, NULL, combo_set_up, func, combo_tear_down)
#define DIALOG_TEST(path, func) \
	g_test_add ("/ui/popups/dialog/" path, DialogFixture, NULL, dialog_set_up, func, dialog_tear_down)
#define TOAST_TEST(path, func) \
	g_test_add ("/ui/popups/toast/" path, ToastFixture, NULL, toast_set_up, func, toast_tear_down)

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/popups/combo/defaults", test_combo_defaults);
	COMBO_TEST ("open-click", test_combo_open_click);
	COMBO_TEST ("open-activate", test_combo_open_activate);
	COMBO_TEST ("select-click", test_combo_select_click);
	COMBO_TEST ("select-keyboard", test_combo_select_keyboard);
	COMBO_TEST ("escape", test_combo_escape);
	COMBO_TEST ("outside-click", test_combo_outside_click);
	COMBO_TEST ("arrows-closed", test_combo_arrows_closed);
	COMBO_TEST ("long-list-scrolls", test_combo_long_list_scrolls);
	COMBO_TEST ("paints", test_combo_paints);
	COMBO_TEST ("chevron-frame-clock", test_combo_chevron_frame_clock);
	g_test_add_func ("/ui/popups/combo/presets", test_combo_presets);

	DIALOG_TEST ("layout", test_dialog_layout);
	DIALOG_TEST ("present-modal", test_dialog_present_modal);
	DIALOG_TEST ("tab-stays-inside", test_dialog_tab_stays_inside);
	DIALOG_TEST ("button-click", test_dialog_button_click);
	DIALOG_TEST ("escape-cancels", test_dialog_escape_cancels);
	DIALOG_TEST ("enter-triggers-primary", test_dialog_enter_triggers_primary);
	DIALOG_TEST ("activate-focused-button", test_dialog_activate_focused_button);
	DIALOG_TEST ("content", test_dialog_content);

	g_test_add_func ("/ui/popups/toast/basics", test_toast_basics);
	TOAST_TEST ("stack-mode", test_toast_stack_mode);
	TOAST_TEST ("sequence-mode", test_toast_sequence_mode);
	TOAST_TEST ("replace-mode", test_toast_replace_mode);
	TOAST_TEST ("click-dismiss", test_toast_click_dismiss);
	TOAST_TEST ("reflow", test_toast_reflow);
	TOAST_TEST ("top-left-and-clear", test_toast_top_left_and_clear);
	TOAST_TEST ("frame-clock", test_toast_frame_clock);
	TOAST_TEST ("paints", test_toast_paints);

	graphics_available = init_graphics_context ();
	g_test_add_func ("/ui/popups/gpu/modal-veil", test_gpu_modal_veil);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
