/* test-accessibility.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgAccessibilitySettings and LrgColorFilter.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgAccessibilitySettings *settings;
} AccessibilityFixture;

static void
accessibility_fixture_set_up (AccessibilityFixture *fixture,
                              gconstpointer         user_data)
{
    fixture->settings = lrg_accessibility_settings_new ();
    g_assert_nonnull (fixture->settings);
}

static void
accessibility_fixture_tear_down (AccessibilityFixture *fixture,
                                 gconstpointer         user_data)
{
    g_clear_object (&fixture->settings);
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_accessibility_settings_new (void)
{
    g_autoptr(LrgAccessibilitySettings) settings = NULL;

    settings = lrg_accessibility_settings_new ();

    g_assert_nonnull (settings);
    g_assert_true (LRG_IS_ACCESSIBILITY_SETTINGS (settings));

    /* Should also be a settings group */
    g_assert_true (LRG_IS_SETTINGS_GROUP (settings));
}

static void
test_accessibility_settings_group_name (AccessibilityFixture *fixture,
                                        gconstpointer         user_data)
{
    const gchar *name;

    name = lrg_settings_group_get_group_name (LRG_SETTINGS_GROUP (fixture->settings));
    g_assert_cmpstr (name, ==, "accessibility");
}

/* ==========================================================================
 * Test Cases - Visual Settings
 * ========================================================================== */

static void
test_accessibility_colorblind_mode (AccessibilityFixture *fixture,
                                    gconstpointer         user_data)
{
    LrgColorblindType type;

    /* Default should be NONE */
    type = lrg_accessibility_settings_get_colorblind_type (fixture->settings);
    g_assert_cmpint (type, ==, LRG_COLORBLIND_NONE);

    /* Test all types */
    lrg_accessibility_settings_set_colorblind_type (fixture->settings, LRG_COLORBLIND_DEUTERANOPIA);
    type = lrg_accessibility_settings_get_colorblind_type (fixture->settings);
    g_assert_cmpint (type, ==, LRG_COLORBLIND_DEUTERANOPIA);

    lrg_accessibility_settings_set_colorblind_type (fixture->settings, LRG_COLORBLIND_PROTANOPIA);
    type = lrg_accessibility_settings_get_colorblind_type (fixture->settings);
    g_assert_cmpint (type, ==, LRG_COLORBLIND_PROTANOPIA);

    lrg_accessibility_settings_set_colorblind_type (fixture->settings, LRG_COLORBLIND_TRITANOPIA);
    type = lrg_accessibility_settings_get_colorblind_type (fixture->settings);
    g_assert_cmpint (type, ==, LRG_COLORBLIND_TRITANOPIA);

    lrg_accessibility_settings_set_colorblind_type (fixture->settings, LRG_COLORBLIND_NONE);
    type = lrg_accessibility_settings_get_colorblind_type (fixture->settings);
    g_assert_cmpint (type, ==, LRG_COLORBLIND_NONE);
}

static void
test_accessibility_high_contrast (AccessibilityFixture *fixture,
                                  gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_high_contrast (fixture->settings));

    lrg_accessibility_settings_set_high_contrast (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_high_contrast (fixture->settings));

    lrg_accessibility_settings_set_high_contrast (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_high_contrast (fixture->settings));
}

static void
test_accessibility_ui_scale (AccessibilityFixture *fixture,
                             gconstpointer         user_data)
{
    gfloat scale;

    /* Default should be 1.0 */
    scale = lrg_accessibility_settings_get_ui_scale (fixture->settings);
    g_assert_cmpfloat_with_epsilon (scale, 1.0f, 0.001f);

    /* Test setting various values */
    lrg_accessibility_settings_set_ui_scale (fixture->settings, 1.5f);
    scale = lrg_accessibility_settings_get_ui_scale (fixture->settings);
    g_assert_cmpfloat_with_epsilon (scale, 1.5f, 0.001f);

    lrg_accessibility_settings_set_ui_scale (fixture->settings, 2.0f);
    scale = lrg_accessibility_settings_get_ui_scale (fixture->settings);
    g_assert_cmpfloat_with_epsilon (scale, 2.0f, 0.001f);

    lrg_accessibility_settings_set_ui_scale (fixture->settings, 0.5f);
    scale = lrg_accessibility_settings_get_ui_scale (fixture->settings);
    g_assert_cmpfloat_with_epsilon (scale, 0.5f, 0.001f);
}

static void
test_accessibility_reduce_motion (AccessibilityFixture *fixture,
                                  gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_reduce_motion (fixture->settings));

    lrg_accessibility_settings_set_reduce_motion (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_reduce_motion (fixture->settings));

    lrg_accessibility_settings_set_reduce_motion (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_reduce_motion (fixture->settings));
}

static void
test_accessibility_screen_shake (AccessibilityFixture *fixture,
                                 gconstpointer         user_data)
{
    gfloat intensity;

    /* Default should be 1.0 (full intensity) */
    intensity = lrg_accessibility_settings_get_screen_shake_intensity (fixture->settings);
    g_assert_cmpfloat_with_epsilon (intensity, 1.0f, 0.001f);

    lrg_accessibility_settings_set_screen_shake_intensity (fixture->settings, 0.5f);
    intensity = lrg_accessibility_settings_get_screen_shake_intensity (fixture->settings);
    g_assert_cmpfloat_with_epsilon (intensity, 0.5f, 0.001f);

    lrg_accessibility_settings_set_screen_shake_intensity (fixture->settings, 0.0f);
    intensity = lrg_accessibility_settings_get_screen_shake_intensity (fixture->settings);
    g_assert_cmpfloat_with_epsilon (intensity, 0.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - Audio Settings
 * ========================================================================== */

static void
test_accessibility_subtitles (AccessibilityFixture *fixture,
                              gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_subtitles_enabled (fixture->settings));

    lrg_accessibility_settings_set_subtitles_enabled (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_subtitles_enabled (fixture->settings));

    lrg_accessibility_settings_set_subtitles_enabled (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_subtitles_enabled (fixture->settings));
}

static void
test_accessibility_closed_captions (AccessibilityFixture *fixture,
                                    gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_closed_captions (fixture->settings));

    lrg_accessibility_settings_set_closed_captions (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_closed_captions (fixture->settings));

    lrg_accessibility_settings_set_closed_captions (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_closed_captions (fixture->settings));
}

static void
test_accessibility_subtitle_size (AccessibilityFixture *fixture,
                                  gconstpointer         user_data)
{
    gfloat size;

    /* Default should be 1.0 */
    size = lrg_accessibility_settings_get_subtitle_size (fixture->settings);
    g_assert_cmpfloat_with_epsilon (size, 1.0f, 0.001f);

    lrg_accessibility_settings_set_subtitle_size (fixture->settings, 1.5f);
    size = lrg_accessibility_settings_get_subtitle_size (fixture->settings);
    g_assert_cmpfloat_with_epsilon (size, 1.5f, 0.001f);

    lrg_accessibility_settings_set_subtitle_size (fixture->settings, 2.0f);
    size = lrg_accessibility_settings_get_subtitle_size (fixture->settings);
    g_assert_cmpfloat_with_epsilon (size, 2.0f, 0.001f);
}

static void
test_accessibility_subtitle_background (AccessibilityFixture *fixture,
                                        gconstpointer         user_data)
{
    gfloat opacity;

    /* Default should be 0.75 (semi-transparent) */
    opacity = lrg_accessibility_settings_get_subtitle_background (fixture->settings);
    g_assert_cmpfloat_with_epsilon (opacity, 0.75f, 0.001f);

    lrg_accessibility_settings_set_subtitle_background (fixture->settings, 1.0f);
    opacity = lrg_accessibility_settings_get_subtitle_background (fixture->settings);
    g_assert_cmpfloat_with_epsilon (opacity, 1.0f, 0.001f);

    lrg_accessibility_settings_set_subtitle_background (fixture->settings, 0.0f);
    opacity = lrg_accessibility_settings_get_subtitle_background (fixture->settings);
    g_assert_cmpfloat_with_epsilon (opacity, 0.0f, 0.001f);
}

static void
test_accessibility_visual_audio_cues (AccessibilityFixture *fixture,
                                      gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_visual_audio_cues (fixture->settings));

    lrg_accessibility_settings_set_visual_audio_cues (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_visual_audio_cues (fixture->settings));

    lrg_accessibility_settings_set_visual_audio_cues (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_visual_audio_cues (fixture->settings));
}

/* ==========================================================================
 * Test Cases - Motor Settings
 * ========================================================================== */

static void
test_accessibility_hold_to_toggle (AccessibilityFixture *fixture,
                                   gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_hold_to_toggle (fixture->settings));

    lrg_accessibility_settings_set_hold_to_toggle (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_hold_to_toggle (fixture->settings));

    lrg_accessibility_settings_set_hold_to_toggle (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_hold_to_toggle (fixture->settings));
}

static void
test_accessibility_auto_aim (AccessibilityFixture *fixture,
                             gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_auto_aim (fixture->settings));

    lrg_accessibility_settings_set_auto_aim (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_auto_aim (fixture->settings));

    lrg_accessibility_settings_set_auto_aim (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_auto_aim (fixture->settings));
}

static void
test_accessibility_input_timing (AccessibilityFixture *fixture,
                                 gconstpointer         user_data)
{
    gfloat multiplier;

    /* Default should be 1.0 */
    multiplier = lrg_accessibility_settings_get_input_timing_multiplier (fixture->settings);
    g_assert_cmpfloat_with_epsilon (multiplier, 1.0f, 0.001f);

    lrg_accessibility_settings_set_input_timing_multiplier (fixture->settings, 2.0f);
    multiplier = lrg_accessibility_settings_get_input_timing_multiplier (fixture->settings);
    g_assert_cmpfloat_with_epsilon (multiplier, 2.0f, 0.001f);

    lrg_accessibility_settings_set_input_timing_multiplier (fixture->settings, 3.0f);
    multiplier = lrg_accessibility_settings_get_input_timing_multiplier (fixture->settings);
    g_assert_cmpfloat_with_epsilon (multiplier, 3.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - Cognitive Settings
 * ========================================================================== */

static void
test_accessibility_objective_reminders (AccessibilityFixture *fixture,
                                        gconstpointer         user_data)
{
    /* Default should be TRUE */
    g_assert_true (lrg_accessibility_settings_get_objective_reminders (fixture->settings));

    lrg_accessibility_settings_set_objective_reminders (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_objective_reminders (fixture->settings));

    lrg_accessibility_settings_set_objective_reminders (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_objective_reminders (fixture->settings));
}

static void
test_accessibility_skip_cutscenes (AccessibilityFixture *fixture,
                                   gconstpointer         user_data)
{
    /* Default should be TRUE (accessibility-friendly default) */
    g_assert_true (lrg_accessibility_settings_get_skip_cutscenes (fixture->settings));

    lrg_accessibility_settings_set_skip_cutscenes (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_skip_cutscenes (fixture->settings));

    lrg_accessibility_settings_set_skip_cutscenes (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_skip_cutscenes (fixture->settings));
}

static void
test_accessibility_pause_during_cutscenes (AccessibilityFixture *fixture,
                                           gconstpointer         user_data)
{
    /* Default should be TRUE */
    g_assert_true (lrg_accessibility_settings_get_pause_during_cutscenes (fixture->settings));

    lrg_accessibility_settings_set_pause_during_cutscenes (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_pause_during_cutscenes (fixture->settings));

    lrg_accessibility_settings_set_pause_during_cutscenes (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_pause_during_cutscenes (fixture->settings));
}

/* ==========================================================================
 * Test Cases - Screen Reader Settings
 * ========================================================================== */

static void
test_accessibility_screen_reader_enabled (AccessibilityFixture *fixture,
                                          gconstpointer         user_data)
{
    /* Default should be FALSE */
    g_assert_false (lrg_accessibility_settings_get_screen_reader_enabled (fixture->settings));

    lrg_accessibility_settings_set_screen_reader_enabled (fixture->settings, TRUE);
    g_assert_true (lrg_accessibility_settings_get_screen_reader_enabled (fixture->settings));

    lrg_accessibility_settings_set_screen_reader_enabled (fixture->settings, FALSE);
    g_assert_false (lrg_accessibility_settings_get_screen_reader_enabled (fixture->settings));
}

static void
test_accessibility_screen_reader_rate (AccessibilityFixture *fixture,
                                       gconstpointer         user_data)
{
    gfloat rate;

    /* Default should be 1.0 */
    rate = lrg_accessibility_settings_get_screen_reader_rate (fixture->settings);
    g_assert_cmpfloat_with_epsilon (rate, 1.0f, 0.001f);

    lrg_accessibility_settings_set_screen_reader_rate (fixture->settings, 0.5f);
    rate = lrg_accessibility_settings_get_screen_reader_rate (fixture->settings);
    g_assert_cmpfloat_with_epsilon (rate, 0.5f, 0.001f);

    lrg_accessibility_settings_set_screen_reader_rate (fixture->settings, 2.0f);
    rate = lrg_accessibility_settings_get_screen_reader_rate (fixture->settings);
    g_assert_cmpfloat_with_epsilon (rate, 2.0f, 0.001f);
}

/* ==========================================================================
 * Test Cases - Color Filter Base Class
 * ========================================================================== */

static void
test_color_filter_type (void)
{
    GType filter_type;

    filter_type = lrg_color_filter_get_type ();
    g_assert_cmpuint (filter_type, !=, G_TYPE_INVALID);

    /* Should be derivable */
    g_assert_true (G_TYPE_IS_DERIVABLE (filter_type));
}

/* ==========================================================================
 * Test Cases - Serialization
 * ========================================================================== */

static void
test_accessibility_serialize (AccessibilityFixture *fixture,
                              gconstpointer         user_data)
{
    g_autoptr(GVariant) variant = NULL;
    g_autoptr(GError)   error = NULL;

    /* Set some values */
    lrg_accessibility_settings_set_colorblind_type (fixture->settings, LRG_COLORBLIND_DEUTERANOPIA);
    lrg_accessibility_settings_set_ui_scale (fixture->settings, 1.5f);
    lrg_accessibility_settings_set_subtitles_enabled (fixture->settings, TRUE);
    lrg_accessibility_settings_set_auto_aim (fixture->settings, TRUE);

    variant = lrg_settings_group_serialize (LRG_SETTINGS_GROUP (fixture->settings), &error);

    g_assert_no_error (error);
    g_assert_nonnull (variant);
    g_assert_true (g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT));
}

static void
test_accessibility_deserialize (AccessibilityFixture *fixture,
                                gconstpointer         user_data)
{
    g_autoptr(GVariant) variant = NULL;
    g_autoptr(GError)   error = NULL;
    GVariantBuilder     builder;

    /* Build a variant with some settings */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&builder, "{sv}", "colorblind-mode",
                           g_variant_new_int32 (LRG_COLORBLIND_TRITANOPIA));
    g_variant_builder_add (&builder, "{sv}", "high-contrast",
                           g_variant_new_boolean (TRUE));
    g_variant_builder_add (&builder, "{sv}", "ui-scale",
                           g_variant_new_double (1.75));
    g_variant_builder_add (&builder, "{sv}", "subtitles-enabled",
                           g_variant_new_boolean (TRUE));
    variant = g_variant_builder_end (&builder);

    g_assert_true (lrg_settings_group_deserialize (LRG_SETTINGS_GROUP (fixture->settings),
                                                   variant, &error));
    g_assert_no_error (error);

    /* Verify values were loaded */
    g_assert_cmpint (lrg_accessibility_settings_get_colorblind_type (fixture->settings),
                     ==, LRG_COLORBLIND_TRITANOPIA);
    g_assert_true (lrg_accessibility_settings_get_high_contrast (fixture->settings));
    g_assert_cmpfloat_with_epsilon (lrg_accessibility_settings_get_ui_scale (fixture->settings),
                                    1.75f, 0.01f);
    g_assert_true (lrg_accessibility_settings_get_subtitles_enabled (fixture->settings));
}

/* ==========================================================================
 * Test Cases - Reset
 * ========================================================================== */

static void
test_accessibility_reset (AccessibilityFixture *fixture,
                          gconstpointer         user_data)
{
    /* Modify all settings */
    lrg_accessibility_settings_set_colorblind_type (fixture->settings, LRG_COLORBLIND_PROTANOPIA);
    lrg_accessibility_settings_set_high_contrast (fixture->settings, TRUE);
    lrg_accessibility_settings_set_ui_scale (fixture->settings, 2.0f);
    lrg_accessibility_settings_set_subtitles_enabled (fixture->settings, TRUE);
    lrg_accessibility_settings_set_auto_aim (fixture->settings, TRUE);

    /* Reset */
    lrg_settings_group_reset (LRG_SETTINGS_GROUP (fixture->settings));

    /* Verify defaults */
    g_assert_cmpint (lrg_accessibility_settings_get_colorblind_type (fixture->settings),
                     ==, LRG_COLORBLIND_NONE);
    g_assert_false (lrg_accessibility_settings_get_high_contrast (fixture->settings));
    g_assert_cmpfloat_with_epsilon (lrg_accessibility_settings_get_ui_scale (fixture->settings),
                                    1.0f, 0.001f);
    g_assert_false (lrg_accessibility_settings_get_subtitles_enabled (fixture->settings));
    g_assert_false (lrg_accessibility_settings_get_auto_aim (fixture->settings));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction */
    g_test_add_func ("/accessibility/new", test_accessibility_settings_new);

    g_test_add ("/accessibility/group-name",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_settings_group_name,
                accessibility_fixture_tear_down);

    /* Visual Settings */
    g_test_add ("/accessibility/visual/colorblind-mode",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_colorblind_mode,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/visual/high-contrast",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_high_contrast,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/visual/ui-scale",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_ui_scale,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/visual/reduce-motion",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_reduce_motion,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/visual/screen-shake",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_screen_shake,
                accessibility_fixture_tear_down);

    /* Audio Settings */
    g_test_add ("/accessibility/audio/subtitles",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_subtitles,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/audio/closed-captions",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_closed_captions,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/audio/subtitle-size",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_subtitle_size,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/audio/subtitle-background",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_subtitle_background,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/audio/visual-audio-cues",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_visual_audio_cues,
                accessibility_fixture_tear_down);

    /* Motor Settings */
    g_test_add ("/accessibility/motor/hold-to-toggle",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_hold_to_toggle,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/motor/auto-aim",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_auto_aim,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/motor/input-timing",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_input_timing,
                accessibility_fixture_tear_down);

    /* Cognitive Settings */
    g_test_add ("/accessibility/cognitive/objective-reminders",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_objective_reminders,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/cognitive/skip-cutscenes",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_skip_cutscenes,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/cognitive/pause-during-cutscenes",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_pause_during_cutscenes,
                accessibility_fixture_tear_down);

    /* Screen Reader */
    g_test_add ("/accessibility/screen-reader/enabled",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_screen_reader_enabled,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/screen-reader/rate",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_screen_reader_rate,
                accessibility_fixture_tear_down);

    /* Color Filter */
    g_test_add_func ("/accessibility/color-filter/type", test_color_filter_type);

    /* Serialization */
    g_test_add ("/accessibility/serialize",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_serialize,
                accessibility_fixture_tear_down);

    g_test_add ("/accessibility/deserialize",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_deserialize,
                accessibility_fixture_tear_down);

    /* Reset */
    g_test_add ("/accessibility/reset",
                AccessibilityFixture, NULL,
                accessibility_fixture_set_up,
                test_accessibility_reset,
                accessibility_fixture_tear_down);

    return g_test_run ();
}
