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
#include <string.h>
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
 * LrgColorGrade
 * ============================================================================
 */

static void
assert_rgb (const gfloat *c,
            gfloat        r,
            gfloat        g,
            gfloat        b)
{
    g_assert_cmpfloat_with_epsilon (c[0], r, 0.002f);
    g_assert_cmpfloat_with_epsilon (c[1], g, 0.002f);
    g_assert_cmpfloat_with_epsilon (c[2], b, 0.002f);
}

static void
test_color_grade_identity (void)
{
    g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();
    const gfloat samples[4][3] = {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.25f, 0.5f, 0.75f }, { 0.9f, 0.1f, 0.4f }
    };
    gfloat out[3];
    guint i;

    /* a new grade changes nothing */
    g_assert_true (lrg_color_grade_is_identity (grade));
    for (i = 0; i < G_N_ELEMENTS (samples); i++)
    {
        lrg_color_grade_apply_rgb (grade, samples[i], out);
        assert_rgb (out, samples[i][0], samples[i][1], samples[i][2]);
    }

    /* any parameter breaks the identity, an intensity of 0 restores it */
    lrg_color_grade_set_saturation (grade, 1.2f);
    g_assert_false (lrg_color_grade_is_identity (grade));
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (grade), 0.0f);
    g_assert_true (lrg_color_grade_is_identity (grade));
    lrg_color_grade_apply_rgb (grade, samples[3], out);
    assert_rgb (out, 0.9f, 0.1f, 0.4f);
}

static void
test_color_grade_mapping (void)
{
    g_autoptr(LrgColorGrade) grade = NULL;
    const gfloat grey[3] = { 0.25f, 0.25f, 0.25f };
    const gfloat colour[3] = { 0.8f, 0.2f, 0.2f };
    gfloat out[3];
    gfloat lum;

    /* exposure: one stop doubles, clamped at white */
    grade = lrg_color_grade_new ();
    lrg_color_grade_set_exposure (grade, 1.0f);
    lrg_color_grade_apply_rgb (grade, grey, out);
    assert_rgb (out, 0.5f, 0.5f, 0.5f);
    lrg_color_grade_set_exposure (grade, 3.0f);
    lrg_color_grade_apply_rgb (grade, grey, out);
    assert_rgb (out, 1.0f, 1.0f, 1.0f);
    g_clear_object (&grade);

    /* gamma 2 lifts the midtones: 0.25 -> 0.5; black and white stay */
    grade = lrg_color_grade_new ();
    lrg_color_grade_set_gamma (grade, 2.0f, 2.0f, 2.0f);
    lrg_color_grade_apply_rgb (grade, grey, out);
    assert_rgb (out, 0.5f, 0.5f, 0.5f);
    g_clear_object (&grade);

    /* saturation 0 gives the Rec. 709 luminance */
    grade = lrg_color_grade_new ();
    lrg_color_grade_set_saturation (grade, 0.0f);
    lrg_color_grade_apply_rgb (grade, colour, out);
    lum = 0.8f * 0.2126f + 0.2f * 0.7152f + 0.2f * 0.0722f;
    assert_rgb (out, lum, lum, lum);
    g_clear_object (&grade);

    /* warmth: red up, blue down by a fifth at full temperature */
    grade = lrg_color_grade_new ();
    lrg_color_grade_set_temperature (grade, 1.0f);
    lrg_color_grade_apply_rgb (grade, grey, out);
    assert_rgb (out, 0.3f, 0.25f, 0.2f);
    g_clear_object (&grade);

    /* half intensity is halfway between the original and the grade */
    grade = lrg_color_grade_new ();
    lrg_color_grade_set_exposure (grade, 1.0f);
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (grade), 0.5f);
    lrg_color_grade_apply_rgb (grade, grey, out);
    assert_rgb (out, 0.375f, 0.375f, 0.375f);
}

static void
test_color_grade_ranges (void)
{
    g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();
    gfloat r, g, b;

    /* gamma never reaches 0 (it divides), gain may exceed 1 */
    lrg_color_grade_set_gamma (grade, 0.0f, 2.2f, 9.0f);
    lrg_color_grade_get_gamma (grade, &r, &g, &b);
    g_assert_cmpfloat_with_epsilon (r, 0.1f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (g, 2.2f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (b, 4.0f, 0.0001f);
    lrg_color_grade_set_gain (grade, -1.0f, 1.5f, 1.0f);
    lrg_color_grade_get_gain (grade, &r, &g, &b);
    g_assert_cmpfloat_with_epsilon (r, 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (g, 1.5f, 0.0001f);

    /* the shader declares every uniform the grade uploads */
    g_assert_nonnull (strstr (lrg_color_grade_get_fragment_shader (), "uniform float exposure"));
    g_assert_nonnull (strstr (lrg_color_grade_get_fragment_shader (), "uniform vec3 gamma"));
    g_assert_nonnull (strstr (lrg_color_grade_get_fragment_shader (), "uniform float intensity"));
    /* headless: no context, no shader (drawing falls back to plain) */
    g_assert_null (lrg_color_grade_get_shader (grade));
}

/*
 * ============================================================================
 * Main
 * ============================================================================
 */

/*
 * ============================================================================
 * M14: colour lookup tables
 * ============================================================================
 */

/* a 2-sided table that swaps red and blue */
static const gchar swap_cube[] =
    "# a comment\n"
    "TITLE \"swap red and blue\"\n"
    "LUT_3D_SIZE 2\n"
    "LUT_IN_VIDEO_RANGE\n"
    "\n"
    "0 0 0\n"
    "0 0 1\n"     /* r = 1, g = 0, b = 0 -> blue */
    "0 1 0\n"
    "0 1 1\n"
    "1 0 0\n"     /* r = 0, g = 0, b = 1 -> red */
    "1 0 1\n"
    "1 1 0\n"
    "1 1 1\r\n";

static void
test_color_lut_parse (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgColorLut) lut = NULL;
    g_autoptr(LrgColorLut) back = NULL;
    g_autofree gchar *text = NULL;
    gfloat in[3] = { 0.8f, 0.3f, 0.1f };
    gfloat out[3];
    gfloat e[3];

    lut = lrg_color_lut_new_from_cube (swap_cube, -1, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_color_lut_get_size (lut), ==, 2);
    g_assert_cmpstr (lrg_color_lut_get_title (lut), ==, "swap red and blue");
    g_assert_true (lrg_color_lut_get_entry (lut, 1, 0, 0, e));
    g_assert_cmpfloat (e[2], ==, 1.0f);
    g_assert_cmpfloat (e[0], ==, 0.0f);
    g_assert_false (lrg_color_lut_get_entry (lut, 2, 0, 0, e));
    g_assert_false (lrg_color_lut_is_identity (lut, 0.01f));

    /* trilinear on a 2-sided linear table is exact */
    lrg_color_lut_sample (lut, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.1f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[1], 0.3f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[2], 0.8f, 1e-5);
    lrg_color_lut_apply_rgb (lut, in, 0.5f, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.45f, 1e-5);

    /* the writer round-trips */
    text = lrg_color_lut_to_cube (lut);
    g_assert_nonnull (strstr (text, "LUT_3D_SIZE 2"));
    back = lrg_color_lut_new_from_cube (text, -1, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_color_lut_get_title (back), ==, "swap red and blue");
    lrg_color_lut_sample (back, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.1f, 1e-5);

    /* inputs outside 0 .. 1 are clamped */
    in[0] = 2.0f;
    in[1] = -1.0f;
    lrg_color_lut_sample (lut, in, out);
    g_assert_cmpfloat_with_epsilon (out[2], 1.0f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[1], 0.0f, 1e-5);
}

static void
test_color_lut_identity_mix (void)
{
    g_autoptr(LrgColorLut) id = lrg_color_lut_new_identity (17);
    g_autoptr(LrgColorLut) swap = lrg_color_lut_new_from_cube (swap_cube, -1, NULL);
    g_autoptr(LrgColorLut) half = NULL;
    g_autoptr(LrgColorLut) big = NULL;
    g_autoptr(GBytes) strip = NULL;
    gfloat in[3] = { 0.37f, 0.62f, 0.05f };
    gfloat out[3];
    const guint8 *px;
    guint w;
    guint h;

    g_assert_true (lrg_color_lut_is_identity (id, 1e-6f));
    lrg_color_lut_sample (id, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.37f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[1], 0.62f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[2], 0.05f, 1e-5);

    /* half way between the identity and the swap, at the identity's size */
    half = lrg_color_lut_new_mix (id, swap, 0.5f);
    g_assert_cmpuint (lrg_color_lut_get_size (half), ==, 17);
    lrg_color_lut_sample (half, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], (0.37f + 0.05f) / 2.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[2], (0.05f + 0.37f) / 2.0f, 1e-4);

    /* resampling keeps a linear table exact */
    big = lrg_color_lut_new_resampled (swap, 9);
    lrg_color_lut_sample (big, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.05f, 1e-5);

    /* the GPU strip: blue slices side by side, red across, green down */
    strip = lrg_color_lut_to_strip (swap, &w, &h);
    g_assert_cmpuint (w, ==, 4);
    g_assert_cmpuint (h, ==, 2);
    px = g_bytes_get_data (strip, NULL);
    /* texel (r = 1, b = 0) of row g = 0 is entry (1, 0, 0): blue */
    g_assert_cmpuint (px[(0 * 4 + 0 * 2 + 1) * 4 + 2], ==, 255);
    g_assert_cmpuint (px[(0 * 4 + 0 * 2 + 1) * 4 + 0], ==, 0);
    /* texel (r = 0, b = 1) of row g = 1 is entry (0, 1, 1): r = 1, g = 1 */
    g_assert_cmpuint (px[(1 * 4 + 1 * 2 + 0) * 4 + 0], ==, 255);
    g_assert_cmpuint (px[(1 * 4 + 1 * 2 + 0) * 4 + 1], ==, 255);
    g_assert_cmpuint (px[(1 * 4 + 1 * 2 + 0) * 4 + 2], ==, 0);
}

static void
test_color_lut_1d_domain (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgColorLut) inv = NULL;
    g_autoptr(LrgColorLut) dom = NULL;
    gfloat in[3] = { 0.25f, 0.5f, 1.0f };
    gfloat out[3];

    /* a 1D table inverting each channel */
    inv = lrg_color_lut_new_from_cube ("LUT_1D_SIZE 3\n1 1 1\n0.5 0.5 0.5\n0 0 0\n", -1, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_color_lut_get_size (inv), ==, 33);
    lrg_color_lut_sample (inv, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.75f, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[1], 0.5f, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[2], 0.0f, 1e-4);

    /* an identity over the domain 0 .. 2: inputs 0 .. 1 read its lower half */
    dom = lrg_color_lut_new_from_cube ("LUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n"
                                       "0 0 0\n2 0 0\n0 2 0\n2 2 0\n0 0 2\n2 0 2\n0 2 2\n2 2 2\n",
                                       -1, &error);
    g_assert_no_error (error);
    lrg_color_lut_sample (dom, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.25f, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[1], 0.5f, 1e-4);
    g_assert_cmpfloat_with_epsilon (out[2], 1.0f, 1e-4);
}

static void
test_color_lut_errors (void)
{
    static const struct
    {
        const gchar *text;
        gint         code;
    } bad[] = {
        { "0 0 0\n1 1 1\n", LRG_COLOR_LUT_ERROR_SIZE },
        { "LUT_3D_SIZE 2\n0 0 0\n", LRG_COLOR_LUT_ERROR_SIZE },
        { "LUT_3D_SIZE 1\n0 0 0\n", LRG_COLOR_LUT_ERROR_SIZE },
        { "LUT_3D_SIZE 65\n", LRG_COLOR_LUT_ERROR_SIZE },
        { "LUT_3D_SIZE\n", LRG_COLOR_LUT_ERROR_SIZE },
        { "LUT_1D_SIZE 2\n0 0\n1 1 1\n", LRG_COLOR_LUT_ERROR_PARSE },
        { "LUT_1D_SIZE 2\n0 0 0\n1 x 1\n", LRG_COLOR_LUT_ERROR_PARSE },
        { "LUT_1D_SIZE 2\n0 0 0 0\n1 1 1\n", LRG_COLOR_LUT_ERROR_PARSE },
        { "LUT_1D_SIZE 2\nDOMAIN_MIN 1 1 1\nDOMAIN_MAX 1 1 1\n0 0 0\n1 1 1\n",
          LRG_COLOR_LUT_ERROR_DOMAIN },
        { "LUT_1D_SIZE 2\nDOMAIN_MIN 0 0\n0 0 0\n1 1 1\n", LRG_COLOR_LUT_ERROR_PARSE },
        { "LUT_1D_SIZE 2\n0 0 nan\n1 1 1\n", LRG_COLOR_LUT_ERROR_PARSE }
    };
    g_autoptr(GError) error = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (bad); i++)
    {
        g_autoptr(GError) e = NULL;
        LrgColorLut *lut = lrg_color_lut_new_from_cube (bad[i].text, -1, &e);

        g_assert_null (lut);
        g_assert_error (e, LRG_COLOR_LUT_ERROR, bad[i].code);
    }
    g_assert_null (lrg_color_lut_new_from_file ("/nonexistent/dir/x.cube", &error));
    g_assert_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_IO);
}

static void
test_color_grade_lut (void)
{
    g_autoptr(LrgColorGrade) grade = lrg_color_grade_new ();
    g_autoptr(LrgColorLut) swap = lrg_color_lut_new_from_cube (swap_cube, -1, NULL);
    g_autoptr(LrgColorLut) baked = NULL;
    gfloat in[3] = { 0.8f, 0.3f, 0.1f };
    gfloat out[3];
    gfloat strength;

    g_assert_true (lrg_color_grade_is_identity (grade));
    lrg_color_grade_set_lut (grade, swap, 1.0f);
    g_assert_true (lrg_color_grade_get_lut (grade, &strength) == swap);
    g_assert_cmpfloat (strength, ==, 1.0f);
    g_assert_false (lrg_color_grade_is_identity (grade));
    lrg_color_grade_apply_rgb (grade, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], 0.1f, 1e-5);
    g_assert_cmpfloat_with_epsilon (out[2], 0.8f, 1e-5);

    /* the table follows the grade's own steps */
    lrg_color_grade_set_saturation (grade, 0.0f);
    lrg_color_grade_apply_rgb (grade, in, out);
    g_assert_cmpfloat_with_epsilon (out[0], out[2], 1e-5);

    /* baking samples the whole grade (with its table) */
    baked = lrg_color_grade_bake_lut (grade, 17);
    {
        gfloat a[3];
        gfloat b[3];
        gfloat p[3] = { 0.5f, 0.25f, 0.75f };

        lrg_color_grade_apply_rgb (grade, p, a);
        lrg_color_lut_sample (baked, p, b);
        g_assert_cmpfloat_with_epsilon (a[0], b[0], 1e-3);
        g_assert_cmpfloat_with_epsilon (a[1], b[1], 1e-3);
    }

    /* strength 0 and no table are the identity again */
    lrg_color_grade_set_saturation (grade, 1.0f);
    lrg_color_grade_set_lut (grade, swap, 0.0f);
    g_assert_true (lrg_color_grade_is_identity (grade));
    lrg_color_grade_set_lut (grade, NULL, 1.0f);
    g_assert_null (lrg_color_grade_get_lut (grade, &strength));
    g_assert_cmpfloat (strength, ==, 0.0f);
}

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

    /* LrgColorGrade tests */
    g_test_add_func ("/postprocess/color-grade/identity", test_color_grade_identity);
    g_test_add_func ("/postprocess/color-grade/mapping", test_color_grade_mapping);
    g_test_add_func ("/postprocess/color-grade/ranges", test_color_grade_ranges);
    g_test_add_func ("/postprocess/color-lut/parse", test_color_lut_parse);
    g_test_add_func ("/postprocess/color-lut/identity-mix", test_color_lut_identity_mix);
    g_test_add_func ("/postprocess/color-lut/1d-domain", test_color_lut_1d_domain);
    g_test_add_func ("/postprocess/color-lut/errors", test_color_lut_errors);
    g_test_add_func ("/postprocess/color-grade/lut", test_color_grade_lut);

    return g_test_run ();
}
