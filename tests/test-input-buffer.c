/* test-input-buffer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgInputBuffer - input buffering for action games.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Cases - LrgInputBuffer Construction
 * ========================================================================== */

static void
test_input_buffer_new (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);

    g_assert_nonnull (buffer);
    g_assert_cmpint (lrg_input_buffer_get_buffer_frames (buffer), ==, 5);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_new_minimum_frames (void)
{
    LrgInputBuffer *buffer;

    /* Minimum valid buffer is 1 frame */
    buffer = lrg_input_buffer_new (1);

    g_assert_nonnull (buffer);
    g_assert_cmpint (lrg_input_buffer_get_buffer_frames (buffer), ==, 1);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_free_null (void)
{
    /* Should handle NULL without crashing */
    lrg_input_buffer_free (NULL);
    g_assert_true (TRUE);
}

/* ==========================================================================
 * Test Cases - LrgInputBuffer Configuration
 * ========================================================================== */

static void
test_input_buffer_buffer_frames (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    g_assert_cmpint (lrg_input_buffer_get_buffer_frames (buffer), ==, 5);

    lrg_input_buffer_set_buffer_frames (buffer, 10);
    g_assert_cmpint (lrg_input_buffer_get_buffer_frames (buffer), ==, 10);

    lrg_input_buffer_set_buffer_frames (buffer, 1);
    g_assert_cmpint (lrg_input_buffer_get_buffer_frames (buffer), ==, 1);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_enabled (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Should be enabled by default */
    g_assert_true (lrg_input_buffer_is_enabled (buffer));

    /* Disable */
    lrg_input_buffer_set_enabled (buffer, FALSE);
    g_assert_false (lrg_input_buffer_is_enabled (buffer));

    /* Re-enable */
    lrg_input_buffer_set_enabled (buffer, TRUE);
    g_assert_true (lrg_input_buffer_is_enabled (buffer));

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_context (void)
{
    LrgInputBuffer *buffer;
    LrgInputContext context;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Default context */
    context = lrg_input_buffer_get_context (buffer);
    g_assert_cmpint (context, ==, LRG_INPUT_CONTEXT_GAMEPLAY);

    /* Set to menu context */
    lrg_input_buffer_set_context (buffer, LRG_INPUT_CONTEXT_MENU);
    context = lrg_input_buffer_get_context (buffer);
    g_assert_cmpint (context, ==, LRG_INPUT_CONTEXT_MENU);

    lrg_input_buffer_free (buffer);
}

/* ==========================================================================
 * Test Cases - LrgInputBuffer Core Operations
 * ========================================================================== */

static void
test_input_buffer_record_and_length (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Empty initially */
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 0);

    /* Record some actions */
    lrg_input_buffer_record (buffer, "jump");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 1);

    lrg_input_buffer_record (buffer, "attack");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 2);

    lrg_input_buffer_record (buffer, "dash");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 3);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_has_action (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Initially empty */
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));

    /* Record and check */
    lrg_input_buffer_record (buffer, "jump");
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));
    g_assert_false (lrg_input_buffer_has_action (buffer, "attack"));

    lrg_input_buffer_record (buffer, "attack");
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));
    g_assert_true (lrg_input_buffer_has_action (buffer, "attack"));

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_consume (void)
{
    LrgInputBuffer *buffer;
    gboolean consumed;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Record an action */
    lrg_input_buffer_record (buffer, "jump");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 1);

    /* Consume with correct context */
    consumed = lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_GAMEPLAY);
    g_assert_true (consumed);
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 0);
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));

    /* Consume again should fail (already consumed) */
    consumed = lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_GAMEPLAY);
    g_assert_false (consumed);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_consume_wrong_context (void)
{
    LrgInputBuffer *buffer;
    gboolean consumed;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Record in gameplay context */
    lrg_input_buffer_set_context (buffer, LRG_INPUT_CONTEXT_GAMEPLAY);
    lrg_input_buffer_record (buffer, "jump");

    /* Try to consume with menu context - should fail */
    consumed = lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_MENU);
    g_assert_false (consumed);

    /* Action should still be in buffer */
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));

    /* Consume with correct context should work */
    consumed = lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_GAMEPLAY);
    g_assert_true (consumed);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_consume_missing_action (void)
{
    LrgInputBuffer *buffer;
    gboolean consumed;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Try to consume action that was never recorded */
    consumed = lrg_input_buffer_consume (buffer, "nonexistent", LRG_INPUT_CONTEXT_GAMEPLAY);
    g_assert_false (consumed);

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_clear (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Record some actions */
    lrg_input_buffer_record (buffer, "jump");
    lrg_input_buffer_record (buffer, "attack");
    lrg_input_buffer_record (buffer, "dash");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 3);

    /* Clear all */
    lrg_input_buffer_clear (buffer);

    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 0);
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));
    g_assert_false (lrg_input_buffer_has_action (buffer, "attack"));
    g_assert_false (lrg_input_buffer_has_action (buffer, "dash"));

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_update_expires_inputs (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (3); /* 3 frame buffer */
    g_assert_nonnull (buffer);

    /* Record an action */
    lrg_input_buffer_record (buffer, "jump");
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));

    /* First update - still valid */
    lrg_input_buffer_update (buffer);
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));

    /* Second update - still valid */
    lrg_input_buffer_update (buffer);
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));

    /* Third update - should be expired (3 frames elapsed) */
    lrg_input_buffer_update (buffer);
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));

    lrg_input_buffer_free (buffer);
}

/* ==========================================================================
 * Test Cases - LrgInputBuffer Disabled Behavior
 * ========================================================================== */

static void
test_input_buffer_disabled_record (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    lrg_input_buffer_set_enabled (buffer, FALSE);

    /* Record should do nothing when disabled */
    lrg_input_buffer_record (buffer, "jump");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 0);
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));

    lrg_input_buffer_free (buffer);
}

static void
test_input_buffer_disabled_consume (void)
{
    LrgInputBuffer *buffer;
    gboolean consumed;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Record while enabled */
    lrg_input_buffer_record (buffer, "jump");
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));

    /* Disable and try to consume */
    lrg_input_buffer_set_enabled (buffer, FALSE);
    consumed = lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_GAMEPLAY);

    /* Consume should return FALSE when disabled */
    g_assert_false (consumed);

    lrg_input_buffer_free (buffer);
}

/* ==========================================================================
 * Test Cases - LrgInputBuffer Context Change
 * ========================================================================== */

static void
test_input_buffer_context_change_clears (void)
{
    LrgInputBuffer *buffer;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Record in gameplay context */
    lrg_input_buffer_record (buffer, "jump");
    lrg_input_buffer_record (buffer, "attack");
    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 2);

    /* Change context - should clear buffer */
    lrg_input_buffer_set_context (buffer, LRG_INPUT_CONTEXT_MENU);

    g_assert_cmpuint (lrg_input_buffer_get_length (buffer), ==, 0);
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));
    g_assert_false (lrg_input_buffer_has_action (buffer, "attack"));

    lrg_input_buffer_free (buffer);
}

/* ==========================================================================
 * Test Cases - LrgInputBuffer Multiple Same Action
 * ========================================================================== */

static void
test_input_buffer_multiple_different_actions (void)
{
    LrgInputBuffer *buffer;
    gboolean consumed;

    buffer = lrg_input_buffer_new (5);
    g_assert_nonnull (buffer);

    /* Record different actions */
    lrg_input_buffer_record (buffer, "jump");
    lrg_input_buffer_record (buffer, "attack");
    lrg_input_buffer_record (buffer, "dash");

    /* All actions should be present */
    g_assert_true (lrg_input_buffer_has_action (buffer, "jump"));
    g_assert_true (lrg_input_buffer_has_action (buffer, "attack"));
    g_assert_true (lrg_input_buffer_has_action (buffer, "dash"));

    /* Consume one at a time */
    consumed = lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_GAMEPLAY);
    g_assert_true (consumed);
    g_assert_false (lrg_input_buffer_has_action (buffer, "jump"));
    g_assert_true (lrg_input_buffer_has_action (buffer, "attack"));
    g_assert_true (lrg_input_buffer_has_action (buffer, "dash"));

    consumed = lrg_input_buffer_consume (buffer, "attack", LRG_INPUT_CONTEXT_GAMEPLAY);
    g_assert_true (consumed);
    g_assert_false (lrg_input_buffer_has_action (buffer, "attack"));

    lrg_input_buffer_free (buffer);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction tests */
    g_test_add_func ("/input-buffer/new",
                     test_input_buffer_new);
    g_test_add_func ("/input-buffer/new-minimum-frames",
                     test_input_buffer_new_minimum_frames);
    g_test_add_func ("/input-buffer/free-null",
                     test_input_buffer_free_null);

    /* Configuration tests */
    g_test_add_func ("/input-buffer/buffer-frames",
                     test_input_buffer_buffer_frames);
    g_test_add_func ("/input-buffer/enabled",
                     test_input_buffer_enabled);
    g_test_add_func ("/input-buffer/context",
                     test_input_buffer_context);

    /* Core operation tests */
    g_test_add_func ("/input-buffer/record-and-length",
                     test_input_buffer_record_and_length);
    g_test_add_func ("/input-buffer/has-action",
                     test_input_buffer_has_action);
    g_test_add_func ("/input-buffer/consume",
                     test_input_buffer_consume);
    g_test_add_func ("/input-buffer/consume-wrong-context",
                     test_input_buffer_consume_wrong_context);
    g_test_add_func ("/input-buffer/consume-missing-action",
                     test_input_buffer_consume_missing_action);
    g_test_add_func ("/input-buffer/clear",
                     test_input_buffer_clear);
    g_test_add_func ("/input-buffer/update-expires-inputs",
                     test_input_buffer_update_expires_inputs);

    /* Disabled behavior tests */
    g_test_add_func ("/input-buffer/disabled-record",
                     test_input_buffer_disabled_record);
    g_test_add_func ("/input-buffer/disabled-consume",
                     test_input_buffer_disabled_consume);

    /* Context change tests */
    g_test_add_func ("/input-buffer/context-change-clears",
                     test_input_buffer_context_change_clears);

    /* Multiple same action tests */
    g_test_add_func ("/input-buffer/multiple-different-actions",
                     test_input_buffer_multiple_different_actions);

    return g_test_run ();
}
