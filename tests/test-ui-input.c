/* test-ui-input.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Canvas-driven input tests. Every case feeds LrgCanvasInput frames
 * through lrg_canvas_process_input(), exactly as the real input loop
 * does, rather than hand-building events for one widget. That is what
 * catches routing bugs like button/x/y argument mix-ups.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

typedef struct
{
    LrgCanvas *canvas;
} CanvasFixture;

static void
canvas_fixture_set_up (CanvasFixture *fixture,
                       gconstpointer  user_data)
{
    fixture->canvas = lrg_canvas_new ();
    lrg_widget_set_size (LRG_WIDGET (fixture->canvas), 800.0f, 600.0f);
}

static void
canvas_fixture_tear_down (CanvasFixture *fixture,
                          gconstpointer  user_data)
{
    g_clear_object (&fixture->canvas);
}

/* frame_pointer:
 * Builds a frame with the pointer at (@x, @y) and nothing else. */
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

/* click_at:
 * Feeds a full left click (press frame, release frame) at (@x, @y). */
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

static void
on_clicked_count (LrgButton *button,
                  gpointer   user_data)
{
    gint *count = user_data;

    (*count)++;
}

/* ==========================================================================
 * Input frame
 * ========================================================================== */

static void
test_input_frame_init (void)
{
    LrgCanvasInput input;

    lrg_canvas_input_init (&input);
    g_assert_cmpuint (input.n_keys, ==, 0);
    g_assert_cmpuint (input.n_chars, ==, 0);
    g_assert_cmpuint (input.buttons_pressed, ==, 0);
    g_assert_cmpint (input.nav, ==, LRG_NAV_DIRECTION_NONE);
    g_assert_false (input.activate);
}

static void
test_input_frame_add_text (void)
{
    LrgCanvasInput input;

    lrg_canvas_input_init (&input);
    lrg_canvas_input_add_text (&input, "aÉ€");
    g_assert_cmpuint (input.n_chars, ==, 3);
    g_assert_cmpuint (input.chars[0], ==, 'a');
    g_assert_cmpuint (input.chars[1], ==, 0x00C9);
    g_assert_cmpuint (input.chars[2], ==, 0x20AC);
}

static void
test_input_frame_overflow (void)
{
    LrgCanvasInput input;
    guint          i;

    lrg_canvas_input_init (&input);
    for (i = 0; i < LRG_CANVAS_INPUT_MAX_KEYS + 5; i++)
    {
        lrg_canvas_input_add_key (&input, GRL_KEY_A);
        lrg_canvas_input_add_char (&input, 'a');
    }
    g_assert_cmpuint (input.n_keys, ==, LRG_CANVAS_INPUT_MAX_KEYS);
    g_assert_cmpuint (input.n_chars, ==, LRG_CANVAS_INPUT_MAX_KEYS);
}

static void
test_input_frame_boxed (void)
{
    g_autoptr(LrgCanvasInput) a = lrg_canvas_input_new ();
    g_autoptr(LrgCanvasInput) b = NULL;

    a->pointer_x = 12.0f;
    lrg_canvas_input_add_char (a, 'z');
    b = lrg_canvas_input_copy (a);
    g_assert_cmpfloat (b->pointer_x, ==, 12.0f);
    g_assert_cmpuint (b->chars[0], ==, 'z');
    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_CANVAS_INPUT));
}

/* ==========================================================================
 * Regression: canvas click routing (button/x/y argument order)
 * ========================================================================== */

static void
test_canvas_click_reaches_button (CanvasFixture *fixture,
                                  gconstpointer  user_data)
{
    LrgButton *button;
    gint       clicks = 0;

    button = lrg_button_new ("Play");
    lrg_widget_set_position (LRG_WIDGET (button), 300.0f, 200.0f);
    lrg_widget_set_size (LRG_WIDGET (button), 120.0f, 40.0f);
    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (button));
    g_signal_connect (button, "clicked", G_CALLBACK (on_clicked_count), &clicks);

    /* Away from x = 0 on purpose: with x and button swapped, a click
     * only "worked" when the pointer x truncated to 0 */
    click_at (fixture->canvas, 350.0f, 215.0f);
    g_assert_cmpint (clicks, ==, 1);

    /* A click outside does nothing */
    click_at (fixture->canvas, 50.0f, 50.0f);
    g_assert_cmpint (clicks, ==, 1);

    g_object_unref (button);
}

static void
test_canvas_right_click_is_not_left (CanvasFixture *fixture,
                                     gconstpointer  user_data)
{
    LrgButton      *button;
    LrgCanvasInput  input;
    gint            clicks = 0;

    button = lrg_button_new ("Menu");
    lrg_widget_set_position (LRG_WIDGET (button), 10.0f, 10.0f);
    lrg_widget_set_size (LRG_WIDGET (button), 100.0f, 30.0f);
    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (button));
    g_signal_connect (button, "clicked", G_CALLBACK (on_clicked_count), &clicks);

    frame_pointer (&input, 40.0f, 20.0f);
    input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT);
    lrg_canvas_process_input (fixture->canvas, &input);
    frame_pointer (&input, 40.0f, 20.0f);
    input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT);
    lrg_canvas_process_input (fixture->canvas, &input);

    g_assert_cmpint (clicks, ==, 0);
    g_object_unref (button);
}

/* ==========================================================================
 * Regression: text input receives composed characters
 * ========================================================================== */

static void
test_canvas_text_input_utf8 (CanvasFixture *fixture,
                             gconstpointer  user_data)
{
    LrgTextInput   *entry;
    LrgCanvasInput  input;

    entry = lrg_text_input_new ();
    lrg_widget_set_position (LRG_WIDGET (entry), 100.0f, 100.0f);
    lrg_widget_set_size (LRG_WIDGET (entry), 200.0f, 32.0f);
    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (entry));

    /* Click to focus */
    click_at (fixture->canvas, 150.0f, 110.0f);
    g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (entry));

    /* Lowercase, uppercase and non-ASCII all arrive intact. The raw
     * key presses that accompany them must not insert anything. */
    frame_pointer (&input, 150.0f, 110.0f);
    lrg_canvas_input_add_key (&input, GRL_KEY_H);
    lrg_canvas_input_add_key (&input, GRL_KEY_I);
    lrg_canvas_input_add_text (&input, "hI ü€");
    lrg_canvas_process_input (fixture->canvas, &input);

    g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "hI ü€");

    g_object_unref (entry);
}

static void
test_canvas_text_input_ignores_control_chars (CanvasFixture *fixture,
                                              gconstpointer  user_data)
{
    LrgTextInput   *entry;
    LrgCanvasInput  input;

    entry = lrg_text_input_new ();
    lrg_widget_set_size (LRG_WIDGET (entry), 200.0f, 32.0f);
    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (entry));
    click_at (fixture->canvas, 10.0f, 10.0f);

    frame_pointer (&input, 10.0f, 10.0f);
    lrg_canvas_input_add_char (&input, 0x07);
    lrg_canvas_input_add_char (&input, 'k');
    lrg_canvas_process_input (fixture->canvas, &input);

    g_assert_cmpstr (lrg_text_input_get_text (entry), ==, "k");
    g_object_unref (entry);
}

static void
test_canvas_text_without_focus (CanvasFixture *fixture,
                                gconstpointer  user_data)
{
    LrgTextInput   *entry;
    LrgCanvasInput  input;
    const gchar    *text;

    entry = lrg_text_input_new ();
    lrg_widget_set_size (LRG_WIDGET (entry), 200.0f, 32.0f);
    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (entry));

    /* No click: nothing is focused, typing goes nowhere */
    frame_pointer (&input, 10.0f, 10.0f);
    lrg_canvas_input_add_text (&input, "nope");
    lrg_canvas_process_input (fixture->canvas, &input);

    text = lrg_text_input_get_text (entry);
    g_assert_true (text == NULL || text[0] == '\0');
    g_object_unref (entry);
}


/* ==========================================================================
 * A recording widget
 * ========================================================================== */

#define TEST_TYPE_PROBE (test_probe_get_type ())
G_DECLARE_FINAL_TYPE (TestProbe, test_probe, TEST, PROBE, LrgContainer)

struct _TestProbe
{
	LrgContainer parent_instance;
	GString     *log;       /* shared, not owned */
	gchar       *tag;
	gboolean     consume;   /* return TRUE from handle_event */
};

G_DEFINE_FINAL_TYPE (TestProbe, test_probe, LRG_TYPE_CONTAINER)

static const gchar *
event_name (LrgUIEventType t)
{
	switch (t)
	{
	case LRG_UI_EVENT_MOUSE_MOVE: return "move";
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN: return "down";
	case LRG_UI_EVENT_MOUSE_BUTTON_UP: return "up";
	case LRG_UI_EVENT_KEY_DOWN: return "key";
	case LRG_UI_EVENT_FOCUS_IN: return "focus-in";
	case LRG_UI_EVENT_FOCUS_OUT: return "focus-out";
	case LRG_UI_EVENT_POINTER_ENTER: return "enter";
	case LRG_UI_EVENT_POINTER_LEAVE: return "leave";
	case LRG_UI_EVENT_ACTIVATE: return "activate";
	case LRG_UI_EVENT_CANCEL: return "cancel";
	case LRG_UI_EVENT_SCROLL: return "scroll";
	default: return "other";
	}
}

static gboolean
test_probe_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
	TestProbe *self = TEST_PROBE (widget);

	if (self->log != NULL)
		g_string_append_printf (self->log, "%s:%s%s ", self->tag,
		                        event_name (lrg_ui_event_get_event_type (event)),
		                        lrg_ui_event_get_bubbling (event) ? "^" : "");
	return self->consume;
}

static void
test_probe_finalize (GObject *object)
{
	g_free (TEST_PROBE (object)->tag);
	G_OBJECT_CLASS (test_probe_parent_class)->finalize (object);
}

static void
test_probe_class_init (TestProbeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = test_probe_finalize;
	LRG_WIDGET_CLASS (klass)->handle_event = test_probe_handle_event;
}

static void
test_probe_init (TestProbe *self)
{
}

static TestProbe *
probe_new (const gchar *tag,
           GString     *log,
           gfloat       x,
           gfloat       y,
           gfloat       w,
           gfloat       h,
           gboolean     focusable)
{
	TestProbe *p = g_object_new (TEST_TYPE_PROBE, NULL);

	p->tag = g_strdup (tag);
	p->log = log;
	lrg_widget_set_position (LRG_WIDGET (p), x, y);
	lrg_widget_set_size (LRG_WIDGET (p), w, h);
	lrg_widget_set_focusable (LRG_WIDGET (p), focusable);
	return p;
}

/* frame_key: one frame with a single key press */
static void
frame_key (LrgCanvas *canvas,
           GrlKey     key)
{
	LrgCanvasInput input;

	frame_pointer (&input, -50.0f, -50.0f);
	lrg_canvas_input_add_key (&input, key);
	lrg_canvas_process_input (canvas, &input);
}

/* frame_nav: one frame with a navigation request */
static void
frame_nav (LrgCanvas       *canvas,
           LrgNavDirection  nav)
{
	LrgCanvasInput input;

	frame_pointer (&input, -50.0f, -50.0f);
	input.nav = nav;
	lrg_canvas_process_input (canvas, &input);
}

static gboolean
log_has (GString     *log,
         const gchar *needle)
{
	return strstr (log->str, needle) != NULL;
}

/* ==========================================================================
 * Bubbling, capture, enter/leave
 * ========================================================================== */

static void
test_bubbling (CanvasFixture *fixture,
               gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *outer = probe_new ("outer", log, 100, 100, 200, 200, FALSE);
	TestProbe         *inner = probe_new ("inner", log, 10, 10, 50, 50, FALSE);

	lrg_container_add_child (LRG_CONTAINER (outer), LRG_WIDGET (inner));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (outer));

	/* Unhandled by inner: bubbles to outer (marked ^) */
	click_at (fixture->canvas, 120.0f, 120.0f);
	g_assert_true (log_has (log, "inner:down "));
	g_assert_true (log_has (log, "outer:down^ "));

	/* Handled by inner: outer never sees it */
	g_string_truncate (log, 0);
	inner->consume = TRUE;
	click_at (fixture->canvas, 120.0f, 120.0f);
	g_assert_true (log_has (log, "inner:down "));
	g_assert_false (log_has (log, "outer:down"));

	g_object_unref (inner);
	g_object_unref (outer);
}

static void
test_pointer_capture (CanvasFixture *fixture,
                      gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *a = probe_new ("a", log, 0, 0, 100, 100, FALSE);
	TestProbe         *b = probe_new ("b", log, 200, 0, 100, 100, FALSE);
	LrgCanvasInput     input;

	a->consume = TRUE;
	b->consume = TRUE;
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (a));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (b));

	/* Press on a, drag over b, release over b: a gets move and up */
	frame_pointer (&input, 50, 50);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (a)) & LRG_WIDGET_STATE_PRESSED);

	g_string_truncate (log, 0);
	frame_pointer (&input, 250, 50);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (log_has (log, "a:move"));
	g_assert_false (log_has (log, "b:move"));
	/* Off the captured widget it no longer looks pressed */
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (a)) & LRG_WIDGET_STATE_PRESSED);

	frame_pointer (&input, 250, 50);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (log_has (log, "a:up"));
	g_assert_false (log_has (log, "b:up"));

	/* Capture released: the next move goes to b */
	g_string_truncate (log, 0);
	frame_pointer (&input, 260, 50);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (log_has (log, "b:move"));

	g_object_unref (a);
	g_object_unref (b);
}

static void
test_enter_leave (CanvasFixture *fixture,
                  gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *outer = probe_new ("outer", log, 0, 0, 300, 300, FALSE);
	TestProbe         *inner = probe_new ("inner", log, 10, 10, 50, 50, FALSE);
	LrgCanvasInput     input;

	lrg_container_add_child (LRG_CONTAINER (outer), LRG_WIDGET (inner));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (outer));

	/* Entering inner enters outer first; both are HOVERED */
	frame_pointer (&input, 20, 20);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_cmpstr (log->str, ==, "outer:enter inner:enter inner:move outer:move^ ");
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (outer)) & LRG_WIDGET_STATE_HOVERED);
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (inner)) & LRG_WIDGET_STATE_HOVERED);
	g_assert_true (lrg_canvas_get_hovered_widget (fixture->canvas) == LRG_WIDGET (inner));

	/* Moving to outer only leaves inner (no FOCUS events any more) */
	g_string_truncate (log, 0);
	frame_pointer (&input, 200, 200);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (g_str_has_prefix (log->str, "inner:leave "));
	g_assert_false (log_has (log, "outer:leave"));
	g_assert_false (log_has (log, "focus"));
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (inner)) & LRG_WIDGET_STATE_HOVERED);

	/* Leaving everything */
	g_string_truncate (log, 0);
	frame_pointer (&input, 700, 500);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (log_has (log, "outer:leave"));
	g_assert_null (lrg_canvas_get_hovered_widget (fixture->canvas));

	g_object_unref (inner);
	g_object_unref (outer);
}

static void
test_pointer_transparent (CanvasFixture *fixture,
                          gconstpointer  user_data)
{
	TestProbe *under = probe_new ("under", NULL, 0, 0, 100, 100, FALSE);
	TestProbe *layer = probe_new ("layer", NULL, 0, 0, 800, 600, FALSE);
	TestProbe *chip = probe_new ("chip", NULL, 500, 500, 20, 20, FALSE);

	lrg_container_add_child (LRG_CONTAINER (layer), LRG_WIDGET (chip));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (under));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (layer));

	g_assert_true (lrg_canvas_widget_at_point (fixture->canvas, 50, 50) == LRG_WIDGET (layer));
	lrg_widget_set_pointer_transparent (LRG_WIDGET (layer), TRUE);
	g_assert_true (lrg_canvas_widget_at_point (fixture->canvas, 50, 50) == LRG_WIDGET (under));
	g_assert_true (lrg_canvas_widget_at_point (fixture->canvas, 505, 505) == LRG_WIDGET (chip));

	g_object_unref (under);
	g_object_unref (layer);
	g_object_unref (chip);
}

/* ==========================================================================
 * Focus
 * ========================================================================== */

static void
test_click_focus_and_visible (CanvasFixture *fixture,
                              gconstpointer  user_data)
{
	TestProbe *field = probe_new ("field", NULL, 0, 0, 100, 40, TRUE);
	TestProbe *icon = probe_new ("icon", NULL, 5, 5, 10, 10, FALSE);
	TestProbe *plain = probe_new ("plain", NULL, 300, 0, 100, 40, FALSE);

	lrg_container_add_child (LRG_CONTAINER (field), LRG_WIDGET (icon));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (field));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (plain));

	/* A click on a non-focusable child focuses its focusable ancestor,
	 * without a focus ring */
	click_at (fixture->canvas, 8, 8);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (field));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (field)) & LRG_WIDGET_STATE_FOCUSED);
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (field)) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	/* Clicking something unfocusable clears focus */
	click_at (fixture->canvas, 350, 20);
	g_assert_null (lrg_canvas_get_focused_widget (fixture->canvas));
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (field)) & LRG_WIDGET_STATE_FOCUSED);

	/* Keyboard focus shows the ring */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (field));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (field)) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	g_object_unref (field);
	g_object_unref (icon);
	g_object_unref (plain);
}

static void
test_tab_order (CanvasFixture *fixture,
                gconstpointer  user_data)
{
	TestProbe *a = probe_new ("a", NULL, 0, 0, 50, 20, TRUE);
	TestProbe *group = probe_new ("group", NULL, 0, 100, 300, 100, FALSE);
	TestProbe *b = probe_new ("b", NULL, 0, 0, 50, 20, TRUE);
	TestProbe *hidden = probe_new ("hidden", NULL, 60, 0, 50, 20, TRUE);
	TestProbe *disabled = probe_new ("disabled", NULL, 120, 0, 50, 20, TRUE);
	TestProbe *c = probe_new ("c", NULL, 0, 300, 50, 20, TRUE);

	lrg_widget_set_visible (LRG_WIDGET (hidden), FALSE);
	lrg_widget_set_enabled (LRG_WIDGET (disabled), FALSE);
	lrg_container_add_child (LRG_CONTAINER (group), LRG_WIDGET (b));
	lrg_container_add_child (LRG_CONTAINER (group), LRG_WIDGET (hidden));
	lrg_container_add_child (LRG_CONTAINER (group), LRG_WIDGET (disabled));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (a));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (group));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (c));

	/* Depth-first, skipping hidden and disabled, wrapping both ways */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (a));
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (b));
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (c));
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (a));
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_PREVIOUS);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (c));

	g_object_unref (a);
	g_object_unref (group);
	g_object_unref (b);
	g_object_unref (hidden);
	g_object_unref (disabled);
	g_object_unref (c);
}

static void
test_spatial_navigation (CanvasFixture *fixture,
                         gconstpointer  user_data)
{
	/*   [nw]        [ne]
	 *        [mid]
	 *   [sw]  [s]        */
	TestProbe *nw = probe_new ("nw", NULL, 0, 0, 40, 20, TRUE);
	TestProbe *ne = probe_new ("ne", NULL, 400, 0, 40, 20, TRUE);
	TestProbe *mid = probe_new ("mid", NULL, 200, 200, 40, 20, TRUE);
	TestProbe *sw = probe_new ("sw", NULL, 0, 400, 40, 20, TRUE);
	TestProbe *s = probe_new ("s", NULL, 210, 400, 40, 20, TRUE);

	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (nw));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (ne));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (mid));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (sw));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (s));
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (mid));

	/* Down prefers the widget in line (s) over the nearer-diagonal sw */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_DOWN);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (s));

	/* Left from s: sw */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_LEFT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (sw));

	/* Up from sw: nw (straight up), not mid */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_UP);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (nw));

	/* Nothing further up: focus stays */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_UP);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (nw));

	/* Arrow keys do the same when unhandled */
	frame_key (fixture->canvas, GRL_KEY_RIGHT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (ne));

	/* ...unless navigation is off */
	lrg_canvas_set_navigation_enabled (fixture->canvas, FALSE);
	frame_key (fixture->canvas, GRL_KEY_LEFT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (ne));

	g_object_unref (nw);
	g_object_unref (ne);
	g_object_unref (mid);
	g_object_unref (sw);
	g_object_unref (s);
}

static void
test_arrows_handled_by_widget (CanvasFixture *fixture,
                               gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *a = probe_new ("a", log, 0, 0, 40, 20, TRUE);
	TestProbe         *b = probe_new ("b", log, 100, 0, 40, 20, TRUE);

	a->consume = TRUE;
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (a));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (b));
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (a));

	/* A widget that consumes arrows keeps focus */
	frame_key (fixture->canvas, GRL_KEY_RIGHT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (a));
	g_assert_true (log_has (log, "a:key"));

	g_object_unref (a);
	g_object_unref (b);
}

static void
on_cancel_count (LrgCanvas *canvas,
                 gpointer   user_data)
{
	(*(gint *) user_data)++;
}

static void
test_activate_and_cancel (CanvasFixture *fixture,
                          gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *a = probe_new ("a", log, 0, 0, 40, 20, TRUE);
	LrgCanvasInput     input;
	gint               cancels = 0;

	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (a));
	g_signal_connect (fixture->canvas, "cancel", G_CALLBACK (on_cancel_count), &cancels);
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (a));

	/* Enter, Space and gamepad A activate, with a one-frame press */
	frame_key (fixture->canvas, GRL_KEY_ENTER);
	g_assert_true (log_has (log, "a:activate"));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (a)) & LRG_WIDGET_STATE_PRESSED);
	frame_pointer (&input, -50, -50);
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (a)) & LRG_WIDGET_STATE_PRESSED);

	g_string_truncate (log, 0);
	frame_pointer (&input, -50, -50);
	input.activate = TRUE;
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_true (log_has (log, "a:activate"));

	/* Escape: CANCEL to the focus, then the canvas signal */
	g_string_truncate (log, 0);
	frame_key (fixture->canvas, GRL_KEY_ESCAPE);
	g_assert_true (log_has (log, "a:cancel"));
	g_assert_cmpint (cancels, ==, 1);

	/* Handled CANCEL does not reach the canvas */
	a->consume = TRUE;
	frame_pointer (&input, -50, -50);
	input.cancel = TRUE;
	lrg_canvas_process_input (fixture->canvas, &input);
	g_assert_cmpint (cancels, ==, 1);

	g_object_unref (a);
}

static void
test_focus_cleared_on_destroy (CanvasFixture *fixture,
                               gconstpointer  user_data)
{
	TestProbe *a = probe_new ("a", NULL, 0, 0, 40, 20, TRUE);

	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (a));
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (a));
	lrg_container_remove_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (a));
	g_object_unref (a);

	/* Weak pointer: no dangling focus after the widget dies */
	g_assert_null (lrg_canvas_get_focused_widget (fixture->canvas));
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
}

static void
test_hidden_focus_dropped (CanvasFixture *fixture,
                           gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *parent = probe_new ("parent", log, 0, 0, 200, 200, FALSE);
	TestProbe         *a = probe_new ("a", log, 0, 0, 40, 20, TRUE);

	lrg_container_add_child (LRG_CONTAINER (parent), LRG_WIDGET (a));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (parent));
	lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (a));

	/* Hiding the focus drops it on the next frame; keys reach nobody */
	lrg_widget_set_visible (LRG_WIDGET (a), FALSE);
	g_string_truncate (log, 0);
	frame_key (fixture->canvas, GRL_KEY_A);
	g_assert_null (lrg_canvas_get_focused_widget (fixture->canvas));
	g_assert_false (log_has (log, "parent:key"));

	g_object_unref (a);
	g_object_unref (parent);
}

/* ==========================================================================
 * Popups
 * ========================================================================== */

static void
test_place_popup_flips (void)
{
	gfloat            x;
	gfloat            y;
	LrgPopupPlacement used;

	/* Room below: stays below, left-aligned with the anchor */
	used = lrg_canvas_place_popup (800, 600, 100, 100, 80, 30, 120, 200,
	                               LRG_POPUP_PLACEMENT_BELOW, &x, &y);
	g_assert_cmpint (used, ==, LRG_POPUP_PLACEMENT_BELOW);
	g_assert_cmpfloat (x, ==, 100.0f);
	g_assert_cmpfloat (y, >, 130.0f);

	/* Near the bottom: flips above */
	used = lrg_canvas_place_popup (800, 600, 100, 500, 80, 30, 120, 200,
	                               LRG_POPUP_PLACEMENT_BELOW, &x, &y);
	g_assert_cmpint (used, ==, LRG_POPUP_PLACEMENT_ABOVE);
	g_assert_cmpfloat (y + 200.0f, <, 500.0f);

	/* Near the right edge: flips left */
	used = lrg_canvas_place_popup (800, 600, 700, 100, 80, 30, 150, 100,
	                               LRG_POPUP_PLACEMENT_RIGHT, &x, &y);
	g_assert_cmpint (used, ==, LRG_POPUP_PLACEMENT_LEFT);
	g_assert_cmpfloat (x + 150.0f, <, 700.0f);

	/* Clamped inside the canvas */
	lrg_canvas_place_popup (800, 600, 780, 100, 10, 10, 300, 100,
	                        LRG_POPUP_PLACEMENT_BELOW, &x, &y);
	g_assert_cmpfloat (x + 300.0f, <=, 800.0f);

	/* Centre */
	lrg_canvas_place_popup (800, 600, 0, 0, 0, 0, 200, 100, LRG_POPUP_PLACEMENT_CENTER, &x, &y);
	g_assert_cmpfloat (x, ==, 300.0f);
}

static void
on_dismissed_count (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	(*(gint *) user_data)++;
}

static void
test_popup_light_dismiss (CanvasFixture *fixture,
                          gconstpointer  user_data)
{
	TestProbe *anchor = probe_new ("anchor", NULL, 100, 100, 80, 30, TRUE);
	TestProbe *popup = probe_new ("popup", NULL, 0, 0, 150, 100, FALSE);
	gint       dismissed = 0;
	gint       i;

	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (anchor));
	g_signal_connect (fixture->canvas, "popup-dismissed", G_CALLBACK (on_dismissed_count), &dismissed);

	lrg_canvas_show_popup (fixture->canvas, LRG_WIDGET (popup), LRG_WIDGET (anchor),
	                       LRG_POPUP_PLACEMENT_BELOW, FALSE);
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 1);
	g_assert_true (lrg_canvas_get_top_popup (fixture->canvas) == LRG_WIDGET (popup));
	g_assert_true (lrg_widget_get_state_flags (LRG_WIDGET (anchor)) & LRG_WIDGET_STATE_OPEN);
	g_assert_cmpfloat (lrg_widget_get_y (LRG_WIDGET (popup)), >, 130.0f);

	/* Popups are hit before the tree */
	g_assert_true (lrg_canvas_widget_at_point (fixture->canvas, 110, 150) == LRG_WIDGET (popup));

	/* A click elsewhere closes it */
	click_at (fixture->canvas, 600, 500);
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 0);
	g_assert_cmpint (dismissed, ==, 1);
	g_assert_false (lrg_widget_get_state_flags (LRG_WIDGET (anchor)) & LRG_WIDGET_STATE_OPEN);

	/* The ghost fades, then the popup is released from the canvas */
	g_assert_nonnull (lrg_widget_get_parent (LRG_WIDGET (popup)));
	for (i = 0; i < 60; i++)
		lrg_canvas_update (fixture->canvas, 1.0f / 60.0f);
	g_assert_null (lrg_widget_get_parent (LRG_WIDGET (popup)));

	g_object_unref (anchor);
	g_object_unref (popup);
}

static void
test_popup_modal_traps (CanvasFixture *fixture,
                        gconstpointer  user_data)
{
	g_autoptr(GString) log = g_string_new (NULL);
	TestProbe         *behind = probe_new ("behind", log, 0, 0, 100, 100, TRUE);
	TestProbe         *dialog = probe_new ("dialog", log, 0, 0, 300, 200, FALSE);
	TestProbe         *ok = probe_new ("ok", log, 20, 20, 60, 30, TRUE);
	TestProbe         *no = probe_new ("no", log, 120, 20, 60, 30, TRUE);
	LrgPaintList      *list;
	guint              n_before;

	lrg_container_add_child (LRG_CONTAINER (dialog), LRG_WIDGET (ok));
	lrg_container_add_child (LRG_CONTAINER (dialog), LRG_WIDGET (no));
	lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (behind));

	list = lrg_canvas_record (fixture->canvas);
	n_before = lrg_paint_list_get_n_instances (list);

	lrg_canvas_show_popup (fixture->canvas, LRG_WIDGET (dialog), NULL,
	                       LRG_POPUP_PLACEMENT_CENTER, TRUE);

	/* Centred; first focusable inside takes focus */
	g_assert_cmpfloat (lrg_widget_get_x (LRG_WIDGET (dialog)), ==, 250.0f);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (ok));

	/* Clicks behind it reach nothing */
	g_string_truncate (log, 0);
	click_at (fixture->canvas, 50, 50);
	g_assert_false (log_has (log, "behind"));
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 1);

	/* Tab cycles inside the dialog only */
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (no));
	frame_nav (fixture->canvas, LRG_NAV_DIRECTION_NEXT);
	g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (ok));

	/* The veil is painted under it */
	lrg_canvas_update (fixture->canvas, 0.5f);
	list = lrg_canvas_record (fixture->canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, n_before);

	/* Escape (unhandled) closes it */
	frame_key (fixture->canvas, GRL_KEY_ESCAPE);
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 0);

	g_object_unref (behind);
	g_object_unref (dialog);
	g_object_unref (ok);
	g_object_unref (no);
}

static void
test_popup_stack_dismiss (CanvasFixture *fixture,
                          gconstpointer  user_data)
{
	TestProbe *a = probe_new ("a", NULL, 0, 0, 100, 100, FALSE);
	TestProbe *b = probe_new ("b", NULL, 0, 0, 100, 100, FALSE);
	TestProbe *c = probe_new ("c", NULL, 0, 0, 100, 100, FALSE);

	lrg_canvas_show_popup (fixture->canvas, LRG_WIDGET (a), NULL, LRG_POPUP_PLACEMENT_CENTER, FALSE);
	lrg_canvas_show_popup (fixture->canvas, LRG_WIDGET (b), NULL, LRG_POPUP_PLACEMENT_CENTER, FALSE);
	lrg_canvas_show_popup (fixture->canvas, LRG_WIDGET (c), NULL, LRG_POPUP_PLACEMENT_CENTER, FALSE);
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 3);

	/* Dismissing b also closes c (opened after it) */
	lrg_canvas_dismiss_popup (fixture->canvas, LRG_WIDGET (b));
	g_assert_cmpuint (lrg_canvas_get_popup_count (fixture->canvas), ==, 1);
	g_assert_true (lrg_canvas_get_top_popup (fixture->canvas) == LRG_WIDGET (a));

	g_object_unref (a);
	g_object_unref (b);
	g_object_unref (c);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/ui/input/frame/init", test_input_frame_init);
    g_test_add_func ("/ui/input/frame/add-text", test_input_frame_add_text);
    g_test_add_func ("/ui/input/frame/overflow", test_input_frame_overflow);
    g_test_add_func ("/ui/input/frame/boxed", test_input_frame_boxed);

    g_test_add ("/ui/input/click/reaches-button", CanvasFixture, NULL,
                canvas_fixture_set_up, test_canvas_click_reaches_button,
                canvas_fixture_tear_down);
    g_test_add ("/ui/input/click/right-is-not-left", CanvasFixture, NULL,
                canvas_fixture_set_up, test_canvas_right_click_is_not_left,
                canvas_fixture_tear_down);

    g_test_add ("/ui/input/text/utf8", CanvasFixture, NULL,
                canvas_fixture_set_up, test_canvas_text_input_utf8,
                canvas_fixture_tear_down);
    g_test_add ("/ui/input/text/control-chars", CanvasFixture, NULL,
                canvas_fixture_set_up, test_canvas_text_input_ignores_control_chars,
                canvas_fixture_tear_down);
    g_test_add ("/ui/input/text/without-focus", CanvasFixture, NULL,
                canvas_fixture_set_up, test_canvas_text_without_focus,
                canvas_fixture_tear_down);

#define ADD(path, fn) \
    g_test_add (path, CanvasFixture, NULL, canvas_fixture_set_up, fn, canvas_fixture_tear_down)

    ADD ("/ui/input/dispatch/bubbling", test_bubbling);
    ADD ("/ui/input/dispatch/capture", test_pointer_capture);
    ADD ("/ui/input/dispatch/enter-leave", test_enter_leave);
    ADD ("/ui/input/dispatch/pointer-transparent", test_pointer_transparent);
    ADD ("/ui/input/focus/click-and-visible", test_click_focus_and_visible);
    ADD ("/ui/input/focus/tab-order", test_tab_order);
    ADD ("/ui/input/focus/spatial", test_spatial_navigation);
    ADD ("/ui/input/focus/arrows-handled", test_arrows_handled_by_widget);
    ADD ("/ui/input/focus/activate-cancel", test_activate_and_cancel);
    ADD ("/ui/input/focus/weak", test_focus_cleared_on_destroy);
    ADD ("/ui/input/focus/hidden-dropped", test_hidden_focus_dropped);
    g_test_add_func ("/ui/input/popup/placement", test_place_popup_flips);
    ADD ("/ui/input/popup/light-dismiss", test_popup_light_dismiss);
    ADD ("/ui/input/popup/modal", test_popup_modal_traps);
    ADD ("/ui/input/popup/stack", test_popup_stack_dismiss);

#undef ADD

    return g_test_run ();
}
