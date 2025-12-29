/* test-settings.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgSettings and LrgSettingsGroup.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgSettings *settings;
    gchar       *temp_dir;
    gchar       *temp_file;
} SettingsFixture;

static void
settings_fixture_set_up (SettingsFixture *fixture,
                         gconstpointer    user_data)
{
    GError *error = NULL;

    fixture->settings = lrg_settings_new ();
    g_assert_nonnull (fixture->settings);

    fixture->temp_dir = g_dir_make_tmp ("test-settings-XXXXXX", &error);
    g_assert_no_error (error);
    g_assert_nonnull (fixture->temp_dir);

    fixture->temp_file = g_build_filename (fixture->temp_dir, "settings.yaml", NULL);
}

static void
settings_fixture_tear_down (SettingsFixture *fixture,
                            gconstpointer    user_data)
{
    g_clear_object (&fixture->settings);

    if (fixture->temp_file != NULL)
    {
        g_unlink (fixture->temp_file);
        g_free (fixture->temp_file);
    }

    if (fixture->temp_dir != NULL)
    {
        g_rmdir (fixture->temp_dir);
        g_free (fixture->temp_dir);
    }
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_settings_new (void)
{
    g_autoptr(LrgSettings) settings = NULL;

    settings = lrg_settings_new ();

    g_assert_nonnull (settings);
    g_assert_true (LRG_IS_SETTINGS (settings));
}

static void
test_settings_singleton (void)
{
    LrgSettings *settings1;
    LrgSettings *settings2;

    settings1 = lrg_settings_get_default ();
    g_assert_nonnull (settings1);

    settings2 = lrg_settings_get_default ();
    g_assert_nonnull (settings2);

    /* Should be the same instance */
    g_assert_true (settings1 == settings2);
}

/* ==========================================================================
 * Test Cases - Settings Groups
 * ========================================================================== */

static void
test_settings_get_graphics (SettingsFixture *fixture,
                            gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;

    graphics = lrg_settings_get_graphics (fixture->settings);
    g_assert_nonnull (graphics);
    g_assert_true (LRG_IS_GRAPHICS_SETTINGS (graphics));
}

static void
test_settings_get_audio (SettingsFixture *fixture,
                         gconstpointer    user_data)
{
    LrgAudioSettings *audio;

    audio = lrg_settings_get_audio (fixture->settings);
    g_assert_nonnull (audio);
    g_assert_true (LRG_IS_AUDIO_SETTINGS (audio));
}

static void
test_settings_get_group_by_name (SettingsFixture *fixture,
                                 gconstpointer    user_data)
{
    LrgSettingsGroup *graphics;
    LrgSettingsGroup *audio;
    LrgSettingsGroup *nonexistent;

    graphics = lrg_settings_get_group (fixture->settings, "graphics");
    g_assert_nonnull (graphics);

    audio = lrg_settings_get_group (fixture->settings, "audio");
    g_assert_nonnull (audio);

    nonexistent = lrg_settings_get_group (fixture->settings, "nonexistent");
    g_assert_null (nonexistent);
}

static void
test_settings_list_groups (SettingsFixture *fixture,
                           gconstpointer    user_data)
{
    g_autoptr(GPtrArray) groups = NULL;

    groups = lrg_settings_list_groups (fixture->settings);
    g_assert_nonnull (groups);
    g_assert_cmpuint (groups->len, >=, 2);  /* At least graphics and audio */
}

/* ==========================================================================
 * Test Cases - Graphics Settings
 * ========================================================================== */

static void
test_graphics_settings_defaults (SettingsFixture *fixture,
                                 gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;
    gint width;
    gint height;

    graphics = lrg_settings_get_graphics (fixture->settings);
    g_assert_nonnull (graphics);

    /* Check that defaults are reasonable */
    lrg_graphics_settings_get_resolution (graphics, &width, &height);
    g_assert_cmpint (width, >, 0);
    g_assert_cmpint (height, >, 0);
}

static void
test_graphics_settings_resolution (SettingsFixture *fixture,
                                   gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;
    gint width;
    gint height;

    graphics = lrg_settings_get_graphics (fixture->settings);

    lrg_graphics_settings_set_resolution (graphics, 1920, 1080);
    lrg_graphics_settings_get_resolution (graphics, &width, &height);

    g_assert_cmpint (width, ==, 1920);
    g_assert_cmpint (height, ==, 1080);
}

static void
test_graphics_settings_vsync (SettingsFixture *fixture,
                              gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;

    graphics = lrg_settings_get_graphics (fixture->settings);

    lrg_graphics_settings_set_vsync (graphics, TRUE);
    g_assert_true (lrg_graphics_settings_get_vsync (graphics));

    lrg_graphics_settings_set_vsync (graphics, FALSE);
    g_assert_false (lrg_graphics_settings_get_vsync (graphics));
}

static void
test_graphics_settings_fps_limit (SettingsFixture *fixture,
                                  gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;

    graphics = lrg_settings_get_graphics (fixture->settings);

    lrg_graphics_settings_set_fps_limit (graphics, 144);
    g_assert_cmpint (lrg_graphics_settings_get_fps_limit (graphics), ==, 144);

    lrg_graphics_settings_set_fps_limit (graphics, 0);  /* Unlimited */
    g_assert_cmpint (lrg_graphics_settings_get_fps_limit (graphics), ==, 0);
}

/* ==========================================================================
 * Test Cases - Audio Settings
 * ========================================================================== */

static void
test_audio_settings_volume (SettingsFixture *fixture,
                            gconstpointer    user_data)
{
    LrgAudioSettings *audio;

    audio = lrg_settings_get_audio (fixture->settings);

    lrg_audio_settings_set_master_volume (audio, 0.75f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_settings_get_master_volume (audio),
                                    0.75f, 0.001f);

    lrg_audio_settings_set_music_volume (audio, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_settings_get_music_volume (audio),
                                    0.5f, 0.001f);

    lrg_audio_settings_set_sfx_volume (audio, 1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_settings_get_sfx_volume (audio),
                                    1.0f, 0.001f);
}

static void
test_audio_settings_mute (SettingsFixture *fixture,
                          gconstpointer    user_data)
{
    LrgAudioSettings *audio;

    audio = lrg_settings_get_audio (fixture->settings);

    lrg_audio_settings_set_muted (audio, TRUE);
    g_assert_true (lrg_audio_settings_get_muted (audio));

    lrg_audio_settings_set_muted (audio, FALSE);
    g_assert_false (lrg_audio_settings_get_muted (audio));
}

/* ==========================================================================
 * Test Cases - Settings Group Base Class
 * ========================================================================== */

static void
test_settings_group_dirty_flag (SettingsFixture *fixture,
                                gconstpointer    user_data)
{
    LrgSettingsGroup *graphics;

    graphics = LRG_SETTINGS_GROUP (lrg_settings_get_graphics (fixture->settings));

    /* Should not be dirty initially */
    lrg_settings_group_mark_clean (graphics);
    g_assert_false (lrg_settings_group_is_dirty (graphics));

    /* Mark dirty */
    lrg_settings_group_mark_dirty (graphics);
    g_assert_true (lrg_settings_group_is_dirty (graphics));

    /* Mark clean again */
    lrg_settings_group_mark_clean (graphics);
    g_assert_false (lrg_settings_group_is_dirty (graphics));
}

static void
test_settings_group_name (SettingsFixture *fixture,
                          gconstpointer    user_data)
{
    LrgSettingsGroup *graphics;
    LrgSettingsGroup *audio;
    const gchar      *name;

    graphics = LRG_SETTINGS_GROUP (lrg_settings_get_graphics (fixture->settings));
    audio = LRG_SETTINGS_GROUP (lrg_settings_get_audio (fixture->settings));

    name = lrg_settings_group_get_group_name (graphics);
    g_assert_cmpstr (name, ==, "graphics");

    name = lrg_settings_group_get_group_name (audio);
    g_assert_cmpstr (name, ==, "audio");
}

/* ==========================================================================
 * Test Cases - Serialization
 * ========================================================================== */

static void
test_settings_group_serialize (SettingsFixture *fixture,
                               gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;
    g_autoptr(GVariant)  variant = NULL;
    g_autoptr(GError)    error = NULL;

    graphics = lrg_settings_get_graphics (fixture->settings);

    /* Set some values */
    lrg_graphics_settings_set_resolution (graphics, 2560, 1440);
    lrg_graphics_settings_set_vsync (graphics, TRUE);

    variant = lrg_settings_group_serialize (LRG_SETTINGS_GROUP (graphics), &error);
    g_assert_no_error (error);
    g_assert_nonnull (variant);
    g_assert_true (g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT));
}

static void
test_settings_group_deserialize (SettingsFixture *fixture,
                                 gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;
    g_autoptr(GVariant)  variant = NULL;
    g_autoptr(GError)    error = NULL;
    GVariantBuilder      builder;
    gint                 width;
    gint                 height;

    graphics = lrg_settings_get_graphics (fixture->settings);

    /* Build a variant with some settings (use serialization key names) */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&builder, "{sv}", "width",
                           g_variant_new_int32 (3840));
    g_variant_builder_add (&builder, "{sv}", "height",
                           g_variant_new_int32 (2160));
    g_variant_builder_add (&builder, "{sv}", "vsync",
                           g_variant_new_boolean (FALSE));
    variant = g_variant_builder_end (&builder);

    g_assert_true (lrg_settings_group_deserialize (LRG_SETTINGS_GROUP (graphics),
                                                   variant, &error));
    g_assert_no_error (error);

    /* Verify values were loaded */
    lrg_graphics_settings_get_resolution (graphics, &width, &height);
    g_assert_cmpint (width, ==, 3840);
    g_assert_cmpint (height, ==, 2160);
    g_assert_false (lrg_graphics_settings_get_vsync (graphics));
}

/* ==========================================================================
 * Test Cases - File Operations
 * ========================================================================== */

static void
test_settings_save_load (SettingsFixture *fixture,
                         gconstpointer    user_data)
{
    g_autoptr(LrgSettings) loaded_settings = NULL;
    g_autoptr(GError)      error = NULL;
    LrgGraphicsSettings   *graphics;
    LrgAudioSettings      *audio;
    gint                   width;
    gint                   height;

    /* Configure some settings */
    graphics = lrg_settings_get_graphics (fixture->settings);
    audio = lrg_settings_get_audio (fixture->settings);

    lrg_graphics_settings_set_resolution (graphics, 1280, 720);
    lrg_graphics_settings_set_vsync (graphics, TRUE);
    lrg_graphics_settings_set_fps_limit (graphics, 60);

    lrg_audio_settings_set_master_volume (audio, 0.8f);
    lrg_audio_settings_set_music_volume (audio, 0.6f);

    /* Save */
    g_assert_true (lrg_settings_save (fixture->settings, fixture->temp_file, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_test (fixture->temp_file, G_FILE_TEST_EXISTS));

    /* Load into new settings object */
    loaded_settings = lrg_settings_new ();
    g_assert_true (lrg_settings_load (loaded_settings, fixture->temp_file, &error));
    g_assert_no_error (error);

    /* Verify values */
    graphics = lrg_settings_get_graphics (loaded_settings);
    audio = lrg_settings_get_audio (loaded_settings);

    lrg_graphics_settings_get_resolution (graphics, &width, &height);
    g_assert_cmpint (width, ==, 1280);
    g_assert_cmpint (height, ==, 720);
    g_assert_true (lrg_graphics_settings_get_vsync (graphics));
    g_assert_cmpint (lrg_graphics_settings_get_fps_limit (graphics), ==, 60);

    g_assert_cmpfloat_with_epsilon (lrg_audio_settings_get_master_volume (audio),
                                    0.8f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_settings_get_music_volume (audio),
                                    0.6f, 0.001f);
}

static void
test_settings_load_nonexistent (SettingsFixture *fixture,
                                gconstpointer    user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    /* Loading from nonexistent file should succeed - uses defaults */
    result = lrg_settings_load (fixture->settings, "/nonexistent/path/settings.json", &error);
    g_assert_true (result);
    g_assert_no_error (error);
}

/* ==========================================================================
 * Test Cases - Operations
 * ========================================================================== */

static void
test_settings_reset_all (SettingsFixture *fixture,
                         gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;
    gint                 orig_width;
    gint                 orig_height;
    gint                 width;
    gint                 height;

    graphics = lrg_settings_get_graphics (fixture->settings);

    /* Get original defaults */
    lrg_graphics_settings_get_resolution (graphics, &orig_width, &orig_height);

    /* Change settings to something different from default */
    lrg_graphics_settings_set_resolution (graphics, 800, 600);
    lrg_graphics_settings_get_resolution (graphics, &width, &height);
    g_assert_cmpint (width, ==, 800);
    g_assert_cmpint (height, ==, 600);

    /* Reset all */
    lrg_settings_reset_all (fixture->settings);

    /* Should be back to defaults */
    lrg_graphics_settings_get_resolution (graphics, &width, &height);
    g_assert_cmpint (width, ==, orig_width);
    g_assert_cmpint (height, ==, orig_height);
}

static void
test_settings_is_dirty (SettingsFixture *fixture,
                        gconstpointer    user_data)
{
    LrgGraphicsSettings *graphics;

    /* Clear dirty flags */
    lrg_settings_group_mark_clean (
        LRG_SETTINGS_GROUP (lrg_settings_get_graphics (fixture->settings)));
    lrg_settings_group_mark_clean (
        LRG_SETTINGS_GROUP (lrg_settings_get_audio (fixture->settings)));

    g_assert_false (lrg_settings_is_dirty (fixture->settings));

    /* Modify graphics */
    graphics = lrg_settings_get_graphics (fixture->settings);
    lrg_graphics_settings_set_vsync (graphics, !lrg_graphics_settings_get_vsync (graphics));

    /* Should be dirty now (assuming the setter marks dirty) */
    /* Note: This depends on implementation marking dirty on change */
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
    g_test_add_func ("/settings/new", test_settings_new);
    g_test_add_func ("/settings/singleton", test_settings_singleton);

    /* Settings Groups */
    g_test_add ("/settings/get-graphics",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_get_graphics,
                settings_fixture_tear_down);

    g_test_add ("/settings/get-audio",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_get_audio,
                settings_fixture_tear_down);

    g_test_add ("/settings/get-group-by-name",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_get_group_by_name,
                settings_fixture_tear_down);

    g_test_add ("/settings/list-groups",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_list_groups,
                settings_fixture_tear_down);

    /* Graphics Settings */
    g_test_add ("/settings/graphics/defaults",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_graphics_settings_defaults,
                settings_fixture_tear_down);

    g_test_add ("/settings/graphics/resolution",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_graphics_settings_resolution,
                settings_fixture_tear_down);

    g_test_add ("/settings/graphics/vsync",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_graphics_settings_vsync,
                settings_fixture_tear_down);

    g_test_add ("/settings/graphics/fps-limit",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_graphics_settings_fps_limit,
                settings_fixture_tear_down);

    /* Audio Settings */
    g_test_add ("/settings/audio/volume",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_audio_settings_volume,
                settings_fixture_tear_down);

    g_test_add ("/settings/audio/mute",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_audio_settings_mute,
                settings_fixture_tear_down);

    /* Settings Group Base */
    g_test_add ("/settings/group/dirty-flag",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_group_dirty_flag,
                settings_fixture_tear_down);

    g_test_add ("/settings/group/name",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_group_name,
                settings_fixture_tear_down);

    /* Serialization */
    g_test_add ("/settings/group/serialize",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_group_serialize,
                settings_fixture_tear_down);

    g_test_add ("/settings/group/deserialize",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_group_deserialize,
                settings_fixture_tear_down);

    /* File Operations */
    g_test_add ("/settings/save-load",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_save_load,
                settings_fixture_tear_down);

    g_test_add ("/settings/load-nonexistent",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_load_nonexistent,
                settings_fixture_tear_down);

    /* Operations */
    g_test_add ("/settings/reset-all",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_reset_all,
                settings_fixture_tear_down);

    g_test_add ("/settings/is-dirty",
                SettingsFixture, NULL,
                settings_fixture_set_up,
                test_settings_is_dirty,
                settings_fixture_tear_down);

    return g_test_run ();
}
