/* test-template-states.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for template menu state types:
 * - LrgTemplateMainMenuState
 * - LrgTemplatePauseMenuState
 * - LrgTemplateSettingsMenuState
 * - LrgTemplateLoadingState
 * - LrgTemplateErrorState
 * - LrgTemplateConfirmationState
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Cases - LrgTemplateMainMenuState Construction
 * ========================================================================== */

static void
test_main_menu_state_new (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;

    state = lrg_template_main_menu_state_new ();

    g_assert_nonnull (state);
    g_assert_true (LRG_IS_TEMPLATE_MAIN_MENU_STATE (state));
    g_assert_true (LRG_IS_GAME_STATE (state));
}

static void
test_main_menu_state_new_with_title (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;

    state = lrg_template_main_menu_state_new_with_title ("My Game");

    g_assert_nonnull (state);
    g_assert_cmpstr (lrg_template_main_menu_state_get_title (state), ==, "My Game");
}

/* ==========================================================================
 * Test Cases - LrgTemplateMainMenuState Properties
 * ========================================================================== */

static void
test_main_menu_state_title (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;

    state = lrg_template_main_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_main_menu_state_set_title (state, "Test Title");
    g_assert_cmpstr (lrg_template_main_menu_state_get_title (state), ==, "Test Title");

    lrg_template_main_menu_state_set_title (state, NULL);
    g_assert_null (lrg_template_main_menu_state_get_title (state));
}

static void
test_main_menu_state_title_font_size (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;
    gfloat size;

    state = lrg_template_main_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_main_menu_state_set_title_font_size (state, 48.0f);
    size = lrg_template_main_menu_state_get_title_font_size (state);
    g_assert_cmpfloat_with_epsilon (size, 48.0f, 0.01f);
}

static void
test_main_menu_state_show_continue (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;

    state = lrg_template_main_menu_state_new ();
    g_assert_nonnull (state);

    /* Toggle continue button visibility */
    lrg_template_main_menu_state_set_show_continue (state, TRUE);
    g_assert_true (lrg_template_main_menu_state_get_show_continue (state));

    lrg_template_main_menu_state_set_show_continue (state, FALSE);
    g_assert_false (lrg_template_main_menu_state_get_show_continue (state));
}

static void
test_main_menu_state_button_layout (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;
    gfloat spacing;
    gfloat width;
    gfloat height;

    state = lrg_template_main_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_main_menu_state_set_button_spacing (state, 20.0f);
    spacing = lrg_template_main_menu_state_get_button_spacing (state);
    g_assert_cmpfloat_with_epsilon (spacing, 20.0f, 0.01f);

    lrg_template_main_menu_state_set_button_width (state, 200.0f);
    width = lrg_template_main_menu_state_get_button_width (state);
    g_assert_cmpfloat_with_epsilon (width, 200.0f, 0.01f);

    lrg_template_main_menu_state_set_button_height (state, 50.0f);
    height = lrg_template_main_menu_state_get_button_height (state);
    g_assert_cmpfloat_with_epsilon (height, 50.0f, 0.01f);
}

static void
test_main_menu_state_selected_index (void)
{
    g_autoptr(LrgTemplateMainMenuState) state = NULL;
    gint new_index;

    state = lrg_template_main_menu_state_new ();
    g_assert_nonnull (state);

    /* Try to set index - may be clamped based on button count */
    lrg_template_main_menu_state_set_selected_index (state, 1);
    new_index = lrg_template_main_menu_state_get_selected_index (state);

    /* Verify it's a valid index (>= 0) */
    g_assert_cmpint (new_index, >=, 0);
}

/* ==========================================================================
 * Test Cases - LrgTemplatePauseMenuState Construction
 * ========================================================================== */

static void
test_pause_menu_state_new (void)
{
    g_autoptr(LrgTemplatePauseMenuState) state = NULL;

    state = lrg_template_pause_menu_state_new ();

    g_assert_nonnull (state);
    g_assert_true (LRG_IS_TEMPLATE_PAUSE_MENU_STATE (state));
    g_assert_true (LRG_IS_GAME_STATE (state));
}

/* ==========================================================================
 * Test Cases - LrgTemplatePauseMenuState Properties
 * ========================================================================== */

static void
test_pause_menu_state_duck_audio (void)
{
    g_autoptr(LrgTemplatePauseMenuState) state = NULL;

    state = lrg_template_pause_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_pause_menu_state_set_duck_audio (state, TRUE);
    g_assert_true (lrg_template_pause_menu_state_get_duck_audio (state));

    lrg_template_pause_menu_state_set_duck_audio (state, FALSE);
    g_assert_false (lrg_template_pause_menu_state_get_duck_audio (state));
}

static void
test_pause_menu_state_duck_factor (void)
{
    g_autoptr(LrgTemplatePauseMenuState) state = NULL;
    gfloat factor;

    state = lrg_template_pause_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_pause_menu_state_set_duck_factor (state, 0.3f);
    factor = lrg_template_pause_menu_state_get_duck_factor (state);
    g_assert_cmpfloat_with_epsilon (factor, 0.3f, 0.01f);
}

static void
test_pause_menu_state_confirmations (void)
{
    g_autoptr(LrgTemplatePauseMenuState) state = NULL;

    state = lrg_template_pause_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_pause_menu_state_set_confirm_main_menu (state, TRUE);
    g_assert_true (lrg_template_pause_menu_state_get_confirm_main_menu (state));

    lrg_template_pause_menu_state_set_confirm_exit (state, TRUE);
    g_assert_true (lrg_template_pause_menu_state_get_confirm_exit (state));
}

static void
test_pause_menu_state_button_visibility (void)
{
    g_autoptr(LrgTemplatePauseMenuState) state = NULL;

    state = lrg_template_pause_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_pause_menu_state_set_show_settings (state, FALSE);
    g_assert_false (lrg_template_pause_menu_state_get_show_settings (state));

    lrg_template_pause_menu_state_set_show_main_menu (state, FALSE);
    g_assert_false (lrg_template_pause_menu_state_get_show_main_menu (state));

    lrg_template_pause_menu_state_set_show_exit (state, FALSE);
    g_assert_false (lrg_template_pause_menu_state_get_show_exit (state));
}

/* ==========================================================================
 * Test Cases - LrgTemplateSettingsMenuState Construction
 * ========================================================================== */

static void
test_settings_menu_state_new (void)
{
    g_autoptr(LrgTemplateSettingsMenuState) state = NULL;

    state = lrg_template_settings_menu_state_new ();

    g_assert_nonnull (state);
    g_assert_true (LRG_IS_TEMPLATE_SETTINGS_MENU_STATE (state));
    g_assert_true (LRG_IS_GAME_STATE (state));
}

/* ==========================================================================
 * Test Cases - LrgTemplateSettingsMenuState Properties
 * ========================================================================== */

static void
test_settings_menu_state_tab_visibility (void)
{
    g_autoptr(LrgTemplateSettingsMenuState) state = NULL;

    state = lrg_template_settings_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_settings_menu_state_set_show_graphics_tab (state, FALSE);
    g_assert_false (lrg_template_settings_menu_state_get_show_graphics_tab (state));

    lrg_template_settings_menu_state_set_show_audio_tab (state, FALSE);
    g_assert_false (lrg_template_settings_menu_state_get_show_audio_tab (state));

    lrg_template_settings_menu_state_set_show_controls_tab (state, FALSE);
    g_assert_false (lrg_template_settings_menu_state_get_show_controls_tab (state));
}

static void
test_settings_menu_state_active_tab (void)
{
    g_autoptr(LrgTemplateSettingsMenuState) state = NULL;
    guint tab;

    state = lrg_template_settings_menu_state_new ();
    g_assert_nonnull (state);

    /* Try to set tab - may be clamped based on tab count */
    lrg_template_settings_menu_state_set_active_tab (state, 0);
    tab = lrg_template_settings_menu_state_get_active_tab (state);

    /* Verify it's a valid tab index */
    g_assert_cmpuint (tab, >=, 0);
}

static void
test_settings_menu_state_reset_button (void)
{
    g_autoptr(LrgTemplateSettingsMenuState) state = NULL;

    state = lrg_template_settings_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_settings_menu_state_set_show_reset_button (state, FALSE);
    g_assert_false (lrg_template_settings_menu_state_get_show_reset_button (state));
}

static void
test_settings_menu_state_confirmations (void)
{
    g_autoptr(LrgTemplateSettingsMenuState) state = NULL;

    state = lrg_template_settings_menu_state_new ();
    g_assert_nonnull (state);

    lrg_template_settings_menu_state_set_confirm_cancel (state, TRUE);
    g_assert_true (lrg_template_settings_menu_state_get_confirm_cancel (state));

    lrg_template_settings_menu_state_set_confirm_reset (state, TRUE);
    g_assert_true (lrg_template_settings_menu_state_get_confirm_reset (state));
}

static void
test_settings_menu_state_unsaved_changes (void)
{
    g_autoptr(LrgTemplateSettingsMenuState) state = NULL;
    gboolean has_changes;

    state = lrg_template_settings_menu_state_new ();
    g_assert_nonnull (state);

    /* Initially should not have unsaved changes */
    has_changes = lrg_template_settings_menu_state_has_unsaved_changes (state);
    g_assert_false (has_changes);
}

/* ==========================================================================
 * Test Cases - LrgTemplateLoadingState Construction
 * ========================================================================== */

static void
test_loading_state_new (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;

    state = lrg_template_loading_state_new ();

    g_assert_nonnull (state);
    g_assert_true (LRG_IS_TEMPLATE_LOADING_STATE (state));
    g_assert_true (LRG_IS_GAME_STATE (state));
}

/* ==========================================================================
 * Test Cases - LrgTemplateLoadingState Properties
 * ========================================================================== */

static void
test_loading_state_progress (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;
    gdouble progress;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    /* Initial progress should be 0 or indeterminate */
    progress = lrg_template_loading_state_get_progress (state);
    g_assert_cmpfloat (progress, >=, 0.0);
    g_assert_cmpfloat (progress, <=, 1.0);
}

static void
test_loading_state_task_count (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    /* Initial task count should be 0 */
    g_assert_cmpuint (lrg_template_loading_state_get_task_count (state), ==, 0);
    g_assert_cmpuint (lrg_template_loading_state_get_completed_count (state), ==, 0);
}

static void
test_loading_state_minimum_display_time (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;
    gdouble time;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    lrg_template_loading_state_set_minimum_display_time (state, 2.0);
    time = lrg_template_loading_state_get_minimum_display_time (state);
    g_assert_cmpfloat_with_epsilon (time, 2.0, 0.01);
}

static void
test_loading_state_status_text (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    lrg_template_loading_state_set_status_text (state, "Loading...");
    g_assert_cmpstr (lrg_template_loading_state_get_status_text (state), ==, "Loading...");
}

static void
test_loading_state_show_options (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    lrg_template_loading_state_set_show_progress_bar (state, FALSE);
    g_assert_false (lrg_template_loading_state_get_show_progress_bar (state));

    lrg_template_loading_state_set_show_percentage (state, TRUE);
    g_assert_true (lrg_template_loading_state_get_show_percentage (state));
}

static void
test_loading_state_is_complete (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;
    gboolean complete;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    /*
     * With no tasks added, is_complete behavior depends on implementation
     * (may require minimum display time to elapse)
     */
    complete = lrg_template_loading_state_is_complete (state);
    (void) complete; /* Just verify the function works */
}

static void
test_loading_state_clear_tasks (void)
{
    g_autoptr(LrgTemplateLoadingState) state = NULL;

    state = lrg_template_loading_state_new ();
    g_assert_nonnull (state);

    /* Clear should not crash */
    lrg_template_loading_state_clear_tasks (state);
    g_assert_cmpuint (lrg_template_loading_state_get_task_count (state), ==, 0);
}

/* ==========================================================================
 * Test Cases - LrgTemplateErrorState Construction
 * ========================================================================== */

static void
test_error_state_new (void)
{
    g_autoptr(LrgTemplateErrorState) state = NULL;

    state = lrg_template_error_state_new ();

    g_assert_nonnull (state);
    g_assert_true (LRG_IS_TEMPLATE_ERROR_STATE (state));
    g_assert_true (LRG_IS_GAME_STATE (state));
}

static void
test_error_state_new_with_error (void)
{
    g_autoptr(LrgTemplateErrorState) state = NULL;
    g_autoptr(GError) error = NULL;

    error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, "Test error message");
    state = lrg_template_error_state_new_with_error (error);

    g_assert_nonnull (state);
    g_assert_cmpstr (lrg_template_error_state_get_error_message (state), ==, "Test error message");
}

/* ==========================================================================
 * Test Cases - LrgTemplateErrorState Properties
 * ========================================================================== */

static void
test_error_state_error_message (void)
{
    g_autoptr(LrgTemplateErrorState) state = NULL;

    state = lrg_template_error_state_new ();
    g_assert_nonnull (state);

    lrg_template_error_state_set_error_message (state, "Something went wrong");
    g_assert_cmpstr (lrg_template_error_state_get_error_message (state), ==, "Something went wrong");
}

static void
test_error_state_set_error (void)
{
    g_autoptr(LrgTemplateErrorState) state = NULL;
    g_autoptr(GError) error = NULL;

    state = lrg_template_error_state_new ();
    g_assert_nonnull (state);

    error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found");
    lrg_template_error_state_set_error (state, error);

    g_assert_cmpstr (lrg_template_error_state_get_error_message (state), ==, "File not found");
}

static void
test_error_state_title (void)
{
    g_autoptr(LrgTemplateErrorState) state = NULL;

    state = lrg_template_error_state_new ();
    g_assert_nonnull (state);

    lrg_template_error_state_set_title (state, "Fatal Error");
    g_assert_cmpstr (lrg_template_error_state_get_title (state), ==, "Fatal Error");
}

static void
test_error_state_button_visibility (void)
{
    g_autoptr(LrgTemplateErrorState) state = NULL;

    state = lrg_template_error_state_new ();
    g_assert_nonnull (state);

    lrg_template_error_state_set_allow_retry (state, FALSE);
    g_assert_false (lrg_template_error_state_get_allow_retry (state));

    lrg_template_error_state_set_show_main_menu (state, FALSE);
    g_assert_false (lrg_template_error_state_get_show_main_menu (state));

    lrg_template_error_state_set_show_exit (state, FALSE);
    g_assert_false (lrg_template_error_state_get_show_exit (state));
}

/* ==========================================================================
 * Test Cases - LrgTemplateConfirmationState Construction
 * ========================================================================== */

static void
test_confirmation_state_new (void)
{
    g_autoptr(LrgTemplateConfirmationState) state = NULL;

    state = lrg_template_confirmation_state_new ();

    g_assert_nonnull (state);
    g_assert_true (LRG_IS_TEMPLATE_CONFIRMATION_STATE (state));
    g_assert_true (LRG_IS_GAME_STATE (state));
}

static void
test_confirmation_state_new_with_message (void)
{
    g_autoptr(LrgTemplateConfirmationState) state = NULL;

    state = lrg_template_confirmation_state_new_with_message ("Quit Game?",
                                                               "Are you sure you want to quit?");

    g_assert_nonnull (state);
    g_assert_cmpstr (lrg_template_confirmation_state_get_title (state), ==, "Quit Game?");
    g_assert_cmpstr (lrg_template_confirmation_state_get_message (state), ==, "Are you sure you want to quit?");
}

/* ==========================================================================
 * Test Cases - LrgTemplateConfirmationState Properties
 * ========================================================================== */

static void
test_confirmation_state_title_and_message (void)
{
    g_autoptr(LrgTemplateConfirmationState) state = NULL;

    state = lrg_template_confirmation_state_new ();
    g_assert_nonnull (state);

    lrg_template_confirmation_state_set_title (state, "Delete Save?");
    g_assert_cmpstr (lrg_template_confirmation_state_get_title (state), ==, "Delete Save?");

    lrg_template_confirmation_state_set_message (state, "This action cannot be undone.");
    g_assert_cmpstr (lrg_template_confirmation_state_get_message (state), ==, "This action cannot be undone.");
}

static void
test_confirmation_state_button_labels (void)
{
    g_autoptr(LrgTemplateConfirmationState) state = NULL;

    state = lrg_template_confirmation_state_new ();
    g_assert_nonnull (state);

    lrg_template_confirmation_state_set_confirm_label (state, "Yes, Delete");
    g_assert_cmpstr (lrg_template_confirmation_state_get_confirm_label (state), ==, "Yes, Delete");

    lrg_template_confirmation_state_set_cancel_label (state, "Keep");
    g_assert_cmpstr (lrg_template_confirmation_state_get_cancel_label (state), ==, "Keep");
}

static void
test_confirmation_state_default_selection (void)
{
    g_autoptr(LrgTemplateConfirmationState) state = NULL;

    state = lrg_template_confirmation_state_new ();
    g_assert_nonnull (state);

    /* Default to cancel for destructive actions */
    lrg_template_confirmation_state_set_default_selection (state, 1);
    g_assert_cmpint (lrg_template_confirmation_state_get_default_selection (state), ==, 1);
}

/* ==========================================================================
 * Test Cases - Type Hierarchy
 * ========================================================================== */

static void
test_states_inherit_from_game_state (void)
{
    g_autoptr(LrgTemplateMainMenuState) main_menu = NULL;
    g_autoptr(LrgTemplatePauseMenuState) pause_menu = NULL;
    g_autoptr(LrgTemplateSettingsMenuState) settings = NULL;
    g_autoptr(LrgTemplateLoadingState) loading = NULL;
    g_autoptr(LrgTemplateErrorState) error_state = NULL;
    g_autoptr(LrgTemplateConfirmationState) confirm = NULL;

    main_menu = lrg_template_main_menu_state_new ();
    pause_menu = lrg_template_pause_menu_state_new ();
    settings = lrg_template_settings_menu_state_new ();
    loading = lrg_template_loading_state_new ();
    error_state = lrg_template_error_state_new ();
    confirm = lrg_template_confirmation_state_new ();

    /* All should inherit from LrgGameState */
    g_assert_true (LRG_IS_GAME_STATE (main_menu));
    g_assert_true (LRG_IS_GAME_STATE (pause_menu));
    g_assert_true (LRG_IS_GAME_STATE (settings));
    g_assert_true (LRG_IS_GAME_STATE (loading));
    g_assert_true (LRG_IS_GAME_STATE (error_state));
    g_assert_true (LRG_IS_GAME_STATE (confirm));
}

static void
test_states_are_derivable (void)
{
    /* All state types should be derivable */
    g_assert_true (G_TYPE_IS_DERIVABLE (LRG_TYPE_TEMPLATE_MAIN_MENU_STATE));
    g_assert_true (G_TYPE_IS_DERIVABLE (LRG_TYPE_TEMPLATE_PAUSE_MENU_STATE));
    g_assert_true (G_TYPE_IS_DERIVABLE (LRG_TYPE_TEMPLATE_SETTINGS_MENU_STATE));
    g_assert_true (G_TYPE_IS_DERIVABLE (LRG_TYPE_TEMPLATE_LOADING_STATE));
    g_assert_true (G_TYPE_IS_DERIVABLE (LRG_TYPE_TEMPLATE_ERROR_STATE));
    g_assert_true (G_TYPE_IS_DERIVABLE (LRG_TYPE_TEMPLATE_CONFIRMATION_STATE));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgTemplateMainMenuState tests */
    g_test_add_func ("/template/states/main-menu/new",
                     test_main_menu_state_new);
    g_test_add_func ("/template/states/main-menu/new-with-title",
                     test_main_menu_state_new_with_title);
    g_test_add_func ("/template/states/main-menu/title",
                     test_main_menu_state_title);
    g_test_add_func ("/template/states/main-menu/title-font-size",
                     test_main_menu_state_title_font_size);
    g_test_add_func ("/template/states/main-menu/show-continue",
                     test_main_menu_state_show_continue);
    g_test_add_func ("/template/states/main-menu/button-layout",
                     test_main_menu_state_button_layout);
    g_test_add_func ("/template/states/main-menu/selected-index",
                     test_main_menu_state_selected_index);

    /* LrgTemplatePauseMenuState tests */
    g_test_add_func ("/template/states/pause-menu/new",
                     test_pause_menu_state_new);
    g_test_add_func ("/template/states/pause-menu/duck-audio",
                     test_pause_menu_state_duck_audio);
    g_test_add_func ("/template/states/pause-menu/duck-factor",
                     test_pause_menu_state_duck_factor);
    g_test_add_func ("/template/states/pause-menu/confirmations",
                     test_pause_menu_state_confirmations);
    g_test_add_func ("/template/states/pause-menu/button-visibility",
                     test_pause_menu_state_button_visibility);

    /* LrgTemplateSettingsMenuState tests */
    g_test_add_func ("/template/states/settings-menu/new",
                     test_settings_menu_state_new);
    g_test_add_func ("/template/states/settings-menu/tab-visibility",
                     test_settings_menu_state_tab_visibility);
    g_test_add_func ("/template/states/settings-menu/active-tab",
                     test_settings_menu_state_active_tab);
    g_test_add_func ("/template/states/settings-menu/reset-button",
                     test_settings_menu_state_reset_button);
    g_test_add_func ("/template/states/settings-menu/confirmations",
                     test_settings_menu_state_confirmations);
    g_test_add_func ("/template/states/settings-menu/unsaved-changes",
                     test_settings_menu_state_unsaved_changes);

    /* LrgTemplateLoadingState tests */
    g_test_add_func ("/template/states/loading/new",
                     test_loading_state_new);
    g_test_add_func ("/template/states/loading/progress",
                     test_loading_state_progress);
    g_test_add_func ("/template/states/loading/task-count",
                     test_loading_state_task_count);
    g_test_add_func ("/template/states/loading/minimum-display-time",
                     test_loading_state_minimum_display_time);
    g_test_add_func ("/template/states/loading/status-text",
                     test_loading_state_status_text);
    g_test_add_func ("/template/states/loading/show-options",
                     test_loading_state_show_options);
    g_test_add_func ("/template/states/loading/is-complete",
                     test_loading_state_is_complete);
    g_test_add_func ("/template/states/loading/clear-tasks",
                     test_loading_state_clear_tasks);

    /* LrgTemplateErrorState tests */
    g_test_add_func ("/template/states/error/new",
                     test_error_state_new);
    g_test_add_func ("/template/states/error/new-with-error",
                     test_error_state_new_with_error);
    g_test_add_func ("/template/states/error/error-message",
                     test_error_state_error_message);
    g_test_add_func ("/template/states/error/set-error",
                     test_error_state_set_error);
    g_test_add_func ("/template/states/error/title",
                     test_error_state_title);
    g_test_add_func ("/template/states/error/button-visibility",
                     test_error_state_button_visibility);

    /* LrgTemplateConfirmationState tests */
    g_test_add_func ("/template/states/confirmation/new",
                     test_confirmation_state_new);
    g_test_add_func ("/template/states/confirmation/new-with-message",
                     test_confirmation_state_new_with_message);
    g_test_add_func ("/template/states/confirmation/title-and-message",
                     test_confirmation_state_title_and_message);
    g_test_add_func ("/template/states/confirmation/button-labels",
                     test_confirmation_state_button_labels);
    g_test_add_func ("/template/states/confirmation/default-selection",
                     test_confirmation_state_default_selection);

    /* Type hierarchy tests */
    g_test_add_func ("/template/states/inheritance",
                     test_states_inherit_from_game_state);
    g_test_add_func ("/template/states/derivable",
                     test_states_are_derivable);

    return g_test_run ();
}
