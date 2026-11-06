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

    return g_test_run ();
}
