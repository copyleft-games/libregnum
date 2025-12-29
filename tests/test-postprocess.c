/* test-postprocess.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for post-processing module.
 */

#define LIBREGNUM_COMPILATION 1

#include <glib.h>
#include <glib-object.h>
#include "../src/libregnum.h"

/*
 * ============================================================================
 * Fixtures
 * ============================================================================
 */

/* Processor fixture */
typedef struct
{
    LrgPostProcessor *processor;
} ProcessorFixture;

static void
processor_fixture_set_up (ProcessorFixture *fixture,
                          gconstpointer     user_data)
{
    (void) user_data;
    fixture->processor = lrg_post_processor_new (800, 600);
}

static void
processor_fixture_tear_down (ProcessorFixture *fixture,
                             gconstpointer     user_data)
{
    (void) user_data;
    g_clear_object (&fixture->processor);
}

/*
 * ============================================================================
 * LrgPostEffect Tests
 * ============================================================================
 */

static void
test_effect_vignette_new (void)
{
    g_autoptr(LrgVignette) vignette = lrg_vignette_new ();

    g_assert_nonnull (vignette);
    g_assert_true (LRG_IS_VIGNETTE (vignette));
    g_assert_true (LRG_IS_POST_EFFECT (vignette));
}

static void
test_effect_enabled (void)
{
    g_autoptr(LrgVignette) vignette = lrg_vignette_new ();
    LrgPostEffect *effect = LRG_POST_EFFECT (vignette);

    /* Should be enabled by default */
    g_assert_true (lrg_post_effect_is_enabled (effect));

    /* Disable */
    lrg_post_effect_set_enabled (effect, FALSE);
    g_assert_false (lrg_post_effect_is_enabled (effect));

    /* Re-enable */
    lrg_post_effect_set_enabled (effect, TRUE);
    g_assert_true (lrg_post_effect_is_enabled (effect));
}

static void
test_effect_priority (void)
{
    g_autoptr(LrgVignette) vignette = lrg_vignette_new ();
    LrgPostEffect *effect = LRG_POST_EFFECT (vignette);

    /* Set priority */
    lrg_post_effect_set_priority (effect, 100);
    g_assert_cmpint (lrg_post_effect_get_priority (effect), ==, 100);

    /* Change priority */
    lrg_post_effect_set_priority (effect, 50);
    g_assert_cmpint (lrg_post_effect_get_priority (effect), ==, 50);
}

static void
test_effect_intensity (void)
{
    g_autoptr(LrgVignette) vignette = lrg_vignette_new ();
    LrgPostEffect *effect = LRG_POST_EFFECT (vignette);

    /* Set intensity */
    lrg_post_effect_set_intensity (effect, 0.75f);
    g_assert_cmpfloat_with_epsilon (lrg_post_effect_get_intensity (effect), 0.75f, 0.001f);
}

/*
 * ============================================================================
 * LrgPostProcessor Tests
 * ============================================================================
 */

static void
test_processor_new (ProcessorFixture *fixture,
                    gconstpointer     user_data)
{
    (void) user_data;

    g_assert_nonnull (fixture->processor);
    g_assert_true (LRG_IS_POST_PROCESSOR (fixture->processor));
    g_assert_cmpuint (lrg_post_processor_get_width (fixture->processor), ==, 800);
    g_assert_cmpuint (lrg_post_processor_get_height (fixture->processor), ==, 600);
}

static void
test_processor_add_effect (ProcessorFixture *fixture,
                           gconstpointer     user_data)
{
    g_autoptr(LrgVignette) vignette = NULL;

    (void) user_data;
    vignette = lrg_vignette_new ();

    g_assert_cmpuint (lrg_post_processor_get_effect_count (fixture->processor), ==, 0);

    lrg_post_processor_add_effect (fixture->processor, LRG_POST_EFFECT (vignette));
    g_assert_cmpuint (lrg_post_processor_get_effect_count (fixture->processor), ==, 1);
}

static void
test_processor_remove_effect (ProcessorFixture *fixture,
                              gconstpointer     user_data)
{
    g_autoptr(LrgVignette) vignette = NULL;

    (void) user_data;
    vignette = lrg_vignette_new ();

    lrg_post_processor_add_effect (fixture->processor, LRG_POST_EFFECT (vignette));
    g_assert_cmpuint (lrg_post_processor_get_effect_count (fixture->processor), ==, 1);

    lrg_post_processor_remove_effect (fixture->processor, LRG_POST_EFFECT (vignette));
    g_assert_cmpuint (lrg_post_processor_get_effect_count (fixture->processor), ==, 0);
}

static void
test_processor_clear_effects (ProcessorFixture *fixture,
                              gconstpointer     user_data)
{
    g_autoptr(LrgVignette) vignette = NULL;
    g_autoptr(LrgBloom) bloom = NULL;

    (void) user_data;
    vignette = lrg_vignette_new ();
    bloom = lrg_bloom_new ();

    lrg_post_processor_add_effect (fixture->processor, LRG_POST_EFFECT (vignette));
    lrg_post_processor_add_effect (fixture->processor, LRG_POST_EFFECT (bloom));
    g_assert_cmpuint (lrg_post_processor_get_effect_count (fixture->processor), ==, 2);

    lrg_post_processor_clear_effects (fixture->processor);
    g_assert_cmpuint (lrg_post_processor_get_effect_count (fixture->processor), ==, 0);
}

static void
test_processor_effect_order (ProcessorFixture *fixture,
                             gconstpointer     user_data)
{
    g_autoptr(LrgVignette) vignette = NULL;
    g_autoptr(LrgBloom) bloom = NULL;
    GList *effects;

    (void) user_data;
    vignette = lrg_vignette_new ();
    bloom = lrg_bloom_new ();

    /* Set priorities (lower = applied first) */
    lrg_post_effect_set_priority (LRG_POST_EFFECT (vignette), 100);
    lrg_post_effect_set_priority (LRG_POST_EFFECT (bloom), 50);

    /* Add in wrong order */
    lrg_post_processor_add_effect (fixture->processor, LRG_POST_EFFECT (vignette));
    lrg_post_processor_add_effect (fixture->processor, LRG_POST_EFFECT (bloom));

    /* Sort by priority */
    lrg_post_processor_sort_effects (fixture->processor);

    /* Verify order (bloom should be first due to lower priority) */
    effects = lrg_post_processor_get_effects (fixture->processor);
    g_assert_nonnull (effects);
    g_assert_true (LRG_IS_BLOOM (effects->data));
}

static void
test_processor_resize (ProcessorFixture *fixture,
                       gconstpointer     user_data)
{
    (void) user_data;

    lrg_post_processor_resize (fixture->processor, 1920, 1080);
    g_assert_cmpuint (lrg_post_processor_get_width (fixture->processor), ==, 1920);
    g_assert_cmpuint (lrg_post_processor_get_height (fixture->processor), ==, 1080);
}

static void
test_processor_enabled (ProcessorFixture *fixture,
                        gconstpointer     user_data)
{
    (void) user_data;

    /* Should be enabled by default */
    g_assert_true (lrg_post_processor_is_enabled (fixture->processor));

    /* Disable */
    lrg_post_processor_set_enabled (fixture->processor, FALSE);
    g_assert_false (lrg_post_processor_is_enabled (fixture->processor));
}

/*
 * ============================================================================
 * LrgVignette Tests
 * ============================================================================
 */

static void
test_vignette_properties (void)
{
    g_autoptr(LrgVignette) vignette = lrg_vignette_new ();

    /* Intensity */
    lrg_vignette_set_intensity (vignette, 0.8f);
    g_assert_cmpfloat_with_epsilon (lrg_vignette_get_intensity (vignette), 0.8f, 0.001f);

    /* Radius */
    lrg_vignette_set_radius (vignette, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_vignette_get_radius (vignette), 0.5f, 0.001f);

    /* Smoothness */
    lrg_vignette_set_smoothness (vignette, 0.3f);
    g_assert_cmpfloat_with_epsilon (lrg_vignette_get_smoothness (vignette), 0.3f, 0.001f);

    /* Roundness */
    lrg_vignette_set_roundness (vignette, 0.9f);
    g_assert_cmpfloat_with_epsilon (lrg_vignette_get_roundness (vignette), 0.9f, 0.001f);
}

static void
test_vignette_color (void)
{
    g_autoptr(LrgVignette) vignette = lrg_vignette_new ();
    gfloat r, g, b;

    /* Set color */
    lrg_vignette_set_color (vignette, 0.1f, 0.0f, 0.2f);
    lrg_vignette_get_color (vignette, &r, &g, &b);

    g_assert_cmpfloat_with_epsilon (r, 0.1f, 0.001f);
    g_assert_cmpfloat_with_epsilon (g, 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (b, 0.2f, 0.001f);
}

/*
 * ============================================================================
 * LrgBloom Tests
 * ============================================================================
 */

static void
test_bloom_new (void)
{
    g_autoptr(LrgBloom) bloom = lrg_bloom_new ();

    g_assert_nonnull (bloom);
    g_assert_true (LRG_IS_BLOOM (bloom));
}

static void
test_bloom_properties (void)
{
    g_autoptr(LrgBloom) bloom = lrg_bloom_new ();

    /* Threshold */
    lrg_bloom_set_threshold (bloom, 1.5f);
    g_assert_cmpfloat_with_epsilon (lrg_bloom_get_threshold (bloom), 1.5f, 0.001f);

    /* Intensity */
    lrg_bloom_set_intensity (bloom, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_bloom_get_intensity (bloom), 2.0f, 0.001f);

    /* Blur size */
    lrg_bloom_set_blur_size (bloom, 8.0f);
    g_assert_cmpfloat_with_epsilon (lrg_bloom_get_blur_size (bloom), 8.0f, 0.001f);

    /* Iterations */
    lrg_bloom_set_iterations (bloom, 4);
    g_assert_cmpuint (lrg_bloom_get_iterations (bloom), ==, 4);

    /* Soft knee */
    lrg_bloom_set_soft_knee (bloom, 0.6f);
    g_assert_cmpfloat_with_epsilon (lrg_bloom_get_soft_knee (bloom), 0.6f, 0.001f);
}

static void
test_bloom_tint (void)
{
    g_autoptr(LrgBloom) bloom = lrg_bloom_new ();
    gfloat r, g, b;

    /* Set warm tint */
    lrg_bloom_set_tint (bloom, 1.0f, 0.9f, 0.8f);
    lrg_bloom_get_tint (bloom, &r, &g, &b);

    g_assert_cmpfloat_with_epsilon (r, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (g, 0.9f, 0.001f);
    g_assert_cmpfloat_with_epsilon (b, 0.8f, 0.001f);
}

/*
 * ============================================================================
 * LrgScreenShake Tests
 * ============================================================================
 */

static void
test_screen_shake_new (void)
{
    g_autoptr(LrgScreenShake) shake = lrg_screen_shake_new ();

    g_assert_nonnull (shake);
    g_assert_true (LRG_IS_SCREEN_SHAKE (shake));
}

static void
test_screen_shake_trauma (void)
{
    g_autoptr(LrgScreenShake) shake = lrg_screen_shake_new ();

    /* Initial trauma should be 0 */
    g_assert_cmpfloat_with_epsilon (lrg_screen_shake_get_trauma (shake), 0.0f, 0.001f);

    /* Add trauma */
    lrg_screen_shake_add_trauma (shake, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_screen_shake_get_trauma (shake), 0.5f, 0.001f);

    /* Add more (should stack) */
    lrg_screen_shake_add_trauma (shake, 0.3f);
    g_assert_cmpfloat_with_epsilon (lrg_screen_shake_get_trauma (shake), 0.8f, 0.001f);

    /* Set directly */
    lrg_screen_shake_set_trauma (shake, 0.25f);
    g_assert_cmpfloat_with_epsilon (lrg_screen_shake_get_trauma (shake), 0.25f, 0.001f);
}

static void
test_screen_shake_update (void)
{
    g_autoptr(LrgScreenShake) shake = lrg_screen_shake_new ();

    /* Set decay rate */
    lrg_screen_shake_set_decay (shake, 1.0f);

    /* Add trauma */
    lrg_screen_shake_set_trauma (shake, 1.0f);

    /* Update (should decay) */
    lrg_screen_shake_update (shake, 0.5f);
    g_assert_cmpfloat (lrg_screen_shake_get_trauma (shake), <, 1.0f);
}

static void
test_screen_shake_offset (void)
{
    g_autoptr(LrgScreenShake) shake = lrg_screen_shake_new ();
    gfloat x, y;

    /* Set max offset */
    lrg_screen_shake_set_max_offset (shake, 20.0f, 15.0f);
    lrg_screen_shake_get_max_offset (shake, &x, &y);

    g_assert_cmpfloat_with_epsilon (x, 20.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (y, 15.0f, 0.001f);
}

/*
 * ============================================================================
 * LrgColorblindFilter Tests
 * ============================================================================
 */

static void
test_colorblind_filter_new (void)
{
    g_autoptr(LrgColorblindFilter) filter = lrg_colorblind_filter_new ();

    g_assert_nonnull (filter);
    g_assert_true (LRG_IS_COLORBLIND_FILTER (filter));
}

static void
test_colorblind_filter_types (void)
{
    g_autoptr(LrgColorblindFilter) filter = lrg_colorblind_filter_new ();

    /* Deuteranopia */
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_DEUTERANOPIA);
    g_assert_cmpint (lrg_colorblind_filter_get_filter_type (filter), ==, LRG_COLORBLIND_DEUTERANOPIA);

    /* Protanopia */
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_PROTANOPIA);
    g_assert_cmpint (lrg_colorblind_filter_get_filter_type (filter), ==, LRG_COLORBLIND_PROTANOPIA);

    /* Tritanopia */
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_TRITANOPIA);
    g_assert_cmpint (lrg_colorblind_filter_get_filter_type (filter), ==, LRG_COLORBLIND_TRITANOPIA);
}

static void
test_colorblind_filter_mode (void)
{
    g_autoptr(LrgColorblindFilter) filter = lrg_colorblind_filter_new ();

    /* Simulate mode */
    lrg_colorblind_filter_set_mode (filter, LRG_COLORBLIND_MODE_SIMULATE);
    g_assert_cmpint (lrg_colorblind_filter_get_mode (filter), ==, LRG_COLORBLIND_MODE_SIMULATE);

    /* Correct mode */
    lrg_colorblind_filter_set_mode (filter, LRG_COLORBLIND_MODE_CORRECT);
    g_assert_cmpint (lrg_colorblind_filter_get_mode (filter), ==, LRG_COLORBLIND_MODE_CORRECT);
}

static void
test_colorblind_filter_strength (void)
{
    g_autoptr(LrgColorblindFilter) filter = lrg_colorblind_filter_new ();

    lrg_colorblind_filter_set_strength (filter, 0.75f);
    g_assert_cmpfloat_with_epsilon (lrg_colorblind_filter_get_strength (filter), 0.75f, 0.001f);
}

/*
 * ============================================================================
 * LrgFxaa Tests
 * ============================================================================
 */

static void
test_fxaa_new (void)
{
    g_autoptr(LrgFxaa) fxaa = lrg_fxaa_new ();

    g_assert_nonnull (fxaa);
    g_assert_true (LRG_IS_FXAA (fxaa));
}

static void
test_fxaa_properties (void)
{
    g_autoptr(LrgFxaa) fxaa = lrg_fxaa_new ();

    /* Subpixel quality */
    lrg_fxaa_set_subpixel_quality (fxaa, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_fxaa_get_subpixel_quality (fxaa), 0.5f, 0.001f);

    /* Edge threshold */
    lrg_fxaa_set_edge_threshold (fxaa, 0.15f);
    g_assert_cmpfloat_with_epsilon (lrg_fxaa_get_edge_threshold (fxaa), 0.15f, 0.001f);

    /* Edge threshold min */
    lrg_fxaa_set_edge_threshold_min (fxaa, 0.05f);
    g_assert_cmpfloat_with_epsilon (lrg_fxaa_get_edge_threshold_min (fxaa), 0.05f, 0.001f);
}

static void
test_fxaa_quality (void)
{
    g_autoptr(LrgFxaa) fxaa = lrg_fxaa_new ();

    /* Set different quality levels */
    lrg_fxaa_set_quality (fxaa, LRG_FXAA_QUALITY_LOW);
    g_assert_cmpint (lrg_fxaa_get_quality (fxaa), ==, LRG_FXAA_QUALITY_LOW);

    lrg_fxaa_set_quality (fxaa, LRG_FXAA_QUALITY_MEDIUM);
    g_assert_cmpint (lrg_fxaa_get_quality (fxaa), ==, LRG_FXAA_QUALITY_MEDIUM);

    lrg_fxaa_set_quality (fxaa, LRG_FXAA_QUALITY_HIGH);
    g_assert_cmpint (lrg_fxaa_get_quality (fxaa), ==, LRG_FXAA_QUALITY_HIGH);
}

/*
 * ============================================================================
 * LrgFilmGrain Tests
 * ============================================================================
 */

static void
test_film_grain_new (void)
{
    g_autoptr(LrgFilmGrain) grain = lrg_film_grain_new ();

    g_assert_nonnull (grain);
    g_assert_true (LRG_IS_FILM_GRAIN (grain));
}

static void
test_film_grain_properties (void)
{
    g_autoptr(LrgFilmGrain) grain = lrg_film_grain_new ();

    /* Intensity */
    lrg_film_grain_set_intensity (grain, 0.3f);
    g_assert_cmpfloat_with_epsilon (lrg_film_grain_get_intensity (grain), 0.3f, 0.001f);

    /* Size */
    lrg_film_grain_set_size (grain, 2.5f);
    g_assert_cmpfloat_with_epsilon (lrg_film_grain_get_size (grain), 2.5f, 0.001f);

    /* Speed */
    lrg_film_grain_set_speed (grain, 1.5f);
    g_assert_cmpfloat_with_epsilon (lrg_film_grain_get_speed (grain), 1.5f, 0.001f);

    /* Colored */
    lrg_film_grain_set_colored (grain, TRUE);
    g_assert_true (lrg_film_grain_get_colored (grain));

    lrg_film_grain_set_colored (grain, FALSE);
    g_assert_false (lrg_film_grain_get_colored (grain));

    /* Luminance response */
    lrg_film_grain_set_luminance_response (grain, 0.7f);
    g_assert_cmpfloat_with_epsilon (lrg_film_grain_get_luminance_response (grain), 0.7f, 0.001f);
}

/*
 * ============================================================================
 * Main
 * ============================================================================
 */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgPostEffect tests */
    g_test_add_func ("/postprocess/effect/vignette/new", test_effect_vignette_new);
    g_test_add_func ("/postprocess/effect/enabled", test_effect_enabled);
    g_test_add_func ("/postprocess/effect/priority", test_effect_priority);
    g_test_add_func ("/postprocess/effect/intensity", test_effect_intensity);

    /* LrgPostProcessor tests */
    g_test_add ("/postprocess/processor/new", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_new, processor_fixture_tear_down);
    g_test_add ("/postprocess/processor/add-effect", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_add_effect, processor_fixture_tear_down);
    g_test_add ("/postprocess/processor/remove-effect", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_remove_effect, processor_fixture_tear_down);
    g_test_add ("/postprocess/processor/clear-effects", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_clear_effects, processor_fixture_tear_down);
    g_test_add ("/postprocess/processor/effect-order", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_effect_order, processor_fixture_tear_down);
    g_test_add ("/postprocess/processor/resize", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_resize, processor_fixture_tear_down);
    g_test_add ("/postprocess/processor/enabled", ProcessorFixture, NULL,
                processor_fixture_set_up, test_processor_enabled, processor_fixture_tear_down);

    /* LrgVignette tests */
    g_test_add_func ("/postprocess/vignette/properties", test_vignette_properties);
    g_test_add_func ("/postprocess/vignette/color", test_vignette_color);

    /* LrgBloom tests */
    g_test_add_func ("/postprocess/bloom/new", test_bloom_new);
    g_test_add_func ("/postprocess/bloom/properties", test_bloom_properties);
    g_test_add_func ("/postprocess/bloom/tint", test_bloom_tint);

    /* LrgScreenShake tests */
    g_test_add_func ("/postprocess/screen-shake/new", test_screen_shake_new);
    g_test_add_func ("/postprocess/screen-shake/trauma", test_screen_shake_trauma);
    g_test_add_func ("/postprocess/screen-shake/update", test_screen_shake_update);
    g_test_add_func ("/postprocess/screen-shake/offset", test_screen_shake_offset);

    /* LrgColorblindFilter tests */
    g_test_add_func ("/postprocess/colorblind/new", test_colorblind_filter_new);
    g_test_add_func ("/postprocess/colorblind/types", test_colorblind_filter_types);
    g_test_add_func ("/postprocess/colorblind/mode", test_colorblind_filter_mode);
    g_test_add_func ("/postprocess/colorblind/strength", test_colorblind_filter_strength);

    /* LrgFxaa tests */
    g_test_add_func ("/postprocess/fxaa/new", test_fxaa_new);
    g_test_add_func ("/postprocess/fxaa/properties", test_fxaa_properties);
    g_test_add_func ("/postprocess/fxaa/quality", test_fxaa_quality);

    /* LrgFilmGrain tests */
    g_test_add_func ("/postprocess/film-grain/new", test_film_grain_new);
    g_test_add_func ("/postprocess/film-grain/properties", test_film_grain_properties);

    return g_test_run ();
}
