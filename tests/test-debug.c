/* test-debug.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Debug module (profiler, console, overlay).
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgProfiler *profiler;
} ProfilerFixture;

typedef struct
{
    LrgDebugConsole *console;
} ConsoleFixture;

typedef struct
{
    LrgDebugOverlay *overlay;
} OverlayFixture;

typedef struct
{
    LrgInspector *inspector;
    LrgWorld     *world;
    LrgGameObject *object;
    LrgComponent  *component;
} InspectorFixture;

static void
profiler_fixture_set_up (ProfilerFixture *fixture,
                         gconstpointer    user_data)
{
    (void)user_data;
    fixture->profiler = lrg_profiler_new ();
    g_assert_nonnull (fixture->profiler);
}

static void
profiler_fixture_tear_down (ProfilerFixture *fixture,
                            gconstpointer    user_data)
{
    (void)user_data;
    g_clear_object (&fixture->profiler);
}

static void
console_fixture_set_up (ConsoleFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;
    fixture->console = lrg_debug_console_new ();
    g_assert_nonnull (fixture->console);
}

static void
console_fixture_tear_down (ConsoleFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;
    g_clear_object (&fixture->console);
}

static void
overlay_fixture_set_up (OverlayFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;
    fixture->overlay = lrg_debug_overlay_new ();
    g_assert_nonnull (fixture->overlay);
}

static void
overlay_fixture_tear_down (OverlayFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;
    g_clear_object (&fixture->overlay);
}

static void
inspector_fixture_set_up (InspectorFixture *fixture,
                          gconstpointer     user_data)
{
    (void)user_data;
    fixture->inspector = lrg_inspector_new ();
    g_assert_nonnull (fixture->inspector);

    /* Create a world with an object and component for testing */
    fixture->world = lrg_world_new ();
    fixture->object = g_object_new (LRG_TYPE_GAME_OBJECT,
                                    "tag", "test-object",
                                    NULL);
    /* Use sprite component as a concrete component */
    fixture->component = LRG_COMPONENT (lrg_sprite_component_new ());

    lrg_game_object_add_component (fixture->object, fixture->component);
    lrg_world_add_object (fixture->world, fixture->object);
}

static void
inspector_fixture_tear_down (InspectorFixture *fixture,
                             gconstpointer     user_data)
{
    (void)user_data;
    g_clear_object (&fixture->inspector);
    g_clear_object (&fixture->world);
    /* object and component are owned by world */
}

/* ==========================================================================
 * Profiler Sample Tests
 * ========================================================================== */

static void
test_profiler_sample_copy_null (void)
{
    LrgProfilerSample *copy;

    copy = lrg_profiler_sample_copy (NULL);
    g_assert_null (copy);
}

static void
test_profiler_sample_free_null (void)
{
    /* Should not crash */
    lrg_profiler_sample_free (NULL);
}

/* ==========================================================================
 * Profiler Tests
 * ========================================================================== */

static void
test_profiler_new (ProfilerFixture *fixture,
                   gconstpointer    user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_PROFILER (fixture->profiler));
}

static void
test_profiler_get_default (void)
{
    LrgProfiler *profiler1;
    LrgProfiler *profiler2;

    profiler1 = lrg_profiler_get_default ();
    g_assert_nonnull (profiler1);

    profiler2 = lrg_profiler_get_default ();
    g_assert_true (profiler1 == profiler2);
}

static void
test_profiler_enabled (ProfilerFixture *fixture,
                       gconstpointer    user_data)
{
    (void)user_data;

    /* Default is disabled */
    g_assert_false (lrg_profiler_is_enabled (fixture->profiler));

    /* Enable */
    lrg_profiler_set_enabled (fixture->profiler, TRUE);
    g_assert_true (lrg_profiler_is_enabled (fixture->profiler));

    /* Disable */
    lrg_profiler_set_enabled (fixture->profiler, FALSE);
    g_assert_false (lrg_profiler_is_enabled (fixture->profiler));
}

static void
test_profiler_max_samples (ProfilerFixture *fixture,
                           gconstpointer    user_data)
{
    (void)user_data;

    /* Default */
    g_assert_cmpuint (lrg_profiler_get_max_samples (fixture->profiler), ==, 60);

    /* Set new value */
    lrg_profiler_set_max_samples (fixture->profiler, 100);
    g_assert_cmpuint (lrg_profiler_get_max_samples (fixture->profiler), ==, 100);

    /* Minimum of 1 */
    lrg_profiler_set_max_samples (fixture->profiler, 0);
    g_assert_cmpuint (lrg_profiler_get_max_samples (fixture->profiler), ==, 1);
}

static void
test_profiler_section_timing (ProfilerFixture *fixture,
                              gconstpointer    user_data)
{
    GList *sections;
    guint count;

    (void)user_data;

    lrg_profiler_set_enabled (fixture->profiler, TRUE);

    /* Begin and end section */
    lrg_profiler_begin_section (fixture->profiler, "test-section");
    g_usleep (1000);  /* 1ms */
    lrg_profiler_end_section (fixture->profiler, "test-section");

    /* Check section exists */
    sections = lrg_profiler_get_section_names (fixture->profiler);
    g_assert_nonnull (sections);
    g_assert_cmpuint (g_list_length (sections), ==, 1);
    g_list_free (sections);

    /* Check sample count */
    count = lrg_profiler_get_sample_count (fixture->profiler, "test-section");
    g_assert_cmpuint (count, ==, 1);

    /* Check timing is reasonable */
    g_assert_cmpfloat (lrg_profiler_get_average_ms (fixture->profiler, "test-section"),
                       >=, 0.5);
}

static void
test_profiler_section_disabled (ProfilerFixture *fixture,
                                gconstpointer    user_data)
{
    GList *sections;

    (void)user_data;

    /* Profiler is disabled by default */
    g_assert_false (lrg_profiler_is_enabled (fixture->profiler));

    lrg_profiler_begin_section (fixture->profiler, "test-section");
    lrg_profiler_end_section (fixture->profiler, "test-section");

    /* No sections should be recorded */
    sections = lrg_profiler_get_section_names (fixture->profiler);
    g_assert_cmpuint (g_list_length (sections), ==, 0);
    g_list_free (sections);
}

static void
test_profiler_frame_timing (ProfilerFixture *fixture,
                            gconstpointer    user_data)
{
    (void)user_data;

    lrg_profiler_set_enabled (fixture->profiler, TRUE);

    /* Initial values */
    g_assert_cmpfloat (lrg_profiler_get_frame_time_ms (fixture->profiler), ==, 0.0);

    /* Frame timing */
    lrg_profiler_begin_frame (fixture->profiler);
    g_usleep (1000);  /* 1ms */
    lrg_profiler_end_frame (fixture->profiler);

    /* Frame time should be recorded */
    g_assert_cmpfloat (lrg_profiler_get_frame_time_ms (fixture->profiler), >=, 0.5);
}

static void
test_profiler_statistics (ProfilerFixture *fixture,
                          gconstpointer    user_data)
{
    gint i;
    gdouble avg;
    gdouble min_val;
    gdouble max_val;

    (void)user_data;

    lrg_profiler_set_enabled (fixture->profiler, TRUE);

    /* Record multiple samples with varying times */
    for (i = 0; i < 5; i++)
    {
        lrg_profiler_begin_section (fixture->profiler, "stats-test");
        g_usleep (1000 * (i + 1));  /* 1-5 ms */
        lrg_profiler_end_section (fixture->profiler, "stats-test");
    }

    avg = lrg_profiler_get_average_ms (fixture->profiler, "stats-test");
    min_val = lrg_profiler_get_min_ms (fixture->profiler, "stats-test");
    max_val = lrg_profiler_get_max_ms (fixture->profiler, "stats-test");

    /* Min should be less than average */
    g_assert_cmpfloat (min_val, <=, avg);
    /* Max should be greater than average */
    g_assert_cmpfloat (max_val, >=, avg);
    /* Sample count */
    g_assert_cmpuint (lrg_profiler_get_sample_count (fixture->profiler, "stats-test"),
                      ==, 5);
}

static void
test_profiler_get_last_sample (ProfilerFixture *fixture,
                               gconstpointer    user_data)
{
    LrgProfilerSample *sample;

    (void)user_data;

    lrg_profiler_set_enabled (fixture->profiler, TRUE);

    /* No sample yet */
    sample = lrg_profiler_get_last_sample (fixture->profiler, "sample-test");
    g_assert_null (sample);

    /* Record a sample */
    lrg_profiler_begin_section (fixture->profiler, "sample-test");
    g_usleep (500);
    lrg_profiler_end_section (fixture->profiler, "sample-test");

    sample = lrg_profiler_get_last_sample (fixture->profiler, "sample-test");
    g_assert_nonnull (sample);
    g_assert_cmpstr (lrg_profiler_sample_get_name (sample), ==, "sample-test");
    g_assert_cmpint (lrg_profiler_sample_get_duration_us (sample), >=, 400);
    g_assert_cmpfloat (lrg_profiler_sample_get_duration_ms (sample), >=, 0.4);

    lrg_profiler_sample_free (sample);
}

static void
test_profiler_clear (ProfilerFixture *fixture,
                     gconstpointer    user_data)
{
    GList *sections;

    (void)user_data;

    lrg_profiler_set_enabled (fixture->profiler, TRUE);

    /* Record some data */
    lrg_profiler_begin_section (fixture->profiler, "clear-test");
    lrg_profiler_end_section (fixture->profiler, "clear-test");

    lrg_profiler_begin_frame (fixture->profiler);
    lrg_profiler_end_frame (fixture->profiler);

    /* Clear */
    lrg_profiler_clear (fixture->profiler);

    /* Everything should be reset */
    sections = lrg_profiler_get_section_names (fixture->profiler);
    g_assert_cmpuint (g_list_length (sections), ==, 0);
    g_list_free (sections);

    g_assert_cmpfloat (lrg_profiler_get_frame_time_ms (fixture->profiler), ==, 0.0);
    g_assert_cmpfloat (lrg_profiler_get_fps (fixture->profiler), ==, 0.0);
}

static void
test_profiler_clear_section (ProfilerFixture *fixture,
                             gconstpointer    user_data)
{
    GList *sections;

    (void)user_data;

    lrg_profiler_set_enabled (fixture->profiler, TRUE);

    /* Record sections */
    lrg_profiler_begin_section (fixture->profiler, "section-a");
    lrg_profiler_end_section (fixture->profiler, "section-a");

    lrg_profiler_begin_section (fixture->profiler, "section-b");
    lrg_profiler_end_section (fixture->profiler, "section-b");

    /* Clear one section */
    lrg_profiler_clear_section (fixture->profiler, "section-a");

    /* Only section-b should remain */
    sections = lrg_profiler_get_section_names (fixture->profiler);
    g_assert_cmpuint (g_list_length (sections), ==, 1);
    g_list_free (sections);

    g_assert_cmpuint (lrg_profiler_get_sample_count (fixture->profiler, "section-a"),
                      ==, 0);
    g_assert_cmpuint (lrg_profiler_get_sample_count (fixture->profiler, "section-b"),
                      ==, 1);
}

/* ==========================================================================
 * Console Output Tests
 * ========================================================================== */

static void
test_console_output_copy_null (void)
{
    LrgConsoleOutput *copy;

    copy = lrg_console_output_copy (NULL);
    g_assert_null (copy);
}

static void
test_console_output_free_null (void)
{
    /* Should not crash */
    lrg_console_output_free (NULL);
}

/* ==========================================================================
 * Console Tests
 * ========================================================================== */

static void
test_console_new (ConsoleFixture *fixture,
                  gconstpointer   user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_DEBUG_CONSOLE (fixture->console));
}

static void
test_console_get_default (void)
{
    LrgDebugConsole *console1;
    LrgDebugConsole *console2;

    console1 = lrg_debug_console_get_default ();
    g_assert_nonnull (console1);

    console2 = lrg_debug_console_get_default ();
    g_assert_true (console1 == console2);
}

static void
test_console_visibility (ConsoleFixture *fixture,
                         gconstpointer   user_data)
{
    (void)user_data;

    /* Default is hidden */
    g_assert_false (lrg_debug_console_is_visible (fixture->console));

    /* Show */
    lrg_debug_console_set_visible (fixture->console, TRUE);
    g_assert_true (lrg_debug_console_is_visible (fixture->console));

    /* Hide */
    lrg_debug_console_set_visible (fixture->console, FALSE);
    g_assert_false (lrg_debug_console_is_visible (fixture->console));

    /* Toggle */
    lrg_debug_console_toggle (fixture->console);
    g_assert_true (lrg_debug_console_is_visible (fixture->console));
}

static void
test_console_max_history (ConsoleFixture *fixture,
                          gconstpointer   user_data)
{
    (void)user_data;

    /* Default */
    g_assert_cmpuint (lrg_debug_console_get_max_history (fixture->console), ==, 100);

    /* Set new value */
    lrg_debug_console_set_max_history (fixture->console, 50);
    g_assert_cmpuint (lrg_debug_console_get_max_history (fixture->console), ==, 50);
}

static void
test_console_builtin_commands (ConsoleFixture *fixture,
                               gconstpointer   user_data)
{
    GList *commands;
    gboolean has_help;
    gboolean has_clear;
    gboolean has_echo;
    gboolean has_history;
    GList *l;

    (void)user_data;

    commands = lrg_debug_console_get_commands (fixture->console);
    g_assert_nonnull (commands);

    has_help = FALSE;
    has_clear = FALSE;
    has_echo = FALSE;
    has_history = FALSE;

    for (l = commands; l != NULL; l = l->next)
    {
        const gchar *name = l->data;
        if (g_strcmp0 (name, "help") == 0) has_help = TRUE;
        if (g_strcmp0 (name, "clear") == 0) has_clear = TRUE;
        if (g_strcmp0 (name, "echo") == 0) has_echo = TRUE;
        if (g_strcmp0 (name, "history") == 0) has_history = TRUE;
    }

    g_list_free (commands);

    g_assert_true (has_help);
    g_assert_true (has_clear);
    g_assert_true (has_echo);
    g_assert_true (has_history);
}

static gint g_custom_cmd_call_count = 0;

static gchar *
custom_command (LrgDebugConsole  *console,
                guint             argc,
                const gchar     **argv,
                gpointer          user_data)
{
    (void)console;
    (void)argc;
    (void)argv;
    (void)user_data;
    g_custom_cmd_call_count++;
    return g_strdup ("custom result");
}

static void
test_console_register_command (ConsoleFixture *fixture,
                               gconstpointer   user_data)
{
    const gchar *desc;
    g_autofree gchar *result = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;
    g_custom_cmd_call_count = 0;

    lrg_debug_console_register_command (fixture->console,
                                        "mycmd",
                                        "My custom command",
                                        custom_command,
                                        NULL, NULL);

    /* Check description */
    desc = lrg_debug_console_get_command_description (fixture->console, "mycmd");
    g_assert_cmpstr (desc, ==, "My custom command");

    /* Execute */
    result = lrg_debug_console_execute (fixture->console, "mycmd", &error);
    g_assert_no_error (error);
    g_assert_cmpstr (result, ==, "custom result");
    g_assert_cmpint (g_custom_cmd_call_count, ==, 1);
}

static void
test_console_unregister_command (ConsoleFixture *fixture,
                                 gconstpointer   user_data)
{
    gboolean removed;

    (void)user_data;

    lrg_debug_console_register_command (fixture->console,
                                        "temp-cmd",
                                        NULL,
                                        custom_command,
                                        NULL, NULL);

    removed = lrg_debug_console_unregister_command (fixture->console, "temp-cmd");
    g_assert_true (removed);

    removed = lrg_debug_console_unregister_command (fixture->console, "temp-cmd");
    g_assert_false (removed);
}

static void
test_console_execute_echo (ConsoleFixture *fixture,
                           gconstpointer   user_data)
{
    g_autofree gchar *result = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    result = lrg_debug_console_execute (fixture->console, "echo hello world", &error);
    g_assert_no_error (error);
    g_assert_cmpstr (result, ==, "hello world");
}

static void
test_console_execute_unknown (ConsoleFixture *fixture,
                              gconstpointer   user_data)
{
    g_autofree gchar *result = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    result = lrg_debug_console_execute (fixture->console, "nonexistent", &error);
    g_assert_error (error, LRG_DEBUG_ERROR, LRG_DEBUG_ERROR_COMMAND_NOT_FOUND);
    g_assert_null (result);
}

static void
test_console_print (ConsoleFixture *fixture,
                    gconstpointer   user_data)
{
    GQueue *output;
    LrgConsoleOutput *entry;

    (void)user_data;

    lrg_debug_console_print (fixture->console, "Test message");

    output = lrg_debug_console_get_output (fixture->console);
    g_assert_cmpuint (g_queue_get_length (output), ==, 1);

    entry = g_queue_peek_tail (output);
    g_assert_cmpstr (lrg_console_output_get_text (entry), ==, "Test message");
    g_assert_false (lrg_console_output_is_command (entry));
    g_assert_false (lrg_console_output_is_error (entry));
}

static void
test_console_print_error (ConsoleFixture *fixture,
                          gconstpointer   user_data)
{
    GQueue *output;
    LrgConsoleOutput *entry;

    (void)user_data;

    lrg_debug_console_print_error (fixture->console, "Error message");

    output = lrg_debug_console_get_output (fixture->console);
    g_assert_cmpuint (g_queue_get_length (output), ==, 1);

    entry = g_queue_peek_tail (output);
    g_assert_cmpstr (lrg_console_output_get_text (entry), ==, "Error message");
    g_assert_true (lrg_console_output_is_error (entry));
}

static void
test_console_printf (ConsoleFixture *fixture,
                     gconstpointer   user_data)
{
    GQueue *output;
    LrgConsoleOutput *entry;

    (void)user_data;

    lrg_debug_console_printf (fixture->console, "Value: %d", 42);

    output = lrg_debug_console_get_output (fixture->console);
    entry = g_queue_peek_tail (output);
    g_assert_cmpstr (lrg_console_output_get_text (entry), ==, "Value: 42");
}

static void
test_console_history (ConsoleFixture *fixture,
                      gconstpointer   user_data)
{
    GQueue *history;

    (void)user_data;

    /* Execute some commands */
    lrg_debug_console_execute (fixture->console, "echo one", NULL);
    lrg_debug_console_execute (fixture->console, "echo two", NULL);

    history = lrg_debug_console_get_history (fixture->console);
    g_assert_cmpuint (g_queue_get_length (history), ==, 2);

    /* Clear history */
    lrg_debug_console_clear_history (fixture->console);
    g_assert_cmpuint (g_queue_get_length (history), ==, 0);
}

static void
test_console_clear (ConsoleFixture *fixture,
                    gconstpointer   user_data)
{
    GQueue *output;

    (void)user_data;

    lrg_debug_console_print (fixture->console, "Message 1");
    lrg_debug_console_print (fixture->console, "Message 2");

    output = lrg_debug_console_get_output (fixture->console);
    g_assert_cmpuint (g_queue_get_length (output), ==, 2);

    lrg_debug_console_clear (fixture->console);

    output = lrg_debug_console_get_output (fixture->console);
    g_assert_cmpuint (g_queue_get_length (output), ==, 0);
}

/* ==========================================================================
 * Overlay Tests
 * ========================================================================== */

static void
test_overlay_new (OverlayFixture *fixture,
                  gconstpointer   user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_DEBUG_OVERLAY (fixture->overlay));
}

static void
test_overlay_get_default (void)
{
    LrgDebugOverlay *overlay1;
    LrgDebugOverlay *overlay2;

    overlay1 = lrg_debug_overlay_get_default ();
    g_assert_nonnull (overlay1);

    overlay2 = lrg_debug_overlay_get_default ();
    g_assert_true (overlay1 == overlay2);
}

static void
test_overlay_visibility (OverlayFixture *fixture,
                         gconstpointer   user_data)
{
    (void)user_data;

    /* Default is hidden */
    g_assert_false (lrg_debug_overlay_is_visible (fixture->overlay));

    /* Show */
    lrg_debug_overlay_set_visible (fixture->overlay, TRUE);
    g_assert_true (lrg_debug_overlay_is_visible (fixture->overlay));

    /* Hide */
    lrg_debug_overlay_set_visible (fixture->overlay, FALSE);
    g_assert_false (lrg_debug_overlay_is_visible (fixture->overlay));

    /* Toggle */
    lrg_debug_overlay_toggle (fixture->overlay);
    g_assert_true (lrg_debug_overlay_is_visible (fixture->overlay));
}

static void
test_overlay_flags (OverlayFixture *fixture,
                    gconstpointer   user_data)
{
    LrgDebugOverlayFlags flags;

    (void)user_data;

    /* Default flags */
    flags = lrg_debug_overlay_get_flags (fixture->overlay);
    g_assert_true ((flags & LRG_DEBUG_OVERLAY_FPS) != 0);
    g_assert_true ((flags & LRG_DEBUG_OVERLAY_FRAME_TIME) != 0);

    /* Set flags */
    lrg_debug_overlay_set_flags (fixture->overlay, LRG_DEBUG_OVERLAY_MEMORY);
    flags = lrg_debug_overlay_get_flags (fixture->overlay);
    g_assert_cmpuint (flags, ==, LRG_DEBUG_OVERLAY_MEMORY);

    /* Add flags */
    lrg_debug_overlay_add_flags (fixture->overlay, LRG_DEBUG_OVERLAY_PROFILER);
    g_assert_true (lrg_debug_overlay_has_flag (fixture->overlay, LRG_DEBUG_OVERLAY_MEMORY));
    g_assert_true (lrg_debug_overlay_has_flag (fixture->overlay, LRG_DEBUG_OVERLAY_PROFILER));

    /* Remove flags */
    lrg_debug_overlay_remove_flags (fixture->overlay, LRG_DEBUG_OVERLAY_MEMORY);
    g_assert_false (lrg_debug_overlay_has_flag (fixture->overlay, LRG_DEBUG_OVERLAY_MEMORY));
    g_assert_true (lrg_debug_overlay_has_flag (fixture->overlay, LRG_DEBUG_OVERLAY_PROFILER));
}

static void
test_overlay_position (OverlayFixture *fixture,
                       gconstpointer   user_data)
{
    gint x;
    gint y;

    (void)user_data;

    /* Default position */
    lrg_debug_overlay_get_position (fixture->overlay, &x, &y);
    g_assert_cmpint (x, ==, 10);
    g_assert_cmpint (y, ==, 10);

    /* Set position */
    lrg_debug_overlay_set_position (fixture->overlay, 50, 100);
    lrg_debug_overlay_get_position (fixture->overlay, &x, &y);
    g_assert_cmpint (x, ==, 50);
    g_assert_cmpint (y, ==, 100);
}

static void
test_overlay_font_size (OverlayFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;

    /* Default */
    g_assert_cmpint (lrg_debug_overlay_get_font_size (fixture->overlay), ==, 16);

    /* Set */
    lrg_debug_overlay_set_font_size (fixture->overlay, 24);
    g_assert_cmpint (lrg_debug_overlay_get_font_size (fixture->overlay), ==, 24);

    /* Minimum of 8 */
    lrg_debug_overlay_set_font_size (fixture->overlay, 4);
    g_assert_cmpint (lrg_debug_overlay_get_font_size (fixture->overlay), ==, 8);
}

static void
test_overlay_padding (OverlayFixture *fixture,
                      gconstpointer   user_data)
{
    (void)user_data;

    /* Default */
    g_assert_cmpint (lrg_debug_overlay_get_padding (fixture->overlay), ==, 5);

    /* Set */
    lrg_debug_overlay_set_padding (fixture->overlay, 10);
    g_assert_cmpint (lrg_debug_overlay_get_padding (fixture->overlay), ==, 10);

    /* Minimum of 0 */
    lrg_debug_overlay_set_padding (fixture->overlay, -5);
    g_assert_cmpint (lrg_debug_overlay_get_padding (fixture->overlay), ==, 0);
}

static void
test_overlay_custom_lines (OverlayFixture *fixture,
                           gconstpointer   user_data)
{
    g_autofree gchar *text = NULL;

    (void)user_data;

    lrg_debug_overlay_set_visible (fixture->overlay, TRUE);
    lrg_debug_overlay_set_flags (fixture->overlay, LRG_DEBUG_OVERLAY_CUSTOM);

    /* Add custom line */
    lrg_debug_overlay_set_custom_line (fixture->overlay, "Score", "%d", 1000);
    lrg_debug_overlay_set_custom_line (fixture->overlay, "Level", "%d", 5);

    text = lrg_debug_overlay_get_text (fixture->overlay);
    g_assert_nonnull (text);
    g_assert_true (g_strstr_len (text, -1, "Score") != NULL);
    g_assert_true (g_strstr_len (text, -1, "1000") != NULL);

    /* Remove custom line */
    lrg_debug_overlay_remove_custom_line (fixture->overlay, "Score");

    g_free (text);
    text = lrg_debug_overlay_get_text (fixture->overlay);
    g_assert_true (g_strstr_len (text, -1, "Score") == NULL);
    g_assert_true (g_strstr_len (text, -1, "Level") != NULL);

    /* Clear all custom lines */
    lrg_debug_overlay_clear_custom_lines (fixture->overlay);

    g_free (text);
    text = lrg_debug_overlay_get_text (fixture->overlay);
    g_assert_cmpstr (text, ==, "");
}

static void
test_overlay_get_text_hidden (OverlayFixture *fixture,
                              gconstpointer   user_data)
{
    g_autofree gchar *text = NULL;

    (void)user_data;

    /* Overlay is hidden by default */
    g_assert_false (lrg_debug_overlay_is_visible (fixture->overlay));

    text = lrg_debug_overlay_get_text (fixture->overlay);
    g_assert_cmpstr (text, ==, "");
}

static void
test_overlay_get_text_visible (OverlayFixture *fixture,
                               gconstpointer   user_data)
{
    g_autofree gchar *text = NULL;

    (void)user_data;

    lrg_debug_overlay_set_visible (fixture->overlay, TRUE);
    lrg_debug_overlay_set_flags (fixture->overlay, LRG_DEBUG_OVERLAY_FPS);

    text = lrg_debug_overlay_get_text (fixture->overlay);
    g_assert_nonnull (text);
    g_assert_true (g_strstr_len (text, -1, "FPS") != NULL);
}

static void
test_overlay_line_count (OverlayFixture *fixture,
                         gconstpointer   user_data)
{
    guint count;

    (void)user_data;

    /* Hidden */
    count = lrg_debug_overlay_get_line_count (fixture->overlay);
    g_assert_cmpuint (count, ==, 0);

    /* Visible with FPS + Frame time */
    lrg_debug_overlay_set_visible (fixture->overlay, TRUE);
    lrg_debug_overlay_set_flags (fixture->overlay,
                                  LRG_DEBUG_OVERLAY_FPS | LRG_DEBUG_OVERLAY_FRAME_TIME);

    count = lrg_debug_overlay_get_line_count (fixture->overlay);
    g_assert_cmpuint (count, ==, 2);
}

/* ==========================================================================
 * Inspector Tests
 * ========================================================================== */

static void
test_inspector_new (InspectorFixture *fixture,
                    gconstpointer     user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_INSPECTOR (fixture->inspector));
}

static void
test_inspector_get_default (void)
{
    LrgInspector *inspector1;
    LrgInspector *inspector2;

    inspector1 = lrg_inspector_get_default ();
    g_assert_nonnull (inspector1);

    inspector2 = lrg_inspector_get_default ();
    g_assert_true (inspector1 == inspector2);
}

static void
test_inspector_visibility (InspectorFixture *fixture,
                           gconstpointer     user_data)
{
    (void)user_data;

    /* Default is hidden */
    g_assert_false (lrg_inspector_is_visible (fixture->inspector));

    /* Show */
    lrg_inspector_set_visible (fixture->inspector, TRUE);
    g_assert_true (lrg_inspector_is_visible (fixture->inspector));

    /* Hide */
    lrg_inspector_set_visible (fixture->inspector, FALSE);
    g_assert_false (lrg_inspector_is_visible (fixture->inspector));

    /* Toggle */
    lrg_inspector_toggle (fixture->inspector);
    g_assert_true (lrg_inspector_is_visible (fixture->inspector));
}

static void
test_inspector_world (InspectorFixture *fixture,
                      gconstpointer     user_data)
{
    (void)user_data;

    /* Initially no world */
    g_assert_null (lrg_inspector_get_world (fixture->inspector));

    /* Set world */
    lrg_inspector_set_world (fixture->inspector, fixture->world);
    g_assert_true (lrg_inspector_get_world (fixture->inspector) == fixture->world);

    /* Object count */
    g_assert_cmpuint (lrg_inspector_get_object_count (fixture->inspector), ==, 1);

    /* Clear world */
    lrg_inspector_set_world (fixture->inspector, NULL);
    g_assert_null (lrg_inspector_get_world (fixture->inspector));
    g_assert_cmpuint (lrg_inspector_get_object_count (fixture->inspector), ==, 0);
}

static void
test_inspector_select_object (InspectorFixture *fixture,
                              gconstpointer     user_data)
{
    GList *objects;

    (void)user_data;

    lrg_inspector_set_world (fixture->inspector, fixture->world);

    /* No selection initially */
    g_assert_null (lrg_inspector_get_selected_object (fixture->inspector));

    /* Get objects */
    objects = lrg_inspector_get_objects (fixture->inspector);
    g_assert_nonnull (objects);
    g_assert_cmpuint (g_list_length (objects), ==, 1);

    /* Select object */
    lrg_inspector_select_object (fixture->inspector, fixture->object);
    g_assert_true (lrg_inspector_get_selected_object (fixture->inspector) == fixture->object);

    /* Select by index */
    lrg_inspector_clear_selection (fixture->inspector);
    g_assert_null (lrg_inspector_get_selected_object (fixture->inspector));

    g_assert_true (lrg_inspector_select_object_at (fixture->inspector, 0));
    g_assert_nonnull (lrg_inspector_get_selected_object (fixture->inspector));

    /* Invalid index */
    g_assert_false (lrg_inspector_select_object_at (fixture->inspector, 999));

    g_list_free (objects);
}

static void
test_inspector_select_component (InspectorFixture *fixture,
                                 gconstpointer     user_data)
{
    GList *components;

    (void)user_data;

    lrg_inspector_set_world (fixture->inspector, fixture->world);
    lrg_inspector_select_object (fixture->inspector, fixture->object);

    /* No component selection initially */
    g_assert_null (lrg_inspector_get_selected_component (fixture->inspector));

    /* Get components */
    components = lrg_inspector_get_components (fixture->inspector);
    g_assert_nonnull (components);
    g_assert_cmpuint (g_list_length (components), ==, 1);

    /* Component count */
    g_assert_cmpuint (lrg_inspector_get_component_count (fixture->inspector), ==, 1);

    /* Select component */
    lrg_inspector_select_component (fixture->inspector, fixture->component);
    g_assert_true (lrg_inspector_get_selected_component (fixture->inspector) == fixture->component);

    /* Select by index */
    lrg_inspector_clear_selection (fixture->inspector);
    lrg_inspector_select_object (fixture->inspector, fixture->object);
    g_assert_true (lrg_inspector_select_component_at (fixture->inspector, 0));
    g_assert_nonnull (lrg_inspector_get_selected_component (fixture->inspector));

    g_list_free (components);
}

static void
test_inspector_get_properties (InspectorFixture *fixture,
                               gconstpointer     user_data)
{
    GParamSpec **props;
    guint        n_props;

    (void)user_data;

    /* Get properties of the component */
    props = lrg_inspector_get_properties (fixture->inspector,
                                          G_OBJECT (fixture->component),
                                          &n_props);
    g_assert_nonnull (props);
    g_assert_cmpuint (n_props, >, 0);

    g_free (props);
}

static void
test_inspector_property_introspection (InspectorFixture *fixture,
                                       gconstpointer     user_data)
{
    GValue  value = G_VALUE_INIT;
    gboolean result;
    g_autofree gchar *str = NULL;

    (void)user_data;

    /* Get a property value from game object (has "tag" property) */
    result = lrg_inspector_get_property_value (fixture->inspector,
                                               G_OBJECT (fixture->object),
                                               "tag",
                                               &value);
    g_assert_true (result);
    g_assert_true (G_VALUE_HOLDS_STRING (&value));
    g_assert_cmpstr (g_value_get_string (&value), ==, "test-object");
    g_value_unset (&value);

    /* Get property as string */
    str = lrg_inspector_get_property_string (fixture->inspector,
                                             G_OBJECT (fixture->object),
                                             "tag");
    g_assert_nonnull (str);
    g_assert_true (g_strstr_len (str, -1, "test-object") != NULL);
}

static void
test_inspector_text_output (InspectorFixture *fixture,
                            gconstpointer     user_data)
{
    g_autofree gchar *world_info = NULL;
    g_autofree gchar *object_info = NULL;
    g_autofree gchar *object_list = NULL;

    (void)user_data;

    lrg_inspector_set_world (fixture->inspector, fixture->world);
    lrg_inspector_select_object (fixture->inspector, fixture->object);

    /* World info */
    world_info = lrg_inspector_get_world_info (fixture->inspector);
    g_assert_nonnull (world_info);
    g_assert_true (g_strstr_len (world_info, -1, "1") != NULL);  /* 1 object */

    /* Object info */
    object_info = lrg_inspector_get_object_info (fixture->inspector);
    g_assert_nonnull (object_info);
    g_assert_true (g_strstr_len (object_info, -1, "test-object") != NULL);

    /* Object list */
    object_list = lrg_inspector_get_object_list (fixture->inspector);
    g_assert_nonnull (object_list);
    g_assert_true (g_strstr_len (object_list, -1, "test-object") != NULL);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Profiler Sample */
    g_test_add_func ("/debug/profiler-sample/copy-null",
                     test_profiler_sample_copy_null);
    g_test_add_func ("/debug/profiler-sample/free-null",
                     test_profiler_sample_free_null);

    /* Profiler */
    g_test_add ("/debug/profiler/new", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_new,
                profiler_fixture_tear_down);
    g_test_add_func ("/debug/profiler/get-default",
                     test_profiler_get_default);
    g_test_add ("/debug/profiler/enabled", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_enabled,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/max-samples", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_max_samples,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/section-timing", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_section_timing,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/section-disabled", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_section_disabled,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/frame-timing", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_frame_timing,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/statistics", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_statistics,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/get-last-sample", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_get_last_sample,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/clear", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_clear,
                profiler_fixture_tear_down);
    g_test_add ("/debug/profiler/clear-section", ProfilerFixture, NULL,
                profiler_fixture_set_up, test_profiler_clear_section,
                profiler_fixture_tear_down);

    /* Console Output */
    g_test_add_func ("/debug/console-output/copy-null",
                     test_console_output_copy_null);
    g_test_add_func ("/debug/console-output/free-null",
                     test_console_output_free_null);

    /* Console */
    g_test_add ("/debug/console/new", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_new,
                console_fixture_tear_down);
    g_test_add_func ("/debug/console/get-default",
                     test_console_get_default);
    g_test_add ("/debug/console/visibility", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_visibility,
                console_fixture_tear_down);
    g_test_add ("/debug/console/max-history", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_max_history,
                console_fixture_tear_down);
    g_test_add ("/debug/console/builtin-commands", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_builtin_commands,
                console_fixture_tear_down);
    g_test_add ("/debug/console/register-command", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_register_command,
                console_fixture_tear_down);
    g_test_add ("/debug/console/unregister-command", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_unregister_command,
                console_fixture_tear_down);
    g_test_add ("/debug/console/execute-echo", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_execute_echo,
                console_fixture_tear_down);
    g_test_add ("/debug/console/execute-unknown", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_execute_unknown,
                console_fixture_tear_down);
    g_test_add ("/debug/console/print", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_print,
                console_fixture_tear_down);
    g_test_add ("/debug/console/print-error", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_print_error,
                console_fixture_tear_down);
    g_test_add ("/debug/console/printf", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_printf,
                console_fixture_tear_down);
    g_test_add ("/debug/console/history", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_history,
                console_fixture_tear_down);
    g_test_add ("/debug/console/clear", ConsoleFixture, NULL,
                console_fixture_set_up, test_console_clear,
                console_fixture_tear_down);

    /* Overlay */
    g_test_add ("/debug/overlay/new", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_new,
                overlay_fixture_tear_down);
    g_test_add_func ("/debug/overlay/get-default",
                     test_overlay_get_default);
    g_test_add ("/debug/overlay/visibility", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_visibility,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/flags", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_flags,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/position", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_position,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/font-size", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_font_size,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/padding", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_padding,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/custom-lines", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_custom_lines,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/get-text-hidden", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_get_text_hidden,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/get-text-visible", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_get_text_visible,
                overlay_fixture_tear_down);
    g_test_add ("/debug/overlay/line-count", OverlayFixture, NULL,
                overlay_fixture_set_up, test_overlay_line_count,
                overlay_fixture_tear_down);

    /* Inspector */
    g_test_add ("/debug/inspector/new", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_new,
                inspector_fixture_tear_down);
    g_test_add_func ("/debug/inspector/get-default",
                     test_inspector_get_default);
    g_test_add ("/debug/inspector/visibility", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_visibility,
                inspector_fixture_tear_down);
    g_test_add ("/debug/inspector/world", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_world,
                inspector_fixture_tear_down);
    g_test_add ("/debug/inspector/select-object", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_select_object,
                inspector_fixture_tear_down);
    g_test_add ("/debug/inspector/select-component", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_select_component,
                inspector_fixture_tear_down);
    g_test_add ("/debug/inspector/get-properties", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_get_properties,
                inspector_fixture_tear_down);
    g_test_add ("/debug/inspector/property-introspection", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_property_introspection,
                inspector_fixture_tear_down);
    g_test_add ("/debug/inspector/text-output", InspectorFixture, NULL,
                inspector_fixture_set_up, test_inspector_text_output,
                inspector_fixture_tear_down);

    return g_test_run ();
}
